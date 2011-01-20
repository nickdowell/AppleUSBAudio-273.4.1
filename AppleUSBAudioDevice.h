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
//	File:		AppleUSBAudioDevice.h
//
//	Contains:	Support for the USB Audio Class Control Interface.
//
//	Technology:	OS X
//
//--------------------------------------------------------------------------------

#ifndef _APPLEUSBAUDIODEVICE_H
#define _APPLEUSBAUDIODEVICE_H

#include <libkern/c++/OSCollectionIterator.h>

#include <IOKit/IOLocks.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOMessage.h>

#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioPort.h>
#include <IOKit/audio/IOAudioControl.h>
#include <IOKit/audio/IOAudioStream.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/audio/IOAudioSelectorControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>	// <rdar://problem/7512583>

#include <IOKit/usb/USB.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/usb/IOUSBRootHubDevice.h>

#include "AppleUSBAudioEngine.h"
#include "AppleUSBAudioCommon.h"
#include "AppleUSBAudioDictionary.h"
#include "BigNum.h"						// <rdar://7446555>

#define kStringBufferSize				255
// The following value is defined in USB 1.0 Class Spec section 5.2.2.4.3.2
#define kNegativeInfinity				0x8000

enum {
	kIOUSBVendorIDHarmonKardon			= 0x05FC, 
	kIOUSBVendorMicronas				= 0x074D
};

enum {
	kVolumeControl						= 1,
	kMuteControl						= 2
};

enum {
	kStudioDisplay15CRT					= 0x9115,
	kStudioDisplay17CRT					= 0x9113,
	kCinemaDisplay						= 0x9116,
	kStudioDisplay17FP					= 0x9117
};

enum {
	kAudioStatusWordFormat				= 1,
	kInterruptDataMessageFormat			= 2
};

// <rdar://7446555>

#define MAX_ANCHOR_ENTRIES			4096 					// <rdar://problem/7666699>
#define MIN_ENTRIES_APPLY_OFFSET	MAX_ANCHOR_ENTRIES / 4	// <rdar://problem/7666699>
#define MIN_FRAMES_APPLY_OFFSET		512						// <rdar://problem/7666699>

typedef struct
{
	U64		X[MAX_ANCHOR_ENTRIES];
	U64		Y[MAX_ANCHOR_ENTRIES];
	U128	XX[MAX_ANCHOR_ENTRIES];
	U128	XY[MAX_ANCHOR_ENTRIES];
	UInt32	index;
	UInt32	n;
	U64		sumX;
	U64		sumY;
	U128	sumXX;
	U128	sumXY;
#if (MAX_ANCHOR_ENTRIES <= 1024)
	U128	P;
	U128	Q;
#else
	U256	P;
	U256	Q;
#endif
	U256	QSumY;
	U256	Qn;
	U128	mExtraPrecision;
	
	UInt32	calculateOffset;		// <rdar://problem/7666699>
	bool	deviceStart;			// <rdar://problem/7666699>

} ANCHORTIME;

class IOUSBInterface;
class AppleUSBAudioEngine;

/*
 * @class AppleUSBAudioDevice
 * @abstract : universal Apple USB driver
 * @discussion : Current version of the driver deals with outputing stereo 16 bits data at 44100kHz
 */

#define kEngine							"engine"
#define kInterface						"interface"
#define kAltSetting						"altsetting"
#define kInputGainControls				"inputgaincontrols"
#define kInputMuteControls				"inputmutecontrols"
#define kOutputMuteControls				"outputmutecontrols"			//	<rdar://6413207>
#define kPassThruVolControls			"passthruvolcontrols"			//	<rdar://5366067>
#define kPassThruToggleControls			"passthrutogglecontrols"		//	<rdar://5366067>
#define kOutputVolControls				"outputvolcontrols"
#define kPassThruPathsArray				"passthrupathsarray"
#define kPassThruSelectorControl		"passthruselectorcontrol"		//	<rdar://5366067>

#define kWallTimeExtraPrecision         10000ull
#define kMaxWallTimePerUSBCycle			1001000ull
#define kMinWallTimePerUSBCycle			999000ull

#define kMaxTimestampJitter				10000ull						// <rdar://7378275>

#define kDisplayRoutingPropertyKey		"DisplayRouting"				// <rdar://problem/7349398>

class AppleUSBAudioDevice : public IOAudioDevice {
    OSDeclareDefaultStructors (AppleUSBAudioDevice);

public:
    IOUSBInterface *					mControlInterface;
	
	// These member variables are for anchored time stamp algorithm
	
	UInt64								mWallTimePerUSBCycle;
	UInt64								mLastUSBFrame;
	UInt64								mLastWallTime_nanos;
	#if DEBUGANCHORS
	UInt64								mAnchorFrames[kAnchorsToAccumulate];
	AbsoluteTime						mAnchorTimes[kAnchorsToAccumulate];
	#endif

	ANCHORTIME							mAnchorTime;					// <rdar://7378275>
	IOLock *							mTimeLock;						// <rdar://7378275>
	UInt64								mRampUpdateCounter;				// <rdar://problem/7666699>
	UInt64								Xcopy[MAX_ANCHOR_ENTRIES];		// <rdar://problem/7666699>
	UInt64								Ycopy[MAX_ANCHOR_ENTRIES];		// <rdar://problem/7666699>

protected:
	AUAConfigurationDictionary *		mConfigDictionary;
	OSArray *							mControlGraph;
	OSArray *							mClockGraph;
    IORecursiveLock *					mInterfaceLock;
	IORecursiveLock *					mRegisteredEnginesMutex;	//  <rdar://problem/6369110>
	IORecursiveLock *					mRegisteredStreamsMutex;	//  <rdar://problem/6420832>
	Boolean								mTerminatingDriver;
	thread_call_t						mInitHardwareThread;		//  <rdar://problem/6686515>
	thread_call_t						mRetryEQDownloadThread;
	thread_call_t						mProcessStatusInterruptThread;	// <rdar://problem/6021475>
	Boolean								mDeviceIsInMonoMode;
	OSArray *							mMonoControlsArray;		// this flag is set by AppleUSBAudioEngine::performFormatChange
	OSArray *							mRegisteredEngines;
	OSArray *							mRegisteredStreams;			//	<rdar://6420832>
	
	// These member variables are for anchored time stamp algorithm
	
	IOTimerEventSource *				mUpdateTimer;
	bool								mSingleSampleRateDevice;
	AppleUSBAudioEngine *				mFailingAudioEngine;
	// [rdar://5355808]
	AppleUSBAudioEngine *				mEngineToRestart;
	UInt32								mTimerCallCount;			//  <rdar://problem/7378275>
	
	// This is for emergency device recovery
	bool								mShouldAttemptDeviceRecovery;

	OSArray *							mEngineArray;
	
	bool								mHasAdaptiveAsynchronousOutput;
	bool								mMultipleAudioEngineDevice;
	
	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	IOUSBPipe *							mInterruptPipe;
	IOBufferMemoryDescriptor *			mInterruptEndpointMemoryDescriptor;	// <rdar://7000283>
	bool								mInterruptPipeStalled;				// <rdar://7865729>
	void *								mStatusInterruptBuffer;
	UInt32								mStatusInterruptBufferType;		//	<rdar://5811247>
	IOUSBCompletion						mStatusInterruptCompletion;
	
public:
	virtual	bool			start (IOService * provider);
    virtual	void			free ();
    virtual	void			stop (IOService *provider);
	virtual	bool			initHardware (IOService * provider);
	static	void			initHardwareThread (AppleUSBAudioDevice * aua, void * provider);	//  <rdar://problem/6686515>
	static	IOReturn		initHardwareThreadAction (OSObject * owner, void * provider, void * arg2, void * arg3, void * arg4);	//  <rdar://problem/6686515>
	virtual	IOReturn		protectedInitHardware (IOService * provider);
	virtual bool			allEnginesStopped ( void );		// <rdar://problem/7666699>
	virtual	bool			createAudioEngines (void);
	virtual	bool			activateAudioEngines (void);
	virtual	bool			createAudioEngine (OSArray * streamInterfaceNumberArray);
	virtual IOReturn		interfaceContainsSupportedFormat ( UInt8 streamInterface, bool *formatSupported );	//  <rdar://problem/6892754>
	virtual	Boolean			ShouldUpdatePRAM (void);
			Boolean			FindSoundNode (void);
    virtual	IOReturn		message (UInt32 type, IOService * provider, void * arg);
	virtual	bool			ControlsStreamNumber (UInt8 streamNumber);
	virtual	IOReturn		createControlsForInterface (IOAudioEngine *audioEngine, UInt8 interfaceNum, UInt8 altSettingNum);
	virtual void			setConfigurationApp (const char *bundleID);
	virtual	IOReturn		performPowerStateChange (IOAudioDevicePowerState oldPowerState, IOAudioDevicePowerState newPowerState, UInt32 *microSecsUntilComplete);
	virtual	bool			willTerminate (IOService * provider, IOOptionBits options);
	virtual	OSArray * 		BuildConnectionGraph (UInt8 controlInterfaceNum);
	virtual	OSArray * 		BuildPath (UInt8 controlInterfaceNum, UInt8 startingUnitID, OSArray *allPaths, OSArray * thisPath);	
	virtual OSArray *		buildClockGraph (UInt8 controlInterfaceNum);																// [rdar://4801032]
	virtual OSArray *		buildClockPath (UInt8 controlInterfaceNum, UInt8 startingUnitID, OSArray *allPaths, OSArray * thisPath);	// [rdar://4801032]
	virtual IOReturn		addSampleRatesFromClockSpace ( void );																// [rdar://4867779]
	virtual IOReturn		addSampleRatesFromClockPath ( OSArray * path, UInt8 streamInterface, UInt8 altSetting );			// [rdar://4867779]
	virtual IOReturn		getClockSourceSampleRates ( OSArray ** sampleRates, UInt8 clockSource );							// [rdar://4867779]
	virtual OSArray *		getOptimalClockPath ( AppleUSBAudioEngine * thisEngine, UInt8 streamInterface, UInt8 altSetting, UInt32 sampleRate, Boolean * otherEngineNeedSampleRateChange, UInt8 * clockPathGroupIndex = NULL );	// [rdar://4867843], <rdar://5811247>
	virtual	OSArray *		getClockPathGroup ( UInt8 streamInterface, UInt8 altSetting, UInt8 * clockPathGroupIndex = NULL );		//	<rdar://5811247>
	virtual	OSArray *		getClockPathGroup ( UInt8  pathGroupIndex );														//	<rdar://5811247>
	virtual	IOReturn		getClockSelectorIDAndPathIndex (UInt8 * selectorID, UInt8 * pathIndex, OSArray * clockPath);		//	<rdar://5811247>
	virtual Boolean			supportSampleRateInClockPath ( OSArray * pathArray, UInt32 sampleRate );																							// [rdar://4867843]
	virtual UInt32			determineClockPathUnitUsage ( AppleUSBAudioEngine * thisEngine, OSArray * thisClockPath );																			// [rdar://4867843]
	virtual Boolean			clockPathCrossed ( OSArray * clockPathA, OSArray * clockPathB );																									// [rdar://4867843]
	virtual	char * 			TerminalTypeString (UInt16 terminalType);
	virtual	char * 			ClockTypeString (UInt8 clockType);								//	<rdar://5811247>

	virtual	IOReturn		registerEngineInfo (AppleUSBAudioEngine * usbAudioEngine);		//	<rdar://6420832>
	virtual	SInt32			getEngineInfoIndex (AppleUSBAudioEngine * inAudioEngine);
	virtual	IOReturn		registerStreamInfo (UInt8 interfaceNum, UInt8 altSettingNum);	//	<rdar://6420832>
	virtual	SInt32			getStreamInfoIndex (UInt8 interfaceNum);						//	<rdar://6420832>						
	virtual	IOReturn		doControlStuff (IOAudioEngine *audioEngine, UInt8 interfaceNum, UInt8 altSettingNum);
	virtual	IOReturn		doPlaythroughSetup (AppleUSBAudioEngine * usbAudioEngine, OSArray * playThroughPaths, UInt8 interfaceNum, UInt8 altSettingNum, UInt8 inputTerminalID);	//	<rdar://5366067>
	virtual	UInt32			getPathIndexForSelectorSetting (OSArray * arrayOfPathsFromOutputTerminal, UInt32 pathsToOutputTerminalN, UInt32 graphPathIndex, UInt8 selectorUnitIndex, UInt8 selectorSetting);	//	<rdar://6523676>, <rdar://problem/6656791>
	virtual	IOReturn		addSelectorSourcesToSelectorControl (IOAudioSelectorControl * theSelectorControl, OSArray * arrayOfPathsFromOutputTerminal, UInt32 pathsToOutputTerminalN, UInt32 graphPathIndex, UInt8 selectorIndex);	// <rdar://problem/6656791>
	virtual	UInt8			getDefaultOutputTerminalID (UInt8 inputTerminalID);							//	<rdar://6413207>
	virtual	UInt32			getNumConnectedOutputTerminals (UInt8 inputTerminalID);						//	<rdar://6413207>
	virtual	OSString *		getNameForTerminal (UInt8 terminalID, UInt8 direction);						//	<rdar://6430836>
	virtual	OSString *		getNameForPath (OSArray * arrayOfPathsFromOutputTerminal, UInt32 * pathIndex, UInt8 startingPoint);
	virtual	OSString *		getNameForMixerPath (OSArray * arrayOfPathsFromOutputTerminal, UInt32 * pathIndex, UInt8 startingPoint);

	static	IOReturn		controlChangedHandler (OSObject *target, IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);
	virtual	IOReturn		protectedControlChangedHandler (IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);
	virtual	IOReturn		doSelectorControlChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue);
	virtual	UInt8			getSelectorSetting (UInt8 selectorID);
	virtual	IOReturn		setSelectorSetting (UInt8 selectorID, UInt8 setting);
	virtual IOReturn		getFeatureUnitRange (UInt8 controlSelector, UInt8 unitID, UInt8 channelNumber, UInt8 requestType, SubRange16 * target) ;
	virtual	IOReturn		getFeatureUnitSetting (UInt8 controlSelector, UInt8 unitID, UInt8 channelNumber, UInt8 requestType, SInt16 * target);
	virtual	IOReturn		setFeatureUnitSetting (UInt8 controlSelector, UInt8 unitID, UInt8 channelNumber, UInt8 requestType, UInt16 newValue, UInt16 newValueLen);
	virtual	OSArray *		getPlaythroughPaths (UInt8 inputTerminalID);	//	<rdar://5366067>
	virtual	UInt8			getBestFeatureUnitInPath (OSArray * thePath, UInt32 direction, UInt8 interfaceNum, UInt8 altSettingNum, UInt32 controlTypeWanted);
	virtual	void			addVolumeControls (AppleUSBAudioEngine * usbAudioEngine, UInt8 featureUnitID, UInt8 terminalID, UInt8 interfaceNum, UInt8 altSettingNum, UInt32 usage);	// <rdar://6413207>
	virtual	void			addMuteControl (AppleUSBAudioEngine * usbAudioEngine, UInt8 featureUnitID, UInt8 terminalID, UInt8 interfaceNum, UInt8 altSettingNum, UInt32 usage);	// <rdar://6413207>
	virtual	IOReturn		getCurMute (UInt8 unitID, UInt8 channelNumber, SInt16 * target);
	virtual	IOReturn		getCurVolume (UInt8 unitID, UInt8 channelNumber, SInt16 * target);
	virtual	IOReturn		getMaxVolume (UInt8 unitID, UInt8 channelNumber, SInt16 * target);
	virtual	IOReturn		getMinVolume (UInt8 unitID, UInt8 channelNumber, SInt16 * target);
	virtual	IOReturn		getVolumeResolution (UInt8 unitID, UInt8 channelNumber, UInt16 * target);
	virtual	IOReturn		setCurVolume (UInt8 unitID, UInt8 channelNumber, SInt16 volume);
	virtual	IOReturn		setCurMute (UInt8 unitID, UInt8 channelNumber, SInt16 mute);
	virtual	IOReturn		doInputSelectorChange (IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);
	virtual	IOReturn		doOutputSelectorChange (IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);		//	<rdar://6413207>
	virtual	IOReturn		doVolumeControlChange (IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);
	virtual	IOReturn		doToggleControlChange (IOAudioControl *audioControl, SInt32 oldValue, SInt32 newValue);
	virtual IOReturn		doPassThruSelectorChange (IOAudioControl * audioControl, SInt32 oldValue, SInt32 newValue);
	virtual	IOFixed			ConvertUSBVolumeTodB (SInt16 volume);
	virtual void			setMonoState (Boolean state);
	virtual IOUSBInterface *getUSBInterface ( UInt8 interfaceNumber );
	virtual UInt8			getDeviceSpeed (); // added for rdar://4168019
	virtual	UInt64			getUSBFrameNumber ();
	virtual	UInt8			getManufacturerStringIndex ();
	virtual	UInt8			getProductStringIndex ();
	virtual	UInt8			getSerialNumberStringIndex ();
    virtual IOReturn		getStringDescriptor(UInt8 index, char *buf, int maxLen, UInt16 lang=0x409);
	virtual UInt16			getVendorID ();
	virtual UInt16			getProductID ();
	virtual OSNumber *		getLocationID ();
	virtual	bool			detectSplitTransactions (); // added for rdar://3959606
	virtual Boolean			checkForUHCI (); // added for rdar://3727110

	virtual	const IOUSBConfigurationDescriptor *	getConfigurationDescriptor (); // added for rdar://5495653
	
	virtual AUAConfigurationDictionary *	getConfigDictionary (void) {return mConfigDictionary;}

	virtual	IOReturn		deviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion = NULL);			// Depricated, don't use
	virtual	IOReturn		deviceRequest (IOUSBDevRequestDesc * request, IOUSBCompletion * completion = NULL);
	static	IOReturn		deviceRequest (IOUSBDevRequest * request, AppleUSBAudioDevice * self, IOUSBCompletion * completion = 0);

	#ifdef DEBUG
    virtual void			retain() const;
    virtual void			release() const;
	#endif
	virtual bool			matchPropertyTable (OSDictionary * table, SInt32 *score);

	// The following methods are for the anchored time stamp algorithm
	virtual IOReturn		getAnchorFrameAndTimeStamp (UInt64 *frame, AbsoluteTime *time);
	virtual UInt64			getWallTimeInNanos (void);
	
	// The following method is for regulating format changes
	virtual	UInt32			formatChangeController (IOAudioEngine *audioEngine, IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate);
	virtual bool			getSingleSampleRateDevice (void) {return mSingleSampleRateDevice;};
	virtual void			setSingleSampleRateDevice (bool isSingleSampleRateDevice) {mSingleSampleRateDevice = isSingleSampleRateDevice;};
	virtual void			setShouldSyncSampleRates (AppleUSBAudioEngine * problemEngine) {mFailingAudioEngine = problemEngine;};
	// [rdar://5355808]
	virtual void			setShouldResetEngine ( AppleUSBAudioEngine * problemEngine ) { mEngineToRestart = problemEngine; };
	
	virtual	AppleUSBAudioEngine * otherEngine (AppleUSBAudioEngine * thisEngine);
	virtual IOReturn		getBothEngines (AppleUSBAudioEngine ** firstEngine, AppleUSBAudioEngine ** secondEngine);
	
	virtual bool			getMultipleAudioEngineDevice (void) {return mMultipleAudioEngineDevice;};
	virtual void			setMultipleAudioEngineDevice (bool isMultipleAudioEngineDevice) {mMultipleAudioEngineDevice = isMultipleAudioEngineDevice;};

	virtual bool			getAdaptiveAsynchronousOutput (void) {return mHasAdaptiveAsynchronousOutput;}
	virtual void			setAdaptiveAsynchronousOutput (bool hasAdaptiveAsynchronousOutput) {mHasAdaptiveAsynchronousOutput = hasAdaptiveAsynchronousOutput;}

	// This is how AppleUSBAudio attempts device recovery.
	virtual void			attemptDeviceRecovery (void);
	virtual void			requestDeviceRecovery (void) {mShouldAttemptDeviceRecovery = true;};
	virtual bool			recoveryRequested (void) {return mShouldAttemptDeviceRecovery;};
	
	// The following methods are for clock entities requests.
	virtual IOReturn		getClockSetting (UInt8 controlSelector, UInt8 unitID, UInt8 requestType, void * target, UInt16 length);
	virtual IOReturn		setClockSetting (UInt8 controlSelector, UInt8 unitID, UInt8 requestType, void * target, UInt16 length);
	virtual	IOReturn		getNumClockSourceSamplingFrequencySubRanges (UInt8 unitID, UInt16 * numSubRanges);
	virtual IOReturn		getIndexedClockSourceSamplingFrequencySubRange (UInt8 unitID, SubRange32 * subRange, UInt16 subRangeIndex);
	virtual IOReturn		getCurClockSourceSamplingFrequency (UInt8 unitID, UInt32 * samplingFrequency, bool * validity);
	virtual IOReturn		setCurClockSourceSamplingFrequency (UInt8 unitID, UInt32 samplingFrequency);
	virtual IOReturn		getCurClockSelector (UInt8 unitID, UInt8 * selector);
	virtual IOReturn		setCurClockSelector (UInt8 unitID, UInt8 selector);
	virtual IOReturn		getCurClockMultiplier (UInt8 unitID, UInt16 * numerator, UInt16 * denominator);

	// The following methods are for discovering & manipulate the sample rates for a clock path.
	virtual IOReturn		getNumSampleRatesForClockPath (UInt8 * numSampleRates, OSArray * clockPath);
	virtual IOReturn		getIndexedSampleRatesForClockPath (SubRange32 * sampleRates, OSArray * clockPath, UInt32 rangeIndex);
	virtual IOReturn		getClockPathCurSampleRate (UInt32 * sampleRate, Boolean * validity, Boolean * isReadOnly, OSArray * clockPath);		//	<rdar://6945472>
	virtual IOReturn		setClockPathCurSampleRate (UInt32 sampleRate, OSArray * clockPath, bool failIfReadOnly = false);					//	<rdar://6945472>
	
	// The following methods are for determining whether the device is capable of having all its audio stream in one audio engine.
	virtual OSArray *		findStreamsWithCommonSampleRates (OSArray * availableStreamList);
	virtual bool			isSampleRateCommonWithAtLeastOneStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList);
	virtual bool			isSampleRateCommonWithAllStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList);
	virtual bool			streamsHaveCommonSampleRates (OSNumber * streamInterfaceNumberA, OSNumber * streamInterfaceNumberB);
	virtual OSArray *		getSampleRatesFromStreamInterface (OSNumber * streamInterfaceNumber);
	virtual void			mergeSampleRates (OSArray * thisArray, OSArray * otherArray);
	virtual bool			compareSampleRates (OSArray * sampleRatesA, OSArray * sampleRatesB);
	virtual OSArray *		findStreamsWithCompatibleEndpoints (OSArray * availableStreamList);
	virtual OSArray *		findStreamsWithDirectionAndSyncType (OSArray * availableStreamList, UInt8 direction, UInt8 syncType);
	virtual bool			streamEndpointsHaveSpecifiedDirectionAndSyncType (OSNumber * streamInterfaceNumber, UInt8 endpointDirection, UInt8 endpointSyncType);
	virtual OSArray *		findStreamsWithCommonClocks (OSArray * availableStreamList);
	virtual bool			isClockCommonWithAtLeastOneStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList);
	virtual bool			isClockCommonWithAllStreamsInList (OSNumber * refStreamInterfaceNumber, OSArray * streamInterfaceNumberList);
	virtual bool			streamsHaveCommonClocks (OSNumber * streamInterfaceNumberA, OSNumber * streamInterfaceNumberB);
	virtual bool			isClockPathCrossed (OSNumber * streamInterfaceNumber, OSArray * otherClockPath, UInt32 sampleRate);

	// The following methods are used for status interrupt endpoint support <rdar://problem/6021475>
	void					checkForStatusInterruptEndpoint ( void );
	static void				statusInterruptHandler ( void * target, void * parameter, IOReturn status, UInt32 bufferSizeRemaining );
	void					controlHasChangedOnDevice ( UInt8 controlID, OSSet * defaultAudioControls );
	static void				processStatusInterrupt ( void * arg );
	static IOReturn			runStatusInterruptTask ( OSObject * target, void * arg0, void * arg1, void * arg2, void * arg3 );	
	void					handleStatusInterrupt ( void );
	
	UInt64					getTimeForFrameNumber ( UInt64 frameNumber );					// <rdar://7378275>
	virtual void			updateUSBCycleTime ( void );									// <rdar://7378275>
	virtual void			calculateOffset ( void );										// <rdar://problem/7666699>
	virtual void			applyOffsetAmountToFilter ( void );								// <rdar://problem/7666699>

private:
	virtual void			resetRateTimer ();	// [rdar://5165798]
	virtual UInt64			lastAnchorFrame ( void );										// <rdar://problem/7666699>
	static	void			TimerAction (OSObject * owner, IOTimerEventSource * sender);
	virtual	void			doTimerAction (IOTimerEventSource * timer);
	#if DEBUGANCHORS
	virtual void			accumulateAnchor (UInt64 anchorFrame, AbsoluteTime timeStamp);
	#endif
	virtual UInt8			pathsContaining (UInt8 unitID);
	virtual	UInt8			pathsContainingFeatureUnitButNotMixerUnit (UInt8 featureUnitID, UInt8 mixerUnitID);		//	<rdar://5366067>
	
};

#endif /* _APPLEUSBAUDIODEVICE_H */

