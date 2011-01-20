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
//	File:		AppleUSBAudioDevice.cpp
//
//	Contains:	Support for the USB Audio Class Control Interface.
//			This includes support for exporting device controls
//			to the Audio HAL such as Volume, Bass, Treble and
//			Mute.
//
//			Future support will include parsing of the device
//			topology and exporting of all appropriate device
//			control functions through the Audio HAL.
//
//	Technology:	OS X
//
//--------------------------------------------------------------------------------

#include "AppleUSBAudioDevice.h"
#include "AppleUSBAudioDictionary.h"

#define super					IOAudioDevice
#define LOCALIZABLE				FALSE

OSDefineMetaClassAndStructors (AppleUSBAudioDevice, super)


void AppleUSBAudioDevice::free () {
    debugIOLog ("+ AppleUSBAudioDevice[%p]::free ()", this);

	if (NULL != mUpdateTimer) 
	{
		mUpdateTimer->cancelTimeout ();
		mUpdateTimer->release ();
		mUpdateTimer = NULL;
	}

	if (mStatusInterruptBuffer) 
	{
		// <rdar://7000283> Pointer was obtained from mInterruptEndpointMemoryDescriptor, so no need to free it
		// explicitly here.
		mStatusInterruptBuffer = NULL;
	}

	if (mInterruptEndpointMemoryDescriptor) 
	{
		mInterruptEndpointMemoryDescriptor->release ();
		mInterruptEndpointMemoryDescriptor = NULL;
	}

	// <rdar://problem/7378275>
    if (mTimeLock) 
	{
        IOLockFree (mTimeLock);
        mTimeLock = NULL;
    }

    if (mInterfaceLock) 
	{
        IORecursiveLockFree (mInterfaceLock);
        mInterfaceLock = NULL;
    }
	
	//  <rdar://problem/6369110>
	if (mRegisteredEnginesMutex) 
	{
        IORecursiveLockFree (mRegisteredEnginesMutex);
        mRegisteredEnginesMutex = NULL;
    }

	//  <rdar://problem/6420832>
	if (mRegisteredStreamsMutex) 
	{
        IORecursiveLockFree (mRegisteredStreamsMutex);
        mRegisteredStreamsMutex = NULL;
    }

	if (mConfigDictionary)
	{
        mConfigDictionary->release ();
        mConfigDictionary = NULL;
    }
	
	if (mRegisteredEngines) 
	{
		mRegisteredEngines->release ();
		mRegisteredEngines = NULL;
	}

	//  <rdar://problem/6420832>
	if (mRegisteredStreams) 
	{
		mRegisteredStreams->release ();
		mRegisteredStreams = NULL;
	}
	
	if ( 0 != mEngineArray )
	{
		for ( UInt32 engineIndex = 0; engineIndex < mEngineArray->getCount (); engineIndex++ )
		{
			AppleUSBAudioEngine * engine = OSDynamicCast ( AppleUSBAudioEngine, mEngineArray->getObject ( engineIndex ) );
			if ( 0 != engine )
			{
				engine->release ();
			}
		}
		mEngineArray->release ();
		mEngineArray = 0;
	}

	if (mMonoControlsArray)
	{
		mMonoControlsArray->release ();
		mMonoControlsArray = NULL;
	}

	if (mRetryEQDownloadThread)
	{
		thread_call_free (mRetryEQDownloadThread);
	}
    super::free ();
    debugIOLog ("- AppleUSBAudioDevice[%p]::free ()", this);
}

bool AppleUSBAudioDevice::ControlsStreamNumber (UInt8 streamNumber) {
	OSArray *						streamNumberArray = NULL;
	OSObject *						arrayObject = NULL;
	OSNumber *						arrayNumber = NULL;
	UInt8							numStreams;
	UInt8							streamIndex;
	bool							doesControl;

	doesControl = FALSE;

	if (mConfigDictionary)
	{
		FailIf (kIOReturnSuccess != mConfigDictionary->getControlledStreamNumbers (&streamNumberArray, &numStreams), Exit);
		for (streamIndex = 0; streamIndex < numStreams; streamIndex++) 
		{
			FailIf (NULL == (arrayObject = streamNumberArray->getObject (streamIndex)), Exit);
			FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
			
			debugIOLog ("? AppleUSBAudioDevice[%p]::ControlsStreamNumber () - Checking stream %d against controled stream %d", this, streamNumber, arrayNumber->unsigned8BitValue());
			if (streamNumber == arrayNumber->unsigned8BitValue()) 
			{
				doesControl = TRUE;
				break;				// get out of for loop
			}
		}
	}
		
Exit:	
	return doesControl;
}

bool AppleUSBAudioDevice::start (IOService * provider) {
	bool								result;

    debugIOLog ("+ AppleUSBAudioDevice[%p]::start (%p)", this, provider);
	result = FALSE;

	mControlInterface = OSDynamicCast (IOUSBInterface, provider);
	FailIf (NULL == mControlInterface, Exit);		// <rdar://7085810>
	FailIf (FALSE == mControlInterface->open (this), Exit);

	//  <rdar://problem/6686515>
	mInitHardwareThread = thread_call_allocate ((thread_call_func_t)AppleUSBAudioDevice::initHardwareThread, (thread_call_param_t)this);
	FailIf (NULL == mInitHardwareThread, Exit);

	result = super::start (provider);				// Causes our initHardware routine to be called.

Exit:
	//  <rdar://problems/6576824&6674310> AppleUSBAudio fails to unload properly
	if ( !result )
	{
		stop ( provider );		
	}
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::start (%p) = result = %d", this, provider, result);

	return result;
}

//  <rdar://problem/6686515>
bool AppleUSBAudioDevice::initHardware (IOService * provider) {
	bool								result;

	result = FALSE;

	FailIf (NULL == mInitHardwareThread, Exit);
	
	//	<rdar://6223517>	Retain a reference in case the AppleUSBAudioDevice object is release while it is trying to execute the thread.
	retain ();
	if (TRUE == thread_call_enter1 (mInitHardwareThread, (void *)provider))
	{
		//	<rdar://6223517>	Release the reference if the thread is already scheduled.
		release ();
	}

	result = TRUE;

Exit:
	return result;
}

//  <rdar://problem/6686515>
void AppleUSBAudioDevice::initHardwareThread (AppleUSBAudioDevice * aua, void * provider) {
	IOCommandGate *						cg;
	IOReturn							result;

	FailIf (NULL == aua, Exit);
//	FailIf (TRUE == aua->mTerminating, Exit);	

	cg = aua->getCommandGate ();
	if (cg) 
	{
		result = cg->runAction (aua->initHardwareThreadAction, provider);
	}

	aua->release ();	//	<rdar://6223517>	Release the reference that were retained before thread_call_enter1().
Exit:
	return;
}

IOReturn AppleUSBAudioDevice::initHardwareThreadAction (OSObject * owner, void * provider, void * arg2, void * arg3, void * arg4) {
	AppleUSBAudioDevice *				aua;
	IOReturn							result;

	result = kIOReturnError;

	aua = (AppleUSBAudioDevice *)owner;
	FailIf (NULL == aua, Exit);

	result = aua->protectedInitHardware ((IOService *)provider);

Exit:
	return result;
}

IOReturn AppleUSBAudioDevice::protectedInitHardware (IOService * provider) {
	char							string[kStringBufferSize];
	UInt8							stringIndex;
	IOReturn						err;
    Boolean							resultCode;
	OSArray *						streamNumberArray;
	UInt8							numStreamInterfaces;
	UInt8							numStreams;
	OSObject *						nameObject = NULL;
	OSString *						nameString = NULL;
	OSString *						localizedBundle = NULL;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::protectedInitHardware (%p)", this, provider);

	resultCode = FALSE;
	FailIf (NULL == mControlInterface, Exit);

	debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - %d configuration(s) on this device. This control interface number is %d", this, mControlInterface->GetDevice()->GetNumConfigurations (), mControlInterface->GetInterfaceNumber ());
		
	debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - Attempting to create configuration dictionary...", this);
	mConfigDictionary = AUAConfigurationDictionary::create (getConfigurationDescriptor(), mControlInterface->GetInterfaceNumber());	// rdar://5495653
	FailIf (NULL == mConfigDictionary, Exit);
	debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - Successfully created configuration dictionary.", this);

	if ( !mConfigDictionary->hasAudioStreamingInterfaces () )
	{
		// Bail since here is nothing related to audio streaming.
		debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - No audio streaming interfaces in configuration dictionary.", this);
		goto Exit;
	}
	
	// <rdar://problem/7378275>
	mTimeLock = IOLockAlloc ();
	FailIf (NULL == mTimeLock, Exit);

	mInterfaceLock = IORecursiveLockAlloc ();
	FailIf (NULL == mInterfaceLock, Exit);
	
	//  <rdar://problem/6369110>  Occassional kernel panic when multiple threads access mRegisteredEngines array simultaneously
	mRegisteredEnginesMutex = IORecursiveLockAlloc ();
	FailIf (NULL == mRegisteredEnginesMutex, Exit);

	//  <rdar://problem/6420832>  Serialize access to mRegisteredStreams
	mRegisteredStreamsMutex = IORecursiveLockAlloc ();
	FailIf (NULL == mRegisteredStreamsMutex, Exit);

	mControlGraph = BuildConnectionGraph (mControlInterface->GetInterfaceNumber ());
	FailIf ( NULL == mControlGraph, Exit );
	FailIf ( 0 == mControlGraph->getCount (), Exit );
	
	// [rdar://4801032]
	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		FailIf ( NULL == ( mClockGraph = buildClockGraph ( mControlInterface->GetInterfaceNumber () ) ), Exit );
		// From this moment forward, we may assume that a device is attempting to be USB 2.0 audio class-compliant by the presence of mClockGraph
		
		// Since supported sample rates are no longer listed explicitly in the USB 2.0 audio specification, we must discover them through device
		// request inquiries.
		
		FailIf ( kIOReturnSuccess != addSampleRatesFromClockSpace (), Exit );
		
	}

	// Check to make sure that the control interface we loaded against has audio streaming interfaces and not just MIDI.
	FailIf (kIOReturnSuccess != mConfigDictionary->getControlledStreamNumbers (&streamNumberArray, &numStreams), Exit);
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumStreamInterfaces (&numStreamInterfaces), Exit);
	debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - %d controlled stream(s). %d stream interface(s).", this, numStreams, numStreamInterfaces);
	FailIf (0 == numStreams, Exit);

	// [rdar://problem/5797761]
	// Try to get the name to give this control.  We will attempt to name it by the following order:
	// 1.  "USB Interface Name" for this control interface
	// 2.  Get the "InterfaceStringIndex" and GetStringDescriptor and use that string
	// 3.  Get the device's "USB Product Name" and use that
	err = kIOReturnError;
	string[0] = 0;

	debugIOLog ( "? AppleUSBAudioDevice[%p]::protectedInitHardware () - Trying to retrieve the USB Interface Name... ", this );
	nameObject = mControlInterface->getProperty ( "USB Interface Name" );
	if ( nameObject )
	{
		if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
		{
			debugIOLog ( "? AppleUSBAudioDevice[%p]::protectedInitHardware () - Retrieved product name %s from registry", this, nameString->getCStringNoCopy () );
			strncpy ( string, nameString->getCStringNoCopy(), kStringBufferSize );
			err = kIOReturnSuccess;
		} 
	}
	else
	{
		stringIndex = mControlInterface->GetInterfaceStringIndex ();
		if (0 != stringIndex) 
		{
			err = mControlInterface->GetDevice()->GetStringDescriptor (stringIndex, string, kStringBufferSize);
			// [rdar://3886272] - This could fail on some devices, so we should retry
			if (kIOReturnSuccess != err)
			{
				// Try a regular retry once
				debugIOLog ("! AppleUSBAudioDevice[%p]::protectedInitHardware () - couldn't get string descriptor. Retrying ...", this);
				err = mControlInterface->GetDevice()->GetStringDescriptor (stringIndex, string, kStringBufferSize);
			}
			if (kIOReturnSuccess != err)
			{
				// Reset the device and try one last time
				debugIOLog ("! AppleUSBAudioDevice[%p]::protectedInitHardware () - Still couldn't get string descriptor. Resetting device ...", this);
				mControlInterface->GetDevice()->ResetDevice();      // Doesn't matter if this fails
				IOSleep (50);										// Give the device 50 ms to get ready
				debugIOLog ("! AppleUSBAudioDevice[%p]::protectedInitHardware () - Last retry ...", this);
				err = mControlInterface->GetDevice()->GetStringDescriptor (stringIndex, string, kStringBufferSize);
			}
		} 
		else 
		{
			// [rdar://5159683] This device may not have either string, so read it from the IOUSBDevice.
			nameObject = NULL;
			nameString = NULL;
			debugIOLog ( "! AppleUSBAudioDevice[%p]::protectedInitHardware () - Trying to retrieve the product name from the IOUSBDevice ... ", this );
			nameObject = mControlInterface->GetDevice ()->getProperty ( "USB Product Name" );
			if ( nameObject )
			{
				if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
				{
					debugIOLog ( "? AppleUSBAudioDevice[%p]::protectedInitHardware () - Retrieved product name %s from registry", this, nameString->getCStringNoCopy () );
					strncpy ( string, nameString->getCStringNoCopy(), kStringBufferSize );
					err = kIOReturnSuccess;
				} 
			}
			else
			{
				// If there is no USB Product Name, there is no ProductString index, so just return an error.
				debugIOLog ("! AppleUSBAudioDevice[%p]::protectedInitHardware () - There was no USB Product Name", this);
				err = kIOReturnBadArgument;
			}	
		}
	}

	if (0 == string[0] || kIOReturnSuccess != err) 
	{
		strncpy (string, "Unknown USB Audio Device", kStringBufferSize);
	}

	setDeviceName (string);
	
	// <rdar://problem/6398888>  Attempt to retrieve the manufacturer name from registry before reading it from the device
	err = kIOReturnError;
	string[0] = 0;
	nameObject = mControlInterface->GetDevice ()->getProperty ( "USB Vendor Name" );
	if ( nameObject )
	{
		if ( NULL != ( nameString = OSDynamicCast ( OSString, nameObject ) ) )
		{
			debugIOLog ( "? AppleUSBAudioDevice[%p]::protectedInitHardware () - Retrieved vendor name %s from registry", this, nameString->getCStringNoCopy () );
			strncpy ( string, nameString->getCStringNoCopy (), kStringBufferSize );
			err = kIOReturnSuccess;
		} 
	}
	else 
	{
		stringIndex = mControlInterface->GetDevice()->GetManufacturerStringIndex ();
		if (0 != stringIndex) 
		{
			err = mControlInterface->GetDevice()->GetStringDescriptor (stringIndex, string, kStringBufferSize);
		}
	}

	if (0 == string[0] || kIOReturnSuccess != err) 
	{
		strncpy (string, "Unknown Manufacturer", kStringBufferSize);
	}

	setManufacturerName (string);
	setDeviceTransportType (kIOAudioDeviceTransportTypeUSB);
	// <rdar://problem/5797761> <rdar://problem/5797877>
	// If our provider has a kIOAudioDeviceLocalizedBundleKey, copy it to our entry.  This allows us to localize 
	// a string by using a codeless kext to specify the bundle
	// Try to get the localized kext bundle.  We will attempt to name it by the following order:
	// 1.  kIOAudioDeviceLocalizedBundleKey for this control interface
	// 2.  Get the device's kIOAudioDeviceLocalizedBundleKey and use that
	localizedBundle = OSDynamicCast ( OSString, mControlInterface->getProperty ( kIOAudioDeviceLocalizedBundleKey ) );
	if ( NULL == localizedBundle )
	{
		localizedBundle = OSDynamicCast ( OSString, mControlInterface->GetDevice()->getProperty ( kIOAudioDeviceLocalizedBundleKey ) );
	}
	if ( NULL != localizedBundle )
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - setting kIOAudioDeviceLocalizedBundleKey property to %s", this, localizedBundle->getCStringNoCopy () );
		setProperty ( kIOAudioDeviceLocalizedBundleKey, localizedBundle );
	}
	#if LOCALIZABLE
	else
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - setting kIOAudioDeviceLocalizedBundleKey property to AppleUSBAudio.kext", this);
		setProperty ( kIOAudioDeviceLocalizedBundleKey, "AppleUSBAudio.kext" );	
	}
	#endif


	// Create the audio engines
	resultCode = createAudioEngines ();
	FailIf ( FALSE == resultCode, Exit );	// <rdar://problem/6576824> AppleUSBAudio fails to unload properly
	
	resultCode = activateAudioEngines ();
	FailIf ( FALSE == resultCode, Exit );	// <rdar://problem/6576824> AppleUSBAudio fails to unload properly
	
	resultCode = super::initHardware (provider);
	FailIf ( FALSE == resultCode, Exit );	// <rdar://problem/6576824> AppleUSBAudio fails to unload properly
	
	// Start the anchored time stamp timer if necessary
	
	// Initialize time stamp member variables
	mLastUSBFrame = 0ull;
	mLastWallTime_nanos = 0ull;
	
	// Initialize mWallTimePerUSBCycle
	resetRateTimer();
	// We should get a new anchor immediately.
	updateUSBCycleTime ();							// <rdar://problem/7378275>, <rdar://problem/7666699>
	
	// [rdar://] This member variable keeps track an engine that has had a catastrophic failure that requires an emergency format change.
	mFailingAudioEngine = NULL;

	// Register and start update timer <rdar://problem/7378275>
	mTimerCallCount = 0;
	mRampUpdateCounter = 0;		// <rdar://problem/7666699>
	mUpdateTimer = IOTimerEventSource::timerEventSource (this, TimerAction);
	FailIf (NULL == mUpdateTimer, Exit);
	workLoop->addEventSource (mUpdateTimer);
	debugIOLog ("? AppleUSBAudioDevice[%p]::protectedInitHardware () - starting rate timer", this);
	TimerAction ( this, mUpdateTimer);
	
	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	mProcessStatusInterruptThread = thread_call_allocate ( ( thread_call_func_t )processStatusInterrupt, ( thread_call_param_t )this );
	FailIf ( NULL == mProcessStatusInterruptThread, Exit );
	checkForStatusInterruptEndpoint ();
	
	// Added for rdar://3993906 . This forces matchPropertyTable () to run again.
	// <rdar://7295322> Asynchronous to prevent deadlock if the device or interface is terminated while
	// registerService() is performing matching.
	IOService::registerService ( kIOServiceAsynchronous );

Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::protectedInitHardware (%p)", this, provider);

	return ( resultCode ? kIOReturnSuccess : kIOReturnError );	// <rdar://problem/6576824>
}

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
void AppleUSBAudioDevice::checkForStatusInterruptEndpoint( void )
{	
	IOUSBFindEndpointRequest			interruptEndpoint;
	UInt8								endpointAddress;
	UInt8								controlInterfaceNum;
	UInt32								messageLength;
	
	debugIOLog ("+ AppleUSBAudioDevice[%p]::checkForStatusInterruptEndpoint ()", this);
	
	FailIf ( NULL == mControlInterface, Exit );		// <rdar://7085810>
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
	// check for interrupt endpoint
	if ( mConfigDictionary->hasInterruptEndpoint ( controlInterfaceNum, 0 ) && ( kIOReturnSuccess == mConfigDictionary->getInterruptEndpointAddress( &endpointAddress, controlInterfaceNum, 0 ) ) )
	{
		// setup pipe
		if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol () )
		{
			// USB Audio 2.0
			mStatusInterruptBufferType = kInterruptDataMessageFormat;
			messageLength = sizeof(USBAUDIO_0200::InterruptDataMessageFormat);
		}
		else
		{
			// USB Audio 1.0
			mStatusInterruptBufferType = kAudioStatusWordFormat;
			messageLength = sizeof(AudioStatusWordFormat);
		}
		interruptEndpoint.type = kUSBInterrupt;
		interruptEndpoint.direction = kUSBIn;	// The associated endpoint always goes "in"
		interruptEndpoint.maxPacketSize = messageLength;
		interruptEndpoint.interval = 0xFF;
		mInterruptPipe = mControlInterface->FindNextPipe ( NULL, &interruptEndpoint );
		FailIf ( NULL == mInterruptPipe, Exit );

		if ( NULL == mInterruptEndpointMemoryDescriptor ) 
		{
			// <rdar://7000283> Remove the use of deprecated IOMallocContiguous(). Use IOBufferMemoryDescriptor to allocate memory instead.
			mInterruptEndpointMemoryDescriptor = IOBufferMemoryDescriptor::withOptions ( kIODirectionInOut, messageLength, 8 );
			FailIf ( NULL == mInterruptEndpointMemoryDescriptor, Exit );
			mStatusInterruptBuffer = mInterruptEndpointMemoryDescriptor->getBytesNoCopy ();
			FailIf ( NULL == mStatusInterruptBuffer, Exit );
			bzero ( mStatusInterruptBuffer, messageLength );
		}
		
		mStatusInterruptCompletion.target = ( void * )this;
		mStatusInterruptCompletion.action = statusInterruptHandler;
		mStatusInterruptCompletion.parameter = NULL;

		mInterruptPipe->retain ();

		FailMessage ( kIOReturnSuccess != mInterruptPipe->Read ( mInterruptEndpointMemoryDescriptor, 0, 0, mInterruptEndpointMemoryDescriptor->getLength(), &mStatusInterruptCompletion ) );
	}
	
Exit:

	debugIOLog ("- AppleUSBAudioDevice[%p]::checkForStatusInterruptEndpoint ()", this);
}

void AppleUSBAudioDevice::statusInterruptHandler( void * target, void * parameter, IOReturn status, UInt32 bufferSizeRemaining )
{
	AppleUSBAudioDevice *		self;
	
	FailIf ( NULL == target, Exit );
	self = ( AppleUSBAudioDevice * )target;
	
	debugIOLog ("+ AppleUSBAudioDevice[%p]::statusInterruptHandler ()", self);
	
	// spawn thread to process interrupt
	// <rdar://problem/6622487> Clear stall if detected
	
	if ( kIOReturnAborted == status || self->isInactive () )
	{
		debugIOLog ("! AppleUSBAudioDevice[%p]::statusInterruptHandler () error from USB: 0x%X or IOService inactive: %u, NOT reposting read to interrupt pipe", self, status, self->isInactive ());
	}
	else
	{
		if ( kIOUSBPipeStalled == status )
		{
			debugIOLog ("! AppleUSBAudioDevice[%p]::statusInterruptHandler () error from USB: kIOUSBPipeStalled, reposting read to interrupt pipe", self);
			// <rdar://7865729> Defer the ClearPipeStall() to our own thread.
			self->mInterruptPipeStalled = true;
		}
		else if ( kIOReturnSuccess != status )
		{
			debugIOLog ("! AppleUSBAudioDevice[%p]::statusInterruptHandler () error from USB: 0x%X, reposting read to interrupt pipe", self, status);
		}
	
		self->retain ();
		if ( TRUE == thread_call_enter1 ( ( thread_call_t )self->mProcessStatusInterruptThread, ( thread_call_param_t )self ) )
		{
			self->release ();
		}
	}
	
Exit:

	debugIOLog ("- AppleUSBAudioDevice[%p]::statusInterruptHandler ()", self);
	return;
}

void	AppleUSBAudioDevice::processStatusInterrupt ( void * arg )
{
	AppleUSBAudioDevice *		self;
	IOCommandGate *				cg;
	
	FailIf ( NULL == arg, Exit );
	self = ( AppleUSBAudioDevice * )arg;
	
	cg = self->getCommandGate ();
	if ( cg )
	{
		cg->runAction ( self->runStatusInterruptTask, ( void * )0, ( void * )0 );
	}
	
	self->release ();

Exit:

	return;
}

IOReturn AppleUSBAudioDevice::runStatusInterruptTask( OSObject * target, void * arg0, void * arg1, void * arg2, void * arg3 )
{
	AppleUSBAudioDevice *		self;
	
	FailIf ( NULL == target, Exit );
	self = ( AppleUSBAudioDevice * )target;
    self->handleStatusInterrupt ();
	
Exit:	

	return kIOReturnSuccess;
}

void AppleUSBAudioDevice::handleStatusInterrupt ( void )
{
	AudioStatusWordFormatPtr						audioStatusWord;
	USBAUDIO_0200::InterruptDataMessageFormatPtr	interruptDataMessage;
	UInt8											subType;
	AppleUSBAudioEngine	*							currentEngine = NULL;
	OSDictionary *									currentEngineInfo = NULL;
	IOUSBDevRequestDesc								devReq;
	OSSet *											defaultAudioControls;
	bool											interruptPending = false;
	bool											originatedFromACInterface = false;
	UInt8											bOriginator;
	
	debugIOLog ("+ AppleUSBAudioDevice[%p]::handleStatusInterrupt ()", this);
	
	FailIf ( NULL == mControlInterface, Exit );

	// <rdar://7865729> Clear the pipe stall here instead of the statusInterruptHandler(). ClearPipeStall() cannot be
	// made at that point because it will never be able to complete the correctly as that is the only thread available for
	// processing interrupts.
	if ( mInterruptPipeStalled )
	{
		debugIOLog ("! AppleUSBAudioDevice[%p]::handleStatusInterrupt () clearing pipe stall", this);
		if ( mInterruptPipe )
		{
			mInterruptPipe->ClearPipeStall ( true );
		}
		mInterruptPipeStalled = false;
	}

	if ( kAudioStatusWordFormat == mStatusInterruptBufferType )
	{
		audioStatusWord = ( AudioStatusWordFormatPtr )mStatusInterruptBuffer;
		interruptPending = ( 0 != ( audioStatusWord->bStatusType & 0x80 ) );
		originatedFromACInterface = ( 0 == ( audioStatusWord->bStatusType & 0x0F ) );
		bOriginator = audioStatusWord->bOriginator;
	}
	else // if ( kInterruptDataMessageFormat == mStatusInterruptBufferType )
	{
		interruptDataMessage = ( USBAUDIO_0200::InterruptDataMessageFormatPtr )mStatusInterruptBuffer;
		interruptPending = ( 0 == ( interruptDataMessage->bInfo & 0x1 ) ); // Class-specific interrupt.
		originatedFromACInterface = ( 0 == ( interruptDataMessage->bInfo & 0x02 ) ) &&  ( ( interruptDataMessage->wIndex & 0xFF ) == mControlInterface->GetInterfaceNumber () );
		bOriginator = (UInt8)( ( interruptDataMessage->wIndex >> 8 ) & 0xFF );
	}
	if ( interruptPending )
	{
		if ( originatedFromACInterface )	// AudioControl interface
		{
			mConfigDictionary->getSubType ( &subType, mControlInterface->GetInterfaceNumber (), 0, bOriginator );
			//  if this is a feature unit, dispatch to engines
			if ( ( FEATURE_UNIT == subType ) || ( SELECTOR_UNIT == subType ) )	//	<rdar://6523676>
			{
				FailIf ( NULL == mRegisteredEngines, Exit );
				for ( UInt8 engineIndex = 0; engineIndex < mRegisteredEngines->getCount (); engineIndex++ )
				{
					currentEngineInfo = OSDynamicCast ( OSDictionary, mRegisteredEngines->getObject ( engineIndex ) );
					FailIf ( NULL == currentEngineInfo, Exit );
					currentEngine = OSDynamicCast ( AppleUSBAudioEngine, currentEngineInfo->getObject ( kEngine ) );
					FailIf ( NULL == currentEngine, Exit );
					defaultAudioControls =  currentEngine->copyDefaultAudioControls ();
					if ( 0 != defaultAudioControls ) // <rdar://7695055>
					{
						controlHasChangedOnDevice ( bOriginator, defaultAudioControls );
						defaultAudioControls->release ();
						defaultAudioControls = NULL;
					}
				}
			}
			else if ( USBAUDIO_0200::CLOCK_SOURCE == subType )
			{
				debugIOLog ("? AppleUSBAudioDevice[%p]::handleStatusInterrupt () - CLOCK_SOURCE : %d", this, bOriginator );
				FailIf ( NULL == mRegisteredEngines, Exit );
				for ( UInt8 engineIndex = 0; engineIndex < mRegisteredEngines->getCount (); engineIndex++ )
				{
					currentEngineInfo = OSDynamicCast ( OSDictionary, mRegisteredEngines->getObject ( engineIndex ) );
					FailIf ( NULL == currentEngineInfo, Exit );
					currentEngine = OSDynamicCast ( AppleUSBAudioEngine, currentEngineInfo->getObject ( kEngine ) );
					FailIf ( NULL == currentEngine, Exit );
					currentEngine->updateClockStatus ( bOriginator );
				}
			}
		}
	}
	
	if ( kAudioStatusWordFormat == mStatusInterruptBufferType )
	{
		// Clear status interrupt
		devReq.bmRequestType = USBmakebmRequestType ( kUSBIn, kUSBClass, kUSBInterface );
		devReq.bRequest = GET_STAT;
		devReq.wValue = 0;
		FailIf ( NULL == mControlInterface, Exit );
		devReq.wIndex = ( 0xFF00 & ( bOriginator << 8 ) ) | ( 0x00FF & mControlInterface->GetInterfaceNumber () );
		devReq.wLength = 0;
		devReq.pData = NULL;
		FailIf ( kIOReturnSuccess != deviceRequest ( &devReq ), Exit );
	}
	else
	{
		// For USB Audio 2.0, interrupts are considered 'edge-triggered' type, meaning that an interrupt is generated whenever
		// an event occurs, but there is no specific action required from the Host to clear the interrupt condition.
	}
	
	// Clear the buffer before next read
	bzero ( mStatusInterruptBuffer, ( kAudioStatusWordFormat == mStatusInterruptBufferType ) ? sizeof (AudioStatusWordFormat) : sizeof (USBAUDIO_0200::InterruptDataMessageFormat) );
	
	// Queue next read
	if ( mInterruptPipe )
	{
		FailMessage ( kIOReturnSuccess != mInterruptPipe->Read ( mInterruptEndpointMemoryDescriptor, 0, 0, mInterruptEndpointMemoryDescriptor->getLength (), &mStatusInterruptCompletion ) );
	}
	
Exit:

	debugIOLog ("- AppleUSBAudioDevice[%p]::handleStatusInterrupt ()", this);
	return;
}

void AppleUSBAudioDevice::controlHasChangedOnDevice ( UInt8 controlID, OSSet * defaultAudioControls )
{
	UInt32					controlSubType;
	SInt16					deviceCur;
	SInt16					deviceMin;
	SInt16					deviceMax;	
	UInt16					volRes;
	SInt32					controlCur;
	OSNumber *				settingNumber = NULL;
	UInt8					numControls;
	UInt8					channelNum;
	IOAudioControl *		controlObject	= NULL ;
	OSCollectionIterator *	controlsIterator = NULL;
	
	// Find all volume/mute controls associated with this controlID
	controlsIterator = OSCollectionIterator::withCollection( defaultAudioControls );
    
    if ( controlsIterator )
	{
		while ( NULL != ( controlObject = OSDynamicCast( IOAudioControl, controlsIterator->getNextObject () ) ) )
		{
			if ( controlID == ( ( UInt8 )controlObject->getControlID () & 0xFF ) )	//	<rdar://6413207>
			{
				controlSubType = 0;
				controlSubType = controlObject->getSubType();
				
				// only adjust volume and mute subtypes
				switch ( controlSubType )
				{
					case kIOAudioLevelControlSubTypeVolume:
						
						FailIf ( NULL == mControlInterface, Exit );
						FailIf ( kIOReturnSuccess != mConfigDictionary->getNumControls ( &numControls, mControlInterface->GetInterfaceNumber (), 0, controlID ), Exit );
						for ( channelNum = 0; channelNum <= numControls; channelNum++ ) 
						{
							//	<rdar://6497818> Update the control with the values in the corresponding channel.
							if ( controlObject->getChannelID () == channelNum )
							{
								//	<rdar://6497818> Only if there is volume control present on the channel.
								if ( mConfigDictionary->channelHasVolumeControl ( mControlInterface->GetInterfaceNumber (), 0, controlID, channelNum ) )
								{
									// Read settings on device
									FailIf ( kIOReturnSuccess != getCurVolume ( controlID, channelNum, &deviceCur ), Exit );
									FailIf ( kIOReturnSuccess != getMinVolume ( controlID, channelNum, &deviceMin ), Exit );
									FailIf ( kIOReturnSuccess != getMaxVolume ( controlID, channelNum, &deviceMax ), Exit );
									getVolumeResolution ( controlID, channelNum, &volRes );
									
									// [rdar://4511427] Need to check volRes for class compliance (Audio Spec 5.2.2.4.3.2).
									FailIf ( 0 == volRes, Exit );
									
									// The current control value is a bit of a special case because the device may violate the spec. The rules are as follows:
									// * If the current value is negative infinity, use a control value of 0.
									// * If the current value is the minimum value, use a control value of 0.
									// * Otherwise calculate the current value as normal.
									if (		( (SInt16) kNegativeInfinity == deviceCur )
											||	( deviceCur == deviceMin ) )
									{
										controlCur = 0;
									}
									else
									{
										controlCur = ( ( deviceCur - deviceMin ) / volRes );
									}
									
									// Call hardwareValueChanged() for each with settings read from device
									FailIf ( NULL == ( settingNumber = OSNumber::withNumber ( controlCur, SIZEINBITS(SInt32) ) ), Exit );
									controlObject->hardwareValueChanged ( settingNumber );
									settingNumber->release ();
									settingNumber = NULL;
								}
								break;	//	<rdar://6497818>
							}
						}
						break;
					
					case kIOAudioToggleControlSubTypeMute:
					
						FailIf ( NULL == mControlInterface, Exit );
						FailIf ( kIOReturnSuccess != mConfigDictionary->getNumControls ( &numControls, mControlInterface->GetInterfaceNumber (), 0, controlID ), Exit );
						for ( channelNum = 0; channelNum <= numControls; channelNum++ ) 
						{
							//	<rdar://6497818> Update the control with the values in the corresponding channel.
							if ( controlObject->getChannelID () == channelNum )
							{
								//	<rdar://6497818> Only if there is mute control present on the channel.
								if ( mConfigDictionary->channelHasMuteControl ( mControlInterface->GetInterfaceNumber (), 0, controlID, channelNum ) )
								{
									FailIf ( kIOReturnSuccess != getCurMute ( controlID, controlObject->getChannelID (), &deviceCur ), Exit );
									FailIf ( NULL == ( settingNumber = OSNumber::withNumber ( deviceCur, SIZEINBITS(SInt16) ) ), Exit );
									controlObject->hardwareValueChanged ( settingNumber );
									settingNumber->release ();
									settingNumber = NULL;
								}
								break;	//	<rdar://6497818>
							}
						}
						break;
				
					//	<rdar://6523676> Handle changes in the input source selector
					case kIOAudioSelectorControlSubTypeInput:
						
						if ( kIOAudioControlTypeSelector == controlObject->getType() )
						{
							UInt8		oldSelectorPosition;
							UInt8		newSelectorPosition;
							
							FailIf ( NULL == mControlInterface, Exit );
							
							oldSelectorPosition = controlObject->getIntValue () & 0x000000FF;
							newSelectorPosition = getSelectorSetting ( controlID );
							
							if ( oldSelectorPosition != newSelectorPosition )
							{
								// The current selected input is no longer valid. Switch over to the new input source.
								OSArray * availableSelections = OSDynamicCast ( OSArray, controlObject->getProperty ( kIOAudioSelectorControlAvailableSelectionsKey ) );
								
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
												if ( ( selection & 0x000000FF ) == newSelectorPosition )
												{
													// Found the input source. Switch over to it.
													debugIOLog ( "? AppleUSBAudioDevice[%p]::controlHasChangedOnDevice () - Switch input selector over to selection = 0x%x", this, selection );
													controlObject->setValue ( selection );
													break;
												}
											}
										}
									}
								}
							}
						}
						break;

				}	// end switch
			}	// end if
		}	// end while
	}

Exit:
    
	controlsIterator->release ();

}

IOReturn AppleUSBAudioDevice::performPowerStateChange (IOAudioDevicePowerState oldPowerState, IOAudioDevicePowerState newPowerState, UInt32 *microSecsUntilComplete) {
	IOReturn						result;
	bool							performDeviceResetOnWake = false;
	IOUSBDevice *					usbDevice = NULL;
	
	// debugIOLog ("+ AppleUSBAudioDevice[%p]::performPowerStateChange (%d, %d, %p)", this, oldPowerState, newPowerState, microSecsUntilComplete);

	result = super::performPowerStateChange (oldPowerState, newPowerState, microSecsUntilComplete);

	// We need to stop the time stamp rate timer now
	if	(		(mUpdateTimer)
			&&	(kIOAudioDeviceSleep == newPowerState))
	{
		// Stop the timer and reset the anchor.
		debugIOLog ("? AppleUSBAudioDevice[%p]::performPowerStateChange () - Going to sleep - stopping the rate timer.", this);
		mUpdateTimer->cancelTimeout ();
		
		// The frame/time correlation isn't preserved across sleep/wake
		mLastUSBFrame = 0ull;
		mLastWallTime_nanos = 0ull;
	}
	
	if (oldPowerState == kIOAudioDeviceSleep) 
	{
		// A new anchor should be taken at the first opportunity. The timer action will handle this with the following instruction.
		// [rdar://4380545] We need to reset the wall time per USB cycle because the frame number could become invalid entering sleep.
		resetRateTimer();
		// We should get a new anchor immediately.
		updateUSBCycleTime ();							// <rdar://problem/7378275>, <rdar://problem/7666699>
		
#if RESETAFTERSLEEP
		// [rdar://4234453] Reset the device after waking from sleep just to be safe.		
		performDeviceResetOnWake = true;
#endif /* RESETAFTERSLEEP */

		FailIf (NULL == mControlInterface, Exit);

		// <rdar://7499125>	Check if the GetDevice() returns a valid pointer.
		usbDevice = mControlInterface->GetDevice ();
		FailIf (NULL == usbDevice, Exit);
			
		// <rdar://problem/6392504> Make sure that the device is connected before resetting it
		if ( kIOReturnSuccess == usbDevice->message( kIOUSBMessageHubIsDeviceConnected, NULL, 0 ) )	// <rdar://7499125>
		{
			if ( performDeviceResetOnWake )
			{
				debugIOLog ("? AppleUSBAudioDevice[%p]::performPowerStateChange () - Resetting port after wake from sleep ...", this);
				usbDevice->ResetDevice();	// <rdar://7499125>
				IOSleep (10);
			}
		}
		
		// We need to restart the time stamp rate timer now
		debugIOLog ("? AppleUSBAudioDevice[%p]::performPowerStateChange () - Waking from sleep - restarting the rate timer.", this);
		TimerAction ( this, mUpdateTimer);
		
		debugIOLog ("? AppleUSBAudioDevice[%p]::performPowerStateChange () - Flushing controls to the device ...", this);
		flushAudioControls ();
	}

Exit:
	// debugIOLog ("- AppleUSBAudioDevice[%p]::performPowerStateChange (%d, %d, %p)", this, oldPowerState, newPowerState, microSecsUntilComplete);
	return result;
}

void AppleUSBAudioDevice::stop (IOService *provider) {

	debugIOLog ("+ AppleUSBAudioDevice[%p]::stop (%p) - audioEngines = %p - rc=%d", this, provider, audioEngines, getRetainCount());

	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	if ( NULL != mInterruptPipe ) 
	{
		mInterruptPipe->Abort ();
		mInterruptPipe->release ();
		mInterruptPipe = NULL;
	}

	if ( mProcessStatusInterruptThread )
	{
		thread_call_cancel ( mProcessStatusInterruptThread );
		thread_call_free ( mProcessStatusInterruptThread );
		mProcessStatusInterruptThread = NULL;
	}
	
	if (mUpdateTimer)
	{
		// Stop the rate calculation timer
		debugIOLog ("? AppleUSBAudioDevice[%p]::stop () - Cancelling time stamp rate timer ...", this);
		mUpdateTimer->cancelTimeout ();
		mUpdateTimer->disable();
	}
	
	//  <rdar://7295322>
	if (mRegisteredEngines) 
	{
		mRegisteredEngines->release ();
		mRegisteredEngines = NULL;
	}
	
	//  <rdar://7295322>
	if (mRegisteredStreams) 
	{
		mRegisteredStreams->release ();
		mRegisteredStreams = NULL;
	}
	
	if ( 0 != mEngineArray )
	{
		for ( UInt32 engineIndex = 0; engineIndex < mEngineArray->getCount (); engineIndex++ )
		{
			AppleUSBAudioEngine * engine = OSDynamicCast ( AppleUSBAudioEngine, mEngineArray->getObject ( engineIndex ) );
			if ( 0 != engine )
			{
				engine->release ();
			}
		}
		mEngineArray->release ();
		mEngineArray = 0;
	}

	//  <rdar://problem/6686515>
	if (mInitHardwareThread)
	{
		thread_call_cancel (mInitHardwareThread);
		thread_call_free (mInitHardwareThread);
		mInitHardwareThread = NULL;
	}
	
	debugIOLog ("? AppleUSBAudioDevice[%p]::stop () - mControlInterface now closing ...", this);
    if (mControlInterface) 
	{
        mControlInterface->close (this);
        mControlInterface = NULL;
    }
		
	super::stop (provider);  // <rdar://problem/7800198> Move this call to the end of the method, after closing interface
	
	debugIOLog("- AppleUSBAudioDevice[%p]::stop ()", this);
}

// <rdar://problem/7666699>
bool AppleUSBAudioDevice::allEnginesStopped ( void )
{
	bool result = TRUE;
	
	if ( NULL != mEngineArray )
	{
		for ( UInt32 engineIndex = 0; engineIndex < mEngineArray->getCount (); engineIndex++ )
		{	
			AppleUSBAudioEngine * engine = OSDynamicCast ( AppleUSBAudioEngine, mEngineArray->getObject ( engineIndex ) );
			if ( 0 != engine )
			{
				if ( engine->mUSBStreamRunning )
				{
					result = FALSE;
					break;
				}
			}
		}
	}
	return result;
}

bool AppleUSBAudioDevice::createAudioEngines (void) {
	
	OSArray *	streamNumberArray;
	UInt8		numStreams;
	OSNumber *	streamInterfaceNumber;
	UInt8		numStreamInterfaces;
	bool		result = false;
	OSBoolean *	useSingleAudioEngine = NULL;
	

	// Find USB interfaces.
	debugIOLog ("+ AppleUSBAudioDevice[%p]::createAudioEngines ()", this);

	FailIf (NULL == mControlInterface, Exit);
	
	FailIf (kIOReturnSuccess != mConfigDictionary->getControlledStreamNumbers (&streamNumberArray, &numStreams), Exit);
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumStreamInterfaces (&numStreamInterfaces), Exit);

	debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - streamNumberArray = %p, numStreams = %lu, numStreamInterfaces = %lu", this, streamNumberArray, numStreams, numStreamInterfaces);

	// The override kext property to turn on/off single audio engine behavior. This takes priority over other mechanism to
	// determine if the device can support single audio engine.
	useSingleAudioEngine = OSDynamicCast ( OSBoolean, mControlInterface->getProperty ( "UseSingleAudioEngine" ) );
	
	if (NULL != useSingleAudioEngine)
	{
		if (useSingleAudioEngine->isTrue())
		{
			debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - Forced (via override kext) single audio engine", this);
			FailMessage ( false == ( result = createAudioEngine ( streamNumberArray ) ) );
		}
		else
		{
			debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - Forced (via override kext) separate audio engines", this);

			for (UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < numStreamInterfaces; streamInterfaceIndex++) 
			{
				FailIf (NULL == (streamInterfaceNumber = OSDynamicCast (OSNumber, streamNumberArray->getObject (streamInterfaceIndex))), Exit);
				
				OSArray * streamInterfaceNumberArray = OSArray::withCapacity ( 1 );
				FailIf ( 0 == streamInterfaceNumberArray, Exit );

				streamInterfaceNumberArray->setObject ( streamInterfaceNumber );
				
				result = createAudioEngine ( streamInterfaceNumberArray );
				FailWithAction ( !result, streamInterfaceNumberArray->release (), Exit );
				
				streamInterfaceNumberArray->release ();
			}
		}
	}
	else if (getMultipleAudioEngineDevice ())
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - Known devices that requires separate audio engines", this);

		for (UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < numStreamInterfaces; streamInterfaceIndex++) 
		{
			FailIf (NULL == (streamInterfaceNumber = OSDynamicCast (OSNumber, streamNumberArray->getObject (streamInterfaceIndex))), Exit);
			
			OSArray * streamInterfaceNumberArray = OSArray::withCapacity ( 1 );
			FailIf ( 0 == streamInterfaceNumberArray, Exit );

			streamInterfaceNumberArray->setObject ( streamInterfaceNumber );
			
			result = createAudioEngine ( streamInterfaceNumberArray );
			FailWithAction ( !result, streamInterfaceNumberArray->release (), Exit );
			
			streamInterfaceNumberArray->release ();
		}
	}
	else
	{
		// If this device is a known single sample rate device, then we should use a single audio engine.
		if (getSingleSampleRateDevice ())
		{
			debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - Known single sample rate device", this);

			FailMessage ( false == ( result = createAudioEngine ( streamNumberArray ) ) );
		}
		else
		{
			OSArray * commonSampleRatesStreamList = NULL;
			
			OSArray * availableStreamsList = OSArray::withArray ( streamNumberArray );
			FailIf ( 0 == availableStreamsList, Exit );
			
			// Find all streams with common sample rates.
			while ( NULL != ( commonSampleRatesStreamList = findStreamsWithCommonSampleRates ( availableStreamsList ) ) )
			{
				OSArray * compatibleEndpointsStreamList = NULL;
				
				// Find all streams with compatible sample rates.
				while ( NULL != ( compatibleEndpointsStreamList = findStreamsWithCompatibleEndpoints ( commonSampleRatesStreamList ) ) )
				{
					// If it is USB Audio 2.0 devices, then find all streams with common clock.
					if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol () )
					{					
						OSArray * commonClockStreamList = NULL;
						
						// Find all streams with common clock.
						while ( NULL != ( commonClockStreamList = findStreamsWithCommonClocks ( compatibleEndpointsStreamList ) ) )
						{
							debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - USB Audio 2.0 device", this);
							FailMessage ( false == ( result = createAudioEngine ( commonClockStreamList ) ) );
							
							commonClockStreamList->release ();
							if (!result) break;
						}
					}
					else
					{
						debugIOLog ("? AppleUSBAudioDevice[%p]::createAudioEngines () - USB Audio 1.0 device", this);
						FailMessage ( false == ( result = createAudioEngine ( compatibleEndpointsStreamList ) ) );
					}
					
					compatibleEndpointsStreamList->release ();
					if (!result) break;
				}
				
				commonSampleRatesStreamList->release ();
				if (!result) break;
			}
			
			availableStreamsList->release ();
		}
	}

Exit:
	
	//  <rdar://problem/6892754> 10.5.7 Regression: Devices with unsupported formats stopped working
	//  If all the streams have unsupported formats and no engines are created, then return an error.
	if ( 0 == mEngineArray )
	{
		result = false;
 	}
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::createAudioEngines () - 0x%x", this, result);
	
	return result;
}

bool AppleUSBAudioDevice::activateAudioEngines (void) {
	
	bool	result = true;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::activateAudioEngines ()", this);

	// Activate audio engines.
	if ( 0 != mEngineArray )
	{
		for ( UInt32 engineIndex = 0; engineIndex < mEngineArray->getCount (); engineIndex++ )
		{
			AppleUSBAudioEngine * engine = OSDynamicCast ( AppleUSBAudioEngine, mEngineArray->getObject ( engineIndex ) );
			FailWithAction ( 0 == engine, result = false, Exit );
			FailWithAction ( kIOReturnSuccess != activateAudioEngine ( engine ), result = false, Exit );
		}
	}
	
Exit:
	//  <rdar://problems/6576824&6674310> AppleUSBAudio fails to unload properly
	if ( !result )
	{		
		deactivateAllAudioEngines ();
	}
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::activateAudioEngines () - 0x%x", this, result);
	
	return result;
}

bool AppleUSBAudioDevice::createAudioEngine (OSArray * streamInterfaceNumberArray) {

	AppleUSBAudioEngine *	engine = NULL;
	bool					result = false;
	OSArray *				newStreamInterfaceArray = NULL;	//  <rdar://problem/6892754>
	bool					formatSupported;				//  <rdar://problem/6892754>
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::createAudioEngine ()", this );

	FailIf ( NULL == streamInterfaceNumberArray, Exit );
	
	newStreamInterfaceArray = OSArray::withCapacity ( streamInterfaceNumberArray->getCount() );
	FailIf ( 0 == newStreamInterfaceArray, Exit );
	
	debugIOLog ( "? AppleUSBAudioDevice[%p]::createAudioEngine () - create audio engine with stream interfaces:", this );
	
	//  <rdar://problem/6892754> 10.5.7 Regression: Devices with unsupported formats stopped working
	for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < streamInterfaceNumberArray->getCount (); streamInterfaceIndex++ ) 
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast ( OSNumber, streamInterfaceNumberArray->getObject ( streamInterfaceIndex ) );
		if ( NULL != streamInterfaceNumber )
		{
			debugIOLog ( "--> #%u", streamInterfaceNumber->unsigned8BitValue () );
				
			// <rdar://problem/6892754>
			// In the case that anything goes wrong while checking the formats, maintain the original behavior and add the interface to the list regardless of the format check
			if ( ( kIOReturnSuccess != interfaceContainsSupportedFormat ( streamInterfaceNumber->unsigned8BitValue (), &formatSupported ) ) || formatSupported )
			{
				newStreamInterfaceArray->setObject ( streamInterfaceNumber );
			}
		}
		else
		{
			debugIOLog ( "! AppleUSBAudioDevice[%p]::createAudioEngine () - could not get streamInterfaceNumber", this );
		}
	}
	
	debugIOLog ( "? AppleUSBAudioDevice[%p]::createAudioEngine () final new array count: %d", this, newStreamInterfaceArray->getCount() );
	
	//  <rdar://problem/6892754>
	//  Create an engine with the new stream interface array that contains interfaces with supported format types 
	if ( newStreamInterfaceArray->getCount() > 0 )
	{
	
		if ( 0 == mEngineArray )
		{
			mEngineArray = OSArray::withCapacity ( 1 );
			FailIf ( 0 == mEngineArray, Exit );
		}
		
		engine = OSTypeAlloc ( AppleUSBAudioEngine );
		FailIf ( 0 == engine, Exit );
		
		if ( engine->init ( newStreamInterfaceArray ) )
		{		
			// <rdar://problem/7349398>
			OSNumber *displayRoutingRead = NULL;
			OSNumber *displayRoutingWrite = NULL;
			
			if ( NULL != mControlInterface )
			{
				displayRoutingRead = OSDynamicCast ( OSNumber, mControlInterface->GetDevice ()->getProperty ( kDisplayRoutingPropertyKey ) );
			}
			
			if ( NULL != displayRoutingRead )
			{
				UInt32 displayRoutingValue = 0;
				displayRoutingValue = displayRoutingRead->unsigned32BitValue ();
				
				if ( 0 == displayRoutingValue )	// 0 is an invalid value
				{
					displayRoutingValue = 1;
				}
				
				displayRoutingWrite = OSNumber::withNumber ( displayRoutingValue, 32 );
				if ( NULL != displayRoutingWrite )
				{
					engine->setProperty ( kDisplayRoutingPropertyKey, displayRoutingWrite );
					displayRoutingWrite->release ();
				}
			}
			
			if ( !mEngineArray->setObject ( engine ) )
			{
				engine->release ();		
			}
		}
		else
		{
			engine->release ();		
		}
	} // else there are no streams with supported format types: in this case, don't init the engine but still return true as this is not an error <rdar://problem/6892754>
	
	result = true;

Exit:
	
	if ( newStreamInterfaceArray )
	{
		newStreamInterfaceArray->release();
	}
	
	debugIOLog ( "- AppleUSBAudioDevice[%p]::createAudioEngine ()", this );

	return result;
}

// <rdar://problem/6892754>
// Look at all of alternate settings and make sure that we support at least one of the formats (Type II AC-3 format not supported)
IOReturn AppleUSBAudioDevice::interfaceContainsSupportedFormat ( UInt8 streamInterface, bool *formatSupported ) {
	
	IOReturn				returnCode = kIOReturnSuccess;
	UInt8					numAltSettings;
	UInt8					altSettingIndex;
	UInt16					format;
	bool					startAtZero = true;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::interfaceContainsSupportedFormat ()", this );
	
	FailWithAction ( NULL == mConfigDictionary, returnCode = kIOReturnError, Exit );
	startAtZero = mConfigDictionary->alternateSettingZeroCanStream ( streamInterface );
	FailIf ( kIOReturnSuccess != ( returnCode = mConfigDictionary->getNumAltSettings ( &numAltSettings, streamInterface ) ), Exit );
	*formatSupported = false;
	
	for ( altSettingIndex = ( startAtZero ? 0 : 1 ); altSettingIndex < numAltSettings; altSettingIndex++ ) 
	{
		FailIf ( kIOReturnSuccess != ( returnCode = mConfigDictionary->getFormat ( &format, streamInterface, altSettingIndex ) ), Exit );
		if ( AC3 != format )
		{
			*formatSupported = true;
		}
		else
		{
			IOLog( "WARNING: AppleUSBAudio has detected an unsupported format type: Type II AC-3" );
		}
	}
	
Exit:
	
	debugIOLog ( "- AppleUSBAudioDevice[%p]::interfaceContainsSupportedFormat () streamInterface: %u returnCode: %d formatSupported: %u", this, streamInterface, returnCode, *formatSupported );
	
	return returnCode;
}

// Return FALSE if you don't want PRAM updated on a volume change, TRUE if you want it updated.
// Only update PRAM if we're on a Cube and the speakers are Cube, SoundSticks, or Mirconas (somethings).
Boolean AppleUSBAudioDevice::ShouldUpdatePRAM (void) {
	const IORegistryPlane *			usbPlane;
	IORegistryEntry *				usbRegEntry;
	OSObject *						obj;
	OSNumber *						number;
	UInt16							productID;
	UInt16							vendorID;
	Boolean							speakersGood;
	Boolean							connectionGood;
	Boolean							result;

	// Assume failure
	result = FALSE;
	speakersGood = FALSE;
	connectionGood = FALSE;
	FailIf (NULL == mControlInterface, Exit);

	// Make sure they're speakers that can support boot beep
	vendorID = mControlInterface->GetDevice()->GetVendorID ();
	debugIOLog ("? AppleUSBAudioDevice[%p]::ShouldUpdatePRAM () - speaker's vendorID = 0x%x", this, vendorID);
	if (kIOUSBVendorIDAppleComputer == vendorID || kIOUSBVendorIDHarmonKardon == vendorID || kIOUSBVendorMicronas == vendorID) 
	{
		speakersGood = TRUE;
	}
	// debugIOLog ("speakersGood = %d", speakersGood);

	// They have to be plugged into a root hub or a hub in monitor that can support boot beep
	if (TRUE == speakersGood) 
	{
		usbPlane = getPlane (kIOUSBPlane);
		FailIf (NULL == usbPlane, Exit);

		usbRegEntry = mControlInterface->GetDevice()->getParentEntry (usbPlane);
		FailIf (NULL == usbRegEntry, Exit);

		obj = usbRegEntry->getProperty (kUSBVendorID);
		number = OSDynamicCast (OSNumber, obj);
		FailIf (NULL == number, Exit);

		vendorID = number->unsigned32BitValue ();
		// debugIOLog ("hub's vendorID = 0x%x", vendorID);

		if (kIOUSBVendorIDAppleComputer == vendorID) 
		{
			obj = usbRegEntry->getProperty (kUSBDevicePropertyLocationID);
			number = OSDynamicCast (OSNumber, obj);
			FailIf (NULL == number, Exit);

			if (OSDynamicCast (IOUSBRootHubDevice, usbRegEntry)) 
			{
				// It's connected to the root hub
				connectionGood = TRUE;
				debugIOLog ("? AppleUSBAudioDevice[%p]::ShouldUpdatePRAM () - Directly connected to the root hub", this);
			} 
			else 
			{
				obj = usbRegEntry->getProperty (kUSBProductID);
				number = OSDynamicCast (OSNumber, obj);
				FailIf (NULL == number, Exit);

				productID = number->unsigned32BitValue ();
				// debugIOLog ("hub's productID = 0x%x", productID);

				if (kStudioDisplay15CRT == productID || kStudioDisplay17CRT == productID || kCinemaDisplay == productID || kStudioDisplay17FP == productID) 
				{
					// It's connected to a good monitor
					connectionGood = TRUE;
					// debugIOLog ("Connected to a capable monitor");
				}
			}
		}
	}
	// debugIOLog ("connectionGood = %d", connectionGood);

	// And there CANNOT be a "sound" node in the device tree so that OF will boot beep through them
	if (TRUE == connectionGood && FALSE == FindSoundNode ()) 
	{
		result = TRUE;
	}

Exit:
	debugIOLog ("? AppleUSBAudioDevice[%p]::ShouldUpdatePRAM () - result = %d", this, result);
	return result;
}

Boolean AppleUSBAudioDevice::FindSoundNode (void) {
	const IORegistryPlane *			dtPlane;
	IORegistryEntry *				regEntry;
	IORegistryIterator *			iterator;
	Boolean							found;
	Boolean							done;
	const char *					name;

	found = FALSE;

	dtPlane = IORegistryEntry::getPlane (kIODeviceTreePlane);
	FailIf (NULL == dtPlane, Exit);

	iterator = IORegistryIterator::iterateOver (dtPlane, kIORegistryIterateRecursively);
	FailIf (NULL == iterator, Exit);

	done = FALSE;
	regEntry = iterator->getNextObject ();
	while (NULL != regEntry && FALSE == done) 
	{
		name = regEntry->getName ();
		if (0 == strcmp (name, "mac-io")) 
		{
			// This is where we want to start the search
			iterator->release ();		// release the current iterator and make a new one rooted at "mac-io"
			iterator = IORegistryIterator::iterateOver (regEntry, dtPlane);
			done = TRUE;
		}
		regEntry = iterator->getNextObject ();
	}

	// Now the real search begins...
	regEntry = iterator->getNextObject ();
	while (NULL != regEntry && FALSE == found) 
	{
		name = regEntry->getName ();
		if (0 == strcmp (name, "sound")) 
		{
			found = TRUE;
		}
		regEntry = iterator->getNextObject ();
	}

	iterator->release ();

Exit:
	return found;
}

IOReturn AppleUSBAudioDevice::message (UInt32 type, IOService * provider, void * arg) {
	AppleUSBAudioEngine	*	currentEngine = NULL;
	OSDictionary *			currentEngineInfo = NULL;
	
	debugIOLog ("+ AppleUSBAudioDevice[%p]::message (0x%x, %p) - rc=%d", this, type, provider, getRetainCount ());

	switch (type) 
	{
		case kIOMessageServiceIsTerminated:
		case kIOMessageServiceIsRequestingClose:
			if (mControlInterface != NULL && mControlInterface == provider) 
			{
				mControlInterface->close (this);
				mControlInterface = NULL;
			}
			break;
		case kIOUSBMessagePortHasBeenReset:
			// If the device has been reset, we must take steps to ensure that streaming can be resumed.
			// Flush controls to the device in case the device reset changed their states.
			debugIOLog ("? AppleUSBAudioDevice[%p]::message () - Flushing controls to the device.", this);
			flushAudioControls ();
			FailIf (NULL == mRegisteredEngines, Exit);
			debugIOLog ("? AppleUSBAudioDevice[%p]::message () - Resetting engines.", this);
			for (UInt8 engineIndex = 0; engineIndex < mRegisteredEngines->getCount (); engineIndex++)
			{
				currentEngineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineIndex));
				FailIf (NULL == currentEngineInfo, Exit);
				currentEngine = OSDynamicCast (AppleUSBAudioEngine, currentEngineInfo->getObject (kEngine));
				FailIf (NULL == currentEngine, Exit);
				
				debugIOLog ("? AppleUSBAudioDevice[%p]::message () - Resetting engine %p...", this, currentEngine);
				currentEngine->pauseAudioEngine ();
				// <rdar://6277511>	Make sure that mStreamInterface is not null.
				if (currentEngine)
				{
					// Close the stream interface just to be safe.
					currentEngine->closeStreamInterfaces ();
					
					// Reopen the stream interface
					currentEngine->openStreamInterfaces ();
				}
				currentEngine->resumeAudioEngine ();
			}
			break;
		default:
			;
	}
Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::message (0x%x, %p) - rc=%d", this, type, provider, getRetainCount ());
	return kIOReturnSuccess;
}

IOUSBInterface * AppleUSBAudioDevice::getUSBInterface ( UInt8 interfaceNumber ) {
	IOUSBInterface *	interface = NULL;
	OSIterator *		iterator = NULL;
	OSObject *			object = NULL;
	
	FailIf ( NULL == mControlInterface, Exit );
	
	iterator = mControlInterface->GetDevice()->getChildIterator ( gIOServicePlane );
	FailIf ( NULL == iterator, Exit );
	
	object = iterator->getNextObject ();
	
	while ( 0 != object )
	{
		interface = OSDynamicCast ( IOUSBInterface, object );
		
		if ( 0 != interface )
		{
			if ( interface->GetInterfaceNumber () == interfaceNumber )
			{
				break;
			}
		}
		
		object = iterator->getNextObject ();
	}
	
	iterator->release ();	//	<rdar://7362494>

Exit:
	return interface;
}

// added for rdar://5495653. Returns the current configuration descriptor.
const IOUSBConfigurationDescriptor * AppleUSBAudioDevice::getConfigurationDescriptor () {
	IOUSBDevice *							usbDevice;
	const IOUSBConfigurationDescriptor *	configDescriptor = NULL;
	UInt8									currentConfigValue = 0;
	UInt8									numConfigs = 0;
	UInt8									index;
	
	debugIOLog ("+ AppleUSBAudioDevice[%p]::getConfigurationDescriptor ()", this);

	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	
	numConfigs = usbDevice->GetNumConfigurations ();

	debugIOLog ("? AppleUSBAudioDevice[%p]::getConfigurationDescriptor () - numConfigs = %d", this, numConfigs);
	
	if (1 < numConfigs)
	{
		usbDevice->GetConfiguration (&currentConfigValue);	

		debugIOLog ("? AppleUSBAudioDevice[%p]::getConfigurationDescriptor () - currentConfigValue = %d", this, currentConfigValue);
		
		for (index = 0; index < numConfigs; index++)
		{
			const IOUSBConfigurationDescriptor * descriptor = usbDevice->GetFullConfigurationDescriptor (index);
			
			if ((NULL != descriptor) && (descriptor->bConfigurationValue == currentConfigValue))
			{
				debugIOLog ("? AppleUSBAudioDevice[%p]::getConfigurationDescriptor () - Found config %d (%p) at index %d", this, currentConfigValue, descriptor, index);
				configDescriptor = descriptor;
				break;
			}
		}
	}
	else
	{
		configDescriptor = usbDevice->GetFullConfigurationDescriptor (0);
	}
	
Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::getConfigurationDescriptor () - configDescriptor=%p", this, configDescriptor);
	return configDescriptor;
}

// added for rdar://4168019 . Returns the device speed (high, full, or low).
UInt8 AppleUSBAudioDevice::getDeviceSpeed () {
	IOUSBDevice *			usbDevice;
	UInt8					speed = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	speed = usbDevice->GetSpeed ();
	#if DEBUGLOGGING
	switch (speed)
	{
		case kUSBDeviceSpeedLow:
			debugIOLog ("? AppleUSBAudioDevice[%p]::getDeviceSpeed () = kUSBDeviceSpeedLow", this);
			break;
		case kUSBDeviceSpeedFull:
			debugIOLog ("? AppleUSBAudioDevice[%p]::getDeviceSpeed () = kUSBDeviceSpeedFull", this);
			break;
		case kUSBDeviceSpeedHigh:
			debugIOLog ("? AppleUSBAudioDevice[%p]::getDeviceSpeed () = kUSBDeviceSpeedHigh", this);
			break;
		default:
			debugIOLog ("? AppleUSBAudioDevice[%p]::getDeviceSpeed () = %d (UNKNOWN)", this, speed);
	}
	#endif
	
Exit:
	return speed;
}

UInt64 AppleUSBAudioDevice::getUSBFrameNumber () {
	IOUSBDevice *			usbDevice;
	UInt64					frameNumber = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	frameNumber = usbDevice->GetBus()->GetFrameNumber();
	
Exit:
	return frameNumber;
}

UInt8 AppleUSBAudioDevice::getManufacturerStringIndex () {
	IOUSBDevice *			usbDevice;
	UInt8					stringIndex = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	stringIndex = usbDevice->GetManufacturerStringIndex();
	
Exit:
	return stringIndex;
}

UInt8 AppleUSBAudioDevice::getProductStringIndex () {
	IOUSBDevice *			usbDevice;
	UInt8					stringIndex = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	stringIndex = usbDevice->GetProductStringIndex();
	
Exit:
	return stringIndex;
}

UInt8 AppleUSBAudioDevice::getSerialNumberStringIndex () {
	IOUSBDevice *			usbDevice;
	UInt8					stringIndex = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	stringIndex = usbDevice->GetSerialNumberStringIndex();
	
Exit:
	return stringIndex;
}

IOReturn AppleUSBAudioDevice::getStringDescriptor(UInt8 index, char *buf, int maxLen, UInt16 lang) {
	IOUSBDevice *			usbDevice;
	IOReturn				result = kIOReturnError;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	result = usbDevice->GetStringDescriptor(index, buf, maxLen, lang);
	
Exit:
	return result;
}

UInt16 AppleUSBAudioDevice::getVendorID () {
	IOUSBDevice *			usbDevice;
	UInt16					vendorID = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	vendorID = usbDevice->GetVendorID();
	
Exit:
	return vendorID;
}

UInt16 AppleUSBAudioDevice::getProductID () {
	IOUSBDevice *			usbDevice;
	UInt16					productID = 0;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	productID = usbDevice->GetProductID();
	
Exit:
	return productID;
}

OSNumber * AppleUSBAudioDevice::getLocationID () {
	IOUSBDevice *			usbDevice;
	OSNumber *				usbLocation = NULL;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	usbLocation = OSDynamicCast (OSNumber, usbDevice->getProperty (kUSBDevicePropertyLocationID));
	
Exit:
	return usbLocation;
}

// added for rdar://3959606 . Detects whether this is a full speed device plugged into a high speed hub.
bool AppleUSBAudioDevice::detectSplitTransactions () {
	IOUSBDevice *			usbDevice;
	const IORegistryPlane *	usbPlane = getPlane (kIOUSBPlane);
	IORegistryEntry *		currentEntry;
	UInt8					speed;
	bool					canStop = false;
	bool					splitTransactions = false;
	
	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	currentEntry = OSDynamicCast (IORegistryEntry, usbDevice);
	
	if (kUSBDeviceSpeedHigh == usbDevice->GetSpeed ())
	{
		debugIOLog ("? AppleUSBAudioDevice::detectSplitTransactions () - This is a high speed device, so there are no split transactions.");
		splitTransactions = false;
		canStop = true;
	}
	
	while	(		(!canStop)
				&&	(currentEntry)
				&&	(usbDevice))
	{
		// Searching for a high speed hub. Since it is not possible to run at high speed with a full speed
		// hub closer to the root hub in the chain, we can stop when we find the first high speed hub.
		
		speed = usbDevice->GetSpeed ();
		if (kUSBDeviceSpeedHigh == speed)
		{
			// Must be connected via USB 2.0 hub
			debugIOLog ("? AppleUSBAudioDevice::detectSplitTransactions () = true");
			splitTransactions = true;
			canStop = true;
		}
		else
		{
			// Get parent in USB plane
			currentEntry = OSDynamicCast (IORegistryEntry, currentEntry->getParentEntry (usbPlane));
			
			// If the current registry entry is not a device, this will make usbDevice NULL and exit the loop
			usbDevice = OSDynamicCast (IOUSBDevice, currentEntry);
		}
	} // end while
	
Exit:
	return splitTransactions;
}

Boolean AppleUSBAudioDevice::checkForUHCI () {
	Boolean					result = FALSE;
	IOUSBDevice *			usbDevice;
	const IORegistryPlane *	servicePlane = getPlane (kIOServicePlane);
	IOService *				currentEntry;
	IORegistryEntry *		parentEntry;
	char					serviceName[20];

	FailIf (NULL == mControlInterface, Exit);
	usbDevice = OSDynamicCast (IOUSBDevice, mControlInterface->GetDevice());
	FailIf (NULL == usbDevice, Exit);
	parentEntry = usbDevice->getParentEntry (servicePlane);
	FailIf (NULL == parentEntry, Exit);
	currentEntry = OSDynamicCast (IOService, parentEntry);
	FailIf (NULL == currentEntry, Exit);
	strncpy (serviceName, currentEntry->getName (servicePlane), 20);

	while (    (currentEntry)
			&& strcmp (serviceName, "AppleUSBUHCI")
			&& strcmp (serviceName, "AppleUSBOHCI")
			&& strcmp (serviceName, "AppleUSBEHCI"))
	{
		// Searching for the root hub type. We only expect one of three types, so we can stop when we have found any of them.
		// The types are AppleUSBUHCI, AppleUSBOHCI, and AppleUSBEHCI.
		
		// Get parent in IOService plane
		parentEntry = currentEntry->getParentEntry (servicePlane);
		FailIf (NULL == parentEntry, Exit);
		currentEntry = OSDynamicCast (IOService, parentEntry);
		if (currentEntry)
		{
			strncpy (serviceName, currentEntry->getName (servicePlane), 20);
		}
	} // end while
	FailIf (NULL == currentEntry, Exit);
	if (!strcmp (serviceName, "AppleUSBUHCI"))
	{
		// We are on a UHCI connection
		debugIOLog ("? AppleUSBAudioDevice::checkForUHCI () - UHCI connection detected!\n");
		result = TRUE;
	}
Exit:
	return result;
}

// <rdar://6420832>
IOReturn AppleUSBAudioDevice::registerEngineInfo (AppleUSBAudioEngine * usbAudioEngine) {
	OSDictionary *						engineInfo;
	SInt32								oldEngineIndex;
	IOReturn							result = kIOReturnError;
	
	if (NULL == mRegisteredEngines) 
	{
		//  <rdar://problem/6369110>  Occassional kernel panic when multiple threads access mRegisteredEngines array simultaneously
		mRegisteredEngines = OSArray::withCapacity (2);
		FailIf (NULL == mRegisteredEngines, Exit);
	}

	engineInfo = OSDictionary::withCapacity (1);
	FailIf (NULL == engineInfo, Exit);
	engineInfo->setObject (kEngine, usbAudioEngine);
	
	//  <rdar://problem/6369110>
	if ( mRegisteredEnginesMutex )
	{
		IORecursiveLockLock (mRegisteredEnginesMutex);
	}
	
	// [rdar://4102789] Be sure to preserve the integrity of mRegisteredEngines. It is vital to perform emergency format changes.
	oldEngineIndex = getEngineInfoIndex (usbAudioEngine);
	if (-1 != oldEngineIndex)
	{
		// This engine already has information stored. We should replace it.
		mRegisteredEngines->replaceObject (oldEngineIndex, engineInfo);
	}
	else
	{
		mRegisteredEngines->setObject (engineInfo);
	}
	
	if ( mRegisteredEnginesMutex )
	{
		IORecursiveLockUnlock (mRegisteredEnginesMutex);
	}
	
	engineInfo->release ();
	
	result = kIOReturnSuccess;
	
Exit:
	return result;
}

SInt32 AppleUSBAudioDevice::getEngineInfoIndex (AppleUSBAudioEngine * inAudioEngine) {
	OSDictionary *						engineInfo;
	AppleUSBAudioEngine *				usbAudioEngine;
	UInt16								engineIndex;
	SInt32								returnIndex;

	returnIndex = -1;
	
	//  <rdar://problem/6369110>  Occassional kernel panic when multiple threads access mRegisteredEngines array simultaneously
	if ( mRegisteredEnginesMutex )
	{
		IORecursiveLockLock (mRegisteredEnginesMutex);
	}
	
	if (mRegisteredEngines) 
	{
		for (engineIndex = 0; engineIndex < mRegisteredEngines->getCount (); engineIndex++) 
		{
			engineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineIndex));
			if (engineInfo) 
			{
				usbAudioEngine = OSDynamicCast (AppleUSBAudioEngine, engineInfo->getObject (kEngine));
				if (inAudioEngine == usbAudioEngine) 
				{
					returnIndex = engineIndex;
					break;		// Get out of for loop with correct index
				}
			}
		}
	}
	
	if ( mRegisteredEnginesMutex )
	{
		IORecursiveLockUnlock (mRegisteredEnginesMutex);
	}

	return returnIndex;
}

// <rdar://6420832>
IOReturn AppleUSBAudioDevice::registerStreamInfo (UInt8 interfaceNum, UInt8 altSettingNum) {
	OSDictionary *						streamInfo;
	SInt32								oldStreamIndex;
	OSNumber *							number;
	IOReturn							result = kIOReturnError;
	
	if (NULL == mRegisteredStreams) 
	{
		mRegisteredStreams = OSArray::withCapacity (4);
		FailIf (NULL == mRegisteredStreams, Exit);
	}

	streamInfo = OSDictionary::withCapacity (2);
	FailIf (NULL == streamInfo, Exit);
	number = OSNumber::withNumber (interfaceNum, 8);
	streamInfo->setObject (kInterface, number);
	number->release ();
	number = OSNumber::withNumber (altSettingNum, 8);
	streamInfo->setObject (kAltSetting, number);
	number->release ();
	
	if ( mRegisteredStreamsMutex )
	{
		IORecursiveLockLock (mRegisteredStreamsMutex);
	}
	
	oldStreamIndex = getStreamInfoIndex (interfaceNum);
	if (-1 != oldStreamIndex)
	{
		// This stream already has information stored. We should replace it.
		mRegisteredStreams->replaceObject (oldStreamIndex, streamInfo);
	}
	else
	{
		mRegisteredStreams->setObject (streamInfo);
	}
	
	if ( mRegisteredStreamsMutex )
	{
		IORecursiveLockUnlock (mRegisteredStreamsMutex);
	}
	
	streamInfo->release ();
	
	result = kIOReturnSuccess;
	
Exit:
	return result;
}

// <rdar://6420832>
SInt32 AppleUSBAudioDevice::getStreamInfoIndex (UInt8 interfaceNum) {
	OSDictionary *						streamInfo;
	OSNumber *							streamInterfaceNumber;
	UInt16								streamIndex;
	SInt32								returnIndex;

	returnIndex = -1;
	
	if ( mRegisteredStreamsMutex )
	{
		IORecursiveLockLock (mRegisteredStreamsMutex);
	}
	
	if (mRegisteredStreams) 
	{
		for (streamIndex = 0; streamIndex < mRegisteredStreams->getCount (); streamIndex++) 
		{
			streamInfo = OSDynamicCast (OSDictionary, mRegisteredStreams->getObject (streamIndex));
			if (streamInfo) 
			{
				streamInterfaceNumber = OSDynamicCast (OSNumber, streamInfo->getObject (kInterface));
				if ( ( NULL != streamInterfaceNumber ) && ( streamInterfaceNumber->unsigned8BitValue() == interfaceNum ) )
				{
					returnIndex = streamIndex;
					break;		// Get out of for loop with correct index
				}
			}
		}
	}
	
	if ( mRegisteredStreamsMutex )
	{
		IORecursiveLockUnlock (mRegisteredStreamsMutex);
	}

	return returnIndex;
}

IOReturn AppleUSBAudioDevice::doControlStuff (IOAudioEngine *audioEngine, UInt8 interfaceNum, UInt8 altSettingNum) {
	AppleUSBAudioEngine *				usbAudioEngine;
	IOAudioSelectorControl *			inputSelector;
	IOAudioSelectorControl *			outputSelector = NULL;				//	<rdar://6413207>
	OSArray *							arrayOfPathsFromOutputTerminal;
	OSArray *							aPath;
	OSArray *							playThroughPaths;
	OSNumber *							theUnitIDNum;
	IOReturn							result;
	UInt32								numUnitsInPath, unitIndexInPath, pathIndex;	//	<rdar://6523676>
	UInt32								inputTerminalIndex;		//	<rdar://6413207>
	UInt32								outputTerminalIndex;
	UInt32								numOutputTerminalArrays;
	UInt32								numPathsFromOutputTerminal;
	UInt32								pathsToOutputTerminalN;
	UInt32								selection;
	SInt32								engineIndex;
	SInt32								streamIndex;			//	<rdar://6420832>
	UInt16								terminalType;
	UInt8								numSelectorUnits;
	UInt8								subType;
	UInt8								numInputTerminals;		//	<rdar://6413207>
	UInt8								numOutputTerminals;
	UInt8								selectorUnitID;
	UInt8								featureUnitID;
	UInt8								volFeatureUnitID;		//	<rdar://5366067>
	UInt8								muteFeatureUnitID;		//	<rdar://5366067>
	SInt16								deviceMax;				//	<rdar://5366067>
	UInt8								controlInterfaceNum;
	UInt8								unitID;
	UInt8								outputTerminalID;
	UInt8								inputTerminalID;		//	<rdar://6430836>
	OSString *							nameString;				//	<rdar://6413207>
	Boolean								done;
	UInt8								numControls;			//	<rdar://5366067>
	UInt8								channelNum;				//	<rdar://5366067>
	UInt8								defaultSelectorSetting;	//	<rdar://6523676>
	// [rdar://5292769] Don't publish mute controls if the path is playthrough capable
	bool								playthroughCapable;
	UInt8								direction;
	// <rdar://problem/6656791>
	UInt32								arrayOfPathsFromOutputTerminalIndex;
	UInt32								numPathsFromOutputTerminals;
	Boolean								finished = FALSE;

	debugIOLog ("+ AppleUSBAudioDevice::doControlStuff(0x%x, %d, %d)", audioEngine, interfaceNum, altSettingNum);
	result = kIOReturnError;
	inputSelector = NULL;
	done = FALSE;

    usbAudioEngine = OSDynamicCast (AppleUSBAudioEngine, audioEngine);
    FailIf (NULL == usbAudioEngine, Exit);
	FailIf (NULL == mControlInterface, Exit);
	debugIOLog ("? AppleUSBAudioDevice::doControlStuff () - This usbAudioEngine = %p", usbAudioEngine);

	FailIf ( kIOReturnSuccess != registerEngineInfo ( usbAudioEngine ), Exit );					//	<rdar://6420832>
	FailIf ( kIOReturnSuccess != registerStreamInfo ( interfaceNum, altSettingNum ), Exit );	//	<rdar://6420832>

	engineIndex = getEngineInfoIndex (usbAudioEngine);											//	<rdar://6413207>
	streamIndex = getStreamInfoIndex (interfaceNum);											//	<rdar://6413207>
	FailIf ( ( -1 == engineIndex ) || ( -1 == streamIndex ), Exit );							//	<rdar://6413207>
	
	featureUnitID = 0;
	volFeatureUnitID = 0;			//	<rdar://5366067>
	muteFeatureUnitID = 0;			//	<rdar://5366067>
	selectorUnitID = 0;
	outputTerminalID = 0;
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	FailIf (kIOReturnSuccess != mConfigDictionary->getNumInputTerminals (&numInputTerminals, controlInterfaceNum, 0), Exit);	//	<rdar://6413207>
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumOutputTerminals (&numOutputTerminals, controlInterfaceNum, 0), Exit);
	FailIf (kIOReturnSuccess != mConfigDictionary->getIsocEndpointDirection (&direction, interfaceNum, altSettingNum), Exit);	

	if (direction == kIOAudioStreamDirectionOutput) 
	{
		UInt32		numConnectedOutputTerminals = 0;		//	<rdar://6413207>
		UInt8		defaultOutputTerminalID = 0;			//	<rdar://6413207>
		bool		hasInitializedOutputControls = false;	//	<rdar://6413207>
		
		//  //	<rdar://6413207>	Get the input terminal that is associated with this interface
		FailIf (kIOReturnSuccess != mConfigDictionary->getTerminalLink (&inputTerminalID, interfaceNum, altSettingNum), Exit);
		if ( 0 == inputTerminalID )
		{
			//  if not found, use the first USB streaming input terminal
			for (inputTerminalIndex = 0; inputTerminalIndex < numInputTerminals; inputTerminalIndex++) 
			{
				FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedInputTerminalType (&terminalType, controlInterfaceNum, 0, inputTerminalIndex), Exit);
				if (USB_STREAMING == terminalType) 
				{ 
					FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedInputTerminalID (&inputTerminalID, controlInterfaceNum, 0, inputTerminalIndex), Exit);
					break;		// Found the (hopefully only) streaming input terminal we're looking for
				}
			}
		}

		defaultOutputTerminalID = getDefaultOutputTerminalID ( inputTerminalID );			//	<rdar://6413207>
		numConnectedOutputTerminals = getNumConnectedOutputTerminals ( inputTerminalID );	//	<rdar://6413207>

		debugIOLog("? AppleUSBAudioDevice::doControlStuff () - numConnectedOutputTerminals = %d inputTerminalID: %d", numConnectedOutputTerminals, inputTerminalID);
		
		for (outputTerminalIndex = 0; outputTerminalIndex < numOutputTerminals && FALSE == done; outputTerminalIndex++) 
		{
			FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalType (&terminalType, controlInterfaceNum, 0, outputTerminalIndex), Exit);
			if (terminalType != USB_STREAMING) 
			{
				UInt8	inputUnitID, outputUnitID;	//	<rdar://6413207>
				
				FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalID (&outputTerminalID, controlInterfaceNum, 0, outputTerminalIndex), Exit);
				numOutputTerminalArrays = mControlGraph->getCount ();
				
				finished = FALSE;	// <rdar://problem/6656791>
								
				for (pathsToOutputTerminalN = 0; ( pathsToOutputTerminalN < numOutputTerminalArrays ) && !finished; pathsToOutputTerminalN++) 
				{
					arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminalN));
					FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
					
					// <rdar://problem/6656791> AppleUSBAudio: Output controls not set up properly for stream interfaces
					numPathsFromOutputTerminals = arrayOfPathsFromOutputTerminal->getCount ();
					
					for ( arrayOfPathsFromOutputTerminalIndex = 0; ( arrayOfPathsFromOutputTerminalIndex < numPathsFromOutputTerminals ) && !finished; arrayOfPathsFromOutputTerminalIndex++ )
					{
						aPath = OSDynamicCast ( OSArray, arrayOfPathsFromOutputTerminal->getObject ( arrayOfPathsFromOutputTerminalIndex ) );
						FailIf (NULL == aPath, Exit);
						theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
						FailIf (NULL == theUnitIDNum, Exit);
						outputUnitID = theUnitIDNum->unsigned8BitValue ();					//	<rdar://6413207>
						theUnitIDNum = OSDynamicCast (OSNumber, aPath->getLastObject ());
						FailIf (NULL == theUnitIDNum, Exit);
						inputUnitID = theUnitIDNum->unsigned8BitValue ();
			
						if ( ( inputUnitID == inputTerminalID ) && ( outputUnitID == outputTerminalID ) )	//	<rdar://6413207>
						{
							if ( !hasInitializedOutputControls )
							{
								if ( numConnectedOutputTerminals > 1 )	//	<rdar://6413207>, <rdar://6500500>
								{
									// Create a virtual selector for output path selection.
									outputSelector = IOAudioSelectorControl::createOutputSelector (defaultOutputTerminalID, kIOAudioControlChannelIDAll, 0, (streamIndex << 16) | ( engineIndex << 8) | 0 );	//	<rdar://6523676>
									FailIf (NULL == outputSelector, Exit);
									outputSelector->setValueChangeHandler (controlChangedHandler, this);
									usbAudioEngine->addDefaultAudioControl (outputSelector);
								}
								
								featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageOutput, interfaceNum, altSettingNum, kVolumeControl);
								if (featureUnitID) 
								{
									// Create the output gain controls
									volFeatureUnitID = featureUnitID;	//	<rdar://5366067>
									debugIOLog("? AppleUSBAudioDevice::doControlStuff () - Creating output gain controls");
									addVolumeControls (usbAudioEngine, volFeatureUnitID, outputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageOutput);	//	<rdar://6413207>
									featureUnitID = 0;
								}
								featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageOutput, interfaceNum, altSettingNum, kMuteControl);
								if (featureUnitID) 
								{
									debugIOLog("? AppleUSBAudioDevice::doControlStuff () - Creating output mute controls");
									muteFeatureUnitID = featureUnitID;	//	<rdar://5366067>
									addMuteControl (usbAudioEngine, muteFeatureUnitID, outputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageOutput);		//	<rdar://6413207>
									featureUnitID = 0;
									// If there is only 1 output terminal, then we are done.
									if ( numConnectedOutputTerminals <= 1 )
									{
										done = TRUE;
									}
								}
								//	<rdar://5366067>	Handle the case where the volume & mute controls are on different feature units.
								if ( volFeatureUnitID != muteFeatureUnitID )
								{
									// Unmute the volume feature unit.
									if ( volFeatureUnitID )
									{
										FailIf (kIOReturnSuccess != mConfigDictionary->getNumControls (&numControls, controlInterfaceNum, 0, volFeatureUnitID), Exit);
										for (channelNum = 0; channelNum < numControls; channelNum++) 
										{
											if ( mConfigDictionary->channelHasMuteControl ( controlInterfaceNum, 0, volFeatureUnitID, channelNum ) )
											{
												setCurMute (volFeatureUnitID, channelNum, 0);
											}
										}
									}
									// Set the gain of the mute feature unit.
									if ( muteFeatureUnitID )
									{
										FailIf (kIOReturnSuccess != mConfigDictionary->getNumControls (&numControls, controlInterfaceNum, 0, muteFeatureUnitID), Exit);
										for (channelNum = 0; channelNum < numControls; channelNum++) 
										{
											if ( mConfigDictionary->channelHasVolumeControl ( controlInterfaceNum, 0, muteFeatureUnitID, channelNum ) )
											{
												if ( kIOReturnSuccess == getMaxVolume (muteFeatureUnitID, channelNum, &deviceMax) )
												{
													setCurVolume ( muteFeatureUnitID, channelNum, (deviceMax >= 0) ? 0 : deviceMax );
												}
											}
										}
									}
								}
							usbAudioEngine->updateChannelNames ( aPath, interfaceNum, altSettingNum );	//	<rdar://6430836> <rdar://problem/6706026>

								hasInitializedOutputControls = true;
							}
												
							if ( NULL != outputSelector )	//	<rdar://6413207>
							{
								// <rdar://6394629>	Get the name from the terminal if it exists.
								nameString = getNameForTerminal ( outputTerminalID, kIOAudioStreamDirectionOutput );
								FailIf (NULL == nameString, Exit);
								if ( !outputSelector->valueExists ( outputTerminalID ) )
								{
									outputSelector->addAvailableSelection (outputTerminalID, nameString);
								}
								nameString->release ();
							}
							finished = TRUE;	// <rdar://problem/6656791>
						}  // if ( ( inputUnitID == inputTerminalID ) && ( outputUnitID == outputTerminalID ) )
					}  // for arrayOfPathsFromOutputTerminalIndex
				} // for pathsToOutputTerminalN
			} // if (unitID == outputTerminalID
		} // for outputTerminalIndex
	} 
	else 
	{		// direction == kIOAudioStreamDirectionInput
	   	//  <rdar://problem/6385557>
		//  Get the output terminal that is associated with this interface
		FailIf (kIOReturnSuccess != mConfigDictionary->getTerminalLink (&outputTerminalID, interfaceNum, altSettingNum), Exit);
		if ( outputTerminalID == 0 )
		{
			//  if not found, use the first USB streaming output terminal
			for (outputTerminalIndex = 0; outputTerminalIndex < numOutputTerminals; outputTerminalIndex++) 
			{
				FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalType (&terminalType, controlInterfaceNum, 0, outputTerminalIndex), Exit);
				if (0x101 == terminalType) 
				{ 
					FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalID (&outputTerminalID, controlInterfaceNum, 0, outputTerminalIndex), Exit);
					break;		// Found the (hopefully only) streaming output terminal we're looking for
				}
			}
		}

		numOutputTerminalArrays = mControlGraph->getCount ();
		finished = FALSE;	// <rdar://problem/6656791>
		
		for (pathsToOutputTerminalN = 0; ( pathsToOutputTerminalN < numOutputTerminalArrays ) && !finished; pathsToOutputTerminalN++) 
		{
			playthroughCapable = false;
			arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminalN));
			FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
			
			// <rdar://problem/6656791>
			numPathsFromOutputTerminals = arrayOfPathsFromOutputTerminal->getCount();
			
			for ( arrayOfPathsFromOutputTerminalIndex = 0; ( arrayOfPathsFromOutputTerminalIndex < numPathsFromOutputTerminals ) && !finished; arrayOfPathsFromOutputTerminalIndex++ )
			{
				aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject ( arrayOfPathsFromOutputTerminalIndex ) );
				FailIf (NULL == aPath, Exit);
				theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
				FailIf (NULL == theUnitIDNum, Exit);
				unitID = theUnitIDNum->unsigned8BitValue ();
				theUnitIDNum = OSDynamicCast (OSNumber, aPath->getLastObject ());	//	<rdar://6430836>
				FailIf (NULL == theUnitIDNum, Exit);
				inputTerminalID =theUnitIDNum->unsigned8BitValue ();
				
				if (unitID == outputTerminalID) 
				{
					numPathsFromOutputTerminal = arrayOfPathsFromOutputTerminal->getCount ();
					FailIf (kIOReturnSuccess != mConfigDictionary->getNumSelectorUnits(&numSelectorUnits, controlInterfaceNum, 0), Exit);
					if (numPathsFromOutputTerminal > 1 && numSelectorUnits) 
					{
						// Found the array of paths that lead to our streaming output terminal
						numUnitsInPath = aPath->getCount ();
						for (unitIndexInPath = 1; unitIndexInPath < numUnitsInPath; unitIndexInPath++) 
						{
							theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (unitIndexInPath));
							FailIf (NULL == theUnitIDNum, Exit);
							unitID = theUnitIDNum->unsigned8BitValue ();
							FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
							if (SELECTOR_UNIT == subType) 
							{							
								//	<rdar://6523676> Get the current selector setting
								defaultSelectorSetting = getSelectorSetting ( unitID );
								if ( 0 == defaultSelectorSetting )
								{
									debugIOLog ( "? AppleUSBAudioDevice::doControlStuff () - Unable to get the selector setting. Defaulting to 1" );
									defaultSelectorSetting = 1;
								}
								debugIOLog ( "? AppleUSBAudioDevice::doControlStuff () - Default selector setting: %d", defaultSelectorSetting );
								
								//	<rdar://6523676> Update the aPath and inputTerminalID if it is not first path.
								pathIndex = 0;
								if ( 1 != defaultSelectorSetting )
								{
									pathIndex = getPathIndexForSelectorSetting ( arrayOfPathsFromOutputTerminal, pathsToOutputTerminalN, arrayOfPathsFromOutputTerminalIndex, unitIndexInPath, defaultSelectorSetting );	// <rdar://problem/6656791>
									aPath = OSDynamicCast ( OSArray, arrayOfPathsFromOutputTerminal->getObject ( pathIndex ) );
									if ( NULL == aPath )	// Make sure that there is a path associated with the selector setting. The device may be returning an invalid value.
									{
										pathIndex = 0;
										defaultSelectorSetting = 1;
										aPath = OSDynamicCast ( OSArray, arrayOfPathsFromOutputTerminal->getObject ( pathIndex ) );
										FailIf ( NULL == aPath, Exit );
									}
									theUnitIDNum = OSDynamicCast ( OSNumber, aPath->getLastObject () );
									FailIf ( NULL == theUnitIDNum, Exit );
									inputTerminalID =theUnitIDNum->unsigned8BitValue ();
								}
								debugIOLog ( "? AppleUSBAudioDevice::doControlStuff () - Selected aPath = %p, pathIndex = %d, inputTerminalID = %d", aPath, pathIndex, inputTerminalID );
								
								if (kIOReturnSuccess == setSelectorSetting (unitID, defaultSelectorSetting))	//	<rdar://6523676>
								{
									selectorUnitID = unitID;
									engineIndex = getEngineInfoIndex (usbAudioEngine);
									streamIndex = getStreamInfoIndex (interfaceNum);		//	<rdar://6420832>
									if ( ( -1 != engineIndex ) && ( -1 != streamIndex ) ) 	//	<rdar://6420832>
									{
										selection = (0xFF000000 & (pathsToOutputTerminalN << 24)) | (0x00FF0000 & (pathIndex << 16)) | (0x0000FF00 & (selectorUnitID << 8)) | (0x000000FF & defaultSelectorSetting);	//	<rdar://6523676>
										inputSelector = IOAudioSelectorControl::createInputSelector (selection, kIOAudioControlChannelIDAll, 0, (streamIndex << 16) | ( engineIndex << 8 ) | selectorUnitID );	//	<rdar://6420832>, <rdar://6523676>
										FailIf (NULL == inputSelector, Exit);
										inputSelector->setValueChangeHandler (controlChangedHandler, this);
										usbAudioEngine->addDefaultAudioControl (inputSelector);
										//	<rdar://5366067> For the inputs, use the feature unit closest to the selector unit. The same feature unit is
										//	used for volume & mute. Look for volume controls first. If it is present, then check if it has mute.
										//	If volume controls are not present, then look for mute controls.
										featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageInput, interfaceNum, altSettingNum, kVolumeControl);
										if (featureUnitID) 
										{
											// Create the input gain controls
											debugIOLog("? AppleUSBAudioDevice::doControlStuff () - Creating input gain controls");
											addVolumeControls (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);		//	<rdar://6413207>
											
											debugIOLog("? AppleUSBAudioDevice::doControlStuff () - Creating input mute controls");
											addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);		//	<rdar://6413207>
											featureUnitID = 0;
										}
										else
										{
											featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageInput, interfaceNum, altSettingNum, kMuteControl);
											if (featureUnitID) 
											{
												debugIOLog("? AppleUSBAudioDevice::doControlStuff () - Creating input mute controls");
												addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);	//	<rdar://6413207>
												featureUnitID = 0;
											}
										}
									} // if ( ( -1 != engineIndex ) && ( -1 != streamIndex ) )	//	<rdar://6420832>
								} // if (kIOReturnSuccess == setSelectorSetting (unitID, 1))
								break;		// Get out of unitIndexInPath for loop
							} // if (SELECTOR_UNIT == subType)
						} // for unitIndexInPath

						if (NULL != inputSelector) 
						{
							addSelectorSourcesToSelectorControl (inputSelector, arrayOfPathsFromOutputTerminal, pathsToOutputTerminalN, arrayOfPathsFromOutputTerminalIndex, unitIndexInPath);	// <rdar://problem/6656791>
							inputSelector->release ();
						} 
						else 
						{
							// There are no programmable selectors, so just find the one feature unit, if it exists.
							featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageInput, interfaceNum, altSettingNum, kVolumeControl);
							if (featureUnitID) 
							{
								// Create the playthrough volume controls
								debugIOLog("? AppleUSBAudioDevice::doControlStuff () - Creating playthrough volume controls");
								addVolumeControls (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);	// <rdar://6413207>
								featureUnitID = 0;
							}
							//	<rdar://6523676> Add the mute controls
							featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageInput, interfaceNum, altSettingNum, kMuteControl);
							if (featureUnitID) 
							{
								// Create the mute controls
								debugIOLog ( "? AppleUSBAudioDevice::doControlStuff () - Creating input mute control (no selectors)" );
								addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);		//	<rdar://6413207>
								featureUnitID = 0;
							}
						}
					} // if (numPathsFromOutputTerminal > 1 && numSelectorUnits)
					else 
					{
						// There are no selectors, so just find the one feature unit, if it exists.
						featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageInput, interfaceNum, altSettingNum, kVolumeControl);
						if (featureUnitID) 
						{
							// Create the playthrough volume controls
							debugIOLog ( "? AppleUSBAudioDevice::doControlStuff () - Creating input volume control (no selectors)" );
							addVolumeControls (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);		// <rdar://6413207>
							featureUnitID = 0;
						}
						featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsageInput, interfaceNum, altSettingNum, kMuteControl);
						if (featureUnitID) 
						{
							// Create the mute controls
							debugIOLog ( "? AppleUSBAudioDevice::doControlStuff () - Creating input mute control (no selectors)" );
							addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsageInput);		//	<rdar://6413207>
							featureUnitID = 0;
						}
					}
					usbAudioEngine->updateChannelNames ( aPath, interfaceNum, altSettingNum );	//	<rdar://6430836> <rdar://problem/6706026>				

					//	<rdar://6523676> Moved the play thru control initialization after the input controls as the selected input terminal may
					//	not be the first one. The code in the input part will set the inputTerminalID to the selected value if necessary.
					// Check for a playthrough path that would require a playthrough control
					playThroughPaths = getPlaythroughPaths (inputTerminalID);	//	<rdar://5366067>
					if (playThroughPaths) 
					{
						// [rdar://5292769] Don't publish mute controls if the path is playthrough capable
						playthroughCapable = true;
						debugIOLog("? AppleUSBAudioDevice::doControlStuff () - performing playthrough setup");
						doPlaythroughSetup (usbAudioEngine, playThroughPaths, interfaceNum, altSettingNum, inputTerminalID);	//	<rdar://5366067>
						playThroughPaths->release ();
					}
					finished = TRUE;	// <rdar://problem/6656791>
				}	// end if (unitID == outputTerminalID) 
			}	// end for (arrayOfPathsFromOutputTerminal)
		}	// end for (pathsToOutputTerminalN)
	}	// end else (input direction)

	result = kIOReturnSuccess;

Exit:
	debugIOLog ("- AppleUSBAudioDevice::doControlStuff(0x%x, %d, %d)", audioEngine, interfaceNum, altSettingNum);
	return result;
}

IOReturn AppleUSBAudioDevice::doPlaythroughSetup (AppleUSBAudioEngine * usbAudioEngine, OSArray * playThroughPaths, UInt8 interfaceNum, UInt8 altSettingNum, UInt8 inputTerminalID) {	//	<rdar://5366067>
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	OSString *							nameString;
	IOAudioSelectorControl *			playThroughSelector;
	OSDictionary *						engineInfo;
	OSDictionary *						streamInfo;				//	<rdar://6420832>
	UInt32								numPlayThroughPaths;
	UInt32								pathIndex;
	UInt32								defaultPathIndex = 0;			//	<rdar://5366067>
	UInt8								defaultPathTerminalType = 0;	//	<rdar://5366067>
	SInt32								engineInfoIndex;
	SInt32								streamInfoIndex;		//	<rdar://6420832>
	UInt16								terminalType;
	UInt8								featureUnitID;
	UInt8								outputTerminalID;					//	<rdar://5366067>
	UInt8								controlInterfaceNum;
	char								stringBuffer[kStringBufferSize];	// <rdar://6394629>
	UInt8								stringIndex;						// <rdar://6394629>
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	engineInfoIndex = getEngineInfoIndex (usbAudioEngine);
	FailIf (-1 == engineInfoIndex, Exit);

	//	<rdar://6420832>
	streamInfoIndex = getStreamInfoIndex (interfaceNum);
	FailIf (-1 == streamInfoIndex, Exit);

	engineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineInfoIndex));
	FailIf (NULL == engineInfo, Exit);

	//	<rdar://6420832>
	streamInfo = OSDynamicCast (OSDictionary, mRegisteredStreams->getObject (streamInfoIndex));
	FailIf (NULL == streamInfo, Exit);
	streamInfo->setObject (kPassThruPathsArray, playThroughPaths);

	numPlayThroughPaths = playThroughPaths->getCount ();
	if (numPlayThroughPaths > 0)	//	<rdar://5366067>
	{
		//	<rdar://5366067> Find the default path index according to the following priority order: Speaker, Headphone, Line Out, ...
		for (pathIndex = 0; pathIndex < numPlayThroughPaths; pathIndex++) 
		{
			aPath = OSDynamicCast (OSArray, playThroughPaths->getObject (pathIndex));
			FailIf (NULL == aPath, Exit);
			theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
			FailIf (NULL == theUnitIDNum, Exit);
			outputTerminalID = theUnitIDNum->unsigned8BitValue ();
			FailIf (kIOReturnSuccess != mConfigDictionary->getOutputTerminalType (&terminalType, controlInterfaceNum, 0, outputTerminalID), Exit);
			
			switch ( defaultPathTerminalType )
			{
				case 0:
					defaultPathTerminalType = terminalType;
					defaultPathIndex = pathIndex;
					break;
				case 0x0301: // Speaker
					break;
				case 0x0302: // Headphones
					if ( 0x0301 == terminalType )
					{
						defaultPathTerminalType = terminalType;
						defaultPathIndex = pathIndex;
					}
					break;
				case 0x0603: // Line Out
					if ( ( 0x0301 == terminalType ) || ( 0x0302 == terminalType ) )
					{
						defaultPathTerminalType = terminalType;
						defaultPathIndex = pathIndex;
					}
					break;
			}
		}
		
		if ( numPlayThroughPaths > 1 )	//	<rdar://6500500>
		{
			// Create a virtual selector to manipulate the mutes on the feature units to toggle through playthrough sources.
			playThroughSelector = IOAudioSelectorControl::create (defaultPathIndex, kIOAudioControlChannelIDAll, 0, (inputTerminalID << 24) | (streamInfoIndex << 16) | ( engineInfoIndex << 8 ) | 0, kIOAudioSelectorControlSubTypeDestination, kIOAudioControlUsagePassThru);	//	<rdar://6420832>, <rdar://5366067>, <rdar://6523676>
			FailIf (NULL == playThroughSelector, Exit);
			playThroughSelector->setValueChangeHandler (controlChangedHandler, this);
			usbAudioEngine->addDefaultAudioControl (playThroughSelector);

			for (pathIndex = 0; pathIndex < numPlayThroughPaths; pathIndex++) 
			{
				aPath = OSDynamicCast (OSArray, playThroughPaths->getObject (pathIndex));
				FailIf (NULL == aPath, Exit);
				featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsagePassThru, interfaceNum, altSettingNum, kMuteControl);
				if (featureUnitID) 
				{
					theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));	//	<rdar://5366067>
					FailIf (NULL == theUnitIDNum, Exit);
					outputTerminalID = theUnitIDNum->unsigned8BitValue ();			//	<rdar://5366067>
					FailIf (kIOReturnSuccess != mConfigDictionary->getOutputTerminalType (&terminalType, controlInterfaceNum, 0, outputTerminalID), Exit);
					// <rdar://6394629>	Get the name from the terminal if it exists.
					stringIndex = 0;
					if ( ( kIOReturnSuccess == mConfigDictionary->getStringIndex ( &stringIndex, controlInterfaceNum, 0, outputTerminalID ) ) &&	//	<rdar://5366067>
						 ( 0 != stringIndex ) &&
						 ( kIOReturnSuccess == getStringDescriptor ( stringIndex, stringBuffer, kStringBufferSize ) ) )
					{						 
						nameString = OSString::withCString (stringBuffer);
					}
					else
					{
						nameString = OSString::withCString (TerminalTypeString (terminalType));
					}					
					FailIf (NULL == nameString, Exit);
					playThroughSelector->addAvailableSelection (pathIndex, nameString);
					nameString->release ();
				}
			}
			streamInfo->setObject (kPassThruSelectorControl, playThroughSelector);				//	<rdar://5366067>
		}
		aPath = OSDynamicCast (OSArray, playThroughPaths->getObject (defaultPathIndex));	//	<rdar://5366067>
		FailIf (NULL == aPath, Exit);
		featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsagePassThru, interfaceNum, altSettingNum, kVolumeControl);		//	<rdar://5366067>
		if (featureUnitID) 
		{
			// Create the playthrough volume controls
			addVolumeControls (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsagePassThru);	// <rdar://6413207>
		}
		featureUnitID = getBestFeatureUnitInPath (aPath, kIOAudioControlUsagePassThru, interfaceNum, altSettingNum, kMuteControl);			//	<rdar://5366067>
		if (featureUnitID) 
		{
			addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSettingNum, kIOAudioControlUsagePassThru);		// <rdar://6413207>
		}
		result = kIOReturnSuccess;
	} // if (numPlayThroughPaths > 0)
	else 
	{
		// No playthrough path.
		result = kIOReturnSuccess;
	}

Exit:
	return result;
}

//	<rdar://6523676> Get the path index corresponding to the selector setting
//  <rdar://problem/6656791>
UInt32 AppleUSBAudioDevice::getPathIndexForSelectorSetting (OSArray * arrayOfPathsFromOutputTerminal, UInt32 pathsToOutputTerminalN, UInt32 graphPathIndex, UInt8 selectorUnitIndex, UInt8 selectorSetting) {
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	OSString *							nameString;
	UInt32								selectorSourceIndex;
	UInt32								pathIndex;
	UInt8								numSelectorSources;
	UInt8								selectorID;
	UInt8								controlInterfaceNum;

	pathIndex = graphPathIndex;	//  <rdar://problem/6656791>

	FailIf (NULL == mControlInterface, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject ( graphPathIndex ));	//  <rdar://problem/6656791>
	FailIf (NULL == aPath, Exit);
	theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (selectorUnitIndex));
	FailIf (NULL == theUnitIDNum, Exit);
	selectorID = theUnitIDNum->unsigned8BitValue ();

	FailIf (kIOReturnSuccess != mConfigDictionary->getNumSources (&numSelectorSources, controlInterfaceNum, 0, selectorID), Exit);
	for (selectorSourceIndex = 0; selectorSourceIndex < numSelectorSources; selectorSourceIndex++) 
	{
		nameString = getNameForPath (arrayOfPathsFromOutputTerminal, &pathIndex, selectorUnitIndex + 1);
		if (NULL != nameString) 
		{
			nameString->release ();
		}
		if  ( ( selectorSourceIndex + 1 ) == selectorSetting )
		{
			// Found the path index that correspond to the selector setting
			pathIndex = pathIndex - 1;
			break;
		}
	}

Exit:
	return pathIndex;
}

// <rdar://problem/6656791>
IOReturn AppleUSBAudioDevice::addSelectorSourcesToSelectorControl (IOAudioSelectorControl * theSelectorControl, OSArray * arrayOfPathsFromOutputTerminal, UInt32 pathsToOutputTerminalN, UInt32 graphPathIndex, UInt8 selectorIndex) {
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	OSString *							nameString;
	UInt32								selectorSourceIndex;
	UInt32								pathIndex;
	UInt32								selection;
	UInt8								numSelectorSources;
	UInt8								selectorID;
	UInt8								controlInterfaceNum;

	FailIf (NULL == mControlInterface, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject ( graphPathIndex ));	// <rdar://problem/6656791>
	FailIf (NULL == aPath, Exit);
	theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (selectorIndex));
	FailIf (NULL == theUnitIDNum, Exit);
	selectorID = theUnitIDNum->unsigned8BitValue ();

	pathIndex = graphPathIndex;	// <rdar://problem/6656791>
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumSources (&numSelectorSources, controlInterfaceNum, 0, selectorID), Exit);
	for (selectorSourceIndex = 0; selectorSourceIndex < numSelectorSources; selectorSourceIndex++) 
	{
		nameString = getNameForPath (arrayOfPathsFromOutputTerminal, &pathIndex, selectorIndex + 1);
		if (NULL != nameString) 
		{
			selection = (0xFF000000 & (pathsToOutputTerminalN << 24)) | (0x00FF0000 & ((pathIndex - 1) << 16)) | (0x0000FF00 & (selectorID << 8)) | (0x000000FF & (selectorSourceIndex + 1));
			theSelectorControl->addAvailableSelection (selection, nameString);
			nameString->release ();
		}
	}

Exit:
	return kIOReturnSuccess;
}

//	<rdar://6413207>
UInt8 AppleUSBAudioDevice::getDefaultOutputTerminalID (UInt8 inputTerminalID) {
	OSArray *							arrayOfPathsFromOutputTerminal;
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	UInt8								controlInterfaceNum;
	UInt8								numOutputTerminals;
	UInt16								terminalType;
	UInt8								outputTerminalID;
	UInt32								outputTerminalIndex;
	UInt32								numOutputTerminalArrays;
	UInt32								pathsToOutputTerminalN;
	UInt8								defaultOutputTerminalID = 0;
	UInt8								defaultOutputTerminalType = 0;
	
	// <rdar://problem/6656791>
	UInt32								arrayOfPathsFromOutputTerminalIndex;
	UInt32								numPathsFromOutputTerminal;
	bool								finished = FALSE;
	

	FailIf (NULL == mControlInterface, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	FailIf (kIOReturnSuccess != mConfigDictionary->getNumOutputTerminals (&numOutputTerminals, controlInterfaceNum, 0), Exit);
	
	for (outputTerminalIndex = 0; outputTerminalIndex < numOutputTerminals; outputTerminalIndex++) 
	{
		FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalType (&terminalType, controlInterfaceNum, 0, outputTerminalIndex), Exit);
		if (terminalType != USB_STREAMING) 
		{
			UInt8	inputUnitID, outputUnitID;
			
			FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalID (&outputTerminalID, controlInterfaceNum, 0, outputTerminalIndex), Exit);
			numOutputTerminalArrays = mControlGraph->getCount ();
			
			finished = FALSE;	// <rdar://problem/6656791>
			
			for (pathsToOutputTerminalN = 0; ( pathsToOutputTerminalN < numOutputTerminalArrays ) && !finished; pathsToOutputTerminalN++) 
			{
				arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminalN));
				FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
				
				// <rdar://problem/6656791>
				numPathsFromOutputTerminal = arrayOfPathsFromOutputTerminal->getCount ();
				
				for ( arrayOfPathsFromOutputTerminalIndex = 0; ( arrayOfPathsFromOutputTerminalIndex < numPathsFromOutputTerminal ) && !finished; arrayOfPathsFromOutputTerminalIndex++ )
				{
					aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (0));
					FailIf (NULL == aPath, Exit);
					theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
					FailIf (NULL == theUnitIDNum, Exit);
					outputUnitID = theUnitIDNum->unsigned8BitValue ();
					theUnitIDNum = OSDynamicCast (OSNumber, aPath->getLastObject ());
					FailIf (NULL == theUnitIDNum, Exit);
					inputUnitID = theUnitIDNum->unsigned8BitValue ();
		
					if ( ( inputUnitID == inputTerminalID ) && ( outputUnitID == outputTerminalID ) )
					{
						// Select the default output terminal according to the following rule.
						// Speaker has priority over all the terminal types. Headphones has the priority over
						// all terminal types except Speaker. Line Out has the 3rd highest priority.
						switch ( defaultOutputTerminalType )
						{
							case 0:
								defaultOutputTerminalType = terminalType;
								defaultOutputTerminalID = outputTerminalID;
								break;
							case 0x0301: // Speaker
								break;
							case 0x0302: // Headphones
								if ( 0x0301 == terminalType )
								{
									defaultOutputTerminalType = terminalType;
									defaultOutputTerminalID = outputTerminalID;
								}
								break;
							case 0x0603: // Line Out
								if ( ( 0x0301 == terminalType ) || ( 0x0302 == terminalType ) )
								{
									defaultOutputTerminalType = terminalType;
									defaultOutputTerminalID = outputTerminalID;
								}
								break;
						}
						finished = TRUE;	// <rdar://problem/6656791>
					} // end if ( ( inputUnitID == inputTerminalID ) 
				} // for ( arrayOfPathsFromOutputTerminalIndex
			} // for pathsToOutputTerminalN
		} // if (unitID == outputTerminalID
	} // for outputTerminalIndex
	
Exit:
	return defaultOutputTerminalID;
}

//	<rdar://6413207>
UInt32 AppleUSBAudioDevice::getNumConnectedOutputTerminals (UInt8 inputTerminalID) {
	OSArray *							arrayOfPathsFromOutputTerminal;
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	UInt8								controlInterfaceNum;
	UInt8								numOutputTerminals;
	UInt16								terminalType;
	UInt8								outputTerminalID;
	UInt32								outputTerminalIndex;
	UInt32								numOutputTerminalArrays;
	UInt32								pathsToOutputTerminalN;
	UInt32								numConnectedOutputTerminals = 0;
	// <rdar://problem/6656791>
	UInt32								arrayOfPathsFromOutputTerminalIndex;
	Boolean								done;

	FailIf (NULL == mControlInterface, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	FailIf (kIOReturnSuccess != mConfigDictionary->getNumOutputTerminals (&numOutputTerminals, controlInterfaceNum, 0), Exit);
	
	for (outputTerminalIndex = 0; outputTerminalIndex < numOutputTerminals; outputTerminalIndex++) 
	{
		FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalType (&terminalType, controlInterfaceNum, 0, outputTerminalIndex), Exit);
		if (terminalType != USB_STREAMING) 
		{
			UInt8	inputUnitID, outputUnitID;
			
			// <rdar://problem/6656791>
			done = FALSE;
			
			FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalID (&outputTerminalID, controlInterfaceNum, 0, outputTerminalIndex), Exit);
			numOutputTerminalArrays = mControlGraph->getCount ();
			
			for (pathsToOutputTerminalN = 0; pathsToOutputTerminalN < numOutputTerminalArrays && !done; pathsToOutputTerminalN++) 
			{
				arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminalN));
				FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
				for ( arrayOfPathsFromOutputTerminalIndex = 0; arrayOfPathsFromOutputTerminalIndex < arrayOfPathsFromOutputTerminal->getCount () && FALSE == done; arrayOfPathsFromOutputTerminalIndex++ )
				{
					aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject ( arrayOfPathsFromOutputTerminalIndex ));
					FailIf (NULL == aPath, Exit);
					theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
					FailIf (NULL == theUnitIDNum, Exit);
					outputUnitID = theUnitIDNum->unsigned8BitValue ();
					theUnitIDNum = OSDynamicCast (OSNumber, aPath->getLastObject ());
					FailIf (NULL == theUnitIDNum, Exit);
					inputUnitID = theUnitIDNum->unsigned8BitValue ();
		
					if ( ( inputUnitID == inputTerminalID ) && ( outputUnitID == outputTerminalID ) )
					{
						numConnectedOutputTerminals++;
						done = TRUE;	// <rdar://problem/6656791>
					}
				}  // for ( arrayOfPathsFromOutputTerminalIndex
			} // for pathsToOutputTerminalN
		} // if (unitID == outputTerminalID
	} // for outputTerminalIndex
	
Exit:
	return numConnectedOutputTerminals;
}

//	<rdar://6430836>
OSString * AppleUSBAudioDevice::getNameForTerminal (UInt8 terminalID, UInt8 direction) {
	OSString *							theString = NULL;
	OSString *							tempString;
	char								stringBuffer[kStringBufferSize];	// <rdar://6394629>
	UInt8								stringIndex = 0;
	UInt8								controlInterfaceNum;
	UInt16								terminalType;

	FailIf (NULL == mControlInterface, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	//	<rdar://6413207> Get the terminal type based on the direction of the terminal.
	if ( kIOAudioStreamDirectionOutput == direction )
	{
		FailIf (kIOReturnSuccess != mConfigDictionary->getOutputTerminalType (&terminalType, controlInterfaceNum, 0, terminalID), Exit);
	}
	else
	{
		FailIf (kIOReturnSuccess != mConfigDictionary->getInputTerminalType (&terminalType, controlInterfaceNum, 0, terminalID), Exit);
	}
	
	if ( ( kIOReturnSuccess == mConfigDictionary->getStringIndex ( &stringIndex, controlInterfaceNum, 0, terminalID ) ) &&
		 ( 0 != stringIndex ) &&
		 ( kIOReturnSuccess == getStringDescriptor ( stringIndex, stringBuffer, kStringBufferSize ) ) )
	{						 
		debugIOLog ("? AppleUSBAudioDevice::getNameForTerminal () - terminalID = %d, stringIndex = %d, stringBuffer = %s", terminalID, stringIndex, stringBuffer);
		tempString = OSString::withCString (stringBuffer);
	}
	else
	{
		tempString = OSString::withCString (TerminalTypeString (terminalType));
	}					
	FailIf (NULL == tempString, Exit);
	theString = OSString::withString (tempString);
	tempString->release ();
	
Exit:
	return theString;
}

// Starting point is the array index of the element after the selector unit.
OSString * AppleUSBAudioDevice::getNameForPath (OSArray * arrayOfPathsFromOutputTerminal, UInt32 * pathIndex, UInt8 startingPoint) {
	OSString *							theString;
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	UInt32								numElementsInPath;
	UInt32								elementIndex;
	UInt16								terminalType;
	UInt8								unitID;
	UInt8								subType;
	UInt8								controlInterfaceNum;
	Boolean								done;

	done = FALSE;
	theString = NULL;
	FailIf (NULL == mControlInterface, Exit);

	aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (*pathIndex));
	FailIf (NULL == aPath, Exit);

	numElementsInPath = aPath->getCount ();
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
	for (elementIndex = startingPoint; elementIndex < numElementsInPath && FALSE == done; elementIndex++) 
	{
		theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (elementIndex));
		FailIf (NULL == theUnitIDNum, Exit);
		unitID = theUnitIDNum->unsigned8BitValue ();
		FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
		switch (subType) 
		{
			case INPUT_TERMINAL:
				FailIf (kIOReturnSuccess != mConfigDictionary->getInputTerminalType (&terminalType, controlInterfaceNum, 0, unitID), Exit);
				// <rdar://6394629> Get the string from the terminal if it exists.
				if ( USB_STREAMING != terminalType )
				{
					theString = getNameForTerminal ( unitID, kIOAudioStreamDirectionInput );	//	<rdar://6430836>, <rdar://6413207>
				}
				(*pathIndex)++;
				break;
			case MIXER_UNIT:
				theString = getNameForMixerPath (arrayOfPathsFromOutputTerminal, pathIndex, elementIndex);
				done = TRUE;
				break;
		}
	}

Exit:
	return theString;
}

// Starting point is the array index of the mixer unit.
OSString * AppleUSBAudioDevice::getNameForMixerPath (OSArray * arrayOfPathsFromOutputTerminal, UInt32 * pathIndex, UInt8 startingPoint) {
	char								string[255];
	OSString *							theString;
	OSString *							tempString;
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	UInt32								numElementsInPath;
	UInt32								mixerSourceIndex;
	UInt32								elementIndex;
	UInt8								numMixerSources;
	UInt8								unitID;
	UInt8								subType;
	UInt8								controlInterfaceNum;

	string[0] = 0;
	FailIf (NULL == mControlInterface, Exit);

	aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (*pathIndex));
	FailIf (NULL == aPath, Exit);
	theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (startingPoint));
	FailIf (NULL == theUnitIDNum, Exit);
	unitID = theUnitIDNum->unsigned8BitValue ();

	numElementsInPath = aPath->getCount ();
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumSources (&numMixerSources, controlInterfaceNum, 0, unitID), Exit);
	for (mixerSourceIndex = *pathIndex; mixerSourceIndex < *pathIndex + numMixerSources; /* mixerSourceIndex incremented elsewhere */) 
	{
		for (elementIndex = startingPoint + 1; elementIndex < numElementsInPath; elementIndex++) 
		{
			theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (elementIndex));
			FailIf (NULL == theUnitIDNum, Exit);
			unitID = theUnitIDNum->unsigned8BitValue ();
			FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
			switch (subType) 
			{
				case INPUT_TERMINAL:
					tempString = getNameForPath (arrayOfPathsFromOutputTerminal, &mixerSourceIndex, elementIndex);
					if (NULL != tempString) 
					{
						strncat (string, tempString->getCStringNoCopy (), 255);
						strncat (string, " & ", 255);
						tempString->release ();
					}
					break;
				case MIXER_UNIT:
					tempString = getNameForMixerPath (arrayOfPathsFromOutputTerminal, &mixerSourceIndex, elementIndex);
					if (NULL != tempString) 
					{
						strncat (string, tempString->getCStringNoCopy (), 255);
						tempString->release ();
					}
					break;
			}
		}
	}
	*pathIndex = mixerSourceIndex;

	if (strlen (string) > 3) 
	{
		string[strlen (string) - 3] = 0;
	}

Exit:
	theString = OSString::withCString (string);
	return theString;
}

void AppleUSBAudioDevice::addVolumeControls (AppleUSBAudioEngine * usbAudioEngine, UInt8 featureUnitID, UInt8 terminalID, UInt8 interfaceNum, UInt8 altSettingNum, UInt32 usage) {	// <rdar://6413207>
	OSArray *							inputGainControlsArray;
	OSArray *							passThruVolControlsArray;
	OSArray *							outputVolControlsArray;
	OSDictionary *						engineInfo;
	OSDictionary *						streamInfo;			//	<rdar://6420832>
	IOAudioLevelControl *				theLevelControl;
	IOFixed								deviceMinDB;
	IOFixed								deviceMaxDB;
	SInt32								engineInfoIndex;
	SInt32								streamInfoIndex;	//	<rdar://6420832>
	SInt16								deviceCur;
	SInt16								deviceMin;
	SInt16								deviceMax;
	UInt16								volRes;
	SInt32								controlCur;
	SInt32								controlMax;
	SInt32								controlMin;
	UInt8								channelNum;
	UInt8								controlInterfaceNum;
	UInt8								numControls;
	bool								extraStep = false;

	debugIOLog ("+ AppleUSBAudioDevice::addVolumeControls (0x%x, %d, %d, %d, %d, %u)", usbAudioEngine, featureUnitID, terminalID, interfaceNum, altSettingNum, usage);
	FailIf (NULL == mControlInterface, Exit);

	engineInfoIndex = getEngineInfoIndex (usbAudioEngine);
	FailIf (-1 == engineInfoIndex, Exit);

	//	<rdar://6420832>
	streamInfoIndex = getStreamInfoIndex (interfaceNum);
	FailIf (-1 == streamInfoIndex, Exit);

	engineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineInfoIndex));
	FailIf (NULL == engineInfo, Exit);

	//	<rdar://6420832>
	streamInfo = OSDynamicCast (OSDictionary, mRegisteredStreams->getObject (streamInfoIndex));
	FailIf (NULL == streamInfo, Exit);

	inputGainControlsArray = NULL;
	passThruVolControlsArray = NULL;
	outputVolControlsArray = NULL;
	
	// remove mono controls array if adding volume controls for output
	if (    (kIOAudioControlUsageOutput == usage)
	     && (NULL != mMonoControlsArray))
	{
		mMonoControlsArray->release ();
		mMonoControlsArray = NULL;
	}

	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumControls (&numControls, controlInterfaceNum, 0, featureUnitID), Exit);
	for (channelNum = 0; channelNum <= numControls; channelNum++) 
	{
		extraStep = false;
		if (mConfigDictionary->channelHasVolumeControl (controlInterfaceNum, 0, featureUnitID, channelNum))
		{
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Creating volume controls for channel %d", this, channelNum);
			FailIf (kIOReturnSuccess != getCurVolume (featureUnitID, channelNum, &deviceCur), Error);
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - deviceCur = 0x%04x", this, deviceCur);
			FailIf (kIOReturnSuccess != getMinVolume (featureUnitID, channelNum, &deviceMin), Error);
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - deviceMin = 0x%04x", this, deviceMin);
			FailIf (kIOReturnSuccess != getMaxVolume (featureUnitID, channelNum, &deviceMax), Error);
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - deviceMax = 0x%04x", this, deviceMax);
			getVolumeResolution (featureUnitID, channelNum, &volRes);
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - volRes = 0x%04x", this, volRes);
			// [rdar://4511427] Need to check volRes for class compliance (Audio Spec 5.2.2.4.3.2).
			FailIf (0 == volRes, Error);
			
			// [rdar://4228556] Unless the current volume is negative infinity, we should try to flush the volume out to the device.
			if ( ( SInt16 ) kNegativeInfinity != deviceCur )
			{
				// [rdar://5292769] If deviceCur lies outside the accepted range, flush out the deviceMin value.
				if (		( deviceCur < deviceMin )
						||	( deviceCur > deviceMax ) )
				{
					debugIOLog ( "! AppleUSBAudioDevice::addVolumeControls () - deviceCur is not in volume range! Setting to deviceMin ..." );
					deviceCur = deviceMin;
				}
				debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Attempting to set volume to current volume ...", this);
				FailIf (kIOReturnSuccess != setCurVolume (featureUnitID, channelNum, HostToUSBWord(deviceCur)), Error);
				debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Volume was set successfully.", this);
			}

			if ( (SInt16) kNegativeInfinity == deviceMin ) 
			{
				debugIOLog ( "! AppleUSBAudioDevice[%p]::addVolumeControls () - device violates USB 2.0 audio spec Section 5.2.5.7.2! Setting deviceMin to 0x8001 ..." );
				deviceMin = (SInt16) 0x8001;
				// Even though the firmware on the device is incorrect, we need to account for this loss of a control step by adding to the maximum later.
				extraStep = true;
				
			}
			deviceMinDB = ConvertUSBVolumeTodB (deviceMin);
			deviceMaxDB = ConvertUSBVolumeTodB (deviceMax);

			// [rdar://5118683] Create volume controls with correct ranges.
			
			controlMin = 0;
			// Also need to add an extra step here if the device reported negative infinity as the min value.
			controlMax = ( ( deviceMax - deviceMin ) / volRes ) + ( extraStep ? 1 : 0 );
			// The current control value is a bit of a special case because the device may violate the spec. The rules are as follows:
			// * If the current value is negative infinity, use a control value of 0.
			// * If the current value is the minimum value, use a control value of 0.
			// * If the current value is the maximum value, use the maximum control value (may be altered by an extra step if the device breaks spec).
			// * Otherwise calculate the current value as normal.
			if (		( (SInt16) kNegativeInfinity == deviceCur )
					||	( deviceCur == deviceMin ) )
			{
				controlCur = 0;
			}
			else if ( deviceCur == deviceMax )
			{
				controlCur = controlMax;
			}
			else
			{
				// If this device breaks spec, we don't care if this value matches with what the control published.
				controlCur = ( ( deviceCur - deviceMin ) / volRes );
			}
			
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Creating control... [%d : %d : %d]", this, controlMin, controlCur, controlMax);
			// <rdar://6413207> Add the terminal ID to the control ID so that it is unique.
			theLevelControl = IOAudioLevelControl::createVolumeControl (controlCur, controlMin, controlMax, deviceMinDB, deviceMaxDB, channelNum, 0, (terminalID << 8) | featureUnitID, usage);
			FailIf (NULL == theLevelControl, Exit);
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Created control %p", this, theLevelControl);
			theLevelControl->setValueChangeHandler (controlChangedHandler, this);
			usbAudioEngine->addDefaultAudioControl (theLevelControl);
			switch (usage) 
			{
				case kIOAudioControlUsageInput:
					if (NULL == inputGainControlsArray) 
					{
						inputGainControlsArray = OSArray::withObjects ((const OSObject **)&theLevelControl, 1);
					} 
					else 
					{
						inputGainControlsArray->setObject (theLevelControl);
					}
					break;
				case kIOAudioControlUsagePassThru:
					if (NULL == passThruVolControlsArray) 
					{
						passThruVolControlsArray = OSArray::withObjects ((const OSObject **)&theLevelControl, 1);
					} 
					else 
					{
						passThruVolControlsArray->setObject (theLevelControl);
					}
					break;
				case kIOAudioControlUsageOutput:
					if (NULL == outputVolControlsArray) 
					{
						outputVolControlsArray = OSArray::withObjects ((const OSObject **)&theLevelControl, 1);
					} 
					else 
					{
						outputVolControlsArray->setObject (theLevelControl);
					}
					
					// add channel number to mono output controls array if necessary
					if (mDeviceIsInMonoMode)
					{
						OSNumber *number = OSNumber::withNumber (channelNum, 8);
						if (NULL == mMonoControlsArray)
						{
							mMonoControlsArray = OSArray::withObjects ((const OSObject **) &number, 1);
						}
						else
						{
							mMonoControlsArray->setObject (number);
						}
						debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Added channel %d to mono controls array", this, channelNum);
						number->release ();
					}
					
					break;
				default:
					debugIOLog ("! AppleUSBAudioDevice[%p]::addVolumeControls () - Control %p has an unknown usage!", this, theLevelControl);
			}
			theLevelControl->release ();
		} 
		else 
		{
			debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Channel %d has no volume controls; skipping ...", this, channelNum);
		}
		goto NoError;
Error: 
		debugIOLog ("! AppleUSBAudioDevice[%p]::addVolumeControls () - Error creating controls for channel %d!", this, channelNum);
NoError:
		debugIOLog ("? AppleUSBAudioDevice[%p]::addVolumeControls () - Done with channel %d", this, channelNum);
	}

	if (NULL != inputGainControlsArray) 
	{
		streamInfo->setObject (kInputGainControls, inputGainControlsArray);		//	<rdar://6420832>
		inputGainControlsArray->release ();
	}
	if (NULL != passThruVolControlsArray) 
	{
		streamInfo->setObject (kPassThruVolControls, passThruVolControlsArray);	//	<rdar://6420832>
		passThruVolControlsArray->release ();
	}
	if (NULL != outputVolControlsArray) 
	{
		streamInfo->setObject (kOutputVolControls, outputVolControlsArray);		//	<rdar://6420832>
		outputVolControlsArray->release ();
	}

Exit:
	debugIOLog ("- AppleUSBAudioDevice::addVolumeControls (0x%x, %d, %d, %d, %d, %u)", usbAudioEngine, featureUnitID, terminalID, interfaceNum, altSettingNum, usage);
	return;
}

void AppleUSBAudioDevice::addMuteControl (AppleUSBAudioEngine * usbAudioEngine, UInt8 featureUnitID, UInt8 terminalID, UInt8 interfaceNum, UInt8 altSettingNum, UInt32 usage)	// <rdar://6413207>
{
	OSArray *							inputMuteControlsArray;
	OSArray *							outputMuteControlsArray;
	OSArray *							passThruToggleControlsArray;	//	<rdar://5366067>
	OSDictionary *						engineInfo;
	OSDictionary *						streamInfo;			//	<rdar://6420832>
	IOAudioToggleControl *				theMuteControl;
	SInt32								engineInfoIndex;
	SInt32								streamInfoIndex;	//	<rdar://6420832>
	SInt16								deviceCur;
	UInt8								channelNum;
	UInt8								controlInterfaceNum;
	UInt8								numControls;
	IOReturn							resultCode;

	debugIOLog ( "+ AppleUSBAudioDevice[%p]::addMuteControl ( %p, %d, %d, %d, %d, 0%x )", this, usbAudioEngine, featureUnitID, terminalID, interfaceNum, altSettingNum, usage );
	FailIf ( NULL == mControlInterface, Exit );

	engineInfoIndex = getEngineInfoIndex ( usbAudioEngine );
	FailIf ( -1 == engineInfoIndex, Exit );

	//	<rdar://6420832>
	streamInfoIndex = getStreamInfoIndex ( interfaceNum );
	FailIf ( -1 == streamInfoIndex, Exit );

	engineInfo = OSDynamicCast ( OSDictionary, mRegisteredEngines->getObject ( engineInfoIndex ) );
	FailIf ( NULL == engineInfo, Exit );

	//	<rdar://6420832>
	streamInfo = OSDynamicCast ( OSDictionary, mRegisteredStreams->getObject ( streamInfoIndex ) );
	FailIf ( NULL == streamInfo, Exit );

	inputMuteControlsArray = NULL;
	outputMuteControlsArray = NULL;		//	<rdar://6413207>
	passThruToggleControlsArray = NULL;	//	<rdar://5366067>

	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
	FailIf ( kIOReturnSuccess != mConfigDictionary->getNumControls ( &numControls, controlInterfaceNum, 0, featureUnitID ), Exit );
	for ( channelNum = 0; channelNum <= numControls; channelNum++ ) 
	{
		if ( mConfigDictionary->channelHasMuteControl ( controlInterfaceNum, 0, featureUnitID, channelNum ) )
		{
			resultCode = getCurMute ( featureUnitID, channelNum, &deviceCur );
			debugIOLog ( "? AppleUSBAudioDevice::addMuteControl () - channel %d, feature unit %d has current mute value 0x%x", channelNum, featureUnitID, deviceCur );
			// [rdar://5292769] Force unmute the device since the only easy way to unmute it afterwards is Audio MIDI Setup
			if (		( 0 != deviceCur )
					&&	( kIOAudioControlUsageInput == usage ) )
			{
				debugIOLog ("! AppleUSBAudioDevice::addMuteControl () - forcing channel %d of this device to unmute in hardware.", channelNum );
				deviceCur = 0x0;
			}
			// <rdar://6413207> Add the terminal ID to the control ID so that it is unique. 
			theMuteControl = IOAudioToggleControl::createMuteControl ( deviceCur, channelNum, 0, (terminalID << 8) | featureUnitID, usage );
			FailIf ( NULL == theMuteControl, Exit );
			setCurMute ( featureUnitID, channelNum, HostToUSBWord( deviceCur ) );
			theMuteControl->setValueChangeHandler ( controlChangedHandler, this );
			usbAudioEngine->addDefaultAudioControl ( theMuteControl );
			switch (usage) 
			{
				case kIOAudioControlUsageInput:
					if ( NULL == inputMuteControlsArray ) 
					{
						inputMuteControlsArray = OSArray::withObjects ( ( const OSObject ** ) &theMuteControl, 1 );
					} 
					else 
					{
						inputMuteControlsArray->setObject ( theMuteControl );
					}
					break;
				case kIOAudioControlUsagePassThru:
					//	<rdar://5366067>
					if ( NULL == passThruToggleControlsArray ) 
					{
						passThruToggleControlsArray = OSArray::withObjects ( ( const OSObject ** ) &theMuteControl, 1 );
					} 
					else 
					{
						passThruToggleControlsArray->setObject ( theMuteControl );
					}
					break;
				case kIOAudioControlUsageOutput:
					//	<rdar://6413207>
					if ( NULL == outputMuteControlsArray ) 
					{
						outputMuteControlsArray = OSArray::withObjects ( ( const OSObject ** ) &theMuteControl, 1 );
					} 
					else 
					{
						outputMuteControlsArray->setObject ( theMuteControl );
					}
					break;
			}
			theMuteControl->release ();
		}
	}

	if ( NULL != inputMuteControlsArray ) 
	{
		streamInfo->setObject ( kInputMuteControls, inputMuteControlsArray );	//	<rdar://6420832>
		inputMuteControlsArray->release ();
	}

	//	<rdar://5366067>
	if ( NULL != passThruToggleControlsArray ) 
	{
		streamInfo->setObject ( kPassThruToggleControls, passThruToggleControlsArray );	//	<rdar://6420832>
		passThruToggleControlsArray->release ();
	}

	//	<rdar://6413207>
	if ( NULL != outputMuteControlsArray ) 
	{
		streamInfo->setObject ( kOutputMuteControls, outputMuteControlsArray );	//	<rdar://6420832>
		outputMuteControlsArray->release ();
	}

Exit:
	debugIOLog ( "- AppleUSBAudioDevice[%p]::addMuteControl ( %p, %d, %d, %d, %d, 0x%x )", this, usbAudioEngine, featureUnitID, terminalID, interfaceNum, altSettingNum, usage );
	return;
}

// This is how the thing is defined in the USB Audio spec (section 5.2.2.4.3.2 for the curious).
// The volume setting of a device is described in 1/256 dB increments using a number that goes from
// a max of 0x7fff (127.9961 dB) down to 0x8001 (-127.9961 dB) using standard signed math, but 0x8000
// is actually negative infinity (not -128 dB), so I have to special case it.
IOFixed AppleUSBAudioDevice::ConvertUSBVolumeTodB (SInt16 volume) {
	IOFixed							dBVolumeFixed;

	if (volume == (SInt16)0x8000) 
	{
		dBVolumeFixed = ((SInt16)0x8000 * 256) << 8;	// really is negative infinity
	} 
	else 
	{
		dBVolumeFixed = volume * 256;
	}

	// debugIOLog ("volume = %d, dBVolumeFixed = 0x%x", volume, dBVolumeFixed);

	return dBVolumeFixed;
}

IOReturn AppleUSBAudioDevice::getFeatureUnitRange (UInt8 controlSelector, UInt8 unitID, UInt8 channelNumber, UInt8 requestType, SubRange16 * target) {
    IOReturn							result;
	IOUSBDevRequestDesc					devReq;
	struct {
	UInt16								wNumSubRanges;
	SubRange16							subRanges[1];
	}									theSetting;
	IOBufferMemoryDescriptor *			theSettingDesc = NULL;
	UInt8								length = sizeof( theSetting );

	result = kIOReturnError;
	// Initialize theSetting so that 
	theSetting.subRanges[0].wMIN = 0;
	theSetting.subRanges[0].wMAX = 0;
	theSetting.subRanges[0].wRES = 0;
	FailIf (NULL == target, Exit);
	FailIf (NULL == mControlInterface, Exit);

	theSettingDesc = IOBufferMemoryDescriptor::withOptions (kIODirectionIn, length);
	FailIf (NULL == theSettingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBIn, kUSBClass, kUSBInterface);
	devReq.bRequest = requestType;
    devReq.wValue = (controlSelector << 8) | channelNumber;
    devReq.wIndex = (0xFF00 & (unitID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = length;
    devReq.pData = theSettingDesc;

	result = deviceRequest (&devReq);
	FailIf (kIOReturnSuccess != result, Exit);
	memcpy (&theSetting, theSettingDesc->getBytesNoCopy (), length);
	
Exit:
	if (NULL != theSettingDesc) 
	{
		theSettingDesc->release ();
	}
	if (NULL != target) 
	{
		if ( USBToHostWord (theSetting.wNumSubRanges) > 0 )
		{
			target->wMIN = USBToHostWord (theSetting.subRanges[0].wMIN);
			target->wMAX = USBToHostWord (theSetting.subRanges[0].wMAX);
			target->wRES = USBToHostWord (theSetting.subRanges[0].wRES);
		}
	}
	return result;
}

IOReturn AppleUSBAudioDevice::getFeatureUnitSetting (UInt8 controlSelector, UInt8 unitID, UInt8 channelNumber, UInt8 requestType, SInt16 * target) {
    IOReturn							result;
	IOUSBDevRequestDesc					devReq;
	UInt16								theSetting;
	IOBufferMemoryDescriptor *			theSettingDesc = NULL;
	UInt8								length;

	result = kIOReturnError;
	// Initialize theSetting so that 
	theSetting = 0;
	FailIf (NULL == target, Exit);
	FailIf (NULL == mControlInterface, Exit);

	switch (controlSelector) 
	{
		case MUTE_CONTROL:
			length = 1;
			break;
		case VOLUME_CONTROL:
			length = 2;
			break;
		default:
			length = 0;
	}
	theSettingDesc = IOBufferMemoryDescriptor::withOptions (kIODirectionIn, length);
	FailIf (NULL == theSettingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBIn, kUSBClass, kUSBInterface);
	devReq.bRequest = requestType;
    devReq.wValue = (controlSelector << 8) | channelNumber;
    devReq.wIndex = (0xFF00 & (unitID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = length;
    devReq.pData = theSettingDesc;

	result = deviceRequest (&devReq);
	FailIf (kIOReturnSuccess != result, Exit);
	memcpy (&theSetting, theSettingDesc->getBytesNoCopy (), length);
	
Exit:
	if (NULL != theSettingDesc) 
	{
		theSettingDesc->release ();
	}
	if (NULL != target) 
	{
		*target = USBToHostWord (theSetting);
	}
	return result;
}

IOReturn AppleUSBAudioDevice::setFeatureUnitSetting (UInt8 controlSelector, UInt8 unitID, UInt8 channelNumber, UInt8 requestType, UInt16 newValue, UInt16 newValueLen) {
    IOUSBDevRequestDesc					devReq;
	IOBufferMemoryDescriptor *			theSettingDesc = NULL;
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	theSettingDesc = IOBufferMemoryDescriptor::withBytes (&newValue, newValueLen, kIODirectionOut);
	FailIf (NULL == theSettingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBOut, kUSBClass, kUSBInterface);
    devReq.bRequest = requestType;
    devReq.wValue = (controlSelector << 8) | channelNumber;
    devReq.wIndex = (0xFF00 & (unitID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = newValueLen;
    devReq.pData = theSettingDesc;

	FailIf ((TRUE == isInactive()), DeviceInactive);  	// In case we've been unplugged during sleep
	result = deviceRequest (&devReq);

Exit:
	if (NULL != theSettingDesc) 
	{
		theSettingDesc->release ();
	}
	return result;
	
DeviceInactive:
	debugIOLog("? AppleUSBAudioDevice::setFeatureUnitSetting () - ERROR attempt to send a device request to an inactive device");
	goto Exit;
}

IOReturn AppleUSBAudioDevice::getCurMute (UInt8 unitID, UInt8 channelNumber, SInt16 * target) {
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);
	
	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		result = getFeatureUnitSetting (MUTE_CONTROL, unitID, channelNumber, USBAUDIO_0200::CUR, target);
	}
	else
	{
		result = getFeatureUnitSetting (MUTE_CONTROL, unitID, channelNumber, GET_CUR, target);
	}
	
Exit:

	return result;
}

IOReturn AppleUSBAudioDevice::getCurVolume (UInt8 unitID, UInt8 channelNumber, SInt16 * target) {
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		result = getFeatureUnitSetting (VOLUME_CONTROL, unitID, channelNumber, USBAUDIO_0200::CUR, target);
	}
	else
	{
		result = getFeatureUnitSetting (VOLUME_CONTROL, unitID, channelNumber, GET_CUR, target);
	}
	
Exit:

	return result;
}

IOReturn AppleUSBAudioDevice::getMaxVolume (UInt8 unitID, UInt8 channelNumber, SInt16 * target) {
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);
	
	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		SubRange16 subRange;
		result = getFeatureUnitRange (VOLUME_CONTROL, unitID, channelNumber, USBAUDIO_0200::RANGE, &subRange);
		
		if ( target )
		{
			memcpy ( target, &subRange.wMAX, 2 );
		}
	}
	else
	{
		result = getFeatureUnitSetting (VOLUME_CONTROL, unitID, channelNumber, GET_MAX, target);
	}

Exit:
	
	return result;
}

IOReturn AppleUSBAudioDevice::getMinVolume (UInt8 unitID, UInt8 channelNumber, SInt16 * target) {
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		SubRange16 subRange;
		result = getFeatureUnitRange (VOLUME_CONTROL, unitID, channelNumber, USBAUDIO_0200::RANGE, &subRange);
		
		if ( target )
		{
			memcpy ( target, &subRange.wMIN, 2 );
		}
	}
	else
	{
		result = getFeatureUnitSetting (VOLUME_CONTROL, unitID, channelNumber, GET_MIN, target);
	}
	
Exit:
	
	return result;
}

IOReturn AppleUSBAudioDevice::getVolumeResolution (UInt8 unitID, UInt8 channelNumber, UInt16 * target) {
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		SubRange16 subRange;
		result = getFeatureUnitRange (VOLUME_CONTROL, unitID, channelNumber, USBAUDIO_0200::RANGE, &subRange);
		
		if ( target )
		{
			memcpy ( target, &subRange.wRES, 2 );
		}
	}
	else
	{
		result = getFeatureUnitSetting (VOLUME_CONTROL, unitID, channelNumber, GET_RES, (SInt16 *) target);
	}
	
Exit:
	
	return result;
}

IOReturn AppleUSBAudioDevice::setCurVolume (UInt8 unitID, UInt8 channelNumber, SInt16 volume) {
	return setFeatureUnitSetting (VOLUME_CONTROL, unitID, channelNumber, SET_CUR, volume, 2);
}

IOReturn AppleUSBAudioDevice::setCurMute (UInt8 unitID, UInt8 channelNumber, SInt16 mute) {
	return setFeatureUnitSetting (MUTE_CONTROL, unitID, channelNumber, SET_CUR, mute, 1);
}

IOReturn AppleUSBAudioDevice::controlChangedHandler (OSObject * target, IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn							result;
	AppleUSBAudioDevice *				self;

	result = kIOReturnError;

	self = OSDynamicCast (AppleUSBAudioDevice, target);
	FailIf (NULL == self, Exit);
	result = self->protectedControlChangedHandler (audioControl, oldValue, newValue);

Exit:
	return result;
}

IOReturn AppleUSBAudioDevice::protectedControlChangedHandler (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn							result;

	result = kIOReturnError;
    switch (audioControl->getType ()) 
	{
		case kIOAudioControlTypeLevel:
			result = doVolumeControlChange (audioControl, oldValue, newValue);
			break;
		case kIOAudioControlTypeToggle:
			result = doToggleControlChange (audioControl, oldValue, newValue);
			break;
		case kIOAudioControlTypeSelector:
			result = doSelectorControlChange (audioControl, oldValue, newValue);
			break;
	}

	return result;
}

IOReturn AppleUSBAudioDevice::doSelectorControlChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn							result;

	result = kIOReturnError;
	switch (audioControl->getUsage ()) 
	{
		case kIOAudioControlUsageInput:
			result = doInputSelectorChange (audioControl, oldValue, newValue);
			break;
		case kIOAudioControlUsageOutput:
			result = doOutputSelectorChange (audioControl, oldValue, newValue);	//	<rdar://6413207>
			break;
		case kIOAudioControlUsagePassThru:
			result = doPassThruSelectorChange (audioControl, oldValue, newValue);
			break;
	}

	return result;
}

IOReturn AppleUSBAudioDevice::doVolumeControlChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
	IOReturn							result;
	SInt16								newVolume;
	SInt16								deviceMin;
	SInt16								offset;
	UInt16								volRes;
	UInt8								unitID;
	UInt8								channelNum;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::doVolumeControlChange( %p, 0x%x, 0x%x )", this, audioControl, oldValue, newValue);
	unitID = audioControl->getControlID () & 0xFF;		// <rdar://6413207>
	channelNum = audioControl->getChannelID ();
	result = kIOReturnError;

	if (    (kIOAudioControlUsageInput == audioControl->getUsage())
	     || (FALSE == mDeviceIsInMonoMode))
	{
		getMinVolume (unitID, channelNum, &deviceMin);
		offset = -deviceMin;

		if (newValue < 0) 
		{
			newVolume = 0x8000;
		} 
		else 
		{
			getVolumeResolution (unitID, channelNum, &volRes);
			newVolume = (newValue * volRes) - offset;						//	<rdar://6377425>
		}

		debugIOLog ("? AppleUSBAudioDevice[%p]::doVolumeControlChange () - Setting channel %d (unit %d) volume to 0x%x", this, channelNum, unitID, newVolume);
		result = setCurVolume (unitID, channelNum, HostToUSBWord (newVolume));
	
	}
	else
	{	// mono output case
		UInt8 i;
		FailIf (NULL == mMonoControlsArray, Exit);
		debugIOLog ("? AppleUSBAudioDevice[%p]::doVolumeControlChange () - Performing mono volume control change", this);
		for (i = 0; i < mMonoControlsArray->getCount (); i++)
		{
			channelNum = ((OSNumber *) mMonoControlsArray->getObject(i))->unsigned8BitValue ();
			getMinVolume (unitID, channelNum, &deviceMin);
			offset = -deviceMin;
		
			if (newValue < 0) 
			{
				newVolume = 0x8000;
			} 
			else 
			{
				getVolumeResolution (unitID, channelNum, &volRes);
				newVolume = (newValue * volRes) - offset;						//	<rdar://6377425>
			}
		
			result = setCurVolume (unitID, channelNum, HostToUSBWord (newVolume));
			debugIOLog ("? AppleUSBAudioDevice[%p]::doVolumeControlChange () - Set volume for channel %d to 0x%x = %d", this, channelNum, newVolume, result);
		}
	}
	
Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::doVolumeControlChange( %p, 0x%x, 0x%x ) = 0x%x", this, audioControl, oldValue, newValue, result);
	return result;
}

IOReturn AppleUSBAudioDevice::doToggleControlChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
	IOReturn							result;
	UInt8								unitID;
	UInt8								channelNum;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::doToggleControlChange( %p, 0x%x, 0x%x )", this, audioControl, oldValue, newValue);

	unitID = audioControl->getControlID () & 0xFF;		// <rdar://6413207>
	channelNum = audioControl->getChannelID ();

	debugIOLog ("? AppleUSBAudioDevice[%p]::doToggleControlChange( %p, 0x%x, 0x%x ) - unitID = %d, channelNum = %d", this, audioControl, oldValue, newValue, unitID, channelNum);

	result = setCurMute (unitID, channelNum, HostToUSBWord (newValue));

	debugIOLog ("- AppleUSBAudioDevice[%p]::doToggleControlChange( %p, 0x%x, 0x%x ) = 0x%x", this, audioControl, oldValue, newValue, kIOReturnSuccess);

	return result;
}

IOReturn AppleUSBAudioDevice::doPassThruSelectorChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
	AppleUSBAudioEngine *				usbAudioEngine;
	OSArray *							playThroughPaths;
	OSArray *							passThruVolControlsArray;
	OSArray *							passThruToggleControlsArray;	//	<rdar://5366067>
	OSArray *							thePath;
	OSNumber *							number;
	OSDictionary *						engineInfo;
	OSDictionary *						streamInfo;				//	<rdar://6420832>
	UInt32								i;
	UInt32								numPassThruVolControls;		//	<rdar://5366067>
	UInt32								numPassThruToggleControls;	//	<rdar://5366067>
	UInt8								interfaceNum;
	UInt8								altSetting;
	UInt8								featureUnitID;
	UInt8								inputTerminalID;		//	<rdar://6413207>
	UInt8								pathIndex;
	SInt32								engineInfoIndex;		//	<rdar://6420832>
	SInt32								streamInfoIndex;		//	<rdar://6420832>

	debugIOLog ("+ AppleUSBAudioDevice[%p]::doPassThruSelectorChange( %p, 0x%x, 0x%x )", this, audioControl, oldValue, newValue);

	if ( oldValue != newValue )		//	<rdar://5366067>
	{
		pathIndex = (newValue & 0x000000FF);
		
		debugIOLog ("? AppleUSBAudioDevice[%p]::doPassThruSelectorChange( %p, 0x%x, 0x%x ) - controlID = 0x%x", this, audioControl, oldValue, newValue, audioControl->getControlID ());

		engineInfoIndex = ( audioControl->getControlID () >> 8 ) & 0xFF;	//	<rdar://6420832>, <rdar://6523676>
		streamInfoIndex = ( audioControl->getControlID () >> 16 ) & 0xFF;	//	<rdar://6420832>

		engineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineInfoIndex));	//	<rdar://6420832>
		FailIf (NULL == engineInfo, Exit);
		usbAudioEngine = OSDynamicCast (AppleUSBAudioEngine, engineInfo->getObject (kEngine));
		FailIf (NULL == usbAudioEngine, Exit);
		
		streamInfo = OSDynamicCast (OSDictionary, mRegisteredStreams->getObject (streamInfoIndex));	//	<rdar://6420832>
		FailIf (NULL == streamInfo, Exit);
		number = OSDynamicCast (OSNumber, streamInfo->getObject (kInterface));						//	<rdar://6420832>
		FailIf (NULL == number, Exit);
		interfaceNum = number->unsigned8BitValue ();
		number = OSDynamicCast (OSNumber, streamInfo->getObject (kAltSetting));						//	<rdar://6420832>
		FailIf (NULL == number, Exit);
		altSetting = number->unsigned8BitValue ();
		passThruVolControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kPassThruVolControls));	//	<rdar://6420832>
		passThruToggleControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kPassThruToggleControls));	//	<rdar://5366067>

		usbAudioEngine->pauseAudioEngine ();
		usbAudioEngine->beginConfigurationChange ();

		if ( NULL != passThruVolControlsArray )
		{
			numPassThruVolControls = passThruVolControlsArray->getCount ();	//	<rdar://5366067>
			for (i = 0; i < numPassThruVolControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)passThruVolControlsArray->getObject (i));
			}
			passThruVolControlsArray->flushCollection ();
			streamInfo->removeObject (kPassThruVolControls);
		}
		
		//	<rdar://5366067>
		if ( NULL != passThruToggleControlsArray )
		{
			numPassThruToggleControls = passThruToggleControlsArray->getCount ();
			for (i = 0; i < numPassThruToggleControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)passThruToggleControlsArray->getObject (i));
			}
			passThruToggleControlsArray->flushCollection ();
			streamInfo->removeObject (kPassThruToggleControls);
		}

		//	<rdar://5366067>
		playThroughPaths = OSDynamicCast (OSArray, streamInfo->getObject (kPassThruPathsArray));	//	<rdar://6420832>
		FailIf (NULL == playThroughPaths, Exit);
		thePath = OSDynamicCast (OSArray, playThroughPaths->getObject (pathIndex));
		FailIf (NULL == thePath, Exit);
		number = OSDynamicCast (OSNumber, thePath->getLastObject());	//	<rdar://6413207>
		FailIf (NULL == number, Exit);
		inputTerminalID = number->unsigned8BitValue ();	
		featureUnitID = getBestFeatureUnitInPath (thePath, kIOAudioControlUsagePassThru, interfaceNum, altSetting, kVolumeControl);
		if ( 0 != featureUnitID )
		{
			addVolumeControls (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSetting, kIOAudioControlUsagePassThru);		//	<rdar://6413207>
		}
		featureUnitID = getBestFeatureUnitInPath (thePath, kIOAudioControlUsagePassThru, interfaceNum, altSetting, kMuteControl);
		if ( 0 != featureUnitID ) 
		{
			addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSetting, kIOAudioControlUsagePassThru);		//	<rdar://6413207>
		}
		usbAudioEngine->completeConfigurationChange ();
		usbAudioEngine->resumeAudioEngine ();
	}
	
Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::doPassThruSelectorChange( %p, 0x%x, 0x%x ) = 0x%x", this, audioControl, oldValue, newValue, kIOReturnSuccess);
	return kIOReturnSuccess;
}

IOReturn AppleUSBAudioDevice::doInputSelectorChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
	AppleUSBAudioEngine *				usbAudioEngine;
	IOAudioSelectorControl *			playThroughSelector;				//	<rdar://5366067>
	OSArray *							playThroughPaths;					//	<rdar://5366067>
	OSArray *							passThruVolControlsArray;			//	<rdar://5366067>
	OSArray *							passThruToggleControlsArray;		//	<rdar://5366067>
	OSArray *							inputGainControlsArray;
	OSArray *							inputMuteControlsArray;				//	<rdar://5366067>
	OSArray *							arrayOfPathsFromOutputTerminal;
	OSArray *							thePath;
	OSNumber *							number;
	OSDictionary *						engineInfo;
	OSDictionary *						streamInfo;	//	<rdar://6420832>
    IOReturn							result = kIOReturnError;
	UInt32								i;
	UInt32								numPassThruVolControls;				//	<rdar://5366067>
	UInt32								numPassThruToggleControls;			//	<rdar://5366067>
	UInt32								numGainControls;
	UInt32								numMuteControls;					//	<rdar://5366067>
	UInt8								interfaceNum;
	UInt8								altSetting;
	UInt8								featureUnitID;
	UInt8								selectorUnitID;
	UInt8								selectorPosition;
	UInt8								pathsToOutputTerminal;
	UInt8								pathIndex;
	UInt8								inputTerminalID;			//	<rdar://6430836>
	SInt32								engineInfoIndex;			//	<rdar://6420832>
	SInt32								streamInfoIndex;			//	<rdar://6420832>
	UInt32								unitIndex;					//	<rdar://5366067>
	Boolean								foundMixerUnit = false;		//	<rdar://5366067>
	UInt8								controlInterfaceNum;		//	<rdar://5366067>
	UInt8								subType;					//	<rdar://5366067>

	debugIOLog ("+ AppleUSBAudioDevice[%p]::doInputSelectorChange( %p, 0x%x, 0x%x )", this, audioControl, oldValue, newValue);

	if ( oldValue != newValue )	//	<rdar://5366067>
	{
		//	<rdar://5366067>
		FailIf (NULL == mControlInterface, Exit);
		controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

		pathsToOutputTerminal = (newValue & 0xFF000000) >> 24;
		pathIndex = (newValue & 0x00FF0000) >> 16;
		selectorUnitID = (newValue & 0x0000FF00) >> 8;
		selectorPosition = newValue & 0x000000FF;
		result = setSelectorSetting (selectorUnitID, selectorPosition);
		FailIf (kIOReturnSuccess != result, Exit);

		engineInfoIndex = ( audioControl->getControlID () >> 8 ) & 0xFF;	//	<rdar://6420832>, <rdar://6523676>
		streamInfoIndex = ( audioControl->getControlID () >> 16 ) & 0xFF;	//	<rdar://6420832>

		engineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineInfoIndex));	//	<rdar://6420832>
		FailIf (NULL == engineInfo, Exit);
		usbAudioEngine = OSDynamicCast (AppleUSBAudioEngine, engineInfo->getObject (kEngine));
		FailIf (NULL == usbAudioEngine, Exit);
		
		streamInfo = OSDynamicCast (OSDictionary, mRegisteredStreams->getObject (streamInfoIndex));	//	<rdar://6420832>
		FailIf (NULL == streamInfo, Exit);
		number = OSDynamicCast (OSNumber, streamInfo->getObject (kInterface));						//	<rdar://6420832>
		FailIf (NULL == number, Exit);
		interfaceNum = number->unsigned8BitValue ();
		number = OSDynamicCast (OSNumber, streamInfo->getObject (kAltSetting));						//	<rdar://6420832>
		FailIf (NULL == number, Exit);
		altSetting = number->unsigned8BitValue ();
		inputGainControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kInputGainControls));					//	<rdar://6420832>
		inputMuteControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kInputMuteControls));					//	<rdar://5366067>
		passThruVolControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kPassThruVolControls));				//	<rdar://6420832>
		passThruToggleControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kPassThruToggleControls));			//	<rdar://5366067>
		playThroughSelector = OSDynamicCast (IOAudioSelectorControl, streamInfo->getObject (kPassThruSelectorControl));	//	<rdar://5366067>
		
		usbAudioEngine->pauseAudioEngine ();
		usbAudioEngine->beginConfigurationChange ();

		if ( NULL != inputGainControlsArray )
		{
			//	<rdar://5366067>
			numGainControls = inputGainControlsArray->getCount ();
			for (i = 0; i < numGainControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)inputGainControlsArray->getObject (i));
			}
			inputGainControlsArray->flushCollection ();
			streamInfo->removeObject (kInputGainControls);
		}

		//	<rdar://5366067>
		if ( NULL != inputMuteControlsArray )
		{
			numMuteControls = inputMuteControlsArray->getCount ();
			for (i = 0; i < numMuteControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)inputMuteControlsArray->getObject (i));
			}
			inputMuteControlsArray->flushCollection ();
			streamInfo->removeObject (kInputMuteControls);
		}

		//	<rdar://5366067>
		if ( NULL != passThruVolControlsArray )
		{
			numPassThruVolControls = passThruVolControlsArray->getCount ();
			for (i = 0; i < numPassThruVolControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)passThruVolControlsArray->getObject (i));
			}
			passThruVolControlsArray->flushCollection ();
			streamInfo->removeObject (kPassThruVolControls);
		}
		
		//	<rdar://5366067>
		if ( NULL != passThruToggleControlsArray )
		{
			numPassThruToggleControls = passThruToggleControlsArray->getCount ();
			for (i = 0; i < numPassThruToggleControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)passThruToggleControlsArray->getObject (i));
			}
			passThruToggleControlsArray->flushCollection ();
			streamInfo->removeObject (kPassThruToggleControls);
		}
		
		//	<rdar://5366067>
		if ( NULL != playThroughSelector )
		{
			usbAudioEngine->removeDefaultAudioControl (playThroughSelector);			
			streamInfo->removeObject (kPassThruSelectorControl);
		}

		arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminal));
		FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
		thePath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (pathIndex));
		FailIf (NULL == thePath, Exit);
		number = OSDynamicCast (OSNumber, thePath->getLastObject());	//	<rdar://6430836>
		FailIf (NULL == number, Exit);
		inputTerminalID = number->unsigned8BitValue ();	
		featureUnitID = getBestFeatureUnitInPath (thePath, kIOAudioControlUsageInput, interfaceNum, altSetting, kVolumeControl);
		if ( 0 != featureUnitID )	//	<rdar://5366067>
		{
			addVolumeControls (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSetting, kIOAudioControlUsageInput);	//	<rdar://6413207>
			addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSetting, kIOAudioControlUsageInput);		//	<rdar://6413207>
		}
		else
		{
			featureUnitID = getBestFeatureUnitInPath (thePath, kIOAudioControlUsageInput, interfaceNum, altSetting, kMuteControl);
			if ( 0 != featureUnitID ) 
			{
				addMuteControl (usbAudioEngine, featureUnitID, inputTerminalID, interfaceNum, altSetting, kIOAudioControlUsageInput);	//	<rdar://6413207>
			}
		}
		//	<rdar://5366067> If the input path has a mixer in it, then don't create playthru path.
		foundMixerUnit = false;
		for (unitIndex = thePath->getCount() - 2; unitIndex > 0; unitIndex--) 
		{
			number = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
			if ( ( NULL != number ) && ( kIOReturnSuccess == mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, number->unsigned8BitValue ()) ) )
			{
				if	(MIXER_UNIT == subType)
				{
					foundMixerUnit = true;
					break;
				}
			}
		}
		if ( !foundMixerUnit )
		{
			playThroughPaths = getPlaythroughPaths (inputTerminalID);
			if (playThroughPaths) 
			{
				debugIOLog("? AppleUSBAudioDevice::doInputSelectorChange () - performing playthrough setup");
				doPlaythroughSetup (usbAudioEngine, playThroughPaths, interfaceNum, altSetting, inputTerminalID);
				playThroughPaths->release ();
			}
		}
		usbAudioEngine->updateChannelNames ( thePath, interfaceNum, altSetting );	//	<rdar://6430836> <rdar://problem/6706026>
		usbAudioEngine->completeConfigurationChange ();
		usbAudioEngine->resumeAudioEngine ();
	}
	else
	{
		result = kIOReturnSuccess;
	}
	
Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::doInputSelectorChange( %p, 0x%x, 0x%x ) = 0x%x", this, audioControl, oldValue, newValue, result);
	return result;
}

//	<rdar://6413207>
IOReturn AppleUSBAudioDevice::doOutputSelectorChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue) {
	AppleUSBAudioEngine *				usbAudioEngine;
	OSArray *							arrayOfPathsFromOutputTerminal;
	UInt32								numOutputTerminalArrays;
	UInt32								pathsToOutputTerminalN;
	OSArray *							outputVolControlsArray;
	OSArray *							outputMuteControlsArray;
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	OSArray *							thePath = NULL;
	OSNumber *							number;
	OSDictionary *						engineInfo;
	OSDictionary *						streamInfo;				//	<rdar://6420832>
	UInt32								i;
	UInt32								numOutputVolControls;
	UInt32								numOutputMuteControls;
	UInt8								interfaceNum;
	UInt8								altSetting;
	UInt8								volFeatureUnitID;		//	<rdar://5366067>
	UInt8								muteFeatureUnitID;		//	<rdar://5366067>
	UInt8								outputUnitID;
	UInt8								selectedOutputTerminalID;
	UInt8								inputTerminalID;
	SInt32								engineInfoIndex;		//	<rdar://6420832>
	SInt32								streamInfoIndex;		//	<rdar://6420832>
	UInt8								controlInterfaceNum;	//	<rdar://5366067>
	UInt8								numControls;			//	<rdar://5366067>
	UInt8								channelNum;				//	<rdar://5366067>
	SInt16								deviceMax;				//	<rdar://5366067>

	debugIOLog ("+ AppleUSBAudioDevice[%p]::doOutputSelectorChange( %p, 0x%x, 0x%x )", this, audioControl, oldValue, newValue);

	if ( oldValue != newValue )
	{
		//	<rdar://5366067>
		FailIf (NULL == mControlInterface, Exit);
		controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
		
		selectedOutputTerminalID = (newValue & 0x000000FF);
		
		engineInfoIndex = ( audioControl->getControlID () >> 8 ) & 0xFF;	//	<rdar://6420832>, <rdar://6523676>
		streamInfoIndex = ( audioControl->getControlID () >> 16 ) & 0xFF;	//	<rdar://6420832>

		engineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineInfoIndex));	//	<rdar://6420832>
		FailIf (NULL == engineInfo, Exit);
		usbAudioEngine = OSDynamicCast (AppleUSBAudioEngine, engineInfo->getObject (kEngine));
		FailIf (NULL == usbAudioEngine, Exit);
		
		streamInfo = OSDynamicCast (OSDictionary, mRegisteredStreams->getObject (streamInfoIndex));	//	<rdar://6420832>
		FailIf (NULL == streamInfo, Exit);
		number = OSDynamicCast (OSNumber, streamInfo->getObject (kInterface));						//	<rdar://6420832>
		FailIf (NULL == number, Exit);
		interfaceNum = number->unsigned8BitValue ();
		number = OSDynamicCast (OSNumber, streamInfo->getObject (kAltSetting));						//	<rdar://6420832>
		FailIf (NULL == number, Exit);
		altSetting = number->unsigned8BitValue ();
		outputVolControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kOutputVolControls));	//	<rdar://6420832>
		outputMuteControlsArray = OSDynamicCast (OSArray, streamInfo->getObject (kOutputMuteControls));

		usbAudioEngine->pauseAudioEngine ();
		usbAudioEngine->beginConfigurationChange ();

		if ( NULL != outputVolControlsArray )
		{
			numOutputVolControls = outputVolControlsArray->getCount ();
			for (i = 0; i < numOutputVolControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)outputVolControlsArray->getObject (i));
			}
			outputVolControlsArray->flushCollection ();
			streamInfo->removeObject (kOutputVolControls);
		}

		if ( NULL != outputMuteControlsArray )
		{
			numOutputMuteControls = outputMuteControlsArray->getCount ();
			for (i = 0; i < numOutputMuteControls; i++) 
			{
				usbAudioEngine->removeDefaultAudioControl ((IOAudioLevelControl *)outputMuteControlsArray->getObject (i));
			}
			outputMuteControlsArray->flushCollection ();
			streamInfo->removeObject (kOutputMuteControls);
		}

		numOutputTerminalArrays = mControlGraph->getCount ();
		for (pathsToOutputTerminalN = 0; pathsToOutputTerminalN < numOutputTerminalArrays; pathsToOutputTerminalN++) 
		{
			arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminalN));
			FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
			aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (0));
			FailIf (NULL == aPath, Exit);
			theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
			FailIf (NULL == theUnitIDNum, Exit);
			outputUnitID = theUnitIDNum->unsigned8BitValue ();

			if ( outputUnitID == selectedOutputTerminalID )
			{
				thePath = aPath;
				break;
			}
		}
		FailIf (NULL == thePath, Exit);
		number = OSDynamicCast (OSNumber, thePath->getLastObject());	//	<rdar://6430836>
		FailIf (NULL == number, Exit);
		inputTerminalID = number->unsigned8BitValue ();	
		volFeatureUnitID = getBestFeatureUnitInPath (thePath, kIOAudioControlUsageOutput, interfaceNum, altSetting, kVolumeControl);
		if ( 0 != volFeatureUnitID )
		{
			addVolumeControls (usbAudioEngine, volFeatureUnitID, selectedOutputTerminalID, interfaceNum, altSetting, kIOAudioControlUsageOutput);	//	<rdar://6413207>
		}
		muteFeatureUnitID = getBestFeatureUnitInPath (thePath, kIOAudioControlUsageOutput, interfaceNum, altSetting, kMuteControl);
		if ( 0 != muteFeatureUnitID ) 
		{
			addMuteControl (usbAudioEngine, muteFeatureUnitID, selectedOutputTerminalID, interfaceNum, altSetting, kIOAudioControlUsageOutput);		//	<rdar://6413207>
		}
		//	<rdar://5366067> Handle the case where the volume & mute controls are on different feature units.
		if ( volFeatureUnitID != muteFeatureUnitID )
		{
			// Unmute the volume feature unit.
			if ( volFeatureUnitID )
			{
				FailIf (kIOReturnSuccess != mConfigDictionary->getNumControls (&numControls, controlInterfaceNum, 0, volFeatureUnitID), Exit);
				for (channelNum = 0; channelNum < numControls; channelNum++) 
				{
					if ( mConfigDictionary->channelHasMuteControl ( controlInterfaceNum, 0, volFeatureUnitID, channelNum ) )
					{
						setCurMute (volFeatureUnitID, channelNum, 0);
					}
				}
			}
			// Set the gain of the mute feature unit.
			if ( muteFeatureUnitID )
			{
				FailIf (kIOReturnSuccess != mConfigDictionary->getNumControls (&numControls, controlInterfaceNum, 0, muteFeatureUnitID), Exit);
				for (channelNum = 0; channelNum < numControls; channelNum++) 
				{
					if ( mConfigDictionary->channelHasVolumeControl ( controlInterfaceNum, 0, muteFeatureUnitID, channelNum ) )
					{
						if ( kIOReturnSuccess == getMaxVolume (muteFeatureUnitID, channelNum, &deviceMax) )
						{
							setCurVolume ( muteFeatureUnitID, channelNum, (deviceMax >= 0) ? 0 : deviceMax );
						}
					}
				}
			}
		}
		usbAudioEngine->updateChannelNames ( thePath, interfaceNum, altSetting );	//	<rdar://6430836> <rdar://problem/6706026>
		usbAudioEngine->completeConfigurationChange ();
		usbAudioEngine->resumeAudioEngine ();
	}

Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::doOutputSelectorChange( %p, 0x%x, 0x%x ) = 0x%x", this, audioControl, oldValue, newValue, kIOReturnSuccess);
	return kIOReturnSuccess;
}

// This should detect a playthrough path; which is a non-streaming input terminal connected to a non-streaming output terminal.
OSArray * AppleUSBAudioDevice::getPlaythroughPaths (UInt8 inputTerminalID) {	//	<rdar://5366067>
	OSArray *							arrayOfPathsFromOutputTerminal;
	OSArray *							playThroughPaths;
	OSArray *							aPath;
	OSNumber *							theUnitIDNum;
	UInt32								numOutputTerminalArrays;
	UInt32								numPathsFromOutputTerminal;
	UInt32								pathsToOutputTerminalN;
	UInt32								pathNumber;
	UInt16								terminalType;
	UInt8								controlInterfaceNum;
	UInt8								unitID;

	playThroughPaths = NULL;
	FailIf (NULL == mControlInterface, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

	numOutputTerminalArrays = mControlGraph->getCount ();
	for (pathsToOutputTerminalN = 0; pathsToOutputTerminalN < numOutputTerminalArrays; pathsToOutputTerminalN++) 
	{
		arrayOfPathsFromOutputTerminal = OSDynamicCast (OSArray, mControlGraph->getObject (pathsToOutputTerminalN));
		FailIf (NULL == arrayOfPathsFromOutputTerminal, Exit);
		aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (0));
		FailIf (NULL == aPath, Exit);
		theUnitIDNum = OSDynamicCast (OSNumber, aPath->getObject (0));
		FailIf (NULL == theUnitIDNum, Exit);
		unitID = theUnitIDNum->unsigned8BitValue ();
		FailIf (kIOReturnSuccess != mConfigDictionary->getOutputTerminalType (&terminalType, controlInterfaceNum, 0, unitID), Exit);
		if (0x101 == terminalType) continue;		// only looking for non-streaming outputs

		numPathsFromOutputTerminal = arrayOfPathsFromOutputTerminal->getCount ();
		for (pathNumber = 0; pathNumber < numPathsFromOutputTerminal; pathNumber++) 
		{
			aPath = OSDynamicCast (OSArray, arrayOfPathsFromOutputTerminal->getObject (pathNumber));
			FailIf (NULL == aPath, Exit);
			theUnitIDNum = OSDynamicCast (OSNumber, aPath->getLastObject ());
			FailIf (NULL == theUnitIDNum, Exit);
			unitID = theUnitIDNum->unsigned8BitValue ();
			if (unitID != inputTerminalID) continue;	//	<rdar://5366067> only looking for outputs attached to the current input.
			FailIf (kIOReturnSuccess != mConfigDictionary->getInputTerminalType (&terminalType, controlInterfaceNum, 0, unitID), Exit);
			if (terminalType != 0x101) 
			{
				if (NULL == playThroughPaths) 
				{
					playThroughPaths = OSArray::withObjects ((const OSObject **)&aPath, 1);
				} 
				else 
				{
					playThroughPaths->setObject (aPath);
				}
			}
		}
	}

Exit:
	return playThroughPaths;
}

// This finds the feature unit closest to the input terminal.
UInt8 AppleUSBAudioDevice::getBestFeatureUnitInPath (OSArray * thePath, UInt32 direction, UInt8 interfaceNum, UInt8 altSettingNum, UInt32 controlTypeWanted) {
	OSNumber *							theUnitIDNum;
	UInt32								numUnitsInPath;
	UInt8								featureUnitID;
	UInt8								unitIndex;
	UInt8								startingUnitIndex;			//	<rdar://5366067>
	UInt8								endingUnitIndex;			//	<rdar://5366067>
	UInt8								controlInterfaceNum;
	UInt8								unitID;
	UInt8								subType;
	UInt8								channelNum;
	UInt8								numChannels;
	UInt8								selectorUnitIndex;			//	<rdar://5366067>
	UInt8								mixerUnitIndex;				//	<rdar://5366067>
	UInt8								mixerUnitID;				//	<rdar://5366067>
	Boolean								foundSelectorUnit;			//	<rdar://5366067>
	Boolean								foundMixerUnit;				//	<rdar://5366067>
	Boolean								foundFeatureUnit;			//	<rdar://5366067>

	debugIOLog ( "+ AppleUSBAudioDevice[%p]::getBestFeatureUnitInPath (%p, %lu, %d, %d, %lu)", this, thePath, direction, interfaceNum, altSettingNum, controlTypeWanted );
	featureUnitID = 0;
	FailIf (NULL == mControlInterface, Exit);
	FailIf (NULL == thePath, Exit);
	controlInterfaceNum = mControlInterface->GetInterfaceNumber ();
	numUnitsInPath = thePath->getCount ();
	foundFeatureUnit = FALSE;	//	<rdar://5366067>

	switch (direction) 
	{
		case kIOAudioControlUsagePassThru:
			// [rdar://4586274] Require playthrough control feature units to be path unique.
			//	<rdar://5366067> Find the mixer unit and look for the FU between the input terminal & mixer unit.
			foundMixerUnit = false;
			mixerUnitID = 0;
			for (unitIndex = numUnitsInPath - 2; unitIndex > 0; unitIndex--) 
			{
				theUnitIDNum = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
				if (NULL != theUnitIDNum) 
				{
					unitID = theUnitIDNum->unsigned8BitValue ();
					FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
					if	(MIXER_UNIT == subType)
					{
						foundMixerUnit = true;
						mixerUnitIndex = unitIndex;
						mixerUnitID = unitID;
						break;
					}
				}
			}
			endingUnitIndex = foundMixerUnit ? mixerUnitIndex : 0;
			// Find the feature unit closest to the input terminal. If there is a mixer unit, also check to see if 
			// the FU is unique between the mixer unit & the input terminal.
			for (unitIndex = numUnitsInPath - 2; unitIndex > endingUnitIndex && !foundFeatureUnit; unitIndex--) 
			{
				theUnitIDNum = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
				if (NULL != theUnitIDNum) 
				{
					unitID = theUnitIDNum->unsigned8BitValue ();
					FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
					if	(		(FEATURE_UNIT == subType)
							&&	(	( 1 == pathsContaining (unitID) ) || 
									( foundMixerUnit &&  ( 0 == pathsContainingFeatureUnitButNotMixerUnit (unitID, mixerUnitID) ) ) ) )
					{
						FailIf (kIOReturnSuccess != mConfigDictionary->getNumChannels (&numChannels, interfaceNum, altSettingNum), Exit);
						for (channelNum = 0; channelNum <= numChannels; channelNum++) 
						{
							switch (controlTypeWanted) 
							{
								case kVolumeControl:
									if (mConfigDictionary->channelHasVolumeControl (controlInterfaceNum, 0, unitID, channelNum))
									{
										featureUnitID = unitID;
										foundFeatureUnit = TRUE;
									}
									break;
								case kMuteControl:
									if (mConfigDictionary->channelHasMuteControl (controlInterfaceNum, 0, unitID, channelNum))
									{
										featureUnitID = unitID;
										foundFeatureUnit = TRUE;
									}
									break;
							} // switch
						} // for channelNum
					} // if (FEATURE_UNIT == subType)
				} // if (NULL != theUnitIDNum)
				else
				{
					debugIOLog ( "! AppleUSBAudioDevice::getBestFeatureUnitInPath () - something is wrong here!!!" );
				}
			} // for unitIndex
			break;
		case kIOAudioControlUsageInput:
			//	<rdar://5366067> See if a selector unit is present in the path.
			foundSelectorUnit = false;
			for (unitIndex = numUnitsInPath - 2; unitIndex > 0; unitIndex--) 
			{
				theUnitIDNum = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
				if (NULL != theUnitIDNum) 
				{
					unitID = theUnitIDNum->unsigned8BitValue ();
					FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
					if	(SELECTOR_UNIT == subType)
					{
						foundSelectorUnit = true;
						selectorUnitIndex = unitIndex;
						break;
					}
				}
			}
			foundFeatureUnit = false;
			if ( foundSelectorUnit )
			{
				//	<rdar://5366067> Find the feature unit closest to the selector.
				for (unitIndex = numUnitsInPath - 2; unitIndex > selectorUnitIndex; unitIndex--) 
				{
					theUnitIDNum = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
					if (NULL != theUnitIDNum) 
					{
						unitID = theUnitIDNum->unsigned8BitValue ();
						FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
						if	(FEATURE_UNIT == subType)
						{
							FailIf (kIOReturnSuccess != mConfigDictionary->getNumChannels (&numChannels, interfaceNum, altSettingNum), Exit);
							for (channelNum = 0; channelNum <= numChannels; channelNum++) 
							{
								switch (controlTypeWanted) 
								{
									case kVolumeControl:
										if (mConfigDictionary->channelHasVolumeControl (controlInterfaceNum, 0, unitID, channelNum))
										{
											featureUnitID = unitID;
											foundFeatureUnit = TRUE;
										}
										break;
									case kMuteControl:
										if (mConfigDictionary->channelHasMuteControl (controlInterfaceNum, 0, unitID, channelNum))
										{
											featureUnitID = unitID;
											foundFeatureUnit = TRUE;
										}
										break;
								} // switch
							} // for channelNum
						} // if (FEATURE_UNIT == subType)
					} // if (NULL != theUnitIDNum)
					else
					{
						debugIOLog ( "! AppleUSBAudioDevice::getBestFeatureUnitInPath () - something is wrong here!!!" );
					}
				} // for unitIndex
			}
			
			if (!foundFeatureUnit)
			{
				//	<rdar://5366067> If selector is not present, this will find the feature unit closest to the input terminal. Otherwise,
				//	it continues on from selector to the output terminal.
				startingUnitIndex = foundSelectorUnit ? ( selectorUnitIndex - 1)  : (  numUnitsInPath - 2 );
				for (unitIndex = startingUnitIndex; unitIndex > 0 && !foundFeatureUnit; unitIndex--) 
				{
					theUnitIDNum = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
					if (NULL != theUnitIDNum) 
					{
						unitID = theUnitIDNum->unsigned8BitValue ();
						FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
						if	(FEATURE_UNIT == subType)
						{
							FailIf (kIOReturnSuccess != mConfigDictionary->getNumChannels (&numChannels, interfaceNum, altSettingNum), Exit);
							for (channelNum = 0; channelNum <= numChannels; channelNum++) 
							{
								switch (controlTypeWanted) 
								{
									case kVolumeControl:
										if (mConfigDictionary->channelHasVolumeControl (controlInterfaceNum, 0, unitID, channelNum))
										{
											featureUnitID = unitID;
											foundFeatureUnit = TRUE;
										}
										break;
									case kMuteControl:
										if (mConfigDictionary->channelHasMuteControl (controlInterfaceNum, 0, unitID, channelNum))
										{
											featureUnitID = unitID;
											foundFeatureUnit = TRUE;
										}
										break;
								} // switch
							} // for channelNum
						} // if (FEATURE_UNIT == subType)
					} // if (NULL != theUnitIDNum)
					else
					{
						debugIOLog ( "! AppleUSBAudioDevice::getBestFeatureUnitInPath () - something is wrong here!!!" );
					}
				} // for unitIndex
			}
			break;
		case kIOAudioControlUsageOutput:
		default:
			// Find the feature unit closest to the output terminal.
			debugIOLog ("? AppleUSBAudioDevice::getBestFeatureUnitInPath () - kIOAudioControlUsageOutput ");
			for (unitIndex = 1; unitIndex < numUnitsInPath && !foundFeatureUnit; unitIndex++) 
			{
				theUnitIDNum = OSDynamicCast (OSNumber, thePath->getObject (unitIndex));
				if (NULL != theUnitIDNum) 
				{
					unitID = theUnitIDNum->unsigned8BitValue ();
					FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
					if (FEATURE_UNIT == subType) 
					{
						debugIOLog ( "  examining feature unit %d ...", unitID );
						FailIf (kIOReturnSuccess != mConfigDictionary->getNumChannels (&numChannels, interfaceNum, altSettingNum), Exit);
						for (channelNum = 0; channelNum <= numChannels; channelNum++) 
						{
							switch (controlTypeWanted) 
							{
								case kVolumeControl:
									if (mConfigDictionary->channelHasVolumeControl (controlInterfaceNum, 0, unitID, channelNum))
									{
										featureUnitID = unitID;
										foundFeatureUnit = TRUE;
									}
									break;
								case kMuteControl:
									if (mConfigDictionary->channelHasMuteControl (controlInterfaceNum, 0, unitID, channelNum)) 
									{
										featureUnitID = unitID;
										foundFeatureUnit = TRUE;
									}
									break;
							} // switch
						} // for channelNum
					} // if (FEATURE_UNIT == subType)
				} // if (NULL != theUnitIDNum)
				else
				{
					debugIOLog ( "! AppleUSBAudioDevice::getBestFeatureUnitInPath () - something is wrong here!!!" );
				}
			} // for unitIndex
			break;
	} // switch (direction)

Exit:
	debugIOLog ( "- AppleUSBAudioDevice[%p]::getBestFeatureUnitInPath () = %d", this, featureUnitID );
	return featureUnitID;
}

UInt8 AppleUSBAudioDevice::pathsContaining (UInt8 unitID)
{
	OSArray *	thisPathGroup = NULL;
	OSArray *	thisPath = NULL;
	OSNumber *  unitNumber = NULL;
	UInt8		numPaths = 0;
	
	if ( mControlGraph )
	{
		for ( UInt8 pathGroupIndex = 0; pathGroupIndex < mControlGraph->getCount(); pathGroupIndex++ )
		{
			thisPathGroup = OSDynamicCast ( OSArray, mControlGraph->getObject ( pathGroupIndex ) );
			if ( thisPathGroup )
			{
				for (UInt8 pathIndex = 0; pathIndex < thisPathGroup->getCount(); pathIndex++ )
				{
					thisPath = OSDynamicCast ( OSArray, thisPathGroup->getObject (pathIndex) );
					if ( thisPath )
					{
						for ( UInt8 unitIndex = 0; unitIndex < thisPath->getCount (); unitIndex++ )
						{
							unitNumber = OSDynamicCast ( OSNumber, thisPath->getObject (unitIndex) );
							if ( unitID == unitNumber->unsigned8BitValue() )
							{
								numPaths++;
								// Circular topologies are not allowed.
								break;
							}
						}
					}
				}
			}
		} 
	}
	return numPaths;
}

//	<rdar://5366067>
UInt8 AppleUSBAudioDevice::pathsContainingFeatureUnitButNotMixerUnit (UInt8 featureUnitID, UInt8 mixerUnitID)
{
	OSArray *	thisPathGroup = NULL;
	OSArray *	thisPath = NULL;
	OSNumber *  unitNumber = NULL;
	UInt8		unitID;
	UInt8		controlInterfaceNum;
	UInt8		numPaths = 0;
	Boolean		foundFeatureUnit;
	Boolean		foundMixerUnit;
	
	if ( mControlGraph )
	{
		FailIf (NULL == mControlInterface, Exit);
		controlInterfaceNum = mControlInterface->GetInterfaceNumber ();

		for ( UInt8 pathGroupIndex = 0; pathGroupIndex < mControlGraph->getCount(); pathGroupIndex++ )
		{
			thisPathGroup = OSDynamicCast ( OSArray, mControlGraph->getObject ( pathGroupIndex ) );
			if ( thisPathGroup )
			{
				for (UInt8 pathIndex = 0; pathIndex < thisPathGroup->getCount(); pathIndex++ )
				{
					thisPath = OSDynamicCast ( OSArray, thisPathGroup->getObject (pathIndex) );
					if ( thisPath )
					{
						foundFeatureUnit = false;
						foundMixerUnit = false;
						for ( UInt8 unitIndex = 0; unitIndex < thisPath->getCount (); unitIndex++ )
						{
							unitNumber = OSDynamicCast ( OSNumber, thisPath->getObject (unitIndex) );
							if ( NULL != unitNumber )
							{
								unitID = unitNumber->unsigned8BitValue ();
								if ( featureUnitID == unitID )
								{
									foundFeatureUnit = true;
								}
								else if ( mixerUnitID == unitID )
								{
									foundMixerUnit = true;
								}
							}
						}
						if ( foundFeatureUnit && !foundMixerUnit )
						{
							numPaths++;
						}
					}
				}
			}
		} 
	}
Exit:
	return numPaths;
}


UInt8 AppleUSBAudioDevice::getSelectorSetting (UInt8 selectorID) {
    IOUSBDevRequestDesc					devReq;
	IOReturn							result;
	UInt8								setting;
	IOBufferMemoryDescriptor *			settingDesc = NULL;

	setting = 0;
	FailIf (NULL == mControlInterface, Exit);

	settingDesc = IOBufferMemoryDescriptor::withOptions (kIODirectionIn, 1);
	FailIf (NULL == settingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBIn, kUSBClass, kUSBInterface);
    devReq.bRequest = ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() ) ? (UInt8)USBAUDIO_0200::CUR : GET_CUR;
    devReq.wValue = ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() ) ? ( (UInt16)USBAUDIO_0200::SU_SELECTOR_CONTROL ) << 8 : 0;	// <rdar://problem/6001162>
    devReq.wIndex = (0xFF00 & (selectorID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = 1;
    devReq.pData = settingDesc;

	result = deviceRequest (&devReq);
	FailIf (kIOReturnSuccess != result, Exit);
	memcpy (&setting, settingDesc->getBytesNoCopy (), 1);

Exit:
	if (NULL != settingDesc) 
	{
		settingDesc->release ();
	}
	return setting;
}

IOReturn AppleUSBAudioDevice::setSelectorSetting (UInt8 selectorID, UInt8 setting) {
    IOUSBDevRequestDesc					devReq;
	IOReturn							result;
	IOBufferMemoryDescriptor *			settingDesc = NULL;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	settingDesc = IOBufferMemoryDescriptor::withBytes (&setting, 1, kIODirectionIn);
	FailIf (NULL == settingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBOut, kUSBClass, kUSBInterface);
    devReq.bRequest = SET_CUR;
    devReq.wValue = ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() ) ? ( ( UInt16 )USBAUDIO_0200::SU_SELECTOR_CONTROL ) << 8 : 0;	// <rdar://problem/6001162>
    devReq.wIndex = (0xFF00 & (selectorID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = 1;
    devReq.pData = settingDesc;

	result = deviceRequest (&devReq);

Exit:
	if (NULL != settingDesc) 
	{
		settingDesc->release ();
	}
	return result;
}

void AppleUSBAudioDevice::setMonoState (Boolean state)
{
	mDeviceIsInMonoMode = state;
}

IOReturn AppleUSBAudioDevice::createControlsForInterface (IOAudioEngine *audioEngine, UInt8 interfaceNum, UInt8 altSettingNum) {
    AppleUSBAudioEngine *				usbAudioEngine;
    IOReturn							result;
    debugIOLog ("? AppleUSBAudioDevice[%p]::createControlsForInterface () - Interface %d alternate setting %d", this, interfaceNum, altSettingNum);

    result = kIOReturnError;
	mTerminatingDriver = FALSE;

    usbAudioEngine = OSDynamicCast (AppleUSBAudioEngine, audioEngine);
    FailIf (NULL == usbAudioEngine, Exit);

	doControlStuff (audioEngine, interfaceNum, altSettingNum);

Exit:
	return result;
}

// [rdar://4867843]
OSArray * AppleUSBAudioDevice::getOptimalClockPath ( AppleUSBAudioEngine * thisEngine, UInt8 streamInterface, UInt8 altSetting, UInt32 sampleRate, Boolean * otherEngineNeedSampleRateChange, UInt8 * clockPathGroupIndex )	//	<rdar://5811247>
{
	OSNumber *						clockSourceIDNumber = NULL;
	OSArray *						pathGroupArray = NULL;
	OSArray *						pathArray = NULL;
	OSArray *						optimalPathArray = NULL;
	UInt8							pathIndex;
	UInt8							terminalID;
	UInt8							clockSourceID;
	UInt32							clockPathUnitUsage = 0xFFFFFFFF;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::getOptimalClockPath ( %d, %d, %d )", this, streamInterface, altSetting, sampleRate );
	FailIf ( NULL == mConfigDictionary, Exit );
	FailIf ( NULL == mControlInterface, Exit );
	
	FailIf ( kIOReturnSuccess != mConfigDictionary->getTerminalLink ( &terminalID, streamInterface, altSetting ), Exit );
	FailIf ( kIOReturnSuccess != mConfigDictionary->getClockSourceID ( &clockSourceID, mControlInterface->GetInterfaceNumber (), 0, terminalID ), Exit ); 
	
	debugIOLog (" ? AppleUSBAudioDevice::getOptimalClockPath () - interface %d, alt setting %d, clock source ID %d", streamInterface, altSetting, clockSourceID );
	
	// Find the path group that begins with the clockSourceID.
	for ( UInt8 pathGroupIndex = 0; pathGroupIndex < mClockGraph->getCount (); pathGroupIndex++ )
	{
		FailIf ( NULL == ( pathGroupArray = OSDynamicCast ( OSArray, mClockGraph->getObject ( pathGroupIndex ) ) ), Exit );
		FailIf ( NULL == ( pathArray = OSDynamicCast ( OSArray, pathGroupArray->getObject ( 0 ) ) ), Exit );
		FailIf ( NULL == ( clockSourceIDNumber = OSDynamicCast ( OSNumber, pathArray->getObject ( 0 ) ) ), Exit );
		if ( clockSourceID == clockSourceIDNumber->unsigned8BitValue () )
		{
			// We have found the path group in which we are interested.
			//	<rdar://5811247>
			if ( NULL != clockPathGroupIndex )
			{
				*clockPathGroupIndex = pathGroupIndex;
			}
			break;
		}
		else
		{
			pathGroupArray = NULL;
		}
	}
	FailIf ( NULL == pathGroupArray, Exit );

	// For each path in the path group, determine if it supported the requested sample rate.
	for ( pathIndex = 0; pathIndex < pathGroupArray->getCount (); pathIndex++ )
	{
		FailIf ( NULL == ( pathArray = OSDynamicCast ( OSArray, pathGroupArray->getObject ( pathIndex ) ) ), Exit );
		
		if ( supportSampleRateInClockPath ( pathArray, sampleRate ) )
		{
			// Need to select the path with the clock units that are not used (or has the smallest usage count) by other paths 
			// in other engines (if possible).
			UInt32 usageCount = determineClockPathUnitUsage ( thisEngine, pathArray );
			
			if ( usageCount < clockPathUnitUsage )
			{
				clockPathUnitUsage = usageCount;
				optimalPathArray = pathArray;
			}
			
			if ( 0 == clockPathUnitUsage )
			{
				// Found a path that is independent of the paths in other engines.
				break;
			}
		}
	}	

Exit:
	if ( NULL != otherEngineNeedSampleRateChange )
	{
		* otherEngineNeedSampleRateChange = ( clockPathUnitUsage > 0 );
	}

	debugIOLog ( "- AppleUSBAudioDevice[%p]::getOptimalClockPath ( %d, %d, %d ) = %p", this, streamInterface, altSetting, sampleRate, optimalPathArray );
	return optimalPathArray;
}

OSArray * AppleUSBAudioDevice::getClockPathGroup ( UInt8 streamInterface, UInt8 altSetting, UInt8 * clockPathGroupIndex )	//	<rdar://5811247>
{
	OSNumber *						clockSourceIDNumber = NULL;
	OSArray *						pathGroupArray = NULL;
	OSArray *						pathArray = NULL;
	UInt8							terminalID;
	UInt8							clockSourceID;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::getClockPathGroup ( %d, %d )", this, streamInterface, altSetting );
	FailIf ( NULL == mConfigDictionary, Exit );
	FailIf ( NULL == mControlInterface, Exit );
	
	FailIf ( kIOReturnSuccess != mConfigDictionary->getTerminalLink ( &terminalID, streamInterface, altSetting ), Exit );
	FailIf ( kIOReturnSuccess != mConfigDictionary->getClockSourceID ( &clockSourceID, mControlInterface->GetInterfaceNumber (), 0, terminalID ), Exit ); 
	
	debugIOLog (" ? AppleUSBAudioDevice::getClockPathGroup () - interface %d, alt setting %d, clock source ID %d", streamInterface, altSetting, clockSourceID );
	
	// Find the path group that begins with the clockSourceID.
	for ( UInt8 pathGroupIndex = 0; pathGroupIndex < mClockGraph->getCount (); pathGroupIndex++ )
	{
		FailIf ( NULL == ( pathGroupArray = OSDynamicCast ( OSArray, mClockGraph->getObject ( pathGroupIndex ) ) ), Exit );
		FailIf ( NULL == ( pathArray = OSDynamicCast ( OSArray, pathGroupArray->getObject ( 0 ) ) ), Exit );
		FailIf ( NULL == ( clockSourceIDNumber = OSDynamicCast ( OSNumber, pathArray->getObject ( 0 ) ) ), Exit );
		if ( clockSourceID == clockSourceIDNumber->unsigned8BitValue () )
		{
			// We have found the path group in which we are interested.
			//	<rdar://5811247>
			if ( NULL != clockPathGroupIndex )
			{
				*clockPathGroupIndex = pathGroupIndex;
			}
			break;
		}
		else
		{
			pathGroupArray = NULL;
		}
	}
	FailIf ( NULL == pathGroupArray, Exit );

Exit:

	debugIOLog ( "- AppleUSBAudioDevice[%p]::getClockPathGroup ( %d, %d ) = %p", this, streamInterface, altSetting, pathGroupArray );
	return pathGroupArray;
}

//	<rdar://5811247>
OSArray * AppleUSBAudioDevice::getClockPathGroup ( UInt8 pathGroupIndex )
{
	OSArray *						pathGroupArray = NULL;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::getClockPathGroup ( %d )", this, pathGroupIndex );
	
	// Find the path group that begins with the clockSourceID.
	FailIf ( pathGroupIndex >= mClockGraph->getCount (), Exit );
	FailIf ( NULL == ( pathGroupArray = OSDynamicCast ( OSArray, mClockGraph->getObject ( pathGroupIndex ) ) ), Exit );

Exit:

	debugIOLog ( "- AppleUSBAudioDevice[%p]::getClockPathGroup ( %d ) = %p", this, pathGroupIndex, pathGroupArray );
	return pathGroupArray;
}

//	<rdar://5811247>
IOReturn AppleUSBAudioDevice::getClockSelectorIDAndPathIndex (UInt8 * selectorID, UInt8 * pathIndex, OSArray * clockPath) {
	IOReturn				result;

	result = kIOReturnNotFound;
	FailIf ( NULL == mControlInterface, Exit );
	FailIf ( NULL == selectorID, Exit );
	FailIf ( NULL == pathIndex, Exit );
	
	*selectorID = 0;
	*pathIndex = 0;
	
	for ( UInt32 clockIndex = 0; clockIndex <  clockPath->getCount (); clockIndex++ ) 
	{
		OSNumber *			clockIDNumber;
		UInt8				clockID;
		UInt8				subType;

		FailIf ( NULL == ( clockIDNumber = OSDynamicCast ( OSNumber, clockPath->getObject ( clockIndex ) ) ), Exit );
		clockID = clockIDNumber->unsigned8BitValue ();
		
		FailIf  (kIOReturnSuccess != ( result = mConfigDictionary->getSubType ( &subType, mControlInterface->GetInterfaceNumber(), 0, clockID ) ), Exit );
		
		if (USBAUDIO_0200::CLOCK_SELECTOR == subType)
		{
			OSArray *		clockSourceIDs = NULL;
			OSNumber *		clockSourceIDNumber = NULL;
			OSNumber *		nextClockIDNumber = NULL;
			UInt8			nextClockID;
			
			FailIf ( kIOReturnSuccess != (result = mConfigDictionary->getClockSelectorSources ( &clockSourceIDs, mControlInterface->GetInterfaceNumber (), 0, clockID ) ), Exit );
			FailIf ( NULL == ( nextClockIDNumber = OSDynamicCast ( OSNumber, clockPath->getObject ( clockIndex + 1 ) ) ), Exit );

			nextClockID = nextClockIDNumber->unsigned8BitValue ();			

			for ( UInt32 index = 0; index < clockSourceIDs->getCount (); index++ )
			{  
				FailIf ( NULL == ( clockSourceIDNumber = OSDynamicCast ( OSNumber, clockSourceIDs->getObject ( index ) ) ), Exit );
			
				if ( clockSourceIDNumber->unsigned8BitValue() == nextClockID )
				{
					*selectorID = clockID;
					*pathIndex = index + 1;
					result = kIOReturnSuccess;
					break;
				}			
			}
		}
	}
	
Exit:
	return result;
}

Boolean AppleUSBAudioDevice::supportSampleRateInClockPath ( OSArray * pathArray, UInt32 sampleRate )
{
	Boolean							sampleRateSupported = false;
	UInt8							numRange;
	UInt8							rangeIndex;
	SubRange32						subRange;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::supportSampleRateInClockPath ()", this );

	FailIf ( kIOReturnSuccess != getNumSampleRatesForClockPath ( &numRange, pathArray ), Exit );
	
	for ( rangeIndex = 0; rangeIndex < numRange; rangeIndex++ )
	{
		if ( kIOReturnSuccess == getIndexedSampleRatesForClockPath ( &subRange, pathArray, rangeIndex ) )
		{		
			// If dRES is not zero, there should be a total of 1 + ( ( dMAX - dMIN ) / dRES ) sample rates including dMIN and dMAX.
			if ( 0 != subRange.dRES )
			{
				// [rdar://5614104] Correct the boundary condition below. 
				// sampleRateIndex should iterate from 0 to one less than the total number of sample rates.
				for ( UInt16 sampleRateIndex = 0; sampleRateIndex <= ( ( subRange.dMAX - subRange.dMIN) / subRange.dRES ); sampleRateIndex++ )		// <rdar://7446555>
				{
					// [rdar://5614104] Correct the conditional below based on the above change.
					if ( sampleRate == ( subRange.dMIN + sampleRateIndex * subRange.dRES ) )
					{
						sampleRateSupported = true;				
						break;
					}
				}
			}
			else
			{
				if ( ( sampleRate == subRange.dMIN ) || ( sampleRate == subRange.dMAX ) )
				{
					sampleRateSupported = true;				
				}
			}
		}
		
		if ( sampleRateSupported )
		{
			break;
		}
	}
	
Exit:
	debugIOLog ( "- AppleUSBAudioDevice[%p]::supportSampleRateInClockPath () = %d", this, sampleRateSupported );
	return sampleRateSupported;
}

UInt32 AppleUSBAudioDevice::determineClockPathUnitUsage ( AppleUSBAudioEngine * thisEngine, OSArray * thisClockPath )
{
	UInt32							usageCount = 0;
	SInt32							engineIndex;
	OSDictionary *					otherAudioEngineInfo = NULL;
	AppleUSBAudioEngine *			otherAudioEngine = NULL;

	debugIOLog ( "+ AppleUSBAudioDevice[%p]::determineClockPathUnitUsage ( %p, %p )", this, thisEngine, thisClockPath );

	if ( NULL != mRegisteredEngines )
	{
		for ( engineIndex = 0; ; engineIndex++ )
		{	
			if ( NULL == ( otherAudioEngineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineIndex) ) ) )
			{
				break;
			}			
			
			if ( NULL == ( otherAudioEngine = OSDynamicCast (AppleUSBAudioEngine, otherAudioEngineInfo->getObject (kEngine) ) ) )
			{
				break;
			}
			
			if (thisEngine != otherAudioEngine)
			{
				if ( NULL != otherAudioEngine->mIOAudioStreamArray )
				{
					for ( UInt32 index = 0; index < otherAudioEngine->mIOAudioStreamArray->getCount (); index++ )
					{
						AppleUSBAudioStream * appleUSBAudioStream = OSDynamicCast ( AppleUSBAudioStream, otherAudioEngine->mIOAudioStreamArray->getObject (index) );
						
						if ( ( NULL != appleUSBAudioStream ) && ( NULL != appleUSBAudioStream->mActiveClockPath ) )
						{
							if ( clockPathCrossed ( thisClockPath, appleUSBAudioStream->mActiveClockPath ) )
							{
								usageCount++;
							}
						}
					}
				}
			}
		}
	}

	debugIOLog ( "+ AppleUSBAudioDevice[%p]::determineClockPathUnitUsage ( %p, %p ) = %d", this, thisEngine, thisClockPath, usageCount );
	return usageCount;
}

Boolean AppleUSBAudioDevice::clockPathCrossed ( OSArray * clockPathA, OSArray * clockPathB )
{
	OSNumber *						clockIDNumberA = NULL;
	OSNumber *						clockIDNumberB = NULL;
	Boolean							pathCrossed = false;
	
	// Determine it the 2 paths crossed.
	for ( UInt8 pathItemA = 0; pathItemA < clockPathA->getCount(); pathItemA++ )
	{
		FailIf ( NULL == ( clockIDNumberA = OSDynamicCast ( OSNumber, clockPathA->getObject (pathItemA) ) ), Exit );
	
		for ( UInt8 pathItemB = 0; pathItemB < clockPathB->getCount(); pathItemB++ )
		{
			FailIf ( NULL == ( clockIDNumberB = OSDynamicCast ( OSNumber, clockPathB->getObject (pathItemB) ) ), Exit );
		
			if ( clockIDNumberA->unsigned8BitValue () == clockIDNumberB->unsigned8BitValue () )
			{
				pathCrossed = true;
				break;
			}						
		}			
		
		if ( pathCrossed )
		{
			break;
		}
	}
	
Exit:
	return pathCrossed;
}

// [rdar://4867779]
IOReturn AppleUSBAudioDevice::addSampleRatesFromClockSpace ()
{
	IOReturn						result = kIOReturnError;
	OSNumber *						clockSourceIDNumber = NULL;
	OSNumber *						streamInterfaceNumber = NULL;
	OSArray *						streamInterfaceNumbers = NULL;
	OSArray *						pathGroupArray = NULL;
	OSArray *						pathArray = NULL;
	UInt8							streamInterface;
	UInt8							numStreamInterfaces;
	UInt8							numAltSettings;
	UInt8							terminalID;
	UInt8							clockSourceID;
	bool							startAtZero;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::addSampleRatesFromClockSpace ()", this );
	FailIf ( NULL == mControlInterface, Exit );		// <rdar://7085810>
	FailIf ( NULL == mConfigDictionary, Exit );
	FailIf ( NULL == mClockGraph, Exit );
	
	// Discover sample rates for each stream interface
	FailIf ( kIOReturnSuccess != mConfigDictionary->getControlledStreamNumbers ( &streamInterfaceNumbers, &numStreamInterfaces ), Exit );
	for ( UInt8 streamInterfaceIndex = 0; streamInterfaceIndex < streamInterfaceNumbers->getCount (); streamInterfaceIndex++ )
	{
		FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, streamInterfaceNumbers->getObject ( streamInterfaceIndex ) ) ), Exit );
		streamInterface = streamInterfaceNumber->unsigned8BitValue ();
		startAtZero = mConfigDictionary->alternateSettingZeroCanStream ( streamInterface );
		
		// Discover sample rates for each alternate setting
		FailIf ( kIOReturnSuccess != mConfigDictionary->getNumAltSettings ( &numAltSettings, streamInterface ), Exit );
		for		(	UInt8 altSettingIndex = ( startAtZero ? 0 : 1 ); 
					altSettingIndex < numAltSettings;
					altSettingIndex++ )
		{
			FailIf ( kIOReturnSuccess != mConfigDictionary->getTerminalLink ( &terminalID, streamInterface, altSettingIndex ), Exit );
			FailIf ( kIOReturnSuccess != mConfigDictionary->getClockSourceID ( &clockSourceID, mControlInterface->GetInterfaceNumber (), 0, terminalID ), Exit ); 
			
			debugIOLog (" ? AppleUSBAudioDevice::addSampleRatesFromClockSpace () - interface %d, alt setting %d, clock source ID %d", streamInterface, altSettingIndex, clockSourceID );
			// Find the path group that begins with the clockSourceID.
			for ( UInt8 pathGroupIndex = 0; pathGroupIndex < mClockGraph->getCount (); pathGroupIndex++ )
			{
				FailIf ( NULL == ( pathGroupArray = OSDynamicCast ( OSArray, mClockGraph->getObject ( pathGroupIndex ) ) ), Exit );
				FailIf ( NULL == ( pathArray = OSDynamicCast ( OSArray, pathGroupArray->getObject ( 0 ) ) ), Exit );
				FailIf ( NULL == ( clockSourceIDNumber = OSDynamicCast ( OSNumber, pathArray->getObject ( 0 ) ) ), Exit );
				if ( clockSourceID == clockSourceIDNumber->unsigned8BitValue () )
				{
					// We have found the path group in which we are interested.
					break;
				}
				else
				{
					pathGroupArray = NULL;
				}
			}
			FailIf ( NULL == pathGroupArray, Exit );
			
			// For each path in the path group, add the available sample rates.
			for ( UInt8 pathIndex = 0; pathIndex < pathGroupArray->getCount (); pathIndex++ )
			{
				FailIf ( NULL == ( pathArray = OSDynamicCast ( OSArray, pathGroupArray->getObject ( pathIndex ) ) ), Exit );
				FailIf ( kIOReturnSuccess != addSampleRatesFromClockPath ( pathArray, streamInterface, altSettingIndex ), Exit );				
			}
		}
	}
	
	result = kIOReturnSuccess;
	
Exit:
	debugIOLog ( "- AppleUSBAudioDevice[%p]::addSampleRatesFromClockSpace () = 0x%x", this, result );
	return result;
}

IOReturn AppleUSBAudioDevice::addSampleRatesFromClockPath ( OSArray * path, UInt8 streamInterface, UInt8 altSetting )
{
	IOReturn						result = kIOReturnError;
	OSNumber *						clockSourceNumber;
	OSObject *						arrayObject = NULL;
	OSNumber *						arrayNumber = NULL;
	UInt32							clockIndex;
	UInt8							clockID;
	UInt8							subType;
	UInt16							numerator;
	UInt16							denominator;
	UInt32							rateIndex;
	OSNumber *						rateNumber;
	OSArray *						rates;
	OSNumber *						sampleRateNumber;
	OSArray *						sampleRates;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::addSampleRatesFromClockPath ( %p, %d, %d )", this, path, streamInterface, altSetting );
	FailIf ( NULL == path, Exit );
	FailIf ( NULL == mConfigDictionary, Exit );
	
	// First get the samples rates on the clock source, which will be the last element of the path.
	FailIf ( NULL == ( clockSourceNumber = OSDynamicCast ( OSNumber, path->getLastObject () ) ), Exit );
	FailIf ( kIOReturnSuccess != getClockSourceSampleRates ( &sampleRates, clockSourceNumber->unsigned8BitValue () ), Exit );
	
	// Clock multipliers will modify the available sample rates, so search for them in the path.
	// [rdar://5600081] Add sample rates via clock multipliers
	for (clockIndex = path->getCount (); clockIndex > 0 ; clockIndex--) 
	{
		FailIf (NULL == (arrayObject = path->getObject (clockIndex - 1)), Exit);
		FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
		clockID = arrayNumber->unsigned8BitValue();
		
		FailIf (kIOReturnSuccess != (result = mConfigDictionary->getSubType (&subType, mControlInterface->GetInterfaceNumber(), 0, clockID)), Exit);
		
		if (USBAUDIO_0200::CLOCK_MULTIPLIER == subType)
		{
			FailIf (kIOReturnSuccess !=  (result = getCurClockMultiplier (clockID, &numerator, &denominator)), Exit);
			
			rates = OSArray::withCapacity ( sampleRates->getCount () );
			FailIf ( NULL == rates, Exit );
			
			for ( rateIndex = 0; rateIndex < sampleRates->getCount (); rateIndex ++ )
			{
				sampleRateNumber = OSDynamicCast ( OSNumber, sampleRates->getObject ( rateIndex ) );
				FailIf ( NULL == sampleRateNumber, Exit );
				
				rateNumber = OSNumber::withNumber ( sampleRateNumber->unsigned32BitValue () * numerator / denominator, 32 );
				FailIf ( NULL == rateNumber, Exit );		
				
				rates->setObject ( rateNumber );
				rates->release ();
			}
			
			if ( NULL != sampleRates )
			{
				sampleRates->release ();
			}
			sampleRates = rates;
		}
	}
		
	// Add the list of discrete sample rates to the stream dictionary
	FailIf ( kIOReturnSuccess != ( result = mConfigDictionary->addSampleRatesToStreamDictionary ( sampleRates, streamInterface, altSetting ) ), Exit ); 
	
Exit:
	if ( NULL != sampleRates )
	{
		sampleRates->release ();
	}
	debugIOLog ( "- AppleUSBAudioDevice[%p]::addSampleRatesFromClockPath ( %p, %d, %d ) = 0x%x", this, path, streamInterface, altSetting, result );
	return result;
}

IOReturn AppleUSBAudioDevice::getClockSourceSampleRates ( OSArray ** sampleRates, UInt8 clockSource )
{
	IOReturn						result = kIOReturnError;
	OSNumber *						sampleRateNumber = NULL;
	SubRange32						subRange;
	UInt32							sampleRate;
	UInt16							numSampleRanges;
	bool							clockIsValid;
	bool							subRangeIsValid;

	debugIOLog ( "+ AppleUSBAudioDevice[%p]::getClockSourceSampleRates ( %p, %d )", this, sampleRates, clockSource );
	FailIf ( NULL == mConfigDictionary, Exit );
	FailIf ( NULL == sampleRates, Exit );
	FailIf ( 0 == clockSource, Exit );
	FailIf ( NULL == mControlInterface, Exit );
	* sampleRates = NULL;
	
	if ( mConfigDictionary->clockSourceHasFrequencyControl ( mControlInterface->GetInterfaceNumber (), 0, clockSource, true ) ||
		 mConfigDictionary->clockSourceHasFrequencyControl ( mControlInterface->GetInterfaceNumber (), 0, clockSource, false ) )
	{
		// Get the number of sample ranges.
		FailIf ( kIOReturnSuccess != ( result = getNumClockSourceSamplingFrequencySubRanges ( clockSource, &numSampleRanges ) ), Exit ) ;
		
		for ( UInt8 sampleRangeIndex = 0; sampleRangeIndex < numSampleRanges; sampleRangeIndex++ )
		{
			FailIf ( kIOReturnSuccess != ( result = getIndexedClockSourceSamplingFrequencySubRange ( clockSource, &subRange, sampleRangeIndex ) ), Exit );
			
			// [rdar://4952486] Make sure that the device isn't just spewing garbage before trying to add these values.
			subRangeIsValid = (			( subRange.dMIN <= subRange.dMAX )
									&&	(		( 0 == subRange.dRES )
											||	( 0 == ( subRange.dMAX - subRange.dMIN ) % subRange.dRES ) ) );		
			if ( ! subRangeIsValid )
			{
				debugIOLog ( "! AppleUSBAudioDevice[%p]::getClockSourceSampleRates () - invalid subrange, skipping ...", this );
				debugIOLog ( "    subRange.dMIN = %lu", subRange.dMIN );
				debugIOLog ( "    subRange.dMAX = %lu", subRange.dMAX );
				debugIOLog ( "    subRange.dRES = %lu", subRange.dRES );
				continue;				
			}
			
			// Add the minimum sample rate no matter what.
			FailWithAction ( NULL == ( sampleRateNumber = OSNumber::withNumber ( subRange.dMIN, SIZEINBITS( sizeof ( UInt32 ) ) ) ), result = kIOReturnError, Exit );
			if ( * sampleRates )
			{
				(* sampleRates)->setObject ( sampleRateNumber );
			}
			else
			{
				* sampleRates = OSArray::withObjects ( ( const OSObject ** ) &sampleRateNumber, 1 );
			}
			
			sampleRateNumber->release ();
			sampleRateNumber = NULL;
			
			// Add the "middle" sample rates if necessary.
			// If dRES is not zero, there should be a total of 1 + ( ( dMAX - dMIN ) / dRES ) sample rates including dMIN and dMAX.
			if ( 0 != subRange.dRES )
			{
				for ( UInt16 sampleRateIndex = 0; sampleRateIndex <  ( ( subRange.dMAX - subRange.dMIN) / subRange.dRES ) - 1; sampleRateIndex++ )
				{
					sampleRate = subRange.dMIN + ( 1 + sampleRateIndex ) * subRange.dRES;
					FailWithAction ( NULL == ( sampleRateNumber = OSNumber::withNumber ( sampleRate, SIZEINBITS( sizeof ( UInt32 ) ) ) ), result = kIOReturnError, Exit );
					(* sampleRates)->setObject ( sampleRateNumber );
					sampleRateNumber->release ();
					sampleRateNumber = NULL;
				}
			}

			// Add the maximum sample rate if necessary.
			if	( subRange.dMAX != subRange.dMIN )
			{
				FailWithAction ( NULL == ( sampleRateNumber = OSNumber::withNumber ( subRange.dMAX, SIZEINBITS( sizeof ( UInt32 ) ) ) ), result = kIOReturnError, Exit );
				(* sampleRates)->setObject ( sampleRateNumber );
				sampleRateNumber->release ();
				sampleRateNumber = NULL;
			}
		}		
	}
	else
	{
		// There is no frequency control. Get the current sample rate only.
		FailIf ( kIOReturnSuccess != ( result = getCurClockSourceSamplingFrequency ( clockSource, &sampleRate, &clockIsValid ) ), Exit );
		FailWithAction ( NULL == ( sampleRateNumber = OSNumber::withNumber ( sampleRate, SIZEINBITS( sizeof ( UInt32 ) ) ) ), result = kIOReturnError, Exit );
		* sampleRates = OSArray::withObjects ( ( const OSObject ** ) &sampleRateNumber, 1 );
		sampleRateNumber->release ();
		sampleRateNumber = NULL;
	}
	
	// All sample rates have been added to the array. We are done.
	
	result = kIOReturnSuccess;
	
Exit:
	debugIOLog ( "- AppleUSBAudioDevice[%p]::getClockSourceSampleRates ( %p, %d ) = 0x%x", this, sampleRates, clockSource, result );
	return result;
}

OSArray * AppleUSBAudioDevice::buildClockGraph ( UInt8 controlInterfaceNum )
{
	OSArray *						allClockPaths = NULL;
	OSArray *						terminalClockEntities = NULL;
	OSArray *						pathsFromClockEntityN = NULL;
	OSArray *						thisPath = NULL;
	OSArray *						thisGroup = NULL;
	OSNumber *						clockIDNum = NULL;
	UInt8							clockID;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::buildClockGraph ( %d )", this, controlInterfaceNum );
	allClockPaths = OSArray::withCapacity ( 1 );
	FailIf ( NULL == allClockPaths, Exit );
	pathsFromClockEntityN = OSArray::withCapacity ( 1 );
	FailIf ( NULL == pathsFromClockEntityN, Exit );
	FailIf ( NULL == ( terminalClockEntities = mConfigDictionary->getTerminalClockEntities ( controlInterfaceNum, 0 ) ), Exit );
	for ( UInt8 clockIndex = 0; clockIndex < terminalClockEntities->getCount (); clockIndex++) 
	{
		FailIf ( NULL == ( clockIDNum = OSDynamicCast ( OSNumber, terminalClockEntities->getObject ( clockIndex ) ) ), Exit );
		clockID = clockIDNum->unsigned8BitValue ();
		debugIOLog ( "? AppleUSBAudioDevice[%p]::buildClockGraph () - Building clock paths from ID %d", this, clockID ); 
		buildClockPath (controlInterfaceNum, clockID, pathsFromClockEntityN, thisPath);
		allClockPaths->setObject ( pathsFromClockEntityN );
		pathsFromClockEntityN->release ();
		pathsFromClockEntityN = OSArray::withCapacity ( 1 );
		FailIf ( NULL == pathsFromClockEntityN, Exit );
	}
	
	// Print clock graph of interest
	char		pathLine[256];
	char		tempString[10];
	
	debugIOLog ( "? AppleUSBAudioDevice[%p]::buildClockGraph ( %d ) - Displaying graph ...", this, controlInterfaceNum, allClockPaths->getCount() );
	for ( UInt8 groupIndex = 0; ( allClockPaths && groupIndex < allClockPaths->getCount () ); groupIndex++ )
	{
		debugIOLog ("   Path Group # %d", groupIndex );
		FailIf ( NULL == ( thisGroup = OSDynamicCast ( OSArray, allClockPaths->getObject ( groupIndex ) ) ), Exit );
		for ( UInt8 pathIndex = 0; pathIndex < thisGroup->getCount(); pathIndex++ )
		{
			* pathLine = '\0';
			snprintf ( tempString, 10, "%2d: ", pathIndex );
			strncat ( pathLine, tempString, 256 );
			FailIf ( NULL == ( thisPath = OSDynamicCast ( OSArray, thisGroup->getObject ( pathIndex ) ) ), Exit );
			for ( UInt8 pathItem = 0; pathItem < thisPath->getCount(); pathItem++ )
			{
				FailIf ( NULL == ( clockIDNum = ( OSNumber * ) thisPath->getObject ( pathItem ) ), Exit );
				snprintf ( tempString, 10, "%d ", clockIDNum->unsigned8BitValue ());
				strncat ( pathLine, tempString, 256 );
			}
			debugIOLog ( "  %s", pathLine );
		}
	}
	
Exit:
	if ( NULL != pathsFromClockEntityN )
	{
		pathsFromClockEntityN->release();
	}
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::buildClockGraph (%d) = %p", this, controlInterfaceNum, allClockPaths );
	return allClockPaths;
}

OSArray * AppleUSBAudioDevice::buildClockPath ( UInt8 controlInterfaceNum, UInt8 startingUnitID, OSArray * allPaths, OSArray * startingPath ) 
{
	OSArray *						curPath = NULL;
	OSArray *						sourceArray = NULL;
	OSNumber *						arrayNumber = NULL;
	OSNumber *						thisUnitIDNum;
	UInt8							thisUnitID;
	UInt8							numSources;
	UInt8							sourceID;
	UInt8							subType;
	
	debugIOLog ( "+ AppleUSBAudioDevice[%p]::buildClockPath ( %d, %d, %p, %p )", controlInterfaceNum, startingUnitID, allPaths, startingPath );
	FailIf ( NULL == mConfigDictionary, Exit );
	thisUnitID = startingUnitID;
	FailIf ( NULL == ( thisUnitIDNum = OSNumber::withNumber ( thisUnitID, 8 ) ), Exit);
	if ( NULL != startingPath ) 
	{
		curPath = OSArray::withArray ( startingPath );
	}
	if ( NULL == curPath ) 
	{
		curPath = OSArray::withObjects ( ( const OSObject ** ) &thisUnitIDNum, 1 );
	} 
	else 
	{
		curPath->setObject ( thisUnitIDNum );
	}
	thisUnitIDNum->release ();
	thisUnitIDNum = NULL;

	FailIf ( kIOReturnSuccess != mConfigDictionary->getSubType ( &subType, controlInterfaceNum, 0, thisUnitID ), Exit );

	while	(		( 0 != subType )
				&&  ( curPath )
				&&	( USBAUDIO_0200::CLOCK_SOURCE != subType ) )
	{
		if ( USBAUDIO_0200::CLOCK_SELECTOR == subType )						 
		{
			debugIOLog ( "    found clock selector @ ID %d", thisUnitID ); 
			FailIf ( kIOReturnSuccess != mConfigDictionary->getNumSources ( &numSources, controlInterfaceNum, 0, thisUnitID ), Exit );
			debugIOLog ( "    found clock selector %d has %d sources", thisUnitID, numSources ); 			
			FailIf ( kIOReturnSuccess != mConfigDictionary->getSourceIDs ( &sourceArray, controlInterfaceNum, 0, thisUnitID ), Exit );			
			for ( UInt8 sourceIndex = 0; sourceIndex < numSources; sourceIndex++ ) 
			{
				FailIf ( NULL == sourceArray, Exit );
				FailIf ( NULL == ( arrayNumber = OSDynamicCast ( OSNumber, sourceArray->getObject ( sourceIndex ) ) ), Exit);
				buildClockPath ( controlInterfaceNum, arrayNumber->unsigned8BitValue(), allPaths, curPath );
			}
			subType = 0;
		}
		else
		{
			// USBAUDIO_0200::CLOCK_MULTIPLIER:
			debugIOLog ( "    found clock multiplier @ ID %d", thisUnitID );
			if ( 1 != curPath->getCount () )
			{
				// We haven't added this yet. We should do so.
				thisUnitIDNum = OSNumber::withNumber ( thisUnitID, 8 );
				if ( NULL != thisUnitIDNum ) 
				{
					curPath->setObject ( thisUnitIDNum );
					thisUnitIDNum->release ();
					thisUnitIDNum = NULL;
				}
			}
			
			// Continue down the path.
			FailIf (kIOReturnSuccess != mConfigDictionary->getSourceID ( &sourceID, controlInterfaceNum, 0, thisUnitID ), Exit );
			thisUnitID = sourceID;
			FailIf ( kIOReturnSuccess != mConfigDictionary->getSubType ( &subType, controlInterfaceNum, 0, thisUnitID ), Exit );
		}
	} // while ( subType != 0 )

	if ( USBAUDIO_0200::CLOCK_SOURCE == subType )
	{
		debugIOLog ( "    found clock source @ ID %d", thisUnitID );
		// [rdar://4952145] This is the end of a clock path. We should set it now.
		debugIOLog (  "    adding path..." );
		allPaths->setObject ( curPath );
	}

Exit:
	if ( curPath )
	{
		curPath->release ();
		curPath = NULL;
	}
	debugIOLog ( "- AppleUSBAudioDevice[%p]::buildClockPath () = %p", curPath );
	return curPath;
}

OSArray * AppleUSBAudioDevice::BuildConnectionGraph (UInt8 controlInterfaceNum) 
{
	OSArray *						allOutputTerminalPaths = NULL;
	OSArray *						pathsFromOutputTerminalN = NULL;
	OSArray *						thisPath = NULL;
	UInt8							terminalIndex;
	UInt8							numTerminals;
	UInt8							terminalID;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::BuildConnectionGraph (%d)", this, controlInterfaceNum);
	allOutputTerminalPaths = OSArray::withCapacity (1);
	FailIf (NULL == allOutputTerminalPaths, Exit);
	pathsFromOutputTerminalN = OSArray::withCapacity (1);
	FailIf (NULL == pathsFromOutputTerminalN, Exit);
	FailIf (kIOReturnSuccess != mConfigDictionary->getNumOutputTerminals (&numTerminals, controlInterfaceNum, 0), Exit);
	for (terminalIndex = 0; terminalIndex < numTerminals; terminalIndex++) 
	{
		FailIf (kIOReturnSuccess != mConfigDictionary->getIndexedOutputTerminalID (&terminalID, controlInterfaceNum, 0, terminalIndex), Exit);
		BuildPath (controlInterfaceNum, terminalID, pathsFromOutputTerminalN, thisPath);
		allOutputTerminalPaths->setObject (pathsFromOutputTerminalN);
		pathsFromOutputTerminalN->release ();
		pathsFromOutputTerminalN = OSArray::withCapacity (1);
		FailIf (NULL == pathsFromOutputTerminalN, Exit);
	}
	
Exit:
	if (NULL != pathsFromOutputTerminalN)
	{
		pathsFromOutputTerminalN->release();
	}
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::BuildConnectionGraph (%d) = %p", this, controlInterfaceNum, allOutputTerminalPaths);
	return allOutputTerminalPaths;
}

OSArray * AppleUSBAudioDevice::BuildPath (UInt8 controlInterfaceNum, UInt8 startingUnitID, OSArray * allPaths, OSArray * startingPath) {
	OSArray *						curPath = NULL;
	OSArray *						tempPath = NULL;
	OSArray *						sourceArray = NULL;
	OSObject *						arrayObject = NULL;
	OSNumber *						arrayNumber = NULL;
	OSNumber *						thisUnitIDNum;
	UInt8							unitID;	
	// UInt8 *							sourceIDs = NULL;
	UInt32							i;
	UInt8							thisUnitID;
	UInt8							numSources;
	UInt8							sourceID;
	UInt8							startingSubType;
	UInt8							subType;
	UInt16							adcVersion;

	FailIf (kIOReturnSuccess != mConfigDictionary->getADCVersion (&adcVersion), Exit);
	
	thisUnitID = startingUnitID;
	FailIf (NULL == (thisUnitIDNum = OSNumber::withNumber (thisUnitID, 8)), Exit);
	if (NULL != startingPath) 
	{
		curPath = OSArray::withArray (startingPath);
	}
	if (NULL == curPath) 
	{
		curPath = OSArray::withObjects ((const OSObject **)&thisUnitIDNum, 1);
	} 
	else 
	{
		curPath->setObject (thisUnitIDNum);
	}
	thisUnitIDNum->release ();
	thisUnitIDNum = NULL;

	FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, thisUnitID), Exit);

	while (INPUT_TERMINAL != subType && subType != 0) 
	{
		if (((kAUAUSBSpec1_0 == adcVersion) && ((MIXER_UNIT == subType) || (SELECTOR_UNIT == subType) || (EXTENSION_UNIT == subType) || (PROCESSING_UNIT == subType))) ||
			((kAUAUSBSpec2_0 == adcVersion) && ((USBAUDIO_0200::MIXER_UNIT == subType) || (USBAUDIO_0200::SELECTOR_UNIT == subType) || (USBAUDIO_0200::EXTENSION_UNIT == subType) || (USBAUDIO_0200::PROCESSING_UNIT == subType))))						 
		{
			FailIf (kIOReturnSuccess != mConfigDictionary->getNumSources (&numSources, controlInterfaceNum, 0, thisUnitID), Exit);
			FailIf (kIOReturnSuccess != mConfigDictionary->getSourceIDs (&sourceArray, controlInterfaceNum, 0, thisUnitID), Exit);
			tempPath = OSArray::withArray (curPath);
			for (i = 0; i < numSources; i++) 
			{
				if (NULL == curPath) 
				{
					curPath = OSArray::withCapacity (1);
				}
				FailIf (NULL == curPath, Exit);
				FailIf (NULL == (arrayObject = sourceArray->getObject (i)), Exit);
				FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
				curPath = BuildPath (controlInterfaceNum, arrayNumber->unsigned8BitValue(), allPaths, tempPath);
				if (curPath && curPath->getCount ()) 
				{
					thisUnitIDNum = OSDynamicCast (OSNumber, curPath->getLastObject ());
					FailIf (NULL == thisUnitIDNum, Exit);
					unitID = thisUnitIDNum->unsigned8BitValue ();
					FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, unitID), Exit);
					if (unitID && subType == INPUT_TERMINAL) 
					{
						allPaths->setObject (curPath);
					}
				}
				if (curPath) 
				{
					curPath->release ();
					curPath = NULL;
				}
			}
			tempPath->release ();
			subType = 0;
		}
		else
		{
			// OUTPUT_TERMINAL, FEATURE_UNIT, EFFECT_UNIT:
			FailIf (kIOReturnSuccess != mConfigDictionary->getSourceID (&sourceID, controlInterfaceNum, 0, thisUnitID), Exit);
			thisUnitID = sourceID;
			thisUnitIDNum = OSNumber::withNumber (thisUnitID, 8);
			if (NULL != thisUnitIDNum) 
			{
				curPath->setObject (thisUnitIDNum);
				thisUnitIDNum->release ();
				thisUnitIDNum = NULL;
			}
			FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&subType, controlInterfaceNum, 0, thisUnitID), Exit);
			FailIf (kIOReturnSuccess != mConfigDictionary->getSubType (&startingSubType, controlInterfaceNum, 0, startingUnitID), Exit);
			if (subType == INPUT_TERMINAL && startingSubType == OUTPUT_TERMINAL) 
			{
				allPaths->setObject (curPath);
			}
		}
	} // while (INPUT_TERMINAL != subType && subType != 0)

Exit:
	return curPath;
}

char * AppleUSBAudioDevice::TerminalTypeString (UInt16 terminalType) 
{
	char *					terminalTypeString;

	switch (terminalType) {
		case 0x101:											terminalTypeString = (char*)"USB streaming";									break;
	#if LOCALIZABLE
		case INPUT_UNDEFINED:								terminalTypeString = (char*)"StringInputUndefined";							break;
		case INPUT_MICROPHONE:								terminalTypeString = (char*)"StringMicrophone";								break;
		case INPUT_DESKTOP_MICROPHONE:						terminalTypeString = (char*)"StringDesktopMicrophone";							break;
		case INPUT_PERSONAL_MICROPHONE:						terminalTypeString = (char*)"StringPersonalMicrophone";						break;
		case INPUT_OMNIDIRECTIONAL_MICROPHONE:				terminalTypeString = (char*)"StringOmnidirectionalMicrophone";					break;
		case INPUT_MICROPHONE_ARRAY:						terminalTypeString = (char*)"StringMicrophoneArray";							break;
		case INPUT_PROCESSING_MICROPHONE_ARRAY:				terminalTypeString = (char*)"StringProcessingMicrophoneArray";					break;
		case INPUT_MODEM_AUDIO:								terminalTypeString = (char*)"StringModemAudio";								break;
		case OUTPUT_UNDEFINED:								terminalTypeString = (char*)"StringOutputUndefined";							break;
		case OUTPUT_SPEAKER:								terminalTypeString = (char*)"StringSpeaker";									break;
		case OUTPUT_HEADPHONES:								terminalTypeString = (char*)"StringHeadphones";								break;
		case OUTPUT_HEAD_MOUNTED_DISPLAY_AUDIO:				terminalTypeString = (char*)"StringHeadMountedDisplayAudio";					break;
		case OUTPUT_DESKTOP_SPEAKER:						terminalTypeString = (char*)"StringDesktopSpeaker";							break;
		case OUTPUT_ROOM_SPEAKER:							terminalTypeString = (char*)"StringRoomSpeaker";								break;
		case OUTPUT_COMMUNICATION_SPEAKER:					terminalTypeString = (char*)"StringCommunicationSpeaker";						break;
		case OUTPUT_LOW_FREQUENCY_EFFECTS_SPEAKER:			terminalTypeString = (char*)"StringLowFrequencyEffectsSpeaker";				break;
		case BIDIRECTIONAL_UNDEFINED:						terminalTypeString = (char*)"StringBidirectionalUndefined";					break;
		case BIDIRECTIONAL_HANDSET:							terminalTypeString = (char*)"StringBidirectionalHandset";						break;
		case BIDIRECTIONAL_HEADSET:							terminalTypeString = (char*)"StringBidirectionalHeadset";						break;
		case BIDIRECTIONAL_SPEAKERPHONE_NO_ECHO_REDX:		terminalTypeString = (char*)"StringBidirectionalSpeakerphoneNoEchoRedx";		break;
		case BIDIRECTIONAL_ECHO_SUPPRESSING_SPEAKERPHONE:	terminalTypeString = (char*)"StringBidirectionalEchoSuppressingSpeakerphone";	break;
		case BIDIRECTIONAL_ECHO_CANCELING_SPEAKERPHONE:		terminalTypeString = (char*)"StringBidirectionalEchoCancelingSpeakerphone";	break;
		case TELEPHONY_UNDEFINED:							terminalTypeString = (char*)"StringTelephoneUndefined";						break;
		case TELEPHONY_PHONE_LINE:							terminalTypeString = (char*)"StringTelephoneLine";								break;
		case TELEPHONY_TELEPHONE:							terminalTypeString = (char*)"StringTelephone";									break;
		case TELEPHONY_DOWN_LINE_PHONE:						terminalTypeString = (char*)"StringDownLinePhone";								break;
		case EXTERNAL_UNDEFINED:							terminalTypeString = (char*)"StringExternalUndefined";							break;
		case EXTERNAL_ANALOG_CONNECTOR:						terminalTypeString = (char*)"StringExternalAnalogConnector";					break;
		case EXTERNAL_DIGITAL_AUDIO_INTERFACE:				terminalTypeString = (char*)"StringExternalDigitalAudioInterface";				break;
		case EXTERNAL_LINE_CONNECTOR:						terminalTypeString = (char*)"StringExternalLineConnector";						break;
		case EXTERNAL_LEGACY_AUDIO_CONNECTOR:				terminalTypeString = (char*)"StringExternalLegacyAudioConnector";				break;
		case EXTERNAL_SPDIF_INTERFACE:						terminalTypeString = (char*)"StringExternalSPDIFInterface";					break;
		case EXTERNAL_1394_DA_STREAM:						terminalTypeString = (char*)"StringExternal1394DAStream";						break;
		case EXTERNAL_1394_DV_STREAM_SOUNDTRACK:			terminalTypeString = (char*)"StringExternal1394DVStreamSoundtrack";			break;
		case EMBEDDED_UNDEFINED:							terminalTypeString = (char*)"StringEmbeddedUndefined";							break;
		case EMBEDDED_LEVEL_CALIBRATION_NOISE_SOURCE:		terminalTypeString = (char*)"StringEmbeddedLevelCalibrationNoiseSource";		break;
		case EMBEDDED_EQUALIZATION_NOISE:					terminalTypeString = (char*)"StringEmbeddedEqualizationNoise";					break;
		case EMBEDDED_CD_PLAYER:							terminalTypeString = (char*)"StringEmbeddedCDPlayer";							break;
		case EMBEDDED_DAT:									terminalTypeString = (char*)"StringEmbeddedDAT";								break;
		case EMBEDDED_DCC:									terminalTypeString = (char*)"StringEmbeddedDCC";								break;
		case EMBEDDED_MINIDISK:								terminalTypeString = (char*)"StringEmbeddedMiniDisc";							break;
		case EMBEDDED_ANALOG_TAPE:							terminalTypeString = (char*)"StringEmbeddedAnalogTape";						break;
		case EMBEDDED_PHONOGRAPH:							terminalTypeString = (char*)"StringEmbeddedPhonograph";						break;
		case EMBEDDED_VCR_AUDIO:							terminalTypeString = (char*)"StringEmbeddedVCRAudio";							break;
		case EMBEDDED_VIDEO_DISC_AUDIO:						terminalTypeString = (char*)"StringEmbeddedVideoDiscAudio";					break;
		case EMBEDDED_DVD_AUDIO:							terminalTypeString = (char*)"StringEmbeddedDVDAudio";							break;
		case EMBEDDED_TV_TUNER_AUDIO:						terminalTypeString = (char*)"StringEmbeddedTVTunerAudio";						break;
		case EMBEDDED_SATELLITE_RECEIVER_AUDIO:				terminalTypeString = (char*)"StringEmbeddedSatelliteReceiverAudio";			break;
		case EMBEDDED_CABLE_TUNER_AUDIO:					terminalTypeString = (char*)"StringEmbeddedCableTunerAudio";					break;
		case EMBEDDED_DSS_AUDIO:							terminalTypeString = (char*)"StringEmbeddedDSSAudio";							break;
		case EMBEDDED_RADIO_RECEIVER:						terminalTypeString = (char*)"StringEmbeddedRadioReceiver";						break;
		case EMBEDDED_RADIO_TRANSMITTER:					terminalTypeString = (char*)"StringEmbeddedRadioTransmitter";					break;
		case EMBEDDED_MULTITRACK_RECORDER:					terminalTypeString = (char*)"StringEmbeddedMultitrackRecorder";				break;
		case EMBEDDED_SYNTHESIZER:							terminalTypeString = (char*)"StringEmbeddedSynthesizer";						break;
		default:											terminalTypeString = (char*)"StringUnknown";									break;
	#else
		case INPUT_UNDEFINED:								terminalTypeString = (char*)"InputUndefined";									break;
		case INPUT_MICROPHONE:								terminalTypeString = (char*)"Microphone";										break;
		case INPUT_DESKTOP_MICROPHONE:						terminalTypeString = (char*)"Desktop Microphone";								break;
		case INPUT_PERSONAL_MICROPHONE:						terminalTypeString = (char*)"Personal Microphone";								break;
		case INPUT_OMNIDIRECTIONAL_MICROPHONE:				terminalTypeString = (char*)"Omnidirectional Microphone";						break;
		case INPUT_MICROPHONE_ARRAY:						terminalTypeString = (char*)"Microphone Array";								break;
		case INPUT_PROCESSING_MICROPHONE_ARRAY:				terminalTypeString = (char*)"Processing Microphone Array";						break;
		case INPUT_MODEM_AUDIO:								terminalTypeString = (char*)"Modem Audio";										break;
		case OUTPUT_UNDEFINED:								terminalTypeString = (char*)"Output Undefined";								break;
		case OUTPUT_SPEAKER:								terminalTypeString = (char*)"Speaker";											break;
		case OUTPUT_HEADPHONES:								terminalTypeString = (char*)"Headphones";										break;
		case OUTPUT_HEAD_MOUNTED_DISPLAY_AUDIO:				terminalTypeString = (char*)"Head Mounted Display Audio";						break;
		case OUTPUT_DESKTOP_SPEAKER:						terminalTypeString = (char*)"Desktop Speaker";									break;
		case OUTPUT_ROOM_SPEAKER:							terminalTypeString = (char*)"Room Speaker";									break;
		case OUTPUT_COMMUNICATION_SPEAKER:					terminalTypeString = (char*)"Communication Speaker";							break;
		case OUTPUT_LOW_FREQUENCY_EFFECTS_SPEAKER:			terminalTypeString = (char*)"Low Frequency Effects Speaker";					break;
		case BIDIRECTIONAL_UNDEFINED:						terminalTypeString = (char*)"Bidirectional Undefined";							break;
		case BIDIRECTIONAL_HANDSET:							terminalTypeString = (char*)"Bidirectional Handset";							break;
		case BIDIRECTIONAL_HEADSET:							terminalTypeString = (char*)"Bidirectional Headset";							break;
		case BIDIRECTIONAL_SPEAKERPHONE_NO_ECHO_REDX:		terminalTypeString = (char*)"Bidirectional Speakerphone No Echo Redx";			break;
		case BIDIRECTIONAL_ECHO_SUPPRESSING_SPEAKERPHONE:	terminalTypeString = (char*)"Bidirectional Echo Suppressing Speakerphone";		break;
		case BIDIRECTIONAL_ECHO_CANCELING_SPEAKERPHONE:		terminalTypeString = (char*)"Bidirectional Echo Canceling Speakerphone";		break;
		case TELEPHONY_UNDEFINED:							terminalTypeString = (char*)"Telephone Undefined";								break;
		case TELEPHONY_PHONE_LINE:							terminalTypeString = (char*)"Telephone Line";									break;
		case TELEPHONY_TELEPHONE:							terminalTypeString = (char*)"Telephone";										break;
		case TELEPHONY_DOWN_LINE_PHONE:						terminalTypeString = (char*)"Down Line Phone";									break;
		case EXTERNAL_UNDEFINED:							terminalTypeString = (char*)"External Undefined";								break;
		case EXTERNAL_ANALOG_CONNECTOR:						terminalTypeString = (char*)"External Analog Connector";						break;
		case EXTERNAL_DIGITAL_AUDIO_INTERFACE:				terminalTypeString = (char*)"External Digital Audio Interface";				break;
		case EXTERNAL_LINE_CONNECTOR:						terminalTypeString = (char*)"External Line Connector";							break;
		case EXTERNAL_LEGACY_AUDIO_CONNECTOR:				terminalTypeString = (char*)"External Legacy Audio Connector";					break;
		case EXTERNAL_SPDIF_INTERFACE:						terminalTypeString = (char*)"External SPDIF Interface";						break;
		case EXTERNAL_1394_DA_STREAM:						terminalTypeString = (char*)"External 1394 DA Stream";							break;
		case EXTERNAL_1394_DV_STREAM_SOUNDTRACK:			terminalTypeString = (char*)"External 1394 DV Stream Soundtrack";				break;
		case EMBEDDED_UNDEFINED:							terminalTypeString = (char*)"Embedded Undefined";								break;
		case EMBEDDED_LEVEL_CALIBRATION_NOISE_SOURCE:		terminalTypeString = (char*)"Embedded Level Calibration Noise Source";			break;
		case EMBEDDED_EQUALIZATION_NOISE:					terminalTypeString = (char*)"Embedded Equalization Noise";						break;
		case EMBEDDED_CD_PLAYER:							terminalTypeString = (char*)"Embedded CD Player";								break;
		case EMBEDDED_DAT:									terminalTypeString = (char*)"Embedded DAT";									break;
		case EMBEDDED_DCC:									terminalTypeString = (char*)"Embedded DCC";									break;
		case EMBEDDED_MINIDISK:								terminalTypeString = (char*)"Embedded Mini Disc";								break;
		case EMBEDDED_ANALOG_TAPE:							terminalTypeString = (char*)"Embedded Analog Tape";							break;
		case EMBEDDED_PHONOGRAPH:							terminalTypeString = (char*)"Embedded Phonograph";								break;
		case EMBEDDED_VCR_AUDIO:							terminalTypeString = (char*)"Embedded VCR Audio";								break;
		case EMBEDDED_VIDEO_DISC_AUDIO:						terminalTypeString = (char*)"Embedded Video Disc Audio";						break;
		case EMBEDDED_DVD_AUDIO:							terminalTypeString = (char*)"Embedded DVD Audio";								break;
		case EMBEDDED_TV_TUNER_AUDIO:						terminalTypeString = (char*)"Embedded TV Tuner Audio";							break;
		case EMBEDDED_SATELLITE_RECEIVER_AUDIO:				terminalTypeString = (char*)"Embedded Satellite Receiver Audio";				break;
		case EMBEDDED_CABLE_TUNER_AUDIO:					terminalTypeString = (char*)"Embedded Cable Tuner Audio";						break;
		case EMBEDDED_DSS_AUDIO:							terminalTypeString = (char*)"Embedded DSS Audio";								break;
		case EMBEDDED_RADIO_RECEIVER:						terminalTypeString = (char*)"Embedded Radio Receiver";							break;
		case EMBEDDED_RADIO_TRANSMITTER:					terminalTypeString = (char*)"Embedded Radio Transmitter";						break;
		case EMBEDDED_MULTITRACK_RECORDER:					terminalTypeString = (char*)"Embedded Multitrack Recorder";					break;
		case EMBEDDED_SYNTHESIZER:							terminalTypeString = (char*)"Embedded Synthesizer";							break;
		default:											terminalTypeString = (char*)"Unknown";											break;
	#endif
	}

	return terminalTypeString;
}

//	<rdar://5811247>
char * AppleUSBAudioDevice::ClockTypeString (UInt8 clockType) 
{
	char *					clockTypeString;

	switch (clockType) {
	#if LOCALIZABLE
		case USBAUDIO_0200::CLOCK_TYPE_EXTERNAL:							clockTypeString = (char*)"StringExternalClock";										break;
		case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_FIXED:						clockTypeString = (char*)"StringInternalFixedClock";								break;
		case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_VARIABLE:					clockTypeString = (char*)"StringInternalVariableClock";								break;
		case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_PROGRAMMABLE:				clockTypeString = (char*)"StringInternalProgrammableClock";							break;
		default:															clockTypeString = (char*)"StringUnknown";											break;
	#else
		case USBAUDIO_0200::CLOCK_TYPE_EXTERNAL:							clockTypeString = (char*)"External Clock";											break;
		case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_FIXED:						clockTypeString = (char*)"Internal Fixed Clock";									break;
		case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_VARIABLE:					clockTypeString = (char*)"Internal Variable Clock";									break;
		case USBAUDIO_0200::CLOCK_TYPE_INTERNAL_PROGRAMMABLE:				clockTypeString = (char*)"Internal Programmable Clock";								break;
		default:											clockTypeString = (char*)"Unknown";																	break;
	#endif
	}

	return clockTypeString;
}

IOReturn AppleUSBAudioDevice::deviceRequest (IOUSBDevRequestDesc * request, IOUSBCompletion * completion) {
	IOReturn						result;
	UInt32							timeout;
	Boolean							done;

	result = kIOReturnSuccess;
	FailIf (NULL == mInterfaceLock, Exit);
	IORecursiveLockLock (mInterfaceLock);

	if (FALSE == mTerminatingDriver) 
	{
		done = FALSE;
		timeout = 5;
		while (!done && timeout && mControlInterface) 
		{
			result = mControlInterface->DeviceRequest (request, completion);
			if (result != kIOReturnSuccess) 
			{
				timeout--;
				IOSleep (1);
			} 
			else 
			{
				done = TRUE;
			}
		}
    }
	IORecursiveLockUnlock (mInterfaceLock);
	#if LOGDEVICEREQUESTS
	debugIOLog ("? AppleUSBAudioDevice[%p]::deviceRequest (%p, %p) = %lx", this, request, completion, result);
	#endif
Exit:
	return result;
}

IOReturn AppleUSBAudioDevice::deviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion) {
	IOReturn						result;
	UInt32							timeout;
	Boolean							done;

	result = kIOReturnSuccess;
	FailIf (NULL == mInterfaceLock, Exit);
	IORecursiveLockLock (mInterfaceLock);

	if (FALSE == mTerminatingDriver) 
	{
		done = FALSE;
		timeout = 5;
		while (!done && timeout && mControlInterface) 
		{
			result = mControlInterface->DeviceRequest (request, completion);
			if (result != kIOReturnSuccess) 
			{
				timeout--;
				IOSleep (1);
			} 
			else 
			{
				done = TRUE;
			}
		}
    }
	IORecursiveLockUnlock (mInterfaceLock);
	#if LOGDEVICEREQUESTS
	debugIOLog ("? AppleUSBAudioDevice[%p]::deviceRequest (%p, %p) = %lx", this, request, completion, result);
	#endif
Exit:
	return result;
}

IOReturn AppleUSBAudioDevice::deviceRequest (IOUSBDevRequest *request, AppleUSBAudioDevice * self, IOUSBCompletion *completion) {
	IOReturn						result;
	UInt32							timeout;
	Boolean							done;

	result = kIOReturnSuccess;
	FailIf (NULL == self->mInterfaceLock, Exit);
	IORecursiveLockLock (self->mInterfaceLock);

	if (FALSE == self->mTerminatingDriver) 
	{
		done = FALSE;
		timeout = 5;
		while (!done && timeout && self->mControlInterface) 
		{
			result = self->mControlInterface->DeviceRequest (request, completion);
			if (result != kIOReturnSuccess) 
			{
				timeout--;
				IOSleep (1);
			} 
			else 
			{
				done = TRUE;
			}
		}
	}
	IORecursiveLockUnlock (self->mInterfaceLock);
	#if LOGDEVICEREQUESTS
	debugIOLog ("? AppleUSBAudioDevice[%p]::deviceRequest (%p, %p) = %lx", self, request, completion, result);
	#endif
Exit:
	return result;
}

bool AppleUSBAudioDevice::willTerminate (IOService * provider, IOOptionBits options) {
	debugIOLog ("+ AppleUSBAudioDevice[%p]::willTerminate (%p)", this, provider);

	if (mControlInterface == provider) 
	{
		mTerminatingDriver = TRUE;
	}
	
	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	if ( NULL != mInterruptPipe ) 
	{
		mInterruptPipe->Abort ();
		mInterruptPipe->release ();
		mInterruptPipe = NULL;
	}

	if ( mProcessStatusInterruptThread )
	{
		thread_call_cancel ( mProcessStatusInterruptThread );
		thread_call_free ( mProcessStatusInterruptThread );
		mProcessStatusInterruptThread = NULL;
	}

	debugIOLog ("- AppleUSBAudioDevice[%p]::willTerminate ()", this);

	return super::willTerminate (provider, options);
}

void AppleUSBAudioDevice::setConfigurationApp (const char *bundleID) {
	setConfigurationApplicationBundle (bundleID);
}

#ifdef DEBUG

void AppleUSBAudioDevice::retain() const
{
//    debugIOLog("AppleUSBAudioDevice(%p)::retain() - rc=%d", this, getRetainCount());
    super::retain();
}

void AppleUSBAudioDevice::release() const
{
//    debugIOLog("AppleUSBAudioDevice(%p)::release() - rc=%d", this, getRetainCount());
	super::release();
}

#endif

// Implemented for rdar://3993906 . Allows matching based on a custom dictionary.
bool AppleUSBAudioDevice::matchPropertyTable(OSDictionary * table, SInt32 *score)
{
	bool		returnValue = false;
	OSObject *	deviceName;
	
	// debugIOLog ("+AppleUSBAudioDevice[%p]::matchPropertyTable (%p, %p)", this, table, score);
	deviceName = table->getObject(kIOAudioDeviceNameKey);
	if (deviceName)
	{
		// This custom dictionary wants the device to have a name.
		if (getProperty (kIOAudioDeviceNameKey))
		{
			// No need to match name; it exists, and that's all that matters
			returnValue = true;
		}
		else
		{
			// Device doesn't have a name yet, so don't match
			returnValue = false;
		}
	}
	else
	{
		// This is our standard matchPropertyTable implementation
		returnValue = super::matchPropertyTable (table, score);
	}
	
	if (    (deviceName)
		 && (returnValue))
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::matchPropertyTable (%p, %p) = %d (custom dictionary match)", 
					this, table, score, returnValue);
	}
	
	return returnValue;
}

#pragma mark Anchored Time Stamps Methods

#if DEBUGANCHORS
void AppleUSBAudioDevice::accumulateAnchor (UInt64 anchorFrame, AbsoluteTime timeStamp)
{
	UInt32		i;
	
	if (mAnchorFrames[(kAnchorsToAccumulate - 1)] == 0ull)
	{
		// Find the last empty space and store this anchor frame and time stamp
		for ( i =  0; i < kAnchorsToAccumulate ; i++)
		{
			if (0 == mAnchorFrames[i])
			{
				mAnchorFrames[i] = anchorFrame;
				mAnchorTimes[i] = timeStamp;
				break;
			}
		}
	}
	
	if (mAnchorFrames[(kAnchorsToAccumulate - 1)] != 0ull)
	{
		UInt64	time_nanos;
		// Maximum number of anchors has already been accumulated.
		debugIOLog ("? AppleUSBAudioDevice::accumulateAnchor () - Frame # %d accumulated.", kAnchorsToAccumulate);
		for (i = 0; i < kAnchorsToAccumulate ; i++)
		{
			absolutetime_to_nanoseconds (mAnchorTimes[i], &time_nanos);
			debugIOLog ("  - %llu \t %llu", mAnchorFrames[i], time_nanos);
		}
		
		// Reset data
		for (i = 0; i < kAnchorsToAccumulate ; i++)
		{
			mAnchorFrames[i] = 0ull;
		}
	}
}
#endif

// <rdar://problem/7378275> Improved timestamp generation accuracy
IOReturn AppleUSBAudioDevice::getAnchorFrameAndTimeStamp (UInt64 *frame, AbsoluteTime *time) {
	AbsoluteTime	finishTime;
	AbsoluteTime	offset;
	AbsoluteTime	curTime;
	AbsoluteTime	prevTime;
	AbsoluteTime	thisTime;
	UInt64			thisFrame;
	AbsoluteTime	diffAbs;
	UInt64			diffNanos;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == mControlInterface, Exit);
	nanoseconds_to_absolutetime (1100000, &offset);
	clock_get_uptime (&finishTime);
	ADD_ABSOLUTETIME (&finishTime, &offset);	// finishTime is when we timeout
	
	clock_get_uptime ( &curTime );
	FailIf (NULL == mControlInterface->GetDevice(), Exit);
	FailIf (NULL == mControlInterface->GetDevice()->GetBus(), Exit);
	thisFrame = mControlInterface->GetDevice()->GetBus()->GetFrameNumber ();
	// spin until the frame changes
	do
	{
		prevTime = curTime;
		clock_get_uptime (&curTime);
	} while (	!mTerminatingDriver && (mControlInterface)		// <rdar://problem/7800198>
				&&	(thisFrame == mControlInterface->GetDevice()->GetBus()->GetFrameNumber ()) 
				&&	(CMP_ABSOLUTETIME (&finishTime, &curTime) > 0));

	clock_get_uptime (&thisTime);
	FailIf (CMP_ABSOLUTETIME (&finishTime, &curTime) < 0, Exit);		// if we timed out
	diffAbs = curTime;
	SUB_ABSOLUTETIME ( &diffAbs, &prevTime );
	absolutetime_to_nanoseconds ( diffAbs, &diffNanos );
	if ( kMaxTimestampJitter < diffNanos )	// this timestamp is an outlier, throw it out
	{
		goto Exit;
	}
	
	diffAbs = thisTime;
	SUB_ABSOLUTETIME ( &diffAbs, &curTime );
	absolutetime_to_nanoseconds ( diffAbs, &diffNanos );
	if ( kMaxTimestampJitter < diffNanos )	// this timestamp is an outlier, throw it out
	{
		goto Exit;
	}
	ADD_ABSOLUTETIME ( &thisTime, &curTime );
	AbsoluteTime_to_scalar ( &thisTime ) /= 2;
	
	*frame = ++thisFrame;
	*time = thisTime;	
	result = kIOReturnSuccess;
Exit:
	return result;
}

UInt64 AppleUSBAudioDevice::getWallTimeInNanos (void)
{
	AbsoluteTime	time;
	UInt64			timeInNanos;
	
	clock_get_uptime (&time);
	absolutetime_to_nanoseconds (time, &timeInNanos);
	
	return timeInNanos;
}

// From http://people.hofstra.edu/stefan_waner/realworld/calctopic1/regression.html:
// The best fit line associated with the n points (x1, y1), (x2, y2), . . . , (xn, yn) has the form
//    y = m *x + b
// slope:
//    m = ( n * sumXY - sumX * sumY ) / ( n * sumXX - sumX * sumX )
//      = P / Q 
//    where P = n * sumXY - sumX * sumY
//          Q = n * sumXX - sumX * sumX 
// intercept:
//    b = ( sumY - m * sumX ) / n
//
// y = m * x + b
//   = m * x + ( ( sumY - m * sumX ) / n )
//   = ( m * n * x + sumY - m * sumX ) / n
//   = ( m * ( n * x - sumX ) + sumY ) / n
//   = ( ( P / Q ) * ( n * x - sumX ) ) + sumY ) / n
//   = ( P * ( n * x - sumX ) + Q * sumY ) / ( Q * n )
//
// <rdar://problem/7378275> Improved timestamp generation accuracy
void updateAnchorTime ( ANCHORTIME * anchorTime, UInt64 X, UInt64 Y )
{
	UInt32 index = anchorTime->index;
	
#if DEBUGANCHORS	
	debugIOLog ("? AppleUSBAudioDevice::updateAnchorTime () - index: %u X: %llu Y: %llu", anchorTime->index, X, Y);
#endif	

	if ( anchorTime->n < MAX_ANCHOR_ENTRIES )
	{
		anchorTime->X [ index ] = X;
		anchorTime->Y [ index ] = Y;
		anchorTime->XX [ index ] = mul64 ( X, X );
		anchorTime->XY [ index ] = mul64 ( X, Y );
		
		anchorTime->sumX += X;
		anchorTime->sumY += Y;
		anchorTime->sumXX = add128 ( anchorTime->sumXX, anchorTime->XX [ index ] );
		anchorTime->sumXY = add128 ( anchorTime->sumXY, anchorTime->XY [ index ] );
		anchorTime->n++;
	}
	else
	{
		anchorTime->sumX -= anchorTime->X [ index ];
		anchorTime->X [ index ] = X;
		anchorTime->sumX += X;
		
		anchorTime->sumY -= anchorTime->Y [ index ];
		anchorTime->Y [ index ] = Y;
		anchorTime->sumY += Y;
		
		anchorTime->sumXX = sub128 ( anchorTime->sumXX, anchorTime->XX [ index ] );
		anchorTime->XX [ index ] = mul64 ( X, X );
		anchorTime->sumXX = add128 ( anchorTime->sumXX, anchorTime->XX [ index ] );
		
		anchorTime->sumXY = sub128 ( anchorTime->sumXY, anchorTime->XY [ index ] );
		anchorTime->XY [ index ] = mul64 ( X, Y );
		anchorTime->sumXY = add128 ( anchorTime->sumXY, anchorTime->XY [ index ] );
	}
	
	if ( anchorTime->n > 1 )
	{
		U256 nSumXY = mul128 ( anchorTime->n, anchorTime->sumXY );
		U128 sumXSumY = mul64 ( anchorTime->sumX, anchorTime->sumY );
#if (MAX_ANCHOR_ENTRIES <= 1024)
		anchorTime->P = sub128 ( nSumXY.lo, sumXSumY );
#else
		anchorTime->P = sub256 ( nSumXY, sumXSumY );
#endif
		
		U256 nSumXX = mul128 ( anchorTime->n, anchorTime->sumXX );
		U128 sumXSumX = mul64 ( anchorTime->sumX, anchorTime->sumX );
#if (MAX_ANCHOR_ENTRIES <= 1024)
		anchorTime->Q = sub128 ( nSumXX.lo, sumXSumX );
#else
		anchorTime->Q = sub256 ( nSumXX, sumXSumX );
#endif
		
#if (MAX_ANCHOR_ENTRIES <= 1024)
		anchorTime->QSumY = mul128 ( anchorTime->Q, anchorTime->sumY );		
		anchorTime->Qn = mul128 ( anchorTime->Q, anchorTime->n );
		anchorTime->mExtraPrecision = div256 ( mul128 ( anchorTime->P, kWallTimeExtraPrecision) , anchorTime->Q ).lo;
#else
		anchorTime->QSumY = mul256 ( anchorTime->Q, anchorTime->sumY ).lo;		
		anchorTime->Qn = mul256 ( anchorTime->Q, anchorTime->n ).lo;
		anchorTime->mExtraPrecision = div256 ( mul256 ( anchorTime->P, kWallTimeExtraPrecision).lo , anchorTime->Q ).lo;
#endif
	}
	else
	{
#if (MAX_ANCHOR_ENTRIES <= 1024)
		anchorTime->P.hi = 0; anchorTime->P.lo = 0;
		anchorTime->Q.hi = 0; anchorTime->Q.lo = 1;
#else
		anchorTime->P.hi.hi = 0; anchorTime->P.hi.lo = 0; anchorTime->P.lo.hi = 0; anchorTime->P.lo.lo = 0;
		anchorTime->Q.hi.hi = 0; anchorTime->Q.hi.lo = 0; anchorTime->Q.lo.hi = 0; anchorTime->Q.lo.lo = 1;
#endif
		anchorTime->mExtraPrecision.hi = anchorTime->mExtraPrecision.lo = 0;
	}
	
	anchorTime->index++;
	if ( anchorTime->index >= MAX_ANCHOR_ENTRIES )
	{
		anchorTime->index = 0;
	}
}

// This function should only be called if anchorTime->n > 1
UInt64 AppleUSBAudioDevice::getTimeForFrameNumber ( UInt64 frameNumber )
{
	UInt64 result = 0;
	
	if ( mAnchorTime.n > 1 )
	{
		// y = ( P * ( n * x - sumX ) + QSumY ) / Qn;
		// <rdar://problem/7711404> Split the calculation up to delay the subtraction to the last moment to avoid underflow.
		U128 nx = mul64 ( mAnchorTime.n, frameNumber );
#if (MAX_ANCHOR_ENTRIES <= 1024)
		U256 Pnx = mul128 ( mAnchorTime.P, nx.lo );
		U256 PsumX = mul128 ( mAnchorTime.P, mAnchorTime.sumX );
		U256 temp = add256 ( Pnx, mAnchorTime.QSumY );
		temp = sub256 ( temp, PsumX );
#else
		U512 Pnx = mul256 ( mAnchorTime.P, nx.lo );
		U512 PsumX = mul256 ( mAnchorTime.P, mAnchorTime.sumX );
		U256 temp = add256 ( Pnx.lo, mAnchorTime.QSumY );
		temp = sub256 ( temp, PsumX.lo );
#endif
		temp = div256 ( temp, mAnchorTime.Qn );
		result = temp.lo.lo;
	}
	
	return result;
}

UInt64 getUSBCycleTime ( ANCHORTIME * anchorTime )
{
	// The slope is the USB cycle time. This has the extra precision factor in it.
	return anchorTime->mExtraPrecision.lo;
}

void AppleUSBAudioDevice::updateUSBCycleTime ( void )
{
	AbsoluteTime	timeStamp;
	UInt64			frameNumber;
	
 	if ( ( kIOReturnSuccess == getAnchorFrameAndTimeStamp ( &frameNumber, &timeStamp ) ) && mTimeLock )
	{
		UInt64 timeStamp_nanos;
		absolutetime_to_nanoseconds ( timeStamp, &timeStamp_nanos );
#if DEBUGTIMESTAMPS
		debugIOLog ("   frameNumber = %llu, timeStamp = %llu\n", frameNumber, timeStamp);
#endif
		IOLockLock ( mTimeLock );
#if DEBUGTIMESTAMPS	 
		debugIOLog ("? AppleUSBAudioDevice::updateUSBCycleTime() mWallTimePerUSBCycle = %llu frames elapsed: %llu\n", mWallTimePerUSBCycle, frameNumber - lastAnchorFrame() );
#endif				
		updateAnchorTime ( &mAnchorTime, frameNumber, timeStamp_nanos );
#if DEBUGTIMESTAMPS		
		debugIOLog ("   mAnchorTime.n  = %u\n", mAnchorTime.n );
#endif
		AbsoluteTime refTime;
		if ( mAnchorTime.n > 1 )
		{
			UInt64 refTime_nanos = getTimeForFrameNumber ( frameNumber );
			nanoseconds_to_absolutetime ( refTime_nanos, &refTime );
#if DEBUGTIMESTAMPS
			debugIOLog ("   frameNumber = %llu, refTime_nanos = %llu\n", frameNumber, refTime_nanos);
#endif			
			mWallTimePerUSBCycle = getUSBCycleTime ( &mAnchorTime );
		}
		else 
		{
			mWallTimePerUSBCycle = 1000000ull * kWallTimeExtraPrecision;
		}
		
		IOLockUnlock ( mTimeLock );
#if DEBUGTIMESTAMPS		
		debugIOLog ("   New anchor! frameNumber = %llu, refTime = %llu\n", frameNumber, refTime);
		debugIOLog ("	 mWallTimePerUSBCycle = %llu\n", mWallTimePerUSBCycle );
#endif
	}
}

// <rdar://problem/7666699>
void AppleUSBAudioDevice::applyOffsetAmountToFilter ( void )
{
	UInt64			timeOffset = 0;
	bool			isPositive = TRUE;
	int				index = 0;
	int				numFilterPoints;
	AbsoluteTime	timeStamp;
	UInt64			currentFrame;
	
	if ( ( kIOReturnSuccess == getAnchorFrameAndTimeStamp ( &currentFrame, &timeStamp ) ) && mTimeLock )
	{
		UInt64 actualTime;
		absolutetime_to_nanoseconds ( timeStamp, &actualTime );
		
		IOLockLock ( mTimeLock );
		
		// offset = predicted - actual
		// predicted time = obtain from filtered data
		
		// don't apply offset unless a minimum number of frames have elapsed and there is data in the filter
		if ( ( ( currentFrame - lastAnchorFrame () ) > MIN_FRAMES_APPLY_OFFSET ) && ( mAnchorTime.n > 0 ) )
		{
			UInt64 predictedTime =  getTimeForFrameNumber ( currentFrame );
			
			if ( predictedTime >= actualTime )
			{
				// new offset is positive
				timeOffset = predictedTime - actualTime;
			}
			else 
			{
				// new offset is negative
				timeOffset = actualTime - predictedTime;
				isPositive = FALSE;
			}
#if DEBUGANCHORS						
			debugIOLog ("? AppleUSBAudioDevice::applyOffsetAmountToFilter timeOffset: %llu framesElapsed: %llu currentFrame: %llu predictedTime: %llu actualTime: %llu", 
						timeOffset, currentFrame - lastAnchorFrame (), currentFrame, predictedTime, actualTime );
#endif				
			numFilterPoints = mAnchorTime.n;
			
			// copy old values
			for ( index = 0; index < numFilterPoints; index++ )
			{
				Xcopy[index] = mAnchorTime.X[index];
				Ycopy[index] = mAnchorTime.Y[index];
			}
			
			// clear out all data from filter
			resetRateTimer ();
			
			// repopulate filter with old timestamp data with offset applied
			for ( index = 0; index < numFilterPoints; index++ )
			{
				updateAnchorTime ( &mAnchorTime, Xcopy[index], ( isPositive ? ( Ycopy[index] - timeOffset ) : ( Ycopy[index] + timeOffset ) ) );
			}
		}
		
		// add the anchor time obtained to calculate offset to the filter
		updateAnchorTime ( &mAnchorTime, currentFrame, actualTime );
		
		IOLockUnlock ( mTimeLock );
	}
}

// <rdar://problem/7666699> Check to see if system time has discontinuity by calculating offset to be applied to filtered cycle time 
void AppleUSBAudioDevice::calculateOffset ( void )
{
	debugIOLog ( "+ AppleUSBAudioDevice::calculateOffset (%p)", this );
	if ( allEnginesStopped () )
	{
		mRampUpdateCounter = 0;	// increase cycle time update frequency
		if ( mTimeLock && ( mAnchorTime.n >= MIN_ENTRIES_APPLY_OFFSET ) )
		{
			applyOffsetAmountToFilter ();
		}
	}
	debugIOLog ( "- AppleUSBAudioDevice::calculateOffset (%p)", this );
}

// [rdar://5165798] At initHardware() time and if anything goes wrong, this is how we reset the timer code.
void AppleUSBAudioDevice::resetRateTimer ()
{
	mWallTimePerUSBCycle = 1000000ull * kWallTimeExtraPrecision;
	bzero ( &mAnchorTime, sizeof ( ANCHORTIME ) );		// <rdar://problem/7378275>
	mAnchorTime.deviceStart = TRUE;						// <rdar://problem/7666699>
}

// <rdar://problem/7666699>
UInt64 AppleUSBAudioDevice::lastAnchorFrame ( void )
{
	return ( mAnchorTime.index ? mAnchorTime.X[mAnchorTime.index - 1] : mAnchorTime.X[MAX_ANCHOR_ENTRIES - 1] );
}

void AppleUSBAudioDevice::TimerAction (OSObject * owner, IOTimerEventSource * sender) 
{
	AppleUSBAudioDevice *	self;
	
	#if DEBUGTIMER
		debugIOLog ("+ AppleUSBAudioDevice::TimerAction (%p, %p)", owner, sender);
	#endif
	FailIf (NULL == owner, Exit);
	self = (AppleUSBAudioDevice *) owner;
	FailIf (NULL == self, Exit);
	self->doTimerAction ( sender );
Exit:
	#if DEBUGTIMER
		debugIOLog ("- AppleUSBAudioDevice::TimerAction ()");
	#endif
	return;
}

void AppleUSBAudioDevice::doTimerAction (IOTimerEventSource * timer) 
{
	// This method updates our running wall time per USB cycle every kRefreshInterval ms.
	// This timer thread is also used to perform routine watchdog-type events.
	
	UInt32 curRefreshInterval = kRefreshInterval;	// <rdar://problem/7378275>
	
	#if DEBUGTIMER
		debugIOLog ("+ AppleUSBAudioDevice::doTimerAction (%p)", timer);
	#endif
	FailIf (NULL == timer, Exit);
	
	// <rdar://problem/7666699>
	if ( mAnchorTime.deviceStart || !allEnginesStopped () )
	{
		updateUSBCycleTime ();							// <rdar://problem/7378275>
	}
	
	// <rdar://problem/7378275>, <rdar://problem/7666699> Determine the next timer firing time.  When the device first 
	//  enumerates and when streaming first starts, we sample anchor times at a high frequency until the least squares
	//  data array fills up.  Once it is full, we reduce to approximately 8 times per second.
	if ( mRampUpdateCounter < MAX_ANCHOR_ENTRIES )
	{
		curRefreshInterval = kAnchorSamplingFreq1;
	}
	else
	{
		curRefreshInterval = kRefreshInterval;
	}
	
	mRampUpdateCounter++;		// <rdar://problem/7666699>
	
	if ( ++mTimerCallCount >= ( kRefreshInterval / curRefreshInterval ) )		// Always perform these items at kRefreshInterval
	{
		// Perform any watchdog-type events here.
		if (mFailingAudioEngine)
		{
			debugIOLog ("! AppleUSBAudioDevice[%p]::doTimerAction () - Detected failing audio engine (%p)! Performing emergency format change.", this, mFailingAudioEngine);
			// Attempt to synchronize the input and output sample rates.
			formatChangeController (mFailingAudioEngine, NULL, NULL, NULL);
			mFailingAudioEngine = NULL;
			setSingleSampleRateDevice (true);
		}
		else if (		( mEngineToRestart )
					&&	( mEngineToRestart->mUSBStreamRunning ) )
		{
			// [rdar://5355808] If AppleUSBAudioEngine::CoalesceInputSamples () gets more than a USB frame's worth of samples behind, we need to restart the engine.
			debugIOLog ( "! AppleUSBAudioDevice[%p]::doTimerAction () - Restarting engine %p", this, mEngineToRestart );
			mEngineToRestart->pauseAudioEngine();
			mEngineToRestart->resumeAudioEngine();
			mEngineToRestart = NULL;
		}
		
		if (mShouldAttemptDeviceRecovery)
		{
			attemptDeviceRecovery ();
			mShouldAttemptDeviceRecovery = false;
		}
		
		//	<rdar://5811247> Run the polled task on the engine.
		if ( 0 != mEngineArray )
		{
			for ( UInt32 engineIndex = 0; engineIndex < mEngineArray->getCount (); engineIndex++ )
			{
				AppleUSBAudioEngine * engine = OSDynamicCast ( AppleUSBAudioEngine, mEngineArray->getObject ( engineIndex ) );
				if ( 0 != engine )
				{
					engine->runPolledTask ();
				}
			}
		}
		mTimerCallCount = 0;
	}
		
	// Schedule the next anchor frame and time update
	if (timer)
	{
		timer->setTimeoutMS ( curRefreshInterval );
	}
Exit:
	#if DEBUGTIMER
		debugIOLog ("- AppleUSBAudioDevice::doTimerAction ()");
	#endif
	return;
}

#pragma mark Format Change Methods

UInt32 AppleUSBAudioDevice::formatChangeController (IOAudioEngine *audioEngine, IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate)
{
	AppleUSBAudioEngine *			thisAudioEngine = NULL;
	AppleUSBAudioEngine *			otherAudioEngine = NULL;
	AppleUSBAudioStream *			thisStream = NULL;
	AppleUSBAudioStream *			otherStream = NULL;
	const IOAudioStreamFormat *		thisFormat;
	const IOAudioStreamFormat *		otherFormat;
	const IOAudioSampleRate *		thisSampleRate;
	const IOAudioSampleRate *		otherSampleRate;
	const IOAudioStreamFormat *		thisDefaultAudioStreamFormat;
	const IOAudioStreamFormat *		otherDefaultAudioStreamFormat;	
	UInt32							result = kAUAFormatChangeError;
	IOReturn						formatChangeReturnCode = kIOReturnError;
	UInt8							altSetting;
	bool							mustMatchFormats;
	bool							enginesPaused = false;
	Boolean							otherEngineNeedSampleRateChange = false;
	
	debugIOLog ("+ AppleUSBAudioDevice[%p]::formatChangeController (%p, %p, %p, %p)", this, audioEngine, audioStream, newFormat, newSampleRate);
	FailIf (NULL == mControlInterface, Exit);
	
	thisAudioEngine = (AppleUSBAudioEngine *) audioEngine;
	
	// [rdar://4801012]
	if ( IP_VERSION_02_00 == mControlInterface->GetInterfaceProtocol() )
	{
		if ( ( newFormat ) && ( newSampleRate ) )
		{
			getOptimalClockPath ( thisAudioEngine, (UInt8)(newFormat->fDriverTag >> 16), (UInt8)(newFormat->fDriverTag), newSampleRate->whole, &otherEngineNeedSampleRateChange );
		}
	}
	
	mustMatchFormats = (		(		( mRegisteredEngines) 
									&&	( 2 == mRegisteredEngines->getCount () )
									&&	(		( true == mSingleSampleRateDevice )
											||	( NULL == audioStream ) ) )
							||  ( true == otherEngineNeedSampleRateChange ) );
	
	if (mustMatchFormats)
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - Attempting to match this format with the format for the other stream interface.", this);
		result = kAUAFormatChangeForceFailure;
		
		if (NULL == thisAudioEngine)
		{
			// Get both engines.
			FailIf (kIOReturnSuccess != getBothEngines (&thisAudioEngine, &otherAudioEngine), Exit);
		}
		else
		{
			// Just get the other engine.
			otherAudioEngine = otherEngine (thisAudioEngine);
		}
		FailIf (NULL == thisAudioEngine, Exit);
		FailIf (NULL == otherAudioEngine, Exit);
		
		thisAudioEngine->pauseAudioEngine ();
		otherAudioEngine->pauseAudioEngine ();
		enginesPaused = true;
		
		// <rdar://7298196> Lock all audio streams for I/O since the format changes deallocate the sample buffer & 
		// related structure. This is to prevent a race condition between the format changes and performClientIO()/USB
		// callback completion routines.
		thisAudioEngine->lockAllStreams();
		otherAudioEngine->lockAllStreams();
		
		// Get formats and other sample rate.
		thisStream = thisAudioEngine->mMainOutputStream ? thisAudioEngine->mMainOutputStream : thisAudioEngine->mMainInputStream;
		otherStream = otherAudioEngine->mMainOutputStream ? otherAudioEngine->mMainOutputStream : otherAudioEngine->mMainInputStream;
		FailIf (NULL == thisStream, Exit);
		FailIf (NULL == otherStream, Exit);
		thisFormat = thisStream->getFormat ();
		otherFormat = otherStream->getFormat ();
		FailIf (NULL == thisFormat, Exit);
		FailIf (NULL == otherFormat, Exit);
		thisSampleRate = thisAudioEngine->getSampleRate ();
		otherSampleRate = otherAudioEngine->getSampleRate ();
		FailIf (NULL == thisSampleRate, Exit);
		FailIf (NULL == otherSampleRate, Exit);
		thisDefaultAudioStreamFormat = &thisStream->mDefaultAudioStreamFormat;
		otherDefaultAudioStreamFormat = &otherStream->mDefaultAudioStreamFormat;
		FailIf (NULL == thisDefaultAudioStreamFormat, Exit);
		FailIf (NULL == otherDefaultAudioStreamFormat, Exit);
		
		// Log what we have so far.
		debugIOLog ("\n");
		debugIOLog ("-------------------- BEFORE --------------------");
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - engine %p (interface %d, alternateSetting %d) info:", this, thisAudioEngine, thisStream->mInterfaceNumber, thisStream->mAlternateSettingID);
		debugIOLog ("    thisFormat = %p", thisFormat);
		debugIOLog ("        fNumChannels = %d", thisFormat->fNumChannels);
		debugIOLog ("        fBitDepth = %d", thisFormat->fBitDepth);
		debugIOLog ("        fDriverTag = 0x%x", thisFormat->fDriverTag);
		debugIOLog ("    thisSampleRate->whole = %lu", thisSampleRate->whole);
		debugIOLog ("\n");
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - engine %p (interface %d, alternateSetting %d) info:", this, otherAudioEngine, otherStream->mInterfaceNumber, otherStream->mAlternateSettingID);
		debugIOLog ("    otherFormat = %p", otherFormat);
		debugIOLog ("        fNumChannels = %d", otherFormat->fNumChannels);
		debugIOLog ("        fBitDepth = %d", otherFormat->fBitDepth);
		debugIOLog ("        fDriverTag = 0x%x", otherFormat->fDriverTag);
		debugIOLog ("    otherSampleRate->whole = %lu", otherSampleRate->whole);
		debugIOLog ("\n");
		debugIOLog (" AppleUSBAudioDevice[%p]::formatChangeController () - newFormat = %p", this, newFormat);
		if (newFormat)
		{
			debugIOLog ("        fNumChannels = %d", newFormat->fNumChannels);
			debugIOLog ("        fBitDepth = %d", newFormat->fBitDepth);
			debugIOLog ("        fDriverTag = 0x%x", newFormat->fDriverTag);
		}
		debugIOLog ( " AppleUSBAudioDevice[%p]::formatChangeController () - newSampleRate = %p", this, newSampleRate );
		if ( newSampleRate )
		{
			debugIOLog ("        whole = %d", newSampleRate->whole );
		}
		
		debugIOLog ("------------------------------------------------");
		debugIOLog ("\n");
		
		if ( ( false == mSingleSampleRateDevice ) && ( false == otherEngineNeedSampleRateChange ) )
		{
			// This is an emergency format change. We need to determine which engine(s) need(!s) to get a format change. Our order of preferability is:
			//		1. This engine should change if the sample rate can be matched at the current bit depth and channel count. If not,
			//		2. The other engine should change if this engine's reported sample rate can be matched at the other engine's current bit depth and channel count. If not,
			//		3. Both engines should return to their default settings, which had better be compatible.
			
			if (kIOReturnSuccess == mConfigDictionary->getAltSettingWithSettings (&altSetting, thisStream->mInterfaceNumber, thisFormat->fNumChannels, thisFormat->fBitDepth, otherSampleRate->whole))
			{
				// We'll change this engine only.
				formatChangeReturnCode = thisAudioEngine->controlledFormatChange (NULL, NULL, otherSampleRate);
				if (kIOReturnSuccess == formatChangeReturnCode)
				{
					debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - This engine (%p) sample rate changed successfully to %lu.", this, thisAudioEngine, otherSampleRate->whole);
					result = kAUAFormatChangeForced;
					thisAudioEngine->hardwareSampleRateChanged (otherSampleRate);
				}
			}
			else if (kIOReturnSuccess == mConfigDictionary->getAltSettingWithSettings (&altSetting, otherStream->mInterfaceNumber, otherFormat->fNumChannels, otherFormat->fBitDepth, thisSampleRate->whole))
			{
				// We'll change the other engine only.
				formatChangeReturnCode = otherAudioEngine->controlledFormatChange (NULL, NULL, thisSampleRate);
				if (kIOReturnSuccess == formatChangeReturnCode)
				{
					debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - Other engine (%p) sample rate changed succsesfully to %lu.", this, otherAudioEngine, thisSampleRate->whole);
					result = kAUAFormatChangeForced;
					otherAudioEngine->hardwareSampleRateChanged (thisSampleRate);
				}
			}
			else
			{
				// We'll restore both engines to their default settings.
				debugIOLog ("! AppleUSBAudioDevice[%p]::formatChangeController () - Restoring both engines to their default settings.", this);
				thisStream->setFormat ( thisDefaultAudioStreamFormat, false );
				formatChangeReturnCode = thisAudioEngine->controlledFormatChange (thisStream, thisDefaultAudioStreamFormat, &(thisAudioEngine->mDefaultAudioSampleRate));
				if (kIOReturnSuccess == formatChangeReturnCode)
				{
					debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - This engine (%p) restored to default settings succsesfully.", this, thisAudioEngine);
					result = kAUAFormatChangeForced;
					thisAudioEngine->hardwareSampleRateChanged (&(thisAudioEngine->mDefaultAudioSampleRate));
				}
				otherStream->setFormat ( otherDefaultAudioStreamFormat, false );
				formatChangeReturnCode = otherAudioEngine->controlledFormatChange (otherStream, otherDefaultAudioStreamFormat, &(otherAudioEngine->mDefaultAudioSampleRate));
				if (kIOReturnSuccess == formatChangeReturnCode)
				{
					debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - Other engine (%p) restored to default settings succsesfully.", this, otherAudioEngine);
					otherAudioEngine->hardwareSampleRateChanged (&(otherAudioEngine->mDefaultAudioSampleRate));
				}
				else
				{
					result = kAUAFormatChangeForceFailure;
				}
			}
		} // Emergency format change case
		else
		{
			// This device is already known to be a single-sample rate device. If the sample rate is changing, change it for both engines or change it for neither.
			if (		(newSampleRate)
					&&	(newSampleRate->whole != otherSampleRate->whole))
			{
				// See if we can the change the other sample rate at the current format.
				if (kIOReturnSuccess == mConfigDictionary->getAltSettingWithSettings (&altSetting, otherStream->mInterfaceNumber, otherFormat->fNumChannels, otherFormat->fBitDepth, newSampleRate->whole))				
				{
					// Issue format changes to both engines.
					thisStream->setFormat ( newFormat, false );
					formatChangeReturnCode = thisAudioEngine->controlledFormatChange (thisStream, newFormat, newSampleRate);
					if (kIOReturnSuccess == formatChangeReturnCode)
					{
						result = kAUAFormatChangeForced;
						thisAudioEngine->hardwareSampleRateChanged (newSampleRate);
						formatChangeReturnCode = otherAudioEngine->controlledFormatChange (otherStream, otherFormat, newSampleRate);
						if (kIOReturnSuccess != formatChangeReturnCode)
						{
							result = kAUAFormatChangeForceFailure;
						}
						else
						{
							otherAudioEngine->hardwareSampleRateChanged (newSampleRate);
						}
					}
				}
				else
				{
					// Fail this request.
					debugIOLog ("! AppleUSBAudioDevice[%p]::formatChangeController () - Other audio engine (%p) does not support sample rate %lu at %d bit %d channel(s). Failing.", 
								this, otherAudioEngine, newSampleRate->whole, otherFormat->fBitDepth, otherFormat->fNumChannels);
					result = kAUAFormatChangeForceFailure;
				}
			}
			else
			{
				// The sample rate isn't changing, so process this request normally.
				result = kAUAFormatChangeNormal;
				thisStream->setFormat ( newFormat, false );
				formatChangeReturnCode = (thisAudioEngine->controlledFormatChange (audioStream, newFormat, newSampleRate));
				// [rdar://5204813] Sending a null pointer here can cause a kernel panic. This should be unnecessary since the sample rate didn't change.
				// thisAudioEngine->hardwareSampleRateChanged (newSampleRate);
			}
		}
		
		FailIf (NULL == thisAudioEngine, Exit);
		FailIf (NULL == otherAudioEngine, Exit);
		thisStream = thisAudioEngine->mMainOutputStream ? thisAudioEngine->mMainOutputStream : thisAudioEngine->mMainInputStream;
		otherStream = otherAudioEngine->mMainOutputStream ? otherAudioEngine->mMainOutputStream : otherAudioEngine->mMainInputStream;
		FailIf (NULL == thisStream, Exit);
		FailIf (NULL == otherStream, Exit);
		thisFormat = thisStream->getFormat ();
		otherFormat = otherStream->getFormat ();
		FailIf (NULL == thisFormat, Exit);
		FailIf (NULL == otherFormat, Exit);
		
		// Log the new values.
		debugIOLog ("\n");
		debugIOLog ("-------------------- AFTER --------------------");
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - engine %p (interface %d, alternateSetting %d) info:", this, thisAudioEngine, thisStream->mInterfaceNumber, thisStream->mAlternateSettingID);
		debugIOLog ("    thisFormat = %p", thisFormat);
		debugIOLog ("        fNumChannels = %d", thisFormat->fNumChannels);
		debugIOLog ("        fBitDepth = %d", thisFormat->fBitDepth);
		debugIOLog ("        fDriverTag = 0x%x", thisFormat->fDriverTag);
		debugIOLog ("    thisSampleRate->whole = %lu", thisSampleRate->whole);
		debugIOLog ("\n");
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - engine %p (interface %d, alternateSetting %d) info:", this, otherAudioEngine, otherStream->mInterfaceNumber, otherStream->mAlternateSettingID);
		debugIOLog ("    otherFormat = %p", otherFormat);
		debugIOLog ("        fNumChannels = %d", otherFormat->fNumChannels);
		debugIOLog ("        fBitDepth = %d", otherFormat->fBitDepth);
		debugIOLog ("        fDriverTag = 0x%x", otherFormat->fDriverTag);
		debugIOLog ("    otherSampleRate->whole = %lu", otherSampleRate->whole);
		debugIOLog ("-----------------------------------------------");
		debugIOLog ("\n");
	}
	else
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - Attempting normal format change request.", this);

		thisAudioEngine->pauseAudioEngine ();	// <rdar://7535236>

		// <rdar://7298196> Lock all audio streams for I/O since the format changes deallocate the sample buffer & 
		// related structure. This is to prevent a race condition between the format changes and performClientIO()/USB
		// callback completion routines.
		thisAudioEngine->lockAllStreams();
		
		result = kAUAFormatChangeNormal;
		// Issue this format change request.
		formatChangeReturnCode = thisAudioEngine->controlledFormatChange (audioStream, newFormat, newSampleRate);

		// <rdar://7298196> Unlock the audio streams for I/O.
		thisAudioEngine->unlockAllStreams();
		
		thisAudioEngine->resumeAudioEngine ();	// <rdar://7535236>
	}
	
	if (kIOReturnSuccess != formatChangeReturnCode)
	{
		result = kAUAFormatChangeError;
		debugIOLog ("! AppleUSBAudioDevice[%p]::formatChangeController () - This format change failed with error 0x%x.", this, result);
	}
	else
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::formatChangeController () - This format change was successful.", this);
		if	(		(kAUAFormatChangeNormal != result)
				&&	(kAUAFormatChangeForced != result))
		{
			debugIOLog ("! AppleUSBAudioDevice[%p]::formatChangeController () - Forced format change failed.", this);
			result = kAUAFormatChangeForceFailure;
		}
	}
	
Exit:
	if (enginesPaused)
	{
		// <rdar://7298196> Unlock the audio streams for I/O.
		otherAudioEngine->unlockAllStreams();
		thisAudioEngine->unlockAllStreams();

		thisAudioEngine->resumeAudioEngine ();
		otherAudioEngine->resumeAudioEngine ();
	}
	debugIOLog ("- AppleUSBAudioDevice[%p]::formatChangeController (%p, %p, %p, %p) = 0x%x", this, audioEngine, audioStream, newFormat, newSampleRate, result);
	return result;
}

AppleUSBAudioEngine * AppleUSBAudioDevice::otherEngine (AppleUSBAudioEngine * thisEngine)
{
	SInt32	engineIndex;
	SInt32	otherEngineIndex;
	OSDictionary * otherAudioEngineInfo = NULL;
	AppleUSBAudioEngine * otherAudioEngine = NULL;
	
	FailIf (NULL == thisEngine, Exit);
	FailIf (NULL == mRegisteredEngines, Exit);
	engineIndex = getEngineInfoIndex (thisEngine);
		
	// Must stop here if we didn't find this engine.
	FailIf (-1 == engineIndex, Exit);
	
	otherEngineIndex = ((1 == engineIndex) ? 0 : 1);
	// The two engine indeces should be 0 and 1, so we'll NOT this engine to get the other one.
	otherAudioEngineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (otherEngineIndex));
	FailIf (NULL == otherAudioEngineInfo, Exit);
		
	otherAudioEngine = OSDynamicCast (AppleUSBAudioEngine, otherAudioEngineInfo->getObject (kEngine));
	FailIf (NULL == otherAudioEngine, Exit);
Exit:
	return otherAudioEngine;
}

IOReturn AppleUSBAudioDevice::getBothEngines (AppleUSBAudioEngine ** firstEngine, AppleUSBAudioEngine ** secondEngine)
{
	SInt32			engineIndex;
	OSDictionary *	firstAudioEngineInfo = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == mRegisteredEngines, Exit);
	engineIndex = 0;
	
	firstAudioEngineInfo = OSDynamicCast (OSDictionary, mRegisteredEngines->getObject (engineIndex));
	// Must stop here if we didn't find this engine's dictionary.
	FailIf (NULL == firstAudioEngineInfo, Exit);
	*firstEngine = OSDynamicCast (AppleUSBAudioEngine, firstAudioEngineInfo->getObject (kEngine));
	FailIf (NULL == *firstEngine, Exit);
	
	// Now that we have the firstEngine, get the second.
	*secondEngine = otherEngine (*firstEngine);
	FailIf (NULL == secondEngine, Exit);
	result = kIOReturnSuccess;
Exit:
	return result;

}

#pragma mark Device Recovery

void AppleUSBAudioDevice::attemptDeviceRecovery ()
{
	debugIOLog ("+ AppleUSBAudioDevice[%p]::attemptDeviceRecovery ()", this);
	
	FailIf (NULL == mControlInterface, Exit);
	/*
	debugIOLog ("? AppleUSBAudioDevice[%p]::attemptDeviceRecovery () - Issuing device reset.", this);
	mControlInterface->GetDevice()->ResetDevice();
	IOSleep (10);
	*/
	
Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::attemptDeviceRecovery ()", this);
	
	
	
}

#pragma mark Clock Entity Requests

IOReturn AppleUSBAudioDevice::getClockSetting (UInt8 controlSelector, UInt8 unitID, UInt8 requestType, void * target, UInt16 length) {
    IOReturn							result;
	IOUSBDevRequestDesc					devReq;
	UInt16								theSetting;
	IOBufferMemoryDescriptor *			theSettingDesc = NULL;

	result = kIOReturnError;
	// Initialize theSetting so that 
	theSetting = 0;
	FailIf (NULL == target, Exit);
	FailIf (NULL == mControlInterface, Exit);

	theSettingDesc = IOBufferMemoryDescriptor::withOptions (kIODirectionIn, length);
	FailIf (NULL == theSettingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBIn, kUSBClass, kUSBInterface);
	devReq.bRequest = requestType;
	devReq.wValue = (controlSelector << 8);
    devReq.wIndex = (0xFF00 & (unitID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = length;
    devReq.pData = theSettingDesc;

	result = deviceRequest (&devReq);
	FailIf (kIOReturnSuccess != result, Exit);
	
	if (NULL != target) 
	{
		memcpy (target, theSettingDesc->getBytesNoCopy (), length);
	}
	
Exit:
	if (NULL != theSettingDesc) 
	{
		theSettingDesc->release ();
	}
	return result;
}

IOReturn AppleUSBAudioDevice::setClockSetting (UInt8 controlSelector, UInt8 unitID, UInt8 requestType, void * target, UInt16 length) {
    IOUSBDevRequestDesc					devReq;
	IOBufferMemoryDescriptor *			theSettingDesc = NULL;
	IOReturn							result;

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	theSettingDesc = IOBufferMemoryDescriptor::withBytes (target, length, kIODirectionOut);
	FailIf (NULL == theSettingDesc, Exit);

    devReq.bmRequestType = USBmakebmRequestType (kUSBOut, kUSBClass, kUSBInterface);
    devReq.bRequest = requestType;
    devReq.wValue = (controlSelector << 8);
    devReq.wIndex = (0xFF00 & (unitID << 8)) | (0x00FF & mControlInterface->GetInterfaceNumber ());
    devReq.wLength = length;
    devReq.pData = theSettingDesc;

	FailIf ((TRUE == isInactive()), DeviceInactive);  	// In case we've been unplugged during sleep
	result = deviceRequest (&devReq);

Exit:
	if (NULL != theSettingDesc) 
	{
		theSettingDesc->release ();
	}
	return result;
	
DeviceInactive:
	debugIOLog("? AppleUSBAudioDevice::setClockSourceSetting () - ERROR attempt to send a device request to an inactive device");
	goto Exit;
}

IOReturn AppleUSBAudioDevice::getNumClockSourceSamplingFrequencySubRanges (UInt8 unitID, UInt16 * numSubRanges) {
	struct {
	UInt16								wNumSubRanges;
	SubRange32							SubRanges[1];
	}									rangeParameterBlock;
	IOReturn							result;

	rangeParameterBlock.wNumSubRanges = 0;

	result = kIOReturnError;
	FailIf (NULL == numSubRanges, Exit);
	
	result = getClockSetting (USBAUDIO_0200::CS_SAM_FREQ_CONTROL, unitID, USBAUDIO_0200::RANGE, &rangeParameterBlock, sizeof(rangeParameterBlock));
	FailIf (kIOReturnSuccess != result, Exit);
	
Exit:
	if (NULL != numSubRanges) 
	{
		*numSubRanges = USBToHostWord (rangeParameterBlock.wNumSubRanges);
	}

	return result;
}

IOReturn AppleUSBAudioDevice::getIndexedClockSourceSamplingFrequencySubRange (UInt8 unitID, SubRange32 * subRange, UInt16 subRangeIndex) {
	struct {
	UInt16								wNumSubRanges;
	SubRange32							SubRanges[1];
	}									rangeParameterBlock;
	void *								theRangeParameterBlock;
	UInt32								theRangeParameterBlockLength;
	SubRange32 *						theSubRanges;
	IOReturn							result;

	rangeParameterBlock.wNumSubRanges = 0;
	theRangeParameterBlock = NULL;

	result = kIOReturnError;
	FailIf (NULL == subRange, Exit);
	
	result = getClockSetting (USBAUDIO_0200::CS_SAM_FREQ_CONTROL, unitID, USBAUDIO_0200::RANGE, &rangeParameterBlock, sizeof(rangeParameterBlock));
	FailIf (kIOReturnSuccess != result, Exit);
	
	rangeParameterBlock.wNumSubRanges = USBToHostWord (rangeParameterBlock.wNumSubRanges);

	result = kIOReturnError;
	FailIf (subRangeIndex >= rangeParameterBlock.wNumSubRanges, Exit);

	theRangeParameterBlockLength = 2 + (rangeParameterBlock.wNumSubRanges * sizeof(SubRange32));	
	theRangeParameterBlock = IOMalloc(theRangeParameterBlockLength);
	FailIf (NULL == theRangeParameterBlock, Exit);
	
	result = getClockSetting (USBAUDIO_0200::CS_SAM_FREQ_CONTROL, unitID, USBAUDIO_0200::RANGE, theRangeParameterBlock, theRangeParameterBlockLength);
	FailIf (kIOReturnSuccess != result, Exit);
	
	theSubRanges = (SubRange32 *)(((UInt8 *) theRangeParameterBlock) + 2);
	
	subRange->dMIN = USBToHostLong (theSubRanges[subRangeIndex].dMIN);
	subRange->dMAX = USBToHostLong (theSubRanges[subRangeIndex].dMAX);
	subRange->dRES = USBToHostLong (theSubRanges[subRangeIndex].dRES);
	
Exit:
	if (NULL != theRangeParameterBlock) 
	{
		IOFree (theRangeParameterBlock, theRangeParameterBlockLength);
	}	

	return result;
}

IOReturn AppleUSBAudioDevice::getCurClockSourceSamplingFrequency (UInt8 unitID, UInt32 * samplingFrequency, bool * validity) {
	UInt32								clockFrequency;
	Boolean								clockValidity;
	bool								hasValidityControl;			// <rdar://7446555>
	IOReturn							result = kIOReturnError;	// <rdar://7446555>

	FailIf ( NULL == mControlInterface, Exit );						// <rdar://7446555>
	FailIf ( NULL == mConfigDictionary, Exit );						// <rdar://7446555>
	
	clockValidity = false;

	result = getClockSetting (USBAUDIO_0200::CS_SAM_FREQ_CONTROL, unitID, USBAUDIO_0200::CUR, &clockFrequency, 4);

	FailIf (kIOReturnSuccess != result, Exit);

	hasValidityControl = mConfigDictionary->clockSourceHasValidityControl ( mControlInterface->GetInterfaceNumber (), 0, unitID );	// <rdar://7446555>
	
	// <rdar://7446555> If the clock source has validity control, ask it for the value, otherwise assume it is there.
	if ( hasValidityControl )
	{
		result = getClockSetting (USBAUDIO_0200::CS_CLOCK_VALID_CONTROL, unitID, USBAUDIO_0200::CUR, &clockValidity, 1);
		FailIf (kIOReturnSuccess != result, Exit);
	}
	else 
	{
		clockValidity = true;
	}
Exit:
	if (NULL != samplingFrequency) 
	{
		*samplingFrequency = USBToHostLong (clockFrequency);
	}

	if (NULL != validity) 
	{
		*validity = clockValidity;
	}

	return result;
}

IOReturn AppleUSBAudioDevice::setCurClockSourceSamplingFrequency (UInt8 unitID, UInt32 samplingFrequency) {
	UInt32								frequency;
	
	frequency = USBToHostLong(samplingFrequency);
		
	return setClockSetting (USBAUDIO_0200::CS_SAM_FREQ_CONTROL, unitID, USBAUDIO_0200::CUR, &frequency, 4);
}

IOReturn AppleUSBAudioDevice::getCurClockSelector (UInt8 unitID, UInt8 * selector) {
	return getClockSetting (USBAUDIO_0200::CX_CLOCK_SELECTOR_CONTROL, unitID, USBAUDIO_0200::CUR, selector, 1);
}

IOReturn AppleUSBAudioDevice::setCurClockSelector (UInt8 unitID, UInt8 selector) {
	return setClockSetting (USBAUDIO_0200::CX_CLOCK_SELECTOR_CONTROL, unitID, USBAUDIO_0200::CUR, &selector, 1);
}

IOReturn AppleUSBAudioDevice::getCurClockMultiplier (UInt8 unitID, UInt16 * numerator, UInt16 * denominator) {
	UInt8								clockNumerator;
	Boolean								clockDenominator;
	IOReturn							result;

	result = getClockSetting (USBAUDIO_0200::CM_NUMERATOR_CONTROL, unitID, USBAUDIO_0200::CUR, &clockNumerator, 2);

	FailIf (kIOReturnSuccess != result, Exit);

	result = getClockSetting (USBAUDIO_0200::CM_DENOMINATOR_CONTROL, unitID, USBAUDIO_0200::CUR, &clockDenominator, 2);

	FailIf (kIOReturnSuccess != result, Exit);

Exit:
	if (NULL != numerator) 
	{
		*numerator = USBToHostWord (clockNumerator);
	}

	if (NULL != denominator) 
	{
		*denominator = USBToHostWord (clockDenominator);
	}

	return result;
}

#pragma mark Clock Path Sample Rates Discovery and Requests

IOReturn AppleUSBAudioDevice::getNumSampleRatesForClockPath (UInt8 * numSampleRates, OSArray * clockPath) {
	OSObject *							arrayObject = NULL;
	OSNumber *							arrayNumber = NULL;
	UInt8								clockID;
	UInt8								subType;
	UInt16								numSubRanges;
	IOReturn							result;

	numSubRanges = 0;

	result = kIOReturnError;
	FailIf (NULL == numSampleRates, Exit);
	FailIf (NULL == mControlInterface, Exit);
	
	// The last object in the clock path is the clock source.
	FailIf (NULL == (arrayObject = clockPath->getLastObject ()), Exit);
	FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
	clockID = arrayNumber->unsigned8BitValue();
	
	FailIf (kIOReturnSuccess != (result = mConfigDictionary->getSubType (&subType, mControlInterface->GetInterfaceNumber(), 0, clockID)), Exit);
	
	if (USBAUDIO_0200::CLOCK_SOURCE == subType)
	{
		FailIf (kIOReturnSuccess !=  (result = getNumClockSourceSamplingFrequencySubRanges (clockID, &numSubRanges)), Exit);
	}

Exit:
	if (NULL != numSampleRates)
	{
		*numSampleRates = numSubRanges;
	}
	
	return result;
}

IOReturn AppleUSBAudioDevice::getIndexedSampleRatesForClockPath (SubRange32 * sampleRates, OSArray * clockPath, UInt32 rangeIndex) {
	OSObject *							arrayObject = NULL;
	OSNumber *							arrayNumber = NULL;
	UInt32								clockIndex;
	UInt8								clockID;
	UInt8								subType;
	SubRange32							subRange;
	UInt16								numerator;
	UInt16								denominator;
	IOReturn							result;

	subRange.dMIN = subRange.dMAX = subRange.dRES = 0;
	
	result = kIOReturnError;
	FailIf (NULL == sampleRates, Exit);
	FailIf (NULL == mControlInterface, Exit);		// <rdar://7085810>
	
	for (clockIndex = clockPath->getCount (); clockIndex > 0 ; clockIndex--) 
	{
		FailIf (NULL == (arrayObject = clockPath->getObject (clockIndex - 1)), Exit);
		FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
		clockID = arrayNumber->unsigned8BitValue();
		
		FailIf (kIOReturnSuccess != (result = mConfigDictionary->getSubType (&subType, mControlInterface->GetInterfaceNumber(), 0, clockID)), Exit);
		
		if (USBAUDIO_0200::CLOCK_SOURCE == subType)
		{
			FailIf (kIOReturnSuccess !=  (result = getIndexedClockSourceSamplingFrequencySubRange (clockID, &subRange, rangeIndex)), Exit);
		}
		else if (USBAUDIO_0200::CLOCK_MULTIPLIER == subType)
		{
			FailIf (kIOReturnSuccess !=  (result = getCurClockMultiplier (clockID, &numerator, &denominator)), Exit);
			
			subRange.dMIN = subRange.dMIN * numerator / denominator;
			subRange.dMAX = subRange.dMAX * numerator / denominator;
			subRange.dRES = subRange.dRES * numerator / denominator;
		}
	}	
	
Exit:
	if (NULL != sampleRates)
	{
		*sampleRates = subRange;
	}
	
	return result;
}

IOReturn AppleUSBAudioDevice::getClockPathCurSampleRate (UInt32 * sampleRate, Boolean * validity, Boolean * isReadOnly, OSArray * clockPath) {		//	<rdar://6945472>
	OSObject *							arrayObject = NULL;
	OSNumber *							arrayNumber = NULL;
	UInt32								clockIndex;
	UInt8								clockID;
	UInt8								subType;
	UInt32								clockRate;
	bool								clockValidity;
	bool								clockIsReadOnly;	//	<rdar://6945472>
	UInt16								numerator;
	UInt16								denominator;
	IOReturn							result;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::getClockPathCurSampleRate", this);

	clockRate = 0;
	clockValidity = false;
	clockIsReadOnly = false;	//	<rdar://6945472>
	
	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	for (clockIndex = clockPath->getCount (); clockIndex > 0 ; clockIndex--) 
	{
		FailIf (NULL == (arrayObject = clockPath->getObject (clockIndex - 1)), Exit);
		FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
		clockID = arrayNumber->unsigned8BitValue();
		
		FailIf (kIOReturnSuccess != (result = mConfigDictionary->getSubType (&subType, mControlInterface->GetInterfaceNumber(), 0, clockID)), Exit);
		
		if (USBAUDIO_0200::CLOCK_SOURCE == subType)
		{
			//	<rdar://6945472>
			if (!mConfigDictionary->clockSourceHasFrequencyControl (mControlInterface->GetInterfaceNumber (), 0, clockID, true))
			{
				clockIsReadOnly = true;
			}
			FailIf (kIOReturnSuccess !=  (result = getCurClockSourceSamplingFrequency (clockID, &clockRate, &clockValidity)), Exit);
		}
		else if (USBAUDIO_0200::CLOCK_MULTIPLIER == subType)
		{
			FailIf (kIOReturnSuccess !=  (result = getCurClockMultiplier (clockID, &numerator, &denominator)), Exit);
			
			clockRate = clockRate * numerator / denominator;
		}
	}	
	
Exit:
	if (NULL != sampleRate)
	{
		*sampleRate = clockRate;
	}
	if (NULL != validity)
	{
		*validity = clockValidity;
	}
	if ( NULL != isReadOnly)	//	<rdar://6945472>
	{
		*isReadOnly = clockIsReadOnly;
	}
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::getClockPathCurSampleRate (%d, %d) = %d", this, clockRate, clockValidity, result);

	return result;
}

IOReturn AppleUSBAudioDevice::setClockPathCurSampleRate (UInt32 sampleRate, OSArray * clockPath, bool failIfReadOnly ) {	//	<rdar://6945472>
	OSObject *							arrayObject = NULL;
	OSNumber *							arrayNumber = NULL;
	UInt32								clockIndex;
	UInt8								clockID;
	UInt8								subType;
	UInt16								numerator;
	UInt16								denominator;
	OSArray *							clockSourceIDs = NULL;
	UInt32								clockSourceIndex;
	UInt8								nextClockID;
	IOReturn							result;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::setClockPathCurSampleRate (%d)", this, sampleRate);

	result = kIOReturnError;
	FailIf (NULL == mControlInterface, Exit);

	for (clockIndex = 0; clockIndex <  clockPath->getCount (); clockIndex++) 
	{
		FailIf (NULL == (arrayObject = clockPath->getObject (clockIndex)), Exit);
		FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
		clockID = arrayNumber->unsigned8BitValue();
		
		FailIf (kIOReturnSuccess != (result = mConfigDictionary->getSubType (&subType, mControlInterface->GetInterfaceNumber(), 0, clockID)), Exit);
		
		if (USBAUDIO_0200::CLOCK_SOURCE == subType)
		{
			if (mConfigDictionary->clockSourceHasFrequencyControl (mControlInterface->GetInterfaceNumber (), 0, clockID, true))
			{
				FailIf (kIOReturnSuccess !=  (result = setCurClockSourceSamplingFrequency (clockID, sampleRate)), Exit);
			}
			else
			{
				if ( failIfReadOnly )			//	<rdar://6945472>	If clock is source read only, check if the current sample rate matches the new sample rate.
				{
					UInt32		clockRate;
					result = getCurClockSourceSamplingFrequency ( clockID, &clockRate, NULL );
					if ( kIOReturnSuccess == result )
					{
						result = ( clockRate == sampleRate ) ? kIOReturnSuccess : kIOReturnUnsupported;
					}
				}
				else 
				{
					result = kIOReturnSuccess;
				}
			}
			break;
		}
		else if (USBAUDIO_0200::CLOCK_SELECTOR == subType)
		{
			FailIf (kIOReturnSuccess != (result = mConfigDictionary->getClockSelectorSources (&clockSourceIDs, mControlInterface->GetInterfaceNumber(), 0, clockID)), Exit);
			
			FailIf (NULL == (arrayObject = clockPath->getObject (clockIndex + 1)), Exit);
			FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
			nextClockID = arrayNumber->unsigned8BitValue();			

			for (clockSourceIndex = 0; clockSourceIndex < clockSourceIDs->getCount (); clockSourceIndex++)
			{  
				FailIf (NULL == (arrayObject = clockSourceIDs->getObject (clockSourceIndex)), Exit);
				FailIf (NULL == (arrayNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
				
				if (arrayNumber->unsigned8BitValue() == nextClockID)
				{
					setCurClockSelector(clockID, clockSourceIndex + 1);
					break;
				}			
			}
   		}
		else if (USBAUDIO_0200::CLOCK_MULTIPLIER == subType)
		{
			FailIf (kIOReturnSuccess !=  (result = getCurClockMultiplier (clockID, &numerator, &denominator)), Exit);

			sampleRate = sampleRate * denominator / numerator;
		}
	}	
	
Exit:
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::setClockPathCurSampleRate (%d) = %d", this, sampleRate, result);

	return result;
}


#pragma mark Single Audio Engine Capability

//	<rdar://5131786>	Find streams that has common sample rates, and return an array of the streams.
//	The caller is responsible to release the returned stream array.
OSArray * AppleUSBAudioDevice::findStreamsWithCommonSampleRates (OSArray * availableStreamList) {

	OSNumber *	streamInterfaceNumber;
	OSArray *	compatibleStreamList = NULL;
	OSArray *	inCompatibleStreamList = NULL;
	OSArray *	result = NULL;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::findStreamsWithCommonSampleRates (%p)", this, availableStreamList);

	FailIf ( NULL == availableStreamList, Exit );
	
	compatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == compatibleStreamList, Exit );

	inCompatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == inCompatibleStreamList, Exit );

	// Go thru all the available streams, and find all streams that have common sample rates and put them on 
	// the compatible list. Otherwise, put them on the incompatible list.
	while ( 0 != availableStreamList->getCount () ) 
	{
		FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, availableStreamList->getObject (0) ) ), Exit );
		streamInterfaceNumber->retain ();
		
		availableStreamList->removeObject (0);
		
		if ( 0 == compatibleStreamList->getCount () )
		{
			// Determine if the stream has common sample rates as one of the stream in the available list.
			if ( isSampleRateCommonWithAtLeastOneStreamsInList ( streamInterfaceNumber, availableStreamList ) )
			{
				// Keep it in the compatible list.
				compatibleStreamList->setObject ( streamInterfaceNumber );
			}
			else
			{
				// Keep it in the incompatible list.
				inCompatibleStreamList->setObject ( streamInterfaceNumber );
			}
		}
		else
		{
			// Determine if the stream has common sample rates with all the streams in the compatible list.
			if ( isSampleRateCommonWithAllStreamsInList ( streamInterfaceNumber, compatibleStreamList ) )
			{
				// Keep it in the compatible list.
				compatibleStreamList->setObject ( streamInterfaceNumber );
			}
			else
			{
				// Keep it in the incompatible list.
				inCompatibleStreamList->setObject ( streamInterfaceNumber );
			}
		}
		
		streamInterfaceNumber->release ();
	}
	
	// The available stream list should be empty here.
	FailIf ( 0 != availableStreamList->getCount (), Exit );
	
	if ( 0 != compatibleStreamList->getCount () )
	{
		// There is some streams with common sample rates in the compatible list. 
		result = compatibleStreamList;
		result->retain ();
	}
	else
	{
		// Nothing in the compatible stream list. Get one from the incompatible list.
		if ( 0 != inCompatibleStreamList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, inCompatibleStreamList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			
			inCompatibleStreamList->removeObject (0);
			
			compatibleStreamList->setObject ( streamInterfaceNumber );
			result = compatibleStreamList;
			result->retain ();			

			streamInterfaceNumber->release ();
		}
	}
	
	#ifdef DEBUGLOGGING
	if ( NULL != result )
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::findStreamsWithCommonSampleRates (%p) - Found streams:", this, availableStreamList);
		for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < result->getCount (); streamInterfaceIndex++ ) 
		{
			OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, result->getObject ( streamInterfaceIndex ) );
			FailMessage ( NULL == streamInterfaceNumber );
			if ( NULL != streamInterfaceNumber )
			{
				debugIOLog ("--> #%u", streamInterfaceNumber->unsigned8BitValue () );
			}
		}
	}
	#endif
	
Exit:
	if ( NULL != inCompatibleStreamList )
	{
		if ( 0 != inCompatibleStreamList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inCompatibleStreamList );
		}
		
		inCompatibleStreamList->release ();
	}

	if ( NULL != compatibleStreamList )
	{
		// If it failed for some reason, put back the compatible streams into the available stream list.
		if ( NULL == result && 0 != compatibleStreamList->getCount () )
		{
			availableStreamList->merge ( compatibleStreamList );
		}
		
		compatibleStreamList->release ();
	}

	debugIOLog ("- AppleUSBAudioDevice[%p]::findStreamsWithCommonSampleRates (%p) - result = %p", this, availableStreamList, result);

	return result;
}

//	<rdar://5131786>	Determine if the stream's sample rates are common with at least one other stream in the stream list.
bool AppleUSBAudioDevice::isSampleRateCommonWithAtLeastOneStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList) {
	
	bool	result = false;
	
	for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < streamInterfaceNumberList->getCount (); streamInterfaceIndex++ ) 
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, streamInterfaceNumberList->getObject ( streamInterfaceIndex ) );
		FailIf ( NULL == streamInterfaceNumber, Exit );
		
		if ( streamsHaveCommonSampleRates ( refStreamInterfaceNumber, streamInterfaceNumber ) )
		{
			result = true;
			break;
		}
	}

Exit:

	return result;
}

//	<rdar://5131786>	Determine if the stream's sample rates are common with all other streams in the stream list.
bool AppleUSBAudioDevice::isSampleRateCommonWithAllStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList) {
	
	bool	result = true;
	
	for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < streamInterfaceNumberList->getCount (); streamInterfaceIndex++ ) 
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, streamInterfaceNumberList->getObject ( streamInterfaceIndex ) );
		FailIf ( NULL == streamInterfaceNumber, Exit );
		
		if ( !streamsHaveCommonSampleRates ( refStreamInterfaceNumber, streamInterfaceNumber ) )
		{
			result = false;
			break;
		}
	}

Exit:

	return result;
}

//	<rdar://5131786>	Determine if the stream's sample rates are common with each other.
bool AppleUSBAudioDevice::streamsHaveCommonSampleRates (OSNumber * streamInterfaceNumberA, OSNumber * streamInterfaceNumberB) {
	
	bool		result = false;
	OSArray *	sampleRatesA = NULL;
	OSArray *	sampleRatesB = NULL;
	 
	debugIOLog ("+ AppleUSBAudioDevice[%p]::streamsHaveCommonSampleRates (%p, %p)", this, streamInterfaceNumberA, streamInterfaceNumberB);

	FailIf ( NULL == streamInterfaceNumberA, Exit );
	FailIf ( NULL == streamInterfaceNumberB, Exit );

	debugIOLog ("? AppleUSBAudioDevice[%p]::streamsHaveCommonSampleRates (%d, %d)", this, streamInterfaceNumberA->unsigned8BitValue (), streamInterfaceNumberB->unsigned8BitValue ());

	sampleRatesA = getSampleRatesFromStreamInterface ( streamInterfaceNumberA );
	sampleRatesB = getSampleRatesFromStreamInterface ( streamInterfaceNumberB );
	
	#ifdef DEBUGLOGGING
	debugIOLog ("Sample rates for interface %d:", streamInterfaceNumberA->unsigned8BitValue ());
	if ( NULL != sampleRatesA )
	{
		for ( UInt32 rateIndex = 0; rateIndex < sampleRatesA->getCount (); rateIndex++ ) 
		{
			OSNumber * rate = OSDynamicCast (OSNumber, sampleRatesA->getObject ( rateIndex ) );
			FailMessage ( NULL == rate );
			if ( NULL != rate )
			{
				debugIOLog ("--> %lu", rate->unsigned32BitValue () );
			}
		}
	}
	debugIOLog ("Sample rates for interface %d:", streamInterfaceNumberB->unsigned8BitValue ());
	if ( NULL != sampleRatesB )
	{
		for ( UInt32 rateIndex = 0; rateIndex < sampleRatesB->getCount (); rateIndex++ ) 
		{
			OSNumber * rate = OSDynamicCast (OSNumber, sampleRatesB->getObject ( rateIndex ) );
			FailMessage ( NULL == rate );
			if ( NULL != rate )
			{
				debugIOLog ("--> %lu", rate->unsigned32BitValue () );
			}
		}
	}
	#endif

	if ( ( NULL != sampleRatesA ) && ( NULL != sampleRatesB ) )
	{
		if ( compareSampleRates ( sampleRatesA, sampleRatesB ) && compareSampleRates ( sampleRatesB, sampleRatesA ) )
		{
			result = true;
		}
	}
	
Exit:
	debugIOLog ("+ AppleUSBAudioDevice[%p]::streamsHaveCommonClocks (%p, %p) - result = %d", this, streamInterfaceNumberA, streamInterfaceNumberB, result);

	return result;
}

//	<rdar://5131786>	Get the sample rates supported by a stream interface.
OSArray * AppleUSBAudioDevice::getSampleRatesFromStreamInterface (OSNumber * streamInterfaceNumber) {
	
	UInt8		interfaceNum;
	UInt8		numAltSettings = 0;
	OSArray *	sampleRates = NULL;

	FailIf ( NULL == streamInterfaceNumber, Exit );
	
	interfaceNum = streamInterfaceNumber->unsigned8BitValue ();
	
	if ( kIOReturnSuccess == mConfigDictionary->getNumAltSettings ( &numAltSettings, interfaceNum ) )
	{
		for ( UInt8 altSetting = 0; altSetting < numAltSettings; altSetting++ )
		{
			OSArray *	rates = mConfigDictionary->getSampleRates ( interfaceNum, altSetting );
			
			if ( NULL != rates )
			{
				if ( NULL == sampleRates )
				{
					sampleRates = OSArray::withCapacity ( rates->getCount () );					
				}
				
				if ( NULL != sampleRates )
				{
					mergeSampleRates ( sampleRates, rates );
				}
			}
		}
	}

Exit:

	return sampleRates;
}

//	<rdar://5131786>	Merge sample rates array, taking in only new sample rates.
void AppleUSBAudioDevice::mergeSampleRates (OSArray * thisArray, OSArray * otherArray)
{
	for (UInt32 otherIndex = 0; otherIndex < otherArray->getCount (); otherIndex++)
	{
		OSNumber *	otherRate = OSDynamicCast ( OSNumber, otherArray->getObject ( otherIndex ) );
		if ( NULL != otherRate )
		{
			bool	rateFound = false;
			
			// Check if the sample rate above is already in our array.
			for ( UInt32 thisIndex = 0; thisIndex < thisArray->getCount (); thisIndex++ )
			{
				OSNumber *	thisRate = OSDynamicCast ( OSNumber, thisArray->getObject ( thisIndex ) );
				if ( NULL != thisRate )
				{
					if ( thisRate->unsigned32BitValue () == otherRate->unsigned32BitValue () )
					{
						rateFound = true;
						break;
					}
				}
			}
			
			if ( !rateFound )
			{
				thisArray->setObject ( otherRate );
			}
		}
	}									
}

//	<rdar://5131786>	Compare the sample rates in both array to determine if all rates are supported in both array.
bool AppleUSBAudioDevice::compareSampleRates (OSArray * sampleRatesA, OSArray * sampleRatesB)
{
	bool	sampleRatesSupported = true;
	
	for ( UInt32 indexA = 0; indexA < sampleRatesA->getCount (); indexA++ )
	{
		OSNumber *	rateA = OSDynamicCast (OSNumber, sampleRatesA->getObject ( indexA ) );
		if ( NULL != rateA )
		{
			bool	rateFound = false;
			
			for ( UInt32 indexB = 0; indexB < sampleRatesB->getCount (); indexB++ )
			{
				OSNumber *	rateB = OSDynamicCast (OSNumber, sampleRatesB->getObject ( indexB ) );
				if ( NULL != rateB )
				{
					if ( rateA->unsigned32BitValue () == rateB->unsigned32BitValue () )
					{
						rateFound = true;
						break;
					}
				}
			}
			
			if ( !rateFound )
			{
				sampleRatesSupported = false;
				break;
			}
		}
	}
	
	return sampleRatesSupported;
}

//	<rdar://5131786>	Find streams that has compatible endpoints, and return an array of the streams.
//	The caller is responsible to release the returned stream array.
OSArray * AppleUSBAudioDevice::findStreamsWithCompatibleEndpoints (OSArray * availableStreamList) {

	OSNumber *	streamInterfaceNumber;
	OSArray *	inputAsynchronousList = NULL;
	OSArray *	inputAdaptiveList = NULL;
	OSArray *	inputSynchronousList = NULL;
	OSArray *	outputAsynchronousList = NULL;
	OSArray *	outputAdaptiveList = NULL;
	OSArray *	outputSynchronousList = NULL;
	OSArray *	inputUnknownList = NULL;
	OSArray *	outputUnknownList = NULL;
	OSArray *	compatibleStreamList = NULL;
	OSArray *	result = NULL;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::findStreamsWithCompatibleEndpoints (%p)", this, availableStreamList);

	FailIf ( NULL == availableStreamList, Exit );
	
	compatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == compatibleStreamList, Exit );

	// Find the input & output streams for direction & sync type.
	inputAsynchronousList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBIn, kAsynchSyncType );
	inputAdaptiveList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBIn, kAdaptiveSyncType );
	inputSynchronousList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBIn, kSynchronousSyncType );
	outputAsynchronousList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBOut, kAsynchSyncType );
	outputAdaptiveList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBOut, kAdaptiveSyncType );
	outputSynchronousList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBOut, kSynchronousSyncType );
	inputUnknownList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBIn, kUnknownSyncType );
	outputUnknownList = findStreamsWithDirectionAndSyncType ( availableStreamList, kUSBOut, kUnknownSyncType );
	
	// The available stream list should be empty here.
	FailIf ( 0 != availableStreamList->getCount (), Exit );

	// Check if there is an asynchronous input stream.
	if ( NULL != inputAsynchronousList && 0 != inputAsynchronousList->getCount () )
	{
		// There is at least an asynchronous input stream. Put the streams on the compatible list.
		compatibleStreamList->merge ( inputAsynchronousList );
		inputAsynchronousList->release ();
		inputAsynchronousList = 0;

		// If there is at least an asynchronous output stream, put the streams on the compatible list.
		if ( NULL != outputAsynchronousList && 0 != outputAsynchronousList->getCount () )
		{
			compatibleStreamList->merge ( outputAsynchronousList );
			outputAsynchronousList->release ();
			outputAsynchronousList = 0;
		}		
		// If there is at least an adaptive output stream, put the streams on the compatible list.
		if ( NULL != outputAdaptiveList && 0 != outputAdaptiveList->getCount () )
		{
			compatibleStreamList->merge ( outputAdaptiveList );
			outputAdaptiveList->release ();
			outputAdaptiveList = 0;
		}		
		// If there is at least a synchronous output stream, put the streams on the compatible list.
		if ( NULL != outputSynchronousList && 0 != outputSynchronousList->getCount () )
		{
			compatibleStreamList->merge ( outputSynchronousList );
			outputSynchronousList->release ();
			outputSynchronousList = 0;
		}		
	}
	else
	{
		// Check if there is an synchronous or adaptive input stream.
		if ( ( NULL != inputSynchronousList ) || ( NULL != inputAdaptiveList ) )
		{
			// If there is at least a synchronous input stream, put the streams on the compatible list.
			if ( NULL != inputSynchronousList && 0 != inputSynchronousList->getCount () )
			{
				compatibleStreamList->merge ( inputSynchronousList );
				inputSynchronousList->release ();
				inputSynchronousList = 0;
			}		
			// If there is at least an adaptive input stream, put the streams on the compatible list.
			if ( NULL != inputAdaptiveList && 0 != inputAdaptiveList->getCount () )
			{
				compatibleStreamList->merge ( inputAdaptiveList );
				inputAdaptiveList->release ();
				inputAdaptiveList = 0;
			}		
			// If there is at least a synchronous output stream, put the streams on the compatible list.
			if ( NULL != outputSynchronousList && 0 != outputSynchronousList->getCount () )
			{
				compatibleStreamList->merge ( outputSynchronousList );
				outputSynchronousList->release ();
				outputSynchronousList = 0;
			}		
			// If there is at least an adaptive output stream, put the streams on the compatible list.
			if ( NULL != outputAdaptiveList && 0 != outputAdaptiveList->getCount () )
			{
				compatibleStreamList->merge ( outputAdaptiveList );
				outputAdaptiveList->release ();
				outputAdaptiveList = 0;
			}
		}
		else
		{
			// No input stream present, so check if there is a synchronous or adaptive output stream present.
			if ( ( NULL != outputSynchronousList ) || ( NULL != outputAdaptiveList ) )
			{
				// If there is at least a synchronous output stream, put the streams on the compatible list.
				if ( NULL != outputSynchronousList && 0 != outputSynchronousList->getCount () )
				{
					compatibleStreamList->merge ( outputSynchronousList );
					outputSynchronousList->release ();
					outputSynchronousList = 0;
				}		
				// If there is at least an adaptive output stream, put the streams on the compatible list.
				if ( NULL != outputAdaptiveList && 0 != outputAdaptiveList->getCount () )
				{
					compatibleStreamList->merge ( outputAdaptiveList );
					outputAdaptiveList->release ();
					outputAdaptiveList = 0;
				}
			}
			else
			{
				// No synchronous or adaptive output streams, so check if there is an asynchronous stream present.
				// If there is at least an asynchronous output stream, put the streams on the compatible list.
				if ( NULL != outputAsynchronousList && 0 != outputAsynchronousList->getCount () )
				{
					compatibleStreamList->merge ( outputAsynchronousList );
					outputAsynchronousList->release ();
					outputAsynchronousList = 0;
				}
			}
		}		
	}
	
	if ( 0 != compatibleStreamList->getCount () )
	{
		// There is some streams with compatible endpoints in the compatible list. 
		result = compatibleStreamList;
		result->retain ();
	}	
	else
	{
		// Nothing in the compatible stream list. Get one from the other lists.
		streamInterfaceNumber = NULL;
		
		if ( NULL != inputAsynchronousList && 0 != inputAsynchronousList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, inputAsynchronousList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			inputAsynchronousList->removeObject (0);
		}
		else if ( NULL != inputAdaptiveList && 0 != inputAdaptiveList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, inputAdaptiveList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			inputAdaptiveList->removeObject (0);
		}
		else if ( NULL != inputSynchronousList && 0 != inputSynchronousList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, inputSynchronousList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			inputSynchronousList->removeObject (0);
		}
		else if ( NULL != outputAsynchronousList && 0 != outputAsynchronousList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, outputAsynchronousList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			outputAsynchronousList->removeObject (0);
		}
		else if ( NULL != outputAdaptiveList && 0 != outputAdaptiveList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, outputAdaptiveList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			outputAdaptiveList->removeObject (0);
		}
		else if ( NULL != outputSynchronousList && 0 != outputSynchronousList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, outputSynchronousList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			outputSynchronousList->removeObject (0);
		}
		else if ( NULL != inputUnknownList && 0 != inputUnknownList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, inputUnknownList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			inputUnknownList->removeObject (0);
		}
		else if ( NULL != outputUnknownList && 0 != outputUnknownList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, outputUnknownList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();
			outputUnknownList->removeObject (0);
		}

		if ( NULL != streamInterfaceNumber )
		{
			compatibleStreamList->setObject ( streamInterfaceNumber );
			result = compatibleStreamList;
			result->retain ();

			streamInterfaceNumber->release ();
		}
	}
	
	#ifdef DEBUGLOGGING
	if ( NULL != result )
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::findStreamsWithCompatibleEndpoints (%p) - Found streams:", this, availableStreamList);
		for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < result->getCount (); streamInterfaceIndex++ ) 
		{
			OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, result->getObject ( streamInterfaceIndex ) );
			FailMessage ( NULL == streamInterfaceNumber );
			if ( NULL != streamInterfaceNumber )
			{
				debugIOLog ("--> #%u", streamInterfaceNumber->unsigned8BitValue () );
			}
		}
	}
	#endif

Exit:
	// Put back unused streams back on the available list.
	if ( NULL != inputAsynchronousList )
	{
		if ( 0 != inputAsynchronousList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inputAsynchronousList );
		}
		
		inputAsynchronousList->release ();
	}
	if ( NULL != inputAdaptiveList )
	{
		if ( 0 != inputAdaptiveList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inputAdaptiveList );
		}
		
		inputAdaptiveList->release ();
	}
	if ( NULL != inputSynchronousList )
	{
		if ( 0 != inputSynchronousList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inputSynchronousList );
		}
		
		inputSynchronousList->release ();
	}
	if ( NULL != outputAsynchronousList )
	{
		if ( 0 != outputAsynchronousList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( outputAsynchronousList );
		}
		
		outputAsynchronousList->release ();
	}
	if ( NULL != outputAdaptiveList )
	{
		if ( 0 != outputAdaptiveList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( outputAdaptiveList );
		}
		
		outputAdaptiveList->release ();
	}
	if ( NULL != outputSynchronousList )
	{
		if ( 0 != outputSynchronousList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( outputSynchronousList );
		}
		
		outputSynchronousList->release ();
	}
	if ( NULL != inputUnknownList )
	{
		if ( 0 != inputUnknownList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inputUnknownList );
		}
		
		inputUnknownList->release ();
	}
	if ( NULL != outputUnknownList )
	{
		if ( 0 != outputUnknownList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( outputUnknownList );
		}
		
		outputUnknownList->release ();
	}
	if ( NULL != compatibleStreamList )
	{
		// If it failed for some reason, put back the compatible streams into the available stream list.
		if ( NULL == result && 0 != compatibleStreamList->getCount () )
		{
			availableStreamList->merge ( compatibleStreamList );
		}
		
		compatibleStreamList->release ();
	}

	debugIOLog ("- AppleUSBAudioDevice[%p]::findStreamsWithCompatibleEndpoints (%p) - result = %p", this, availableStreamList, result);

	return result;
}

//	<rdar://5131786>	Find streams with the specified direction & sync type in the available list.
OSArray * AppleUSBAudioDevice::findStreamsWithDirectionAndSyncType (OSArray * availableStreamList, UInt8 direction, UInt8 syncType ) {

	OSNumber *	streamInterfaceNumber;
	OSArray *	compatibleStreamList = NULL;
	OSArray *	inCompatibleStreamList = NULL;
	OSArray *	result = NULL;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::findStreamsWithDirectionAndSyncType (%p, %d, %d)", this, availableStreamList, direction, syncType);

	FailIf ( NULL == availableStreamList, Exit );
	
	compatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == compatibleStreamList, Exit );

	inCompatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == inCompatibleStreamList, Exit );

	// Go thru all the available streams, and find all streams that have compatible endpoints and put them on 
	// the compatible list. Otherwise, put them on the incompatible list.
	while ( 0 != availableStreamList->getCount () ) 
	{
		FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, availableStreamList->getObject (0) ) ), Exit );
		streamInterfaceNumber->retain ();
		
		availableStreamList->removeObject (0);
		
		if ( streamEndpointsHaveSpecifiedDirectionAndSyncType ( streamInterfaceNumber, direction, syncType ) )
		{
			// Keep it in the compatible list.
			compatibleStreamList->setObject ( streamInterfaceNumber );
		}
		else
		{
			// Keep it in the incompatible list.
			inCompatibleStreamList->setObject ( streamInterfaceNumber );
		}

		streamInterfaceNumber->release ();
	}
	
	// The available stream list should be empty here.
	FailIf ( 0 != availableStreamList->getCount (), Exit );
	
	if ( 0 != compatibleStreamList->getCount () )
	{
		// There is some streams with compatible endpoints in the compatible list. 
		result = compatibleStreamList;
		result->retain ();
	}
	
	#ifdef DEBUGLOGGING
	if ( NULL != result )
	{
		debugIOLog ("- AppleUSBAudioDevice[%p]::findStreamsWithDirectionAndSyncType (%p, %d, %d) - Found streams:", this, availableStreamList, direction, syncType);
		for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < result->getCount (); streamInterfaceIndex++ ) 
		{
			OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, result->getObject ( streamInterfaceIndex ) );
			FailMessage ( NULL == streamInterfaceNumber );
			if ( NULL != streamInterfaceNumber )
			{
				debugIOLog ("--> #%u", streamInterfaceNumber->unsigned8BitValue () );
			}
		}
	}
	#endif

Exit:
	if ( NULL != inCompatibleStreamList )
	{
		if ( 0 != inCompatibleStreamList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inCompatibleStreamList );
		}
		
		inCompatibleStreamList->release ();
	}

	if ( NULL != compatibleStreamList )
	{
		// If it failed for some reason, put back the compatible streams into the available stream list.
		if ( NULL == result && 0 != compatibleStreamList->getCount () )
		{
			availableStreamList->merge ( compatibleStreamList );
		}
		
		compatibleStreamList->release ();
	}

	debugIOLog ("- AppleUSBAudioDevice[%p]::findStreamsWithDirectionAndSyncType (%p, %d, %d) - result = %p", this, availableStreamList, direction, syncType, result);

	return result;
}

//	<rdar://5131786>	Determine if the stream's endpoints are compatible with the specified direction & sync type.
bool AppleUSBAudioDevice::streamEndpointsHaveSpecifiedDirectionAndSyncType ( OSNumber * streamInterfaceNumber, UInt8 endpointDirection, UInt8 endpointSyncType ) {

	UInt8	interfaceNum;
	UInt8	numAltSettings = 0;
	bool	hasSpecifiedDirection = true;
	bool	hasSpecifiedSyncType = true;
	bool	result = false;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::streamEndpointsHaveSpecifiedDirectionAndSyncType (%p, %d, %d)", this, streamInterfaceNumber, endpointDirection, endpointSyncType);

	FailIf ( NULL == streamInterfaceNumber, Exit );

	interfaceNum = streamInterfaceNumber->unsigned8BitValue ();
	
	debugIOLog ("? AppleUSBAudioDevice[%p]::streamEndpointsHaveSpecifiedDirectionAndSyncType (%d, %d, %d)", this, interfaceNum, endpointDirection, endpointSyncType);

	if ( kIOReturnSuccess == mConfigDictionary->getNumAltSettings ( &numAltSettings, interfaceNum ) )
	{
		bool	startAtZero = mConfigDictionary->alternateSettingZeroCanStream ( interfaceNum );
		
		if ( kUnknownSyncType == endpointSyncType )
		{
			UInt32	compareSyncType = kUnknownSyncType;
			
			hasSpecifiedSyncType = false;
			
			// Make sure that all alternate settings has the same sync type.			
			for ( UInt8 altSetting = (startAtZero ? 0 : 1); altSetting < numAltSettings; altSetting++ )
			{
				UInt8	direction = 0;
				UInt8	address = 0;
				UInt8	syncType = 0;
				// <rdar://6378666> Don't exit if there are AS interfaces with missing class-specific interface descriptor. 
				if ( ( kIOReturnSuccess == mConfigDictionary->getIsocEndpointDirection (&direction, interfaceNum, altSetting) ) && 
					 ( kIOReturnSuccess == mConfigDictionary->getIsocEndpointAddress (&address, interfaceNum, altSetting, direction) ) &&
					 ( kIOReturnSuccess == mConfigDictionary->getIsocEndpointSyncType (&syncType, interfaceNum, altSetting, address) ) )
				{
					if ( endpointDirection != direction )
					{
						hasSpecifiedDirection = false;
						break;
					}
					
					if ( kUnknownSyncType == compareSyncType )
					{
						compareSyncType = syncType;
					}
					else
					{
						if ( compareSyncType != syncType )
						{
							hasSpecifiedSyncType = true;
						}
					}
				}
			}
		}
		else
		{
			// Make sure that all the input & output endpoints has the compatible sync type.			
			for ( UInt8 altSetting = (startAtZero ? 0 : 1); altSetting < numAltSettings; altSetting++ )
			{
				UInt8	direction = 0;
				UInt8	address = 0;
				UInt8	syncType = 0;
				// <rdar://6378666> Don't exit if there are AS interfaces with missing class-specific interface descriptor. 
				if ( ( kIOReturnSuccess == mConfigDictionary->getIsocEndpointDirection (&direction, interfaceNum, altSetting) ) && 
					 ( kIOReturnSuccess == mConfigDictionary->getIsocEndpointAddress (&address, interfaceNum, altSetting, direction) ) &&
					 ( kIOReturnSuccess == mConfigDictionary->getIsocEndpointSyncType (&syncType, interfaceNum, altSetting, address) ) )
				{
					if ( endpointDirection != direction )
					{
						hasSpecifiedDirection = false;
						break;
					}
					
					switch ( endpointSyncType )
					{
						case kNoneSyncType:
						case kSynchronousSyncType:
							if ( ( kNoneSyncType != syncType ) && ( kSynchronousSyncType != syncType ) )
							{
								hasSpecifiedSyncType = false;
							}
							break;
						default:
							if ( endpointSyncType != syncType )
							{
								hasSpecifiedSyncType = false;
							}
							break;
					}
				}
					
				if ( !hasSpecifiedSyncType) break;
			}
		}
	}
	
	if ( hasSpecifiedDirection && hasSpecifiedSyncType )
	{
		result = true;
	}
	
Exit:
	
	debugIOLog ("- AppleUSBAudioDevice[%p]::streamEndpointsHaveSpecifiedDirectionAndSyncType (%p, %d, %d) - result = %d", this, streamInterfaceNumber, endpointDirection, endpointSyncType, result);

	return result;
}

//	<rdar://5131786>	Find streams that has common clocks, and return an array of the streams.
//	The caller is responsible to release the returned stream array.
OSArray * AppleUSBAudioDevice::findStreamsWithCommonClocks (OSArray * availableStreamList) {

	OSNumber *	streamInterfaceNumber;
	OSArray *	compatibleStreamList = NULL;
	OSArray *	inCompatibleStreamList = NULL;
	OSArray *	result = NULL;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::findStreamsWithCommonClocks (%p)", this, availableStreamList);

	FailIf ( NULL == availableStreamList, Exit );
	
	compatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == compatibleStreamList, Exit );

	inCompatibleStreamList =  OSArray::withCapacity ( 1 );
	FailIf ( 0 == inCompatibleStreamList, Exit );

	// Go thru all the available streams, and find all streams that have common sample rates and put them on 
	// the compatible list. Otherwise, put them on the incompatible list.
	while ( 0 != availableStreamList->getCount () ) 
	{
		FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, availableStreamList->getObject (0) ) ), Exit );
		streamInterfaceNumber->retain ();
		
		availableStreamList->removeObject (0);
		
		if ( 0 == compatibleStreamList->getCount () )
		{
			// Determine if the stream has common clocks as one of the stream in the available list.
			if ( isClockCommonWithAtLeastOneStreamsInList ( streamInterfaceNumber, availableStreamList ) )
			{
				// Keep it in the compatible list.
				compatibleStreamList->setObject ( streamInterfaceNumber );
			}
			else
			{
				// Keep it in the incompatible list.
				inCompatibleStreamList->setObject ( streamInterfaceNumber );
			}
		}
		else
		{
			// Determine if the stream has common clocks with all the streams in the compatible list.
			if ( isClockCommonWithAllStreamsInList ( streamInterfaceNumber, compatibleStreamList ) )
			{
				// Keep it in the compatible list.
				compatibleStreamList->setObject ( streamInterfaceNumber );
			}
			else
			{
				// Keep it in the incompatible list.
				inCompatibleStreamList->setObject ( streamInterfaceNumber );
			}
		}
		
		streamInterfaceNumber->release ();
	}
	
	// The available stream list should be empty here.
	FailIf ( 0 != availableStreamList->getCount (), Exit );
	
	if ( 0 != compatibleStreamList->getCount () )
	{
		// There is some streams with common clocks in the compatible list. 
		result = compatibleStreamList;
		result->retain ();
	}
	else
	{
		// Nothing in the compatible stream list. Get one from the incompatible list.
		if ( 0 != inCompatibleStreamList->getCount () )
		{
			FailIf ( NULL == ( streamInterfaceNumber = OSDynamicCast ( OSNumber, inCompatibleStreamList->getObject (0) ) ), Exit );
			streamInterfaceNumber->retain ();

			inCompatibleStreamList->removeObject (0);
			
			compatibleStreamList->setObject ( streamInterfaceNumber );
			result = compatibleStreamList;
			result->retain ();			

			streamInterfaceNumber->release ();
		}
	}
	
	#ifdef DEBUGLOGGING
	if ( NULL != result )
	{
		debugIOLog ("? AppleUSBAudioDevice[%p]::findStreamsWithCommonClocks (%p) - Found streams:", this, availableStreamList);
		for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < result->getCount (); streamInterfaceIndex++ ) 
		{
			OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, result->getObject ( streamInterfaceIndex ) );
			FailMessage ( NULL == streamInterfaceNumber );
			if ( NULL != streamInterfaceNumber )
			{
				debugIOLog ("--> #%u", streamInterfaceNumber->unsigned8BitValue () );
			}
		}
	}
	#endif

Exit:	
	if ( NULL != inCompatibleStreamList )
	{
		if ( 0 != inCompatibleStreamList->getCount () )
		{
			// Put back the incompatible streams into the available stream list.
			availableStreamList->merge ( inCompatibleStreamList );
		}
		
		inCompatibleStreamList->release ();
	}

	if ( NULL != compatibleStreamList )
	{
		// If it failed for some reason, put back the compatible streams into the available stream list.
		if ( NULL == result && 0 != compatibleStreamList->getCount () )
		{
			availableStreamList->merge ( compatibleStreamList );
		}
		
		compatibleStreamList->release ();
	}

	debugIOLog ("- AppleUSBAudioDevice[%p]::findStreamsWithCommonClocks (%p) - result = %p", this, availableStreamList, result);

	return result;
}

//	<rdar://5131786>	Determine if the stream's clocks are common with at least one other stream in the stream list.
bool AppleUSBAudioDevice::isClockCommonWithAtLeastOneStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList) {
	
	bool	result = false;
	
	for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < streamInterfaceNumberList->getCount (); streamInterfaceIndex++ ) 
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, streamInterfaceNumberList->getObject ( streamInterfaceIndex ) );
		FailIf ( NULL == streamInterfaceNumber, Exit );
		
		if ( streamsHaveCommonClocks ( refStreamInterfaceNumber, streamInterfaceNumber ) )
		{
			result = true;
			break;
		}
	}

Exit:

	return result;
}

//	<rdar://5131786>	Determine if the stream's clocks are common with all other streams in the stream list.
bool AppleUSBAudioDevice::isClockCommonWithAllStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList) {
	
	bool	result = true;
	
	for ( UInt32 streamInterfaceIndex = 0; streamInterfaceIndex < streamInterfaceNumberList->getCount (); streamInterfaceIndex++ ) 
	{
		OSNumber * streamInterfaceNumber = OSDynamicCast (OSNumber, streamInterfaceNumberList->getObject ( streamInterfaceIndex ) );
		FailIf ( NULL == streamInterfaceNumber, Exit );
		
		if ( !streamsHaveCommonClocks ( refStreamInterfaceNumber, streamInterfaceNumber ) )
		{
			result = false;
			break;
		}
	}

Exit:

	return result;
}

//	<rdar://5131786>	Determine if the stream's clocks are common with each other.
bool AppleUSBAudioDevice::streamsHaveCommonClocks (OSNumber * streamInterfaceNumberA, OSNumber * streamInterfaceNumberB) {
	
	OSArray *	clockPathGroup;
	OSArray	*	clockPath;
	UInt8		pathIndex;
	UInt8		rateIndex;
	UInt32		numClockPathSupported = 0;
	UInt32		numClockPathCrossed = 0;
	UInt8		interfaceNum;
	UInt8		numAltSettings = 0;
	bool		result = false;
	 
	debugIOLog ("+ AppleUSBAudioDevice[%p]::streamsHaveCommonClocks (%p, %p)", this, streamInterfaceNumberA, streamInterfaceNumberB);

	FailIf ( NULL == streamInterfaceNumberA, Exit );
	FailIf ( NULL == streamInterfaceNumberB, Exit );
	
	debugIOLog ("? AppleUSBAudioDevice[%p]::streamsHaveCommonClocks (%d, %d)", this, streamInterfaceNumberA->unsigned8BitValue (), streamInterfaceNumberB->unsigned8BitValue ());

	interfaceNum = streamInterfaceNumberB->unsigned8BitValue ();

	if ( kIOReturnSuccess == mConfigDictionary->getNumAltSettings ( &numAltSettings, interfaceNum ) )
	{
		bool	startAtZero = mConfigDictionary->alternateSettingZeroCanStream ( interfaceNum );
		
		for ( UInt8 altSetting = (startAtZero ? 0 : 1); altSetting < numAltSettings; altSetting++ )
		{
			OSArray *	rates = mConfigDictionary->getSampleRates ( interfaceNum, altSetting );
			if (NULL != rates)
			{
				clockPathGroup = getClockPathGroup ( interfaceNum, altSetting );
				FailIf ( NULL == clockPathGroup, Exit );

				for ( rateIndex = 0; rateIndex < rates->getCount (); rateIndex++ )
				{
					OSNumber *	rate = OSDynamicCast ( OSNumber, rates->getObject ( rateIndex ) );
					if (NULL != rate)
					{
						// For each path in the path group, determine if it crosses path with the other stream interface.
						for ( pathIndex = 0; pathIndex < clockPathGroup->getCount (); pathIndex++ )
						{
							FailIf ( NULL == ( clockPath = OSDynamicCast ( OSArray, clockPathGroup->getObject ( pathIndex ) ) ), Exit );
							
							if ( supportSampleRateInClockPath ( clockPath, rate->unsigned32BitValue () ) )
							{
								numClockPathSupported ++;
								if ( isClockPathCrossed ( streamInterfaceNumberA, clockPath, rate->unsigned32BitValue () ) )
								{
									numClockPathCrossed ++;
								}
								else
								{
									break;
								}
							}
						}
					}
					
					if ( numClockPathSupported != numClockPathCrossed )
					{
						break;
					}									
				}	
			}
			
			if ( numClockPathSupported != numClockPathCrossed )
			{
				break;
			}
		}
	}
	
	result = ( 0 != numClockPathSupported ) && ( 0 != numClockPathCrossed ) && ( numClockPathSupported == numClockPathCrossed );

Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::streamsHaveCommonClocks (%p, %p) - result = %d", this, streamInterfaceNumberA, streamInterfaceNumberB, result);

	return result;
}

//	<rdar://5131786>	Check if the specified clock path crosses with clock path in the first interface.
bool AppleUSBAudioDevice::isClockPathCrossed (OSNumber * streamInterfaceNumber, OSArray * otherClockPath, UInt32 sampleRate)
{
	UInt8		interfaceNum;
	UInt8		numAltSettings = 0;
	OSArray *	clockPathGroup;
	OSArray	*	thisClockPath;
	UInt8		pathIndex;
	bool		startAtZero;
	UInt32		numClockPathSupported = 0;
	UInt32		numClockPathCrossed = 0;
	bool		result = false;

	debugIOLog ("+ AppleUSBAudioDevice[%p]::isClockPathCrossed (%p, %p, %lu)", this, streamInterfaceNumber, otherClockPath, sampleRate);
	
	// Check if the clock path against the first stream interface to determine if they have a common clock source.
	interfaceNum = streamInterfaceNumber->unsigned8BitValue ();
	
	if ( kIOReturnSuccess == mConfigDictionary->getNumAltSettings ( &numAltSettings, interfaceNum ) )
	{
		startAtZero = mConfigDictionary->alternateSettingZeroCanStream ( interfaceNum );

		for ( UInt8 altSetting = (startAtZero ? 0 : 1); altSetting < numAltSettings; altSetting++ )
		{
			clockPathGroup = getClockPathGroup ( interfaceNum, altSetting );
			
			if ( NULL != clockPathGroup )
			{
				// For each path in the path group, determine if it supported the requested sample rate.
				for ( pathIndex = 0; pathIndex < clockPathGroup->getCount (); pathIndex++ )
				{
					FailIf ( NULL == ( thisClockPath = OSDynamicCast ( OSArray, clockPathGroup->getObject ( pathIndex ) ) ), Exit );
					
					if ( supportSampleRateInClockPath ( thisClockPath, sampleRate ) )
					{
						numClockPathSupported ++;
						
						if ( clockPathCrossed ( thisClockPath, otherClockPath ) )
						{
							numClockPathCrossed ++;
						}
						else
						{
							break;
						}
					}
				}
				
				if ( numClockPathSupported != numClockPathCrossed )
				{
					break;
				}
			}
		}
	}
	
	result = ( 0 != numClockPathSupported ) && ( 0 != numClockPathCrossed ) && ( numClockPathSupported == numClockPathCrossed );

Exit:
	debugIOLog ("- AppleUSBAudioDevice[%p]::isClockPathCrossed (%p, %p, %lu) - result = %d", this, streamInterfaceNumber, otherClockPath, sampleRate, result);

	return result;
}

