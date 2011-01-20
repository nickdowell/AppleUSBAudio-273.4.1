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
 
#include <libkern/c++/OSArray.h>
#include <IOKit/usb/IOUSBInterface.h>

#include "AppleUSBAudioCommon.h"

#ifndef __AppleUSBAudioDictionary__
#define __AppleUSBAudioDictionary__

#pragma mark Constants
#define	kUSBAudioStreamInterfaceSubclass	2
#define	kRootAlternateSetting				0
#define	kMaxPacketSize_Mask					0x07FF	// <rdar://problem/6789044>
#define	kTransactionsPerMicroframe_Mask		0x1800	// <rdar://problem/6789044>

enum {
	kMuteBit					= 0,
	kVolumeBit					= 1,
	kBassBit					= 2,
	kMidBit						= 3,
	kTrebleBit					= 4,
	kEQBit						= 5,
	kAGBit						= 6,
	kDelayBit					= 7,
	kBassBoostBit				= 8,
	kLoudnessBit				= 9
};

#define CLASS_PROPERTY_NAME					"class"
#define SUBCLASS_PROPERTY_NAME				"subClass"
#define PROTOCOL_PROPERTY_NAME				"protocol"
#define VENDOR_PROPERTY_NAME				"vendor"
#define PRODUCT_PROPERTY_NAME				"product"
#define VERSION_PROPERTY_NAME				"version"

enum {
    sampleFreqControlBit					= 0,
    pitchControlBit							= 1,
    maxPacketsOnlyBit						= 7
};

enum {
	kNoneSyncType							= 0x00,
	kAsynchSyncType							= 0x01,
	kAdaptiveSyncType						= 0x02,
	kSynchronousSyncType					= 0x03,
	kUnknownSyncType						= 0xFF
};

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
enum {
	kInterruptType							= 0x03
};

enum {
    DEVICE									= 0x01,
    CONFIGURATION							= 0x02,
    STRING									= 0x03,
    INTERFACE								= 0x04,
    ENDPOINT								= 0x05,
	DEVICE_QUALIFIER						= 0x06,
	OTHER_SPEED_CONFIGURATION				= 0x07,
	INTERFACE_ASSOCIATION					= 0x0B
};

enum {
	GET_STATUS								= 0x00,
	CLEAR_FREATURE							= 0x01,
	SET_FEATURE								= 0x03,
	SET_ADDRESS								= 0x05,
	GET_DESCRIPTOR							= 0x06,
	SET_DESCRIPTOR							= 0x07,
	GET_CONFIGURATION						= 0x08,
	SET_CONFIGURATION						= 0x09,
	GET_INTERFACE							= 0x0A,
	SET_INTERFACE							= 0x0B,
	SYNCH_FRAME								= 0x0C
};

enum {
    // For descriptor type
    CS_UNDEFINED							= 0x20,
    CS_DEVICE								= 0x21,
    CS_CONFIGURATION						= 0x22,
    CS_STRING								= 0x23,
    CS_INTERFACE							= 0x24,
    CS_ENDPOINT								= 0x25
};

enum {
	// Audio Interface Class Code
    AUDIO									= 0x01
};

enum {
	// Audio Interface Subclass Codes
	SUBCLASS_UNDEFINED						= 0x00,
    AUDIOCONTROL							= 0x01,
    AUDIOSTREAMING							= 0x02,
	MIDISTREAMING							= 0x03,
	VENDOR_SPECIFIC							= 0xff
};

enum {
    // Audio Control (AC) interface descriptor subtypes
    AC_DESCRIPTOR_UNDEFINED					= 0x00,
    HEADER									= 0x01,
    INPUT_TERMINAL							= 0x02,
    OUTPUT_TERMINAL							= 0x03,
    MIXER_UNIT								= 0x04,
    SELECTOR_UNIT							= 0x05,
    FEATURE_UNIT							= 0x06,
    PROCESSING_UNIT							= 0x07,
    EXTENSION_UNIT							= 0x08
};

enum {
    USB_STREAMING							= 0x0101
};

enum {
    // Audio Stream (AS) interface descriptor subtypes
    AS_DESCRIPTOR_UNDEFINED					= 0x00,
    AS_GENERAL								= 0x01,
    FORMAT_TYPE								= 0x02,
    FORMAT_SPECIFIC							= 0x03
};

enum {
    FORMAT_TYPE_UNDEFINED					= 0x00,
    FORMAT_TYPE_I							= 0x01,
    FORMAT_TYPE_II							= 0x02,
    FORMAT_TYPE_III							= 0x03
};

enum {
    // Audio data format type I codes
    TYPE_I_UNDEFINED						= 0x0000,
    PCM										= 0x0001,
    PCM8									= 0x0002,
    IEEE_FLOAT								= 0x0003,
    ALAW									= 0x0004,
    MULAW									= 0x0005
};

enum {
	// Audio data format type II codes
	TYPE_II_UNDEFINED						= 0x1000,
	MPEG									= 0x1001,
	AC3										= 0x1002
};

enum {
	// Audio data format type III codes
	TYPE_III_UNDEFINED						= 0x2000,
	IEC1937_AC3								= 0x2001,
	IEC1937_MPEG1_Layer1					= 0x2002,
	IEC1937_MPEG1_Layer2or3					= 0x2003,
	IEC1937_MPEG2_NOEXT						= 0x2003,
	IEC1937_MPEG2_EXT						= 0x2004,
	IEC1937_MPEG2_Layer1_LS					= 0x2005,
	IEC1937_MPEG2_Layer2or3_LS				= 0x2006
};

enum {
	// MPEG control selectors
	MPEG_CONTROL_UNDEFINED					= 0x00,
	MP_DUAL_CHANNEL_CONTROL					= 0x01,
	MP_SECOND_STEREO_CONTROL				= 0x02,
	MP_MULTILINGUAL_CONTROL					= 0x03,
	MP_DYN_RANGE_CONTROL					= 0x04,
	MP_SCALING_CONTROL						= 0x05,
	MP_HILO_SCALING_CONTROL					= 0x06
};

enum {
	// AC-3 control selectors
	AC_CONTROL_UNDEFINED					= 0x00,
	AC_MODE_CONTROL							= 0x01,
	AC_DYN_RANGE_CONTROL					= 0x02,
	AC_SCALING_CONTROL						= 0x03,
	AC_HILO_SCALING_CONTROL					= 0x04
};

enum {
    // Audio Class-specific endpoint descriptor subtypes
    DESCRIPTOR_UNDEFINED					= 0x00,
    EP_GENERAL								= 0x01
};

enum {
    REQUEST_CODE_UNDEFINED					= 0x00,
    SET_CUR									= 0x01,
    GET_CUR									= 0x81,
    SET_MIN									= 0x02,
    GET_MIN									= 0x82,
    SET_MAX									= 0x03,
    GET_MAX									= 0x83,
    SET_RES									= 0x04,
    GET_RES									= 0x84,
    SET_MEM									= 0x05,
    GET_MEM									= 0x85,
    GET_STAT								= 0xff
};

enum {
    FU_CONTROL_UNDEFINED					= 0x00,
    MUTE_CONTROL							= 0x01,
    VOLUME_CONTROL							= 0x02,
    BASS_CONTROL							= 0x03,
    MID_CONTROL								= 0x04,
    TREBLE_CONTROL							= 0x05,
    GRAPHIC_EQUALIZER_CONTROL				= 0x06,
    AUTOMATIC_GAIN_CONTROL					= 0x07,
    DELAY_CONTROL							= 0x08,
    BASS_BOOST_CONTROL						= 0x09,
    LOUDNESS_CONTROL						= 0x0a
};

enum {
	EP_CONTROL_UNDEFINED					= 0x00,
	SAMPLING_FREQ_CONTROL					= 0x01,
	PITCH_CONTROL							= 0x02
};

enum {
	INTERFACE_PROTOCOL_UNDEFINED			= 0x00,
	IP_VERSION_02_00						= 0x20
};

namespace USBAUDIO_0200 {
// USB Device Class Specification for Audio Devices Release 2.0
enum {
	// A.1 Audio Function Class Code
	AUDIO_FUNCTION							= AUDIO
};

enum {
	// A.2 Audio Function Subclass Codes
	FUNCTION_SUBCLASS_UNDEFINED				= 0x00
};

enum {
	// A.3 Audio Function Protocol Codes
	FUNCTION_PROTOCOL_UNDEFINED				= 0x00,
	AF_VERSION_02_00						= IP_VERSION_02_00
};

enum {
	// A.7 Audio Function Category Codes
	FUNCTION_CATEGORY_UNDEFINED				= 0x00,
	DESKTOP_SPEAKER							= 0x01,
	HOME_THEATHER							= 0x02,
	MICROPHONE								= 0x03,
	HEADSET									= 0x04,
	TELEPHONE								= 0x05,
	CONVERTER								= 0x06,
	VOICE_SOUND_RECORDER					= 0x07,
	IO_BOX									= 0x08,
	MUSICAL_INSTRUMENT						= 0x09,
	PRO_AUDIO								= 0x0A,
	AUDIO_VIDEO								= 0x0B,
	CONTROL_PANEL							= 0x0C,
	OTHER									= 0xFF
};

enum {
	// A.9 Audio Class-Specific AC Interface Descriptor Subtypes
	AC_DESCRIPTOR_UNDEFINED					= 0x00,
	HEADER									= 0x01,
	INPUT_TERMINAL							= 0x02,
	OUTPUT_TERMINAL							= 0x03,
	MIXER_UNIT								= 0x04,
	SELECTOR_UNIT							= 0x05,
	FEATURE_UNIT							= 0x06,
	EFFECT_UNIT								= 0x07,
	PROCESSING_UNIT							= 0x08,
	EXTENSION_UNIT							= 0x09,
	CLOCK_SOURCE							= 0x0A,
	CLOCK_SELECTOR							= 0x0B,
	CLOCK_MULTIPLIER						= 0x0C,
	SAMPLE_RATE_CONVERTER					= 0x0D
};

enum { 
	// A.10 Audio Class-Specific AS Interface Descriptor Subtypes
	AS_DESCRIPTOR_UNDEFINED					= 0x00,
	AS_GENERAL								= 0x01,
	FORMAT_TYPE								= 0x02,
	ENCODER									= 0x03,
	DECODER									= 0x04			
};

enum {
	// A.13 Audio Class-Specific Endpoint Descriptor Subtypes
	DESCRIPTOR_UNDEFINED					= 0x00,
	EP_GENERAL								= 0x01
};

enum {
	// A.14 Audio Class-Specific Request Codes
	REQUESET_CODE_UNDEFINED					= 0x00,
	CUR										= 0x01,
	RANGE									= 0x02,
	MEM										= 0x03
};

enum {
	// A.15 Encoder Type Codes
	ENCODER_UNDEFINED						= 0x00,
	OTHER_ENCODER							= 0x01,
	MPEG_ENCODER							= 0x02,
	AC3_ENCODER								= 0x03,
	WMA_ENCODER								= 0x04,
	DTS_ENCODER								= 0x05
};

enum {
	// A.16 Decoder Type Codes
	DECODER_UNDEFINED						= 0x00,
	OTHER_DECODER							= 0x01,
	MPEG_DECODER							= 0x02,
	AC3_DECODER								= 0x03,
	WMA_DECODER								= 0x04,
	DTS_DECODER								= 0x05
};

enum {
	// A.17.1 Clock Source Control Selectors
	CS_CONTROL_UNDEFINED					= 0x00,
	CS_SAM_FREQ_CONTROL						= 0x01,
	CS_CLOCK_VALID_CONTROL					= 0x02
};

enum {
	// A.17.2 Clock Selector Control Selectors
	CX_CONTROL_UNDEFINED					= 0x00,
	CX_CLOCK_SELECTOR_CONTROL				= 0x01
};

enum {
	// A.17.3 Clock Multiplier Control Selectors
	CM_CONTROL_UNDEFINED					= 0x00,
	CM_NUMERATOR_CONTROL					= 0x01,
	CM_DENOMINATOR_CONTROL					= 0x02
};

enum {
	// A.17.4 Terminal Control Selectors 
	TE_CONTROL_UNDEFINED					= 0x00, 
	TE_COPY_PROTECT_CONTROL					= 0x01, 
	TE_CONNECTOR_CONTROL					= 0x02, 
	TE_OVERLOAD_CONTROL						= 0x03, 
	TE_CLUSTER_CONTROL						= 0x04, 
	TE_UNDERFLOW_CONTROL					= 0x05, 
	TE_OVERFLOW_CONTROL						= 0x06, 
	TE_LATENCY_CONTROL						= 0x07 
};

enum {
	// A.17.5 Mixer Control Selectors 
	MU_CONTROL_UNDEFINED					= 0x00, 
	MU_MIXER_CONTROL						= 0x01,
	MU_CLUSTER_CONTROL						= 0x02, 
	MU_UNDERFLOW_CONTROL					= 0x03, 
	MU_OVERFLOW_CONTROL						= 0x04, 
	MU_LATENCY_CONTROL						= 0x05 
};

enum {
	// A.17.6 Selector Control Selectors 
	SU_CONTROL_UNDEFINED					= 0x00, 
	SU_SELECTOR_CONTROL						= 0x01, 
	SU_LATENCY_CONTROL						= 0x02 
};

enum {
	// A.17.7 Feature Unit Control Selectors 
	FU_CONTROL_UNDEFINED					= 0x00,
	FU_MUTE_CONTROL							= 0x01,
	FU_VOLUME_CONTROL						= 0x02,
	FU_BASS_CONTROL							= 0x03,
	FU_MID_CONTROL							= 0x04,
	FU_TREBLE_CONTROL						= 0x05,
	FU_GRAPHIC_EQUALIZER_CONTROL			= 0x06,
	FU_AUTOMATIC_GAIN_CONTROL				= 0x07,
	FU_DELAY_CONTROL						= 0x08,
	FU_BASS_BOOST_CONTROL					= 0x09,
	FU_LOUDNESS_CONTROL						= 0x0A,
	FU_INPUT_GAIN_CONTROL					= 0x0B,
	FU_INPUT_GAIN_PAD_CONTROL				= 0x0C,
	FU_PHASE_INVERTER_CONTROL				= 0x0D,
	FU_UNDERFLOW_CONTROL					= 0x0E,
	FU_OVERFLOW_CONTROL						= 0x0F,
	FU_LATENCY_CONTROL						= 0x10
};

enum {
	// A.17.11 AudioStreaming Interface Control Selectors 
	AS_CONTROL_UNDEFINED					= 0x00, 
	AS_ACT_ALT_SETTING_CONTROL				= 0x01, 
	AS_VAL_ALT_SETTINGS_CONTROL				= 0x02, 
	AS_AUDIO_DATA_FORMAT_CONTROL			= 0x03 
};

enum {
	// A.17.12 Encoder Control Selectors 
	EN_CONTROL_UNDEFINED					= 0x00, 
	EN_BIT_RATE_CONTROL						= 0x01, 
	EN_QUALITY_CONTROL						= 0x02, 
	EN_VBR_CONTROL							= 0x03, 
	EN_TYPE_CONTROL							= 0x04, 
	EN_UNDERFLOW_CONTROL					= 0x05, 
	EN_OVERFLOW_CONTROL						= 0x06, 
	EN_ENCODER_ERROR_CONTROL				= 0x07, 
	EN_PARAM1_CONTROL						= 0x08, 
	EN_PARAM2_CONTROL						= 0x09, 
	EN_PARAM3_CONTROL						= 0x0A, 
	EN_PARAM4_CONTROL						= 0x0B, 
	EN_PARAM5_CONTROL						= 0x0C, 
	EN_PARAM6_CONTROL						= 0x0D, 
	EN_PARAM7_CONTROL						= 0x0E, 
	EN_PARAM8_CONTROL						= 0x0F 
};

enum {
	// A.17.13.1 MPEG Decoder Control Selectors 
	MPD_CONTROL_UNDEFINED					= 0x00, 
	MPD_DUAL_CHANNEL_CONTROL				= 0x01, 
	MPD_SECOND_STEREO_CONTROL				= 0x02, 
	MPD_MULTILINGUAL_CONTROL				= 0x03, 
	MPD_DYN_RANGE_CONTROL					= 0x04, 
	MPD_SCALING_CONTROL						= 0x05, 
	MPD_HILO_SCALING_CONTROL				= 0x06, 
	MPD_UNDERFLOW_CONTROL					= 0x07, 
	MPD_OVERFLOW_CONTROL					= 0x08, 
	MPD_DECODER_ERROR_CONTROL				= 0x09 
};

enum {
	// A.17.13.2 AC-3 Decoder Control Selectors 
	AD_CONTROL_UNDEFINED					= 0x00, 
	AD_MODE_CONTROL							= 0x01, 
	AD_DYN_RANGE_CONTROL					= 0x02, 
	AD_SCALING_CONTROL						= 0x03, 
	AD_HILO_SCALING_CONTROL					= 0x04, 
	AD_UNDERFLOW_CONTROL					= 0x05, 
	AD_OVERFLOW_CONTROL						= 0x06, 
	AD_DECODER_ERROR_CONTROL				= 0x07 
};

enum {
	// A.17.13.3 WMA Decoder Control Selectors 
	WD_CONTROL_UNDEFINED					= 0x00, 
	WD_UNDERFLOW_CONTROL					= 0x01, 
	WD_OVERFLOW_CONTROL						= 0x02, 
	WD_DECODER_ERROR_CONTROL				= 0x03 
};

enum {
	// A.17.13.4 DTS Decoder Control Selectors 
	DD_CONTROL_UNDEFINED					= 0x00, 
	DD_UNDERFLOW_CONTROL					= 0x01, 
	DD_OVERFLOW_CONTROL						= 0x02, 
	DD_DECODER_ERROR_CONTROL				= 0x03 
};

enum {
	// A.17.14 Endpoint Control Selectors 
	EP_CONTROL_UNDEFINED					= 0x00, 
	EP_PITCH_CONTROL						= 0x01, 
	EP_DATA_OVERRUN_CONTROL					= 0x02, 
	EP_DATA_UNDERRUN_CONTROL				= 0x03 
};		

// USB Device Class Definition for Audio Data Formats Release 2.0
enum {
	// Table A-1: Format Type Codes
    FORMAT_TYPE_UNDEFINED					= 0x00,
    FORMAT_TYPE_I							= 0x01,
    FORMAT_TYPE_II							= 0x02,
    FORMAT_TYPE_III							= 0x03,
    FORMAT_TYPE_IV							= 0x04,
    EXT_FORMAT_TYPE_I						= 0x05,
    EXT_FORMAT_TYPE_II						= 0x06,
    EXT_FORMAT_TYPE_III						= 0x07
};

enum {
    // Table A-2: Audio Data Format Type I Bit Allocations
    PCM										= 0x00000001,
    PCM8									= 0x00000002,
    IEEE_FLOAT								= 0x00000004,
    ALAW									= 0x00000008,
    MULAW									= 0x00000010,
	TYPE_I_RAW_DATA							= 0x80000000
};

enum {
    // Table A-3: Audio Data Format Type II Bit Allocations
    MPEG									= 0x00000001,
    AC3										= 0x00000002,
    WMA										= 0x00000004,
    DTS										= 0x00000008,
	TYPE_II_RAW_DATA						= 0x80000000
};

enum {
    // Table A-4: Audio Data Format Type III Bit Allocations
    IEC61937_AC3							= 0x00000001,
    IEC1937_MPEG1_Layer1					= 0x00000002,
    IEC1937_MPEG1_Layer2or3					= 0x00000004,
    IEC1937_MPEG2_NOEXT						= 0x00000004,
    IEC61937_MPEG2_EXT						= 0x00000008,
    IEC61937_MPEG2_AAC_ADTS					= 0x00000010,
	IEC1937_MPEG2_Layer1_LS					= 0x00000020,
	IEC1937_MPEG2_Layer2or3_LS				= 0x00000040,
	IEC61937_DTS_I							= 0x00000080,
	IEC61937_DTS_II							= 0x00000100,
	IEC61937_DTS_III						= 0x00000200,
	IEC61937_ATRAC							= 0x00000400,
	IEC61937_ATRAC2or3						= 0x00000800,
	TYPE_III_WMA							= 0x00001000
};

// USB Device Class Definition for Terminal Types Release 2.0
// Table 2-1: USB Terminal Types 
enum {
    USB_STREAMING							= 0x0101
};
 
}; // namespace USBAUDIO_0200

#define USB_AUDIO_IS_FUNCTION(subtype)			((subtype >= MIXER_UNIT) && (subtype <= EXTENSION_UNIT))
#define USB_AUDIO_IS_TERMINAL(subtype)			((subtype == INPUT_TERMINAL) || (subtype == OUTPUT_TERMINAL))
#define	ERROR_IF_FALSE(condition)				((condition) ? kIOReturnSuccess : kIOReturnError)
#define RELEASE_IF_FALSE(ptr, condition)		do {if (false == (condition)) {(ptr)->release(); (ptr) = NULL;}} while (0)

#pragma mark Constants

#define kAUAUSBSpec1_0				0x0100
#define kAUAUSBSpec2_0				0x0200
#define kBytesPerSampleFrequency	3

#pragma mark DictionaryKeys

// AUAASEndpointDictionary
#define kHasSampleFreqControl		"hasSampleFreqControl"
#define kHasPitchControl			"hasPitchControl"
#define kHasMaxPacketsOnly			"HasMaxPacketsOnly"
#define kLockDelayUnits				"LockDelayUnits"
#define kLockDelay					"LockDelay"

// AUAConfigurationDictionary

#define kControlDictionaries		"ControlDictionaries"
#define	kStreamDictionaries			"StreamDictionaries"
#define kControlInterfaceNumber		"ControlInterfaceNumber"

// AUAControlDictionary
#define kInputTerminals				"InputTerminals"
#define kOutputTerminals			"OutputTerminals"
#define kExtensionUnits				"ExtensionUnits"
#define kFeatureUnits				"FeatureUnits"
#define kMixerUnits					"MixerUnits"
#define kNumStreamInterfaces		"NumStreamInterfaces"
#define kEffectUnits				"EffectUnits"
#define kProcessingUnits			"ProcessingUnits"
#define kSelectorUnits				"SelectorUnits"
#define kClockSources				"ClockSources"
#define kClockSelectors				"ClockSelectors"
#define kClockMultipliers			"ClockMultipliers"
#define kStreamInterfaceNumbers		"StreamInterfaceNumbers"
#define kSubType					"SubType"
#define kAlternateSetting			"AlternateSetting"
#define kInterfaceClass				"InterfaceClass"
#define kInterfaceNumber			"InterfaceNumber"
#define kInterfaceProtocol			"InterfaceProtocol"
#define kInterfaceSubClass			"InterfaceSubClass"
#define kNumEndpoints				"NumEndpoints"
#define kADCVersion					"ADCVersion"
#define kStringIndex				"StringIndex"					// <rdar://6394629>

// AUAEndpointDictionary
#define kAddress					"Address"
#define kAttributes					"Attributes"
#define kDirection					"Direction"
#define kInterval					"Interval"
#define kMaxPacketSize				"MaxPacketSize"
#define	kSynchAddress				"SynchAddress"
#define kSyncType					"SyncType"
#define kRefreshInt					"RefreshInt"

// AUAInputTerminalDictionary + AUAOutputTerminalDictionary
#define kAssocTerminal				"AssocTerminal"
#define kChannelConfig				"ChannelConfig"
#define kNumChannels				"NumChannels"
#define kTerminalType				"TerminalType"
#define	kChannelNames				"ChannelNames"					//	<rdar://6430836>

// AUAMixerUnitDictionary
#define kNumInPins					"NumInPins"

// AUAEffectUnitDictionary
#define kEffectType					"EffectType"

// AUAProcessingUnitDictionary
#define kProcessType				"ProcessType"

// AUAStreamDictionary
#define kEndpoints					"Endpoints"
#define	kTerminalLink				"TerminalLink"
#define kDelay						"Delay"
#define	kFormatTag					"FormatTag"
#define kNumChannels				"NumChannels"
#define	kSubframeSize				"SubframeSize"
#define	kBitResolution				"BitResolution"
#define kSampleRates				"SampleRates"
#define	kMaxBitRate					"MaxBitRate"
#define kSamplesPerFrame			"kSamplesPerFrame"
#define kMPEGCapabilities			"MPEGCapabilities"
#define	kMPEGFeatures				"MPEGFeatures"
#define kAC3BSID					"AC3BSID"
#define	kAC3Features				"AC3Features"
#define kASIsocEndpoint				"ASIsocEndpoint"
#define kNumSampleRates				"NumSampleRates"
#define kTerminalLink				"TerminalLink"
#define kFormats					"Formats"

// UnitDictionaries
#define kControlsArray				"ControlsArray"
#define kSourceID					"SourceID"
#define kSourceIDs					"SourceIDs"
#define kUnitID						"UnitID"
#define kControlSize				"ControlSize"
#define kNumControls				"NumControls"

//ClockDictionaries
#define kCSourceID					"CSourceID"
#define kCSourceIDs					"CSourceIDs"

// End dictionary keys

#pragma mark Structures

typedef struct USBDeviceDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt16									bcdUSB;
    UInt8									bDeviceClass;
    UInt8									bDeviceSubClass;
    UInt8									bDeviceProtocol;
    UInt8									bMaxPacketSize0;
    UInt16									idVendor;
    UInt16									idProduct;
    UInt16									bcdDevice;
    UInt8									iManufacturer;
    UInt8									iProduct;
    UInt8									iSerialNumber;
    UInt8									bNumConfigurations;
} USBDeviceDescriptor, *USBDeviceDescriptorPtr;

typedef struct USBConfigurationDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt16									wTotalLength;
    UInt8									bNumInterfaces;
    UInt8									bConfigurationValue;
    UInt8									iConfiguration;
    UInt8									bmAttributes;
    UInt8									MaxPower;	// expressed in 2mA units
} USBConfigurationDescriptor, *USBConfigurationDescriptorPtr;

// Standard USB Interface Association Descriptor
typedef struct USBInterfaceAssociationDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
	UInt8									bFirstInterface;
	UInt8									bInterfaceCount;
	UInt8									bFunctionClass;
	UInt8									bFunctionSubClass;
	UInt8									bFunctionProtocol;
	UInt8									iFunction;
} USBInterfaceAssociationDescriptor, *USBInterfaceAssociationDescriptorPtr;

typedef struct USBInterfaceDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bAlternateSetting;
    UInt8									bNumEndpoints;
    UInt8									bInterfaceClass;
    UInt8									bInterfaceSubClass;
    UInt8									bInterfaceProtocol;
    UInt8									iInterface;
} USBInterfaceDescriptor, *USBInterfaceDescriptorPtr;

typedef struct USBEndpointDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bEndpointAddress;
    UInt8									bmAttributes;
    UInt16									wMaxPacketSize;
    UInt8									bInterval;
    UInt8									bRefresh;
    UInt8									bSynchAddress;
} USBEndpointDescriptor, *USBEndpointDescriptorPtr;

typedef struct ACFunctionDescriptorHeader {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bFunctionID;
} ACFunctionDescriptorHeader;

typedef struct ACInterfaceHeaderDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bcdADC[2];					// Not PPC aligned
    UInt8									wTotalLength[2];			// Not PPC aligned
    UInt8									bInCollection;
    UInt8									baInterfaceNr[1];			// There are bInCollection of these
} ACInterfaceHeaderDescriptor, *ACInterfaceHeaderDescriptorPtr;

typedef struct ACInterfaceDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bInterfaceNumber;
    UInt8									bAlternateSetting;
    UInt8									bNumEndpoints;
    UInt8									bInterfaceClass;
    UInt8									bInterfaceSubClass;
    UInt8									bInterfaceProtocol;
    UInt8									iInterface;
} ACInterfaceDescriptor, *ACInterfaceDescriptorPtr;

typedef struct ACInputTerminalDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bTerminalID;
    UInt16									wTerminalType;
    UInt8									bAssocTerminal;
    UInt8									bNrChannels;
    UInt16									wChannelConfig; 
    UInt8									iChannelNames;
    UInt8									iTerminal;
} ACInputTerminalDescriptor, *ACInputTerminalDescriptorPtr;

typedef struct ACOutputTerminalDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bTerminalID;
    UInt16									wTerminalType;
    UInt8									bAssocTerminal;
    UInt8									bSourceID;
    UInt8									iTerminal;
} ACOutputTerminalDescriptor, *ACOutputTerminalDescriptorPtr;

typedef struct ACFeatureUnitDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bUnitID;
    UInt8									bSourceID;
    UInt8									bControlSize;
    UInt8									bmaControls[2];
    // bmaControls size is actually bControlSize, so it might be one or two bytes
} ACFeatureUnitDescriptor, *ACFeatureUnitDescriptorPtr;

typedef struct ACMixerUnitDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bUnitID;
	UInt8									bNrInPins;
	UInt8									baSourceID[1];	// there are bNrInPins of these
	// Can't have a static structure to define these locations:
	// UInt8								bNrChannels
	// UInt16								wChannelConfig
	// UInt8								iChannelNames
	// UInt8								bmControls[]	// you don't know the size of this, calculate it using bLength and bNrInPins
	// UInt8								iMixer
} ACMixerUnitDescriptor, *ACMixerUnitDescriptorPtr;

typedef struct ACSelectorUnitDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bUnitID;
	UInt8									bNrInPins;
	UInt8									baSourceID[1];	// there are bNrInPins of these
	// Can't have a static structure to define this location:
	// UInt8								iSelector
} ACSelectorUnitDescriptor, *ACSelectorUnitDescriptorPtr;

typedef struct ACProcessingUnitDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bUnitID;
	UInt16									wProcessType;
	UInt8									bNrInPins;
	UInt8									baSourceID[1];	// there are bNrInPins of these
	// Can't have a static structure to define these locations:
	// UInt8								bNrChannels
	// UInt16								wChannelConfig
	// UInt8								iChannelNames
	// UInt8								bControlSize
	// UInt8								bmControls[]
	// UInt8								iProcessing
	// UInt8								processSpecific[]	// no set size, calculate
} ACProcessingUnitDescriptor, *ACProcessingUnitDescriptorPtr;

typedef struct ACExtensionUnitDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bUnitID;
	UInt16									wExtensionCode;
	UInt8									bNrInPins;
	UInt8									baSourceID[1];	// there are bNrInPins of these
	// Can't have a static structure to define these locations:
	// UInt8								bNrChannels
	// UInt16								wChannelConfig
	// UInt8								iChannelNames
	// UInt8								bControlSize
	// UInt8								bmControls[]
	// UInt8								iExtension
} ACExtensionUnitDescriptor, *ACExtensionUnitDescriptorPtr;

/*	From USB Device Class Definition for Audio Data Formats 2.4.1:
	The Type III Format Type is identical to the Type I PCM Format Type, set up for two-channel 16-bit PCM data.
	It therefore uses two audio subframes per audio frame.  The subframe size is two bytes and the bit resolution
	is 16 bits.  The Type III Format Type descriptor is identical to the Type I Format Type descriptor but with
	the bNrChannels field set to two, the bSubframeSize field set to two and the bBitResolution field set to 16.
	All the techniques used to correctly transport Type I PCM formatted streams over USB equally apply to Type III
	formatted streams.

	The non-PCM encoded audio bitstreams that are transferred within the basic 16-bit data area of the IEC1937
	subframes (time-slots 12 [LSB] to 27 [MSB]) are placed unaltered in the two available 16-bit audio subframes
	per audio frame of the Type III formatted USB stream.  The additional information in the IEC1937 subframes
	(channel status, user bit, etc.) is discarded.  Refer to the IEC1937 standard for a detailed description of the
	exact contents of the subframes.
*/
typedef struct ASFormatTypeIDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bFormatType;
    UInt8									bNrChannels;
    UInt8									bSubframeSize;
    UInt8									bBitResolution;
    UInt8									bSamFreqType;
    UInt8									sampleFreq[3];	// sample rates are 24 bit values -- arg!
    //... fill in for sample freqs - probably a union for either a min/max or an array
} ASFormatTypeIDescriptor, *ASFormatTypeIDescriptorPtr;

typedef struct ASFormatTypeIIDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bFormatType;
	UInt16									wMaxBitRate;
	UInt16									wSamplesPerFrame;
    UInt8									bSamFreqType;
    UInt8									sampleFreq[3];	// sample rates are 24 bit values -- arg!
    //... fill in for sample freqs - probably a union for either a min/max or an array
} ASFormatTypeIIDescriptor, *ASFormatTypeIIDescriptorPtr;

typedef struct ASInterfaceDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bTerminalLink;
    UInt8									bDelay;
    UInt8									wFormatTag[2];	// because it's not PPC aligned when declared as UInt16
} ASInterfaceDescriptor, *ASInterfaceDescriptorPtr;

typedef struct ASFormatSpecificDescriptorHeader {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									wFormatTag[2];	// because it's not PPC aligned when declared as UInt16
} ASFormatSpecificDescriptorHeader, *ASFormatSpecificDescriptorHeaderPtr;

typedef struct ASMPEGFormatSpecificDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									wFormatTag[2];	// because it's not PPC aligned when declared as UInt16
	UInt8									bmMPEGCapabilities[2];	// because it's not PPC aligned when declared as UInt16
	UInt8									bmMPEGFeatures;
} ASMPEGFormatSpecificDescriptor, *ASMPEGFormatSpecificDescriptorPtr;

typedef struct ASAC3FormatSpecificDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									wFormatTag[2];	// because it's not PPC aligned when declared as UInt16
	UInt8									bmBSID[4];		// because it's not PPC aligned when declared as UInt32
	UInt8									bmAC3Features;
} ASAC3FormatSpecificDescriptor, *ASAC3FormatSpecificDescriptorPtr;

typedef struct ASEndpointDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bmAttributes;
    UInt8									bLockDelayUnits;
    UInt8									wLockDelay[2];	// because it's not PPC aligned when declared as UInt16
} ASEndpointDescriptor, *ASEndpointDescriptorPtr;

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
typedef struct AudioStatusWordFormat {
    UInt8									bStatusType;
    UInt8									bOriginator;
} AudioStatusWordFormat, *AudioStatusWordFormatPtr;

namespace USBAUDIO_0200 {
// USB Device Class Specification for Audio Devices Release 2.0

// Standard USB Audio Endpoint descriptor
typedef struct USBEndpointDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bEndpointAddress;
    UInt8									bmAttributes;
    UInt16									wMaxPacketSize;
    UInt8									bInterval;
} USBEndpointDescriptor, *USBEndpointDescriptorPtr;

// Table 4-5: Class-Specific AC Interface Header Descriptor
typedef struct ACInterfaceHeaderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bcdADC[2];				// Not PPC aligned
	UInt8									bCategory;
	UInt16									wTotalLength;
	UInt8									bmControls;
} ACInterfaceHeaderDescriptor, *ACInterfaceHeaderDescriptorPtr;

// Table 4-6: Clock Source Descriptor
typedef struct ACClockSourceDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bClockID;
	UInt8									bmAttributes;
	UInt8									bmControls;
	UInt8									bAssocTerminal;
	UInt8									iClockSource;
} ACClockSourceDescriptor, *ACClockSourceDescriptorPtr;

//	<rdar://5811247>
enum {
	CLOCK_TYPE_EXTERNAL						= 0x00,
	CLOCK_TYPE_INTERNAL_FIXED				= 0x01,
	CLOCK_TYPE_INTERNAL_VARIABLE			= 0x02,
	CLOCK_TYPE_INTERNAL_PROGRAMMABLE		= 0x03
};

// Table 4-7: Clock Selector Descriptor
typedef struct ACClockSelectorDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bClockID;
	UInt8									bNrInPins;
	UInt8									baCSourceID[1]; // 1..bNrInPins
	//UInt8									bmControls;
	//UInt8									iClockSelector;
} ACClockSelectorDescriptor, *ACClockSelectorDescriptorPtr;

// Table 4-8: Clock Multiplier Descriptor
typedef struct ACClockMultiplierDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bClockID;
	UInt8									bCSourceID;
	UInt8									bmControls;
	UInt8									iClockMultiplier;
} ACClockMultiplierDescriptor, *ACClockMultiplierDescriptorPtr;

// Table 4-9: Input Terminal Descriptor
typedef struct ACInputTerminalDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bTerminalID;
	UInt16									wTerminalType;
	UInt8									bAssocTerminal;
	UInt8									bCSourceID;
	UInt8									bNrChannels;
	UInt8									bmChannelConfig[4];		// Not PPC aligned
	UInt8									iChannelNames;
	UInt16									bmControls;
	UInt8									iTerminal;
} ACInputTerminalDescriptor, *ACInputTerminalDescriptorPtr;

// Table 4-10: Output Terminal Descriptor
typedef struct ACOutputTerminalDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bTerminalID;
	UInt16									wTerminalType;
	UInt8									bAssocTerminal;
	UInt8									bSourceID;
	UInt8									bCSourceID;
	UInt8									bmControls[2];			// Not PPC aligned
	UInt8									iTerminal;
} ACOutputTerminalDescriptor, *ACOutputTerminalDescriptorPtr;

// Table 4-11: Mixer Unit Descriptor
typedef struct ACMixerUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt8									bNrInPins;
	UInt8									baSourceID[1]; // 1..bNrInPins
	//UInt8									bNrChannels;
	//UInt32								bmChannelConfig;
	//UInt8									iChannelNames;
	//UInt8									bmMixerControls[];
	//UInt8									bmControls;
	//UInt8									iMixer;
} ACMixerUnitDescriptor, *ACMixerUnitDescriptorPtr;

// Table 4-12: Selector Unit Descriptor
typedef struct ACSelectorUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt8									bNrInPins;
	UInt8									baSourceID[1];
	//UInt8									bmControls;
	//UInt8									iSelector;
} ACSelectorUnitDescriptor, *ACSelectorUnitDescriptorPtr;

// Table 4-13: Feature Unit Descriptor
typedef struct ACFeatureUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt8									bSourceID;
	UInt8									bmaControls[1]; // 1..ch
	//UInt8									iFeature;
} ACFeatureUnitDescriptor, *ACFeatureUnitDescriptorPtr;

// Table 4-14: Sampling Rate Converter Unit Descriptor
typedef struct ACSrcUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt8									bSourceID;
	UInt8									bCSourceInID;
	UInt8									bCSourceOutID;
	UInt8									iSRC;
} ACSrcUnitDescriptor, *ACSrcUnitDescriptorPtr;

// Table 4-15: Common Part of the Effect Unit Descriptor
typedef struct ACEffectUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt16									wEffectType;
	UInt8									bSourceID;
	UInt8									bmaControls[1][4]; // 1..ch	Not PPC aligned		
	//UInt8									iEffects;
} ACEffectUnitDescriptor, *ACEffectUnitDescriptorPtr;

// Table 4-20: Common Part of the Processing Unit Descriptor
typedef struct ACProcessingUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt16									wProcessType;
	UInt8									bNrInPins;
	UInt8									baSourceID[1]; // 1..bNrInPins
	//UInt8									bNrChannels;
	//UInt32								bmChannelConfig;
	//UInt8									iChannelNames;
	//UInt16								bmControls;
	//UInt8									iProcessing;
} ACProcessingUnitDescriptor, *ACProcessingUnitDescriptorPtr;

// Table 4-24: Extension Unit Descriptor
typedef struct ACExtensionUnitDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bUnitID;
	UInt16									wExtensionCode;
	UInt8									bNrInPins;
	UInt8									baSourceID[1]; // 1..bNrInPins
	//UInt8									bNrChannels;
	//UInt32								bmChannelConfig;
	//UInt8									iChannelNames;
	//UInt8									bmControls;
	//UInt8									iExtension;
} ACExtensionUnitDescriptor, *ACExtensionUnitDescriptorPtr;

// Table 4-27: Class-Specific AS Interface Header Descriptor
typedef struct ASInterfaceDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bTerminalLink;
	UInt8									bmControls;
	UInt8									bFormatType;
	UInt8									bmFormats[4];			// Not PPC aligned
	UInt8									bNrChannels;
	UInt8									bmChannelConfig[4];		// Not PPC aligned
	UInt8									iChannelNames;
} ASInterfaceDescriptor, *ASInterfaceDescriptorPtr;

// Table 4-28: Encoder Descriptor
typedef struct ASEncoderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bEncoderID;
	UInt8									bEncoder;
	UInt8									bmControls[4];			// Not PPC aligned
	UInt8									iParam[8];
	UInt8									iEncoder;
} ASEncoderDescriptor, *ASEncoderDescriptorPtr;

// Decoder Descriptor
typedef struct ASDecoderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bDecoderID;
	UInt8									bDecoder;
} ASDecoderDescriptor, *ASDecoderDescriptorPtr;

// Table 4-29: MPEG Decoder Descriptor
typedef struct ASMPEGDecoderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bDecoderID;
	UInt8									bDecoder;
	UInt8									bmMPEGCapabilities[2];	// Not PPC aligned
	UInt8									bmMPEGFeatures;
	UInt8									bmControls;
	UInt8									iDecoder;
} ASMPEGDecoderDescriptor, *ASMPEGDecoderDescriptorPtr;

// Table 4-30: AC-3 Decoder Descriptor
typedef struct ASAC3DecoderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bDecoderID;
	UInt8									bDecoder;
	UInt8									bmBSID[4];				// Not PPC aligned
	UInt8									bmAC3Features;
	UInt8									bmControls;
	UInt8									iDecoder;
} ASAC3DecoderDescriptor, *ASAC3DecoderDescriptorPtr;

// Table 4-31: WMA Decoder Descriptor
typedef struct ASWMADecoderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bDecoderID;
	UInt8									bDecoder;
	UInt8									bmWMAProfile[2];		// Not PPC aligned
	UInt8									bmControls;
	UInt8									iDecoder;
} ASWMADecoderDescriptor, *ASWMADecoderDescriptorPtr;

// Table 4-32: DTS Decoder Descriptor
typedef struct ASDTSDecoderDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bDecoderID;
	UInt8									bDecoder;
	UInt8									bmCapabilities;
	UInt8									bmControls;
	UInt8									iDecoder;
} ASDTSDecoderDescriptor, *ASDTSDecoderDescriptorPtr;

// Table 4-34: Class-Specific AS Isochronous Audio Data Endpoint Descriptor
typedef struct ASEndpointDescriptor {
	UInt8									bLength;
	UInt8									bDescriptorType;
	UInt8									bDescriptorSubtype;
	UInt8									bmAttributes;
	UInt8									bmControls;
	UInt8									bLockDelayUnits;
	UInt16									wLockDelay;
} ASEndpointDescriptor, *ASEndpointDescriptorPtr;

// Table 6-1: Interrupt Data Message Format
typedef struct InterruptDataMessageFormat {
	UInt8									bInfo;
	UInt8									bAttribute;
	UInt16									wValue;
	UInt16									wIndex;
} InterruptDataMessageFormat, *InterruptDataMessageFormatPtr;

// USB Device Class Definition for Audio Data Formats Release 2.0

// Table 2-2: Type I Format Type Descriptor 
typedef struct ASFormatTypeIDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bFormatType;
    UInt8									bSubslotSize;
    UInt8									bBitResolution;
} ASFormatTypeIDescriptor, *ASFormatTypeIDescriptorPtr;

// Table 2-3: Type II Format Type Descriptor 
typedef struct ASFormatTypeIIDescriptor {
    UInt8									bLength;
    UInt8									bDescriptorType;
    UInt8									bDescriptorSubtype;
    UInt8									bFormatType;
    UInt16									wMaxBitRate;
    UInt16									wSlotsPerFrame;
} ASFormatTypeIIDescriptor, *ASFormatTypeIIDescriptorPtr;

};

// Sub Ranges
typedef struct SubRange8 {
	UInt8									bMIN;
	UInt8									bMAX;
	UInt8									bRES;
} SubRange8, *SubRange8Ptr;

typedef struct SubRange16 {
	UInt16									wMIN;
	UInt16									wMAX;
	UInt16									wRES;
} SubRange16, *SubRange16Ptr;

typedef struct SubRange32 {
	UInt32									dMIN;
	UInt32									dMAX;
	UInt32									dRES;
} SubRange32, *SubRange32Ptr;

//	<rdar://6430836>
typedef struct AudioClusterDescriptor {
	UInt8									bNrChannels;
	UInt32									bmChannelConfig;
	UInt8									iChannelNames;
} AudioClusterDescriptor, *AudioClusterDescriptorPtr;

#pragma mark Classes

class AppleUSBAudioDictionary : public OSDictionary 
{
	OSDeclareDefaultStructors (AppleUSBAudioDictionary);

protected:
	IOReturn					setDictionaryObjectAndRelease (const char * key, OSObject * object);
	IOReturn					setDictionaryValue (const char * key, bool value);
	IOReturn					setDictionaryValue (const char * key, UInt8 value);
	IOReturn					setDictionaryValue (const char * key, UInt16 value);
	IOReturn					setDictionaryValue (const char * key, UInt32 value);

public:
	OSArray *					getDictionaryArray (const char * key);
	IOReturn					getDictionaryValue (const char * key, bool * value);
    IOReturn					getDictionaryValue (const char * key, UInt8 * value);
    IOReturn					getDictionaryValue (const char * key, UInt16 * value);
    IOReturn					getDictionaryValue (const char * key, UInt32 * value);
	bool						initDictionaryForUse (void);
 	void						logDescriptor (UInt8 * descriptor, UInt8 length);
};


class AUAUnitDictionary : public AppleUSBAudioDictionary 
{
    OSDeclareDefaultStructors (AUAUnitDictionary);

//private:
//	UInt8						unitID;
//	UInt8						sourceID;
//	UInt8						descriptorSubType;

public:

	OSArray *					getControls (void) {return getDictionaryArray (kControlsArray);}
	IOReturn					getControlSize (UInt8 * controlSize) {return getDictionaryValue (kControlSize, controlSize);}
	IOReturn					getDescriptorSubType (UInt8 * descriptorSubType) {return getDictionaryValue (kSubType, descriptorSubType);}
	virtual	IOReturn			getNumInPins (UInt8 * numInPins) {* numInPins = 1; return kIOReturnSuccess;}
	IOReturn					getSourceID (UInt8 * sourceID) {return getDictionaryValue (kSourceID, sourceID);}
	IOReturn					getClockSourceID (UInt8 * clockSourceID) {return getDictionaryValue (kCSourceID, clockSourceID);}
	IOReturn					getUnitID (UInt8 * unitID) {return getDictionaryValue (kUnitID, unitID);}
	IOReturn					getStringIndex (UInt8 * stringIndex) {return getDictionaryValue (kStringIndex, stringIndex);}	// <rdar://6394629>
	IOReturn					getChannelNames (UInt8 * channelNames) {return getDictionaryValue (kChannelNames, channelNames);}		//	<rdar://6430836>
	IOReturn					getNumChannels (UInt8 * numChannels) {return getDictionaryValue (kNumChannels, numChannels);}			//	<rdar://6430836>
	IOReturn					getChannelConfig (UInt32 * channelConfig) {return getDictionaryValue (kChannelConfig, channelConfig);}	//	<rdar://6430836>

	IOReturn					setDescriptorSubType (UInt8 subType) {return setDictionaryValue (kSubType, subType);}
	IOReturn					setNumInPins (UInt8 numInPins) {return setDictionaryValue (kNumInPins, numInPins);}
	IOReturn					setSourceID (UInt8 sourceID) {return setDictionaryValue (kSourceID, sourceID);}
	IOReturn					setUnitID (UInt8 unitID) {return setDictionaryValue (kUnitID, unitID);}
	IOReturn					setStringIndex (UInt8 stringIndex) {return setDictionaryValue (kStringIndex, stringIndex);}		// <rdar://6394629>

};

class AUAInputTerminalDictionary : public AUAUnitDictionary 
{
//private:
//	UInt16						terminalType;
//	UInt16						channelConfig;
//	UInt8						assocTerminal;
//	UInt8						numChannels;

public:

	IOReturn					getClockSourceID (UInt8 * clockSourceID) {return getDictionaryValue (kCSourceID, clockSourceID);}
	IOReturn					setAssocTerminal (UInt8 assocTerminal) {return setDictionaryValue (kAssocTerminal, assocTerminal);}
	IOReturn					setChannelConfig (UInt32 channelConfig) {return setDictionaryValue (kChannelConfig, channelConfig);}
	IOReturn					setNumChannels (UInt8 numChannels) {return setDictionaryValue (kNumChannels, numChannels);}
	IOReturn					setTerminalType (UInt16 terminalType) {return setDictionaryValue (kTerminalType, terminalType);}
	IOReturn					setClockSourceID (UInt8 clockSourceID) {return setDictionaryValue (kCSourceID, clockSourceID);}
	IOReturn					setChannelNames (UInt8 channelNames) {return setDictionaryValue (kChannelNames, channelNames);}		//	<rdar://6430836>
};

class AUAOutputTerminalDictionary : public AUAUnitDictionary 
{
//private:
//	UInt16						terminalType;
//	UInt8						assocTerminal;

public:

	IOReturn					getClockSourceID (UInt8 * clockSourceID) {return getDictionaryValue (kCSourceID, clockSourceID);}
	IOReturn					setAssocTerminal (UInt8 assocTerminal) {return setDictionaryValue (kAssocTerminal, assocTerminal);}
	IOReturn					setTerminalType (UInt16 terminalType) {return setDictionaryValue (kTerminalType, terminalType);}
	IOReturn					setClockSourceID (UInt8 clockSourceID) {return setDictionaryValue (kCSourceID, clockSourceID);}
};

class AUAMixerUnitDictionary : public AUAUnitDictionary 
{
//private:
//	UInt8 *						baSourceIDs;		// there are numInPins of these
//	UInt8 *						bmControls;			// you don't know the size of this, calculate it using bLength and numInPins
//	UInt16						channelConfig;
//	UInt8						controlSize;		// This is the calculated size of bmControls
//	UInt8						numInPins;
//	UInt8						numChannels;

public:

	IOReturn					getNumInPins (UInt8 * numInPins) {return getDictionaryValue (kNumInPins, numInPins);}
	IOReturn					getNumChannels (UInt8 * numChannels) {return getDictionaryValue (kNumChannels, numChannels);}
	IOReturn					getSources (OSArray ** baSourceIDs) {return ERROR_IF_FALSE(baSourceIDs && (NULL != (*baSourceIDs = getDictionaryArray (kSourceIDs))));}

	void						initControlsArray (UInt8 * channelConfig, UInt8 bmControlSize);
	void						initSourceIDs (UInt8 * baSourceIDs, UInt8 numInPins);
	IOReturn					setChannelConfig (UInt32 channelConfig) {return setDictionaryValue (kChannelConfig, channelConfig);}
	IOReturn					setNumChannels (UInt8 numChannels) {return setDictionaryValue (kNumChannels, numChannels);}
	IOReturn					setChannelNames (UInt8 channelNames) {return setDictionaryValue (kChannelNames, channelNames);}	// <rdar://problem/6706026>
};

class AUASelectorUnitDictionary : public AUAUnitDictionary 
{
//private:
//	UInt8 *						baSourceIDs;		// there are numInPins of these
//	UInt8						numInPins;

public:

	IOReturn					getNumInPins (UInt8 * numInPins) {return getDictionaryValue (kNumInPins, numInPins);}
	IOReturn					getSources (OSArray ** baSourceIDs) {return ERROR_IF_FALSE(baSourceIDs && (NULL != (*baSourceIDs = getDictionaryArray (kSourceIDs))));}
	void						initSourceIDs (UInt8 * baSourceIDs, UInt8 numInPins);
};

class AUAEffectUnitDictionary : public AUAUnitDictionary 
{
//private:
//	UInt8 *						bmaControls;
//	UInt8						controlSize;
//	UInt8						numControls;

public:

	IOReturn					getBMAControls (OSArray ** bmaControls) {return ERROR_IF_FALSE(bmaControls && (NULL != (*bmaControls = getDictionaryArray (kControlsArray))));}
	IOReturn					getControlSize (UInt8 * controlSize) {return getDictionaryValue (kControlSize, controlSize);}
	IOReturn					getNumControls (UInt8 * numControls) {return getDictionaryValue (kNumControls, numControls);}

	void						initControlsArray (UInt8 * bmaControls, UInt8 numControls);
	IOReturn					setControlSize (UInt8 controlSize) {return setDictionaryValue (kControlSize, controlSize);}
	IOReturn					setEffectType (UInt16 wEffectType) {return setDictionaryValue (kEffectType, wEffectType);}
};

class AUAProcessingUnitDictionary : public AUAUnitDictionary 
{
//private:
//	UInt8 *						bmControls;			// you don't know the size of this, calculate it using bLength and numInPins
//	UInt8 *						baSourceIDs;		// there are numInPins of these
//	UInt16						processType;
//	UInt16						channelConfig;
//	UInt8						numInPins;
//	UInt8						controlSize;		// This is the calculated size of bmControls
//	UInt8						numChannels;

public:

	IOReturn					getNumInPins (UInt8 * numInPins) {return getDictionaryValue (kNumInPins, numInPins);}
	IOReturn					getSources (OSArray ** baSourceIDs) {return ERROR_IF_FALSE(baSourceIDs && (NULL != (*baSourceIDs = getDictionaryArray (kSourceIDs))));}

	void						initSourceIDs (UInt8 * baSourceIDs, UInt8 numInPins);
	void						initControlsArray (UInt8 * bmControls, UInt8 bmControlSize);
	IOReturn					setChannelConfig (UInt32 chanConfig) {return setDictionaryValue (kChannelConfig, chanConfig);}
	IOReturn					setProcessType (UInt16 wProcessType) {return setDictionaryValue (kProcessType, wProcessType);}
	IOReturn					setNumChannels (UInt8 numChannels) {return setDictionaryValue (kNumChannels, numChannels);}
	IOReturn					setChannelNames (UInt8 channelNames) {return setDictionaryValue (kChannelNames, channelNames);}	// <rdar://problem/6706026>
};

class AUAExtensionUnitDictionary : public AUAUnitDictionary 
{
//private:
//	UInt8 *						bmControls;			// you don't know the size of this, calculate it using bLength and numInPins
//	UInt8 *						baSourceIDs;		// there are numInPins of these
//	UInt16						extensionCode;
//	UInt16						channelConfig;
//	UInt8						numInPins;
//	UInt8						controlSize;		// This is the calculated size of bmControls
//	UInt8						numChannels;

public:
	
	IOReturn					getNumInPins (UInt8 * numInPins) {return getDictionaryValue (kNumInPins, numInPins);}
	IOReturn					getSources (OSArray ** baSourceIDs) {return ERROR_IF_FALSE(baSourceIDs && (NULL != (*baSourceIDs = getDictionaryArray (kSourceIDs))));}

	void						initControlsArray (UInt8 * channelConfig, UInt8 bmControlSize);
	void						initSourceIDs (UInt8 * baSourceIDs, UInt8 numInPins);
	IOReturn					setChannelConfig (UInt32 channelConfig) {return setDictionaryValue (kChannelConfig, channelConfig);}
	IOReturn					setNumChannels (UInt8 numChannels) {return setDictionaryValue (kNumChannels, numChannels);}
	IOReturn					setChannelNames (UInt8 channelNames) {return setDictionaryValue (kChannelNames, channelNames);}	// <rdar://problem/6706026>
};

class AUAFeatureUnitDictionary : public AUAUnitDictionary 
{
//private:
//	UInt8 *						bmaControls;
//	UInt8						controlSize;
//	UInt8						numControls;

public:

	bool						channelHasMuteControl (UInt8 channelNum);
	bool						channelHasVolumeControl (UInt8 channelNum);
	IOReturn					getBMAControls (OSArray ** bmaControls) {return ERROR_IF_FALSE(bmaControls && (NULL != (*bmaControls = getDictionaryArray (kControlsArray))));}
	IOReturn					getControlSize (UInt8 * controlSize) {return getDictionaryValue (kControlSize, controlSize);}
	IOReturn					getNumControls (UInt8 * numControls) {return getDictionaryValue (kNumControls, numControls);}
	bool						masterHasMuteControl (void);

	void						initControlsArray (UInt8 * bmaControls, UInt8 numControls);
	IOReturn					setControlSize (UInt8 controlSize) {return setDictionaryValue (kControlSize, controlSize);}
};

class AUAClockSourceDictionary : public AUAUnitDictionary 
{
public:
	IOReturn					getClockType (UInt8 * clockType);
	IOReturn					getAssocTerminal (UInt8 * assocTerminal) {return getDictionaryValue (kAssocTerminal, assocTerminal);}	//	<rdar://5811247>
	IOReturn					getControlBitmap (UInt8 * bmControls);
	IOReturn					getAttributes (UInt8 * attributes) {return getDictionaryValue (kAttributes, attributes);}

	void						initControlsArray (UInt8 * bmControls, UInt8 bmControlSize);
	IOReturn					setAttributes (UInt8 attributes) {return setDictionaryValue (kAttributes, attributes);}
	IOReturn					setAssocTerminal (UInt8 assocTerminal) {return setDictionaryValue (kAssocTerminal, assocTerminal);}
};

class AUAClockSelectorDictionary : public AUAUnitDictionary 
{
public:
	IOReturn					getNumInPins (UInt8 * numInPins) {return getDictionaryValue (kNumInPins, numInPins);}
	IOReturn					getClockSources (OSArray ** baCSourceIDs) {return ERROR_IF_FALSE(baCSourceIDs && (NULL != (*baCSourceIDs = getDictionaryArray (kCSourceIDs))));}

	void						initClockSourceIDs (UInt8 * baCSourceIDs, UInt8 numInPins);
	void						initControlsArray (UInt8 * bmControls, UInt8 bmControlSize);
};

class AUAClockMultiplierDictionary : public AUAUnitDictionary 
{
public:
	IOReturn					getClockSourceID (UInt8 * bCSourceID) {return getDictionaryValue (kCSourceID, bCSourceID);}

	void						initControlsArray (UInt8 * bmControls, UInt8 bmControlSize);
	IOReturn					setClockSourceID (UInt8 bCSourceID) {return setDictionaryValue (kCSourceID, bCSourceID);}
};

class AUAEndpointDictionary;

class AUAControlDictionary : public AppleUSBAudioDictionary 
{
	friend class AUAConfigurationDictionary;

    OSDeclareDefaultStructors (AUAControlDictionary);

private:
//    UInt16					adcVersion;			// in BCD
//    UInt8						interfaceNumber;
//    UInt8						alternateSetting;
//    UInt8						numEndpoints;
//    UInt8						interfaceClass;		// should always be 1
//    UInt8						interfaceSubClass;	// should always be 1
//    UInt8						interfaceProtocol;
//    UInt8						numStreamInterfaces;	
	OSArray *					getExtensionUnits (void) {return getDictionaryArray (kExtensionUnits);}
	OSArray *					getFeatureUnits (void) {return getDictionaryArray (kFeatureUnits);}
    OSArray *					getInputTerminals (void) {return getDictionaryArray (kInputTerminals);}
	OSArray *					getMixerUnits (void) {return getDictionaryArray (kMixerUnits);}
	OSArray *					getOutputTerminals (void) {return getDictionaryArray (kOutputTerminals);}
	OSArray *					getEffectUnits (void) {return getDictionaryArray (kEffectUnits);}
	OSArray *					getProcessingUnits (void) {return getDictionaryArray (kProcessingUnits);}
	OSArray *					getSelectorUnits (void) {return getDictionaryArray (kSelectorUnits);}
	OSArray *					getClockSources (void) {return getDictionaryArray (kClockSources);}
	OSArray *					getClockSelectors (void) {return getDictionaryArray (kClockSelectors);}
	OSArray *					getClockMultipliers (void) {return getDictionaryArray (kClockMultipliers);}
	OSArray *					getStreamInterfaceNumbers (void) {return getDictionaryArray (kStreamInterfaceNumbers);}
	OSArray *					getTerminalClockEntities (void);


	AUAFeatureUnitDictionary *			getFeatureUnitDictionary (UInt8 unitID);
	AUAFeatureUnitDictionary *			getIndexedFeatureUnitDictionary (UInt8 index);
	AUAInputTerminalDictionary *		getInputTerminalDictionary (UInt8 unitID);
	AUAMixerUnitDictionary *			getIndexedMixerUnitDictionary (UInt8 index);
	AUASelectorUnitDictionary *			getIndexedSelectorUnitDictionary (UInt8 index);
	AUAMixerUnitDictionary *			getMixerUnitDictionary (UInt8 unitID);
	AUAOutputTerminalDictionary *		getOutputTerminalDictionary (UInt8 unitID);
	AUASelectorUnitDictionary *			getSelectorUnitDictionary (UInt8 unitID);
	AUAEffectUnitDictionary *			getEffectUnitDictionary (UInt8 unitID);
	AUAProcessingUnitDictionary *		getProcessingUnitDictionary (UInt8 unitID);
	AUAExtensionUnitDictionary *		getExtensionUnitDictionary (UInt8 unitID);
	AUAClockSourceDictionary *			getClockSourceDictionary (UInt8 unitID);
	AUAClockSourceDictionary *			getIndexedClockSourceDictionary (UInt8 index);
	AUAClockSelectorDictionary *		getClockSelectorDictionary (UInt8 unitID);
	AUAClockSelectorDictionary *		getIndexedClockSelectorDictionary (UInt8 index);
	AUAClockMultiplierDictionary *		getClockMultiplierDictionary (UInt8 unitID);
	AUAClockMultiplierDictionary *		getIndexedClockMultiplierDictionary (UInt8 index);
	AUAUnitDictionary *					getUnitDictionary (UInt8 unitID);

    IOReturn					setAlternateSetting (UInt8 alternateSetting) {return setDictionaryValue (kAlternateSetting, alternateSetting);}
    IOReturn					setInterfaceClass (UInt8 interfaceClass) {return setDictionaryValue (kInterfaceClass, interfaceClass);}
    IOReturn					setInterfaceNumber (UInt8 interfaceNumber) {return setDictionaryValue (kInterfaceNumber, interfaceNumber);}
    IOReturn					setInterfaceProtocol (UInt8 interfaceProtocol) {return setDictionaryValue (kInterfaceProtocol, interfaceProtocol);}
    IOReturn					setInterfaceSubClass (UInt8 interfaceSubClass) {return setDictionaryValue (kInterfaceSubClass, interfaceSubClass);}
    IOReturn					setNumEndpoints (UInt8 numEndpoints) {return setDictionaryValue (kNumEndpoints, numEndpoints);}

public:
    static AUAControlDictionary *		create (void);
    
	// [rdar://5346021] Keep strack of the descriptor length to guard against malformed descriptors.
    USBInterfaceDescriptorPtr	parseACInterfaceDescriptor (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength);
    USBInterfaceDescriptorPtr	parseACInterfaceDescriptor_0200 (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength);
	void						parseInterfaceAssociationDescriptor (USBInterfaceAssociationDescriptorPtr theInterfaceAssociationPtr);

	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	USBInterfaceDescriptorPtr	parseACInterruptEndpointDescriptor ( USBInterfaceDescriptorPtr theInterfacePtr );
	IOReturn					getInterruptEndpointAddress ( UInt8 * address );
	IOReturn					getInterruptEndpointInterval ( UInt8 * interval );
	bool						hasInterruptEndpoint ( void );
	AUAEndpointDictionary *		getIndexedEndpointDictionary (UInt8 index);
	OSArray *					getEndpoints ();
		
	bool						channelHasMuteControl (UInt8 featureUnitID, UInt8 channelNum);
	bool						channelHasVolumeControl (UInt8 featureUnitID, UInt8 channelNum);
	bool						clockSourceHasFrequencyControl ( UInt8 clockSourceID, bool isProgrammable );			// [rdar://4867779]
	bool						clockSourceHasValidityControl ( UInt8 clockSourceID );									// [rdar://7446555]
	IOReturn					getADCVersion (UInt16 * adcVersion) {return getDictionaryValue (kADCVersion, adcVersion);}
    IOReturn					getAlternateSetting (UInt8 * altSettingID) {return getDictionaryValue (kAlternateSetting, altSettingID);}
	IOReturn					getFeatureSourceID (UInt8 * sourceID, UInt8 featureUnitID);
	IOReturn					getFeatureUnitIDConnectedToOutputTerminal (UInt8 * featureUnitID, UInt8 outputTerminalID);
	IOReturn					getIndexedFeatureUnitID (UInt8 * featureUnitID, UInt8 featureUnitIndex);
	IOReturn					getIndexedMixerUnitID (UInt8 * mixerUnitID, UInt8 mixerUnitIndex);
	IOReturn					getIndexedSelectorUnitID (UInt8 * selectorUnitID, UInt8 selectorUnitIndex);
	IOReturn					getIndexedInputTerminalType (UInt16 * terminalType, UInt8 index);
	IOReturn					getIndexedInputTerminalID (UInt8 * inputTerminalID, UInt8 index);
	IOReturn					getIndexedOutputTerminalID (UInt8 * outputTerminalID, UInt8 index);
	IOReturn					getIndexedOutputTerminalType (UInt16 * terminalType, UInt8 index);
	IOReturn					getIndexedClockSourceID (UInt8 * clockSourceID, UInt8 clockSourceIndex);
	IOReturn					getIndexedClockSelectorID (UInt8 * clockSelectorID, UInt8 clockSelectorIndex);
	IOReturn					getIndexedClockMultiplierID (UInt8 * clockMultiplierID, UInt8 clockMultiplierIndex);
	IOReturn					getInputTerminalType (UInt16 * terminalType, UInt8 terminalID);
    IOReturn					getInterfaceNumber (UInt8 * interfaceNumber) {return getDictionaryValue (kInterfaceNumber, interfaceNumber);}
    IOReturn					getInterfaceClass (UInt8 * interfaceClass) {return getDictionaryValue (kInterfaceClass, interfaceClass);}
	IOReturn					getInterfaceProtocol (UInt8 * interfaceProtocol) {return getDictionaryValue (kInterfaceProtocol, interfaceProtocol);}
    IOReturn					getInterfaceSubClass (UInt8 * interfaceSubClass) {return getDictionaryValue (kInterfaceSubClass, interfaceSubClass);}
	IOReturn					getNumControls (UInt8 * numControls, UInt8 featureUnitID);
	IOReturn					getNumEndpoints (UInt8 * numEndpoints) {return getDictionaryValue (kNumEndpoints, numEndpoints);}
	IOReturn					getNumInputTerminals (UInt8 * numInputTerminals);
	IOReturn					getNumOutputTerminals (UInt8 * numOutputTerminals);
	IOReturn					getNumSelectorUnits (UInt8 * numSelectorUnits);
	IOReturn					getNumClockSources (UInt8 * numClockSources);
	IOReturn					getNumClockSelectors (UInt8 * numClockSelectors);
	IOReturn					getNumClockMultipliers (UInt8 * numClockMultipliers);
	IOReturn					getNumSelectorSources (UInt8 * numSelectorSources, UInt8 unitID);
	IOReturn					getNumSources (UInt8 * numSources, UInt8 unitID);
	IOReturn					getNumStreamInterfaces (UInt8 * numStreamInterfaces) {return getDictionaryValue (kNumStreamInterfaces, numStreamInterfaces);}
	IOReturn					getStreamInterfaceNumbers (OSArray ** streamInterfaceNumbers) {return ERROR_IF_FALSE(streamInterfaceNumbers && (NULL != (*streamInterfaceNumbers = getDictionaryArray (kStreamInterfaceNumbers))));}
	IOReturn					getOutputTerminalType (UInt16 * terminalType, UInt8 terminalID);
	IOReturn					getSelectorSources (OSArray ** selectorSources, UInt8 unitID);
	IOReturn					getMixerSources (OSArray ** mixerSources, UInt8 unitID);
	IOReturn					getExtensionUnitSources (OSArray ** extensionUnitSources, UInt8 unitID);
	IOReturn					getProcessingUnitSources (OSArray ** processingUnitSources, UInt8 unitID);
	IOReturn					getClockSelectorSources (OSArray ** clockSelectorSources, UInt8 unitID);
	IOReturn					getClockSourceClockType (UInt8 * clockType, UInt8 unitID);								//	<rdar://5811247>
	IOReturn					getClockSourceAssocTerminal (UInt8 * assocTerminal, UInt8 unitID);						//	<rdar://5811247>
	IOReturn					getSourceID (UInt8 * sourceID, UInt8 unitID);
	IOReturn					getSourceIDs (OSArray ** sourceIDs, UInt8 unitID);
	IOReturn					getClockSourceID (UInt8 * sourceID, UInt8 unitID);
	IOReturn					getSubType (UInt8 * subType, UInt8 unitID);
	IOReturn					getStringIndex (UInt8 * stringIndex, UInt8 unitID);		// <rdar://6394629>
	IOReturn					getAudioClusterDescriptor (AudioClusterDescriptor * clusterDescriptor, UInt8 unitID);	// <rdar://6430836>
	bool						masterHasMuteControl (UInt8 featureUnitID);
};

class AUAEndpointDictionary : public AppleUSBAudioDictionary 
{
    OSDeclareDefaultStructors (AUAEndpointDictionary);

//private:
//  UInt8						address;
//  UInt8						attributes;
//  UInt16						maxPacketSize;
//  UInt8						synchAddress;
//	UInt8						refreshInt;		// interpreted as 2^(10-refreshInt)ms between refreshes

public:
    static AUAEndpointDictionary * 	create (void);

	IOReturn					getAddress (UInt8 * address) {return getDictionaryValue (kAddress, address);}
	IOReturn					getAttributes (UInt8 * attributes) {return getDictionaryValue (kAttributes, attributes);}
	IOReturn					getDirection (UInt8 * direction);
	IOReturn					getInterval (UInt8 * interval) {return getDictionaryValue (kInterval, interval);}
	IOReturn					getMaxPacketSize (UInt16 * maxPacketSize) {return getDictionaryValue (kMaxPacketSize, maxPacketSize);}
	IOReturn					getSynchAddress (UInt8 * synchAddress) {return getDictionaryValue (kSynchAddress, synchAddress);}
	IOReturn					getSyncType (UInt8 * syncType);
	IOReturn					getRefreshInt (UInt8 * refreshInt) {return getDictionaryValue (kRefreshInt, refreshInt);}
	
	bool						isIsocStreaming (void);
	bool						isIsocFeedback (void);

    IOReturn					setAddress (UInt8 address) {return setDictionaryValue (kAddress, address);}
    IOReturn					setAttributes (UInt8 attributes) {return setDictionaryValue (kAttributes, attributes);}
	IOReturn					setInterval (UInt8 interval) {return setDictionaryValue (kInterval, interval);}
    IOReturn					setMaxPacketSize (UInt16 maxPacketSize) {return setDictionaryValue (kMaxPacketSize, maxPacketSize);}
    IOReturn					setSynchAddress (UInt8 synchAddress) {return setDictionaryValue (kSynchAddress, synchAddress);}
	IOReturn					setRefreshInt (UInt8 refreshInt) {return setDictionaryValue (kRefreshInt, refreshInt);}
};

class AUAASEndpointDictionary : public AUAEndpointDictionary 
{
//private:
//  bool						sampleFreqControl;
//  bool						pitchControl;
//  bool						maxPacketsOnly;
//  UInt8						lockDelayUnits;
//	UInt16						lockDelay;
//  UInt8						attributes;

	// Shouldn't need to get the attributes directly, instead use DoesMaxPacketsOnly (), hasPitchControl (), and hasSampleFreqControl ()
	IOReturn					getAttributes (UInt8 * attributes) {return getDictionaryValue (kAttributes, attributes);}

public:
    AUAASEndpointDictionary (bool theSampleFreqControl, bool thePitchControl, bool theMaxPacketsOnly, UInt8 theLockDelayUnits, UInt16 theLockDelay);

	IOReturn					hasMaxPacketsOnly (bool * maxPacketsOnly) {return getDictionaryValue (kHasMaxPacketsOnly, maxPacketsOnly);}
	IOReturn					getLockDelay (UInt8 * lockDelay) {return getDictionaryValue (kLockDelay, lockDelay);}
	IOReturn					getLockDelayUnits (UInt8 * lockDelayUnits) {return getDictionaryValue (kLockDelayUnits, lockDelayUnits);}
	IOReturn					hasPitchControl (bool * pitchControl) {return getDictionaryValue (kHasPitchControl, pitchControl);}
	IOReturn					hasSampleFreqControl (bool * sampleFreqControl) {return getDictionaryValue (kHasSampleFreqControl, sampleFreqControl);}
};

class AUAStreamDictionary : public AppleUSBAudioDictionary 
{
	friend class AUAConfigurationDictionary;

    OSDeclareDefaultStructors (AUAStreamDictionary);

//private:
//    OSArray *						theEndpointObjects;
//    AUAASEndpointDictionary *		theIsocEndpointObject;
//
//	UInt32 *					sampleFreqs;
//	UInt32						bmAC3BSID;
//	UInt16						bmMPEGCapabilities; 
//  UInt16						formatTag;
//	UInt16						maxBitRate;
//	UInt16						samplesPerFrame;
//	UInt8						bmMPEGFeatures;
//	UInt8						bmAC3Features;
//  UInt8						interfaceNumber;
//  UInt8						alternateSetting;
//  UInt8						numEndpoints;
//  UInt8						interfaceClass;		// should always be 1
//  UInt8						interfaceSubClass;	// should always be 2
//  UInt8						interfaceProtocol;
//  UInt8						terminalLink;

	OSArray *						getEndpoints (void);
	AUAEndpointDictionary *			getEndpointByAddress (UInt8 address);
    AUAEndpointDictionary *			getIndexedEndpointDictionary (UInt8 index);
    AUAASEndpointDictionary *		getIndexedASIsocEndpointDictionary (UInt8 index);
    AUAEndpointDictionary *			getEndpointDictionaryByAddress (UInt8 address);
    AUAASEndpointDictionary *		getASIsocEndpointDictionaryByAddress (UInt8 address);

	IOReturn					addSampleRate (UInt32 sampleRate);
		
    IOReturn					setAlternateSetting (UInt8 alternateSetting) {return setDictionaryValue (kAlternateSetting, alternateSetting);}
    IOReturn					setInterfaceClass (UInt8 interfaceClass) {return setDictionaryValue (kInterfaceClass, interfaceClass);}
    IOReturn					setInterfaceNumber (UInt8 interfaceNumber) {return setDictionaryValue (kInterfaceNumber, interfaceNumber);}
    IOReturn					setInterfaceProtocol (UInt8 interfaceProtocol) {return setDictionaryValue (kInterfaceProtocol, interfaceProtocol);}
    IOReturn					setInterfaceSubClass (UInt8 interfaceSubClass) {return setDictionaryValue (kInterfaceSubClass, interfaceSubClass);}
    IOReturn					setNumEndpoints (UInt8 numEndpoints) {return setDictionaryValue (kNumEndpoints, numEndpoints);}

public:
    static AUAStreamDictionary *		create (void);
	
	// [rdar://5346021]	Keep strack of the descriptor length to guard against malformed descriptors.
    USBInterfaceDescriptorPtr		parseASInterfaceDescriptor (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength);
    USBInterfaceDescriptorPtr		parseASInterfaceDescriptor_0200 (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength);

	IOReturn					addSampleRatesToStreamDictionary ( OSArray * sampleRates );		// [rdar://4867779]
	IOReturn					getAC3BSID (UInt32 * bmAC3BSID) {return getDictionaryValue (kAC3BSID, bmAC3BSID);}
    IOReturn					getAlternateSetting (UInt8 * alternateSetting) {return getDictionaryValue (kAlternateSetting, alternateSetting);}
	AUAASEndpointDictionary *	getASEndpointDictionary (void);
	IOReturn					getDelay (UInt8 * delay) {return getDictionaryValue (kDelay, delay);}
	IOReturn					getFormatTag (UInt16 * formatTag) {return getDictionaryValue (kFormatTag, formatTag);}
    IOReturn					getInterfaceNumber (UInt8 * interfaceNumber) {return getDictionaryValue (kInterfaceNumber, interfaceNumber);}
    IOReturn					getInterfaceClass (UInt8 * interfaceClass) {return getDictionaryValue (kInterfaceClass, interfaceClass);}
	IOReturn					getInterfaceProtocol (UInt8 * interfaceProtocol) {return getDictionaryValue (kInterfaceProtocol, interfaceProtocol);}
    IOReturn					getInterfaceSubClass (UInt8 * interfaceSubClass) {return getDictionaryValue (kInterfaceSubClass, interfaceSubClass);}
	IOReturn					getIsocAssociatedEndpointAddress (UInt8 * assocEndpointAddress, UInt8 address);
	IOReturn					getIsocAssociatedEndpointMaxPacketSize (UInt16 * assocEndpointMaxPacketSize, UInt8 address);
	IOReturn					getIsocAssociatedEndpointRefreshInt (UInt8 * assocEndpointRefreshInt, UInt8 address);
	IOReturn 					getIsocEndpointAddress (UInt8 * address, UInt8 direction);
	IOReturn					getIsocEndpointDirection (UInt8 * direction, UInt8 index);
	IOReturn					getIsocEndpointInterval (UInt8 * interval, UInt8 direction);
	IOReturn					getIsocEndpointMaxPacketSize (UInt16 * maxPacketSize, UInt8 direction);
	IOReturn					getIsocEndpointSyncType (UInt8 * syncType, UInt8 address);
	IOReturn					getMaxBitRate (UInt16 * maxBitRate) {return getDictionaryValue (kMaxBitRate, maxBitRate);}
    IOReturn					getNumChannels (UInt8 * numChannels) {return getDictionaryValue (kNumChannels, numChannels);}
	IOReturn					getNumEndpoints (UInt8 * numEndpoints) {return getDictionaryValue (kNumEndpoints, numEndpoints);}
    IOReturn					getNumSampleRates (UInt8 * numSampleFreqs) {return getDictionaryValue (kNumSampleRates, numSampleFreqs);}
	IOReturn					getSamplesPerFrame (UInt16 * samplesPerFrame) {return getDictionaryValue (kSamplesPerFrame, samplesPerFrame);}
    IOReturn					getBitResolution (UInt8 * bitResolution) {return getDictionaryValue (kBitResolution, bitResolution);}
    IOReturn					getSubframeSize (UInt8 * subframeSize) {return getDictionaryValue (kSubframeSize, subframeSize);}
    OSArray *					getSampleRates (void) {return getDictionaryArray (kSampleRates);}
    IOReturn					getTerminalLink (UInt8 * terminalLink) {return getDictionaryValue (kTerminalLink, terminalLink);}

	bool						asEndpointHasMaxPacketsOnly (void);
	IOReturn					asEndpointGetLockDelay (UInt8 * lockDelay);
	IOReturn					asEndpointGetLockDelayUnits (UInt8 * lockDelayUnits);
	bool						asEndpointHasPitchControl (void);
	bool						asEndpointHasSampleFreqControl (void);
};

class AUAConfigurationDictionary : public AppleUSBAudioDictionary 
{
    OSDeclareDefaultStructors (AUAConfigurationDictionary);

//private:
//  IOUSBConfigurationDescriptor *	theConfigurationDescriptorPtr;
//  OSArray *						theControls;
//	UInt8							theControlInterfaceNum;
//  OSArray *						theStreams;

public:
    static AUAConfigurationDictionary *	create (const IOUSBConfigurationDescriptor * newConfigurationDescriptor, UInt8 controlInterfaceNum);
    virtual bool						init (const IOUSBConfigurationDescriptor * newConfigurationDescriptor, UInt8 controlInterfaceNum);
	
	IOReturn					addSampleRatesToStreamDictionary ( OSArray * sampleRates, UInt8 streamInterface, UInt8 altSetting );	// [rdar://4867779]
	bool						alternateSettingZeroCanStream (UInt8 interfaceNum);
	bool						asEndpointHasMaxPacketsOnly (UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					asEndpointGetLockDelay (UInt8 * lockDelay, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					asEndpointGetLockDelayUnits (UInt8 * lockDelayUnits, UInt8 interfaceNum, UInt8 altSettingID);
	bool						asEndpointHasPitchControl (UInt8 interfaceNum, UInt8 altSettingID);
	bool						asEndpointHasSampleFreqControl (UInt8 interfaceNum, UInt8 altSettingID);
	bool						channelHasMuteControl (UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID, UInt8 channelNum);
	bool						channelHasVolumeControl (UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID, UInt8 channelNum);
	IOReturn					getADCVersion (UInt16 * adcVersion);
	IOReturn					getNextAltSettingWithNumChannels (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 startingAltSettingID, UInt8 numChannelsRequested);
	IOReturn					getNextAltSettingWithSampleSize (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 startingAltSettingID, UInt8 sampleSizeRequested);
	IOReturn					getNextAltSettingWithSampleRate (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 startingAltSettingID, UInt32 sampleRateRequested);
	IOReturn					getAltSettingWithSettings (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 numChannels, UInt8 sampleSize, UInt32 sampleRate = 0);
	IOReturn					getAC3BSID (UInt32 * ac3BSID, UInt8 interfaceNum, UInt8 altSettingNum);
	IOReturn					getFeatureUnitIDConnectedToOutputTerminal (UInt8 * featureUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 outputTerminalID);
    IOReturn					getFirstStreamInterfaceNum (UInt8 * interfaceNum);
	IOReturn					getControlledStreamNumbers (OSArray **controlledStreams, UInt8 *numControlledStreams);
    IOReturn					getControlInterfaceNum (UInt8 * interfaceNum);
	IOReturn					getIsocEndpointInterval (UInt8 * interval, UInt8 interfaceNum, UInt8 altSettingID, UInt8 direction);
	IOReturn					getFormat (UInt16 * format, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getHighestSampleRate (UInt32 * sampleRate, UInt8 interfaceNum, UInt8 altSettingId);
	IOReturn					getIsocAssociatedEndpointAddress (UInt8 * assocEndpointAddress, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address);
	IOReturn					getIsocAssociatedEndpointMaxPacketSize (UInt16 * maxPacketSize, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address);
	IOReturn					getIsocAssociatedEndpointRefreshInt (UInt8 * refreshInt, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address);
	IOReturn					getIsocEndpointAddress (UInt8 * address, UInt8 interfaceNum, UInt8 altSettingID, UInt8 direction);
	IOReturn					getIsocEndpointDirection (UInt8 * direction, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getIsocEndpointMaxPacketSize (UInt16 * maxPacketSize, UInt8 interfaceNum, UInt8 altSettingID, UInt8 direction);
	IOReturn					getIsocEndpointSyncType (UInt8 * syncType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address);
	IOReturn					getIndexedFeatureUnitID (UInt8 * featureUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitIndex);
	IOReturn					getIndexedMixerUnitID (UInt8 * mixerUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 mixerUnitIndex);
	IOReturn					getIndexedSelectorUnitID (UInt8 * selectorUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 selectorUnitIndex);
	IOReturn					getIndexedInputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index);
	IOReturn					getIndexedInputTerminalID (UInt8 * terminalID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index);
	IOReturn					getIndexedOutputTerminalID (UInt8 * terminalID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index);
	IOReturn					getIndexedOutputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index);
	IOReturn					getIndexedClockSourceID (UInt8 * clockSourceID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 clockSourceIndex);
	IOReturn					getIndexedClockSelectorID (UInt8 * clockSelectorID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 clockSelectorIndex);
	IOReturn					getIndexedClockMultiplierID (UInt8 * clockMultiplierID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 clockMultiplierIndex);
	IOReturn					getInputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 terminalID);
    IOReturn					getInterfaceClass (UInt8 * interfaceClass, UInt8 interfaceNum, UInt8 altSettingID);
    IOReturn					getInterfaceSubClass (UInt8 * interfaceSubClass, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getLowestSampleRate (UInt32 * sampleRate, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getMaxBitRate (UInt16 * maxBitRate, UInt8 interfaceNum, UInt8 altSettingID);
    IOReturn					getNumAltSettings (UInt8 * numAltSettings, UInt8 interfaceNum);
    IOReturn					getNumChannels (UInt8 * numChannels, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumControls (UInt8 * numControls, UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID);
    IOReturn					getNumSampleRates (UInt8 * numSampleRates, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumInputTerminals (UInt8 * numInputTerminals, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumOutputTerminals (UInt8 * numOutputTerminals, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumSelectorUnits (UInt8 * numSelectorUnits, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumClockSources (UInt8 * numClockSources, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumClockSelectors (UInt8 * numClockSelectors, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumClockMultipliers (UInt8 * numClockMultipliers, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getNumSources (UInt8 * numSources, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);
    IOReturn					getNumStreamInterfaces (UInt8 * numStreamInterfaces);
	IOReturn					getOutputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 terminalID);
	IOReturn					getSamplesPerFrame (UInt16 * samplesPerFrame, UInt8 interfaceNum, UInt8 altSettingID);
    OSArray *					getSampleRates (UInt8 interfaceNum, UInt8 altSettingID);
    IOReturn					getBitResolution (UInt8 * sampleSize, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getSelectorSources (OSArray ** selectorSources, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);
	IOReturn					getClockSelectorSources (OSArray ** clockSelectorSources, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);
	IOReturn					getClockSourceClockType (UInt8 * clockType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);					//	<rdar://5811247>
	IOReturn					getClockSourceAssocTerminal (UInt8 * assocTerminal, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);			//	<rdar://5811247>
    IOReturn					getSubframeSize (UInt8 * subframeSize, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getSubType (UInt8 * subType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);
	IOReturn					getSourceID (UInt8 * sourceID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);		// Used for units that have only one input source
	IOReturn					getSourceIDs (OSArray ** sourceIDs, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);		// Used for units that have multiple input sources
	OSArray *					getTerminalClockEntities (UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getClockSourceID (UInt8 * clockSourceID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);		// Used for units that have only one input source
	IOReturn					getStringIndex (UInt8 * stringIndex, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);		// <rdar://6394629>
	IOReturn					getAudioClusterDescriptor (AudioClusterDescriptor * clusterDescriptor, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID);	// <rdar://6430836>
    IOReturn					getTerminalLink (UInt8 * terminalLink, UInt8 interfaceNum, UInt8 altSettingID);

	// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
	IOReturn					getInterruptEndpointAddress (UInt8 * address, UInt8 interfaceNum, UInt8 altSettingID);
	IOReturn					getInterruptEndpointInterval (UInt8 * interval, UInt8 interfaceNum, UInt8 altSettingID);
	bool						hasInterruptEndpoint (UInt8 interfaceNum, UInt8 altSettingID);

	bool						masterHasMuteControl (UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID);
	bool						verifySampleRateIsSupported (UInt8 interfaceNum, UInt8 altSettingID, UInt32 verifyRate);
	bool						clockSourceHasFrequencyControl (UInt8 interfaceNum, UInt8 altSetting, UInt8 clockSourceID, bool isProgrammable);		// [rdar://4867779]
	bool						clockSourceHasValidityControl (UInt8 interfaceNum, UInt8 altSetting, UInt8 clockSourceID);								// [rdar://7446555]

	bool						hasAudioStreamingInterfaces (void);
	
private:
	OSArray *						getControlDictionaries (void) {return getDictionaryArray (kControlDictionaries);}
	AUAControlDictionary *			getControlDictionary (UInt8 interfaceNum, UInt8 altSettingID);
	OSArray *						getStreamDictionaries (void) {return getDictionaryArray (kStreamDictionaries);}
    AUAStreamDictionary *			getStreamDictionary (UInt8 interfaceNum, UInt8 altSettingID);
    IOReturn						parseConfigurationDescriptor (IOUSBConfigurationDescriptor * configurationDescriptor);
    USBInterfaceDescriptorPtr		parseInterfaceDescriptor (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 * interfaceClass, UInt8 * interfaceSubClass, UInt8 * interfaceProtocol);
	void							dumpConfigMemoryToIOLog (IOUSBConfigurationDescriptor * configurationDescriptor);

};

#endif
