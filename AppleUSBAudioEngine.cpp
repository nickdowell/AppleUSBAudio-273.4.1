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
//	File:		AppleUSBAudioEngine.cpp
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

#include "AppleUSBAudioEngine.h"
#include "AppleUSBAudioPlugin.h"

#define super IOAudioEngine

OSDefineMetaClassAndStructors(AppleUSBAudioEngine, IOAudioEngine)

#pragma mark -IOKit Routines-

void AppleUSBAudioEngine::free () {
	debugIOLog ("+ AppleUSBAudioEngine[%p]::free ()", this);

	//	<rdar://5811247>
	if (NULL != mClockSelectorControl)
	{
		mClockSelectorControl->release ();
		mClockSelectorControl = NULL;
	}
	
	if (NULL != mStreamInterfaceNumberArray)
	{
		mStreamInterfaceNumberArray->release ();
		mStreamInterfaceNumberArray = NULL;
	}
		
	if (NULL != mIOAudioStreamArray)
	{
		mIOAudioStreamArray->release ();
		mIOAudioStreamArray = NULL;
	}

	mMainOutputStream = NULL;
	mMainInputStream = NULL;
	
	super::free ();
	debugIOLog ("- AppleUSBAudioEngine[%p]::free()", this);
}

bool AppleUSBAudioEngine::init ( OSArray * streamInterfaceNumberArray ) {
	Boolean			result = false;

	debugIOLog("+ AppleUSBAudioEngine[%p]::init ()", this);

	FailIf (NULL == streamInterfaceNumberArray, Exit);
	FailIf (0 == streamInterfaceNumberArray->getCount (), Exit);
	
	result = FALSE;
	FailIf (FALSE == super::init (NULL), Exit);

	mStreamInterfaceNumberArray = streamInterfaceNumberArray;
	mStreamInterfaceNumberArray->retain ();
	
	mIOAudioStreamArray = OSArray::withCapacity (1);
	FailIf ( NULL == mIOAudioStreamArray, Exit );

	// Change this to use defines from the IOAudioFamily when they are available
	setProperty ("IOAudioStreamSampleFormatByteOrder", "Little Endian");

	result = TRUE;
        
Exit:
	debugIOLog("- AppleUSBAudioEngine[%p]::init ()", this);
	return result;
}

bool AppleUSBAudioEngine::requestTerminate (IOService * provider, IOOptionBits options) {
	bool						result = TRUE;

	debugIOLog ("+ AppleUSBAudioEngine[%p]::requestTerminate (%p, %x)", this, provider, options);
	
	// if interface or audio device
	if ( mUSBAudioDevice == provider )
	{
		result = TRUE;		// it is OK to terminate us
	} 
	else 
	{
		result = FALSE;		// don't terminate us
	}
	
	debugIOLog ("- AppleUSBAudioEngine[%p]::requestTerminate (%p, %x) = %d", this, provider, options, result);
	return result;
}

bool AppleUSBAudioEngine::start (IOService * provider, IOAudioDevice * device) {
	bool								resultCode;

	debugIOLog ("+ AppleUSBAudioEngine[%p]::start (%p)", this, provider);

	resultCode = FALSE;		
	
	FailIf (NULL == device, Exit);

	mUSBAudioDevice = OSDynamicCast (AppleUSBAudioDevice, device);
	//	<rdar://62775111>	Retain a reference to the AppleUSBAudioDevice object here so that it doesn't goes away
	//	while it is initializing.
	mUSBAudioDevice->retain ();

	resultCode = super::start (provider, device);
    
Exit:    
	//	<rdar://62775111>	In case it failed, clean up to prevent leakage as stop() will not be called.
	if (!resultCode)
	{
		if (NULL != mUSBAudioDevice)
		{
			mUSBAudioDevice->release ();
			mUSBAudioDevice = NULL;
		}
	}
	
	debugIOLog ("- AppleUSBAudioEngine[%p]::start (%p) = %d", this, provider, resultCode);
	return resultCode;
}
    
void AppleUSBAudioEngine::stop (IOService * provider) {
    debugIOLog("+ AppleUSBAudioEngine[%p]::stop (%p)", this, provider);

	if (NULL != mPluginNotification) 
	{
		mPluginNotification->remove ();
		mPluginNotification = NULL;
	}
	
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

	super::stop (provider);

	debugIOLog ( "- AppleUSBAudioEngine[%p]::stop (%p) - rc=%ld", this, provider, getRetainCount() );
}

bool AppleUSBAudioEngine::terminate (IOOptionBits options) {
	bool							shouldTerminate;
	bool							result;

	result = TRUE;
	shouldTerminate = TRUE;

	debugIOLog ("+ AppleUSBAudioEngine[%p]::terminate ()", this);

	if (shouldTerminate) 
	{
		result = super::terminate (options);
	}

	debugIOLog ("- AppleUSBAudioEngine[%p]::terminate ()", this);

	return result;
}

bool AppleUSBAudioEngine::matchPropertyTable(OSDictionary * table, SInt32 *score)
{
	bool		returnValue = false;
	
	//debugIOLog ("+ AppleUSBAudioEngine[%p]::matchPropertyTable (%p, %p)", this, table, score);
	
	if (super::matchPropertyTable (table, score))
	{
		if (compareProperty (table, kIDVendorString) && 
			compareProperty (table, kIDProductString))
		{
			returnValue = true;
		}
	}

	//debugIOLog ("- AppleUSBAudioEngine[%p]::matchPropertyTable (%p, %p) = %d", this, table, score, returnValue);
	
	return returnValue;
}

// <rdar://7295322> Asynchronous to prevent deadlock if the device or interface is terminated while
// registerService() is performing matching.
void AppleUSBAudioEngine::registerService(IOOptionBits options)
{
	debugIOLog ("+ AppleUSBAudioEngine[%p]::registerService ( 0x%lx )", this, options);

	if ( 0 == ( kIOServiceSynchronous & options ) )
	{
		options |= kIOServiceAsynchronous;
	}
	
	super::registerService ( options );

	debugIOLog ("- AppleUSBAudioEngine[%p]::registerService ( 0x%lx )", this, options);
}

void AppleUSBAudioEngine::openStreamInterfaces () {

	FailIf ( NULL == mIOAudioStreamArray, Exit );
	
	for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
	{
		AppleUSBAudioStream * audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
		FailIf ( NULL == audioStream, Exit );
		
		audioStream->openStreamInterface ();
	}
	
Exit:
	return;
}

void AppleUSBAudioEngine::closeStreamInterfaces () {

	FailIf ( NULL == mIOAudioStreamArray, Exit );
	
	for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
	{
		AppleUSBAudioStream * audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
		FailIf ( NULL == audioStream, Exit );
		
		audioStream->closeStreamInterface ();
	}
	
Exit:
	return;
}

#pragma mark -USB Audio driver-

IOReturn AppleUSBAudioEngine::clipOutputSamples (const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream) {
	IOReturn					result;
	AppleUSBAudioStream *		appleUSBAudioStream;			

	result = kIOReturnError;
	
	appleUSBAudioStream = OSDynamicCast ( AppleUSBAudioStream, audioStream );
	FailIf ( NULL == appleUSBAudioStream, Exit );
	
	appleUSBAudioStream->queueOutputFrames ();
    
	if (TRUE == streamFormat->fIsMixable) 
	{
		if (appleUSBAudioStream->mPlugin)
		{
			appleUSBAudioStream->mPlugin->pluginProcess ((Float32*)mixBuf + (firstSampleFrame * streamFormat->fNumChannels), numSampleFrames, streamFormat->fNumChannels);
		}
		if (mPlugin) 
		{
			if (appleUSBAudioStream == mMainOutputStream)
			{
				mPlugin->pluginProcess ((Float32*)mixBuf + (firstSampleFrame * streamFormat->fNumChannels), numSampleFrames, streamFormat->fNumChannels);
			}
		}
		result = clipAppleUSBAudioToOutputStream (mixBuf, sampleBuf, firstSampleFrame, numSampleFrames, streamFormat);
		
		#if DEBUGLATENCY
			if (!mHaveClipped)
			{
				mHaveClipped = true;
				// debugIOLog ("! FIRST clip at sample frame %lu when current USB Frame is %llu", firstSampleFrame, mUSBAudioDevice->getUSBFrameNumber());
				// debugIOLog ("! AppleUSBAudioEngine::clipOutputSamples() - Sample frame %lu queued for USB frame %llu (current frame = %llu)", firstSampleFrame, appleUSBAudioStream->getQueuedFrameForSample (firstSampleFrame), mUSBAudioDevice->getUSBFrameNumber());
			}
			else if ((0 == firstSampleFrame) || true)
			{
				// debugIOLog ("! clipOutputSamples: Index %lu of sample buffer written when current USB frame is %llu", firstSampleFrame, mUSBAudioDevice->getUSBFrameNumber());
				// debugIOLog ("! AppleUSBAudioEngine::clipOutputSamples() - Sample frame %lu queued for USB frame %llu (current frame = %llu)", firstSampleFrame, appleUSBAudioStream->getQueuedFrameForSample (firstSampleFrame), mUSBAudioDevice->getUSBFrameNumber());
			}
		#endif
		
		if (mUHCISupport)
		{
			// If we've wrapped, copy to sampleBuffer extension create/keep mLastClippedFrame for nonmixable cases.
			// If we return and see frame count lower than mLastClippedFrame, we've wrapped.
			UInt16 alternateFrameSize = appleUSBAudioStream->getAlternateFrameSize ();
			UInt32 start = firstSampleFrame * streamFormat->fNumChannels * (streamFormat->fBitWidth / 8); // first byte index of first sample that is being clipped
			if ( start < alternateFrameSize )
			{
				// This is correct because mAverageFrameSize is in bytes. size is the amount of data we want to copy into the scribble-ahead area. 
				UInt32 size = alternateFrameSize - start;
				memcpy( &( ( ( char * )sampleBuf )[ appleUSBAudioStream->mSampleBufferSize + start ] ), & ( ( ( char * )sampleBuf )[ start ] ), size );
				#if DEBUGUHCI
				debugIOLog( "? AppleUSBAudioEngine::clipOutputSamples () - firstSampleFrame = %d. Just copied %d bytes from %d to %d \n", firstSampleFrame, size, start, start + appleUSBAudioStream->mSampleBufferSize );			
				#endif
			}
		} // UHCI support
	} 
	else 
	{
		// Non-mixable case
		UInt32			offset;

		offset = firstSampleFrame * streamFormat->fNumChannels * (streamFormat->fBitWidth / 8);

		memcpy ((UInt8 *)sampleBuf + offset, (UInt8 *)mixBuf, numSampleFrames * streamFormat->fNumChannels * (streamFormat->fBitWidth / 8));
		mLastClippedFrame = firstSampleFrame + numSampleFrames;
		result = kIOReturnSuccess;
	}

Exit:
	return result;
}

// [rdar://3918719] The following method now does the work of performFormatChange after being regulated by AppleUSBAudioDevice::formatChangeController().
IOReturn AppleUSBAudioEngine::controlledFormatChange (IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate)
{
	IOReturn							result;
	AUAConfigurationDictionary *		configDictionary = NULL;
	UInt32								numSamplesInBuffer;
	bool								needToChangeChannels;
	bool								forcedFormatChange = false;
	// bool								streamIsRunning;
	AppleUSBAudioStream *				appleUSBAudioStream;
	OSDictionary *						channelNamesDictionary = NULL;	// <rdar://6430836>	

	debugIOLog ("+ AppleUSBAudioEngine[%p]::controlledFormatChange (%p, %p, %p)", this, audioStream, newFormat, newSampleRate);

	result = kIOReturnError;

	FailIf (NULL == mStreamInterfaceNumberArray, Exit);
	FailIf (NULL == mUSBAudioDevice, Exit);	
	FailIf (NULL == mUSBAudioDevice->mControlInterface, Exit);		// <rdar://7085810>
	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);

	if	(		(newFormat == NULL)
			&&	(audioStream != NULL))
	{
		result = kIOReturnSuccess;
		goto Exit;
	}
	
	if (NULL == audioStream)
	{
		// This is an emergency format change request initiated to keep the input and output at the same sample rate. We need to supply the correct stream for this engine.
		forcedFormatChange = true;

		audioStream = (NULL != mMainOutputStream) ? mMainOutputStream : mMainInputStream;
		
		// We also need to get the format if it wasn't supplied.
		if (NULL == newFormat)
		{
			newFormat = &(audioStream->format);
		}
	}
	FailIf (NULL == audioStream, Exit);
	FailIf (NULL == newFormat, Exit);
	
	appleUSBAudioStream = OSDynamicCast ( AppleUSBAudioStream, audioStream );
	FailIf ( NULL == appleUSBAudioStream, Exit );
	
	result = appleUSBAudioStream->controlledFormatChange ( newFormat, newSampleRate );
	FailIf ( kIOReturnSuccess != result, Exit );

	for (UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++)
	{
		AppleUSBAudioStream * stream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
		FailIf ( NULL == stream, Exit );
	
		if ( stream != appleUSBAudioStream )
		{
			stream->controlledFormatChange ( stream->getFormat (), newSampleRate );
		}
	}
	
	if (newFormat->fNumChannels != audioStream->format.fNumChannels) 
	{
		needToChangeChannels = TRUE;
		debugIOLog ("? AppleUSBAudioEngine[%p]::controlledFormatChange () - Need to adjust channel controls (cur = %d, new = %d)", this, audioStream->format.fNumChannels, newFormat->fNumChannels);
		
		if (kIOAudioStreamDirectionOutput == appleUSBAudioStream->getDirection ())
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
		debugIOLog ("? AppleUSBAudioEngine[%p]::controlledFormatChange () - Changing sampling rate to %d", this, newSampleRate->whole);
		mCurSampleRate = *newSampleRate;
	} 
	else 
	{
		debugIOLog ("Keeping existing sampling rate of %d", mCurSampleRate.whole);
	}

	// If both input & output streams are present, then use the output stream format.
	if ((appleUSBAudioStream == mMainOutputStream) || ((appleUSBAudioStream == mMainInputStream) && (NULL == mMainOutputStream)))
	{
		if (mPlugin) 
		{
			mPlugin->pluginSetFormat (newFormat, &mCurSampleRate);
		}
	}

	setNumSampleFramesPerBuffer (0);
	
	mAverageSampleRate = mCurSampleRate.whole;	// Set this as the default until we are told otherwise
	debugIOLog ("mAverageSampleRate = %d", mAverageSampleRate);

	// Need a minimum of two pages in OHCI/UHCI
	numSamplesInBuffer = mAverageSampleRate / 4;
	numSamplesInBuffer += (PAGE_SIZE*2 - 1);
	numSamplesInBuffer /= PAGE_SIZE*2;
	numSamplesInBuffer *= PAGE_SIZE*2;

	debugIOLog("? AppleUSBAudioEngine[%p]::controlledFormatChange () - New numSamplesInBuffer = %d", this, numSamplesInBuffer );

	setNumSampleFramesPerBuffer (numSamplesInBuffer);

	if ( TRUE == needToChangeChannels )
	{
		beginConfigurationChange ();
		// <rdar://6430836>	Dictionary for channel names
		channelNamesDictionary = OSDictionary::withCapacity ( 4 );
		FailIf ( NULL == channelNamesDictionary, Exit );
		setProperty ( kIOAudioEngineFullChannelNamesKey, channelNamesDictionary );
		channelNamesDictionary->release ();
		removeAllDefaultAudioControls ();
		for (UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++)
		{
			AppleUSBAudioStream * stream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
			if ( NULL != stream )
			{
				mUSBAudioDevice->createControlsForInterface (this, stream->mInterfaceNumber, stream->mAlternateSettingID);
			}
		}
		//	<rdar://5811247>
		if ( IP_VERSION_02_00 == mUSBAudioDevice->mControlInterface->GetInterfaceProtocol() )
		{
			// Clock selector control for USB Audio 2.0 devices
			doClockSelectorSetup ( appleUSBAudioStream->mInterfaceNumber, appleUSBAudioStream->mAlternateSettingID, mCurSampleRate.whole );
		}	
		completeConfigurationChange ();
	}

	debugIOLog ("? AppleUSBAudioEngine[%p]::controlledFormatChange () - Called setNumSampleFramesPerBuffer with %d", this, appleUSBAudioStream->mSampleBufferSize / (appleUSBAudioStream->mSampleSize ? appleUSBAudioStream->mSampleSize : 1));
	debugIOLog ("? AppleUSBAudioEngine[%p]::controlledFormatChange () - newFormat->fNumChannels = %d, newFormat->fBitWidth %d", this, newFormat->fNumChannels, newFormat->fBitWidth);

	result = kIOReturnSuccess;

Exit:
	debugIOLog ("- AppleUSBAudioEngine[%p]::controlledFormatChange () = 0x%x", this, result);
    return result;

}

// When convertInputSamples is called, we have a window of samples that might possibly still be in transit on the USB bus.
// The number of samples that might be in transit depends on how long our USB read completion routines have been held off.
// Best case is that we have already coalesced all the samples that have been recorded because we weren't held off.
// Worst case is that we've been held off for longer than (framesPerList * mNumUSBFrameListsToQueue) milliseconds and we haven't
// coalesced anything that's been recorded and we don't have any more reads queued up and we're being asked for something
// that hasn't been recorded.
// The normal case should be not much worse than the best case and that's what we're trying to make better.
// The case we are really trying to fix is when we have been held off for (framesPerList * 1 or 2) ms and we have the data
// that's being asked for, it just hasn't been coalesced yet.
// What we have is a window of samples that are outstanding, either they have been recorded and we haven't gotten the
// completion routine called for them, or they are still in the future and haven't been recorded yet.  We must figure out
// where that window is in our buffer and if the request is for sample inside of that window, coalesce and return those
// samples.  If the request is for samples outside of that window, just return those samples because there is no possibility
// that they are in the recording buffer (it's either the worst case, or someone is asking for old data).
// The window goes from (mCurrentFrameList + 1) to (mCurrentFrameList + numFramesListsToQueue) which is at most
// mReadUSBFrameListSize * mNumUSBFrameListsToQueue in size.  It's actually probably smaller than that, but we'll assume that it's
// that big and if can't get coalesce enough bytes from the read buffer, then we'll return old data since there is nothing
// else that we can do anyway (perhaps we could return an error to the HAL too).
IOReturn AppleUSBAudioEngine::convertInputSamples (const void *sampleBuf,
                                                                void *destBuf,
                                                                UInt32 firstSampleFrame,
                                                                UInt32 numSampleFrames,
                                                                const IOAudioStreamFormat *streamFormat,
                                                                IOAudioStream *audioStream) {
	UInt32						lastSampleByte;
	UInt32						windowStartByte;
	UInt32						windowEndByte;
	IOReturn					coalescenceErrorCode = kIOReturnSuccess;
	IOReturn					result = kIOReturnSuccess;
	AppleUSBAudioStream *		appleUSBAudioStream;
	
	#if DEBUGCONVERT
		debugIOLog ("+ AppleUSBAudioEngine::convertInputSamples (%p, %p, %lu, %lu, %p, %p)", sampleBuf, destBuf, firstSampleFrame, numSampleFrames, streamFormat, audioStream);
	#endif

	// <rdar://7298196> Verify that the sample & destination buffers are valid. Also validate that the firstSampleFrame is
	// not equal or larger than the number of sample frames in the buffer.
	FailWithAction ( 0 == sampleBuf, result = kIOReturnError, Exit );
	FailWithAction ( 0 == destBuf, result = kIOReturnError, Exit );
	FailWithAction ( getNumSampleFramesPerBuffer () <= firstSampleFrame, result = kIOReturnError, Exit );
			
	appleUSBAudioStream = OSDynamicCast ( AppleUSBAudioStream, audioStream );
	FailWithAction ( NULL == appleUSBAudioStream, result = kIOReturnError, Exit );
	
//	if (!appleUSBAudioStream->mHaveTakenFirstTimeStamp)
//	{
//		debugIOLog ("! AppleUSBAudioEngine::convertInputSamples () - called before first time stamp!");
//	}

	// <rdar://7298196> Only perform the conversion if the audio engine is running.
	if ( mUSBStreamRunning )
	{
		appleUSBAudioStream->queueInputFrames (); 
		
		// <rdar://problem/7378275>
		if (appleUSBAudioStream->mCoalescenceMutex)
		{
			IORecursiveLockLock (appleUSBAudioStream->mCoalescenceMutex);
		}
		
		lastSampleByte = (firstSampleFrame + numSampleFrames) * streamFormat->fNumChannels * (streamFormat->fBitWidth / 8);
		// Is the request inside our window of possibly recorded samples?
		if (appleUSBAudioStream->mBufferOffset + 1 > appleUSBAudioStream->getSampleBufferSize ()) 
		{
			windowStartByte = 0;
		} 
		else 
		{
			windowStartByte = appleUSBAudioStream->mBufferOffset + 1;
		}
		windowEndByte = windowStartByte + (appleUSBAudioStream->mNumUSBFrameListsToQueue * appleUSBAudioStream->mReadUSBFrameListSize);
		if (windowEndByte > appleUSBAudioStream->getSampleBufferSize ()) 
		{
			windowEndByte -= appleUSBAudioStream->getSampleBufferSize ();
		}
		if ((windowStartByte < lastSampleByte && windowEndByte > lastSampleByte) ||
			(windowEndByte > lastSampleByte && windowStartByte > windowEndByte) ||
			(windowStartByte < lastSampleByte && windowStartByte > windowEndByte && windowEndByte < lastSampleByte)) 
		{
			// debugIOLog ("%ld, %ld, %ld, %ld, %ld, %ld, %ld", firstSampleFrame * 4, numSampleFrames, lastSampleByte, appleUSBAudioStream->mCurrentFrameList, appleUSBAudioStream->mBufferOffset, windowStartByte, windowEndByte);
			if (appleUSBAudioStream->mBufferOffset < lastSampleByte) 
			{
				// [rdar://5355808] Keep track of sample data underruns.
				coalescenceErrorCode = appleUSBAudioStream->CoalesceInputSamples (lastSampleByte - appleUSBAudioStream->mBufferOffset, NULL);
				#if DEBUGLOADING
				debugIOLog ("! AppleUSBAudioEngine::convertInputSamples () - Coalesce from convert %d bytes", lastSampleByte - appleUSBAudioStream->mBufferOffset);
				#endif
			} 
			else 
			{
				// Have to wrap around the buffer.
				UInt32		numBytesToCoalesce = appleUSBAudioStream->getSampleBufferSize () - appleUSBAudioStream->mBufferOffset + lastSampleByte;
				// [rdar://5355808] Keep track of sample data underruns.
				coalescenceErrorCode = appleUSBAudioStream->CoalesceInputSamples (numBytesToCoalesce, NULL);
				#if DEBUGLOADING
				debugIOLog ("! AppleUSBAudioEngine::convertInputSamples () - Coalesce from convert %d bytes (wrapping)", numBytesToCoalesce);
				#endif
			}
		}
	
		// <rdar://problem/7378275>
		if (appleUSBAudioStream->mCoalescenceMutex)
		{
			IORecursiveLockUnlock (appleUSBAudioStream->mCoalescenceMutex);
		}
		result = convertFromAppleUSBAudioInputStream_NoWrap (sampleBuf, destBuf, firstSampleFrame, numSampleFrames, streamFormat);

		if (appleUSBAudioStream->mPlugin)
		{
			appleUSBAudioStream->mPlugin->pluginProcessInput ((float *)destBuf, numSampleFrames, streamFormat->fNumChannels);
		}
		if (mPlugin) 
		{
			if (appleUSBAudioStream == mMainInputStream)
			{
				mPlugin->pluginProcessInput ((float *)destBuf, numSampleFrames, streamFormat->fNumChannels);
			}
		}
		#if 0
		if (0 == firstSampleFrame) // if this convert began with sample 0
		{
			debugIOLog("sample 0 converted on USB frame %llu", mUSBAudioDevice->getUSBFrameNumber());
		}
		#endif
	}

Exit:	
	// [rdar://5355808] Keep track of sample data underruns.
	if ( kIOReturnSuccess != coalescenceErrorCode )
	{
		result = coalescenceErrorCode;
	}
	
	if ( kIOReturnSuccess != result )
	{
		debugIOLog ( "! AppleUSBAudioEngine::convertInputSamples () = 0x%x", result );
	}

	return result;
}

IOReturn AppleUSBAudioEngine::eraseOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{	
	super::eraseOutputSamples (mixBuf, sampleBuf, firstSampleFrame, numSampleFrames, streamFormat, audioStream);

	// if on a UHCI connection and using output, erase extended buffer area; necessary to avoid an audio artifact after stopping the stream
	if (mUHCISupport)
	{
		AppleUSBAudioStream * appleUSBAudioStream = OSDynamicCast ( AppleUSBAudioStream, audioStream );
		FailIf ( NULL == appleUSBAudioStream, Exit );
	
		if ( appleUSBAudioStream->getDirection () == kIOAudioStreamDirectionOutput )
		{
			UInt16 alternateFrameSize = appleUSBAudioStream->getAlternateFrameSize ();
			UInt32 start = firstSampleFrame * streamFormat->fNumChannels * (streamFormat->fBitWidth / 8); // first byte index of first sample that is being clipped
			if ( start < alternateFrameSize )
			{
				UInt32 size = alternateFrameSize - start;
				bzero( &( ( ( char * )sampleBuf )[ appleUSBAudioStream->mSampleBufferSize + start ] ), size );
			}
		}
	}

Exit:
	return kIOReturnSuccess;
}

UInt32 AppleUSBAudioEngine::getCurrentSampleFrame () {
	UInt32	currentSampleFrame;

	currentSampleFrame = 0;
	FailIf (NULL == mMainOutputStream && NULL == mMainInputStream, Exit);
	
	//	<rdar://6343818> getCurrentSampleFrame() is used by the audio engine to perform erase on the 
	//	output stream, so return the output stream value if it is present.
	if (mMainOutputStream)
	{
		currentSampleFrame = mMainOutputStream->getCurrentSampleFrame ();
	} 
	else if (mMainInputStream)
	{
		currentSampleFrame = mMainInputStream->getCurrentSampleFrame ();
	}

Exit:
	return currentSampleFrame;
}

IOReturn AppleUSBAudioEngine::GetDefaultSampleRate (IOAudioSampleRate * sampleRate) {
	IOReturn						result = kIOReturnError;
	UInt16							format;
	UInt8							newAltSettingID;
	UInt8							interfaceNumber;
	IOAudioSampleRate				newSampleRate;
	AUAConfigurationDictionary *	configDictionary = NULL;
	UInt32							streamIndex;
	OSNumber *						streamInterfaceNumber;
	bool							foundSampleRate = false;
	
	debugIOLog ("+ AppleUSBAudioEngine[%p]::GetDefaultSampleRate ()", this);
	result = kIOReturnError;
	
	FailIf (NULL == mStreamInterfaceNumberArray, Exit);
	
	newSampleRate.whole = kDefaultSamplingRate;
	newSampleRate.fraction = 0;
	configDictionary = mUSBAudioDevice->getConfigDictionary ();

	for ( streamIndex = 0; streamIndex < mStreamInterfaceNumberArray->getCount (); streamIndex++ )
	{
		result = kIOReturnError;
		streamInterfaceNumber = OSDynamicCast (OSNumber, mStreamInterfaceNumberArray->getObject (streamIndex));
		FailIf (NULL == streamInterfaceNumber, Exit);
		
		interfaceNumber = streamInterfaceNumber->unsigned8BitValue();
		
		// If possible, never pick anything other than PCM for default.
		// We, as the driver, have to pick a default sample rate, size, and number of channels, so try 16-bit stereo 44.1kHz
		result = configDictionary->getAltSettingWithSettings (&newAltSettingID, interfaceNumber, kChannelDepth_STEREO, kBitDepth_16bits, newSampleRate.whole);
		if (		( kIOReturnSuccess == result )
				&&	( kIOReturnSuccess == configDictionary->getFormat( &format, interfaceNumber, newAltSettingID ) )
				&&	( PCM == format ) )
		{
			foundSampleRate = true;
			break;
		}
	}
	
	if (!foundSampleRate)
	{
		for ( streamIndex = 0; streamIndex < mStreamInterfaceNumberArray->getCount (); streamIndex++ )
		{
			result = kIOReturnError;
			streamInterfaceNumber = OSDynamicCast (OSNumber, mStreamInterfaceNumberArray->getObject (streamIndex));
			FailIf (NULL == streamInterfaceNumber, Exit);
			
			interfaceNumber = streamInterfaceNumber->unsigned8BitValue();
			
			// Didn't have stereo, so try mono
			result = configDictionary->getAltSettingWithSettings (&newAltSettingID, interfaceNumber, kChannelDepth_MONO, kBitDepth_16bits, newSampleRate.whole);
			if (		( kIOReturnSuccess == result )
					&&	( kIOReturnSuccess == configDictionary->getFormat( &format, interfaceNumber, newAltSettingID ) )
					&&	( PCM == format ) )
			{
				foundSampleRate = true;
				break;
			}
		}
	}
	
	if (!foundSampleRate)
	{
		for ( streamIndex = 0; streamIndex < mStreamInterfaceNumberArray->getCount (); streamIndex++ )
		{
			result = kIOReturnError;
			streamInterfaceNumber = OSDynamicCast (OSNumber, mStreamInterfaceNumberArray->getObject (streamIndex));
			FailIf (NULL == streamInterfaceNumber, Exit);
			
			interfaceNumber = streamInterfaceNumber->unsigned8BitValue();
			
			// Don't have a mono or stereo 16-bit 44.1kHz interface, so try for a stereo 16-bit interface with any sample rate
			if (		( kIOReturnSuccess == (result = configDictionary->getAltSettingWithSettings (&newAltSettingID, interfaceNumber, kChannelDepth_STEREO, kBitDepth_16bits ) ) )
					&&	( kIOReturnSuccess == configDictionary->getFormat( &format, interfaceNumber, newAltSettingID )
					&&  ( PCM == format ) ) )
			{
				// we'll run at the highest sample rate that the device has at stereo 16-bit
				configDictionary->getHighestSampleRate (&(newSampleRate.whole), interfaceNumber, newAltSettingID);			
				foundSampleRate = true;
				break;
			}
		}
	}

	if (!foundSampleRate)
	{
		for ( streamIndex = 0; streamIndex < mStreamInterfaceNumberArray->getCount (); streamIndex++ )
		{
			result = kIOReturnError;
			streamInterfaceNumber = OSDynamicCast (OSNumber, mStreamInterfaceNumberArray->getObject (streamIndex));
			FailIf (NULL == streamInterfaceNumber, Exit);
			
			interfaceNumber = streamInterfaceNumber->unsigned8BitValue();
			
			// Don't have a stereo 16-bit interface, so try for a mono 16-bit interface with any sample rate
			if (		( kIOReturnSuccess == (result = configDictionary->getAltSettingWithSettings (&newAltSettingID, interfaceNumber, kChannelDepth_MONO, kBitDepth_16bits ) ) )
					&&	( kIOReturnSuccess == configDictionary->getFormat( &format, interfaceNumber, newAltSettingID )
					&&  ( PCM == format ) ) )
			{
				// we'll run at the highest sample rate that the device has at mono 16-bit
				configDictionary->getHighestSampleRate (&(newSampleRate.whole), interfaceNumber, newAltSettingID);			
				foundSampleRate = true;
				break;
			}
		}
	}

	if (!foundSampleRate)
	{
		result = kIOReturnError;
		streamInterfaceNumber = OSDynamicCast (OSNumber, mStreamInterfaceNumberArray->getObject (0));
		FailIf (NULL == streamInterfaceNumber, Exit);
		
		interfaceNumber = streamInterfaceNumber->unsigned8BitValue();
		
		// Just take the first interface.
		newAltSettingID = configDictionary->alternateSettingZeroCanStream (interfaceNumber) ? 0 : 1;
		debugIOLog ("? AppleUSBAudioEngine[%p]::GetDefaultSampleRate () - Taking first available alternate setting (%d)", this, newAltSettingID);
		FailIf (kIOReturnSuccess != (result = configDictionary->getHighestSampleRate (&(newSampleRate.whole), interfaceNumber, newAltSettingID)), Exit);
	}

	debugIOLog ("? AppleUSBAudioEngine[%p]::GetDefaultSampleRate () - Default sample rate is %d", this, newSampleRate.whole);
	debugIOLog ("? AppleUSBAudioEngine[%p]::GetDefaultSampleRate () - Default alternate setting ID is %d", this, newAltSettingID);
	FailIf (0 == newSampleRate.whole, Exit);
	*sampleRate = newSampleRate;
	result = kIOReturnSuccess;
	
Exit:
	debugIOLog ("- AppleUSBAudioEngine[%p]::GetDefaultSampleRate (%lu) = 0x%x", this, sampleRate->whole, result);
	return result;
}

OSString * AppleUSBAudioEngine::getGlobalUniqueID () {
    char *						uniqueIDStr;
    OSString *					localID;
    OSString *					uniqueID;
	OSNumber *					usbLocation;
	IOReturn					err;
    UInt32						uniqueIDSize;
	UInt32						locationID;
	UInt8						stringIndex;
	UInt8						interfaceNumber;
	char						productString[kStringBufferSize];
	char						manufacturerString[kStringBufferSize];
	char						serialNumberString[kStringBufferSize];
	char						locationIDString[kStringBufferSize];
	char						interfaceNumberString[kStringBufferSize];	// <rdar://7676010>
	// <rdar://problem/6398888>
	OSObject *					nameObject = NULL; 
	OSString *					nameString = NULL;


	usbLocation = NULL;
	uniqueIDStr = NULL;
	localID = NULL;
	uniqueID = NULL;

	FailIf (NULL == mUSBAudioDevice, Exit);
	FailIf (NULL == mUSBAudioDevice->mControlInterface, Exit);

	uniqueIDSize = 5;		// This is the number of ":" in the final string, plus space for the trailing NULL that ends the string
	uniqueIDSize += strlen ("AppleUSBAudioEngine");

	err = kIOReturnSuccess;
	manufacturerString[0] = 0;
	// <rdar://problem/6398888>  Attempt to retrieve the manufacturer name from registry before reading it from the device
	nameObject = mUSBAudioDevice->mControlInterface->GetDevice ()->getProperty ( "USB Vendor Name" );
	if ( nameObject )
	{
		if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
		{
			debugIOLog ( "? AppleUSBAudioEngine[%p]::getGlobalUniqueID () - Retrieved vendor name %s from registry", this, nameString->getCStringNoCopy () );
			strncpy ( manufacturerString, nameString->getCStringNoCopy (), kStringBufferSize );
			err = kIOReturnSuccess;
		} 
	}
	else
	{
		stringIndex = mUSBAudioDevice->getManufacturerStringIndex ();
		if (0 != stringIndex) 
		{
			err = mUSBAudioDevice->getStringDescriptor (stringIndex, manufacturerString, kStringBufferSize);
		}
	}

	if (0 == manufacturerString[0] || kIOReturnSuccess != err) 
	{
		strncpy (manufacturerString, "Unknown Manufacturer", kStringBufferSize);
	}
	uniqueIDSize += strlen (manufacturerString);

	err = kIOReturnSuccess;
	productString[0] = 0;
	// <rdar://problem/6398888>  Attempt to retrieve product name from registry before reading it from the device
	nameObject = mUSBAudioDevice->mControlInterface->GetDevice ()->getProperty ( "USB Product Name" );
	if ( nameObject )
	{
		if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
		{
			debugIOLog ( "? AppleUSBAudioEngine[%p]::getGlobalUniqueID () - Retrieved product name %s from registry", this, nameString->getCStringNoCopy () );
			strncpy ( productString, nameString->getCStringNoCopy (), kStringBufferSize );
			err = kIOReturnSuccess;
		} 
	}
	else
	{
		stringIndex = mUSBAudioDevice->getProductStringIndex ();
		if (0 != stringIndex) 
		{
			err = mUSBAudioDevice->getStringDescriptor (stringIndex, productString, kStringBufferSize);
		}
	}

	if (0 == productString[0] || kIOReturnSuccess != err) 
	{
		strncpy (productString, "Unknown USB Audio Device", kStringBufferSize);
	}
	uniqueIDSize += strlen (productString);

	// <rdar://problem/6470583>
	stringIndex = 0;
	err = kIOReturnSuccess;
	serialNumberString[0] = 0;
	// <rdar://problem/6398888>  Attempt to retrieve serial number from registry before reading it from the device
	nameObject = mUSBAudioDevice->mControlInterface->GetDevice ()->getProperty ( "USB Serial Number" );
	if ( nameObject )
	{
		if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
		{
			debugIOLog ( "? AppleUSBAudioEngine[%p]::getGlobalUniqueID () - Retrieved serial number %s from registry", this, nameString->getCStringNoCopy () );
			strncpy ( serialNumberString, nameString->getCStringNoCopy (), kStringBufferSize );
			err = kIOReturnSuccess;
		} 
	}
	else
	{
		stringIndex = mUSBAudioDevice->getSerialNumberStringIndex ();
		if (0 != stringIndex) 
		{
			err = mUSBAudioDevice->getStringDescriptor (stringIndex, serialNumberString, kStringBufferSize);
		}
	}
	
	if (0 == serialNumberString[0] || kIOReturnSuccess != err) 
	{
		// device doesn't have a serial number, get its location ID
		usbLocation = mUSBAudioDevice->getLocationID ();
		if (NULL != usbLocation) 
		{
			locationID = usbLocation->unsigned32BitValue ();
			snprintf (locationIDString, kStringBufferSize, "%x", locationID);
		} 
		else 
		{
			strncpy (locationIDString, "Unknown location", kStringBufferSize);
		}
		uniqueIDSize += strlen (locationIDString);
	} 
	else 
	{
		// device has a serial number that we can use to track it
		debugIOLog ("? AppleUSBAudioEngine[%p]::getGlobalUniqueID () - Device has a serial number = %s", this, serialNumberString);
		uniqueIDSize += strlen (serialNumberString);
	}

	// <rdar://7676010> Concatenate the interface number(s).
	interfaceNumberString[0] = 0;
	for ( UInt32 index = 0; index < mStreamInterfaceNumberArray->getCount (); index++ )
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast ( OSNumber, mStreamInterfaceNumberArray->getObject ( index ) );
		if (NULL != streamInterfaceNumber)
		{
			interfaceNumber = streamInterfaceNumber->unsigned8BitValue ();
			snprintf (interfaceNumberString, 4, "%s%s%d", interfaceNumberString, (index == 0) ? "" : ",", interfaceNumber);
			uniqueIDSize += strlen (interfaceNumberString) + ( (index == 0) ? 0 : 1 );
		}
	}
	
	uniqueIDStr = (char *)IOMalloc (uniqueIDSize);

	if (NULL != uniqueIDStr) 
	{
		uniqueIDStr[0] = 0;
	
		if (0 == serialNumberString[0]) 
		{
			snprintf (uniqueIDStr, uniqueIDSize, "AppleUSBAudioEngine:%s:%s:%s", manufacturerString, productString, locationIDString);
		} 
		else 
		{
			snprintf (uniqueIDStr,uniqueIDSize,  "AppleUSBAudioEngine:%s:%s:%s", manufacturerString, productString, serialNumberString);
		}

		// <rdar://7676010> Concatenate the interface number(s).
		strncat ( uniqueIDStr, ":", 1 );
		strncat ( uniqueIDStr, interfaceNumberString, kStringBufferSize );

		uniqueID = OSString::withCString (uniqueIDStr);
		debugIOLog ("AppleUSBAudioEngine[%p]::getGlobalUniqueID () - getGlobalUniqueID = %s", this, uniqueIDStr);
		IOFree (uniqueIDStr, uniqueIDSize);
	}

Exit:

	return uniqueID;
}

//--------------------------------------------------------------------------------
bool AppleUSBAudioEngine::initHardware (IOService *provider) {
	OSNumber *							idVendor = NULL;
	OSNumber *							idProduct = NULL;
	AUAConfigurationDictionary *		configDictionary;
	IOReturn							resultCode;
	Boolean								resultBool;
	OSObject *							nameObject = NULL;
	OSString *							nameString = NULL;
	OSString *							coreAudioPluginPathString = NULL;
	char *								streamDescription = NULL;
	bool								syncOutputCompensation = false;	//	<rdar://6343818>
	bool								syncInputCompensation = false;	//	<rdar://6343818>
	AppleUSBAudioStream	*				masterStream = NULL;
	OSDictionary *						channelNamesDictionary = NULL;	//	<rdar://6430836>
	OSObject *							propertiesReadyObject = NULL;		//  <rdar://problem/6686515>
	UInt32								numTriesForStreamPropertiesReady;	//  <rdar://problem/6686515>


    debugIOLog ("+ AppleUSBAudioEngine[%p]::initHardware (%p)", this, provider);

    resultBool = FALSE;
	mTerminatingDriver = FALSE;

    FailIf (FALSE == super::initHardware (provider), Exit);

	FailIf (NULL == mUSBAudioDevice, Exit);							// <rdar://7085810>
	FailIf (NULL == mUSBAudioDevice->mControlInterface, Exit);		// <rdar://7085810>
	FailIf (NULL == mStreamInterfaceNumberArray, Exit);
	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary ()), Exit);

	// Choose default sampling rate ( rdar://3866513 )
	GetDefaultSampleRate (&sampleRate);
	mCurSampleRate = sampleRate;

	// See if UHCI support is necessary
	mUHCISupport = mUSBAudioDevice->checkForUHCI ();
	
	mSplitTransactions = mUSBAudioDevice->detectSplitTransactions ();
	
	setSampleRate (&sampleRate);

	// Store default sample rate
	mDefaultAudioSampleRate = sampleRate;
	
	// <rdar://6430836>	Dictionary for channel names
	channelNamesDictionary = OSDictionary::withCapacity ( 4 );
	FailIf ( NULL == channelNamesDictionary, Exit );
	setProperty ( kIOAudioEngineFullChannelNamesKey, channelNamesDictionary );
	channelNamesDictionary->release ();

	mStartInputChannelID = 1;
	mStartOutputChannelID = 1;
	
	for ( UInt32 streamIndex = 0; streamIndex < mStreamInterfaceNumberArray->getCount (); streamIndex++ )
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, mStreamInterfaceNumberArray->getObject (streamIndex) );
		FailIf (NULL == streamInterfaceNumber, Exit);

		IOUSBInterface * streamInterface = mUSBAudioDevice->getUSBInterface (streamInterfaceNumber->unsigned8BitValue ());
		FailIf (NULL == streamInterface, Exit);
		
		//  <rdar://problem/6686515> Wait for stream interface nubs to appear to give opportunity for override kext properties to merge
		propertiesReadyObject = NULL;
		
		for ( numTriesForStreamPropertiesReady = 0; numTriesForStreamPropertiesReady < kMaxTriesForStreamPropertiesReady; numTriesForStreamPropertiesReady++ )
		{
			propertiesReadyObject = streamInterface->getProperty ( "AppleUSBAudioStreamPropertiesReady" );
			if ( propertiesReadyObject )
			{
				debugIOLog ( "! AppleUSBAudioStream[%p]::initHardware () - AppleUSBAudioStreamPropertiesReady found for stream #%d", this, streamInterfaceNumber->unsigned8BitValue () );
				break;
			}
			debugIOLog ( "! AppleUSBAudioStream[%p]::initHardware () - AppleUSBAudioStreamPropertiesReady NOT found for stream #%d, sleeping 10 ms...", this, streamInterfaceNumber->unsigned8BitValue () );
			IOSleep ( 10 );
			FailIf ( mUSBAudioDevice->isInactive (), Exit );
		}
				
		// Instantiate one AppleUSBStream per IOUSBInterface.
		AppleUSBAudioStream  * audioStream = OSTypeAlloc ( AppleUSBAudioStream );
		FailIf (NULL == audioStream, Exit);
		
		// If this engine supports more than 1 audio stream, show different description for the streams.
		if ( mStreamInterfaceNumberArray->getCount () > 1 )
		{
			nameObject = streamInterface->getProperty ( "USB Interface Name" );
			if ( nameObject )
			{
				if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
				{
					debugIOLog ( "! AppleUSBAudioStream[%p]::initHardware () - Retrieved product name %s", this, nameString->getCStringNoCopy () );
					streamDescription = (char *)nameString->getCStringNoCopy();
				} 
			}
		}
		
		FailWithAction (!audioStream->initWithAudioEngine (mUSBAudioDevice, this, streamInterface, sampleRate, streamDescription), audioStream->release(), Exit);

		mIOAudioStreamArray->setObject ( audioStream );		

		if (kIOAudioStreamDirectionOutput == audioStream->getDirection ())
		{
			if (NULL == mMainOutputStream)
			{
				mMainOutputStream = audioStream;
			}
			else
			{
				//	<rdar://6343818> If the output is adaptive, use it as the main output stream.
				if ( kAdaptiveSyncType == audioStream->getSyncType () )
				{
					mMainOutputStream = audioStream;
				}
			}
		}
		if (NULL == mMainInputStream && kIOAudioStreamDirectionInput == audioStream->getDirection ())
		{
			mMainInputStream = audioStream;
		}

		FailWithAction (!audioStream->configureAudioStream (sampleRate), audioStream->release(), Exit);

		FailWithAction (kIOReturnSuccess != (resultCode = addAudioStream (audioStream)), audioStream->release(), Exit);

		if ( kIOAudioStreamDirectionOutput == audioStream->getDirection () )
		{
			mStartOutputChannelID += audioStream->getFormat ()->fNumChannels;
		}
		else
		{
			mStartInputChannelID += audioStream->getFormat ()->fNumChannels;
		}
				
		audioStream->release ();
	}
	
	//	<rdar://6343818> Determine which of the stream is to be the master stream.
	if ( ( NULL != mMainOutputStream ) && ( kAdaptiveSyncType == mMainOutputStream->getSyncType () ) )
	{
		// The main output stream is present & adaptive, so designate it as the master stream to generate time stamps.
		masterStream = mMainOutputStream;

		debugIOLog ("? AppleUSBAudioEngine[%p]::initHardware (%p) - Main output stream (%p) is adaptive and designated as master", this, provider, mMainOutputStream);

	}
	else
	{
		// Otherwise, use the input stream as master stream if it is present. 
		if ( NULL != mMainInputStream )
		{
			masterStream = mMainInputStream;
		}
		else
		{
			masterStream = mMainOutputStream;
		}

		// If there is more than 1 audio streams present, then we need to compensate for synchronization between the input & output
		// streams running on the same engine.
		if ( 1 < mIOAudioStreamArray->getCount () )
		{
			syncOutputCompensation = true;
		}
	}
	
	FailIf ( NULL == masterStream, Exit );
	
	masterStream->setMasterStreamMode ( true );
		
	debugIOLog ("? AppleUSBAudioEngine[%p]::initHardware (%p) - Compensate for output synchronization: %d", this, provider, syncOutputCompensation);
		
	if ( syncOutputCompensation )
	{
		//	<rdar://6343818> Compensate for the synchronization on the output since the time stamps are generated on the input stream.
		for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
		{
			AppleUSBAudioStream * audioStream = OSDynamicCast ( AppleUSBAudioStream, mIOAudioStreamArray->getObject ( streamIndex ) );
			
			if ( NULL != audioStream && kIOAudioStreamDirectionOutput == audioStream->getDirection () )
			{
				audioStream->compensateForSynchronization ( true );
				audioStream->updateSampleOffsetAndLatency ();
			}
		}
	}

	debugIOLog ("? AppleUSBAudioEngine[%p]::initHardware (%p) - Compensate for input synchronization: %d", this, provider, syncInputCompensation);

	if ( syncInputCompensation )
	{
		//	<rdar://6343818> Compensate for the synchronization on the input since the time stamps are generated on the output stream.
		for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
		{
			AppleUSBAudioStream * audioStream = OSDynamicCast ( AppleUSBAudioStream, mIOAudioStreamArray->getObject ( streamIndex ) );
			
			if ( NULL != audioStream && kIOAudioStreamDirectionInput == audioStream->getDirection () )
			{
				audioStream->compensateForSynchronization ( true );
				audioStream->updateSampleOffsetAndLatency ();
			}
		}
	}

	// <rdar://7558825>	Clock domain for different synchronization type.
	switch ( masterStream->getSyncType () )
	{
		case kSynchronousSyncType:
		case kAdaptiveSyncType:
			setClockDomain ( getSystemClockDomain () );
			break;
		default:
			setClockDomain ();
			break;
	}
	
	// If there is more than one audio streams per engine, then use the name of the control interface. Otherwise,
	// use the name of the stream interface if available.
	if ( mIOAudioStreamArray->getCount () > 1 )
	{
		nameObject = mUSBAudioDevice->mControlInterface->getProperty ( "USB Interface Name" );
	}
	else
	{
		// [rdar://problem/5797877] Show different names for input and output interfaces of a USB Audio device
		if ((NULL != mMainOutputStream) && (NULL != mMainOutputStream->mStreamInterface))
		{
			nameObject = mMainOutputStream->mStreamInterface->getProperty ( "USB Interface Name" );
		}
		else if ((NULL != mMainInputStream) && (NULL != mMainInputStream->mStreamInterface))
		{
			nameObject = mMainInputStream->mStreamInterface->getProperty ( "USB Interface Name" );
		}
	}
	if ( nameObject )
	{
		if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
		{
			debugIOLog ( "! AppleUSBAudioEngine[%p]::initHardware () - Retrieved product name %s", this, nameString->getCStringNoCopy () );
			setDescription ( nameString->getCStringNoCopy() );
		} 
	}

	// If there is more than one audio streams per engine, then look for the plugin path specified in the control interface
	// first. If it is not there, then look for the plugin path in the stream interface.
	// If there is only one audio stream per engine, then look for the the plugin path in the stream interface first. If it 
	// is not there, look for the plugin path in the specified in the control interface.
	if ( mIOAudioStreamArray->getCount () > 1 )
	{
		coreAudioPluginPathString = OSDynamicCast ( OSString, mUSBAudioDevice->mControlInterface->getProperty ( kIOAudioEngineCoreAudioPlugInKey ) );
	}
	if (NULL == coreAudioPluginPathString)
	{
		if ((NULL != mMainOutputStream) && (NULL != mMainOutputStream->mStreamInterface))
		{
			coreAudioPluginPathString = OSDynamicCast ( OSString,  mMainOutputStream->mStreamInterface->getProperty ( kIOAudioEngineCoreAudioPlugInKey ) );
		}
		else if ((NULL != mMainInputStream) && (NULL != mMainInputStream->mStreamInterface))
		{
			coreAudioPluginPathString = OSDynamicCast ( OSString,  mMainInputStream->mStreamInterface->getProperty ( kIOAudioEngineCoreAudioPlugInKey ) );
		}
	}
	if (NULL == coreAudioPluginPathString)
	{
		coreAudioPluginPathString = OSDynamicCast ( OSString, mUSBAudioDevice->mControlInterface->getProperty ( kIOAudioEngineCoreAudioPlugInKey ) );
	}
	if (NULL != coreAudioPluginPathString)
	{
		debugIOLog ( "! AppleUSBAudioEngine[%p]::initHardware () - Retrieved CoreAudio plugin path %s", this, coreAudioPluginPathString->getCStringNoCopy () );
		setProperty ( kIOAudioEngineCoreAudioPlugInKey, coreAudioPluginPathString );
	}
	
    resultBool = TRUE;


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
	// <rdar://problem/6576824> AppleUSBAudio fails to unload properly
	if ( !resultBool )
	{
		// <rdar://7295322> Clean up the default audio controls in the case when the engine becomes inactive while the starting.
		// The controls doesn't get detach from the engine by IOAudioEngine::removeAllDefaultAudioControls() as the engine
		// is already inactive.
		if (defaultAudioControls) 
		{
			if ( isInactive() ) 
			{
				OSCollectionIterator *controlIterator;
				
				controlIterator = OSCollectionIterator::withCollection(defaultAudioControls);
				
				if (controlIterator) {
					IOAudioControl *control;
					
					while (control = (IOAudioControl *)controlIterator->getNextObject()) 
					{
						control->detach(this);
					}
					
					controlIterator->release();
				}
			}
		}
		
		stop ( provider );
	}
	
    debugIOLog("- AppleUSBAudioEngine[%p]::initHardware(%p), resultCode = %x, resultBool = %d", this, provider, resultCode, resultBool);
    return resultBool;
}

//--------------------------------------------------------------------------------
// <rdar://7558825> Get the system clock domain
UInt32 AppleUSBAudioEngine::getSystemClockDomain ( void )
{
	UInt32					result = kIOAudioNewClockDomain;
	OSDictionary *			serviceMatchingDict = 0;
	OSIterator *			iterator = 0;
	OSObject *				object;
	
	serviceMatchingDict = serviceMatching ( "AppleHDAController" );
	FailIf ( 0 == serviceMatchingDict, Exit );
	
	iterator = getMatchingServices ( serviceMatchingDict );
	FailIf ( 0 == iterator, Exit );
	
	object = iterator->getNextObject ();
	
	while ( object )
	{
		IOService * service = OSDynamicCast ( IOService, object );
		
		if ( service )
		{
			IOService * provider = service->getProvider ();
			
			if ( provider )
			{
				if ( 0 == strncmp ( provider->getName (), "HDEF", sizeof ( "HDEF" ) ) )
				{
					result = ( UInt32 )( uintptr_t )( service );
					break;
				}
			}
		}
		
		object = iterator->getNextObject ();
	}
	
Exit:
	if ( serviceMatchingDict )
	{
		serviceMatchingDict->release ();
	}
	if ( iterator )
	{
		iterator->release ();
	}
	
	return result;
}

//	<rdar://6430836> <rdar://problem/6706026>	Retrieve the channel name for the specified channel.
OSString * AppleUSBAudioEngine::getChannelNameString ( UInt8 unitID, UInt8 channelNum )
{
	AUAConfigurationDictionary *		configDictionary = NULL;
	UInt8								controlInterfaceNum;
	AudioClusterDescriptor				clusterDescriptor;
	UInt8								numPreDefinedChannels = 0;
	char								stringBuffer[kStringBufferSize];
	IOReturn							result = kIOReturnNotFound;
	OSString *							theString = NULL;

	FailIf ( NULL == mUSBAudioDevice, Exit );
	FailIf ( NULL == mUSBAudioDevice->mControlInterface, Exit );		// <rdar://7085810>
	controlInterfaceNum = mUSBAudioDevice->mControlInterface->GetInterfaceNumber();

	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);

	FailIf ( kIOReturnSuccess != configDictionary->getAudioClusterDescriptor ( &clusterDescriptor, controlInterfaceNum, 0, unitID ), Exit );

	debugIOLog ("? AppleUSBAudioEngine[%p]::getChannelNameString (%d, %d) - Audio cluster descriptor { %d, %d, %d }", this, unitID, channelNum, clusterDescriptor.bNrChannels, clusterDescriptor.bmChannelConfig, clusterDescriptor.iChannelNames);
	
	// See if the channel has a predefined spatial location.
	for ( UInt8 channelIndex = 0; channelIndex < sizeof ( UInt32 ) * 8; channelIndex++ )		// <rdar://problem/6679955>
	{
		if ( clusterDescriptor.bmChannelConfig & ( 1 << channelIndex ) )
		{
			numPreDefinedChannels++;
		}
		
		if ( numPreDefinedChannels == channelNum )
		{
			// Found the channel that has pre-defined spatial location. Use the spatial location name.
			strncpy ( stringBuffer, ChannelConfigString ( channelIndex ), kStringBufferSize );
			result = kIOReturnSuccess;
			break;
		}
	}
	
	if ( ( numPreDefinedChannels != channelNum ) && ( channelNum <= clusterDescriptor.bNrChannels ) )
	{
		// Channel doesn't have a predefined spatial location. See if iChannelNames is present, and use
		// it to get the name.
		if ( 0 != clusterDescriptor.iChannelNames )
		{
			result = mUSBAudioDevice->getStringDescriptor ( clusterDescriptor.iChannelNames + channelNum - 1 - numPreDefinedChannels, stringBuffer, kStringBufferSize );
			debugIOLog ("? AppleUSBAudioDevice::getChannelNameString (%d, %d) - stringIndex = %d, stringBuffer = %s, result = 0%x", unitID, channelNum, clusterDescriptor.iChannelNames + channelNum - 1 - numPreDefinedChannels, stringBuffer, result);
		}
	}
	
	if ( kIOReturnSuccess == result )
	{
		debugIOLog ("? AppleUSBAudioDevice::getChannelNameString () - terminalID = %d, channelNum = %d, stringBuffer = %s", unitID, channelNum, stringBuffer);
		theString = OSString::withCString ( stringBuffer );
	}
	
Exit:
	return theString;
}

//	<rdar://6430836> <rdar://problem/6706026> Update the channel names in the IOAudioEngineFullChannelNames dictionary for the specified stream.
void AppleUSBAudioEngine::updateChannelNames ( OSArray* thePath, UInt8 interfaceNum, UInt8 altSettingNum ) {
	OSDictionary *						oldChannelNamesDictionary = NULL;	//	<rdar://6500720>
	OSDictionary *						newChannelNamesDictionary = NULL;	//	<rdar://6500720>
	AUAConfigurationDictionary *		configDictionary = NULL;
	UInt8								controlInterfaceNum;
	char								keyString[32];
	OSString *							nameString = NULL;
	AudioClusterDescriptor				clusterDescriptor;
	// <rdar://problem/6706026>
	UInt32								unitIndex;
	UInt8								subType;
	OSNumber *							number;
	UInt8								unitID = 0;
	
    debugIOLog ("+ AppleUSBAudioEngine[%p]::updateChannelNames (%d, %d)", this, interfaceNum, altSettingNum);

	FailIf ( NULL == mUSBAudioDevice, Exit );
	FailIf ( NULL == mUSBAudioDevice->mControlInterface, Exit );		// <rdar://7085810>
	controlInterfaceNum = mUSBAudioDevice->mControlInterface->GetInterfaceNumber();

	FailIf (NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary()), Exit);
		
	for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
	{
		AppleUSBAudioStream * audioStream = OSDynamicCast ( AppleUSBAudioStream, mIOAudioStreamArray->getObject ( streamIndex ) );
		
		if ( NULL != audioStream &&  interfaceNum == audioStream->mInterfaceNumber && altSettingNum == audioStream->mAlternateSettingID )
		{
			debugIOLog ("? AppleUSBAudioEngine[%p]::updateChannelNames (%d, %d) - Found audio stream = %p", this, interfaceNum, altSettingNum, audioStream);

			//	<rdar://6500270> Panic trying to change a collection in the registry on K64. IORegistryEntry object marks collections immutable 
			//	when set as properties of a registry entry that's attached to a plane.
			oldChannelNamesDictionary = OSDynamicCast ( OSDictionary, getProperty ( kIOAudioEngineFullChannelNamesKey ) );
			FailIf ( NULL == oldChannelNamesDictionary, Exit );
			
			newChannelNamesDictionary = OSDictionary::withDictionary ( oldChannelNamesDictionary );
			FailIf ( NULL == newChannelNamesDictionary, Exit );			
			
			// <rdar://problem/6706026> Find the channel names from either the mixer, processing, extension unit, or input terminal closest to the output terminal.  The driver should use the first unit that has an audio cluster descriptor.
			FailIf ( NULL == thePath, Exit );
			
			for ( unitIndex = 1; unitIndex < thePath->getCount (); unitIndex++ )	// Search from the unit connected to the output terminal (index 0 + 1) to the input terminal
			{
				number = OSDynamicCast ( OSNumber, thePath->getObject ( unitIndex ) );
				if ( ( NULL != number ) && ( kIOReturnSuccess == configDictionary->getSubType ( &subType, controlInterfaceNum, 0, number->unsigned8BitValue () ) ) )
				{
					if	( MIXER_UNIT == subType || PROCESSING_UNIT == subType || EXTENSION_UNIT == subType || INPUT_TERMINAL == subType )
					{
						unitID = number->unsigned8BitValue ();
						FailIf ( kIOReturnSuccess != configDictionary->getAudioClusterDescriptor ( &clusterDescriptor, controlInterfaceNum, 0, unitID ), Exit );
						if ( clusterDescriptor.bNrChannels > 0 )	// We found an audio cluster descriptor
						{
							break;
						}
					}
				}
			}
			
			debugIOLog ("? AppleUSBAudioEngine[%p]::updateChannelNames (%d, %d, %d) - Audio cluster descriptor { %d, %d, %d }", this, unitID, interfaceNum, altSettingNum, clusterDescriptor.bNrChannels, clusterDescriptor.bmChannelConfig, clusterDescriptor.iChannelNames);

			for ( UInt8 channelIndex = 0; channelIndex < clusterDescriptor.bNrChannels; channelIndex++ )
			{
				nameString = getChannelNameString ( unitID, channelIndex + 1 );			// <rdar://problem/6706026>
				if ( NULL != nameString )
				{
					keyString[0] = 0;
					if ( kIOAudioStreamDirectionOutput == audioStream->getDirection () )
					{
						snprintf ( keyString, 32, kIOAudioEngineFullChannelNameKeyOutputFormat, audioStream->getStartingChannelID () + channelIndex );
					}
					else
					{
						snprintf ( keyString, 32, kIOAudioEngineFullChannelNameKeyInputFormat, audioStream->getStartingChannelID () + channelIndex );
					}
					
					debugIOLog ("? AppleUSBAudioEngine[%p]::updateChannelNames (%d, %d, %d) - Setting %s to %s", this, unitID, interfaceNum, altSettingNum, keyString, nameString->getCStringNoCopy());
					
					newChannelNamesDictionary->setObject ( keyString, nameString );	//	<rdar://6500270> 
					nameString->release ();
				}
			}
			//	<rdar://6500270> Update the dictionary.
			setProperty ( kIOAudioEngineFullChannelNamesKey, newChannelNamesDictionary );
			newChannelNamesDictionary->release ();
			break;
		}
	}
	
Exit:
    debugIOLog ("- AppleUSBAudioEngine[%p]::updateChannelNames (%d, %d, %d)", this, unitID, interfaceNum, altSettingNum);
	return;
}

char * AppleUSBAudioEngine::ChannelConfigString (UInt8 channel) 
{
	char *					channelNameString;

	switch (channel) {
	#if LOCALIZABLE
		case 0:												channelNameString = (char*)"StringFrontLeft";									break;
		case 1:												channelNameString = (char*)"StringFrontRight";									break;
		case 2:												channelNameString = (char*)"StringFrontCenter";									break;
		case 3:												channelNameString = (char*)"StringLowFrequencyEffects";							break;
		case 4:												channelNameString = (char*)"StringBackLeft";									break;
		case 5:												channelNameString = (char*)"StringBackRight";									break;
		case 6:												channelNameString = (char*)"StringFrontLeftofCenter";							break;
		case 7:												channelNameString = (char*)"StringFrontRightofCenter";							break;
		case 8:												channelNameString = (char*)"StringBackCenter";									break;
		case 9:												channelNameString = (char*)"StringSideLeft";									break;
		case 10:											channelNameString = (char*)"StringSideRight";									break;
		case 11:											channelNameString = (char*)"StringTopCenter";									break;
		case 12:											channelNameString = (char*)"StringTopFrontLeft";								break;
		case 13:											channelNameString = (char*)"StringTopFrontCenter";								break;
		case 14:											channelNameString = (char*)"StringTopFrontRight";								break;
		case 15:											channelNameString = (char*)"StringTopBackLeft";									break;
		case 16:											channelNameString = (char*)"StringTopBackCenter";								break;
		case 17:											channelNameString = (char*)"StringTopBackRight";								break;
		case 18:											channelNameString = (char*)"StringTopFrontLeftofCenter";						break;
		case 19:											channelNameString = (char*)"StringTopFrontRightofCenter";						break;
		case 20:											channelNameString = (char*)"StringLeftLowFrequencyEffects";						break;
		case 21:											channelNameString = (char*)"StringRightLowFrequencyEffects";					break;
		case 22:											channelNameString = (char*)"StringTopSideLeft";									break;
		case 23:											channelNameString = (char*)"StringTopSideRight";								break;
		case 24:											channelNameString = (char*)"StringBottomCenter";								break;
		case 25:											channelNameString = (char*)"StringBackLeftofCenter";							break;
		case 26:											channelNameString = (char*)"StringBackRightofCenter";							break;
		case 27:
		case 28:
		case 29:
		case 30:											channelNameString = (char*)"StringReserved";										break;
		case 31:											channelNameString = (char*)"StringRawData";										break;
		default:											channelNameString = (char*)"StringUnknown";										break;
	#else
		case 0:												channelNameString = (char*)"Front Left";										break;
		case 1:												channelNameString = (char*)"Front Right";										break;
		case 2:												channelNameString = (char*)"Front Center";										break;
		case 3:												channelNameString = (char*)"Low Frequency Effects";								break;
		case 4:												channelNameString = (char*)"Back Left";											break;
		case 5:												channelNameString = (char*)"Back Right";										break;
		case 6:												channelNameString = (char*)"Front Left of Center";								break;
		case 7:												channelNameString = (char*)"Front Right of Center";								break;
		case 8:												channelNameString = (char*)"Back Center";										break;
		case 9:												channelNameString = (char*)"Side Left";											break;
		case 10:											channelNameString = (char*)"Side Right";										break;
		case 11:											channelNameString = (char*)"Top Center";										break;
		case 12:											channelNameString = (char*)"Top Front Left";									break;
		case 13:											channelNameString = (char*)"Top Front Center";									break;
		case 14:											channelNameString = (char*)"Top Front Right";									break;
		case 15:											channelNameString = (char*)"Top Back Left";										break;
		case 16:											channelNameString = (char*)"Top Back Center";									break;
		case 17:											channelNameString = (char*)"Top Back Right";									break;
		case 18:											channelNameString = (char*)"Top Front Left of Center";							break;
		case 19:											channelNameString = (char*)"Top Front Right of Center";							break;
		case 20:											channelNameString = (char*)"Left Low Frequency Effects";						break;
		case 21:											channelNameString = (char*)"Right Low Frequency Effects";						break;
		case 22:											channelNameString = (char*)"Top Side Left";										break;
		case 23:											channelNameString = (char*)"Top Side Right";									break;
		case 24:											channelNameString = (char*)"Bottom Center";										break;
		case 25:											channelNameString = (char*)"Back Left of Center";								break;
		case 26:											channelNameString = (char*)"Back Right of Center";								break;
		case 27:
		case 28:
		case 29:
		case 30:											channelNameString = (char*)"Reserved";											break;
		case 31:											channelNameString = (char*)"Raw Data";											break;
		default:											channelNameString = (char*)"Unknown";											break;
	#endif
	}

	return channelNameString;
}

void AppleUSBAudioEngine::registerPlugin (AppleUSBAudioPlugin * thePlugin) {
	mPlugin = thePlugin;
	mPluginInitThread = thread_call_allocate ((thread_call_func_t)pluginLoaded, (thread_call_param_t)this);

	if (NULL != mPluginInitThread) 
	{
		thread_call_enter (mPluginInitThread);
	}
}

void AppleUSBAudioEngine::pluginLoaded (AppleUSBAudioEngine * usbAudioEngineObject) {
	IOReturn							result;

	// It can only be output or input for plugin at the engine level. For engine with multiple streams,
	// plugin should be instantiated at stream level. If a plugin appears at the engine level, treat it as
	// an output plugin.
	FailIf ( (NULL == usbAudioEngineObject->mMainOutputStream) && (NULL == usbAudioEngineObject->mMainInputStream), Exit );
	
	if (usbAudioEngineObject->mPlugin && (usbAudioEngineObject->mMainOutputStream->mStreamInterface || usbAudioEngineObject->mMainInputStream->mStreamInterface)) 
	{
		usbAudioEngineObject->mPlugin->open (usbAudioEngineObject);

		result = usbAudioEngineObject->mPlugin->pluginInit (usbAudioEngineObject, usbAudioEngineObject->mUSBAudioDevice->getVendorID (), usbAudioEngineObject->mUSBAudioDevice->getProductID ());
		if (result == kIOReturnSuccess) 
		{
			debugIOLog ("success initing the plugin");
			if (usbAudioEngineObject->mMainOutputStream)
			{
				usbAudioEngineObject->mPlugin->pluginSetDirection ((IOAudioStreamDirection) kIOAudioStreamDirectionOutput); 
				usbAudioEngineObject->mPlugin->pluginSetFormat (usbAudioEngineObject->mMainOutputStream->getFormat (), &usbAudioEngineObject->sampleRate);
			}
			else if (usbAudioEngineObject->mMainInputStream)
			{
				usbAudioEngineObject->mPlugin->pluginSetDirection ((IOAudioStreamDirection) kIOAudioStreamDirectionInput); 
				usbAudioEngineObject->mPlugin->pluginSetFormat (usbAudioEngineObject->mMainInputStream->getFormat (), &usbAudioEngineObject->sampleRate);
			}
		} 
		else 
		{
			debugIOLog ("Error initing the plugin");
			usbAudioEngineObject->mPlugin->close (usbAudioEngineObject);
			usbAudioEngineObject->mPlugin = NULL;
		}

		if (NULL != usbAudioEngineObject->mPluginNotification) 
		{
			usbAudioEngineObject->mPluginNotification->remove ();
			usbAudioEngineObject->mPluginNotification = NULL;
		}
	}
	
Exit:
	return;
}

IOReturn AppleUSBAudioEngine::pluginDeviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion) {
	IOReturn						result;

	result = kIOReturnBadArgument;
	if (request) 
	{
		result = mUSBAudioDevice->deviceRequest (request, mUSBAudioDevice, completion);
	}

	return result;
}

void AppleUSBAudioEngine::pluginSetConfigurationApp (const char * bundleID) {
	if (bundleID) 
	{
		mUSBAudioDevice->setConfigurationApp (bundleID);
	}
}

IOReturn AppleUSBAudioEngine::performAudioEngineStart () {
    IOReturn				resultCode;
	UInt32					usbFramesToDelay = 0;
	UInt32					lockDelayFrames = 0;
	UInt64					currentUSBFrame = 0;
	UInt32					streamIndex;
	AppleUSBAudioStream *	audioStream;
	
    debugIOLog ("+ AppleUSBAudioEngine[%p]::performAudioEngineStart ()", this);

    resultCode = kIOReturnError;
	FailIf (NULL == mUSBAudioDevice, Exit);
	FailIf (NULL == mIOAudioStreamArray, Exit);
	
	if (mUSBAudioDevice->mAnchorTime.n == 0)
	{
		// We have to have an anchor frame and time before we can take a time stamp. Generate one now.
		debugIOLog ("! AppleUSBAudioEngine[%p]::performAudioEngineStart () - Getting an anchor for the first timestamp.", this);
		mUSBAudioDevice->updateUSBCycleTime ();
		FailIf (mUSBAudioDevice->mAnchorTime.n == 0, Exit);
	}
	
	mUSBAudioDevice->calculateOffset ();		// <rdar://problem/7666699>
	
	resultCode = kIOReturnSuccess;
	

	if (mPlugin) 
	{
		mPlugin->pluginStart ();
	}
	for ( streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
	{
		audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
		if (audioStream && audioStream->mPlugin)
		{
			audioStream->mPlugin->pluginStart ();
		}
	}

	if (!mUSBStreamRunning) 
	{
		#if DEBUGLATENCY
			mHaveClipped = false;
		#endif
		for ( streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
		{
			resultCode = kIOReturnError;
			
			audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
			FailIf ( NULL == audioStream, Exit );
			
			resultCode = audioStream->prepareUSBStream ();
			FailIf ( kIOReturnSuccess != resultCode, Exit );
			
			lockDelayFrames = audioStream->getLockDelayFrames ();
			if ( usbFramesToDelay < lockDelayFrames )
			{
				usbFramesToDelay = lockDelayFrames;
			}
		}
		
		currentUSBFrame = mUSBAudioDevice->getUSBFrameNumber ();
		// Added an offset to compensate for the time needed for some of the operation in AppleUSBAudioStream::startUSBStream();
		currentUSBFrame += kStartDelayOffset; 
		
		for ( streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
		{
			AppleUSBAudioStream * audioStream;
			
			resultCode = kIOReturnError;
			
			audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
			FailIf ( NULL == audioStream, Exit );
			
			resultCode = audioStream->startUSBStream (currentUSBFrame, usbFramesToDelay);
			FailIf ( kIOReturnSuccess != resultCode, Exit );
		}

		if ( kIOReturnSuccess == resultCode )
		{
			if ( 0 != usbFramesToDelay )
			{
				// [rdar://5083342] Sleep for the amount of frames delayed.
				IOSleep ( usbFramesToDelay );
			}
			
			mUSBStreamRunning = TRUE;
		}
    }
	
Exit:
	if (resultCode != kIOReturnSuccess) 
	{
		debugIOLog ("! AppleUSBAudioEngine[%p]::performAudioEngineStart () - NOT started, error = 0x%x", this, resultCode);
		
		// It is safe to call performAudioEngineStop from here because we are failing the performAudioEngineStart method.
		performAudioEngineStop ();
		
		if	(		(		(kIOReturnNotResponding == resultCode)
						||	(kIOReturnExclusiveAccess == resultCode))
				&&	( ! mUSBAudioDevice->recoveryRequested ()))
		{
			// The device is in an odd state. We should ask for a device recovery attempt.
			if (mUSBAudioDevice)
			{
				debugIOLog ("! AppleUSBAudioEngine[%p]::performAudioEngineStart () - Device not responding! Requesting a recovery attempt.");
				mUSBAudioDevice->requestDeviceRecovery ();
			}
		} 
	} 
	else 
	{
		debugIOLog ("\n");
		debugIOLog ("      sampleRate->whole = %lu", getSampleRate()->whole);
		debugIOLog ("\n");
	}

    debugIOLog ("- AppleUSBAudioEngine[%p]::performAudioEngineStart ()", this);
    return resultCode;
}

IOReturn AppleUSBAudioEngine::performAudioEngineStop() {
	UInt32					streamIndex;
	AppleUSBAudioStream *	audioStream;

    debugIOLog("+ AppleUSBAudioEngine[%p]::performAudioEngineStop ()", this);
	
	for ( streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
	{
		audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
		if (audioStream && audioStream->mPlugin)
		{
			audioStream->mPlugin->pluginStop ();
		}
	}
	if (mPlugin) 
	{
		mPlugin->pluginStop ();
	}

    if (mUSBStreamRunning) 
	{
 		for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
		{
			AppleUSBAudioStream * audioStream;
						
			audioStream = OSDynamicCast (AppleUSBAudioStream, mIOAudioStreamArray->getObject (streamIndex) );
			if ( NULL != audioStream )
			{
				audioStream->stopUSBStream ();
			}
		}
   }

 	mUSBStreamRunning = FALSE;
	
	if ( NULL != mUSBAudioDevice )								// <rdar://problem/7779397>
	{
		mUSBAudioDevice->mAnchorTime.deviceStart = FALSE;		// <rdar://problem/7666699>
	}
	
    debugIOLog("? AppleUSBAudioEngine[%p]::performAudioEngineStop() - stopped", this);

    debugIOLog("- AppleUSBAudioEngine[%p]::performAudioEngineStop()", this);
    return kIOReturnSuccess;
}

// This gets called when the HAL wants to select one of the different formats that we made available via mMainStream->addAvailableFormat
IOReturn AppleUSBAudioEngine::performFormatChange (IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate) {
    IOReturn							result;
	UInt32								controllerResult;
	bool								streamIsRunning = false;

	debugIOLog ("+ AppleUSBAudioEngine[%p]::performFormatChange (%p, %p, %p)", this, audioStream, newFormat, newSampleRate);
	result = kIOReturnError;
	FailIf (NULL == mUSBAudioDevice, Exit);
	streamIsRunning = mUSBStreamRunning;
	if (streamIsRunning)
	{
		pauseAudioEngine ();
	}
	controllerResult = mUSBAudioDevice->formatChangeController (this, audioStream, newFormat, newSampleRate);
	
	switch (controllerResult)
	{
		case kAUAFormatChangeNormal:
			result = kIOReturnSuccess;
			break;
		case kAUAFormatChangeForced:
			debugIOLog ("? AppleUSBAudioEngine[%p]::performFormatChange () - This request was forced.");
			result = kIOReturnSuccess;
			break;
		case kAUAFormatChangeForceFailure:
			debugIOLog ("! AppleUSBAudioEngine[%p]::performFormatChange () - Force of this request was attempted but failed.");
			result = kIOReturnSuccess;
			break;
		case kAUAFormatChangeError:
		default:
			debugIOLog ("! AppleUSBAudioEngine[%p]::performFormatChange () - Error encountered.");
			result = kIOReturnError;
	}

Exit:
	if (streamIsRunning)
	{
		resumeAudioEngine ();
	}
	debugIOLog ("- AppleUSBAudioEngine[%p]::performFormatChange () = 0x%x", this, result);
    return result;
}

void AppleUSBAudioEngine::resetClipPosition (IOAudioStream *audioStream, UInt32 clipSampleFrame) {
	AppleUSBAudioStream *		appleUSBAudioStream;			

	appleUSBAudioStream = OSDynamicCast ( AppleUSBAudioStream, audioStream );
	FailIf ( NULL == appleUSBAudioStream, Exit );

	if (appleUSBAudioStream->mPlugin)
	{
		appleUSBAudioStream->mPlugin->pluginReset ();
	}
	if (mPlugin) 
	{
		mPlugin->pluginReset ();
	}
	
Exit:
	return;
}

void AppleUSBAudioEngine::takeTimeStamp (bool incrementLoopCount, AbsoluteTime *timestamp)
{
	#if LOGTIMESTAMPS
	UInt64	time_nanos;
	
	absolutetime_to_nanoseconds (*timestamp, &time_nanos);
	// if (getDirection () == kIOAudioStreamDirectionInput)
	// if (getDirection () == kIOAudioStreamDirectionOutput)
	if (true)
	{
		debugIOLog ("? AppleUSBAudioEngine[%p]::takeTimeStamp (%d, %p) = %llu ns", this, timestamp, time_nanos);
	}
	#endif
	super::takeTimeStamp (incrementLoopCount, timestamp);
}

bool AppleUSBAudioEngine::willTerminate (IOService * provider, IOOptionBits options) {
	debugIOLog ("+ AppleUSBAudioEngine[%p]::willTerminate (%p)", this, provider);	

	if (mUSBAudioDevice == provider)
	{
		mTerminatingDriver = TRUE;
	}

	debugIOLog ("- AppleUSBAudioEngine[%p]::willTerminate ()", this);

	return super::willTerminate (provider, options);
}

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
OSSet * AppleUSBAudioEngine::copyDefaultAudioControls ( void )
{
	// <rdar://7695055>	If there are controls present, copy it.
	if ( defaultAudioControls && defaultAudioControls->getCount () )
	{
		return OSSet::withSet ( defaultAudioControls, defaultAudioControls->getCount () );
	}
	else 
	{
		return NULL;
	}

}

#pragma mark -Clock Source Selector-

//	<rdar://5811247>
IOReturn AppleUSBAudioEngine::doClockSelectorSetup (UInt8 interfaceNum, UInt8 altSettingNum, UInt32 sampleRate) {
	OSArray *							clockPathGroup = NULL;
	OSNumber *							clockIDNumber;
	UInt8								clockSelectorID = 0;
	UInt8								clockID;
	OSArray	*							clockPath;
	UInt8								clockPathGroupIndex = 0;
	UInt8								clockPathIndex = 0;
	UInt32								clockSelection = 0;
	UInt8								controlInterfaceNum;
	AUAConfigurationDictionary *		configDictionary = NULL;
	bool								hasNonProgrammableClockSource = false;
	IOReturn							result;

	debugIOLog ("+ AppleUSBAudioEngine[%p]::doClockSelectorSetup( 0x%x, 0x%x, %d )", this, interfaceNum, altSettingNum, sampleRate);

	result = kIOReturnError;
	FailIf ( NULL == mUSBAudioDevice, Exit );
	FailIf ( NULL == mUSBAudioDevice->mControlInterface, Exit );
	FailIf ( NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary () ), Exit );

	controlInterfaceNum = mUSBAudioDevice->mControlInterface->GetInterfaceNumber ();
	
	if ( NULL == mClockSelectorControl )
	{
		clockPath = mUSBAudioDevice->getOptimalClockPath ( this, interfaceNum, altSettingNum, sampleRate, NULL, &clockPathGroupIndex );
		FailIf ( NULL == clockPath, Exit );
		FailIf ( NULL == ( clockIDNumber = OSDynamicCast ( OSNumber, clockPath->getLastObject () ) ), Exit );
		clockID = clockIDNumber->unsigned8BitValue ();		
		result = mUSBAudioDevice->getClockSelectorIDAndPathIndex ( &clockSelectorID, &clockPathIndex, clockPath );
		FailIf ( kIOReturnSuccess != result, Exit );
		result = mUSBAudioDevice->getClockPathCurSampleRate ( NULL, &mClockSourceValidity, NULL, clockPath );		//	<rdar://6945472>
		FailIf ( kIOReturnSuccess != result, Exit );
		if ( !mClockSourceValidityInitialized )
		{
			mClockSourceValidityInitialized = true;
			mShouldRepublishFormat = true;
		}
	}
	else
	{
		clockID = (mClockSelectorControl->getIntValue () >> 24) & 0xFF;
		clockSelectorID = (mClockSelectorControl->getIntValue () >> 16) & 0xFF;
		clockPathGroupIndex = (mClockSelectorControl->getIntValue () >> 8) & 0xFF;
		clockPathIndex = mClockSelectorControl->getIntValue () & 0xFF;
		result = kIOReturnSuccess;

		mClockSelectorControl->release ();
		mClockSelectorControl = NULL;
	}
	mCurrentClockSourceID = clockID;
	mCurrentClockPathGroupIndex = clockPathGroupIndex;
	mCurrentClockPathIndex = clockPathIndex;
	
	// If there is a clock selector in the path, then create a clock selector control.
	if ( ( kIOReturnSuccess == result ) &&
		 ( 0 != clockSelectorID ) && ( 0 != clockPathIndex ) )
	{
		debugIOLog ("? AppleUSBAudioEngine[%p]::doClockSelectorSetup( 0x%x, 0x%x, %d ) - clockSelectorID = %d, clockPathIndex = %d", this, interfaceNum, altSettingNum, sampleRate, clockSelectorID, clockPathIndex);
		
		FailIf ( kIOReturnSuccess != mUSBAudioDevice->setCurClockSelector ( clockSelectorID, clockPathIndex ), Exit );

		clockSelection = ( ( UInt32 )( clockID ) << 24 ) | ( ( UInt32 )( clockSelectorID ) << 16 ) | ( clockPathGroupIndex << 8 ) | ( clockPathIndex );
						 
		mClockSelectorControl = IOAudioSelectorControl::create ( clockSelection, 
																kIOAudioControlChannelIDAll, 
																kIOAudioControlChannelNameAll, 
																0, 
																kIOAudioSelectorControlSubTypeClockSource,
																kIOAudioControlUsageInput);	
		FailIf ( NULL == mClockSelectorControl, Exit );
		
		mClockSelectorControl->setValueChangeHandler ( controlChangedHandler, this );

		clockPathGroup = mUSBAudioDevice->getClockPathGroup ( interfaceNum, altSettingNum );
		FailIf ( NULL == clockPathGroup, Exit );
		
		for ( UInt32 pathIndex = 0; pathIndex < clockPathGroup->getCount (); pathIndex++ )
		{
			UInt8				subType;

			FailIf ( NULL == ( clockPath = OSDynamicCast ( OSArray, clockPathGroup->getObject ( pathIndex ) ) ), Exit );
			FailIf ( NULL == ( clockIDNumber = OSDynamicCast ( OSNumber, clockPath->getLastObject () ) ), Exit );
			clockID = clockIDNumber->unsigned8BitValue ();
			
			FailIf  (kIOReturnSuccess != ( result = configDictionary->getSubType ( &subType, controlInterfaceNum, 0, clockID ) ), Exit );
			
			debugIOLog ("? AppleUSBAudioEngine[%p]::doClockSelectorSetup( 0x%x, 0x%x, %d ) - %d: clockID = %d, subType = %d", this, interfaceNum, altSettingNum, sampleRate, pathIndex, clockID, subType);
			
			if ( USBAUDIO_0200::CLOCK_SOURCE == subType )
			{
				UInt8			clockType;
				UInt8			assocTerminal = 0;
				UInt16			terminalType = 0;
				UInt8			stringIndex;
				char *			clockSelectionString = NULL;
				
				clockSelection = ( ( UInt32 )( clockID ) << 24 ) | ( ( UInt32 )( clockSelectorID ) << 16 ) | ( clockPathGroupIndex << 8 ) | ( pathIndex + 1 );
				
				if ( !mClockSelectorControl->valueExists ( clockSelection ) )
				{
					char	tempString[kStringBufferSize] = { 0 };
					
					if  ( ( kIOReturnSuccess == configDictionary->getStringIndex ( &stringIndex, controlInterfaceNum, 0, clockID ) ) &&
						  ( 0 != stringIndex ) &&
						  ( kIOReturnSuccess == mUSBAudioDevice->getStringDescriptor(stringIndex, tempString, kStringBufferSize) ) )
					{
						// There is a string specified for this clock source. Use this for the name.
						clockSelectionString = tempString;
					}
					else
					{
						// No string specified. Generate one based on the following:
						// 1. If it is internal clock type, it should be "Device"
						// 2. If it is external clock type, check the associated terminal type. If it is present, then check the
						// following:
						// (a) If it is USB streaming, then it should be "Mac Sync". 
						// (b) Otherwise, use the name of the terminal, if available. 
						// (c) If no terminal name is available, then fallback to the terminal type.
						// 3. If there is no assoc terminal specified, then use the clock type to generate the string.
						FailIf ( kIOReturnSuccess != ( result = configDictionary->getClockSourceClockType ( &clockType, controlInterfaceNum, 0, clockID ) ), Exit );
						
						switch ( clockType )
						{
							case USBAUDIO_0200::CLOCK_TYPE_EXTERNAL:
								FailIf ( kIOReturnSuccess != ( result = configDictionary->getClockSourceAssocTerminal ( &assocTerminal, controlInterfaceNum, 0, clockID ) ), Exit );
								if ( 0 != assocTerminal )
								{
									FailIf ( kIOReturnSuccess != ( result = configDictionary->getInputTerminalType (&terminalType, controlInterfaceNum, 0, assocTerminal) ), Exit);
									
									if ( USB_STREAMING == terminalType )
									{
										#if LOCALIZABLE
										clockSelectionString = (char *)"StringMacSync";
										#else
										clockSelectionString = (char *)"Mac Sync";
										#endif
									}
									else
									{
										if  ( ( kIOReturnSuccess == configDictionary->getStringIndex ( &stringIndex, controlInterfaceNum, 0, assocTerminal ) ) &&
											  ( 0 != stringIndex ) &&
											  ( kIOReturnSuccess == mUSBAudioDevice->getStringDescriptor(stringIndex, tempString, kStringBufferSize) ) )
										{
											// There is a string specified for the assoc terminal. Use this for the name.
											clockSelectionString = tempString;
										}
										else
										{
											clockSelectionString = mUSBAudioDevice->TerminalTypeString ( terminalType );
										}
									}
								}
								else
								{
									clockSelectionString = mUSBAudioDevice->ClockTypeString ( clockType );
								}
								break;
							case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_FIXED:
							case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_VARIABLE:
							case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_PROGRAMMABLE:
								#if LOCALIZABLE
								clockSelectionString = (char *)"StringDevice";
								#else
								clockSelectionString = (char *)"Device";
								#endif
								break;								
						} 
					}

					if ( NULL != clockSelectionString )
					{
						mClockSelectorControl->addAvailableSelection ( clockSelection, clockSelectionString );
					}
				}
				
				if ( !configDictionary->clockSourceHasFrequencyControl ( controlInterfaceNum, 0, clockID, true ) )
				{
					hasNonProgrammableClockSource = true;
				}
			}
		}

		addDefaultAudioControl ( mClockSelectorControl );
	}
	else
	{
		hasNonProgrammableClockSource = !configDictionary->clockSourceHasFrequencyControl ( controlInterfaceNum, 0, mCurrentClockSourceID, true );
	}
	
	determineMacSyncMode ( mCurrentClockSourceID );
	
	#if POLLCLOCKSTATUS
	// Only poll if there isn't an interrupt endpoint, and at least one of the clock source is non-programmable.
	mShouldPollClockStatus = hasNonProgrammableClockSource && ( !configDictionary->hasInterruptEndpoint ( controlInterfaceNum, 0 ) );
	mPollClockStatusCounter = 0;
	debugIOLog ("? AppleUSBAudioEngine[%p]::doClockSelectorSetup( 0x%x, 0x%x, %d ) - Should poll = %d", this, interfaceNum, altSettingNum, sampleRate, mShouldPollClockStatus);
	#endif // POLLCLOCKSTATUS
	
	result = kIOReturnSuccess;
Exit:

	debugIOLog ("- AppleUSBAudioEngine[%p]::doClockSelectorSetup( 0x%x, 0x%x, %d ) = 0x%x", this, interfaceNum, altSettingNum, sampleRate, result);

	return result;
}

//	<rdar://5811247>
IOReturn AppleUSBAudioEngine::controlChangedHandler (OSObject * target, IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn							result;
	AppleUSBAudioEngine *				self;

	result = kIOReturnError;

	self = OSDynamicCast (AppleUSBAudioEngine, target);
	FailIf (NULL == self, Exit);
	result = self->protectedControlChangedHandler (audioControl, oldValue, newValue);

Exit:
	return result;
}

//	<rdar://5811247>
IOReturn AppleUSBAudioEngine::protectedControlChangedHandler (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn							result;

	result = kIOReturnError;
    switch (audioControl->getType ()) 
	{
		case kIOAudioControlTypeSelector:
			if ( kIOAudioSelectorControlSubTypeClockSource == audioControl->getSubType () )
			{
				result = doClockSelectorChange (audioControl, oldValue, newValue);
			}
			break;
	}

	return result;
}

//	<rdar://5811247>
IOReturn AppleUSBAudioEngine::doClockSelectorChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
	UInt8						clockSourceID;
	UInt8						clockSelectorID;
	UInt8						newClockPathGroupIndex;
	UInt8						newClockPathIndex;
	OSArray *					newClockPathGroup;
	OSArray *					newClockPath;
	UInt8						oldClockPathGroupIndex;
	UInt8						oldClockPathIndex;
	OSArray *					oldClockPathGroup;
	OSArray *					oldClockPath;
	Boolean						clockValidity = false;
	IOAudioSampleRate			newSampleRate;
	IOAudioSampleRate			oldSampleRate;
    IOReturn					result = kIOReturnError;

	debugIOLog ("+ AppleUSBAudioEngine[%p]::doClockSelectorChange( %p, 0x%x, 0x%x )", this, audioControl, oldValue, newValue);
	
	clockSourceID = ( newValue >> 24 ) & 0xFF;
	clockSelectorID = ( newValue >> 16 ) & 0xFF;
	newClockPathGroupIndex = ( newValue >> 8 ) & 0xFF;
	newClockPathIndex = newValue & 0xFF;
	oldClockPathGroupIndex = ( oldValue >> 8 ) & 0xFF;
	oldClockPathIndex = oldValue & 0xFF;

	//	When switching the clocks, determine if the device is locked to the selected clock. This is done by checking the clock validity 
	//	bit on the clock source. If it is valid, then perform the switch to the clock. Otherwise, just return success and schedule a
	//	restore of the UI so that it switches back to the old value.
	if ( oldValue != newValue )
	{
		pauseAudioEngine ();
		beginConfigurationChange ();
		
		// Determine if the selected clock source is valid.
		newClockPathGroup = mUSBAudioDevice->getClockPathGroup ( newClockPathGroupIndex );
		FailIf ( NULL == newClockPathGroup, Exit );
		newClockPath = OSDynamicCast ( OSArray, newClockPathGroup->getObject ( newClockPathIndex - 1 ) );
		FailIf ( NULL == newClockPath, Exit )

		newSampleRate.whole = 0;
		newSampleRate.fraction = 0;
		oldSampleRate.whole = 0;
		oldSampleRate.fraction = 0;
		
		result = mUSBAudioDevice->getClockPathCurSampleRate ( &newSampleRate.whole, &clockValidity, NULL, newClockPath );		//	<rdar://6945472>
		debugIOLog ("? AppleUSBAudioEngine[%p]::doClockSelectorChange( %p, 0x%x, 0x%x ) - result = 0x%x, sample rate = %d, clockValidity = %d", this, audioControl, oldValue, newValue, result, newSampleRate.whole, clockValidity);
		
		if ( kIOReturnSuccess == result )
		{
			if ( clockValidity )
			{
				// Set the old clock path to the same sample rate as the new one. Some devices lock their external clock using
				// the internal one as the reference, so it doesn't lock if the frequency is not the same (or close to it).
				oldClockPathGroup = mUSBAudioDevice->getClockPathGroup ( oldClockPathGroupIndex );
				FailIf ( NULL == oldClockPathGroup, Exit );
				oldClockPath = OSDynamicCast ( OSArray, oldClockPathGroup->getObject ( oldClockPathIndex - 1 ) );
				FailIf ( NULL == oldClockPath, Exit )
				result = mUSBAudioDevice->getClockPathCurSampleRate ( &oldSampleRate.whole, NULL, NULL, oldClockPath );		//	<rdar://6945472>
				FailIf ( kIOReturnSuccess != result, Exit );
				result = mUSBAudioDevice->setClockPathCurSampleRate ( newSampleRate.whole, oldClockPath );
				FailIf ( kIOReturnSuccess != result, Exit );
				
				mCurrentClockSourceID = clockSourceID;
				mCurrentClockPathIndex = newClockPathIndex;
		
				// Re-publish all the streams supported sample rates & formats.
				republishAvailableFormats ();
				
				// Do the sample rate change.
				result = performFormatChange ( NULL, NULL, &newSampleRate );
				FailMessage ( kIOReturnSuccess != result );
				
				if ( kIOReturnSuccess == result )
				{
					determineMacSyncMode ( mCurrentClockSourceID );

					// Inform CoreAudio that the sample rate has changed.
					hardwareSampleRateChanged ( &newSampleRate );
				}
				else
				{
					// Failed to switch over to the new sample rate. Try to recover.
					mCurrentClockSourceID = ( oldValue >> 24 ) & 0xFF;
					mCurrentClockPathIndex = oldClockPathIndex;
			
					// Re-publish all the streams supported sample rates & formats.
					republishAvailableFormats ();
					
					// Do the sample rate change.
					FailMessage ( kIOReturnSuccess != performFormatChange ( NULL, NULL, &oldSampleRate ) );
					
					// Indicate that we need to restore the clock selection in the polled task.
					mRestoreClockSelection = true;
					mRestoreClockSelectionValue = oldValue;

					result = kIOReturnSuccess;
				} 
			}
			else
			{
				// No valid clock on the selected clock source. Schedule a restore of the old selector value.
				mRestoreClockSelection = true;
				mRestoreClockSelectionValue = oldValue;
				result = kIOReturnSuccess;
			}
		}

		completeConfigurationChange ();
		resumeAudioEngine ();
	}
	else
	{
		clockSelectorID = ( newValue >> 16 ) & 0xFF;
		newClockPathIndex = newValue & 0xFF;

		result = mUSBAudioDevice->setCurClockSelector ( clockSelectorID, newClockPathIndex );
	}

	debugIOLog ("- AppleUSBAudioEngine[%p]::doClockSelectorChange( %p, 0x%x, 0x%x ) = 0x%x", this, audioControl, oldValue, newValue, result);

Exit:
	return result;
}

//	<rdar://5811247>
IOReturn AppleUSBAudioEngine::republishAvailableFormats () {
	AUAConfigurationDictionary *		configDictionary = NULL;

	FailIf ( NULL == ( configDictionary = mUSBAudioDevice->getConfigDictionary () ), Exit );

	for ( UInt32 streamIndex = 0; streamIndex < mIOAudioStreamArray->getCount (); streamIndex++ )
	{
		AppleUSBAudioStream * audioStream = OSDynamicCast ( AppleUSBAudioStream, mIOAudioStreamArray->getObject ( streamIndex ) );
		
		if ( NULL != audioStream )
		{
			audioStream->clearAvailableFormats ();
			audioStream->addAvailableFormats ( configDictionary );
		}
	}
	
Exit:
	return kIOReturnSuccess;
}

//	<rdar://5811247> Determine if the engine is running in Mac sync mode, and setup the appropriate stream to be the master stream.
bool AppleUSBAudioEngine::determineMacSyncMode ( UInt8 clockID )
{
	UInt8								clockType = 0xFF;
	UInt8								assocTerminal = 0;
	UInt16								terminalType = 0;
	AUAConfigurationDictionary *		configDictionary = NULL;
	UInt8								controlInterfaceNum;
	AppleUSBAudioStream *				masterStream = NULL;	// <rdar://7558825>
	bool								macSyncMode = false;

	debugIOLog ( "+ AppleUSBAudioEngine[%p]::determineMacSyncMode( %d )", this, clockID );

	FailIf ( NULL == mUSBAudioDevice, Exit );
	FailIf ( NULL == mUSBAudioDevice->mControlInterface, Exit );
	FailIf ( NULL == (configDictionary = mUSBAudioDevice->getConfigDictionary () ), Exit );

	controlInterfaceNum = mUSBAudioDevice->mControlInterface->GetInterfaceNumber ();
	
	FailIf ( kIOReturnSuccess != configDictionary->getClockSourceClockType ( &clockType, controlInterfaceNum, 0, clockID ), Exit );
	FailIf ( kIOReturnSuccess != configDictionary->getClockSourceAssocTerminal ( &assocTerminal, controlInterfaceNum, 0, clockID ), Exit );
	if ( ( USBAUDIO_0200::CLOCK_TYPE_EXTERNAL == clockType ) && ( 0 != assocTerminal ) )
	{
		FailIf ( kIOReturnSuccess != configDictionary->getInputTerminalType (&terminalType, controlInterfaceNum, 0, assocTerminal), Exit);
		macSyncMode = ( USB_STREAMING == terminalType );
	}
	
	if ( ( NULL != mMainOutputStream ) && ( NULL != mMainInputStream ) )
	{
		if ( ( macSyncMode ) || ( kAdaptiveSyncType == mMainOutputStream->getSyncType () ) )
		{
			mMainInputStream->setMasterStreamMode ( false );
			mMainOutputStream->setMasterStreamMode ( true );
			
			masterStream = mMainOutputStream;	// <rdar://7558825>
		}
		else
		{
			mMainInputStream->setMasterStreamMode ( true );
			mMainOutputStream->setMasterStreamMode ( false );
			
			masterStream = mMainInputStream;	// <rdar://7558825>
		}
	}
	else if ( NULL != mMainOutputStream )		// <rdar://7558825>
	{
		masterStream = mMainOutputStream;	
	}
	else if ( NULL != mMainInputStream )		// <rdar://7558825>
	{
		masterStream = mMainInputStream;	
	}

	// <rdar://7558825> Clock domain for different synchronization type.
	if ( masterStream )
	{
		if ( ( macSyncMode ) || ( kSynchronousSyncType == masterStream->getSyncType () ) ||
			( kAdaptiveSyncType == masterStream->getSyncType () ) )
		{
			setClockDomain ( getSystemClockDomain () );
		}
		else 
		{
			setClockDomain ();
		}
	}
	
Exit:
	debugIOLog ( "- AppleUSBAudioEngine[%p]::determineMacSyncMode( %d ) = %d", this, clockID, macSyncMode );
	
	return macSyncMode;
}

//	<rdar://6945472>
IOAudioSampleRate AppleUSBAudioEngine::getCurrentClockPathSampleRate ( void )
{
	OSArray *				clockPathGroup;
	OSArray *				clockPath;
	IOAudioSampleRate		sampleRate;
	
	sampleRate.whole = 0;
	sampleRate.fraction = 0;
	
	// <rdar://6945472> Get the current clock path sample rate.
	clockPathGroup = mUSBAudioDevice->getClockPathGroup ( mCurrentClockPathGroupIndex );
	FailIf ( NULL == clockPathGroup, Exit );
	clockPath = OSDynamicCast ( OSArray, clockPathGroup->getObject ( mCurrentClockPathIndex - 1 ) );
	FailIf ( NULL == clockPath, Exit )
	FailIf ( kIOReturnSuccess != mUSBAudioDevice->getClockPathCurSampleRate ( &sampleRate.whole, NULL, NULL, clockPath ), Exit );
	
Exit:
	return sampleRate;
}

//	<rdar://5811247>
void AppleUSBAudioEngine::updateClockStatus ( UInt8 clockID )
{
	UInt32				clockRate;
	bool				clockValidity = false;
	
	if ( ( 0 != clockID ) && ( clockID == mCurrentClockSourceID ) )
	{
		if ( kIOReturnSuccess == mUSBAudioDevice->getCurClockSourceSamplingFrequency ( clockID, &clockRate, &clockValidity ) )
		{
			if ( NULL != mClockSelectorControl )
			{
				if ( !clockValidity )
				{
					SInt32	currentValue = mClockSelectorControl->getIntValue ();
					
					// The current selected clock is no longer valid. Switch over to a valid clock source.
					OSArray * availableSelections = OSDynamicCast ( OSArray, mClockSelectorControl->getProperty ( kIOAudioSelectorControlAvailableSelectionsKey ) );
					if ( NULL != availableSelections )
					{
						for ( UInt32 index = 0; index < availableSelections->getCount (); index++ )
						{
							OSDictionary * selectionDictionary = OSDynamicCast ( OSDictionary, availableSelections->getObject ( index ) );
							if ( NULL != selectionDictionary )
							{
								OSNumber * selectionNumber = OSDynamicCast ( OSNumber, selectionDictionary->getObject ( kIOAudioSelectorControlSelectionValueKey ) );
								if ( NULL != selectionNumber )
								{
									SInt32 selection = (SInt32)selectionNumber->unsigned32BitValue ();
									if ( selection != currentValue )
									{
										clockID = ( selection >> 24 ) & 0xFF;
										if ( kIOReturnSuccess == mUSBAudioDevice->getCurClockSourceSamplingFrequency ( clockID, &clockRate, &clockValidity ) && ( clockValidity ) )
										{
											// Found a clock source which is valid. Switch over to it.
											debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Switch over to selection = 0x%x", this, selection );
											mClockSelectorControl->setValue ( selection );
											break;
										}
									}
								}
							}
						}
					}
				}
				else 
				{
					// // <rdar://6945472> If the sample rate has changed, then it needs to be republished and CoreAudio be informed of the change.
					IOAudioSampleRate sampleRate = getCurrentClockPathSampleRate ();
					
					debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Current sample rate = %u, new sample rate = %u", this, mCurSampleRate.whole, sampleRate.whole );

					if ( mCurSampleRate.whole != sampleRate.whole )
					{
						pauseAudioEngine ();
						beginConfigurationChange ();

						debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Re-publishing audio formats", this );

						// Re-publish all the streams supported sample rates & formats.
						republishAvailableFormats ();
						
						debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Performing format - sample rate changes", this );

						// Do the sample rate change.
						FailMessage ( kIOReturnSuccess != performFormatChange ( NULL, NULL, &sampleRate ) );
						
						debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Informing CoreAudio of the sample rate change", this );

						// Inform CoreAudio that the sample rate has changed.
						hardwareSampleRateChanged ( &sampleRate );
						
						completeConfigurationChange();
						resumeAudioEngine ();
					}
				}
			}
			else
			{
				debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - No clock selector present - update audio formats if necessary", this );

				// Don't have a selector so we need to republish the available format if the current clock validity state or sample rate is not 
				// the same as what we thought was.
				//	<rdar://6945472>
				if ( mShouldRepublishFormat || ( clockValidity != mClockSourceValidity ) || ( mCurSampleRate.whole != sampleRate.whole ) ) // <rdar://6945472>
				{
					pauseAudioEngine ();
					beginConfigurationChange ();

					mClockSourceValidity = clockValidity;
					
					IOAudioSampleRate sampleRate = getCurrentClockPathSampleRate ();

					debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - current sample rate = %u, new sample rate = %u", this, mCurSampleRate.whole, sampleRate.whole );

					debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Re-publishing audio formats", this );

					// Re-publish all the streams supported sample rates & formats.
					republishAvailableFormats ();
					
					debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Performing format - sample rate changes", this );

					// Do the sample rate change.
					FailMessage ( kIOReturnSuccess != performFormatChange ( NULL, NULL, &sampleRate ) );
					
					debugIOLog ( "? AppleUSBAudioDevice[%p]::updateClockStatus () - Informing CoreAudio of the sample rate change", this );

					// <rdar://6945472> Inform CoreAudio that the sample rate has changed.
					hardwareSampleRateChanged ( &sampleRate );

					mShouldRepublishFormat = false;
					
					completeConfigurationChange();
					resumeAudioEngine ();
				}
			}
		}
	}

	return;
}

//	<rdar://5811247>
void AppleUSBAudioEngine::runPolledTask () {

	FailIf ( NULL == mUSBAudioDevice, Exit );
	
	//	<rdar://5811247> Restore the clock selector value when the change failed.
	if ( mRestoreClockSelection )
	{
		OSNumber * number = OSNumber::withNumber ( mRestoreClockSelectionValue, 32 );
		if ( NULL != number )
		{
			if ( NULL != mClockSelectorControl )
			{
				mClockSelectorControl->hardwareValueChanged ( number );
			}
			number->release ();
		}
		mRestoreClockSelection = false;
	}
	#if POLLCLOCKSTATUS
	//	<rdar://5811247>
	else if ( mShouldPollClockStatus )
	{
		if ( 0 == mPollClockStatusCounter )
		{
			updateClockStatus ( mCurrentClockSourceID );
		}
		
		mPollClockStatusCounter++;
		if ( ( 1024 / kRefreshInterval ) == mPollClockStatusCounter ) // Poll once every 1024 millisecond.
		{
			mPollClockStatusCounter = 0;
		}
	}
	#endif // POLLCLOCKSTATUS
	
Exit:
	return;
}
