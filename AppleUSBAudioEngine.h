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
//	File:		AppleUSBAudioEngine.h
//
//	Contains:	Support for the USB Audio Class Stream Interface.
//
//	Technology:	Mac OS X
//
//--------------------------------------------------------------------------------

#ifndef _APPLEUSBAUDIOENGINE_H
#define _APPLEUSBAUDIOENGINE_H

#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSCollectionIterator.h>
#include <libkern/c++/OSMetaClass.h>

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOMemoryCursor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#ifdef __LP64__
#include <IOKit/IOSubMemoryDescriptor.h>
#endif /* !__LP64__ */
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMultiMemoryDescriptor.h>

#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioPort.h>
#include <IOKit/audio/IOAudioTypes.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioStream.h>

#include <IOKit/usb/IOUSBPipe.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBInterface.h>

#include <AvailabilityMacros.h>

#include "AppleUSBAudioCommon.h"
#include "AppleUSBAudioDevice.h"
#include "AppleUSBAudioDictionary.h"
#include "AppleUSBAudioClip.h"
#include "AppleUSBAudioStream.h"

class AppleUSBAudioDevice;

//	-----------------------------------------------------------------

#define	kSampleRate_44100				44100
#define kDefaultSamplingRate			kSampleRate_44100
#define	kBitDepth_16bits				16
#define	kChannelDepth_MONO				1
#define	kChannelDepth_STEREO			2

#define kFixedPoint10_14ByteSize		3
#define	kFixedPoint16_16ByteSize		4

#define kMinimumFrameOffset				6

#define kAnchorSamplingFreqSec			1024							// <rdar://problem/7378275>
#define kAnchorSamplingFreq1			kAnchorSamplingFreqSec/64		// <rdar://problem/7378275>
#define kAnchorSamplingFreq2			kAnchorSamplingFreqSec/32		// <rdar://problem/7378275>
#define kAnchorSamplingFreq3			kAnchorSamplingFreqSec/16		// <rdar://problem/7378275>
#define kAnchorSamplingFreq4			kAnchorSamplingFreqSec/8		// <rdar://problem/7378275>

#define RECORD_NUM_USB_FRAME_LISTS				128
#define RECORD_NUM_USB_FRAMES_PER_LIST			2
#define RECORD_NUM_USB_FRAME_LISTS_TO_QUEUE		64

#define PLAY_NUM_USB_FRAME_LISTS				4
#define PLAY_NUM_USB_FRAMES_PER_LIST			64
#define PLAY_NUM_USB_FRAME_LISTS_TO_QUEUE		2
#define	PLAY_NUM_USB_FRAMES_PER_LIST_SYNC		32

// [rdar://5623096] Make note of the slowest polling interval in ms for feedback endpoints

#define kMaxFeedbackPollingInterval				512

#define kFormatChangeDelayInMs					667
#define	kStartDelayOffset						5

#define	kMaxTriesForStreamPropertiesReady		500	//  <rdar://problem/6686515> 500 x 10ms = 5 second timeout

class AppleUSBAudioEngine;
class AppleUSBAudioPlugin;
class AppleUSBAudioStream;

/* THIS LOOKS UNUSED
typedef struct FrameListWriteInfo {
    AppleUSBAudioEngine *				audioEngine;
    UInt32								frameListNum;
    UInt32								retryCount;
} FrameListWriteInfo;
*/

class AppleUSBAudioEngine : public IOAudioEngine {
    friend class AppleUSBAudioDevice;
    friend class AppleUSBAudioStream;

    OSDeclareDefaultStructors (AppleUSBAudioEngine);

public:
    virtual bool init ( OSArray * streamInterfaceNumberArray );
    virtual void free ();
    virtual bool initHardware (IOService *provider);
    virtual bool start (IOService *provider, IOAudioDevice * device);
    virtual void stop (IOService *provider);
	virtual bool requestTerminate (IOService * provider, IOOptionBits options);
    virtual bool terminate (IOOptionBits options = 0);
	virtual	bool matchPropertyTable(OSDictionary * table, SInt32 *score);
    virtual void registerService(IOOptionBits options = 0);		// <rdar://7295322>

    virtual IOReturn performAudioEngineStart ();
    virtual IOReturn performAudioEngineStop ();
    
	virtual IOReturn pluginDeviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion);
	virtual void pluginSetConfigurationApp (const char * bundleID);
	virtual void registerPlugin (AppleUSBAudioPlugin * thePlugin);
	static void	pluginLoaded (AppleUSBAudioEngine * usbAudioEngineObject);
	
	virtual void openStreamInterfaces ( void );
	virtual void closeStreamInterfaces ( void );

	virtual OSString * getChannelNameString ( UInt8 unitID, UInt8 channelNum );													//	<rdar://6430836> <rdar://problem/6706026>
	virtual	void updateChannelNames ( OSArray* thePath, UInt8 interfaceNum, UInt8 altSettingNum );								//	<rdar://6430836> <rdar://problem/6706026>
	virtual char * ChannelConfigString (UInt8 channel);																			//	<rdar://6430836>
	
	virtual IOReturn doClockSelectorSetup (UInt8 interfaceNum, UInt8 altSettingNum, UInt32 sampleRate);							//	<rdar://5811247>
	static	IOReturn controlChangedHandler (OSObject *target, IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);	//	<rdar://5811247>
	virtual	IOReturn protectedControlChangedHandler (IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);			//	<rdar://5811247>
	virtual	IOReturn doClockSelectorChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue);					//	<rdar://5811247>
	virtual	IOReturn republishAvailableFormats ( void );																		//	<rdar://5811247>
	virtual	bool determineMacSyncMode ( UInt8 clockID );																		//	<rdar://5811247>
	virtual IOAudioSampleRate getCurrentClockPathSampleRate ( void );															//	<rdar://6945472>
	virtual void updateClockStatus ( UInt8 clockID );																			//	<rdar://5811247>
	virtual void runPolledTask ( void );																						//	<rdar://5811247>
	
protected:
	bool								mSplitTransactions;
	
	AppleUSBAudioDevice *				mUSBAudioDevice;
	
	void *								mReadBuffer;			
	thread_call_t						mPluginInitThread;
	IOAudioSampleRate					mCurSampleRate;
	UInt32								mLastClippedFrame;
	UInt32								mAverageSampleRate;
	Boolean								mUSBStreamRunning;
	Boolean								mTerminatingDriver;
	Boolean								mUHCISupport;
	AppleUSBAudioStream *				mMainOutputStream;
	AppleUSBAudioStream *				mMainInputStream;

	OSArray *							mStreamInterfaceNumberArray;
	OSArray *							mIOAudioStreamArray;
	UInt32								mStartInputChannelID;
	UInt32								mStartOutputChannelID;
	
	IONotifier *						mPluginNotification;
	AppleUSBAudioPlugin *				mPlugin;

	#if DEBUGLATENCY
		bool								mHaveClipped;
	#endif
	bool								mForceAdaptiveOutputMode;
	
	// Default sample rate are stored.
	IOAudioSampleRate					mDefaultAudioSampleRate;
	
	// Clock control
	IOAudioSelectorControl *			mClockSelectorControl;				//	<rdar://5811247>
	UInt8								mCurrentClockSourceID;				//	<rdar://5811247>
	UInt8								mCurrentClockPathGroupIndex;		//	<rdar://5811247>
	UInt8								mCurrentClockPathIndex;				//	<rdar://5811247>
	bool								mRestoreClockSelection;				//	<rdar://5811247>
	UInt32								mRestoreClockSelectionValue;		//	<rdar://5811247>
	bool								mShouldPollClockStatus;				//	<rdar://5811247>
	UInt32								mPollClockStatusCounter;			//	<rdar://5811247>
	Boolean								mClockSourceValidity;				//	<rdar://5811247>
	bool								mClockSourceValidityInitialized;	//	<rdar://5811247>
	bool								mShouldRepublishFormat;				//	<rdar://5811247>
	
	static inline IOFixed IOUFixedDivide(UInt32 a, UInt32 b)
	{
		return (IOFixed)((((UInt64) a) << 16) / ((UInt64) b));
	}

	static inline UInt32 IOUFixedMultiply(UInt32 a, UInt32 b)
	{
		return (UInt32)((((UInt64) a) * ((UInt64) b)) >> 16);
	}

	IOReturn	GetDefaultSampleRate (IOAudioSampleRate * sampleRate);	// added for rdar://3866513 

	virtual bool willTerminate (IOService * provider, IOOptionBits options);
	
	virtual OSString * getGlobalUniqueID ();

    virtual UInt32 getCurrentSampleFrame (void);

	virtual void resetClipPosition (IOAudioStream *audioStream, UInt32 clipSampleFrame);
    virtual IOReturn clipOutputSamples (const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
	virtual IOReturn convertInputSamples (const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
	
    virtual IOReturn performFormatChange (IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate);
	virtual	IOReturn controlledFormatChange (IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate);
public:
	virtual	void takeTimeStamp (bool incrementLoopCount = true, AbsoluteTime *timestamp = NULL);
protected:
	
	// for UHCI
	virtual IOReturn eraseOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);

	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	OSSet * copyDefaultAudioControls ( void );	

	UInt32 getSystemClockDomain ( void );	// <rdar://7558825>
};

#endif /* _APPLEUSBAUDIOENGINE_H */
