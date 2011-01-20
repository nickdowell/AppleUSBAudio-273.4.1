/*
 * Copyright (c) 1998-2010 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
 
//--------------------------------------------------------------------------------
//
//	File:		AppleUSBAudioStream.cpp
//
//	Contains:	Support for the USB Audio Class Stream Interface.
//			This includes support for setting sample rate (via
//			a sample rate endpoint control and appropriate
//			sized construction of USB isochronous frame lists),
//			channel depth selection and bit depth selection.
//
//	Technology:	Mac OS X
//
//--------------------------------------------------------------------------------

#include "AppleUSBAudioStream.h"
#include "AppleUSBAudioEngine.h"
#include "AppleUSBAudioPlugin.h"

#define super IOAudioStream

OSDefineMetaClassAndStructors(AppleUSBAudioStream, IOAudioStream)

#pragma mark -IOKit Routines-

void AppleUSBAudioStream::free () {
	UInt32			i;

	debugIOLog ("+ AppleUSBAudioStream[%p]::free ()", this);

	if (NULL != mCoalescenceMutex)
	{
		IORecursiveLockFree (mCoalescenceMutex);	// <rdar://problem/7378275>
		mCoalescenceMutex = NULL;
	}

	if (NULL != mFrameQueuedForList) 
	{
		delete [] mFrameQueuedForList;
		mFrameQueuedForList = NULL;
	}

	if (mAverageSampleRateBuffer) 
	{
		// <rdar://7000283> Pointer was obtained from mAssociatedEndpointMemoryDescriptor, so no need to free it
		// explicitly here.
		mAverageSampleRateBuffer = NULL;
	}

	if (mAssociatedEndpointMemoryDescriptor) 
	{
		mAssociatedEndpointMemoryDescriptor->release ();
		mAssociatedEndpointMemoryDescriptor = NULL;
	}

	if (mUSBBufferDescriptor) 
	{
		mUSBBufferDescriptor->release ();
		mUSBBufferDescriptor = NULL;
	}

	if (mWrapRangeDescriptor) 
	{
		mWrapRangeDescriptor->release ();
		mWrapDescriptors[0]->release ();
		mWrapDescriptors[1]->release ();
		mWrapRangeDescriptor = NULL;
	}

	if (mSampleBufferMemoryDescriptor) 
	{
		mSampleBufferMemoryDescriptor->release ();
		mSampleBufferMemoryDescriptor = NULL;
	}

	if (NULL != mSampleBufferDescriptors) 
	{
		for (i = 0; i < mNumUSBFrameLists; i++) 
		{
			if (NULL != mSampleBufferDescriptors[i]) 
			{
				mSampleBufferDescriptors[i]->release ();
				mSampleBufferDescriptors[i] = NULL;
			}
		}

		IOFree (mSampleBufferDescriptors, mNumUSBFrameLists * sizeof (IOSubMemoryDescriptor *));
		mSampleBufferDescriptors = NULL;
	}

	if (NULL != mUSBIsocFrames) 
	{
		IOFree (mUSBIsocFrames, mNumUSBFrameLists * mNumTransactionsPerList * sizeof (IOUSBLowLatencyIsocFrame));
		mUSBIsocFrames = NULL;
	}

	if (NULL != mUSBCompletion) 
	{
		IOFree (mUSBCompletion, mNumUSBFrameLists * sizeof (IOUSBLowLatencyIsocCompletion));
		mUSBCompletion = NULL;
	}

	if (mUSBAudioDevice) 
	{
		mUSBAudioDevice->release ();
		mUSBAudioDevice = NULL;
	}

	if (mUSBAudioEngine) 
	{
		mUSBAudioEngine->release ();
		mUSBAudioEngine = NULL;
	}

	if (mStreamInterface) 
	{
		mStreamInterface->release ();
		mStreamInterface = NULL;
	}

	super::free ();
	debugIOLog ("- AppleUSBAudioStream[%p]::free()", this);
}

bool AppleUSBAudioStream::initWithAudioEngine (AppleUSBAudioDevice * device, AppleUSBAudioEngine * engine, IOUSBInterface * streamInterface, IOAudioSampleRate sampleRate, const char *streamDescription, OSDictionary *properties) {
	AUAConfigurationDictionary *		configDictionary;
	Boolean								result = false;
	UInt32								startChannelID;

	debugIOLog("+ AppleUSBAudioStream[%p]::initWithAudioEngine ()", this);

	FailIf (NULL == device, Exit);
	FailIf (NULL == engine, Exit);
	FailIf (NULL == streamInterface, Exit);

	result = FALSE;

	mUSBAudioDevice = device;
	mUSBAudioDevice->retain ();
	
	mUSBAudioEngine = engine;
	mUSBAudioEngine->retain ();
	
	mStreamInterface = streamInterface;
	mStreamInterface->retain ();

	mInterfaceNumber = mStreamInterface->GetInterfaceNumber ();
	debugIOLog ("? AppleUSBAudioStream[%p]::initWithAudioEngine () - mInterfaceNumber = %d", this, mInterfaceNumber);

	mVendorID = mUSBAudioDevice->getVendorID ();
	mProductID = mUSBAudioDevice->getProductID ();
	
	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary ()), Exit);

	// Choose default alternate setting ID and sampling rate ( rdar://3866513 )
	
	FailIf (kIOReturnSuccess != GetDefaultSettings (&mAlternateSettingID, &sampleRate), Exit);
	FailIf (kIOReturnSuccess != configDictionary->getIsocEndpointDirection (&mDirection, mInterfaceNumber, mAlternateSettingID), Exit);	

	startChannelID = ( kIOAudioStreamDirectionOutput == mDirection ) ? mUSBAudioEngine->mStartOutputChannelID : mUSBAudioEngine->mStartInputChannelID;
	
	FailIf (FALSE == super::initWithAudioEngine (engine, (IOAudioStreamDirection)mDirection, startChannelID, streamDescription, properties), Exit);

	// Safeguard against USB-Audio 2.0 descriptors for rdar://4798933. 
	// Check both INTERFACE_PROTOCOL_UNDEFINED and IP_PROTOCOL_VERSION to protect against unsupported
	// protocols.
	FailIf ((INTERFACE_PROTOCOL_UNDEFINED != mStreamInterface->GetInterfaceProtocol()) && (IP_VERSION_02_00 != mStreamInterface->GetInterfaceProtocol()), Exit);

	// Change this to use defines from the IOAudioFamily when they are available
	setProperty ("IOAudioStreamSampleFormatByteOrder", "Little Endian");
    
	// Attach to the stream interface so we get willTerminate message.
	attach ( mStreamInterface );	
	
	mInitStampDifference = true;	// <rdar://problem/7378275>
	
	result = TRUE;
        
Exit:
	debugIOLog("- AppleUSBAudioStream[%p]::initWithAudioEngine ()", this);
	return result;
}

bool AppleUSBAudioStream::requestTerminate (IOService * provider, IOOptionBits options) {
	bool					result;

	debugIOLog ("+ AppleUSBAudioStream[%p]::requestTerminate (%p, %x)", this, provider, options);

	// if interface or audio device
	if ( mUSBAudioEngine == provider || mStreamInterface == provider ) 
	{
		result = TRUE;		// it is OK to terminate us
	} 
	else 
	{
		result = FALSE;		// don't terminate us
	}

	debugIOLog ("- AppleUSBAudioStream[%p]::requestTerminate (%p, %x) = %d", this, provider, options, result);
	return result;
}

void AppleUSBAudioStream::stop (IOService * provider) {
    debugIOLog("+ AppleUSBAudioStream[%p]::stop (%p)", this, provider);

	if (NULL != mPluginInitThread)
	{
		thread_call_cancel (mPluginInitThread);
		thread_call_free (mPluginInitThread);
		mPluginInitThread = NULL;
	}

	if (mPlugin) 
	{
		mPlugin->close (this);
		mPlugin = NULL;
	}

	if (mUSBAudioDevice) 
	{
		mUSBAudioDevice->release ();
		mUSBAudioDevice = NULL;
	}

	if (mUSBAudioEngine) 
	{
		mUSBAudioEngine->release ();
		mUSBAudioEngine = NULL;
	}

	if (mPipe) 
	{
		mPipe->release ();
		mPipe = NULL;
	}

	if (mAssociatedPipe) 
	{
		mAssociatedPipe->release ();
		mAssociatedPipe = NULL;
	}

	// [rdar://4287899] We don't expect the stream interface to need closing unless the following conditions are true.
	if (		(mStreamInterface)
			&&	( (provider == mUSBAudioEngine) || (provider == mStreamInterface) )
			&&	(mStreamInterface->isOpen())) 
	{
		debugIOLog ("! AppleUSBAudioStream[%p]::stop () - mStreamInterface was still open when stop() was called. Closing ...", this);
		mStreamInterface->close (this);
		mStreamInterface->release ();
		mStreamInterface = NULL;
	}

//Exit:
	super::stop (provider);

	debugIOLog ("- AppleUSBAudioStream[%p]::stop (%p) - rc=%ld", this, provider, getRetainCount());
}

bool AppleUSBAudioStream::terminate (IOOptionBits options) {
	bool							shouldTerminate;
	bool							result;

	result = TRUE;
	shouldTerminate = TRUE;

	debugIOLog ("+ AppleUSBAudioStream[%p]::terminate ()", this);

	if (shouldTerminate) 
	{
		result = super::terminate (options);
	}

	debugIOLog ("- AppleUSBAudioStream[%p]::terminate ()", this);

	return result;
}

bool AppleUSBAudioStream::matchPropertyTable(OSDictionary * table, SInt32 *score)
{
	bool		returnValue = false;
	
	//debugIOLog ("+ AppleUSBAudioStream[%p]::matchPropertyTable (%p, %p)", this, table, score);
	
	if (super::matchPropertyTable (table, score))
	{
		if (compareProperty (table, kIDVendorString) && 
			compareProperty (table, kIDProductString) &&
			compareProperty (table, kIOAudioStreamDirectionKey))
		{
			returnValue = true;
		}
	}

	//debugIOLog ("- AppleUSBAudioStream[%p]::matchPropertyTable (%p, %p) = %d", this, table, score, returnValue);
	
	return returnValue;
}

// <rdar://7295322> Asynchronous to prevent deadlock if the device or interface is terminated while
// registerService() is performing matching.
void AppleUSBAudioStream::registerService(IOOptionBits options)
{
	debugIOLog ("+ AppleUSBAudioStream[%p]::registerService ( 0x%lx )", this, options);

	if ( 0 == ( kIOServiceSynchronous & options ) )
	{
		options |= kIOServiceAsynchronous;
	}
	
	super::registerService ( options );

	debugIOLog ("- AppleUSBAudioStream[%p]::registerService ( 0x%lx )", this, options);
}

#pragma mark -USB Audio driver-

IOReturn AppleUSBAudioStream::addAvailableFormats (AUAConfigurationDictionary * configDictionary)
{
    IOAudioStreamFormat					streamFormat;
    IOAudioStreamFormatExtension		streamFormatExtension;
    IOAudioSampleRate					lowSampleRate;
    IOAudioSampleRate					highSampleRate;
	OSArray *							sampleRates = NULL;
	OSObject *							arrayObject = NULL;
	OSNumber *							arrayNumber = NULL;
	IOReturn							result = kIOReturnError;
	UInt32								thisSampleRate;
	UInt32								otherSampleRate;
	UInt16								format;
    UInt8								numAltInterfaces;
    UInt8								numSampleRates;
    UInt8								altSettingIndex;
	UInt8								numChannels;
	UInt8								rateIndex;
	UInt8								candidateAC3AltSetting;
	Boolean								hasNativeAC3Format;
	Boolean								hasDigitalOutput = FALSE;
	Boolean								isClockSourceProgrammable = TRUE;		//	<rdar://5811247>

	debugIOLog ("+ AppleUSBAudioStream[%p]::addAvailableFormats (%p)", this, configDictionary);
	FailIf (NULL == mUSBAudioDevice, Exit);							// <rdar://7085810>
	FailIf (NULL == mUSBAudioEngine, Exit);							// <rdar://7085810>
	FailIf (NULL == mUSBAudioDevice->mControlInterface, Exit);		// <rdar://7085810>
	FailIf (NULL == configDictionary, Exit);

	FailIf (kIOReturnSuccess != configDictionary->getNumAltSettings (&numAltInterfaces, mInterfaceNumber), Exit);
	debugIOLog ("? AppleUSBAudioStream[%p]::addAvailableFormats () - There are %d alternate interfaces @ interface %d", this, numAltInterfaces, mInterfaceNumber);
	hasNativeAC3Format = FALSE;
	candidateAC3AltSetting = 0;

	//	<rdar://5811247>
	if ( ( IP_VERSION_02_00 == mUSBAudioDevice->mControlInterface->GetInterfaceProtocol() ) && ( 0 != mUSBAudioEngine->mCurrentClockSourceID ) )
	{
		isClockSourceProgrammable = configDictionary->clockSourceHasFrequencyControl ( mUSBAudioDevice->mControlInterface->GetInterfaceNumber (), 0, mUSBAudioEngine->mCurrentClockSourceID, true );
	}
	
	// Find all of the available formats on the device.
	altSettingIndex = configDictionary->alternateSettingZeroCanStream (mInterfaceNumber) ? 0 : 1;
	
	for ( ; altSettingIndex < numAltInterfaces; altSettingIndex++) 
	{
		//	<rdar://5811247>If the clock source is present, use that to find out if the clock source is programmable.
		//	If it is programmable, then do what it is done now. If it is not programmable, then only 1 sample rate is supported, which
		//	is the sample rate of the clock source.
		if ( isClockSourceProgrammable )
		{
			// [rdar://5067229]
			if ( kIOReturnSuccess != configDictionary->getNumSampleRates (&numSampleRates, mInterfaceNumber, altSettingIndex) )
			{
				continue;
			}
			sampleRates = configDictionary->getSampleRates (mInterfaceNumber, altSettingIndex);
		}
		else
		{
			numSampleRates = 0;
			sampleRates = NULL;
		}
		
		// [rdar://5284099] Check the format before deciding whether to retrieve the following values.
		FailIf (kIOReturnSuccess != configDictionary->getFormat (&format, mInterfaceNumber, altSettingIndex), Exit);
		if (		( PCM == format )
				||	( IEC1937_AC3 == format ) )
		{
			FailIf (kIOReturnSuccess != configDictionary->getNumChannels (&numChannels, mInterfaceNumber, altSettingIndex), Exit);
			FailIf (kIOReturnSuccess != configDictionary->getBitResolution (&(streamFormat.fBitDepth), mInterfaceNumber, altSettingIndex), Exit);
			FailIf (kIOReturnSuccess != configDictionary->getSubframeSize (&(streamFormat.fBitWidth), mInterfaceNumber, altSettingIndex), Exit);
		}
		else
		{
			numChannels = 0;
		}

		streamFormat.fNumChannels = numChannels;
		streamFormat.fBitWidth *= 8;
		streamFormat.fAlignment = kIOAudioStreamAlignmentLowByte;
		streamFormat.fByteOrder = kIOAudioStreamByteOrderLittleEndian;
		streamFormat.fDriverTag = (mInterfaceNumber << 16) | altSettingIndex;

		streamFormatExtension.fVersion = kFormatExtensionCurrentVersion;
		streamFormatExtension.fFlags = 0;
		streamFormatExtension.fFramesPerPacket = 1;
		streamFormatExtension.fBytesPerPacket = numChannels * (streamFormat.fBitWidth / 8);

		
		switch (format) 
		{
			case PCM:
				streamFormat.fSampleFormat = kIOAudioStreamSampleFormatLinearPCM;
				streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
				streamFormat.fIsMixable = TRUE;
				if (2 == streamFormat.fNumChannels && 16 == streamFormat.fBitDepth && 16 == streamFormat.fBitWidth) 
				{
					candidateAC3AltSetting = altSettingIndex;
				}
				break;
			case AC3:	// just starting to stub something in for AC-3 support
				debugIOLog ("? AppleUSBAudioStream[%p]::addAvailableFormats () - Variable bit rate AC-3 audio format type", this);
				continue;	// We're not supporting this at the moment, so just skip it.
				streamFormat.fSampleFormat = kIOAudioStreamSampleFormatAC3;
				streamFormat.fIsMixable = FALSE;
				streamFormat.fNumChannels = 6;
				streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
				streamFormat.fBitDepth = 16;
				streamFormat.fBitWidth = 16;
				streamFormat.fByteOrder = kIOAudioStreamByteOrderBigEndian;

				FailIf (kIOReturnSuccess != configDictionary->getAC3BSID (&(streamFormatExtension.fFlags), mInterfaceNumber, altSettingIndex), Exit);
//				streamFormatExtension.fFlags = USBToHostLong (configDictionary->GetAC3BSID (mInterfaceNumber, altSettingIndex));
//				streamFormatExtension.fFramesPerPacket = configDictionary->GetSamplesPerFrame (mInterfaceNumber, altSettingIndex);
				streamFormatExtension.fFramesPerPacket = 1536;
//				streamFormatExtension.fBytesPerPacket = ((configDictionary->GetMaxBitRate (mInterfaceNumber, altSettingIndex) * 1024 / 8) + 500) / 1000;
				streamFormatExtension.fBytesPerPacket = streamFormatExtension.fFramesPerPacket * streamFormat.fNumChannels * (streamFormat.fBitWidth / 8);
				break;
			case IEC1937_AC3:
				debugIOLog ("? AppleUSBAudioStream[%p]::addAvailableFormats () - IEC1937 AC-3 audio format type", this);
				hasNativeAC3Format = TRUE;
				streamFormat.fSampleFormat = kIOAudioStreamSampleFormat1937AC3;
				streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
				streamFormat.fIsMixable = FALSE;

				streamFormatExtension.fFramesPerPacket = 1536;
				streamFormatExtension.fBytesPerPacket = streamFormatExtension.fFramesPerPacket * streamFormat.fNumChannels * (streamFormat.fBitWidth / 8);
				break;
			default:
				debugIOLog ("? AppleUSBAudioStream[%p]::addAvailableFormats () - Interface format = 0x%x not published.", this, format);
				continue;	// skip this alternate interface
		}

		debugIOLog ("? AppleUSBAudioStream[%p]::addAvailableFormats () - Interface %d, Alt %d has a ", this, mInterfaceNumber, altSettingIndex);
		debugIOLog ("     %d bit interface, ", streamFormat.fBitDepth);
		debugIOLog ("     %d channel(s), and ", streamFormat.fNumChannels);
		debugIOLog ("     %d sample rate(s), which is/are:", numSampleRates);

		if (numSampleRates && sampleRates) 
		{
			for (rateIndex = 0; rateIndex < numSampleRates; rateIndex++) 
			{

				FailIf (NULL == (arrayObject = sampleRates->getObject (rateIndex)), Exit);
				FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
				thisSampleRate = arrayNumber->unsigned32BitValue();
				debugIOLog ("          %d", thisSampleRate);
				lowSampleRate.whole = thisSampleRate;
				lowSampleRate.fraction = 0;
				this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &lowSampleRate);
				if (kIOAudioStreamSampleFormatLinearPCM == streamFormat.fSampleFormat) 
				{
					streamFormat.fIsMixable = FALSE;
					this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &lowSampleRate);
					streamFormat.fIsMixable = TRUE;		// set it back to TRUE for next time through the loop
				}
			}
			debugIOLog ("");
		} 
		else if (sampleRates) 
		{
			FailIf (NULL == (arrayObject = sampleRates->getObject (0)), Exit);
			FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
			thisSampleRate = arrayNumber->unsigned32BitValue();
			FailIf (NULL == (arrayObject = sampleRates->getObject (1)), Exit);
			FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
			otherSampleRate = arrayNumber->unsigned32BitValue();

			debugIOLog ("          %d to %d", thisSampleRate, otherSampleRate);
			lowSampleRate.whole = thisSampleRate;
			lowSampleRate.fraction = 0;
			highSampleRate.whole = otherSampleRate;
			highSampleRate.fraction = 0;
			this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &highSampleRate);
			if (kIOAudioStreamSampleFormatLinearPCM == streamFormat.fSampleFormat) 
			{
				streamFormat.fIsMixable = FALSE;
				this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &highSampleRate);
			}
		}
		else
		{
			//	<rdar://5811247>
			if ( !isClockSourceProgrammable )
			{
				OSArray * clockPath = NULL;
				OSArray * clockPathGroup = mUSBAudioDevice->getClockPathGroup ( mInterfaceNumber, altSettingIndex );
				FailIf ( NULL == clockPathGroup, Exit );
				FailIf ( NULL == ( clockPath = OSDynamicCast ( OSArray, clockPathGroup->getObject ( mUSBAudioEngine->mCurrentClockPathIndex - 1 ) ) ), Exit );

				if ( kIOReturnSuccess == mUSBAudioDevice->getClockPathCurSampleRate ( &thisSampleRate, NULL, NULL, clockPath ) )	//	<rdar://6945472>
				{
					debugIOLog ("          %d", thisSampleRate);
					lowSampleRate.whole = thisSampleRate;
					lowSampleRate.fraction = 0;
					this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &lowSampleRate);
				}
			}
		}
	} // for altSettingIndex

	/*
	configDictionary->getOutputTerminalType (&terminalType, mUSBAudioDevice->mControlInterface->GetInterfaceNumber (), 0, altSettingIndex);
	switch (terminalType) 
	{
		case EXTERNAL_DIGITAL_AUDIO_INTERFACE:
		case EXTERNAL_SPDIF_INTERFACE:
		case EMBEDDED_DVD_AUDIO:
			hasDigitalOutput = TRUE;
			break;
		default:
			hasDigitalOutput = FALSE;
	}
	*/

	if (TRUE == hasDigitalOutput && FALSE == hasNativeAC3Format && 0 != candidateAC3AltSetting && kIOAudioStreamDirectionOutput == getDirection ()) 
	{
		FailIf (kIOReturnSuccess != configDictionary->getNumSampleRates (&numSampleRates, mInterfaceNumber, candidateAC3AltSetting), Exit);
		sampleRates = configDictionary->getSampleRates (mInterfaceNumber, candidateAC3AltSetting);

		FailIf (kIOReturnSuccess != configDictionary->getNumChannels (&numChannels, mInterfaceNumber, candidateAC3AltSetting), Exit);
		streamFormat.fNumChannels = numChannels;
		FailIf (kIOReturnSuccess != configDictionary->getBitResolution (&(streamFormat.fBitDepth), mInterfaceNumber, candidateAC3AltSetting), Exit);
		FailIf (kIOReturnSuccess != configDictionary->getSubframeSize (&(streamFormat.fBitWidth), mInterfaceNumber, candidateAC3AltSetting), Exit); 
		streamFormat.fBitWidth *= 8;
		streamFormat.fAlignment = kIOAudioStreamAlignmentLowByte;
		streamFormat.fByteOrder = kIOAudioStreamByteOrderLittleEndian;
		streamFormat.fDriverTag = (mInterfaceNumber << 16) | candidateAC3AltSetting;
		streamFormat.fSampleFormat = kIOAudioStreamSampleFormat1937AC3;
		streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
		streamFormat.fIsMixable = FALSE;

		streamFormatExtension.fVersion = kFormatExtensionCurrentVersion;
		streamFormatExtension.fFlags = 0;
		streamFormatExtension.fFramesPerPacket = 1536;
		streamFormatExtension.fBytesPerPacket = streamFormatExtension.fFramesPerPacket * streamFormat.fNumChannels * (streamFormat.fBitWidth / 8);

		if (numSampleRates && sampleRates) 
		{
			for (rateIndex = 0; rateIndex < numSampleRates; rateIndex++) 
			{
				FailIf (NULL == (arrayObject = sampleRates->getObject (rateIndex)), Exit);
				FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
				thisSampleRate = arrayNumber->unsigned32BitValue();
				lowSampleRate.whole = thisSampleRate;
				lowSampleRate.fraction = 0;
				this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &lowSampleRate);
			}
		} 
		else if (sampleRates) 
		{
			FailIf (NULL == (arrayObject = sampleRates->getObject (0)), Exit);
			FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
			thisSampleRate = arrayNumber->unsigned32BitValue();
			FailIf (NULL == (arrayObject = sampleRates->getObject (1)), Exit);
			FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
			otherSampleRate = arrayNumber->unsigned32BitValue();
			lowSampleRate.whole = thisSampleRate;
			lowSampleRate.fraction = 0;
			highSampleRate.whole = otherSampleRate;
			highSampleRate.fraction = 0;
			this->addAvailableFormat (&streamFormat, &streamFormatExtension, &lowSampleRate, &highSampleRate);
		}
	} // if hasDigitalOutput ...

	result = kIOReturnSuccess;

Exit:
	debugIOLog ("- AppleUSBAudioStream[%p]::addAvailableFormats (%p) = 0x%x", this, configDictionary, result);
	return result;
}

// <rdar://7259238>
IOReturn AppleUSBAudioStream::setFormat(const IOAudioStreamFormat *streamFormat, bool callDriver)
{
	IOReturn	result;
	
	debugIOLog ("- AppleUSBAudioStream[%p]::setFormat (%p, %d)", this, streamFormat, callDriver );
	
	result = super::setFormat ( streamFormat, callDriver );
	
	debugIOLog ("- AppleUSBAudioStream[%p]::setFormat (%p, %d) = 0x%x", this, streamFormat, callDriver, result);
	
	return result;
}

// <rdar://7259238>
IOReturn AppleUSBAudioStream::setFormat(const IOAudioStreamFormat *streamFormat, const IOAudioStreamFormatExtension *formatExtension, OSDictionary *formatDict, bool callDriver)
{
	IOReturn	result = kIOReturnError;
	bool		streamIsRunning = false;
	
	FailIf ( 0 == mUSBAudioEngine, Exit );
	
	debugIOLog ("- AppleUSBAudioStream[%p]::setFormat (%p, %p, %p, %d)", this, streamFormat, formatExtension, formatDict, callDriver );

	streamIsRunning = mUSBStreamRunning;
	if (streamIsRunning)
	{
		mUSBAudioEngine->pauseAudioEngine ();
	}
	
	mUSBAudioEngine->beginConfigurationChange ();
	
	result = super::setFormat ( streamFormat, formatExtension, formatDict, callDriver );
	
	if ( kIOReturnSuccess == result )
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::setFormat (%p, %p, %p, %d) - Delaying %u ms...", this, streamFormat, formatExtension, formatDict, callDriver, kFormatChangeDelayInMs );
		// Wait a bit after format change so that the USB audio device has a chance to catch up.
		IOSleep ( kFormatChangeDelayInMs );
	}

	// Send an engine change notification so that the HAL refreshes its settings.
	mUSBAudioEngine->completeConfigurationChange ();

	if (streamIsRunning)
	{
		mUSBAudioEngine->resumeAudioEngine ();
	}
	
Exit:
	debugIOLog ("- AppleUSBAudioStream[%p]::setFormat (%p, %p, %p, %d) = 0x%x", this, streamFormat, formatExtension, formatDict, callDriver, result);
	
	return result;
}

// [rdar://4487489] - Use this method to allocate all USB buffers.
IOBufferMemoryDescriptor * AppleUSBAudioStream::allocateBufferDescriptor (IOOptionBits options, vm_size_t capacity, vm_offset_t alignment)
{
	IOBufferMemoryDescriptor *	bufferDescriptorPtr = NULL;
	#ifdef IOMEMORYDESCRIPTOR_SUPPORTS_DMACOMMAND
	mach_vm_address_t			physicalMask;
	IOOptionBits				usbOptions;
	IOUSBControllerV2 *			usbController;
	#endif
	
	debugIOLog ("+ AppleUSBAudioStream[%p]::allocateBufferDescriptor ()", this);
	FailIf (NULL == mStreamInterface, Exit);
	#ifdef IOMEMORYDESCRIPTOR_SUPPORTS_DMACOMMAND
	FailIf (NULL == (usbController = OSDynamicCast (IOUSBControllerV2, mStreamInterface->GetDevice()->GetBus ())), Exit);
	// The following API call was introduced in IOUSBFamily 2.6.0b6 [rdar://4492080]
	FailIf (kIOReturnSuccess !=  usbController->GetLowLatencyOptionsAndPhysicalMask (&usbOptions, &physicalMask), Exit);
	options |= usbOptions;
	debugIOLog ("? AppleUSBAudioStream[%p]::allocateBufferDescriptor () - allocating a buffer with mask 0x%x", this, physicalMask);
	bufferDescriptorPtr = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, options, capacity, physicalMask);
	#else
	if ( mUHCISupport )
	{
		options |= kIOMemoryPhysicallyContiguous;
	}
	bufferDescriptorPtr = IOBufferMemoryDescriptor::withOptions ( options, capacity, alignment );
	#endif
	
Exit:
	debugIOLog ("- AppleUSBAudioStream[%p]::allocateBufferDescriptor () = %p", this, bufferDescriptorPtr);
	return bufferDescriptorPtr;
}

void AppleUSBAudioStream::calculateSamplesPerPacket (UInt32 sampleRate, UInt16 * averageFrameSamples, UInt16 * additionalSampleFrameFreq) 
{
	UInt32					divisor;
	UInt32					modulus;

	// [rdar://4801012] For USB 2.0 audio, packets correspond to microframes.
	
	if ( mTransactionsPerUSBFrame )
	{
		modulus = 1000 * mTransactionsPerUSBFrame;
	}
	else
	{
		modulus = 1000;
	}
		
	* averageFrameSamples = sampleRate / modulus;

	// [rdar://5600254] For UAC2.0, the following calculation is not useful. 
	// Aside from iSub, there should be no reason to use additionalSampleFrameFreq.
	divisor = (sampleRate % modulus);

	if ( divisor )
	{
		* additionalSampleFrameFreq = modulus / divisor;
	}
	else
	{
		* additionalSampleFrameFreq = 0;
	}
	
	// [rdar://5600254] We can log the data cadence here.
	#if SHOWCADENCE
	UInt32					transferSampleRate;
	UInt32					currentSampleRate;
	UInt32					remainder;
	UInt32					accumulatedSamples = 0ul;
	UInt16					transfer = 0;
	UInt16					cycleLength = 0;
	UInt8					powerOfTwo = 0;
	UInt8					transactionsPerMS;
	
	transactionsPerMS = mTransactionsPerUSBFrame;
	
	while ( transactionsPerMS > 1 )
	{
		transactionsPerMS >>= 1;
		powerOfTwo++;
	}
	
	transferSampleRate = sampleRate << ( 16 - powerOfTwo );
	currentSampleRate = ( sampleRate / ( mTransactionsPerUSBFrame * 1000 ) );
	currentSampleRate *= 1000;
	currentSampleRate <<= 16;
	remainder = transferSampleRate - currentSampleRate;
	
	debugIOLog ( "? AppleUSBAudioStream[%p]::calculateSamplesPerPacket - transferSampleRate = 0x%8X, currentSampleRate = 0x%X", this, transferSampleRate, currentSampleRate );
	
	transfer = 1;
	cycleLength = 0;
	
	while (			( remainder )
				&&	(		( 0 == accumulatedSamples )
						||	( accumulatedSamples % ( 1000 << 16 ) ) ) )
	{
		accumulatedSamples += remainder;
		if ( accumulatedSamples >= ( 1000 << 16 ) )
		{
			debugIOLog ( "? AppleUSBAudioStream[%p]::calculateSamplesPerPacket - large packet on transfer %d (1-indexed)", this , transfer );
			accumulatedSamples -= ( 1000 << 16 );
		}
		transfer++;
		cycleLength++;
		if ( ! accumulatedSamples )
		{
			break;
		}
	}
	debugIOLog ( "? AppleUSBAudioStream[%p]::calculateSamplesPerPacket - cycleLength = %d", this, cycleLength );
	#endif
}

IOReturn AppleUSBAudioStream::checkForFeedbackEndpoint (AUAConfigurationDictionary * configDictionary)
{
	IOUSBFindEndpointRequest			associatedEndpoint;
	IOReturn							result;
	UInt16								maxPacketSize;
	UInt8								assocEndpoint;
	UInt8								address;
 	UInt8								syncType;

	result = kIOReturnSuccess;
	mAssociatedPipe = NULL;
	FailIf (NULL == mUSBAudioDevice, Exit);
	FailIf (NULL == mStreamInterface, Exit);
	FailIf (configDictionary->getIsocEndpointAddress (&address, mInterfaceNumber, mAlternateSettingID, mDirection), Exit);
	FailIf (configDictionary->getIsocEndpointSyncType (&syncType, mInterfaceNumber, mAlternateSettingID, address), Exit);
	if (kAsynchSyncType == syncType) 
	{
		// debugIOLog ("checking endpoint %d for an associated endpoint", address);
		FailIf (kIOReturnSuccess != configDictionary->getIsocAssociatedEndpointAddress (&assocEndpoint, mInterfaceNumber, mAlternateSettingID, address), Exit);
		if (assocEndpoint != 0) 
		{
			debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - assocEndpoint = 0x%x", this, assocEndpoint);
			FailIf (kIOReturnSuccess != configDictionary->getIsocAssociatedEndpointRefreshInt (&mRefreshInterval, mInterfaceNumber, mAlternateSettingID, assocEndpoint), Exit);
			FailIf (kIOReturnSuccess != configDictionary->getIsocAssociatedEndpointMaxPacketSize (&maxPacketSize, mInterfaceNumber, mAlternateSettingID, assocEndpoint), Exit);
			if (kUSBDeviceSpeedHigh == mUSBAudioDevice->getDeviceSpeed())
			{
				// Request 4 bytes for the 16.16 value if the endpoint allows it
				mFeedbackPacketSize = (maxPacketSize < kFixedPoint16_16ByteSize) ? maxPacketSize : kFixedPoint16_16ByteSize;
			}
			else
			{
				mFeedbackPacketSize = kFixedPoint10_14ByteSize;
			}
			debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - Synch endpoint has refresh interval %d, feedback packet size %d", this, mRefreshInterval, mFeedbackPacketSize);
			mRefreshInterval = mRefreshInterval ? mRefreshInterval : kMinimumSyncRefreshInterval;
			mFramesUntilRefresh = 1 << mRefreshInterval;		// the same as 2^mRefreshInterval
			
			// If the hardware needs to be updated more often than PLAY_NUM_USB_FRAMES_PER_LIST ms, change list size to PLAY_NUM_USB_FRAMES_PER_LIST_SYNC frames.			
			if (mFramesUntilRefresh < mNumUSBFramesPerList) 
			{
				debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - Need to adjust mNumUSBFramesPerList: %ld < %ld", mFramesUntilRefresh, mNumUSBFramesPerList);
				if (NULL != mUSBIsocFrames) 
				{
					debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - Disposing of current mUSBIsocFrames [%p]", this, mUSBIsocFrames);
					IOFree (mUSBIsocFrames, mNumUSBFrameLists * mNumTransactionsPerList * sizeof (IOUSBLowLatencyIsocFrame));
					mUSBIsocFrames = NULL;
				}
				mNumUSBFramesPerList = PLAY_NUM_USB_FRAMES_PER_LIST_SYNC;
				mNumTransactionsPerList = mNumUSBFramesPerList * mTransactionsPerUSBFrame;
				mNumUSBFrameLists = mNumUSBFrameListsToQueue * 2;
				debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - mNumUSBFramesPerList = %d, mNumUSBFrameListsToQueue = %d, mNumUSBFrameLists = %d", this, mNumUSBFramesPerList, mNumUSBFrameListsToQueue, mNumUSBFrameLists);
				mUSBIsocFrames = (IOUSBLowLatencyIsocFrame *)IOMalloc (mNumUSBFrameLists * mNumTransactionsPerList * sizeof (IOUSBLowLatencyIsocFrame));
				debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - mUSBIsocFrames is now %p", this, mUSBIsocFrames);
				FailIf (NULL == mUSBIsocFrames, Exit);
				
				// <rdar://7568547> Initialize the USB Isoc frames so that CoalesceInputSamples() will not panic
				// due to uninitialized values in frStatus & frActCount.
				initializeUSBFrameList ( mUSBIsocFrames, mNumUSBFrameLists * mNumTransactionsPerList );
			}
			associatedEndpoint.type = kUSBIsoc;
			associatedEndpoint.direction = kUSBIn;	// The associated endpoint always goes "in"
			// The sample rate should be either a 3-byte 10.14 or a 4-byte 16.16
			associatedEndpoint.maxPacketSize = mFeedbackPacketSize;
			associatedEndpoint.interval = 0xFF;
			mAssociatedPipe = mStreamInterface->FindNextPipe (NULL, &associatedEndpoint);
			FailWithAction (NULL == mAssociatedPipe, result = kIOReturnError, Exit);

			if (NULL == mAssociatedEndpointMemoryDescriptor) 
			{
				// <rdar://7000283> Remove the use of deprecated IOMallocContiguous(). Use IOBufferMemoryDescriptor to allocate memory instead.
				mAssociatedEndpointMemoryDescriptor = IOBufferMemoryDescriptor::withOptions ( kIODirectionInOut, sizeof (UInt32), sizeof (UInt32) );
				FailIf ( NULL == mAssociatedEndpointMemoryDescriptor, Exit );
				mAverageSampleRateBuffer = (UInt32 *) mAssociatedEndpointMemoryDescriptor->getBytesNoCopy ();
				FailIf (NULL == mAverageSampleRateBuffer, Exit);
				bzero (mAverageSampleRateBuffer, sizeof (UInt32));
			}
			mSampleRateFrame.frStatus = -1;
			mSampleRateFrame.frReqCount = mFeedbackPacketSize;
			mSampleRateFrame.frActCount = 0;
			mSampleRateCompletion.target = (void *)this;
			mSampleRateCompletion.action = sampleRateHandler;
			mSampleRateCompletion.parameter = 0;

			mAssociatedPipe->retain ();
		} // if (assocEndpoint != 0) 
		else 
		{
			debugIOLog ("! AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - No associated synch endpoint found.", this);
		}
	} // if (kAsynchSyncType == syncType)
	else 
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::checkForFeedbackEndpoint () - No associated synch endpoint.", this);
	}

Exit:
	return result;
}

// This function is called from both the IOProc's call to convertInputSamples and by the readHandler.
// To figure out where to start coalescing from, it looks at the mCurrentFrameList, which is updated by the readHandler.
// It will copy from currentFameList+1 the number of bytes requested or one USB frame list.
// When numBytesToCoalesce == 0 it will coalesce the current USB frame list (however big it is).
// If numBytesToCoalesce != 0, it will coalesce that many bytes starting from the current frame list and going to the next one if needed.
// When called from the readHandler it will just coalesce one USB frame starting from mCurrentFrameList.
// When called from convertInputSamples, it will convert the number of bytes that corresponds to the number of samples that are being asked to be converted,
// starting from mCurrentFrameList.

IOReturn AppleUSBAudioStream::CoalesceInputSamples (UInt32 numBytesToCoalesce, IOUSBLowLatencyIsocFrame * pFrames) {
	IOReturn						result = kIOReturnSuccess;
	AbsoluteTime					time;
	UInt32							usbFrameIndex;
	UInt32							firstUSBFrameIndex;		//	<rdar://6094454>
	UInt32							totalNumUSBFrames;		//	<rdar://6094454>
	UInt32							numFramesChecked;
	UInt32							numBytesToCopy;
	UInt32							numBytesToEnd;
	UInt32							numBytesCopied;
	UInt32							originalBufferOffset;
	SInt32							numBytesLeft;
	UInt32							preWrapBytes = 0;
	UInt32							byteCount = 0;
	UInt8 *							source;
	UInt8 *							dest;
	Boolean							done;
	bool							onCoreAudioThread;
#if DEBUGINPUT
	// <rdar://problem/7378275>
	UInt32							numBytesOnLastCopy;
	UInt32							numBytesMargin = -1;
	UInt32							usbFrameIndexOnLastCopy;
	UInt32							firstUSBFrameIndexOnLastCopy;
	IOUSBLowLatencyIsocFrame *		pFramesOnLastCopy;
#endif
    
	if (mCoalescenceMutex)
	{
		IORecursiveLockLock (mCoalescenceMutex);
	}
	
	#if DEBUGINPUT
	debugIOLog ("+ AppleUSBAudioStream[%p]::CoalesceInputSamples (%lu, %p)", this, numBytesToCoalesce, pFrames); 
	#endif
	
	originalBufferOffset = 0;
	if (0 != numBytesToCoalesce) 
	{
		// This is being called from the CoreAudio thread
		onCoreAudioThread = true;
		originalBufferOffset = mBufferOffset;		// So that when we later get called from the readHandler that we'll put everything in the right spot
		#if DEBUGINPUT
		debugIOLog ("! AppleUSBAudioStream[%p]::CoalesceInputSamples () - Coalesce from %ld %ld bytes (framelist %ld) on CoreAudio thread", this, originalBufferOffset, numBytesToCoalesce, mCurrentFrameList);
		#endif
		if ( mMasterMode && !mHaveTakenFirstTimeStamp )
		{
			debugIOLog ("! AppleUSBAudioStream[%p]::CoalesceInputSamples () - CoreAudio thread is asking for samples without having been sent a timestamp!", this );
		}
	}
	else
	{
		onCoreAudioThread = false;
	}

	if (NULL == pFrames) 
	{
		pFrames = &mUSBIsocFrames[mCurrentFrameList * mNumTransactionsPerList];
	}

	dest = (UInt8 *)getSampleBuffer () + mBufferOffset;
	source = (UInt8 *)mReadBuffer + (mCurrentFrameList * mReadUSBFrameListSize);

	//	<rdar://6094454>	Pre-compute these values here instead in the while loop. There is a race condition where 
	//	mCurrentFrameList is updated in the readHandler(), and if it changes, then it could cause usbFrameIndex to get
	//	out of range when accessing pFrames. firstUSBFrameIndex should be tied to pFrames, so it should only change 
	//	when pFrames changes in the wrap situation. totalNumUSBFrames shouldn't change at all.	
	firstUSBFrameIndex = (mCurrentFrameList * mNumTransactionsPerList);
	totalNumUSBFrames = (mNumUSBFrameLists * mNumTransactionsPerList);

	usbFrameIndex = 0;
	numFramesChecked = 0;
	numBytesCopied = 0;
	numBytesLeft = numBytesToCoalesce;
	done = FALSE;

	while (    (FALSE == done) 
			&& ('llit' != pFrames[usbFrameIndex].frStatus)			// IOUSBFamily is processing this now
			&& (-1 != pFrames[usbFrameIndex].frStatus))				// IOUSBFamily hasn't gotten here yet
	{
		// Log unusual status here
		if (		(!(mShouldStop))
				&&	(		(kIOReturnSuccess != pFrames[usbFrameIndex].frStatus)
				&&	(		(kIOReturnUnderrun != pFrames[usbFrameIndex].frStatus)
						||	(pFrames[usbFrameIndex].frActCount < (mAverageFrameSize - 2 * mSampleSize))))) // [rdar://5889101]
		{
			debugIOLog ("! AppleUSBAudioStream[%p]::CoalesceInputSamples () - encountered unusual frame with status 0x%x in frame list %lu", this, pFrames[usbFrameIndex].frStatus, usbFrameIndex);
			debugIOLog ("     pFrames[%lu].frStatus = 0x%x", usbFrameIndex, pFrames[usbFrameIndex].frStatus);
			debugIOLog ("     pFrames[%lu].frReqCount = %lu", usbFrameIndex, pFrames[usbFrameIndex].frReqCount);
			debugIOLog ("     pFrames[%lu].frActCount = %lu", usbFrameIndex, pFrames[usbFrameIndex].frActCount);
			debugIOLog ("     pFrames[%lu].frTimeStamp = 0x%x", usbFrameIndex, (* (UInt64 *) &(pFrames[usbFrameIndex].frTimeStamp)));
			// break;
			// <rdar://6902105>, <rdar://6411577> Workaround for issue where the device sends more data than it should. 
			// This causes overruns and the USB host controller may indicate that the frActCount is zero (different 
			// host controller behaves differently).
			if ( ( kIOReturnOverrun == pFrames[usbFrameIndex].frStatus ) && ( 0 == pFrames[usbFrameIndex].frActCount ) )
			{
				// Set the frActCount to frReqCount so that at least the timing is somewhat preserved and not dropping the
				// whole packet.
				pFrames[usbFrameIndex].frActCount = pFrames[usbFrameIndex].frReqCount;
				mOverrunsCount++;
				
				// If there is too many overruns, the audio stream is possibly corrupt constantly, so restart the 
				// audio engine if the engine has multiple streams and this input stream is the master stream. This
				// is to prevent the continous corruptions.
				if ( mMasterMode && ( mOverrunsCount >= mOverrunsThreshold ) )
				{
					if ( mUSBAudioDevice && mUSBAudioEngine && mUSBAudioEngine->mIOAudioStreamArray )
					{
						if ( 1 < mUSBAudioEngine->mIOAudioStreamArray->getCount() )
						{
							// Reset the engine to prevent constant corruption.
							mUSBAudioDevice->setShouldResetEngine ( mUSBAudioEngine );
						}
					}
				}
			}
		}
		
		numBytesToEnd = getSampleBufferSize () - mBufferOffset;
		
		// We should take the first time stamp now if we are receiving our first byte when we expect; otherwise wait until the first buffer loop.
		if	(		(!mHaveTakenFirstTimeStamp)
				&&	(0 == mBufferOffset)
				&&	(pFrames[usbFrameIndex].frActCount > 0))
		{
			if ( mMasterMode && !mShouldStop )										// <rdar://problem/7378275>
			{
				debugIOLog ("? AppleUSBAudioStream::CoalesceInputSamples () - Taking first time stamp.");
				time = generateTimeStamp ( ( ( SInt32 )usbFrameIndex ) - 1, 0, 0);	// <rdar://problem/7378275>
				takeTimeStamp (false, &time);
			}
		}
		
		if ((UInt32)(pFrames[usbFrameIndex].frActCount) >= numBytesToEnd) 			// <rdar://problem/7378275>
		{
			// This copy will wrap
			numBytesToCopy = numBytesToEnd;
			
			// Store numbers for time stamping
			preWrapBytes = numBytesToEnd;
			byteCount = pFrames[usbFrameIndex].frActCount;
		} 
		else 
		{
			numBytesToCopy = pFrames[usbFrameIndex].frActCount;
			if (0 == numBytesToCoalesce) 
			{
				pFrames[usbFrameIndex].frActCount = 0;
				#ifdef DEBUG
				// We don't want to see these frames logged as errors later, so cook the error code if necessary.
				if	(kIOReturnUnderrun == pFrames[usbFrameIndex].frStatus)
				{ 
					pFrames[usbFrameIndex].frStatus = kIOReturnSuccess;
				}
				#endif DEBUG
			}
		}
#if DEBUGINPUT
		// <rdar://problem/7378275>
		if ( pFrames[usbFrameIndex].frActCount >= numBytesLeft )
		{
			numBytesOnLastCopy = numBytesLeft;
			usbFrameIndexOnLastCopy = usbFrameIndex;
			firstUSBFrameIndexOnLastCopy = firstUSBFrameIndex;
			pFramesOnLastCopy = pFrames;
		}
#endif		
		if (0 != numBytesToCopy)
		{
			memcpy (dest, source, numBytesToCopy);
			mBufferOffset 	+= numBytesToCopy;
			numBytesLeft 	-= numBytesToCopy;
		}
		numBytesCopied 	= numBytesToCopy;

		if (pFrames[usbFrameIndex].frActCount >= numBytesToEnd) 				// <rdar://problem/7378275>
		{
			numBytesToCopy = pFrames[usbFrameIndex].frActCount - numBytesToEnd;
			dest = (UInt8 *)getSampleBuffer ();
			memcpy (dest, source + numBytesCopied, numBytesToCopy);
			mBufferOffset = numBytesToCopy;
			numBytesLeft -= numBytesToCopy;

			if (0 == numBytesToCoalesce) 
			{
				if ( mMasterMode && !mShouldStop )								// <rdar://problem/7378275>
				{
					// we have wrapped and we were called by the completion routine -- take timestamp
					// Calculate a time stamp based on our filtered rates
					time = generateTimeStamp (usbFrameIndex, preWrapBytes, byteCount);
					takeTimeStamp (TRUE, &time);
				}
			}
		}

		dest += numBytesToCopy;
		source += pFrames[usbFrameIndex].frReqCount;
		usbFrameIndex++;
		numFramesChecked++;
		//	<rdar://6094454> Use the pre-computed value of firstUSBFrameIndex and totalNumUSBFrames. 
		//	firstUSBFrameIndex should be tied to pFrames, so it should only change when pFrames changes 
		//	in the wrap situation. totalNumUSBFrames shouldn't change at all.
		if (0 != numBytesToCoalesce && (usbFrameIndex + firstUSBFrameIndex) == totalNumUSBFrames)
		{
			pFrames = &mUSBIsocFrames[0];			// wrap around the frame list and keep trying to coalesce
			usbFrameIndex = 0;
			firstUSBFrameIndex = 0;					// <rdar://6094454> Start at frame# 0.
			source = (UInt8 *)mReadBuffer;
			//debugIOLog ("wrapping coalesce numBytesToCoalesce = %d", numBytesToCoalesce); 
		}
		if (((0 == numBytesToCoalesce) && (mNumTransactionsPerList == usbFrameIndex)) ||		// We've coalesced the current frame list
			((0 != numBytesToCoalesce) && (0 >= numBytesLeft)) ||							// We've coalesced the requested number of bytes
			((0 != numBytesToCoalesce) && (numFramesChecked >= (mNumTransactionsPerList * mNumUSBFrameLists)))) // We've gone through all the frame lists and there's nothing left to coalesce (starvation case)
		{			
			done = TRUE;
#if DEBUGINPUT
			// <rdar://problem/7378275>
			if ( ( 0 != numBytesToCoalesce ) && ( 0 >= numBytesLeft ) )
			{
				UInt32 actualCount = 0;
				while ( ( 'llit' != pFramesOnLastCopy[usbFrameIndexOnLastCopy].frStatus )			// IOUSBFamily is processing this now
					   && ( -1 != pFramesOnLastCopy[usbFrameIndexOnLastCopy].frStatus ) )
				{
					actualCount += pFramesOnLastCopy[usbFrameIndexOnLastCopy].frActCount;
					usbFrameIndexOnLastCopy++;
					if ( ( usbFrameIndexOnLastCopy + firstUSBFrameIndexOnLastCopy ) == totalNumUSBFrames )
					{
						pFramesOnLastCopy = &mUSBIsocFrames[0];			// wrap around the frame list
						usbFrameIndexOnLastCopy = 0;
						firstUSBFrameIndexOnLastCopy = 0;					// Start at frame# 0.
					}
				}
				
				if ( actualCount > 0 )
				{
					numBytesMargin = actualCount - numBytesOnLastCopy;
					debugIOLog ("! AppleUSBAudioStream[%p]::CoalesceInputSamples () - numBytesMargin: %lu frames: %lu\n", this, numBytesMargin, numBytesMargin/mSampleSize );
				}
			}
#endif
		}
	}

	if (0 != numBytesToCoalesce) 
	{
		mBufferOffset = originalBufferOffset;
	}

	// Log here if we are requesting more bytes than is possible to coalesce in mNumTransactionsPerList.
	if (		( 0 != numBytesToCoalesce )
			&&	( numBytesLeft > 0 )
			&&  ( NULL != mStreamInterface ) )
	{
		debugIOLog ("! AppleUSBAudioStream[%p]::CoalesceInputSamples () - Requested: %lu, Remaining: %lu on frame list %lu\n", this, numBytesToCoalesce, numBytesLeft, mCurrentFrameList);
	}

	#if DEBUGINPUT
	debugIOLog ("- AppleUSBAudioStream[%p]::CoalesceInputSamples (%lu, %p)", this, numBytesToCoalesce, pFrames);
	#endif
	
	if (mCoalescenceMutex)
	{
		IORecursiveLockUnlock (mCoalescenceMutex);		// <rdar://problem/7378275>
	}
	
	if ( kIOReturnSuccess != result )
	{
		debugIOLog ( "! AppleUSBAudioStream[%p]::CoalesceInputSamples (%lu, %p) = 0x%x", this, numBytesToCoalesce, pFrames, result );
	}
	return result;
}

// [rdar://3918719] The following method now does the work of performFormatChange after being regulated by AppleUSBAudioDevice::formatChangeController().
IOReturn AppleUSBAudioStream::controlledFormatChange (const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate)
{
	IOReturn							result;
	AUAConfigurationDictionary *		configDictionary = NULL;
    void *								sampleBuffer;
	UInt32								i;
	UInt32								numSamplesInBuffer;
	UInt16								averageFrameSamples;
	UInt16								additionalSampleFrameFreq;
	UInt16								maxPacketSize;
	UInt8								endpointAddress;
	UInt8								interval;
	UInt8								newAlternateSettingID;
	UInt8								newDirection;
	UInt8								oldTransactionsPerFrame;
	bool								needToChangeChannels;
	UInt32								remainder;								// <rdar://problem/6954295>
	IOAudioSampleRate					sampleRate;								//<rdar://6945472>
	bool								needToUpdateStampDifference = false;	// <rdar://problem/7378275>
	

	debugIOLog ("+ AppleUSBAudioStream[%p]::controlledFormatChange (%p, %p)", this, newFormat, newSampleRate);

	result = kIOReturnError;

	FailIf (NULL == mStreamInterface, Exit);
	FailIf (NULL == mUSBAudioDevice, Exit);	
	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);
	FailIf (NULL == newFormat, Exit);
	
	// Can't rely on the driver tag to be correct because IOAudioFamily only looks for formats without respect to sample rate,
	// but it's an optimization in the general case.
	mInterfaceNumber = (UInt8)(newFormat->fDriverTag >> 16);
	newAlternateSettingID = (UInt8)(newFormat->fDriverTag);
	
	if (newFormat->fNumChannels != this->format.fNumChannels) 
	{
		needToChangeChannels = TRUE;
		debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - Need to adjust channel controls (cur = %d, new = %d)", this, this->format.fNumChannels, newFormat->fNumChannels);
		
		if (kIOAudioStreamDirectionOutput == mDirection)
		{
			// check for mono mode
			if (1 == newFormat->fNumChannels)
			{
				mUSBAudioDevice->setMonoState (TRUE);
			}
			else
			{
				mUSBAudioDevice->setMonoState (FALSE);
			}
		}
	} 
	else
	{
		needToChangeChannels = FALSE;
		// debugIOLog ("No need to adjust channel controls.");
	}
	
	if (newSampleRate) 
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - Changing sampling rate to %d", this, newSampleRate->whole);
		sampleRate = *newSampleRate;	//	<rdar://6945472>
		needToUpdateStampDifference = true;	// <rdar://problem/7378275>
	} 
	else 
	{
		// debugIOLog ("Keeping existing sampling rate of %d", mCurSampleRate.whole);
		sampleRate = mCurSampleRate;	//	<rdar://6945472>
	}

	if (false == configDictionary->verifySampleRateIsSupported (mInterfaceNumber, newAlternateSettingID, sampleRate.whole))		//	<rdar://6945472>
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - %d channel %d bit @ %d Hz is not supported by alternate setting %d.", this,
					newFormat->fNumChannels, newFormat->fBitDepth, sampleRate.whole, newAlternateSettingID);
		FailIf (kIOReturnSuccess != configDictionary->getAltSettingWithSettings (&newAlternateSettingID, mInterfaceNumber, newFormat->fNumChannels, newFormat->fBitDepth, sampleRate.whole), Exit);
	}

	FailIf (kIOReturnSuccess != configDictionary->getIsocEndpointDirection (&newDirection, mInterfaceNumber, newAlternateSettingID), Exit);
	FailIf (newDirection != mDirection, Exit);

	// Set the sampling rate on the device [rdar://4867843], <rdar://6945472>
	if (IP_VERSION_02_00 == mStreamInterface->GetInterfaceProtocol())
	{
		//	<rdar://5811247>
		OSArray * pathArray = NULL;
		if ( NULL != mUSBAudioEngine->mClockSelectorControl && 0 != mUSBAudioEngine->mCurrentClockPathIndex )
		{
			// The clock source to use is dependent on what the clock selector is set to. Ask the engine what
			// is the current clock selector is pointed to, and use that to set the sample rate.
			UInt32 pathIndex = mUSBAudioEngine->mCurrentClockPathIndex;
			FailIf ( 0 == pathIndex, Exit );
			OSArray * clockPathGroup = mUSBAudioDevice->getClockPathGroup ( mInterfaceNumber, newAlternateSettingID );
			FailIf ( NULL == clockPathGroup, Exit );
			pathArray = OSDynamicCast ( OSArray, clockPathGroup->getObject ( pathIndex - 1 ) );
		}
		else
		{
			pathArray = mUSBAudioDevice->getOptimalClockPath ( mUSBAudioEngine, mInterfaceNumber, newAlternateSettingID, sampleRate.whole, NULL );
		}
		FailIf ( NULL == pathArray, Exit );			
		FailIf ( kIOReturnSuccess != mUSBAudioDevice->setClockPathCurSampleRate ( sampleRate.whole, pathArray, TRUE ), Exit );

		mActiveClockPath = pathArray;
 	}
	
	mCurSampleRate = sampleRate;
	
	if (mPlugin) 
	{
		mPlugin->pluginSetFormat (newFormat, &mCurSampleRate);
	}
	
	debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - about to set: mInterfaceNumber = %d & newAlternateSettingID = %d", this, mInterfaceNumber, newAlternateSettingID);
	mAlternateSettingID = newAlternateSettingID;

	oldTransactionsPerFrame = mTransactionsPerUSBFrame;
	// [rdar://4801012] Must determine the number of transfer opportunities per millisecond.
	if	(		( IP_VERSION_02_00 == mStreamInterface->GetInterfaceProtocol () )
			&&	( kUSBDeviceSpeedHigh == mUSBAudioDevice->getDeviceSpeed () ) )
	{
		FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointInterval ( &interval, mInterfaceNumber, mAlternateSettingID, mDirection ), Exit );
		if ( 0 == interval )
		{
			debugIOLog ( "! AppleUSBAudioStream[%p]::controlledFormatChange () - ERROR! Isoc endpoint has a refresh interval of 0! Treating as 4 ...", this );
			mTransactionsPerUSBFrame = 1;
		}
		else
		{
			FailIf ( interval > 4, Exit );
			mTransactionsPerUSBFrame = 8 >> ( interval - 1 );
			
		}
	}
	else
	{
		mTransactionsPerUSBFrame = 1;
	}	

	// [rdar://4801012] Now determine the number of transactions per list.
	mNumTransactionsPerList = mNumUSBFramesPerList * mTransactionsPerUSBFrame;
	
	// [rdar://4801012] Allocate the isoc frames if necessary.
	if	(		( NULL != mUSBIsocFrames )
			&&	( oldTransactionsPerFrame != mTransactionsPerUSBFrame ) )
	{
		// Dispose of the current isoc frames.
		IOFree ( mUSBIsocFrames, mNumUSBFrameLists * mNumUSBFramesPerList * oldTransactionsPerFrame	* sizeof ( IOUSBLowLatencyIsocFrame ) );
		mUSBIsocFrames = NULL;
	}
	
	if ( NULL == mUSBIsocFrames )
	{
		mUSBIsocFrames = ( IOUSBLowLatencyIsocFrame * ) IOMalloc ( mNumUSBFrameLists * mNumTransactionsPerList * sizeof ( IOUSBLowLatencyIsocFrame ) );
		FailIf ( 0 == mUSBIsocFrames, Exit );	// <rdar://7568547>

		// <rdar://7568547> Initialize the USB Isoc frames so that CoalesceInputSamples() will not panic
		// due to uninitialized values in frStatus & frActCount.
		initializeUSBFrameList ( mUSBIsocFrames, mNumUSBFrameLists * mNumTransactionsPerList );
	}

	// Set the sampling rate on the endpoint
	if (configDictionary->asEndpointHasSampleFreqControl (mInterfaceNumber, mAlternateSettingID))
	{
		FailIf (kIOReturnSuccess != configDictionary->getIsocEndpointAddress (&endpointAddress, mInterfaceNumber, mAlternateSettingID, mDirection), Exit);
		(void) setSampleRateControl (endpointAddress, mCurSampleRate.whole);		// no need to check the error, it's not a real problem if it doesn't work
	}	
	
	// Set this as the default until we are told otherwise <rdar://problem/6954295>
	// Take the current sample rate (in Hz) and and transform it into samples per packet represented in 16.16 fixed point.  When calculating the  
	// fractional part, store the fraction x 1000 to maintain precision.
	mSamplesPerPacket.whole = mCurSampleRate.whole / ( mTransactionsPerUSBFrame * 1000 );
	remainder = mCurSampleRate.whole - ( mSamplesPerPacket.whole * mTransactionsPerUSBFrame * 1000 );	// same as (mCurSampleRate.whole % 1000) * mTransactionsPerUSBFrame
	mSamplesPerPacket.fraction = ( remainder * 65536 ) / mTransactionsPerUSBFrame;
	debugIOLog ( "? AppleUSBAudioStream[%p]::controlledFormatChange () - mSamplesPerPacket: %u(whole) %u(fraction)", this, mSamplesPerPacket.whole, mSamplesPerPacket.fraction );
	
	calculateSamplesPerPacket (mCurSampleRate.whole, &averageFrameSamples, &additionalSampleFrameFreq);
	debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - averageFrameSamples = %d", this, averageFrameSamples);

	mSampleBitWidth = newFormat->fBitWidth;
	mNumChannels =  newFormat->fNumChannels;
	mSampleSize = newFormat->fNumChannels * (newFormat->fBitWidth / 8);
	mAverageFrameSize = averageFrameSamples * mSampleSize;
	mAlternateFrameSize = (averageFrameSamples + 1) * mSampleSize;
	debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - mAverageFrameSize = %d, mAlternateFrameSize = %d", this, mAverageFrameSize, mAlternateFrameSize);

	mOverrunsThreshold = kOverrunsThreshold;	// <rdar://6411577> Threshold for overruns. 
	
	// You have to make the read buffer the size of the alternate frame size because we have to ask for mAlternateFrameSize bytes
	// with each read.  If you don't make the buffer big enough, you don't get all the data from the last frame...
	// USB says that if the device is running at an even multiple of the bus clock (i.e. 48KHz) that it can send frames that 
	// have +/- one sample (i.e. 47, 48, or 49 samples per frame) from the average.  This differs from when it's not a even 
	// multiple and it can send only +1.5 sample from the average.
	if (kUSBIn == mDirection) 
	{
		// mReadUSBFrameListSize = mAlternateFrameSize * mNumTransactionsPerList;
		// [rdar://5355808] [rdar://5889101] Be a little more lenient than the spec dictates to accommodate ill-behaved devices if possible.
		FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointMaxPacketSize ( &maxPacketSize, mInterfaceNumber, mAlternateSettingID, mDirection ), Exit );
		mReadUSBFrameListSize = ( ( mAlternateFrameSize + 2 * mSampleSize ) < maxPacketSize ) ? ( mAlternateFrameSize + 2 * mSampleSize ) : maxPacketSize; 
		mReadUSBFrameListSize *= mNumTransactionsPerList;
	}

	// Need a minimum of two pages in OHCI/UHCI
	numSamplesInBuffer = mCurSampleRate.whole / 4;	// <rdar://problem/6954295>
	numSamplesInBuffer += (PAGE_SIZE*2 - 1);
	numSamplesInBuffer /= PAGE_SIZE*2;
	numSamplesInBuffer *= PAGE_SIZE*2;
	mSampleBufferSize = numSamplesInBuffer * mSampleSize;
	if ( ( mUHCISupport ) && ( kUSBIn != mDirection ) )		//	<rdar://6564854> For output sample buffer only.
	{
		mSampleBufferSizeExtended = mSampleBufferSize + PAGE_SIZE;
	}
	else
	{
		mSampleBufferSizeExtended = mSampleBufferSize;
	}
	
	debugIOLog("? AppleUSBAudioStream[%p]::controlledFormatChange () - New mSampleBufferSize = %d numSamplesInBuffer = %d", this, mSampleBufferSize, numSamplesInBuffer );

	// <rdar://problem/7378275>
	if ( needToUpdateStampDifference || mInitStampDifference )
	{
		// Prime the filter with the nominal sample rate.
		mFilterWritePointer = 0;
		mLastFilteredStampDifference = jitterFilter ( ( 1000000000ull * numSamplesInBuffer ) / mCurSampleRate.whole, 0 );
		mNumTimestamp = 1;
		mInitStampDifference = false;
	}
	
	if (NULL != mSampleBufferDescriptors) 
	{
		for (i = 0; i < mNumUSBFrameLists; i++) 
		{
			if (NULL != mSampleBufferDescriptors[i]) 
			{
				mSampleBufferDescriptors[i]->release ();
				mSampleBufferDescriptors[i] = NULL;
			}
		}
	}

	if (kUSBIn == mDirection) 
	{
		if (NULL != mReadBuffer) 
		{
			mUSBBufferDescriptor->release ();
		}
		
		mUSBBufferDescriptor = allocateBufferDescriptor (kIODirectionIn, mNumUSBFrameLists * mReadUSBFrameListSize, PAGE_SIZE);
		
		FailIf (NULL == mUSBBufferDescriptor, Exit);
		mReadBuffer = mUSBBufferDescriptor->getBytesNoCopy ();
		FailIf (NULL == mReadBuffer, Exit);

		for (i = 0; i < mNumUSBFrameLists; i++) 
		{
			mSampleBufferDescriptors[i] = OSTypeAlloc (IOSubMemoryDescriptor);
			mSampleBufferDescriptors[i]->initSubRange (mUSBBufferDescriptor, i * mReadUSBFrameListSize, mReadUSBFrameListSize, kIODirectionIn);
			FailIf (NULL == mSampleBufferDescriptors[i], Exit);
		}

		if (NULL != mSampleBufferMemoryDescriptor) 
		{
			this->setSampleBuffer (NULL, 0);
			mSampleBufferMemoryDescriptor->release ();
		}
		
		mSampleBufferMemoryDescriptor = IOBufferMemoryDescriptor::withOptions (kIODirectionInOut, mSampleBufferSize, PAGE_SIZE);
		FailIf (NULL == mSampleBufferMemoryDescriptor, Exit);
		sampleBuffer = mSampleBufferMemoryDescriptor->getBytesNoCopy ();
	} 
	else 
	{
		// This is the output case.		
		if (NULL != mUSBBufferDescriptor) 
		{
			this->setSampleBuffer (NULL, 0);
			mUSBBufferDescriptor->release ();
		}
		if (mUHCISupport)
		{
			// Allocate an additional alternate frame size (mSampleBufferSizeExtended total) as our scribble ahead for frames that would be wrapped in clipOutputSamples
			mUSBBufferDescriptor = allocateBufferDescriptor (kIODirectionOut, mSampleBufferSizeExtended, PAGE_SIZE);
		}
		else
		{
			mUSBBufferDescriptor = allocateBufferDescriptor (kIODirectionOut, mSampleBufferSize, PAGE_SIZE);
		}
		FailIf (NULL == mUSBBufferDescriptor, Exit);

		for (i = 0; i < mNumUSBFrameLists; i++) 
		{
			if (NULL != mSampleBufferDescriptors[i]) 
			{
				mSampleBufferDescriptors[i]->release ();
			}
			mSampleBufferDescriptors[i] = OSTypeAlloc (IOSubMemoryDescriptor);
			mSampleBufferDescriptors[i]->initSubRange (mUSBBufferDescriptor, 0, mSampleBufferSize, kIODirectionOut);
			FailIf (NULL == mSampleBufferDescriptors[i], Exit);

			result = mSampleBufferDescriptors[i]->prepare();
			FailIf (kIOReturnSuccess != result, Exit);
		}

		sampleBuffer = mUSBBufferDescriptor->getBytesNoCopy();
		FailIf (NULL == sampleBuffer, Exit);
	}
	
	this->setSampleBuffer (sampleBuffer, mSampleBufferSize);

	updateSampleOffsetAndLatency ();
	
	debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - Called setNumSampleFramesPerBuffer with %d", this, mSampleBufferSize / (mSampleSize ? mSampleSize : 1));
	debugIOLog ("? AppleUSBAudioStream[%p]::controlledFormatChange () - newFormat->fNumChannels = %d, newFormat->fBitWidth %d", this, newFormat->fNumChannels, newFormat->fBitWidth);

	result = kIOReturnSuccess;

Exit:
	debugIOLog ("- AppleUSBAudioStream[%p]::controlledFormatChange () = 0x%x", this, result);
    return result;

}

void AppleUSBAudioStream::updateSampleOffsetAndLatency ()
{
	UInt32								minimumSafeSampleOffset;
	UInt32								cautiousSafeSampleOffset;
	UInt16								averageFrameSamples;
	UInt16								additionalSampleFrameFreq;
	bool								highSpeedCompensation;
	OSDictionary *						sampleOffsetDictionary = NULL;
	OSDictionary *						sampleLatencyDictionary = NULL;
	UInt32								newSampleOffset;
	UInt32								newSampleLatency;	

	// <rdar://problem/7378275>
	averageFrameSamples = mCurSampleRate.whole / 1000; // per ms
	additionalSampleFrameFreq = mCurSampleRate.whole - ( averageFrameSamples * 1000 );

	if (kUSBIn == mDirection) 
	{
		// Check to see if latency should be higher for EHCI (rdar://3959606 ) 
		if (mSplitTransactions)
		{
			debugIOLog ("? AppleUSBAudioStream[%p]::updateSampleOffsetAndLatency () - Compensating for high speed timing difference in sample offset", this);
			highSpeedCompensation = true;
		}
		else
		{
			highSpeedCompensation = false;
		}
		minimumSafeSampleOffset = averageFrameSamples + 1;
		cautiousSafeSampleOffset = minimumSafeSampleOffset + (minimumSafeSampleOffset / kUSBInputRecoveryTimeFraction);
		
		newSampleOffset = cautiousSafeSampleOffset;
		
		//	<rdar://6343818> Adjust the safety offset by 1.875ms to compensate for the time stamps generation 
		//	in the output stream when both the input & output streams are on the same engine. 
		if ( mSyncCompensation )
		{
			debugIOLog ("? AppleUSBAudioStream[%p]::updateSampleOffsetAndLatency () - Compensating for time stamp generation in sample offset", this);
			newSampleOffset += cautiousSafeSampleOffset * 3 / 2;
		}
		
		// Check to see if there is an override in the vendor specific kext.
		sampleOffsetDictionary = OSDynamicCast ( OSDictionary, mStreamInterface->getProperty ( kIOAudioEngineInputSampleOffsetKey ) );
		if (NULL == sampleOffsetDictionary)
		{
			sampleOffsetDictionary = OSDynamicCast ( OSDictionary, mStreamInterface->getProperty ( kIOAudioEngineSampleOffsetKey ) );
		}
		if (NULL != sampleOffsetDictionary)
		{
			char sampleOffsetKeyString[16];
			snprintf (sampleOffsetKeyString, 16, "%d", mCurSampleRate.whole);
			
			OSNumber * sampleOffset = OSDynamicCast ( OSNumber, sampleOffsetDictionary->getObject ( sampleOffsetKeyString ) );
			if ( NULL != sampleOffset )
			{				
				debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - override input sample offset (%lu) to %lu sample frames", this, newSampleOffset, sampleOffset->unsigned32BitValue ());
				newSampleOffset = sampleOffset->unsigned32BitValue ();
			}
		}

		// Add an extra frame and a half of samples to the offset if going through a USB 2.0 hub.
		newSampleOffset += (highSpeedCompensation ? (5 * minimumSafeSampleOffset / 3) : 0);
		
		// Set the offset for input devices (microphones, etc.)
		mUSBAudioEngine->setInputSampleOffset (newSampleOffset);
		debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - setting input sample offset to %lu sample frames", this, newSampleOffset);
		
		newSampleLatency = averageFrameSamples * (/* kMinimumFrameOffset + */ 1);
		
		// Check to see if there is an override in the vendor specific kext.
		sampleLatencyDictionary = OSDynamicCast ( OSDictionary, mStreamInterface->getProperty ( kIOAudioEngineInputSampleLatencyKey ) );
		if (NULL != sampleLatencyDictionary)
		{
			char sampleLatencyKeyString[16];
			snprintf (sampleLatencyKeyString, 16, "%d", mCurSampleRate.whole);
			
			OSNumber * sampleLatency = OSDynamicCast ( OSNumber, sampleLatencyDictionary->getObject ( sampleLatencyKeyString ) );
			if ( NULL != sampleLatency )
			{				
				debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - override input sample latency (%lu) to %lu sample frames", this, newSampleLatency, sampleLatency->unsigned32BitValue ());
				newSampleLatency = sampleLatency->unsigned32BitValue ();
			}
		}
		
		// setSampleLatency chosen via heuristics
		mUSBAudioEngine->setInputSampleLatency (newSampleLatency);
		debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - setting input sample latency to %lu sample frames", this, newSampleLatency);
	} 
	else 
	{
		// Output case.
		cautiousSafeSampleOffset =  averageFrameSamples + 1;
		
		//	<rdar://6343818> Adjust the safety offset by + 0.5ms to compensate for the time stamps generation 
		//	in the input stream when both the input & output streams are on the same engine. 
		if ( mSyncCompensation )
		{
			debugIOLog ("? AppleUSBAudioStream[%p]::updateSampleOffsetAndLatency () - Compensating for time stamp generation in sample offset", this);
			minimumSafeSampleOffset = cautiousSafeSampleOffset;
		}
		else
		{
			minimumSafeSampleOffset = cautiousSafeSampleOffset / 2;
		}

		newSampleOffset = minimumSafeSampleOffset;
		
		// Check to see if there is an override in the vendor specific kext.
		sampleOffsetDictionary = OSDynamicCast ( OSDictionary, mStreamInterface->getProperty ( kIOAudioEngineSampleOffsetKey ) );
		if (NULL != sampleOffsetDictionary)
		{
			char sampleOffsetKeyString[16];
			snprintf (sampleOffsetKeyString, 16, "%d", mCurSampleRate.whole);
			
			OSNumber * sampleOffset = OSDynamicCast ( OSNumber, sampleOffsetDictionary->getObject ( sampleOffsetKeyString ) );
			if ( NULL != sampleOffset )
			{				
				debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - override output sample offset (%lu) to %lu sample frames", this, newSampleOffset, sampleOffset->unsigned32BitValue ());
				newSampleOffset = sampleOffset->unsigned32BitValue ();
			}
		}

		// Set the offset for output devices (speakers, etc.) to 1 USB frame (+1 ms to latency). This is necessary to ensure that samples are not clipped 
		// to a portion of the buffer whose DMA is in process.
		mUSBAudioEngine->setOutputSampleOffset (newSampleOffset);
		debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - setting output sample offset to %lu sample frames", this, newSampleOffset);
		
		newSampleLatency = additionalSampleFrameFreq ? averageFrameSamples + 1 : averageFrameSamples;
		
		// Check to see if there is an override in the vendor specific kext.
		sampleLatencyDictionary = OSDynamicCast ( OSDictionary, mStreamInterface->getProperty ( kIOAudioEngineOutputSampleLatencyKey ) );
		if (NULL != sampleLatencyDictionary)
		{
			char sampleLatencyKeyString[16];
			snprintf (sampleLatencyKeyString, 16, "%d", mCurSampleRate.whole);
			
			OSNumber * sampleLatency = OSDynamicCast ( OSNumber, sampleLatencyDictionary->getObject ( sampleLatencyKeyString ) );
			if ( NULL != sampleLatency )
			{				
				debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - override output sample latency (%lu) to %lu sample frames", this, newSampleLatency, sampleLatency->unsigned32BitValue ());
				newSampleLatency = sampleLatency->unsigned32BitValue ();
			}
		}
		
		// setSampleLatency chosen via heuristics 
		mUSBAudioEngine->setOutputSampleLatency (newSampleLatency);
		debugIOLog ("? AppleUSBAudioEngine[%p]::updateSampleOffsetAndLatency () - setting output sample latency to %lu sample frames", this, newSampleLatency);
	}
}

// <rdar://problem/7378275> Improved timestamp generation accuracy
IOReturn AppleUSBAudioStream::copyAnchor (UInt64 anchorFrame, AbsoluteTime * anchorTime, UInt64 * usbCycleTime)
{
	IOReturn		result = kIOReturnError;
	UInt64			anchorTime_nanos;
	
	FailIf (NULL == mUSBAudioDevice, Exit);
	FailIf (NULL == mUSBAudioDevice->mTimeLock, Exit);
	
	IOLockLock ( mUSBAudioDevice->mTimeLock );
	
	anchorTime_nanos = mUSBAudioDevice->getTimeForFrameNumber(anchorFrame);
	nanoseconds_to_absolutetime( anchorTime_nanos, anchorTime );
	*usbCycleTime = mUSBAudioDevice->mWallTimePerUSBCycle;
	
	IOLockUnlock ( mUSBAudioDevice->mTimeLock );
	
	result = kIOReturnSuccess;
	
Exit:
	return result;
}

// <rdar://problem/7378275> Improved timestamp generation accuracy
// 32-tap FIR
// nIter is the iteration number. It should begin at zero and continue increasing (up to the value of nFilterSize)
// If the timestamps are stopped and then restarted, the nIter value should reset to zero to ensure the filter starts up correctly.
UInt64 AppleUSBAudioStream::jitterFilter (UInt64 curr, UInt32 nIter) 
{
	const UInt64 filterCoefficients[] = {1, 2, 4, 7, 10, 14, 19, 25, 31, 37, 43, 49, 54, 58, 62, 64, 64, 64, 62, 58, 54, 49, 43, 37, 31, 25, 19, 14, 10, 7, 4, 2, 1};
	const UInt64 filterCoefficientsSmall[] = {256, 256, 256, 256};
	UInt64 result = 0;
	
	// On the first iteration, initialise all the data with the first coefficient, otherwise, instert in the circular array
	if ( 0 == nIter )
	{
		for ( UInt32 filterIndex = 0; filterIndex < kMaxFilterSize; filterIndex++ )
		{
			mFilterData [ filterIndex ] = curr;
		}
	}
	else
	{
		mFilterData [ mFilterWritePointer ] = curr;
	}
	
	// Calculate filter output - if we are just starting up, use the smaller filter, otherwise use the larger filter with increased attenuation
	if ( nIter < kMaxFilterSize )
	{
		for ( UInt32 filterIndex = 0; filterIndex < ( sizeof ( filterCoefficientsSmall ) / sizeof ( filterCoefficientsSmall [0] ) ); filterIndex++ )
		{
			result += filterCoefficientsSmall [ filterIndex ] * mFilterData [ (kMaxFilterSize + mFilterWritePointer - filterIndex ) % kMaxFilterSize ];
		}
	}
	else
	{
		for ( UInt32 filterIndex = 0; filterIndex < kMaxFilterSize; filterIndex++ )
		{
			result += filterCoefficients [ filterIndex ] * mFilterData [ ( kMaxFilterSize + mFilterWritePointer - filterIndex ) % kMaxFilterSize ];
		}
	}
	
	result += kFilterScale / 2;
	result /= kFilterScale;
	
	// Update the write pointer for the next iteration
	mFilterWritePointer = ( kMaxFilterSize + mFilterWritePointer + 1 ) % kMaxFilterSize;
	
	return result;
}

// <rdar://problem/6354240> Timestamp calculation is incorrect when there is more than one transaction per USB frame
// <rdar://problem/7378275> Improved timestamp generation accuracy
AbsoluteTime AppleUSBAudioStream::generateTimeStamp (SInt32 transactionIndex, UInt32 preWrapBytes, UInt32 byteCount)
{
	UInt64			raw_time_nanos = 0ull;
	UInt64			filtered_time_nanos = 0ull;
	UInt64			referenceWallTime_nanos = 0ull;
	AbsoluteTime	anchorTime;
	AbsoluteTime	time;
	UInt64			usbCycleTime;
	SInt64			rawStampDifference = 0;
	SInt64			filteredStampDifference = 0;
#if DEBUGTIMESTAMPS
	UInt64			frameDifference = 0;
	UInt64			sampleRate = 0;
	SInt64			stampJitter = 0;
#endif
	UInt64			thisFrameNum;
	UInt32			divisor;
	UInt32			remainingFullTransactions = 0ul;
	UInt32			partialFrame = 0;
	UInt32			numOutstandingTransactions = 0;
	UInt32			numOutStandingUSBFrames = 0;
		
	FailIf ( NULL == mFrameQueuedForList, Exit );
	FailIf ( NULL == mUSBAudioDevice, Exit );
	FailIf ( 0 == mTransactionsPerUSBFrame, Exit );
	
	// In the future, we could remove the increment/decrement adjustments to the numOutstandingTransactions if we fix PrepareWriteFrameList() (or the writeHandler()) 
	// to account for pre-wrap bytes, as it is currently done for input
	numOutstandingTransactions = transactionIndex + 1;
	
	if ( 0 != preWrapBytes )
	{
		numOutstandingTransactions--;	// <rdar://5192321>
#if DEBUGTIMESTAMPS
		debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp () - prewrap not zero, value: %lu", this, preWrapBytes );
#endif
	}
	
	numOutStandingUSBFrames = numOutstandingTransactions / mTransactionsPerUSBFrame;
	
	// <rdar://problem/6328817> Fixed the calculation of remainingFullTransactions that was broken in cases where there is more than one transaction per USB frame.	
	remainingFullTransactions = numOutstandingTransactions - ( numOutStandingUSBFrames * mTransactionsPerUSBFrame );
		
	thisFrameNum = mFrameQueuedForList[mCurrentFrameList] + numOutStandingUSBFrames;
#if DEBUGTIMESTAMPS		
	debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp () - thisFrameNum = %llu, transactionIndex: %d numOutstandingTransactions: %lu numOutStandingUSBFrames: %lu remainingFullTransactions: %lu", this, thisFrameNum, transactionIndex, numOutstandingTransactions, numOutStandingUSBFrames, remainingFullTransactions ); 
#endif	
	FailIf ( kIOReturnSuccess != copyAnchor ( thisFrameNum, &anchorTime, &usbCycleTime ), Exit ); // always use this frame as the anchor frame
		
	// The following code seeks to implement the following equations (though in the code below the original equation is obfuscated by algebra used to defer
	// division for as long as possible to increase precision. Quotient terms involving byteCount and remainingFullTransactions are only included when these 
	// respective variables are nonzero.
	//
	//	[ time = anchorTime + wallTimePerUSBCycle * ( remainingFullTransactions   +              preWrapBytes               ) ]
	//	[                                             --------------------------     ---------------------------------------  ]
	//	[                                              transactionsPerUSBFrame       ( transactionsPerUSBFrame * byteCount )  ]
	//

	partialFrame = ( ( mTransactionsPerUSBFrame != 1 ) && ( byteCount ) ) ? ( byteCount * remainingFullTransactions ) : 0;
	
	divisor = byteCount ? ( byteCount * mTransactionsPerUSBFrame ) : mTransactionsPerUSBFrame; 

	raw_time_nanos = partialFrame + preWrapBytes;
	
#if DEBUGTIMESTAMPS
	debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp () - partialFrame: %lu, preWrapBytes: %lu usbCycleTime: %llu", this, partialFrame, preWrapBytes, usbCycleTime );
#endif	
	// raw_time_nanos now represents the time at which the byte in question should have begun transfer. In the case of input, we won't have access to this byte
	// until one USB frame later.
	if	(getDirection () == kIOAudioStreamDirectionInput)
	{
		raw_time_nanos += divisor;
	}	

	// [rdar://5178614] Divide this into two operations to prevent roundoff error.
	raw_time_nanos *= usbCycleTime;
		
	raw_time_nanos /= ( kWallTimeExtraPrecision * divisor );
	
	absolutetime_to_nanoseconds (anchorTime, &referenceWallTime_nanos);
	raw_time_nanos += referenceWallTime_nanos;
#if DEBUGTIMESTAMPS	
	debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp () - time_nanos before filter: %llu", this, raw_time_nanos );
#endif	
	filtered_time_nanos = raw_time_nanos;
	
	if (0ull != mLastRawTimeStamp_nanos)
	{
#if DEBUGTIMESTAMPS
		if ( 0ull != mLastWrapFrame )
		{
			frameDifference = thisFrameNum - mLastWrapFrame;
		}
#endif		
		rawStampDifference = raw_time_nanos - mLastRawTimeStamp_nanos;

		filteredStampDifference = jitterFilter ( rawStampDifference, mNumTimestamp );
		
		mNumTimestamp++;
		
		filtered_time_nanos = mLastFilteredTimeStamp_nanos + filteredStampDifference;
			
#if DEBUGTIMESTAMPS
#define MAGNITUDEOF( x ) ( ( ( x ) > 0 ) ? ( x ) : ( - ( x ) ) )
			
		if ( 0ull != mLastFilteredStampDifference )
		{
			stampJitter = filteredStampDifference - mLastFilteredStampDifference;
		}
		
		sampleRate = ( 1000000000ull * ( mSampleBufferSize / mSampleSize ) * 1000 ) / filteredStampDifference;
		mStampDrift += stampJitter;
		debugIOLog ( "   transactionIndex = %lu, remainingFullTransactions = %lu, preWrapBytes = %u, byteCount = %u", transactionIndex, remainingFullTransactions, preWrapBytes, byteCount );
		debugIOLog ( "   frameDifference = %llu, referenceWallTime_nanos = %llu, mWallTimePerUSBCycle = %llu, time = %llu", 
						frameDifference, referenceWallTime_nanos, mUSBAudioDevice->mWallTimePerUSBCycle, filtered_time_nanos );
		debugIOLog ("    thisFrameNum = %llu, anchorTime = %llu", thisFrameNum, anchorTime);
		if	(getDirection () == kIOAudioStreamDirectionInput)
		{
			debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp (I)   stampDifference = %lld, stampJitter = %lld, mStampDrift = %lld, sampleRate = %llu \n", this, filteredStampDifference, stampJitter, mStampDrift, sampleRate );
		}
		else
		{
			debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp (O)   stampDifference = %lld, stampJitter = %lld, mStampDrift = %lld, sampleRate =                  %llu \n", this, filteredStampDifference, stampJitter, mStampDrift, sampleRate );
		}
		
		if (		( MAGNITUDEOF( stampJitter ) > 1000000 )
				&&	( TRUE ) )
		{
			debugIOLog( "\nthisFrameNum = %llu, mFrameQueuedForList = %llu, remainingFullTransactions = %lu", thisFrameNum, mFrameQueuedForList[mCurrentFrameList], remainingFullTransactions );
		}
		
		sampleRate = ( 1000000000ull * ( mSampleBufferSize / mSampleSize ) * 1000 ) / rawStampDifference;
		if	(getDirection () == kIOAudioStreamDirectionInput)
		{
			debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp (RI)   stampDifference = %lld, sampleRate = %llu \n", this, rawStampDifference, sampleRate );
		}
		else
		{
			debugIOLog ( "? AppleUSBAudioStream[%p]::generateTimeStamp (RO)   stampDifference = %lld, sampleRate =                  %llu \n", this, rawStampDifference, sampleRate );
		}
#endif
	}

	//Update references
	mLastRawTimeStamp_nanos = raw_time_nanos;
	mLastFilteredTimeStamp_nanos = filtered_time_nanos;
	if (0ull != filteredStampDifference)
	{
		mLastFilteredStampDifference = filteredStampDifference;
	}
	mLastWrapFrame = thisFrameNum;
	
Exit:
	
	nanoseconds_to_absolutetime (filtered_time_nanos, &time);
	return time;
}

UInt32 AppleUSBAudioStream::getCurrentSampleFrame () {
	const IOAudioStreamFormat			*theFormat;
	UInt32								currentSampleFrame;

	currentSampleFrame = 0;
	theFormat = this->getFormat ();
	if (getDirection () == kIOAudioStreamDirectionOutput) 
	{
		currentSampleFrame = mSafeErasePoint;
	} 
	else 
	{
		currentSampleFrame = (mBufferOffset == mSampleBufferSize ? 0 : mBufferOffset);
	}
	currentSampleFrame /= (theFormat->fNumChannels * (theFormat->fBitWidth / 8));

	return currentSampleFrame;
}

// GetDefaultSettings added for rdar://3866513

IOReturn AppleUSBAudioStream::GetDefaultSettings (UInt8 * altSettingID, IOAudioSampleRate * sampleRate) {
	IOReturn						result;
	UInt16							format;
	UInt8							newAltSettingID;
	IOAudioSampleRate				newSampleRate;
	AUAConfigurationDictionary *	configDictionary = NULL;
	
	debugIOLog ("+ AppleUSBAudioStream[%p]::GetDefaultSettings ()", this);
	result = kIOReturnError;
	newSampleRate.whole = sampleRate->whole;
	newSampleRate.fraction = 0;
	configDictionary = mUSBAudioDevice->getConfigDictionary ();
	
	// The sample rate is passsed in, so all we should do here is to pick the sample format (size, and number of channels),
	// so try 16-bit stereo. If possible, never pick anything other than PCM for default.
	result = configDictionary->getAltSettingWithSettings (&newAltSettingID, mInterfaceNumber, kChannelDepth_STEREO, kBitDepth_16bits, newSampleRate.whole);
	if (		( kIOReturnSuccess != result )
			||	(		( kIOReturnSuccess == configDictionary->getFormat( &format, mInterfaceNumber, newAltSettingID )
					&&	( PCM != format ) ) ) )
	{
		// Didn't have stereo, so try mono
		result = configDictionary->getAltSettingWithSettings (&newAltSettingID, mInterfaceNumber, kChannelDepth_MONO, kBitDepth_16bits, newSampleRate.whole);
	}
	if (		( kIOReturnSuccess != result )
			||	(		( kIOReturnSuccess == configDictionary->getFormat( &format, mInterfaceNumber, newAltSettingID )
					&&	( PCM != format ) ) ) )
	{
		UInt8	numAltSettings = 0;
		if (kIOReturnSuccess == configDictionary->getNumAltSettings (&numAltSettings, mInterfaceNumber))
		{
			bool	startAtZero = configDictionary->alternateSettingZeroCanStream (mInterfaceNumber);
			
			for (UInt8 altSetting = (startAtZero ? 0 : 1); altSetting < numAltSettings; altSetting++)
			{
				// Don't have a mono or stereo 16-bit interface, so try for any format at the given sample rate.
				if (		( configDictionary->verifySampleRateIsSupported (mInterfaceNumber, altSetting, newSampleRate.whole ) )
						&&	( kIOReturnSuccess == configDictionary->getFormat( &format, mInterfaceNumber, altSetting )
						&&  ( PCM == format ) ) )
				{
					newAltSettingID = altSetting;
					result = kIOReturnSuccess;
					break;
				}
			}
		}
	}	

	if (		( kIOReturnSuccess != result )
			||	(		( kIOReturnSuccess == configDictionary->getFormat( &format, mInterfaceNumber, newAltSettingID )
					&&	( PCM != format ) ) ) )
	{
		// Just take the first interface.
		newAltSettingID = configDictionary->alternateSettingZeroCanStream (mInterfaceNumber) ? 0 : 1;
		debugIOLog ("? AppleUSBAudioStream[%p]::GetDefaultSettings () - Taking first available alternate setting (%d)", this, newAltSettingID);
		FailIf (kIOReturnSuccess != (result = configDictionary->getHighestSampleRate (&(newSampleRate.whole), mInterfaceNumber, newAltSettingID)), Exit);;
	}
	debugIOLog ("? AppleUSBAudioStream[%p]::GetDefaultSettings () - Default sample rate is %d", this, newSampleRate.whole);
	debugIOLog ("? AppleUSBAudioStream[%p]::GetDefaultSettings () - Default alternate setting ID is %d", this, newAltSettingID);
	FailIf (0 == newSampleRate.whole, Exit);
	*sampleRate = newSampleRate;
	*altSettingID = newAltSettingID;
	result = kIOReturnSuccess;
	
Exit:
	debugIOLog ("- AppleUSBAudioStream[%p]::GetDefaultSettings (%d, %lu) = 0x%x", this, *altSettingID, sampleRate->whole, result);
	return result;
}

#if DEBUGLATENCY
UInt64 AppleUSBAudioStream::getQueuedFrameForSample (UInt32 sampleFrame)
{
	UInt64	usbFrame = 0ull;
	UInt32	sampleByteOffset;
	UInt32	bufferByteOffset;
	UInt32	bytesToQueuePoint;
	UInt8	frameListNumber;
	UInt8	i;
	
	// debugIOLog ("+ AppleUSBAudioStream::getQueuedFrameForSample (%lu)", sampleFrame);
	FailIf (NULL == mFrameQueuedForList, Exit);
	FailIf (0 == mSampleSize, Exit);
	sampleByteOffset = sampleFrame * mSampleSize;
	bytesToQueuePoint = (mLastPreparedBufferOffset > sampleByteOffset ? 0 : getSampleBufferSize()) + mLastPreparedBufferOffset - sampleByteOffset;
	// debugIOLog ("? AppleUSBAudioStream::getQueuedFrameForSample (%lu) - mLastPreparedBufferOffset = %lu, sampleByteOffset = %lu, bytesToQueuePoint = %lu, sampleBufferSize = %lu", sampleFrame, mLastPreparedBufferOffset, sampleByteOffset, bytesToQueuePoint, getSampleBufferSize());
	
	if (bytesToQueuePoint > mThisFrameListSize + mLastFrameListSize)
	{
		debugIOLog ("? AppleUSBAudioStream::getQueuedFrameForSample (%lu) - sample frame is not queued in the previous two frame lists", sampleFrame);
	}
	else
	{
		// Find the USB frame on which this sample frame is queued to be transmitted on the bus
		if (bytesToQueuePoint <= mThisFrameListSize)
		{
			// This sample frame is queued to go out in the most recently queued frame list (bad if this has been clipped!)
			// Store the initial offset.
			bufferByteOffset = mThisFrameListSize;
			
			// Find the frame list number.
			// frameListNumber = (mNumUSBFrameLists + mCurrentFrameList + mNumUSBFrameListsToQueue - 2) % mNumUSBFrameLists;
			frameListNumber = (mCurrentFrameList + 1) % mNumUSBFrameLists;
			// debugIOLog ("? AppleUSBAudioStream::getQueuedFrameForSample (%lu) - sample frame is queued in *PREVIOUS* frame list (%d) (mThisFrameListSize = %lu, mLastFrameListSize = %lu)", sampleFrame, frameListNumber, mThisFrameListSize, mLastFrameListSize);
		}
		else
		{
			// This sample frame is queued to go out in the least recently queued frame list (expected if this has been clipped)
			// Store the intial offset.
			bufferByteOffset = mThisFrameListSize + mLastFrameListSize;
			
			// Find the frame list number.
			// frameListNumber = (mNumUSBFrameLists + mCurrentFrameList + mNumUSBFrameListsToQueue - 3) % mNumUSBFrameLists;
			frameListNumber = mCurrentFrameList;
			// debugIOLog ("? AppleUSBAudioStream::getQueuedFrameForSample (%lu) - sample frame was queued in frame list %d (mThisFrameListSize = %lu, mLastFrameListSize = %lu)", sampleFrame, frameListNumber, mThisFrameListSize, mLastFrameListSize);
		}
			
		// Get the first byte of the frame list
		bufferByteOffset = (getSampleBufferSize() + mLastPreparedBufferOffset - bufferByteOffset) % getSampleBufferSize();
		// debugIOLog ("? AppleUSBAudioStream::getQueuedFrameForSample (%lu) - frame list %d starting queueing at byte %lu", sampleFrame, frameListNumber, bufferByteOffset); 
			
		// We've already determined in which frame list the sample frame lies and the buffer offset at which we should start. We no longer care about the 
		// actual buffer offset, but we must preserve the order of the buffer offset and the sample byte offset. We can do this by "unrolling" the ring
		// buffer and not marking the wrap any more.
		
		if (bufferByteOffset > sampleByteOffset)
		{
			// Add a buffer size so we don't have to worry around looping back around to the zero byte again
			sampleByteOffset += getSampleBufferSize ();
		}
		
		// Find the sample byte
		for (i = 0; i < mNumTransactionsPerList; i++)
		{
			bufferByteOffset += mUSBIsocFrames[frameListNumber * mNumTransactionsPerList].frReqCount;
			// debugIOLog ("i: %d, bufferByteOffset = %ld", i, bufferByteOffset);
			if (sampleByteOffset < bufferByteOffset)
			{
				// The sample frame is queued to go out in this frame.
				usbFrame = mFrameQueuedForList[frameListNumber] + i;
				break;
			}
		}
		FailIf (0 == usbFrame, Exit);
	}
Exit:
	// debugIOLog ("- AppleUSBAudioStream::getQueuedFrameForSample (%lu) = %llu", sampleFrame, usbFrame);
	return usbFrame;
	
}
#endif

//--------------------------------------------------------------------------------
bool AppleUSBAudioStream::configureAudioStream (IOAudioSampleRate sampleRate) {
	OSNumber *							idVendor = NULL;
	OSNumber *							idProduct = NULL;
	AUAConfigurationDictionary *		configDictionary;
	UInt8								deviceClass;
	UInt8								deviceSubclass;
    IOAudioStreamFormat					streamFormat;
	IOReturn							resultCode;
	Boolean								resultBool;
	UInt32								index;
	UInt16								terminalType;
	UInt16								format;
	UInt8								numChannels;

	#if STAGGERINTERFACES
	if ( mInterfaceNumber % 2 != 1 )
	{
		IOSleep (1000);
	}
	#endif
    debugIOLog ("+ AppleUSBAudioStream[%p]::configureAudioStream ()", this);

    resultBool = FALSE;
	mTerminatingDriver = FALSE;
	mCoalescenceMutex = NULL;

	FailIf (NULL == mUSBAudioDevice, Exit);							// <rdar://7085810>
	FailIf (NULL == mUSBAudioDevice->mControlInterface, Exit);		// <rdar://7085810>
	FailIf (NULL == mStreamInterface, Exit);

	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary ()), Exit);

	if (kUSBIn == mDirection) 
	{
		// look for a streaming output terminal that's connected to a non-streaming input terminal
		debugIOLog ("? AppleUSBAudioStream[%p]::initHardware () - This is an input type endpoint (mic, etc.)", this);
		index = 0;
		do 
		{
			resultCode = configDictionary->getIndexedInputTerminalType (&terminalType, mUSBAudioDevice->mControlInterface->GetInterfaceNumber (), 0, index++);
		} while (terminalType == INPUT_UNDEFINED && index < 256 && kIOReturnSuccess == resultCode);

		mNumUSBFrameLists = RECORD_NUM_USB_FRAME_LISTS;
		mNumUSBFramesPerList = RECORD_NUM_USB_FRAMES_PER_LIST;
		mNumUSBFrameListsToQueue = RECORD_NUM_USB_FRAME_LISTS_TO_QUEUE;
		
		// We need a mutex for CoalesceInputSamples () in case something goes wrong at the start of the stream.
		mCoalescenceMutex = IORecursiveLockAlloc ();
		FailIf (NULL == mCoalescenceMutex, Exit);
	} 
	else if (kUSBOut == mDirection) 
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::configureAudioStream () - This is an output type endpoint (speaker, etc.)", this);
		index = 0;
		do 
		{
			resultCode = configDictionary->getIndexedOutputTerminalType (&terminalType, mUSBAudioDevice->mControlInterface->GetInterfaceNumber (), 0, index++);
		} while (terminalType == OUTPUT_UNDEFINED && index < 256 && kIOReturnSuccess == resultCode);

		mNumUSBFrameLists = PLAY_NUM_USB_FRAME_LISTS;
		mNumUSBFramesPerList = PLAY_NUM_USB_FRAMES_PER_LIST;
		mNumUSBFrameListsToQueue = PLAY_NUM_USB_FRAME_LISTS_TO_QUEUE;
	} 
	else 
	{
		FailIf ("Couldn't get the endpoint direction!", Exit);
	}
	
	// See if UHCI support is necessary
	mUHCISupport = mUSBAudioDevice->checkForUHCI ();
	// mUHCISupport = TRUE;
	
	mSplitTransactions = mUSBAudioDevice->detectSplitTransactions ();
	
	mFrameQueuedForList = NULL;
	
	// Allocate frame list time stamp array
	mFrameQueuedForList = new UInt64[mNumUSBFrameLists];
	FailIf (NULL == mFrameQueuedForList, Exit);
	
	setTerminalType (terminalType);

	// [rdar://4801012] We can't allocate the isoc frames yet because we don't know how many transactions there will be per frame list until
	// the alternate setting is chosen. This must be done in controlledFormatChange().
	// mUSBIsocFrames = (IOUSBLowLatencyIsocFrame *)IOMalloc (mNumUSBFrameLists * mNumUSBFramesPerList * sizeof (IOUSBLowLatencyIsocFrame));
	mUSBCompletion = (IOUSBLowLatencyIsocCompletion *)IOMalloc (mNumUSBFrameLists * sizeof (IOUSBLowLatencyIsocCompletion));
	mSampleBufferDescriptors = (IOSubMemoryDescriptor **)IOMalloc (mNumUSBFrameLists * sizeof (IOSubMemoryDescriptor *));
	bzero (mSampleBufferDescriptors, mNumUSBFrameLists * sizeof (IOSubMemoryDescriptor *));
	mWrapDescriptors[0] = OSTypeAlloc (IOSubMemoryDescriptor);
	mWrapDescriptors[1] = OSTypeAlloc (IOSubMemoryDescriptor);
	FailIf (NULL == mWrapDescriptors[0], Exit);
	FailIf (NULL == mWrapDescriptors[1], Exit);
	FailIf (NULL == mUSBCompletion, Exit);
	FailIf (NULL == mSampleBufferDescriptors, Exit);

	FailIf (kIOReturnSuccess != addAvailableFormats (configDictionary), Exit);
	
	mCurSampleRate = sampleRate;
	
	// Tell the IOAudioFamily what format we are going to be running in.
	// <rdar://problem/6892754> 10.5.7 Regression: Devices with unsupported formats stopped working
	FailIf ( kIOReturnSuccess != configDictionary->getFormat ( &format, mInterfaceNumber, mAlternateSettingID ), Exit );
	if ( ( PCM == format ) || ( IEC1937_AC3 == format ) )
	{
		FailIf (kIOReturnSuccess != configDictionary->getNumChannels (&numChannels, mInterfaceNumber, mAlternateSettingID), Exit);
		streamFormat.fNumChannels = numChannels;
		FailIf (kIOReturnSuccess != configDictionary->getBitResolution (&(streamFormat.fBitDepth), mInterfaceNumber, mAlternateSettingID), Exit);
		FailIf (kIOReturnSuccess != configDictionary->getSubframeSize (&(streamFormat.fBitWidth), mInterfaceNumber, mAlternateSettingID), Exit);
	}	
	streamFormat.fBitWidth *= 8;
	streamFormat.fAlignment = kIOAudioStreamAlignmentLowByte;
	streamFormat.fByteOrder = kIOAudioStreamByteOrderLittleEndian;
	streamFormat.fDriverTag = (mInterfaceNumber << 16) | mAlternateSettingID;
	
	switch (format) 
	{
		case PCM:
			streamFormat.fSampleFormat = kIOAudioStreamSampleFormatLinearPCM;
			streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
			streamFormat.fIsMixable = TRUE;
			break;
		case AC3:	// just starting to stub something in for AC-3 support
			streamFormat.fSampleFormat = kIOAudioStreamSampleFormatAC3;
			streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
			streamFormat.fIsMixable = FALSE;
			streamFormat.fNumChannels = 6;
			streamFormat.fBitDepth = 16;
			streamFormat.fBitWidth = 16;
			streamFormat.fByteOrder = kIOAudioStreamByteOrderBigEndian;
			break;
		case IEC1937_AC3:
			streamFormat.fSampleFormat = kIOAudioStreamSampleFormat1937AC3;
			streamFormat.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
			streamFormat.fIsMixable = FALSE;
			break;
		default:
			FailIf ("Interface doesn't have any supported formats!\n", Exit);
	}
	
	// Store default stream format and sample rate
	mDefaultAudioStreamFormat = streamFormat;
	mDefaultAudioSampleRate = sampleRate;
	
	FailIf (FALSE == mStreamInterface->open (this), Exit);		// Have to open the interface because calling setFormat will call performFormatChange, which expects the interface to be open.
	FailIf (kIOReturnSuccess != (resultCode = mStreamInterface->SetAlternateInterface (this, kRootAlternateSetting)), Exit);		// Select the first alternate interface to init the hardware
	FailIf (kIOReturnSuccess != (resultCode = this->setFormat (&streamFormat)), Exit);

    // Verify that this 'start' request is targeting a USB Audio Stream interface
    // (i.e. it must be an audio class and a stream subclass).
	
	FailIf (kIOReturnSuccess != configDictionary->getInterfaceClass (&deviceClass, mInterfaceNumber, mAlternateSettingID), Exit);
	FailIf (kUSBAudioClass != deviceClass, Exit);
	FailIf (kIOReturnSuccess != configDictionary->getInterfaceSubClass (&deviceSubclass, mInterfaceNumber, mAlternateSettingID), Exit);
	FailIf (kUSBAudioStreamInterfaceSubclass != deviceSubclass, Exit);
	
	resultBool = true;
	
	// Ask for plugin to load (if it exists)
	idVendor = OSNumber::withNumber (mUSBAudioDevice->getVendorID (), 16);
	if (NULL != idVendor)
	{
		setProperty (kIDVendorString, idVendor);
		idVendor->release ();
	}
	idProduct = OSNumber::withNumber (mUSBAudioDevice->getProductID (), 16);
	if (NULL != idProduct)
	{
		setProperty (kIDProductString, idProduct);
		idProduct->release ();
	}

Exit:
    debugIOLog("- AppleUSBAudioStream[%p]::configureAudioStream(), resultCode = %x, resultBool = %d", this, resultCode, resultBool);
    return resultBool;
}

bool AppleUSBAudioStream::openStreamInterface () {
	bool	result = FALSE;
	
	// <rdar://6277511>	Make sure that mStreamInterface is not null.
	if ( NULL != mStreamInterface )
	{	
		result = mStreamInterface->open (this);
	}

	return result;
}

void AppleUSBAudioStream::closeStreamInterface () {

	// <rdar://6277511>	Make sure that mStreamInterface is not null.
	if ( NULL != mStreamInterface )
	{	
		mStreamInterface->close (this);
	}
}

void AppleUSBAudioStream::queueInputFrames () {
	UInt64	curUSBFrameNumber;
	UInt64	framesLeftInQueue;

	#if 1	// enabled for [3091812]
	if (0 == mShouldStop && TRUE != mInCompletion && NULL != mStreamInterface) 
	{
		curUSBFrameNumber = mStreamInterface->GetDevice()->GetBus()->GetFrameNumber ();
		framesLeftInQueue = mUSBFrameToQueue - curUSBFrameNumber;

		if ( framesLeftInQueue < ( mNumUSBFramesPerList * ( mNumUSBFrameListsToQueue / 2 )) / 2 ) 	// <rdar://problem/6327095>
		{
			while ( framesLeftInQueue < mNumUSBFramesPerList * ( mNumUSBFrameListsToQueue - 1 ) && 0 == mShouldStop ) 
			{
				#if DEBUGLOADING
				debugIOLog ("! AppleUSBAudioEngine::convertInputSamples () - Queue a read from convertInputSamples: framesLeftInQueue = %ld", (UInt32)framesLeftInQueue);
				#endif
				readHandler (this, mUSBCompletion[mCurrentFrameList].parameter, kIOReturnSuccess, &mUSBIsocFrames[mCurrentFrameList * mNumUSBFramesPerList]);

				curUSBFrameNumber = mStreamInterface->GetDevice()->GetBus()->GetFrameNumber ();
				framesLeftInQueue = mUSBFrameToQueue - curUSBFrameNumber;
			}
		}
	}
	#endif
}

void AppleUSBAudioStream::queueOutputFrames () {
	UInt64	curUSBFrameNumber;
	UInt64	framesLeftInQueue;

	if (0 == mShouldStop && TRUE != mInCompletion && NULL != mStreamInterface) 
	{
		curUSBFrameNumber = mStreamInterface->GetDevice()->GetBus()->GetFrameNumber ();
		framesLeftInQueue = mUSBFrameToQueue - curUSBFrameNumber;
		if ( framesLeftInQueue < ( mNumUSBFramesPerList * ( mNumUSBFrameListsToQueue / 2 )) / 2 )	// <rdar://problem/6327095>
		{
			debugIOLog ("! AppleUSBAudioStream::queueSampleFrameWrite () - Queue a write from clipOutputSamples: framesLeftInQueue = %ld", (UInt32)framesLeftInQueue);
			writeHandler (this, mUSBCompletion[mCurrentFrameList].parameter, kIOReturnSuccess, &mUSBIsocFrames[mCurrentFrameList * mNumUSBFramesPerList]);
		}
	}
}

UInt16 AppleUSBAudioStream::getAlternateFrameSize () {
	return mAlternateFrameSize;
}

// <rdar://7568547> Initialize the USB frame list to proper values.
void AppleUSBAudioStream::initializeUSBFrameList ( IOUSBLowLatencyIsocFrame * usbIsocFrames, UInt32 numFrames )
{
	for ( UInt32 frameIndex = 0; frameIndex < numFrames; frameIndex++ )
	{
		usbIsocFrames[frameIndex].frStatus		= -1;
		usbIsocFrames[frameIndex].frReqCount	= 0;
		usbIsocFrames[frameIndex].frActCount	= 0;
		* (UInt64 *) ( & ( usbIsocFrames[frameIndex].frTimeStamp)) = 0ull;
	}
}

IOReturn AppleUSBAudioStream::PrepareWriteFrameList (UInt32 arrayIndex) {
	const IOAudioStreamFormat *			theFormat;
	IOReturn							result;
	UInt32								thisFrameListSize;
	#if DEBUGLATENCY
	UInt32								frameListByteCount;			// this is always updated, regardless of wrapping
	#endif
	UInt32								thisFrameSize;
	UInt32								firstFrame;
	UInt32								numBytesToBufferEnd;
	UInt32								lastPreparedByte;
	UInt32								numTransactionsPrepared;
	UInt32								remainderedSamples;
	UInt16								integerSamplesInFrame;
	UInt16								averageSamplesInFrame;
	UInt16								bytesAfterWrap = 0;			// for UHCI support
	UInt8								transactionsPerMS;
	UInt8								powerOfTwo = 0;
	Boolean								haveWrapped;

	result = kIOReturnError;		// assume failure
	FailIf ( 0 == mTransactionsPerUSBFrame, Exit );
	haveWrapped = FALSE;
	firstFrame = arrayIndex * mNumTransactionsPerList;
	mUSBCompletion[arrayIndex].target = (void *)this;
	mUSBCompletion[arrayIndex].action = writeHandler;
	mUSBCompletion[arrayIndex].parameter = 0;			// Set to number of bytes from the 0 wrap, 0 if this buffer didn't wrap

	theFormat = this->getFormat ();

	numBytesToBufferEnd = getSampleBufferSize () - mLastPreparedBufferOffset;
	lastPreparedByte = mLastPreparedBufferOffset;
	thisFrameListSize = 0;
	#if DEBUGLATENCY
		frameListByteCount = 0;
	#endif
	
	
	transactionsPerMS = mTransactionsPerUSBFrame;
	while ( transactionsPerMS > 1 )
	{
		transactionsPerMS >>= 1;
		powerOfTwo++;
	}	 
	
	// <rdar://problems/5600254> Calculate the sample rate in terms of transactions instead of milliseconds. For full speed devices, this changes nothing.
	// <rdar://problems/6954295> Store Async feedback in samples per frame/microframe as a 16.16 fixed point number
	averageSamplesInFrame = mSamplesPerPacket.whole;
	remainderedSamples = mSamplesPerPacket.fraction;

	for (numTransactionsPrepared = 0; numTransactionsPrepared < mNumTransactionsPerList; numTransactionsPrepared++) 
	{
		// [rdar://5600254] Remaindered samples are to be determined on a transaction basis, not a USB frame basis.		
		integerSamplesInFrame = averageSamplesInFrame;
		mFractionalSamplesLeft += remainderedSamples;
		if ( mFractionalSamplesLeft >= kSampleFractionAccumulatorRollover ) 	// <rdar://problem/6954295>
		{
			integerSamplesInFrame++;
			mFractionalSamplesLeft -= kSampleFractionAccumulatorRollover;		// <rdar://problem/6954295>
		}
		thisFrameSize = integerSamplesInFrame * mSampleSize;
		#if DEBUGLATENCY
			frameListByteCount += thisFrameSize;
		#endif
		if (thisFrameSize >= numBytesToBufferEnd) 
		{
			bytesAfterWrap = thisFrameSize - numBytesToBufferEnd;
			mNumFramesInFirstList = numTransactionsPrepared + 1;
			mUSBCompletion[arrayIndex].parameter = (void *)((mNumFramesInFirstList << 16) | bytesAfterWrap);	// Number of bytes after wrap
			
			if (mUHCISupport)
			{
				
				#if DEBUGUHCI
				debugIOLog ("PrepareWriteFrameList: Wrapping because (thisFrameSize = ) %d >= (numBytesToBufferEnd = ) %d", thisFrameSize, numBytesToBufferEnd); 
				debugIOLog ("PrepareWriteFrameList: bytesAfterWrap = %d, numTransactionsPrepared = %d, numTransactionsPerList %d", bytesAfterWrap, numTransactionsPrepared, mNumTransactionsPerList);
				#endif
				
				mWrapDescriptors[0]->initSubRange (mUSBBufferDescriptor,
													mLastPreparedBufferOffset,
													getSampleBufferSize () + bytesAfterWrap  - mLastPreparedBufferOffset,
													kIODirectionOut);
				#if DEBUGUHCI
				debugIOLog ("PrepareWriteFrameList: initSubRange 0: %d to %d", mLastPreparedBufferOffset, getSampleBufferSize () + bytesAfterWrap);
				debugIOLog ("PrepareWriteFrameList: %d frames in first list", mNumFramesInFirstList);
				#endif
			}
			else
			{
				mWrapDescriptors[0]->initSubRange (mUSBBufferDescriptor, mLastPreparedBufferOffset, getSampleBufferSize () - mLastPreparedBufferOffset, kIODirectionOut);
			}
			
			numBytesToBufferEnd = getSampleBufferSize () - bytesAfterWrap;
			lastPreparedByte = bytesAfterWrap;
			haveWrapped = TRUE;
		} // if (thisFrameSize >= numBytesToBufferEnd)
		else 
		{
			thisFrameListSize += thisFrameSize;
			lastPreparedByte += thisFrameSize;
			numBytesToBufferEnd -= thisFrameSize;
		}
		mUSBIsocFrames[firstFrame + numTransactionsPrepared].frStatus = -1;
		mUSBIsocFrames[firstFrame + numTransactionsPrepared].frActCount = 0;
		mUSBIsocFrames[firstFrame + numTransactionsPrepared].frReqCount = thisFrameSize;
	}  // for numTransactionsPrepared
	
	if (TRUE == haveWrapped) 
	{
		mNeedTimeStamps = TRUE;
		if (mUHCISupport)
		{
			// debugIOLog("bytesAfterWrap = %d lastPreparedByte = %d numBytesToBufferEnd = %d  mNumFramesInFirstList = %d numTransactionsPrepared = %d", bytesAfterWrap, lastPreparedByte, numBytesToBufferEnd, mNumFramesInFirstList, numTransactionsPrepared );
			mWrapDescriptors[1]->initSubRange (mUSBBufferDescriptor, bytesAfterWrap, lastPreparedByte - bytesAfterWrap, kIODirectionOut);
			#if DEBUGUHCI
			debugIOLog ("PrepareWriteFrameList: initSubRange 1: %d to %d", bytesAfterWrap, lastPreparedByte);
			#endif
			if (lastPreparedByte != bytesAfterWrap)
			{
				// This is where we setup our extra completion for the second wrap write
				mExtraUSBCompletion.target = (void *)this;
				mExtraUSBCompletion.action = writeHandlerForUHCI;
				// mExtraUSBCompletion.parameter = (void *)((mNumFramesInFirstList << 16) | bytesAfterWrap);
			}
		}
		else
		{
			mWrapDescriptors[1]->initSubRange (mUSBBufferDescriptor, 0, lastPreparedByte, kIODirectionOut);

			if (NULL != mWrapRangeDescriptor) 
			{
				mWrapRangeDescriptor->release ();
				mWrapRangeDescriptor = NULL;
			}

			mWrapRangeDescriptor = IOMultiMemoryDescriptor::withDescriptors ((IOMemoryDescriptor **)mWrapDescriptors, 2, kIODirectionOut, true);
		}
	} 
	else 
	{
		mSampleBufferDescriptors[arrayIndex]->initSubRange (mUSBBufferDescriptor, mLastPreparedBufferOffset, thisFrameListSize, kIODirectionOut);
		FailIf (NULL == mSampleBufferDescriptors[arrayIndex], Exit);
	}

	mSafeErasePoint = mLastSafeErasePoint;
	mLastSafeErasePoint = mLastPreparedBufferOffset;
	mLastPreparedBufferOffset = lastPreparedByte;
	#if DEBUGLATENCY
		mLastFrameListSize = mThisFrameListSize;
		mThisFrameListSize = frameListByteCount;
	#endif
	result = kIOReturnSuccess;

Exit:
	return result;
}

IOReturn AppleUSBAudioStream::PrepareAndReadFrameLists (UInt8 sampleSize, UInt8 numChannels, UInt32 usbFrameListIndex) {
	IOReturn							result;
	UInt32								firstFrame;
	UInt32								numTransactionsPrepared;
	UInt16								averageFrameSamples;
	UInt16								additionalSampleFrameFreq;
	UInt16								bytesToRead;

	#if DEBUGINPUT
	debugIOLog ("+ AppleUSBAudioStream::PrepareAndReadFrameLists (%d, %d, %ld)", sampleSize, numChannels, usbFrameListIndex);
	#endif
	
	result = kIOReturnError;		// assume failure
	firstFrame = usbFrameListIndex * mNumTransactionsPerList;
	mUSBCompletion[usbFrameListIndex].target = (void *)this;
	mUSBCompletion[usbFrameListIndex].action = readHandler;
	mUSBCompletion[usbFrameListIndex].parameter = (void *)usbFrameListIndex;	// what frame list index this buffer is

	calculateSamplesPerPacket (mCurSampleRate.whole, &averageFrameSamples, &additionalSampleFrameFreq);
	mBytesPerSampleFrame = sampleSize * numChannels;
	// [rdar://5355808] This should be the smaller of the calculated size and the maxPacketSize.
	bytesToRead = mReadUSBFrameSize;

	for (numTransactionsPrepared = 0; numTransactionsPrepared < mNumTransactionsPerList; numTransactionsPrepared++) 
	{
		mUSBIsocFrames[firstFrame + numTransactionsPrepared].frStatus = -1;
		mUSBIsocFrames[firstFrame + numTransactionsPrepared].frActCount = 0;
		mUSBIsocFrames[firstFrame + numTransactionsPrepared].frReqCount = bytesToRead;
		
		// This casting can be removed when AbsoluteTime is changed to UInt64 in Tiger.
		
		* (UInt64 *) ( & ( mUSBIsocFrames[firstFrame + numTransactionsPrepared].frTimeStamp)) = 0ull;
	}

	if (NULL != mPipe) 
	{
		result = mPipe->Read (mSampleBufferDescriptors[usbFrameListIndex], mUSBFrameToQueue, mNumTransactionsPerList, &mUSBIsocFrames[firstFrame], &mUSBCompletion[usbFrameListIndex], 1);	// Update timestamps every 1ms
		if (result != kIOReturnSuccess)
		{
			debugIOLog ("! AppleUSBAudioStream[%p]::PrepareAndReadFrameLists () - Error 0x%x reading from pipe", this, result);
		}
		
		// keep track of this frame number for time stamping
		if (NULL != mFrameQueuedForList)
		{
			mFrameQueuedForList[usbFrameListIndex] = mUSBFrameToQueue;
		}
		mUSBFrameToQueue += mNumUSBFramesPerList;
	}
	else
	{
		debugIOLog ("! AppleUSBAudioStream[%p]::PrepareAndReadFrameLists () - mPipe is NULL!", this);
	}

	#if DEBUGINPUT
	debugIOLog ("- AppleUSBAudioStream::PrepareAndReadFrameLists ()");
	#endif
	return result;
}

#if PRIMEISOCINPUT
// This method starts an input isoc stream to a device and disregards kNumIsocFramesToPrime frames. 
void AppleUSBAudioStream::primeInputPipe (IOUSBPipe * pipeToPrime, UInt32 bytesPerUSBFrame, UInt32 usbFramesToDelay) 
{
	// IOUSBLowLatencyIsocFrame *	isocFrames = NULL;
	IOReturn					result;
	UInt32						i;
	bool						dataWrittenToPipe = false;
	
	FailIf (NULL == pipeToPrime, Exit);
	FailIf (0 == bytesPerUSBFrame, Exit);
	
	// Make sure the lock delay isn't too large.
	FailIf ( (bytesPerUSBFrame * usbFramesToDelay) > mSampleBufferSize, Exit );
	
	mPrimeInputIsocFrames = (IOUSBLowLatencyIsocFrame *)IOMalloc (usbFramesToDelay * sizeof (IOUSBLowLatencyIsocFrame));
	FailIf (NULL == mPrimeInputIsocFrames, Exit);
	
	mPrimeInputCompletion.target	= (void  *) this;
	mPrimeInputCompletion.action	= primeInputPipeHandler;
	mPrimeInputCompletion.parameter	= (void *) usbFramesToDelay;				// So we know how many frames to free in the completion
		
	// Initialize isoc frame list
	for (i = 0; i < usbFramesToDelay; i++)
	{
		(mPrimeInputIsocFrames + i)->frStatus = -1;
		(mPrimeInputIsocFrames + i)->frReqCount = bytesPerUSBFrame;
		(mPrimeInputIsocFrames + i)->frActCount = 0;
	}
	
	// attempt to read from USB pipe
	result = pipeToPrime->Read (mUSBBufferDescriptor, mUSBFrameToQueue, usbFramesToDelay, mPrimeInputIsocFrames, &mPrimeInputCompletion, 0);
	if (result == kIOReturnSuccess)
	{
		// Our request was queued. We can let the completion handle memory deallocation.
		dataWrittenToPipe = true;
		
		// Advance the frame to queue by the number of frames primed
		mUSBFrameToQueue += usbFramesToDelay;
		debugIOLog ("? AppleUSBAudioStream::primeInputPipe (%p, %lu) - %d frames primed. mUSBFrameToQueue = %llu", pipeToPrime, bytesPerUSBFrame, usbFramesToDelay, mUSBFrameToQueue);
	}
	
Exit:
	if (false == dataWrittenToPipe)
	{
		if (mPrimeInputIsocFrames)
		{
			IOFree (mPrimeInputIsocFrames, usbFramesToDelay * sizeof (IOUSBLowLatencyIsocFrame));
			mPrimeInputIsocFrames = NULL;
		}
	}
}

void AppleUSBAudioStream::primeInputPipeHandler (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames)
{
	AppleUSBAudioStream *	self;
	UInt32					usbFramesToDelay;
	
	debugIOLog ("+ AppleUSBAudioStream::primeInputPipeHandler (%p, %lu, 0x%x, %p)", object, (uintptr_t) parameter, result, pFrames); 
	self = (AppleUSBAudioStream *)object;
	FailIf (NULL == self, Exit);
	
	// If any analysis of primed input is required, insert it after here.

	// If any analysis of primed input is required, insert it before here.
	
Exit:
	if (self->mPrimeInputIsocFrames)
	{
		usbFramesToDelay = (UInt32)(uintptr_t)( parameter );
		IOFree (self->mPrimeInputIsocFrames, usbFramesToDelay * sizeof (IOUSBLowLatencyIsocFrame));
		self->mPrimeInputIsocFrames = NULL;
	}
	debugIOLog ("- AppleUSBAudioStream::primeInputPipeHandler (%p, %lu, 0x%x, %p)", object, (uintptr_t) parameter, result, pFrames);
}

#endif

IOReturn AppleUSBAudioStream::readFrameList (UInt32 frameListNum) {
	const IOAudioStreamFormat *			theFormat;
    IOReturn							result;

	#if DEBUGINPUT
	debugIOLog ("+ AppleUSBAudioStream::readFrameList ()");
	#endif
	theFormat = this->getFormat ();

	result = PrepareAndReadFrameLists (theFormat->fBitWidth / 8, theFormat->fNumChannels, frameListNum);
	#if DEBUGINPUT
	debugIOLog ("- AppleUSBAudioStream::readFrameList ()");
    #endif
	return result;
}

void AppleUSBAudioStream::readHandler (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames) {
	AppleUSBAudioStream *			self;
	UInt64							currentUSBFrameNumber;
	UInt32							frameListToRead;
	UInt32							thisActCount = 0;
	UInt32							minimumUSBFrameSize = 0;
	UInt8							frameIndex;
	IOReturn						thisStatus = 0;
	bool							flagOverrun;

	#if DEBUGINPUT
	debugIOLog ("+ AppleUSBAudioStream::readHandler ()");
	#endif
	self = (AppleUSBAudioStream *)object;
	FailIf (TRUE == self->mInCompletion, Exit);
	self->mInCompletion = TRUE;
		
	if	(		(self->mUSBAudioDevice)
			&&	(false == self->mUSBAudioDevice->getSingleSampleRateDevice ())		// We didn't know this was a single sample rate device at this time
			&&	(kIOReturnOverrun == result))										// This is what IOUSBFamily should be reporting on an overrun
	{
		debugIOLog ("! AppleUSBAudioStream::readHandler () - Encountered fatal error 0x%x on frame list %d (frReqCount = %d).", result, self->mCurrentFrameList, pFrames->frReqCount );
		// [rdar://5417631] Drop a once-per-attach log message that the device is generating overruns.
		if ( false == self->mGeneratesOverruns )
		{
			IOLog ( "WARNING: AppleUSBAudio has detected that a connected USB audio device is sending too much audio data.\n" );
			IOLog ( "WARNING: This USB audio device may not function properly. Please notify the device manufacturer.\n" );
			self->mGeneratesOverruns = true;
		}
		flagOverrun = true;
		// [rdar://4456484] If every frame in this frame list generated an overrun, we may need to take drastic measures.
		for ( UInt16 frameIndex = 0; frameIndex < self->mNumTransactionsPerList && pFrames; frameIndex++ )
		{
			if ( kIOReturnOverrun != pFrames[frameIndex].frStatus )
			{
				flagOverrun = false;
				break;
			}
		}
		
		if ( flagOverrun )
		{
			// This is a fatal error. Notify the AppleUSBAudioDevice to sync the sample rates when possible if this device has two streaming interfaces.
			self->mUSBAudioDevice->setShouldSyncSampleRates (self->mUSBAudioEngine);
			goto Exit; // [rdar://5889101]
		}
	}
	
	FailIf (NULL == self->mStreamInterface, Exit);
	currentUSBFrameNumber = self->mStreamInterface->GetDevice()->GetBus()->GetFrameNumber();
	
	if (kIOReturnAborted != result)
	{
		#if 1 // enabled for [3091812]
		if (0 == self->mShouldStop && (SInt32)(self->mUSBFrameToQueue - currentUSBFrameNumber) > (SInt32)(self->mNumUSBFramesPerList * (self->mNumUSBFrameListsToQueue - 1))) 
		{
			// The frame list that this would have queued has already been queued by convertInputSamples
			#if DEBUGLOADING
			debugIOLog ("Not queuing a frame list in readHandler (%ld)", (SInt32)(self->mUSBFrameToQueue - currentUSBFrameNumber));
			#endif
			goto Exit;
		}
		#endif
		
		// Comb the returned statuses for alarming statuses
		for (frameIndex = 0; frameIndex < self->mNumTransactionsPerList && pFrames; frameIndex++)
		{
			thisStatus = (pFrames + frameIndex)->frStatus;
			thisActCount = (pFrames + frameIndex)->frActCount;
			// [rdar://5355808] [rdar://5889101]
			minimumUSBFrameSize = self->mAverageFrameSize - 2 * self->mSampleSize;
			#ifdef DEBUG
			if (		(!(self->mShouldStop))
					&&	(thisStatus != kIOReturnSuccess)
					&&	(		(thisStatus != kIOReturnUnderrun)
							||	(		(thisStatus == kIOReturnUnderrun)
									&&	(thisActCount < minimumUSBFrameSize))))
			{
				debugIOLog ("! AppleUSBAudioStream::readHandler () - Frame list %d frame index %d returned error 0x%x (frActCount = %lu, result = 0x%x)", self->mCurrentFrameList, frameIndex, thisStatus, thisActCount, result);
			}
			#endif
			
			if (thisActCount < minimumUSBFrameSize)
			{
				// IOLog ("AppleUSBAudio: ERROR on input! Short packet of size %lu encountered when %lu bytes were requested.\n", thisActCount, (pFrames + frameIndex)->frReqCount);
			}
			
			if (kIOReturnNotResponding == thisStatus)
			{
				if (		(self->mUSBAudioDevice)
						&&	(false == self->mUSBAudioDevice->recoveryRequested ()))
				{
					self->mUSBAudioDevice->requestDeviceRecovery ();
				}
			}
			
		}
	}
	
	if (kIOReturnSuccess != result && kIOReturnAborted != result && kIOReturnUnderrun != result) 
	{
		// skip ahead and see if that helps
		if (self->mUSBFrameToQueue <= currentUSBFrameNumber) 
		{
			self->mUSBFrameToQueue = currentUSBFrameNumber + kMinimumFrameOffset;
		}
	}

	if (kIOReturnAborted != result) 
	{
		self->CoalesceInputSamples (0, pFrames);
	}

	if (self->mShouldStop > 0) 
	{
		if (		(self->mShouldStop == 1)
				||	(self->mShouldStop == self->mNumUSBFrameListsToQueue))
		{
			// Only really care about the first and last stopped frame lists.
			debugIOLog("? AppleUSBAudioStream::readHandler() - stopping: %d", self->mShouldStop);
		}
		self->mShouldStop++;
	} 
	else if (kIOReturnAborted != result)
	{
		// <rdar://7568547> Acquire the lock when updating the mCurrentFrameList value so that it is 
		// consistent when accessed in CoalesceInputSamples().
		if (self->mCoalescenceMutex)
		{
			IORecursiveLockLock (self->mCoalescenceMutex);
		}
		
		if (self->mCurrentFrameList == self->mNumUSBFrameLists - 1) 
		{
			self->mCurrentFrameList = 0;
		} 
		else 
		{
			self->mCurrentFrameList++;
		}

		// <rdar://7568547> Release the lock.
		if (self->mCoalescenceMutex)
		{
			IORecursiveLockUnlock (self->mCoalescenceMutex);
		}

		frameListToRead = (self->mCurrentFrameList - 1) + self->mNumUSBFrameListsToQueue;
		if (frameListToRead >= self->mNumUSBFrameLists) 
		{
			frameListToRead -= self->mNumUSBFrameLists;
		}
		self->readFrameList (frameListToRead);
	}

Exit:
	self->mInCompletion = FALSE;
	#if DEBUGINPUT
	debugIOLog ("- AppleUSBAudioStream::readHandler ()");
	#endif
	return;
}

//  <rdar://problem/6954295> Store Async feedback in samples per frame/microframe as a 16.16 fixed point number
//  <rdar://problem/7345441> AppleUSBAudio: getRateFromSamplesPerPacket() has a rounding error in sample rate calculation
UInt32 AppleUSBAudioStream::getRateFromSamplesPerPacket ( IOAudioSamplesPerFrame samplesPerPacket )
{
	UInt64 sampleRate = 0;
	
	if ( 0 != samplesPerPacket.whole )
	{
		sampleRate = samplesPerPacket.whole * 1000;	// multiply by 1000 to get Hz (stored per ms)
		sampleRate <<= 16;
		sampleRate += samplesPerPacket.fraction;	// stored value already multiplied by 1000 in sampleRateHandler()
	}
	
	if ( 0 != mTransactionsPerUSBFrame )
	{
		sampleRate *= mTransactionsPerUSBFrame;
	}
		
	return ( UInt32 )( ( sampleRate >> 16 ) & 0x00000000FFFFFFFF );
}

/*
	The purpose of this function is to deal with asynchronous synchronization of isochronous output streams.
	On devices that can lock their output clock to an external source, they can report that value to the driver
	so that the driver doesn't feed data too quickly or too slowly to the device (so that the device's FIFO's
	don't overrun or underrun).

	The device returns a 10.14 unsigned fixed point value in a 24 bit result or a 16.16 unsigned fixed point 
	value in a 32 bit result. This value says how many samples per frame (or microframe) the device wants for the current 
	sampling period.  The device reports the current sampling period in its feedback/synch endpoint, which 
	can be retrieved with the GetIsocAssociatedEndpointRefreshInt call (interpreted as 2^(10-x) frames where 
	x is the value returned by GetIsocAssociatedEndpointRefreshInt).

	The endpoint should not be read from more often than once every 2^(10-x) frames as the number isn't updated
	by the device any more often than that.  Because x can range from 1 to 9, the sample rate may need to be
	adjusted anywhere from once every 2 frames to once every 512 frames.

	If the number returned is larger than the last number returned, the device needs more data, if it is smaller
	than the previous value, the device needs less data.

	In typical usage, the value should not change by a large value (less than 1% of the clock value).
	A typical result would be be a value of 0x0b0667 which in 10.14 is 44.10004.  This is represented in the
	driver as 0x2c199c which is the 16.16 value for 44.10004.
	
	See this radar for more details: <rdar://problem/6954295> Store Async feedback in samples per frame/microframe as 
	a 16.16 fixed point number
*/
void AppleUSBAudioStream::sampleRateHandler (void * target, void * parameter, IOReturn result, IOUSBIsocFrame * pFrames) {
    AppleUSBAudioStream	*			self;
	UInt32							requestedSamplesPerFrame;	// sample frames requested per frame or microframe <rdar://problem/6954295>
	IOAudioSamplesPerFrame			oldSamplesPerFrame;			// sample frames per frame or microframe <rdar://problem/6954295>
	IOAudioSamplesPerFrame			newSamplesPerFrame;			// sample frames per frame or microframe <rdar://problem/6954295>
	UInt32							framesToAdvance = 0;
	IOReturn						readStatus = kIOReturnError;
	UInt32							sampleRateBuffer;			// <rdar://problem/6954295>

    self = (AppleUSBAudioStream *)target;

	if	(		(pFrames)
			&&	(		(kIOReturnSuccess == result)
					||	(kIOReturnUnderrun == result)))
	{
		// <rdar://problem/6954295>
		sampleRateBuffer = *( self->mAverageSampleRateBuffer );
		requestedSamplesPerFrame = USBToHostLong ( sampleRateBuffer );
		oldSamplesPerFrame = self->mSamplesPerPacket;
		
		switch (pFrames->frActCount)
		{
			case kFixedPoint10_14ByteSize:
				// Assign 10.14 value to an IOAudioSamplesPerFrame struct <rdar://problem/6954295>
				requestedSamplesPerFrame = requestedSamplesPerFrame << 2;
				newSamplesPerFrame.whole = requestedSamplesPerFrame >> 16;
				newSamplesPerFrame.fraction = ( requestedSamplesPerFrame & 0x0000FFFF ) * 1000;	// fraction is stored x 1000 to maintain precision
				break;
			case kFixedPoint16_16ByteSize:
				// Assign 16.16 value to an IOAudioSamplesPerFrame struct <rdar://problem/6954295>
				newSamplesPerFrame.whole = requestedSamplesPerFrame >> 16;
				newSamplesPerFrame.fraction = ( requestedSamplesPerFrame & 0x0000FFFF ) * 1000;	// fraction is stored x 1000 to maintain precision
				break;
			default:
				// We shouldn't get here. Set newSamplesPerFrame to 0 so that next check fails. <rdar://problem/6954295>
				newSamplesPerFrame.whole = 0;
				newSamplesPerFrame.fraction = 0;
		}
		// <rdar://problem/6954295>
		if (  ( newSamplesPerFrame.whole != 0 ) && 
			  ( newSamplesPerFrame.whole != oldSamplesPerFrame.whole || newSamplesPerFrame.fraction != oldSamplesPerFrame.fraction ) ) 
		{
			// Need to make sure this sample rate isn't way out of the ballpark 
			// i.e. each frame/microframe cannot vary by more than +/- one sample <rdar://problem/6954295>
			if (		( newSamplesPerFrame.whole > ( oldSamplesPerFrame.whole + 1 ) )
					||	( newSamplesPerFrame.whole < ( oldSamplesPerFrame.whole - 1 ) ) )
			{
				debugIOLog ( "! AppleUSBAudioStream::sampleRateHandler () - ignoring sample rate %d as out of bounds", self->getRateFromSamplesPerPacket ( newSamplesPerFrame ) );
			}
			else
			{
				// The device has changed the sample rate that it needs, let's roll with the new sample rate <rdar://problem/6954295>
				self->mSamplesPerPacket = newSamplesPerFrame;
#if DEBUGSAMPLERATEHANDLER
				debugIOLog ("? AppleUSBAudioStream::sampleRateHandler () - Sample rate changed, requestedFrameRate: %u mSamplesPerPacket: %lu %lu\n", self->getRateFromSamplesPerPacket ( self->mSamplesPerPacket ), self->mSamplesPerPacket.whole, self->mSamplesPerPacket.fraction );
#endif
			}
		}
#if DEBUGSAMPLERATEHANDLER
		debugIOLog ("? AppleUSBAudioStream::sampleRateHandler () - currentFrameRate: %u mSamplesPerPacket: %lu %lu\n", self->getRateFromSamplesPerPacket ( self->mSamplesPerPacket ), self->mSamplesPerPacket.whole, self->mSamplesPerPacket.fraction );
#endif
	}
	else
	{
		debugIOLog ("! AppleUSBAudioStream::sampleRateHandler () - ignoring isoc result due to error 0x%x", result);
		if ( pFrames )
		{
			debugIOLog ("    pFrames->frReqCount = %d", pFrames->frReqCount);
			debugIOLog ("    pFrames->frActCount = %d", pFrames->frActCount);
			debugIOLog ("    pFrames->frStatus = 0x%x", pFrames->frStatus);
		}
		else
		{
			debugIOLog ("    pFrames = NULL");
		}
	}

	if (0 == self->mShouldStop) 
	{
		// Have to reset these parameters otherwise the read doesn't happen.
		self->mSampleRateFrame.frStatus = -1;
		self->mSampleRateFrame.frReqCount = self->mFeedbackPacketSize;	
		self->mSampleRateFrame.frActCount = 0;

		framesToAdvance = (1 << self->mRefreshInterval);
		// Due to limitations in IOUSBFamily, the earliest this isochronous transaction can be scheduled is kMinimumFrameOffset frames in the future.
		while ( framesToAdvance < kMinimumFrameOffset )
		{
			// Keep raising the power
			framesToAdvance *= 2;
		}
		
		if (NULL != self->mAssociatedPipe) 
		{
			while	(		(readStatus != kIOReturnSuccess)
						&&	(framesToAdvance <= kMaxFeedbackPollingInterval))
			{
				self->mNextSyncReadFrame += framesToAdvance;
				readStatus = self->mAssociatedPipe->Read (self->mAssociatedEndpointMemoryDescriptor, self->mNextSyncReadFrame, 1, &(self->mSampleRateFrame), &(self->mSampleRateCompletion));
				if ( kIOReturnSuccess != readStatus )
				{
					debugIOLog ( "! AppleUSBAudioStream::sampleRateHandler () - framesToAdvance = %d, mNextSyncReadFrame = %llu, readStatus = 0x%x", framesToAdvance, self->mNextSyncReadFrame, readStatus );
					self->mNextSyncReadFrame -= framesToAdvance;
					framesToAdvance *= 2;
				}
			}
			
			if ( kIOReturnSuccess != readStatus )
			{
				debugIOLog ( "! AppleUSBAudioStream::sampleRateHandler () - Could not queue feedback endpoint isoc request. Feedback request chain is halted!" );
				debugIOLog ( "  mRefreshInterval = %d, framesToAdvance = %d, mNextSyncReadFrame = %llu, readStatus = 0x%x", self->mRefreshInterval, framesToAdvance, self->mNextSyncReadFrame, readStatus );
			}
		}
	}
	else
	{
		debugIOLog ( "? AppleUSBAudioStream::sampleRateHandler () - Stopping feedback chain because stream is stopping." );
	}

	//	self->release ();
	return;
}

IOReturn AppleUSBAudioStream::setSampleRateControl (UInt8 address, UInt32 sampleRate) {
	IOUSBDevRequest				devReq;
	UInt32						theSampleRate;
	IOReturn					result;

	result = kIOReturnError;
	FailIf (NULL == mStreamInterface, Exit);
	// if (usbAudio->IsocEndpointHasSampleFreqControl (mInterfaceNumber, mAlternateSettingID)) 
	devReq.bmRequestType = USBmakebmRequestType (kUSBOut, kUSBClass, kUSBEndpoint);
	devReq.bRequest = SET_CUR;
	devReq.wValue = (SAMPLING_FREQ_CONTROL << 8) | 0;
	devReq.wIndex = address;
	devReq.wLength = 3;
	theSampleRate = OSSwapHostToLittleInt32 (sampleRate);
	devReq.pData = &theSampleRate;

	debugIOLog ("? AppleUSBAudioStream[%p]::SetSampleRate () - Control interface %d, alt setting %d, endpoint address 0x%x, sample rate (little endian) 0x%x", this, mInterfaceNumber, mAlternateSettingID, devReq.wIndex, theSampleRate);
	result = mStreamInterface->GetDevice()->DeviceRequest (&devReq);
	FailIf (kIOReturnSuccess != result, Exit);

Exit:
	if (kIOReturnSuccess != result)
	{
		debugIOLog ("! AppleUSBAudioStream[%p]::setSampleRateControl () = 0x%x", this, result);
	}
	return result;
}

UInt8 AppleUSBAudioStream::getSyncType () {	
	AUAConfigurationDictionary *		configDictionary;
	UInt8								direction = 0;
	UInt8								address = 0;
	UInt8								syncType = kNoneSyncType;

	FailIf ( NULL == ( configDictionary = mUSBAudioDevice->getConfigDictionary () ), Exit );

	FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointDirection ( &direction, mInterfaceNumber, mAlternateSettingID ), Exit ); 
	FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointAddress ( &address, mInterfaceNumber, mAlternateSettingID, direction ), Exit );
	FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointSyncType ( &syncType, mInterfaceNumber, mAlternateSettingID, address ), Exit );

Exit:
	return syncType;
}

UInt32 AppleUSBAudioStream::getLockDelayFrames () {
	AUAConfigurationDictionary *		configDictionary;
	UInt32								usbFramesToDelay			= 0;
	UInt16								averageFrameSamples;
	UInt16								additionalSampleFrameFreq;
	UInt8								lockDelay					= 0;
	UInt8								lockDelayUnits				= 0;

	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);

	calculateSamplesPerPacket (mCurSampleRate.whole, &averageFrameSamples, &additionalSampleFrameFreq);

	// If successful, this operation will advance the first frame to queue, so this must be done prior to reading the frame lists.
	// [rdar://5083342] Use the lockDelay if available to determine how many USB frames to prime.
	
	FailIf ( kIOReturnSuccess != configDictionary->asEndpointGetLockDelay( &lockDelay, mInterfaceNumber, mAlternateSettingID ), Exit );
	FailIf ( kIOReturnSuccess != configDictionary->asEndpointGetLockDelayUnits( &lockDelayUnits, mInterfaceNumber, mAlternateSettingID ), Exit );

	if	(		( lockDelay )
			&&	( lockDelayUnits ) )
	{
		switch ( lockDelayUnits )
		{
			case kLockDelayUnitMilliseconds:
				usbFramesToDelay = lockDelay;
				break;
			case kLockDelayUnitsDecodedPCMSamples:
				// Add one USB frame for every maximum packet size in samples and one for any partial remainder
				usbFramesToDelay = ( lockDelay / ( averageFrameSamples + 1 ) ) + ( ( lockDelay % ( averageFrameSamples + 1) ) ? 1 : 0 );  
				break;
			default:
				usbFramesToDelay = kNumUSBFramesToPrime;
		}
	}
	else
	{
		usbFramesToDelay = kNumUSBFramesToPrime;
	}
	
Exit:
	return usbFramesToDelay;
}

IOReturn AppleUSBAudioStream::prepareUSBStream () {
	const IOAudioStreamFormat *			theFormat;
	IOReturn							resultCode;
	IOUSBFindEndpointRequest			audioIsochEndpoint;
	AUAConfigurationDictionary *		configDictionary;
	UInt32								numQueued;
	UInt16								averageFrameSamples;
	UInt16								additionalSampleFrameFreq;
	UInt16								maxFrameSize;
	UInt16								maxPacketSize				= 0;
	UInt8								endpointAddress;
	UInt32								remainder;					// <rdar://problem/6954295>

    debugIOLog ("+ AppleUSBAudioStream[%p]::prepareUSBStream ()", this);
    // Assume the entire method will fail.
	resultCode = kIOReturnError;
	numQueued = 0;

    // Start the IO audio engine
    // Enable interrupts for this stream
    // The interrupt should be triggered at the start of the sample buffer
    // The interrupt handler should increment the fCurrentLoopCount and fLastLoopTime fields

	mCurrentFrameList = 0;
	mSafeErasePoint = 0;
	mLastSafeErasePoint = 0;
	mBufferOffset = 0;
	mLastPreparedBufferOffset = 0;		// Start playing from the start of the buffer
	mFractionalSamplesLeft = 0;			// Reset our parital frame list info
	
	mOverrunsCount = 0;

    mShouldStop = 0;
	
	// Set this as the default until we are told otherwise <rdar://problem/6954295>
	// Take the current sample rate (in Hz) and and transform it into samples per packet represented in 16.16 fixed point.  When calculating the  
	// fractional part, store the fraction x 1000 to maintain precision.
	mSamplesPerPacket.whole = mCurSampleRate.whole / ( mTransactionsPerUSBFrame * 1000 );
	remainder = mCurSampleRate.whole - ( mSamplesPerPacket.whole * mTransactionsPerUSBFrame * 1000 );	// same as (mCurSampleRate.whole % 1000) * mTransactionsPerUSBFrame
	mSamplesPerPacket.fraction = ( remainder * 65536 ) / mTransactionsPerUSBFrame;
	debugIOLog ( "? AppleUSBAudioStream[%p]::prepareUSBStream () - mSamplesPerPacket: %u(whole) %u(fraction)", this, mSamplesPerPacket.whole, mSamplesPerPacket.fraction );
	
    FailIf ((mNumUSBFrameLists < mNumUSBFrameListsToQueue), Exit);
	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);
	FailIf (NULL == mStreamInterface, Exit);

	resultCode = mStreamInterface->SetAlternateInterface (this, mAlternateSettingID);
	debugIOLog ("? AppleUSBAudioStream[%p]::prepareUSBStream () - mStreamInterface->SetAlternateInterface (this, %d) = 0x%x", this, mAlternateSettingID, resultCode);
	FailIf (kIOReturnSuccess != resultCode, Exit);
	
	if (configDictionary->asEndpointHasSampleFreqControl (mInterfaceNumber, mAlternateSettingID))
	{
		FailIf (kIOReturnSuccess != configDictionary->getIsocEndpointAddress (&endpointAddress, mInterfaceNumber, mAlternateSettingID, mDirection), Exit);
		(void) setSampleRateControl (endpointAddress, mCurSampleRate.whole);		// no need to check the error, it's not a real problem if it doesn't work
	}	

	// Acquire a PIPE for the isochronous stream.
	audioIsochEndpoint.type = kUSBIsoc;
	audioIsochEndpoint.direction = mDirection;

	mPipe = mStreamInterface->FindNextPipe (NULL, &audioIsochEndpoint);
	FailIf (NULL == mPipe, Exit);
    mPipe->retain ();

	if (getDirection () == kIOAudioStreamDirectionOutput) 
	{
		// Not concerned with errors in this function at this time.
		(void)checkForFeedbackEndpoint ( configDictionary );
	}

	calculateSamplesPerPacket (mCurSampleRate.whole, &averageFrameSamples, &additionalSampleFrameFreq);
	theFormat = this->getFormat ();
	// [rdar://4664738] Check the maximum packet size of the isoc data endpoint.
	FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointMaxPacketSize ( &maxPacketSize, mInterfaceNumber, mAlternateSettingID, mDirection ), Exit );
	
	// [rdar://4664738] Disallow values of zero for the maximum packet size.
	FailIf ( 0 == maxPacketSize, Exit );
	
	// [rdar://2750290] Make sure we have enough bandwidth (and give back any that we won't be using). 
	if (kUSBIn == mDirection) 
	{
		// [rdar://5355808] [rdar://5889101] Be a little more lenient with this calculation if possible to safeguard against ill-behaved devices.
		maxFrameSize = ( averageFrameSamples + 3 ) * ( theFormat->fNumChannels * ( theFormat->fBitWidth / 8 ) );
		
		// [rdar://4664738] Never call setPipePolicy in excess of the maximum packet size for the endpoint.
		maxFrameSize = ( maxFrameSize > maxPacketSize ) ? maxPacketSize : maxFrameSize;
		debugIOLog ( "? AppleUSBAudioStream[%p]::prepareUSBStream () - maxPacketSize = %d, maxFrameSize = %d", this, maxPacketSize, maxFrameSize );
		mReadUSBFrameSize = maxFrameSize;
	} 
	else 
	{
		if	(		( 0 == additionalSampleFrameFreq )
				&&	( NULL == mAssociatedPipe ) )			// [rdar://5032866]
		{
			maxFrameSize = averageFrameSamples * ( theFormat->fNumChannels * ( theFormat->fBitWidth / 8 ) );
		} 
		else 
		{
			maxFrameSize = ( averageFrameSamples + 1 ) * ( theFormat->fNumChannels * ( theFormat->fBitWidth / 8 ) );
		}
	}

	debugIOLog ("? AppleUSBAudioStream[%p]::prepareUSBStream () - calling SetPipePolicy (%d)", this, maxFrameSize);
	resultCode = mPipe->SetPipePolicy (maxFrameSize, 0);
	FailIf (kIOReturnSuccess != resultCode, Exit);
Exit:
    debugIOLog ("- AppleUSBAudioStream[%p]::prepareUSBStream () = %x", this, resultCode);
    return resultCode;
}

IOReturn AppleUSBAudioStream::startUSBStream (UInt64 currentUSBFrame, UInt32 usbFramesToDelay) {
	const IOAudioStreamFormat *			theFormat;
	IOReturn							resultCode = kIOReturnError;
	IOReturn							interimResult;
	AUAConfigurationDictionary *		configDictionary;
	UInt32								frameListNum;
	UInt16								maxConsecutiveFramesToPrime;
	UInt16								transactionsToQueue;
	UInt16								remainingFrames;
	UInt16								averageFrameSamples;
	UInt16								additionalSampleFrameFreq;
	UInt16								maxFrameSize;
	UInt16								maxPacketSize				= 0;
	bool								encounteredQueuingError = false;

	debugIOLog ("+ AppleUSBAudioStream[%p]::startUSBStream (%lld, %ld, %d)", this, currentUSBFrame, usbFramesToDelay, maxFrameSize);
 
    FailIf ((mNumUSBFrameLists < mNumUSBFrameListsToQueue), Exit);
	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);
	FailIf (NULL == mStreamInterface, Exit);

#if DEBUGTIMESTAMPS
	mStampDrift = 0ll;						// <rdar://problem/7378275>
#endif
	mLastRawTimeStamp_nanos = 0ull;			// <rdar://problem/7378275>
	mLastFilteredTimeStamp_nanos = 0ull;	// <rdar://problem/7378275>
	mLastWrapFrame = 0ull;

	calculateSamplesPerPacket (mCurSampleRate.whole, &averageFrameSamples, &additionalSampleFrameFreq);
	theFormat = this->getFormat ();
	// [rdar://4664738] Check the maximum packet size of the isoc data endpoint.
	FailIf ( kIOReturnSuccess != configDictionary->getIsocEndpointMaxPacketSize ( &maxPacketSize, mInterfaceNumber, mAlternateSettingID, mDirection ), Exit );
	
	// [rdar://4664738] Disallow values of zero for the maximum packet size.
	FailIf ( 0 == maxPacketSize, Exit );
	
	// [rdar://2750290] Make sure we have enough bandwidth (and give back any that we won't be using). 
	if (kUSBIn == mDirection) 
	{
		// [rdar://5355808] [rdar://5889101] Be a little more lenient with this calculation if possible to safeguard against ill-behaved devices.
		maxFrameSize = ( averageFrameSamples + 3 ) * ( theFormat->fNumChannels * ( theFormat->fBitWidth / 8 ) );
		
		// [rdar://4664738] Never call setPipePolicy in excess of the maximum packet size for the endpoint.
		maxFrameSize = ( maxFrameSize > maxPacketSize ) ? maxPacketSize : maxFrameSize;
		debugIOLog ( "? AppleUSBAudioStream[%p]::startUSBStream () - maxPacketSize = %d, maxFrameSize = %d", this, maxPacketSize, maxFrameSize );
		mReadUSBFrameSize = maxFrameSize;
	} 
	else 
	{
		if	(		( 0 == additionalSampleFrameFreq )
				&&	( NULL == mAssociatedPipe ) )			// [rdar://5032866]
		{
			maxFrameSize = averageFrameSamples * ( theFormat->fNumChannels * ( theFormat->fBitWidth / 8 ) );
		} 
		else 
		{
			maxFrameSize = ( averageFrameSamples + 1 ) * ( theFormat->fNumChannels * ( theFormat->fBitWidth / 8 ) );
		}
	}

	// The current frame is already in processing, and it may be nearly done. Must queue a minimum of kMinimumFrameOffset USB frames in the future to ensure
	// that our DMA occurs when we request it.
	mUSBFrameToQueue = currentUSBFrame + kMinimumFrameOffset;
	debugIOLog ("? AppleUSBAudioStream[%p]::startUSBStream () - mUSBFrameToQueue = %llu", this, mUSBFrameToQueue);
	
	if (NULL != mAssociatedPipe) 
	{
		mNextSyncReadFrame = mUSBFrameToQueue;
		debugIOLog ("? AppleUSBAudioStream[%p]::startUSBStream () - Starting feedback endpoint stream at frame %llu", this, mNextSyncReadFrame);
		(void)mAssociatedPipe->Read (mAssociatedEndpointMemoryDescriptor, mNextSyncReadFrame, 1, &mSampleRateFrame, &mSampleRateCompletion);
		debugIOLog ("? AppleUSBAudioStream[%p]::startUSBStream () - Feedback endpoint stream started.", this);		
	}

	// Note that we haven't taken our first time stamp yet. This will help us determine when we should take it.
	mHaveTakenFirstTimeStamp = false;
	
	if (getDirection () == kIOAudioStreamDirectionInput) 
	{				
		#if PRIMEISOCINPUT
		maxConsecutiveFramesToPrime = mSampleBufferSize / maxFrameSize;
		FailIf ( 0 == maxConsecutiveFramesToPrime, Exit );
		
		// Add one transaction for every buffer size in bytes and one for any partial remainder
		transactionsToQueue = ( usbFramesToDelay / maxConsecutiveFramesToPrime ) + ( ( usbFramesToDelay % maxConsecutiveFramesToPrime ) ? 1 : 0 );
		debugIOLog ("? AppleUSBAudioStream[%p]::startUSBStream () - Priming input stream at frame %llu ( %d USB frames ) in %d transaction(s)", this, mUSBFrameToQueue, usbFramesToDelay, transactionsToQueue );
		remainingFrames = usbFramesToDelay;
	
		// No longer does the following as it it compensated by kStartDelayOffset in AppleUSBAudioEngine::performAudioEngineStart().
		// [rdar://5148788] Update the frame to queue. Some of the above operations could cost as as much as a millisecond if we get time-sliced out.
		// currentUSBFrame = mStreamInterface->GetDevice()->GetBus()->GetFrameNumber();
		// mUSBFrameToQueue = currentUSBFrame + kMinimumFrameOffset;
		
		for ( UInt16 currentTransaction = 0; currentTransaction < transactionsToQueue; currentTransaction++ )
		{
			if ( remainingFrames < maxConsecutiveFramesToPrime )
			{
				// This is the last transaction
				primeInputPipe ( mPipe, maxFrameSize, remainingFrames );
				remainingFrames = 0;
			}
			else
			{
				// We will have more to queue after this
				primeInputPipe ( mPipe, maxFrameSize, maxConsecutiveFramesToPrime );
				remainingFrames -= maxConsecutiveFramesToPrime;
			}
		}
		
		#endif
		debugIOLog ("? AppleUSBAudioStream[%p]::startUSBStream () - Starting input stream at frame %llu", this, mUSBFrameToQueue);
		for (frameListNum = mCurrentFrameList; frameListNum < mNumUSBFrameListsToQueue; frameListNum++) 
		{
			interimResult = readFrameList (frameListNum);
			if (kIOReturnSuccess != interimResult)
			{
				debugIOLog ("! AppleUSBAudioStream[%p]::startUSBStream () - readFrameList (%d) failed with error 0x%x!", this, frameListNum, interimResult);
				encounteredQueuingError = true;
			}
		}
	} 
	else 
	{
		#if PRIMEISOCINPUT
		mUSBFrameToQueue += usbFramesToDelay;
		#endif
		
		// This cast can be removed when AbsoluteTime is UInt64.
		* (UInt64 *) ( & (mUSBIsocFrames[0].frTimeStamp)) = 0xFFFFFFFFFFFFFFFFull;
		mUSBIsocFrames[0].frStatus = kUSBLowLatencyIsochTransferKey;
		for (frameListNum = mCurrentFrameList; frameListNum < mNumUSBFrameListsToQueue; frameListNum++) 
		{
			interimResult = writeFrameList (frameListNum);
			if (kIOReturnSuccess != interimResult)
			{
				debugIOLog ("! AppleUSBAudioStream[%p]::startUSBStream () - writeFrameList (%d) failed with error 0x%x!", this, frameListNum, interimResult);
				encounteredQueuingError = true;
			}
		}
	}

	// Here we need to determine if the stream is started to our satisfaction before returning. Currently, we expect to queue exacty mNumUSBFrameListsToQueue.

	if (encounteredQueuingError)
	{
		debugIOLog ("! AppleUSBAudioStream[%p]::startUSBStream () - Stream will *NOT* start because of queuing errors", this);
		resultCode = kIOReturnError;
	}
	else
	{
		mUSBStreamRunning = TRUE;
		resultCode = kIOReturnSuccess;
	}


	if ( kIOReturnSuccess == resultCode )
	{
		debugIOLog ("\n");
		debugIOLog ("  -------------------- Starting Stream (interface %d, alternate setting %d) --------------------", mInterfaceNumber, mAlternateSettingID);
		debugIOLog ("      format = %p", getFormat ());
		debugIOLog ("          fNumChannels = %d", getFormat ()->fNumChannels);
		debugIOLog ("          fBitDepth = %d", getFormat ()->fBitDepth);
		debugIOLog ("          fDriverTag = 0x%x", getFormat ()->fDriverTag);
		debugIOLog ("\n");
	}
	
Exit:
    debugIOLog ("- AppleUSBAudioStream[%p]::startUSBStream () = %x", this, resultCode);
    return resultCode;
}

IOReturn AppleUSBAudioStream::stopUSBStream () {
	debugIOLog ("+ AppleUSBAudioStream[%p]::stopUSBStream ()", this);

	if (0 == mShouldStop) 
	{
		mShouldStop = 1;
	}

	if (NULL != mPipe) 
	{
		if (FALSE == mTerminatingDriver) 
		{
			// <rdar://7251353> Abort() needs to be called first to cancel all existing transfers before returning the
			// bandwidth via SetPipePolicy().
			// <rdar://6277511> Abort the pipe to cancel all pending transactions.
			mPipe->Abort ();

			// Don't call USB if we are being terminated because we could deadlock their workloop.
			mPipe->SetPipePolicy (0, 0);			
		}

		// Have to close the current pipe so we can open a new one because changing the alternate interface will tear down the current pipe
		mPipe->release ();
		mPipe = NULL;
	}

	if (NULL != mAssociatedPipe) 
	{
		if (FALSE == mTerminatingDriver) 
		{
			// <rdar://6277511> Abort the pipe to cancel all pending transactions.
			mAssociatedPipe->Abort ();
		}
		
		mAssociatedPipe->release ();
		mAssociatedPipe = NULL;
	}

	if (FALSE == mTerminatingDriver) 
	{
		// Don't call USB if we are being terminated because we could deadlock their workloop.
		if (NULL != mStreamInterface) 
		{	// if we don't have an interface, message() got called and we are being terminated
			(void)mStreamInterface->SetAlternateInterface (this, kRootAlternateSetting);
		}
	}

	mUSBStreamRunning = FALSE;

	debugIOLog ("- AppleUSBAudioStream[%p]::stopUSBStream ()", this);
	return kIOReturnSuccess;
}

bool AppleUSBAudioStream::willTerminate (IOService * provider, IOOptionBits options) {
	debugIOLog ("+ AppleUSBAudioStream[%p]::willTerminate (%p)", this, provider);

	if ( (mUSBAudioEngine == provider) || (mStreamInterface == provider) )
	{
		mTerminatingDriver = TRUE;
		// [rdar://5535738] Always close the stream interface here. Don't do it in the isoc completion routines.
		debugIOLog ("? AppleUSBAudioStream[%p]::willTerminate () - Closing stream interface", this);
		if	(		( mUSBStreamRunning )
				&&	( NULL != mPipe ) )
		{
			mPipe->Abort ();
			if ( 0 == mShouldStop )
			{
				mShouldStop++;
			}
		}
		if ( NULL != mStreamInterface )
		{
			mStreamInterface->close (this);
		}
	}

	debugIOLog ("- AppleUSBAudioStream[%p]::willTerminate ()", this);

	return super::willTerminate (provider, options);
}

IOReturn AppleUSBAudioStream::writeFrameList (UInt32 frameListNum) {
    IOReturn							result;
	
	result = PrepareWriteFrameList (frameListNum);
	FailIf (kIOReturnSuccess != result, Exit);
	FailIf (NULL == mStreamInterface, Exit);
	result = kIOReturnError;		// reset the error in case mPipe is null

	FailIf (NULL == mPipe, Exit);

	if (mNeedTimeStamps) 
	{
		if (mUHCISupport)
		{
			// We might have to do two separate writes here. Do the first one, and then if necessary, do the second.
			#if DEBUGUHCI
			debugIOLog ("? AppleUSBAudioStream::writeFrameList () - Writing mSampleBufferWrapDescriptors[0]");
			#endif
			result = mPipe->Write (mWrapDescriptors[0], mUSBFrameToQueue, mNumFramesInFirstList, &mUSBIsocFrames[frameListNum * mNumTransactionsPerList], &mUSBCompletion[frameListNum], 1);
		
			// mNumFramesInFirstList must be less than mNumUSBFramesPerList if we wrapped.
			if ( mNumFramesInFirstList < mNumTransactionsPerList )
			{
				#if DEBUGUHCI
				debugIOLog ("? AppleUSBAudioStream::writeFrameList () - writeFrameList: Writing mSampleBufferWrapDescriptors[1] to frame %ld", mUSBFrameToQueue + mNumFramesInFirstList);
				#endif
				result = mPipe->Write (mWrapDescriptors[1], mUSBFrameToQueue + mNumFramesInFirstList, mNumTransactionsPerList - mNumFramesInFirstList, &mUSBIsocFrames[frameListNum * mNumTransactionsPerList + mNumFramesInFirstList], &mExtraUSBCompletion, 1);
				// result = thePipe->Write (mSampleBufferWrapDescriptors[1], usbFrameToQueueAt + mNumFramesInFirstList, numUSBFramesPerList - mNumFramesInFirstList, &theUSBIsocFrames[frameListNumber * numUSBFramesPerList + mNumFramesInFirstList], &extraUsbCompletion, 1);
			}
		}
		else
		{
			result = mPipe->Write (mWrapRangeDescriptor, mUSBFrameToQueue, mNumTransactionsPerList, &mUSBIsocFrames[frameListNum * mNumTransactionsPerList], &mUSBCompletion[frameListNum], 1);
		}
		mNeedTimeStamps = FALSE;
	} 
	else 
	{
		result = mPipe->Write (mSampleBufferDescriptors[frameListNum], mUSBFrameToQueue, mNumTransactionsPerList, &mUSBIsocFrames[frameListNum * mNumTransactionsPerList], &mUSBCompletion[frameListNum], 0);
	}
	FailIf (result != kIOReturnSuccess, Exit);
	
	// keep track of this frame number for time stamping
	if (NULL != mFrameQueuedForList)
	{
		mFrameQueuedForList[frameListNum] = mUSBFrameToQueue;
	}
		
	mUSBFrameToQueue += mNumUSBFramesPerList;

Exit:
	if (kIOReturnSuccess != result)
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::writeFrameList () - failed with error 0x%x", this, result);
	}
	return result;
}

void AppleUSBAudioStream::writeHandler (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames) {
    AppleUSBAudioStream *   self;
    AbsoluteTime            time;
    UInt64                  curUSBFrameNumber;
    UInt32                  frameListToWrite;
    UInt32                  byteOffset;
	UInt32                  frameIndex;
    UInt32                  byteCount;
    UInt32                  preWrapBytes;
	UInt32					numberOfFramesToCheck;
    SInt64                  frameDifference;
    SInt32                  expectedFrames;

    self = (AppleUSBAudioStream *)object;
    FailIf (TRUE == self->mInCompletion, Exit);
    self->mInCompletion = TRUE;
    FailIf (NULL == self->mStreamInterface, Exit);

    curUSBFrameNumber = self->mStreamInterface->GetDevice()->GetBus()->GetFrameNumber ();
    frameDifference = (SInt64)(self->mUSBFrameToQueue - curUSBFrameNumber);
    expectedFrames = (SInt32)(self->mNumUSBFramesPerList * (self->mNumUSBFrameListsToQueue / 2)) + 1;
	numberOfFramesToCheck = 0;
	
	#if DEBUGUHCI
	debugIOLog ("? AppleUSBAudioStream::writeHandler () - writeHandler: curUSBFrameNumber = %llu parameter = 0x%x mUSBFrameToQueue = %llu", curUSBFrameNumber, (UInt32)parameter, self->mUSBFrameToQueue);
	debugIOLog ("? AppleUSBAudioStream::writeHandler () - %llu ?> %lu", frameDifference, expectedFrames);
	#endif
	
	// This logical expression checks to see if IOUSBFamily fell behind. If so, we don't need to advance the frame list
    if (    (frameDifference > expectedFrames) 
         && (    (!(self->mUHCISupport))		// This is not a UHCI connection
              || (0 == parameter)))				// or this is a wrapping condition for a UHCI connection
    {
        debugIOLog ("? AppleUSBAudioStream::writeHandler () - Not advancing frame list");
        goto Exit;
    }
    
    if (kIOReturnAborted != result) 
    {
        
		if (kIOReturnSuccess != result)
		{
			debugIOLog ("! AppleUSBAudioStream::writeHandler () - Frame list %d write returned with error 0x%x", self->mCurrentFrameList, result);
        }
		
		numberOfFramesToCheck = ((self->mUHCISupport && (UInt32) (uintptr_t) parameter) ? self->mNumFramesInFirstList : self->mNumTransactionsPerList);
		if	(		self->mMasterMode 
				&&	(!(self->mHaveTakenFirstTimeStamp))
				&&	(0 == self->mBufferOffset))
		{
			// Check to see if we should take our first time stamp in this frame list.
			for (UInt16 i = 0; i < numberOfFramesToCheck && pFrames; i++)
			{
				if (pFrames[i].frActCount && !self->mShouldStop )					// <rdar://problem/7378275>
				{
					// We should take our first time stamp here. Here, i represents the first frame in the framelist with a nonzero frActCount, i.e., our
					// first isoc data transfer.
					debugIOLog ("? AppleUSBAudioStream::writeHandler () - Taking first time stamp on frame list %lu frame index %d", self->mCurrentFrameList, i); 
					debugIOLog ("     pFrames[%d].frStatus = %lu", i, pFrames[i].frStatus);
					debugIOLog ("     pFrames[%d].frReqCount = %lu", i, pFrames[i].frReqCount);
					debugIOLog ("     pFrames[%d].frActCount = %lu", i, pFrames[i].frActCount);
					debugIOLog ("     pFrames[%d].frTimeStamp = 0x%x", i, * (UInt64 *) &(pFrames[i].frTimeStamp));
					time = self->generateTimeStamp ( ( ( SInt32 ) i ) - 1, 0, 0);	// <rdar://problem/7378275>
					self->takeTimeStamp (FALSE, &time);
					break;
				}
			}
		}
		#ifdef DEBUG
		// Comb the isoc frame list for alarming statuses.
		
		for (UInt16 i = 0; i < numberOfFramesToCheck && pFrames; i++) 
        {
            if	(		(kIOReturnSuccess != pFrames[i].frStatus)
					||	(pFrames[i].frActCount != pFrames[i].frReqCount))
            {
                debugIOLog ("! AppleUSBAudioStream::writeHandler () - pFrames[%d].frStatus = 0x%x", i, pFrames[i].frStatus);
				debugIOLog ("     pFrames[%d].frReqCount = %lu", i, pFrames[i].frReqCount);
				debugIOLog ("     pFrames[%d].frActCount = %lu", i, pFrames[i].frActCount);
				debugIOLog ("     pFrames[%d].frTimeStamp = 0x%x", i, * (UInt64 *) &(pFrames[i].frTimeStamp));
				// IOLog ("AppleUSBAudio: ERROR on output! Short packet of size %lu encountered when %lu bytes were requested.\n", pFrames[i].frActCount, pFrames[i].frReqCount);
            }
        }
		#endif
        
        // skip ahead and see if that helps
		
        if (self->mUSBFrameToQueue <= curUSBFrameNumber) 
        {
			debugIOLog ("! AppleUSBAudioStream::writeHandler - Fell behind! mUSBFrameToQueue = %llu, curUSBFrameNumber = %llu", self->mUSBFrameToQueue, curUSBFrameNumber);
            self->mUSBFrameToQueue = curUSBFrameNumber + kMinimumFrameOffset;
        }
    }

    if (0 != parameter) 
    {
		if ( self->mMasterMode && !self->mShouldStop )			// <rdar://problem/7378275>
		{
			// Take a timestamp
			byteOffset = (UInt32)(uintptr_t)parameter & 0xFFFF;	// <rdar://6300220> Fixed truncation to 8-bit bug which causes audio output to click/pop as the
																// generated time stamp jitters more than 1ms (HALLab will show a red too early time stamp).
			frameIndex = ((UInt32)(uintptr_t)parameter >> 16) - 1;     // zero-indexed frame index in mCurrentFrameList
			byteCount = pFrames[frameIndex].frActCount;     // number of bytes written
			preWrapBytes = byteCount - byteOffset;          // number of bytes written - wrapped bytes
			time = self->generateTimeStamp (frameIndex, preWrapBytes, byteCount);
			self->takeTimeStamp (TRUE, &time);
		}
		
		// Now that we've taken the time stamp, if this is UHCI and the first of two writes, we need to exit now.
		// writeHandlerForUHCI will advance the frame list and queue the next write for us. If we do not let the
		// next write be queued by writeHandlerForUHCI, we will get intermittent artifacts after many minutes of
		// streaming.
		
		if (    (self->mUHCISupport)
			 &&	(frameDifference > expectedFrames - 1))
		{
			// Check to see if we should stop since we're about to skip the normal check
			if (self->mShouldStop > 0) 
			{
				debugIOLog ("? AppleUSBAudioStream::writeHandler() - stopping: %d", self->mShouldStop);
				self->mShouldStop++;
			} // mShouldStop check
			
			// We're done for now. writeHandlerForUHCI will handle the rest.
			goto Exit;
			
		} // if this is the first of two UHCI write callbacks
		
    } // if we have wrapped

	if (self->mCurrentFrameList == self->mNumUSBFrameLists - 1) 
	{
		self->mCurrentFrameList = 0;
	} 
	else 
	{
		self->mCurrentFrameList++;
	}
    
    if (self->mShouldStop > 0) 
    {
        debugIOLog ("? AppleUSBAudioStream::writeHandler() - stopping: %d", self->mShouldStop);
        self->mShouldStop++;
	} 
    else 
    {
		// Queue another write
        frameListToWrite = (self->mCurrentFrameList - 1) + self->mNumUSBFrameListsToQueue;
        if (frameListToWrite >= self->mNumUSBFrameLists) 
        {
            frameListToWrite -= self->mNumUSBFrameLists;
        }
        self->writeFrameList (frameListToWrite);
    }

Exit:
    self->mInCompletion = FALSE;
    return;
}

void AppleUSBAudioStream::writeHandlerForUHCI (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames) {
    AppleUSBAudioStream *   self;
	UInt64                  curUSBFrameNumber;
	SInt64                  frameDifference;
    SInt32                  expectedFrames;
	UInt32                  frameListToWrite;
	UInt32					numberOfFramesToCheck;

    self = (AppleUSBAudioStream *)object;
    FailIf (TRUE == self->mInCompletion, Exit);
    self->mInCompletion = TRUE;
    FailIf (NULL == self->mStreamInterface, Exit);

    curUSBFrameNumber = self->mStreamInterface->GetDevice()->GetBus()->GetFrameNumber ();
    frameDifference = (SInt64)(self->mUSBFrameToQueue - curUSBFrameNumber);
    expectedFrames = (SInt32)(self->mNumUSBFramesPerList * (self->mNumUSBFrameListsToQueue / 2)) + 1;
	numberOfFramesToCheck = 0;
	
	#if DEBUGUHCI
	debugIOLog ("? AppleUSBAudioStream[%p]::writeHandlerForUHCI () - writeHandlerForUHCI: curUSBFrameNumber = %llu parameter = 0x%x mUSBFrameToQueue = %llu", curUSBFrameNumber, (UInt32)parameter, self->mUSBFrameToQueue);
	debugIOLog ("? AppleUSBAudioStream[%p]::writeHandlerForUHCI () -  %llu ?> %lu", frameDifference, expectedFrames);
	#endif
	
	// This logical expression checks to see if IOUSBFamily fell behind. If so, we don't need to advance the frame list
    
	if (kIOReturnAborted != result) 
    {        
		if (kIOReturnSuccess != result)
		{
			debugIOLog ("! AppleUSBAudioStream::writeHandlerForUHCI () - Frame list %d (split for UHCI) write returned with error 0x%x", self->mCurrentFrameList, result);
        }
		#ifdef DEBUG
		// Comb the isoc frame list for alarming statuses.
		numberOfFramesToCheck = (self->mUHCISupport ? (self->mNumTransactionsPerList - self->mNumFramesInFirstList) : self->mNumTransactionsPerList);
		
		for (UInt16 i = 0; i < numberOfFramesToCheck && pFrames; i++) 
        {
            if	(		(kIOReturnSuccess != pFrames[i].frStatus)
					||	(pFrames[i].frActCount != pFrames[i].frReqCount))
            {
                debugIOLog ("! AppleUSBAudioStream::writeHandlerForUHCI () - pFrames[%d].frStatus = 0x%x ", i, pFrames[i].frStatus);
				debugIOLog ("     pFrames[%d].frReqCount = %lu", i, pFrames[i].frReqCount);
				debugIOLog ("     pFrames[%d].frActCount = %lu", i, pFrames[i].frActCount);
				debugIOLog ("     pFrames[%d].frTimeStamp = 0x%x", i, * (UInt64 *) &(pFrames[i].frTimeStamp));
				// IOLog ("AppleUSBAudio: ERROR on output! Short packet of size %lu encountered when %lu bytes were requested.\n", pFrames[i].frActCount, pFrames[i].frReqCount);
            }
        }
		#endif
        
        // skip ahead and see if that helps
        if (self->mUSBFrameToQueue <= curUSBFrameNumber) 
        {
			debugIOLog ("! AppleUSBAudioStream[%p]::writeHandlerForUHCI () - Fell behind! mUSBFrameToQueue = %llu, curUSBFrameNumber = %llu", self->mUSBFrameToQueue, curUSBFrameNumber);
			debugIOLog ("! AppleUSBAudioStream[%p]::writeHandlerForUHCI () - Skipping ahead ...");
            self->mUSBFrameToQueue = curUSBFrameNumber + kMinimumFrameOffset;
        }
    }

    // Advance the frame list
	
	if (self->mCurrentFrameList == self->mNumUSBFrameLists - 1) 
	{
		self->mCurrentFrameList = 0;
	} 
	else 
	{
		self->mCurrentFrameList++;
	}
    
	// Queue another write if we don't need to stop
	// self->mShouldStop is handled by writeHandler ()
	
    if (0 == self->mShouldStop) 
    {
		// Queue another write
        frameListToWrite = (self->mCurrentFrameList - 1) + self->mNumUSBFrameListsToQueue;
        if (frameListToWrite >= self->mNumUSBFrameLists) 
        {
            frameListToWrite -= self->mNumUSBFrameLists;
        }
        self->writeFrameList (frameListToWrite);
    }
	else
	{
		debugIOLog ("? AppleUSBAudioStream[%p]::writeHandlerForUHCI () - Halting.", self);
	}

Exit:
    self->mInCompletion = FALSE;
    return;
}

void AppleUSBAudioStream::takeTimeStamp (bool incrementLoopCount, AbsoluteTime *timestamp)
{
	if ( NULL != mUSBAudioEngine )
	{
		if (false == mHaveTakenFirstTimeStamp)
		{
			mUSBAudioEngine->takeTimeStamp (false, timestamp);
			debugIOLog ("? AppleUSBAudioStream[%p]::takeTimeStamp (0, %p) - First timestamp taken", this, timestamp);
			mHaveTakenFirstTimeStamp = true;
		}
		else
		{
			mUSBAudioEngine->takeTimeStamp (incrementLoopCount, timestamp);
		}
	}
}

#pragma mark -USB Audio Plugin-

void AppleUSBAudioStream::registerPlugin (AppleUSBAudioPlugin * thePlugin) {
	mPlugin = thePlugin;
	mPluginInitThread = thread_call_allocate ((thread_call_func_t)pluginLoaded, (thread_call_param_t)this);

	if (NULL != mPluginInitThread) 
	{
		thread_call_enter (mPluginInitThread);
	}
}

void AppleUSBAudioStream::pluginLoaded (AppleUSBAudioStream * usbAudioStreamObject) {
	IOReturn							result;

	if (usbAudioStreamObject->mPlugin && usbAudioStreamObject->mStreamInterface)
	{
		usbAudioStreamObject->mPlugin->open (usbAudioStreamObject);

		result = usbAudioStreamObject->mPlugin->pluginInit (usbAudioStreamObject, usbAudioStreamObject->mStreamInterface->GetDevice()->GetVendorID (), usbAudioStreamObject->mStreamInterface->GetDevice()->GetProductID ());
		if (result == kIOReturnSuccess) 
		{
			debugIOLog ("success initing the plugin");
			usbAudioStreamObject->mPlugin->pluginSetDirection ((IOAudioStreamDirection) usbAudioStreamObject->mDirection); 
			usbAudioStreamObject->mPlugin->pluginSetFormat (usbAudioStreamObject->getFormat (), &usbAudioStreamObject->mCurSampleRate);
		} 
		else 
		{
			debugIOLog ("Error initing the plugin");
			usbAudioStreamObject->mPlugin->close (usbAudioStreamObject);
			usbAudioStreamObject->mPlugin = NULL;
		}
	}
	
	return;
}

IOReturn AppleUSBAudioStream::pluginDeviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion) {
	IOReturn						result;

	result = kIOReturnBadArgument;
	if (request) 
	{
		result = mUSBAudioDevice->deviceRequest (request, mUSBAudioDevice, completion);
	}

	return result;
}

void AppleUSBAudioStream::pluginSetConfigurationApp (const char * bundleID) {
	if (bundleID) 
	{
		mUSBAudioDevice->setConfigurationApp (bundleID);
	}
}

#pragma mark -AppleUSBAudioStreamNode-

//  <rdar://problem/6686515>
OSDefineMetaClassAndStructors ( AppleUSBAudioStreamNode, IOService )

bool AppleUSBAudioStreamNode::start ( IOService * provider ) 
{
	debugIOLog ( "+ AppleUSBAudioStreamNode[%p]::start (%p)", this, provider );
	
	if ( IOService::start ( provider ) )
	{
		if ( provider )
		{
			provider->setProperty ( "AppleUSBAudioStreamPropertiesReady", "Yes" );
		}
		
		IOService::stop ( provider );	// We know we are going to fail and detach so we should call super stop()
	}

	debugIOLog ( "- AppleUSBAudioStreamNode[%p]::start (%p)", this, provider );
	
	return FALSE;	// to detach and shut down
}

