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
//	File:		AppleUSBAudioStream.h
//
//	Contains:	Support for the USB Audio Class Stream Interface.
//
//	Technology:	Mac OS X
//
//--------------------------------------------------------------------------------

#ifndef _APPLEUSBAUDIOSTREAM_H
#define _APPLEUSBAUDIOSTREAM_H

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

#include "AppleUSBAudioCommon.h"
#include "AppleUSBAudioDevice.h"
#include "AppleUSBAudioDictionary.h"
#include "AppleUSBAudioClip.h"

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

#define RECORD_NUM_USB_FRAME_LISTS				128
#define RECORD_NUM_USB_FRAMES_PER_LIST			2
#define RECORD_NUM_USB_FRAME_LISTS_TO_QUEUE		64

#define PLAY_NUM_USB_FRAME_LISTS				4
#define PLAY_NUM_USB_FRAMES_PER_LIST			64
#define PLAY_NUM_USB_FRAME_LISTS_TO_QUEUE		2
#define	PLAY_NUM_USB_FRAMES_PER_LIST_SYNC		32

// [rdar://5623096] Make note of the slowest polling interval in ms for feedback endpoints

#define kMaxFeedbackPollingInterval				512

#define kSampleFractionAccumulatorRollover		65536 * 1000	// <rdar://problem/6954295> Fractional part of mSamplesPerPacket stored x 1000

#define kMaxFilterSize							33				// <rdar://problem/7378275>
#define kFilterScale							1024			// <rdar://problem/7378275>

// <rdar://problem/6954295>
typedef struct _IOAudioSamplesPerFrame {
    UInt32	whole;
    UInt32	fraction;		// This fraction is stored x 1000 to preserve precision
} IOAudioSamplesPerFrame;

// <rdar://6411577> Overruns threshold in packets (about 2ms at 48kHz, close to the safety offset value)
#define kOverrunsThreshold						100

class AppleUSBAudioEngine;
class AppleUSBAudioPlugin;

class AppleUSBAudioStream : public IOAudioStream {
    friend class AppleUSBAudioDevice;
    friend class AppleUSBAudioEngine;

    OSDeclareDefaultStructors (AppleUSBAudioStream);

public:
    virtual bool initWithAudioEngine(AppleUSBAudioDevice * device, AppleUSBAudioEngine *engine, IOUSBInterface * streamInterface, IOAudioSampleRate sampleRate, const char *streamDescription = NULL, OSDictionary *properties = 0);
    virtual void free ();
//    virtual bool initHardware (IOService *provider);
    virtual void stop (IOService *provider);
	virtual bool requestTerminate (IOService * provider, IOOptionBits options);
    virtual bool terminate (IOOptionBits options = 0);
	virtual	bool matchPropertyTable(OSDictionary * table, SInt32 *score);
    virtual void registerService(IOOptionBits options = 0);			// <rdar://7295322>

    virtual IOReturn setFormat(const IOAudioStreamFormat *streamFormat, bool callDriver = true);																					// <rdar://7259238>
    virtual IOReturn setFormat(const IOAudioStreamFormat *streamFormat, const IOAudioStreamFormatExtension *formatExtension, OSDictionary *formatDict, bool callDriver = true);		// <rdar://7259238>

	virtual	bool configureAudioStream (IOAudioSampleRate sampleRate);
	virtual bool openStreamInterface ( void );
	virtual void closeStreamInterface ( void );
	
	virtual IOReturn pluginDeviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion);
	virtual void pluginSetConfigurationApp (const char * bundleID);
	virtual void registerPlugin (AppleUSBAudioPlugin * thePlugin);
	static void	pluginLoaded (AppleUSBAudioStream * usbAudioStreamObject);
	
	virtual UInt32 getRateFromSamplesPerPacket ( IOAudioSamplesPerFrame samplesPerPacket );	//  <rdar://problem/6954295>
	static void sampleRateHandler (void * target, void * parameter, IOReturn result, IOUSBIsocFrame * pFrames);
	#if PRIMEISOCINPUT
	static void primeInputPipeHandler (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames);
	#endif
    static void readHandler (void * object, void * frameListIndex, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames);
    static void writeHandler (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames);
	static void writeHandlerForUHCI (void * object, void * parameter, IOReturn result, IOUSBLowLatencyIsocFrame * pFrames);

protected:
	volatile UInt32						mCurrentFrameList;
	volatile UInt32						mShouldStop;
	UInt64								mUSBFrameToQueue;
	UInt64								mNextSyncReadFrame;
	
	UInt64 *							mFrameQueuedForList;
	
	// <rdar://problem/7378275>
	UInt64								mLastRawTimeStamp_nanos;
	UInt64								mLastFilteredTimeStamp_nanos;
	UInt64								mLastFilteredStampDifference;
	UInt64								mLastWrapFrame;
	Boolean								mInitStampDifference;
	UInt32								mNumTimestamp;
	UInt64								mFilterData[kMaxFilterSize];
	UInt32								mFilterWritePointer;
#if DEBUGTIMESTAMPS
	SInt64								mStampDrift;
#endif

	bool								mSplitTransactions;
	IORecursiveLock *					mCoalescenceMutex;						// <rdar://problem/7378275>
	
	IOUSBLowLatencyIsocFrame *			mUSBIsocFrames;
	IOUSBIsocFrame						mSampleRateFrame;
	IOUSBLowLatencyIsocCompletion *		mUSBCompletion;
	IOUSBIsocCompletion					mSampleRateCompletion;
	IOUSBInterface *					mStreamInterface;
	IOUSBPipe *							mPipe;
	IOUSBPipe *							mAssociatedPipe;
	AppleUSBAudioDevice *				mUSBAudioDevice;
	AppleUSBAudioEngine *				mUSBAudioEngine;
	IOBufferMemoryDescriptor *			mUSBBufferDescriptor;
	IOBufferMemoryDescriptor *			mSampleBufferMemoryDescriptor;
	#if PRIMEISOCINPUT
	IOUSBLowLatencyIsocFrame *			mPrimeInputIsocFrames;
	IOUSBLowLatencyIsocCompletion		mPrimeInputCompletion;
	#endif
	IOMultiMemoryDescriptor *			mWrapRangeDescriptor;
	IOSubMemoryDescriptor *				mWrapDescriptors[2];
	IOSubMemoryDescriptor **			mSampleBufferDescriptors;
	IOBufferMemoryDescriptor *			mAssociatedEndpointMemoryDescriptor;	// <rdar://7000283>
	
	bool								mMasterMode;
	bool								mSyncCompensation;
	
	thread_call_t						mPluginInitThread;
	AppleUSBAudioPlugin *				mPlugin;

	// UHCI additions
	
	UInt32								mSampleBufferSizeExtended;			
	UInt16								mNumFramesInFirstList;
	IOUSBLowLatencyIsocCompletion		mExtraUSBCompletion;
	UInt16								mAverageFrameSize;					// These sizes are in bytes.
	UInt16								mAlternateFrameSize;
	UInt16								mReadUSBFrameSize;

	// end UHCI additions
	
	void *								mReadBuffer;			
	UInt32 *							mAverageSampleRateBuffer;		// needs to be 4 bytes for a 10.14 or 16.16 value
	IOAudioSampleRate					mCurSampleRate;
	UInt32								mLastPreparedBufferOffset;
	UInt32								mSafeErasePoint;
	UInt32								mLastSafeErasePoint;
	UInt32								mReadUSBFrameListSize;
	UInt32								mBufferOffset;
	
	IOAudioSamplesPerFrame				mSamplesPerPacket;				// store this as a 16.16 value <rdar://problem/6954295>
	
	UInt32								mNumUSBFrameLists;
	UInt32								mNumUSBFramesPerList;
	UInt32								mNumTransactionsPerList;
	UInt32								mNumUSBFrameListsToQueue;
	UInt32								mSampleBufferSize;
	UInt32								mBytesPerSampleFrame;
	UInt32								mFractionalSamplesLeft;
	#if DEBUGLATENCY
		UInt32								mLastFrameListSize;
		UInt32								mThisFrameListSize;
	#endif
	UInt16								mSampleSize;
	UInt16								mSampleBitWidth;
	UInt32								mNumChannels;
	UInt16								mFramesUntilRefresh;
	UInt8								mInterfaceNumber;
	UInt8								mAlternateSettingID;
	UInt8								mRefreshInterval;
	UInt8								mFeedbackPacketSize;
	UInt8								mDirection;
	UInt8								mTransactionsPerUSBFrame;
	Boolean								mInCompletion;
	Boolean								mUSBStreamRunning;
	Boolean								mTerminatingDriver;
	Boolean								mUHCISupport;
	OSArray *							mActiveClockPath;

	Boolean								mNeedTimeStamps;
	bool								mHaveTakenFirstTimeStamp;
	// [rdar://5417631] Keep track of devices that generate overruns on input
	bool								mGeneratesOverruns;
	UInt32								mOverrunsCount;			// <rdar://6902105>
	UInt32								mOverrunsThreshold;		// <rdar://6411577>
		
	UInt64								mNumSampleRateFeedbackChangesCounter;
	UInt64								mNumSampleRateFeedbackEqualCounter;
	
	UInt16								mVendorID;
	UInt16								mProductID;
	
	// Default stream format and sample rate are stored.
	IOAudioStreamFormat					mDefaultAudioStreamFormat;
	IOAudioSampleRate					mDefaultAudioSampleRate;
	
	static inline IOFixed IOUFixedDivide(UInt32 a, UInt32 b)
	{
		return (IOFixed)((((UInt64) a) << 16) / ((UInt64) b));
	}

	static inline UInt32 IOUFixedMultiply(UInt32 a, UInt32 b)
	{
		return (UInt32)((((UInt64) a) * ((UInt64) b)) >> 16);
	}

	IOReturn	PrepareWriteFrameList (UInt32 usbFrameListIndex);
	IOReturn	PrepareAndReadFrameLists (UInt8 sampleSize, UInt8 numChannels, UInt32 usbFrameListIndex);
	IOReturn	setSampleRateControl (UInt8 address, UInt32 sampleRate);
	IOReturn	addAvailableFormats (AUAConfigurationDictionary * configDictionary);
	IOReturn	checkForFeedbackEndpoint (AUAConfigurationDictionary * configDictionary);
	
	void		queueInputFrames (void);
	void		queueOutputFrames (void);
	UInt16		getAlternateFrameSize (void);
	UInt8		getSyncType (void);
	UInt32		getLockDelayFrames (void);
		
	void		initializeUSBFrameList ( IOUSBLowLatencyIsocFrame * usbIsocFrames, UInt32 numFrames );	// <rdar://7568547>

	void		setMasterStreamMode ( bool masterMode ) { mMasterMode = masterMode; }
	void		compensateForSynchronization ( bool syncCompensation ) { mSyncCompensation = syncCompensation; }

	IOReturn	GetDefaultSettings (UInt8 * altSettingID, IOAudioSampleRate * sampleRate);	// added for rdar://3866513 

	virtual bool willTerminate (IOService * provider, IOOptionBits options);

    virtual IOReturn readFrameList (UInt32 frameListNum);
    virtual IOReturn writeFrameList (UInt32 frameListNum);

	virtual IOReturn prepareUSBStream (void);
    virtual IOReturn startUSBStream (UInt64 currentUSBFrame, UInt32 usbFramesToDelay);
    virtual IOReturn stopUSBStream ();

    virtual UInt32 getCurrentSampleFrame (void);

	virtual IOReturn CoalesceInputSamples (UInt32 numBytesToCoalesce, IOUSBLowLatencyIsocFrame * pFrames);
	
	virtual	IOReturn controlledFormatChange (const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate);
	void calculateSamplesPerPacket (UInt32 sampleRate, UInt16 * averageFrameSize, UInt16 * additionalSampleFrameFreq);
	void updateSampleOffsetAndLatency (void);
	#if DEBUGLATENCY
	virtual UInt64 getQueuedFrameForSample (UInt32 sampleFrame);
	#endif
	#if PRIMEISOCINPUT
	virtual	void primeInputPipe (IOUSBPipe * pipeToPrime, UInt32 bytesPerUSBFrame, UInt32 usbFramesToDelay);
	#endif
	virtual AbsoluteTime generateTimeStamp (SInt32 usbFrameIndex, UInt32 preWrapBytes, UInt32 byteCount);
	virtual IOReturn copyAnchor (UInt64 anchorFrame, AbsoluteTime * anchorTime, UInt64 * usbCycleTime); 	// <rdar://problem/7378275>
	virtual	UInt64 jitterFilter (UInt64 curr, UInt32 nIter);												// <rdar://problem/7378275>
	
	virtual	void takeTimeStamp (bool incrementLoopCount = true, AbsoluteTime *timestamp = NULL);

	// for IODMACommand
	virtual IOBufferMemoryDescriptor * allocateBufferDescriptor (IOOptionBits options, vm_size_t capacity, vm_offset_t alignment = 1);
	
};

class AppleUSBAudioStreamNode : public IOService 					//<radr://6686515>
{
    OSDeclareDefaultStructors ( AppleUSBAudioStreamNode );

public:

	virtual	bool		start ( IOService * provider );	
};


#endif /* _APPLEUSBAUDIOSTREAM_H */
