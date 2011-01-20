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
 
/*
    The classes contained herein abstract a USB audio class configuartion descriptor.

    A USB device is described by a section of length-formatted memory on the device.
    It is arranged as a list starting with the device descriptor and then followed by 
	one or more configuration descriptors, one or more stream descriptors, and one or 
	more HID descriptors.  It is possible for the configuration and stream interface 
	descriptors to come in any order, not necessarily all config descriptors *THEN*
    all stream interface descriptors.

    These objects only parse configuration and interface descriptors.  Code calling 
	ther parser must pass it configuration descriptor pointer obtained from the USB 
	services API via GetFullConfigurationDescriptor ().

    AUAConfigurationDictionary is the entry point for accessing all of this stored data.
    Directly calling any of the other objects is unnecessary. First allocate a 
	AUAConfigurationDictionary and call its init function which will do the parsing of 
	the configuration descriptor and create the other objects as necessary.  Once the 
	AUAConfigurationDictionary has been init'ed, call getNumStreamInterfaces, 
	GetNumAltStreamInterfaces, and getFirstStreamInterfaceNum so each interface can be
	queried for its properties.  Once a suitable interface has been found, configuring
    the interface with the normal USB calls can be conducted with ease.

    AUAConfigurationDictionary contains arrays of control and stream interfaces.  As the 
	configuration descriptor is parsed, new control or stream dictionaries are created 
	and inserted into the appropriate arrays.
*/

#include <libkern/c++/OSArray.h>
#include <IOKit/audio/IOAudioTypes.h>
#include "AppleUSBAudioDictionary.h"
#include "AppleUSBAudioCommon.h"


#pragma mark AppleUSBAudioDictionary
/* ------------------------------------------------------
    AppleUSBAudioDictionary
------------------------------------------------------ */
OSDefineMetaClassAndStructors (AppleUSBAudioDictionary, OSDictionary);

OSArray * AppleUSBAudioDictionary::getDictionaryArray (const char * key)
{
	OSObject *		dictionaryObject = NULL;
	OSArray *		dictionaryArray = NULL;
	
	dictionaryObject = getObject (key);
	if (dictionaryObject)
	{
		dictionaryArray = OSDynamicCast (OSArray, dictionaryObject);
	}
	
	// debugIOLog ("? AppleUSBAudioDictionary[%p]::getDictionaryArray (%s) = %p", this, key, dictionaryArray);
	return dictionaryArray;
}

IOReturn AppleUSBAudioDictionary::getDictionaryValue (const char * key, bool * value)
{
	OSObject *		dictionaryObject = NULL;
	OSBoolean *		dictionaryBoolean = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == value, Exit);
	FailIf (NULL == (dictionaryObject = getObject (key)), Exit);
	FailIf (NULL == (dictionaryBoolean = OSDynamicCast (OSBoolean, dictionaryObject)), Exit);
	* value = dictionaryBoolean->getValue();
	result = kIOReturnSuccess;

Exit:
	if (value && result != kIOReturnSuccess)
	{
		debugIOLog ("! AppleUSBAudioDictionary[%p]::getDictionaryValue (%s, %p = %d) = 0x%x", this, key, value, * value, result);
	}
	return result;
}

IOReturn AppleUSBAudioDictionary::getDictionaryValue (const char * key, UInt8 * value)
{
	OSObject *		dictionaryObject = NULL;
	OSNumber *		dictionaryNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == value, Exit);
	FailIf (NULL == (dictionaryObject = getObject (key)), Exit);
	FailIf (NULL == (dictionaryNumber = OSDynamicCast (OSNumber, dictionaryObject)), Exit);
	* value = dictionaryNumber->unsigned8BitValue();
	result = kIOReturnSuccess;

Exit:
	if (value && result != kIOReturnSuccess)
	{
		debugIOLog ("! AppleUSBAudioDictionary[%p]::getDictionaryValue (%s, %p = %d) = 0x%x", this, key, value, * value, result);
	}
	return result;
}

IOReturn AppleUSBAudioDictionary::getDictionaryValue (const char * key, UInt16 * value)
{
	OSObject *		dictionaryObject = NULL;
	OSNumber *		dictionaryNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == value, Exit);
	FailIf (NULL == (dictionaryObject = getObject (key)), Exit);
	FailIf (NULL == (dictionaryNumber = OSDynamicCast (OSNumber, dictionaryObject)), Exit);
	* value = dictionaryNumber->unsigned16BitValue();
	result = kIOReturnSuccess;

Exit:
	if (value && result != kIOReturnSuccess)
	{
		debugIOLog ("! AppleUSBAudioDictionary[%p]::getDictionaryValue (%s, %p = %d) = 0x%x", this, key, value, * value, result);
	}
	return result;
}

IOReturn AppleUSBAudioDictionary::getDictionaryValue (const char * key, UInt32 * value)
{
	OSObject *		dictionaryObject = NULL;
	OSNumber *		dictionaryNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == value, Exit);
	FailIf (NULL == (dictionaryObject = getObject (key)), Exit);
	FailIf (NULL == (dictionaryNumber = OSDynamicCast (OSNumber, dictionaryObject)), Exit);
	* value = dictionaryNumber->unsigned32BitValue();
	result = kIOReturnSuccess;

Exit:
	if (value && result != kIOReturnSuccess)
	{
		debugIOLog ("! AppleUSBAudioDictionary[%p]::getDictionaryValue (%s, %p = %lu) = 0x%x", this, key, value, * value, result);
	}
	return result;
}

// The following method is necessary to prepare an OSDictionary subclass for use after being allocated with "new."
bool AppleUSBAudioDictionary::initDictionaryForUse ()
{
	return initWithCapacity (1);
}

void AppleUSBAudioDictionary::logDescriptor (UInt8 * descriptor, UInt8 length)
{
	char *		descriptorString = NULL;
	char		byteAndSpace[4];
	UInt8		stringSize;
	
	FailIf (NULL == descriptor, Exit);
	FailIf (descriptor[0] != length, Exit);
	stringSize = (length * 3 + 1) * sizeof (char);
	FailIf (NULL == (descriptorString = (char *) IOMalloc (stringSize)), Exit);
	descriptorString[0] = '\0';
	
	for (UInt8 offset = 0; offset < length; offset++)
	{
		snprintf (byteAndSpace, 4, "%02x ", descriptor[offset]);
		strncat (descriptorString, byteAndSpace, stringSize);
	}
	
	debugIOLog ("  Current Descriptor: %s", descriptorString);
Exit:
	if (NULL != descriptorString)
	{
		IOFree (descriptorString, stringSize);
		descriptorString = NULL;
	}
	return;
}

IOReturn AppleUSBAudioDictionary::setDictionaryObjectAndRelease (const char * key, OSObject * object)
{
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == object, Exit);
	FailIf (false == setObject (key, object), Exit);
	result = kIOReturnSuccess;
	
Exit:
	if (object)
	{
		object->release ();
	}
	debugIOLog ("? AppleUSBAudioDictionary[%p]::setDictionaryObject (%s, %p) = 0x%x", this, key, object, result);
	return result;
}

IOReturn AppleUSBAudioDictionary::setDictionaryValue (const char * key, bool value)
{
	OSBoolean *		dictionaryBoolean = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == (dictionaryBoolean = OSBoolean::withBoolean (value)), Exit);
	FailIf (false == (setObject (key, dictionaryBoolean)), Exit);
	dictionaryBoolean->release ();
	dictionaryBoolean = NULL;
	result = kIOReturnSuccess;

Exit:
	debugIOLog ("? AppleUSBAudioDictionary[%p]::setDictionaryValue (%s, %d) = 0x%x", this, key, value, result);
	return result;

}

IOReturn AppleUSBAudioDictionary::setDictionaryValue (const char * key, UInt8 value)
{
	OSNumber *		dictionaryNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == (dictionaryNumber = OSNumber::withNumber (value, SIZEINBITS(UInt8))), Exit);
	FailIf (false == setObject (key, dictionaryNumber), Exit);
	dictionaryNumber->release ();
	dictionaryNumber = NULL;
	result = kIOReturnSuccess;

Exit:
	debugIOLog ("? AppleUSBAudioDictionary[%p]::setDictionaryValue (%s, %d) = 0x%x", this, key, value, result);
	return result;

}

IOReturn AppleUSBAudioDictionary::setDictionaryValue (const char * key, UInt16 value)
{
	OSNumber *		dictionaryNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == (dictionaryNumber = OSNumber::withNumber (value, SIZEINBITS(UInt16))), Exit);
	FailIf (false == (setObject (key, dictionaryNumber)), Exit);
	dictionaryNumber->release ();
	dictionaryNumber = NULL;
	result = kIOReturnSuccess;

Exit:
	debugIOLog ("? AppleUSBAudioDictionary[%p]::setDictionaryValue (%s, %d) = 0x%x", this, key, value, result);
	return result;

}

IOReturn AppleUSBAudioDictionary::setDictionaryValue (const char * key, UInt32 value)
{
	OSNumber *		dictionaryNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == (dictionaryNumber = OSNumber::withNumber (value, SIZEINBITS(UInt32))), Exit);
	FailIf (false == (setObject (key, dictionaryNumber)), Exit);
	dictionaryNumber->release ();
	dictionaryNumber = NULL;
	result = kIOReturnSuccess;

Exit:
	debugIOLog ("? AppleUSBAudioDictionary[%p]::setDictionaryValue (%s, %ld) = 0x%x", this, key, value, result);
	return result;

}

#pragma mark AUAConfigurationDictionary
/* ------------------------------------------------------
    AUAConfigurationDictionary
------------------------------------------------------ */
OSDefineMetaClassAndStructors (AUAConfigurationDictionary, AppleUSBAudioDictionary);

// [rdar://4867779]
IOReturn AUAConfigurationDictionary::addSampleRatesToStreamDictionary ( OSArray * sampleRates, UInt8 streamInterface, UInt8 altSetting )
{
	IOReturn						result = kIOReturnError;
	AUAStreamDictionary *			thisStream = NULL;
	
	FailIf ( NULL == (thisStream = getStreamDictionary ( streamInterface, altSetting ) ), Exit );
	result = thisStream->addSampleRatesToStreamDictionary ( sampleRates );

Exit:
	return result;
	
}

bool AUAConfigurationDictionary::alternateSettingZeroCanStream (UInt8 interfaceNum)
{
	OSObject *						dictionaryValue = NULL;
	OSArray *						streamInterfaces = NULL;
	OSArray *						endpoints = NULL;
	AUAStreamDictionary *			thisStream  = NULL;
	AUAEndpointDictionary *			thisEndpoint = NULL;
	UInt16							maxPacketSize;
	UInt8							streamInterfaceNum;
	UInt8							streamAltSettingID;
	UInt8							streamNumEndpoints;
	bool							result = false;
	
	dictionaryValue = getObject (kStreamDictionaries);
	if (dictionaryValue)
	{
		streamInterfaces = OSDynamicCast (OSArray, dictionaryValue);
	}
	
	if (NULL != streamInterfaces) 
	{
		for (UInt8 streamIndex = 0; streamIndex < streamInterfaces->getCount (); streamIndex++) 
		{
			thisStream = OSDynamicCast (AUAStreamDictionary, streamInterfaces->getObject (streamIndex));
			if (thisStream)
			{
				FailIf (kIOReturnSuccess != thisStream->getInterfaceNumber (&streamInterfaceNum), Exit);
				FailIf (kIOReturnSuccess != thisStream->getAlternateSetting (&streamAltSettingID), Exit);
				FailIf (kIOReturnSuccess != thisStream->getNumEndpoints (&streamNumEndpoints), Exit);
			}
			if (		(thisStream)
					&&	(streamInterfaceNum == interfaceNum)
					&&	(streamAltSettingID == 0)
					&&	(streamNumEndpoints > 0))					// [rdar://4340026] We don't care about this unless it has a streaming endpoint.
			{
				// [rdar://4793892] - There are streaming endpoints @ alt interface 0. Check to see if any have a nonzero maximum packet size.
				FailIf ( NULL == ( endpoints = thisStream->getEndpoints () ), Exit );
				for ( UInt8 endpointIndex = 0; endpointIndex < streamNumEndpoints; endpointIndex++ )
				{
					FailIf ( NULL == ( thisEndpoint = OSDynamicCast ( AUAEndpointDictionary, endpoints->getObject ( endpointIndex ) ) ), Exit );
					FailIf ( kIOReturnSuccess != thisEndpoint->getMaxPacketSize ( &maxPacketSize), Exit );
					if ( 0 < maxPacketSize )
					{
						debugIOLog ("? AUAConfigurationDictionary[%p]::alternateSettingZeroCanStream () = true", this);
						result = true;
						break;
					}
				}
			}
		}
	}
	
Exit:

	return result;
}

bool AUAConfigurationDictionary::asEndpointHasMaxPacketsOnly (UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	bool						result = false;
	
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	result = thisStream->asEndpointHasMaxPacketsOnly ();
	
Exit:
	return result;

}

IOReturn AUAConfigurationDictionary::asEndpointGetLockDelay (UInt8 * lockDelay, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

	* lockDelay = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisStream->asEndpointGetLockDelay (lockDelay)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::asEndpointGetLockDelayUnits (UInt8 * lockDelayUnits, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

	* lockDelayUnits = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisStream->asEndpointGetLockDelayUnits (lockDelayUnits)), Exit);

Exit:
    return result;
}

bool AUAConfigurationDictionary::asEndpointHasPitchControl (UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	bool						result = false;

    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	result = thisStream->asEndpointHasPitchControl ();
	
Exit:
    return result;
}

bool AUAConfigurationDictionary::asEndpointHasSampleFreqControl (UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	bool						result = false;

    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	result = thisStream->asEndpointHasSampleFreqControl ();

Exit:
    return result;
}

bool AUAConfigurationDictionary::channelHasMuteControl (UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID, UInt8 channelNum) 
{
    AUAControlDictionary *		thisControl;
    bool						returnBool = false;

    thisControl = getControlDictionary (interfaceNum, altSettingID);
    if (thisControl)
	{
		returnBool = thisControl->channelHasMuteControl (featureUnitID, channelNum);
	}
    return returnBool;
}

bool AUAConfigurationDictionary::channelHasVolumeControl (UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID, UInt8 channelNum) 
{
    AUAControlDictionary *		thisControl;
    bool						returnBool = false;

    thisControl = getControlDictionary (interfaceNum, altSettingID);
    if (thisControl)
	{
		returnBool = thisControl->channelHasVolumeControl (featureUnitID, channelNum);
	}
	else
	{
		debugIOLog ( "! AUAConfigurationDictionary::channelHasVolumeControl () - no control for interface %d alt setting %d", interfaceNum, altSettingID );
	}
	
    return returnBool;
}

// [rdar://4867779]
bool AUAConfigurationDictionary::clockSourceHasFrequencyControl (UInt8 interfaceNum, UInt8 altSetting, UInt8 clockSourceID, bool isProgrammable)
{
    AUAControlDictionary *		thisControl;
	bool						returnBool = false;
	
    thisControl = getControlDictionary (interfaceNum, altSetting);
    if (thisControl)
	{
		returnBool = thisControl->clockSourceHasFrequencyControl ( clockSourceID, isProgrammable );
	}
	else
	{
		debugIOLog ( "! AUAConfigurationDictionary::clockSourceHasFrequencyControl () - no control for interface %d alt setting %d programmable %d", interfaceNum, altSetting, isProgrammable );
	}
	
    return returnBool;
}

// <rdar://7446555>
bool AUAConfigurationDictionary::clockSourceHasValidityControl (UInt8 interfaceNum, UInt8 altSetting, UInt8 clockSourceID)
{
    AUAControlDictionary *		thisControl;
	bool						returnBool = false;
	
    thisControl = getControlDictionary (interfaceNum, altSetting);
    if (thisControl)
	{
		returnBool = thisControl->clockSourceHasValidityControl ( clockSourceID );
	}
	else
	{
		debugIOLog ( "! AUAConfigurationDictionary::clockSourceHasValidityControl () - no control for interface %d alt setting %d", interfaceNum, altSetting );
	}
	
    return returnBool;
}

IOReturn AUAConfigurationDictionary::getADCVersion (UInt16 * adcVersion)
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;
	UInt8						controlInterfaceNum = 0;
	
    * adcVersion = 0;
	FailIf (kIOReturnSuccess != (result = getControlInterfaceNum (&controlInterfaceNum)), Exit);
	
	result = kIOReturnError;
    FailIf (NULL == (thisControl = getControlDictionary (controlInterfaceNum, 0)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getADCVersion (adcVersion)), Exit);

Exit:
    return result;
}

AUAConfigurationDictionary * AUAConfigurationDictionary::create (const IOUSBConfigurationDescriptor * newConfigurationDescriptor, UInt8 controlInterfaceNum) 
{
    AUAConfigurationDictionary *			configDictionary = NULL;

	debugIOLog ("+ AUAConfigurationDictionary::create (%p, %d)", newConfigurationDescriptor, controlInterfaceNum);
	FailIf (NULL == newConfigurationDescriptor, Exit);
    FailIf (NULL == (configDictionary =  new AUAConfigurationDictionary), Exit);
	RELEASE_IF_FALSE (configDictionary, configDictionary->init(newConfigurationDescriptor, controlInterfaceNum));

Exit:
	debugIOLog ("- AUAConfigurationDictionary::create () = %p", configDictionary);
    return configDictionary;
}

#if DEBUGLOGGING
void AUAConfigurationDictionary::dumpConfigMemoryToIOLog (IOUSBConfigurationDescriptor * configurationDescriptor) 
{
	UInt8 *		descriptorPtr;
	UInt8 *		finalPtr;
	UInt8		descriptorIndex;
	UInt8		length;
	char		descriptor[256];
	char		num[4];

	descriptorPtr = (UInt8*) configurationDescriptor;
	finalPtr = (UInt8 *) ((uintptr_t) configurationDescriptor + USBToHostWord (configurationDescriptor->wTotalLength));
	
	while	(		(0 != *descriptorPtr)
				&&	( ( (uintptr_t) descriptorPtr + *descriptorPtr ) <= (uintptr_t) finalPtr) )
	{
		length = *descriptorPtr;
		descriptor[0] = 0;
		for (descriptorIndex = 0; descriptorIndex < length; descriptorIndex++) 
		{
			snprintf (num, 4, "%02X ", *descriptorPtr++);
			strncat (descriptor, num, 256);
		}
		descriptor[length * 3] = 0;
		debugIOLog ("%s", descriptor);
	}
}

#endif

IOReturn AUAConfigurationDictionary::getNextAltSettingWithNumChannels (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 startingAltSettingID, UInt8 numChannelsRequested) 
{
	IOReturn						result = kIOReturnError;
	UInt8							numAltSettings;
	UInt8							numChannels;
	UInt8							altSettingIndex;

	FailIf (NULL == altSettingID, Exit);
	* altSettingID = 0;
	FailIf (kIOReturnSuccess != getNumAltSettings (&numAltSettings, interfaceNum), Exit);
	for (altSettingIndex = startingAltSettingID; altSettingIndex < numAltSettings && result != kIOReturnSuccess; altSettingIndex++) 
	{
		FailIf (kIOReturnSuccess != getNumChannels (&numChannels, interfaceNum, altSettingIndex), Exit);
		if (numChannelsRequested == numChannels) 
		{
			* altSettingID = altSettingIndex;
			result = kIOReturnSuccess;
		}
	}
Exit:
	if (		( altSettingID )
			&&	( kIOReturnSuccess == result ) )
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::getNextAltSettingWithNumChannels (%p, %d, %d, %d) = 0x%x, choosing altSetting %d", 
					this, altSettingID, interfaceNum, startingAltSettingID, numChannelsRequested, result, * altSettingID);
	}
	else
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::getNextAltSettingWithNumChannels (%p, %d, %d, %d) = 0x%x, not found", 
					this, altSettingID, interfaceNum, startingAltSettingID, numChannelsRequested, result);
	}
	return result;
}

IOReturn AUAConfigurationDictionary::getNextAltSettingWithSampleSize (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 startingAltSettingID, UInt8 sampleSizeRequested) 
{
	IOReturn						result = kIOReturnError;
	UInt8							numAltSettings;
	UInt8							sampleSize;
	UInt8							altSettingIndex;

	FailIf (NULL == altSettingID, Exit);
	FailIf (kIOReturnSuccess != getNumAltSettings (&numAltSettings, interfaceNum), Exit);
	for (altSettingIndex = startingAltSettingID; altSettingIndex < numAltSettings && result != kIOReturnSuccess; altSettingIndex++) 
	{
		FailIf (kIOReturnSuccess != getBitResolution (&sampleSize, interfaceNum, altSettingIndex), Exit);
		if (sampleSize == sampleSizeRequested) 
		{
			* altSettingID = altSettingIndex;
			result = kIOReturnSuccess;
		}
	}
Exit:
	if (		( altSettingID )
			&&	( result == kIOReturnSuccess ) )
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::getNextAltSettingWithSampleSize (%p, %d, %d, %d) = 0x%x, choosing altSetting %d", 
					this, altSettingID, interfaceNum, startingAltSettingID, sampleSizeRequested, result, * altSettingID);
	}
	else
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::getNextAltSettingWithSampleSize (%p, %d, %d, %d) = 0x%x,not found", 
					this, altSettingID, interfaceNum, startingAltSettingID, sampleSizeRequested, result);
	}
	return result;
}

IOReturn AUAConfigurationDictionary::getNextAltSettingWithSampleRate (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 startingAltSettingID, UInt32 sampleRateRequested) 
{
	IOReturn						result = kIOReturnError;
	UInt8							numAltSettings;
	UInt8							altSettingIndex;

	FailIf (NULL == altSettingID, Exit);
	FailIf (kIOReturnSuccess != getNumAltSettings (&numAltSettings, interfaceNum), Exit);
	for (altSettingIndex = startingAltSettingID; altSettingIndex < numAltSettings && result != kIOReturnSuccess; altSettingIndex++) 
	{
		if (true == verifySampleRateIsSupported (interfaceNum, altSettingIndex, sampleRateRequested)) 
		{
			* altSettingID = altSettingIndex;
			result = kIOReturnSuccess;
		}
	}
Exit:
	if (		( altSettingID )
			&&	( kIOReturnSuccess == result ) )
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::getNextAltSettingWithSampleRate (%p, %d, %d, %lu) = 0x%x, choosing altSetting %d", 
					this, altSettingID, interfaceNum, startingAltSettingID, sampleRateRequested, result, * altSettingID);
	}
	else
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::getNextAltSettingWithSampleRate (%p, %d, %d, %lu) = 0x%x, not found", 
					this, altSettingID, interfaceNum, startingAltSettingID, sampleRateRequested, result);
	}
	return result;
}

IOReturn AUAConfigurationDictionary::getAltSettingWithSettings (UInt8 * altSettingID, UInt8 interfaceNum, UInt8 numChannels, UInt8 sampleSize, UInt32 sampleRate) 
{
	UInt8			potentialAltSetting;
	UInt8			thisAltSetting;
	IOReturn		result = kIOReturnError;
	IOReturn		interimResult;
	UInt16			format;

	debugIOLog ("+ AUAConfigurationDictionary[%p]::getAltSettingWithSettings (%p, %d, %d, %d, %lu)", this, altSettingID, interfaceNum, numChannels, sampleSize, sampleRate);
	// [rdar://4228470] Only consider alternate interface 0 if there are streaming endpoints. /thw
	if (alternateSettingZeroCanStream (interfaceNum))
	{
		potentialAltSetting = 0;
	}
	else
	{
		potentialAltSetting = 1;
	}
	
	while	(		(kIOReturnSuccess != result)
				&&	(potentialAltSetting != 255)) 
	{

		if	(		(kIOReturnSuccess == getFormat (&format, interfaceNum, potentialAltSetting) )
				&&	( 0 != (format & 0x0FFF)))
		{	
			// Make sure it's not an undefined format
			if (kIOReturnSuccess != getNextAltSettingWithNumChannels (&thisAltSetting, interfaceNum, potentialAltSetting, numChannels))
			{
				debugIOLog ("? AUAConfigurationDictionary[%p]::getAltSettingWithSettings () - No alternate setting ID for interface %d, %d channels. Terminating loop.", this, interfaceNum, numChannels);
				break;		// Done, so break out of while loop
			}
			else
			{
				debugIOLog ("? AUAConfigurationDictionary[%p]::getAltSettingWithSettings () - Examining alt interface %d.", this, thisAltSetting);
				potentialAltSetting = thisAltSetting;				
			}
			// Any errors here are non-fatal.
			interimResult = getNextAltSettingWithSampleSize (&thisAltSetting, interfaceNum, potentialAltSetting, sampleSize);
			if	(		(kIOReturnSuccess == interimResult)
					&&	(potentialAltSetting == thisAltSetting)) 
			{
				if (0 != sampleRate) 
				{
					// Must find a specific sample rate here.
					debugIOLog ("? ConfigurationDictionary[%p]::getAltSettingWithSettings () - Attempting to match sample rate %lu.", this, sampleRate);
					interimResult = getNextAltSettingWithSampleRate (&thisAltSetting, interfaceNum, potentialAltSetting, sampleRate);
					if	(		(kIOReturnSuccess == interimResult)
							&&	(potentialAltSetting == thisAltSetting)) 
					{
						* altSettingID = potentialAltSetting;
						result = kIOReturnSuccess;
					} 
					else 
					{
						potentialAltSetting++;
					}
				} 
				else 
				{
					debugIOLog ("? ConfigurationDictionary[%p]::getAltSettingWithSettings () - Not attempting to match sample rate.", this);
					// Sample rate matching is unnecessary.
					* altSettingID = potentialAltSetting;
					result = kIOReturnSuccess;
				}
			} 
			else 
			{
				potentialAltSetting++;
			}
		} 
		else 
		{
			potentialAltSetting++;
		}
	} // while
	
	debugIOLog ("- AUAConfigurationDictionary[%p]::getAltSettingWithSettings (%p = %d, %d, %d, %d, %lu) = 0x%x", this, altSettingID, * altSettingID, interfaceNum, numChannels, sampleSize, sampleRate, result);
	return result;
}

IOReturn AUAConfigurationDictionary::getAC3BSID (UInt32 * ac3BSID, UInt8 interfaceNum, UInt8 altSettingID) 
{
	AUAStreamDictionary * 		thisStream;
	IOReturn					result = kIOReturnError;
	
	*ac3BSID = 0;
	thisStream = getStreamDictionary (interfaceNum, altSettingID);
	if (thisStream)
	{
		result = thisStream->getAC3BSID (ac3BSID);
	}
	else
	{
		debugIOLog ("! AUAConfigurationDictionary[%p]::getAC3BSID (%p, %d, %d) - Could not get stream interface dictionary.", this, ac3BSID, interfaceNum, altSettingID);
	}
	return result;
}

IOReturn AUAConfigurationDictionary::getFeatureUnitIDConnectedToOutputTerminal (UInt8 * featureUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 outputTerminalID) 
{
    AUAControlDictionary *		thisControl;
    IOReturn					result = kIOReturnError;

    thisControl = getControlDictionary (interfaceNum, altSettingID);
    if (thisControl)
	{
		result = thisControl->getFeatureUnitIDConnectedToOutputTerminal (featureUnitID, outputTerminalID);
	}
	else
	{
		debugIOLog ("! AUAConfigurationDictionary[%p]::GetFeatureUnitConnectedToOutputTerminal (%p, %d, %d, %d) - Could not get control interface dictionary.", 
					this, featureUnitID, interfaceNum, altSettingID, outputTerminalID);
	}
    return result;
}

IOReturn AUAConfigurationDictionary::getFirstStreamInterfaceNum (UInt8 * interfaceNum) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	OSArray	*					streamDictionaries = NULL;
	OSObject *					dictionaryValue = NULL;
	IOReturn					result = kIOReturnError;

	* interfaceNum = 0;
	
	FailIf (NULL == (dictionaryValue = getObject (kStreamDictionaries)), Exit);
	FailIf (NULL == (streamDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);
	FailIf (NULL == (thisStream = OSDynamicCast (AUAStreamDictionary, streamDictionaries->getObject (0))), Exit);

	result = thisStream->getInterfaceNumber (interfaceNum);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getControlledStreamNumbers (OSArray **controlledStreams, UInt8 *numControlledStreams) 
{
    AUAControlDictionary * 		thisControl = NULL;
	OSArray *					controlDictionaries = NULL;
	OSObject *					dictionaryValue = NULL;
	IOReturn					result = kIOReturnError;

	FailIf (NULL == controlledStreams, Exit);
	* controlledStreams = NULL;
	* numControlledStreams = 0;

	FailIf (NULL == (dictionaryValue = getObject (kControlDictionaries)), Exit);
	FailIf (NULL == (controlDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);
	FailIf (NULL == (thisControl = OSDynamicCast (AUAControlDictionary, controlDictionaries->getObject (0))), Exit); 
	
	FailIf (NULL == (*controlledStreams = thisControl->getStreamInterfaceNumbers ()), Exit);
	FailIf (kIOReturnSuccess != (result = (thisControl->getNumStreamInterfaces (numControlledStreams))), Exit);
	
Exit:
	return result;
}
/*
IOReturn AUAConfigurationDictionary::getControlInterfaceNum (UInt8 *interfaceNum) 
{
    AUAControlDictionary * 		thisControl = NULL;
	OSArray *					controlDictionaries = NULL;
	OSObject *					dictionaryValue = NULL;
	IOReturn					result = kIOReturnError;

	* interfaceNum = 0;
	FailIf (NULL == (dictionaryValue = getObject (kControlDictionaries)), Exit);
	FailIf (NULL == (controlDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);
	FailIf (NULL == (thisControl = OSDynamicCast (AUAControlDictionary, controlDictionaries->getObject (0))), Exit);
	FailIf (kIOReturnSuccess != (result = (thisControl->getInterfaceNumber (interfaceNum))), Exit);

Exit:
    return result;
}
*/

IOReturn AUAConfigurationDictionary::getControlInterfaceNum (UInt8 *interfaceNum)
{
	return getDictionaryValue (kControlInterfaceNumber, interfaceNum);
}

IOReturn AUAConfigurationDictionary::getIsocEndpointInterval (UInt8 * interval, UInt8 interfaceNum, UInt8 altSettingID, UInt8 direction)
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* interval = 0;
	thisStream = getStreamDictionary ( interfaceNum, altSettingID );
	if ( thisStream )
	{
		FailIf ( kIOReturnSuccess != ( result = thisStream->getIsocEndpointInterval ( interval, direction ) ), Exit );
	}
Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getFormat (UInt16 * format, UInt8 interfaceNum, UInt8 altSettingID)
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* format = TYPE_I_UNDEFINED;
	thisStream = getStreamDictionary (interfaceNum, altSettingID);
	if (thisStream)
	{
		FailIf (kIOReturnSuccess != (result = thisStream->getFormatTag (format)), Exit);
	}
Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getHighestSampleRate (UInt32 * sampleRate, UInt8 interfaceNum, UInt8 altSettingID) 
{
	OSArray *				sampleRates = NULL;
	OSObject *				arrayValue;
	OSNumber *				sampleRateNumber;
	IOReturn				result = kIOReturnError;
	UInt32					sampleRateIndex;
	UInt8					numSampleRates;

	debugIOLog ("+ AUAConfigurationDictionary[%p]::getHighestSampleRate (%p, %d, %d)", this, sampleRate, interfaceNum, altSettingID);
	* sampleRate = 0;
	FailIf (NULL == (sampleRates = getSampleRates (interfaceNum, altSettingID)), Exit);

	// [rdar://5313648] The number of sample rates could be variable, so check the size of the array.
	numSampleRates = sampleRates->getCount ();
	 
	for (sampleRateIndex = 0; sampleRateIndex < numSampleRates; sampleRateIndex++) 
	{
		FailIf (NULL == (arrayValue = sampleRates->getObject (sampleRateIndex)), Exit);
		FailIf (NULL == (sampleRateNumber = OSDynamicCast (OSNumber, arrayValue)), Exit);
		
		debugIOLog ( "   %d: %d", sampleRateIndex, sampleRateNumber->unsigned32BitValue () );
		
		// Keep the higher sample rate.
		* sampleRate = (sampleRateNumber->unsigned32BitValue () > ( * sampleRate ) ? sampleRateNumber->unsigned32BitValue () : ( * sampleRate ));
	}
	result = kIOReturnSuccess;
	
Exit:
	debugIOLog ("- AUAConfigurationDictionary[%p]::getHighestSampleRate (%p = %lu, %d, %d) = 0x%x", this, sampleRate, *sampleRate, interfaceNum, altSettingID, result);
	return result;
}

IOReturn AUAConfigurationDictionary::getIsocAssociatedEndpointAddress (UInt8 * assocEndpointAddress, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address) 
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* assocEndpointAddress = 0;
	FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	result = thisStream->getIsocAssociatedEndpointAddress (assocEndpointAddress, address);
	
	
Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getIsocAssociatedEndpointMaxPacketSize (UInt16 * maxPacketSize, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address) 
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* maxPacketSize = 0;
	FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	result = thisStream->getIsocAssociatedEndpointMaxPacketSize (maxPacketSize, address);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getIsocAssociatedEndpointRefreshInt (UInt8 * refreshInt, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address) 
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* refreshInt = 0;
	FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);	
	result = thisStream->getIsocAssociatedEndpointRefreshInt (refreshInt, address);
	
Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getIsocEndpointAddress (UInt8 * address, UInt8 interfaceNum, UInt8 altSettingID, UInt8 direction) 
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* address = 0;
	FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);		
	result = thisStream->getIsocEndpointAddress (address, direction);

Exit:
	return result;
}

// Use getTerminalLink to get the unit number of the input or output terminal with which the endpoint is associated.
// From that terminal the direction can be determined, which in turn clarifies the endpoint direction.

IOReturn AUAConfigurationDictionary::getIsocEndpointDirection (UInt8 * direction, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary *		thisStream = NULL;
    AUAControlDictionary * 		thisControl = NULL;
	IOReturn					result = kIOReturnError;
    UInt8						endpointDirection;
	UInt8						terminalLink;
	UInt8						numOutputs;
	UInt8						numInputs;
	UInt8						numEndpoints;
	UInt8						index;
	UInt8						terminalID;
	UInt8						controlInterfaceNum;
	bool						done = false;

	debugIOLog ("+ AUAConfigurationDictionary[%p]::getIsocEndpointDirection (%p, %d, %d)", this, direction, interfaceNum, altSettingID);
	
	index = 0;
	numEndpoints = 0;

	FailIf (NULL == direction, Exit);
	* direction = 0xFF;
	
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != getControlInterfaceNum (&controlInterfaceNum), Exit);
	FailIf (NULL == (thisControl = getControlDictionary (controlInterfaceNum, 0)), Exit);
	
	// The following returns the unitID of the terminal with which the endpoint is associated
	FailIf (kIOReturnSuccess != thisStream->getTerminalLink (&terminalLink), Exit);		
	
	debugIOLog ("? AUAConfigurationDictionary[%p]::getIsocEndpointDirection () - terminalLink = %d", this, terminalLink);
	if (terminalLink) 
	{
		FailIf (kIOReturnSuccess != thisControl->getNumOutputTerminals (&numOutputs), Exit);
		debugIOLog ("? AUAConfigurationDictionary[%p]::getIsocEndpointDirection() - numOutputs = %d", this, numOutputs);
		while (index < numOutputs) 
		{
			FailIf (kIOReturnSuccess != thisControl->getIndexedOutputTerminalID (&terminalID, index), Exit);
			if (terminalID == terminalLink) 
			{
				* direction = kUSBIn;
				numEndpoints = numOutputs;
				debugIOLog ("? AUAConfigurationDictionary[%p]::getIsocEndpointDirection () - Found an output terminal (%d) at index %d", this, terminalID, index);
				break;
			} 
			else 
			{
				index++;
			}
		}
		
		if (0xFF == * direction) 
		{
			FailIf (kIOReturnSuccess != (result = thisControl->getNumInputTerminals (&numInputs)), Exit);
			debugIOLog ("? AUAConfigurationDictionary[%p]::getIsocEndpointDirection() - numInputs = %d", this, numInputs);
			index = 0;
			while (index < numInputs) 
			{
				FailIf (kIOReturnSuccess != (result = thisControl->getIndexedInputTerminalID (&terminalID, index)), Exit);
				if (terminalID == terminalLink) 
				{
					* direction = kUSBOut;
					numEndpoints = numInputs;
					debugIOLog ("? AUAConfigurationDictionary[%p]::getIsocEndpointDirection () - Found an input terminal (%d) at index %d", this, terminalID, index);
					break;
				} 
				else 
				{
					index++;
				}
			} 
		} 
				
	} // (if terminalLink)
	else
	{
		result = kIOReturnError;
	}
	
	if (0xFF != * direction) 
	{
		for (index = 0; index < numEndpoints; index++) 
		{
			FailIf (kIOReturnSuccess != (result = thisStream->getIsocEndpointDirection (&endpointDirection, index)), Exit);
			if (endpointDirection == * direction) 
			{
				done = true;
				break;		// found the right endpoint, get out of for loop
			}
		}
		result = (done ? kIOReturnSuccess : kIOReturnError);
	}
	
Exit:
	debugIOLog ("- AUAConfigurationDictionary[%p]::getIsocEndpointDirection (%d, %d, %d) = 0x%x", this, *direction, interfaceNum, altSettingID, result);
    return result;
}

IOReturn AUAConfigurationDictionary::getIsocEndpointMaxPacketSize (UInt16 * maxPacketSize, UInt8 interfaceNum, UInt8 altSettingID, UInt8 direction)
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* maxPacketSize = 0;
	FailIf ( NULL == ( thisStream = getStreamDictionary ( interfaceNum, altSettingID ) ), Exit );		
	result = thisStream->getIsocEndpointMaxPacketSize (maxPacketSize, direction);

Exit:
	return result;	
}


IOReturn AUAConfigurationDictionary::getIsocEndpointSyncType (UInt8 * syncType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 address) 
{
	AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* syncType = 0;
	FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getIsocEndpointSyncType (syncType, address)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getIndexedFeatureUnitID (UInt8 * featureUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitIndex) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * featureUnitID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getIndexedFeatureUnitID (featureUnitID, featureUnitIndex)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedMixerUnitID (UInt8 * mixerUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 mixerUnitIndex) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * mixerUnitID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getIndexedMixerUnitID (mixerUnitID, mixerUnitIndex)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedSelectorUnitID (UInt8 * selectorUnitID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 selectorUnitIndex) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * selectorUnitID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getIndexedSelectorUnitID (selectorUnitID, selectorUnitIndex)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedInputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

	* terminalType = USBToHostWord (INPUT_UNDEFINED);
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getIndexedInputTerminalType (terminalType, index)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedInputTerminalID (UInt8 * terminalID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

	* terminalID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getIndexedInputTerminalID (terminalID, index)), Exit);
	
Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedOutputTerminalID (UInt8 * terminalID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

	* terminalID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getIndexedOutputTerminalID (terminalID, index)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedOutputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 index) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

	* terminalType = USBToHostWord (OUTPUT_UNDEFINED);
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getIndexedOutputTerminalType (terminalType, index)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getInputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 terminalID) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * terminalType = USBToHostWord (INPUT_UNDEFINED);
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getInputTerminalType (terminalType, terminalID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedClockSourceID (UInt8 * clockSourceID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 clockSourceIndex) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * clockSourceID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getIndexedClockSourceID (clockSourceID, clockSourceIndex)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedClockSelectorID (UInt8 * clockSelectorID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 clockSelectorIndex) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * clockSelectorID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getIndexedClockSelectorID (clockSelectorID, clockSelectorIndex)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getIndexedClockMultiplierID (UInt8 * clockMultiplierID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 clockMultiplierIndex) 
{
    AUAControlDictionary *		thisControl = NULL;
    IOReturn					result = kIOReturnError;

    * clockMultiplierID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getIndexedClockMultiplierID (clockMultiplierID, clockMultiplierIndex)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getInterfaceClass (UInt8 * interfaceClass, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream;
    IOReturn					result = kIOReturnError;

    * interfaceClass = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getInterfaceClass (interfaceClass)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getInterfaceSubClass (UInt8 * interfaceSubClass, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
    IOReturn					result = kIOReturnError;

    * interfaceSubClass = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getInterfaceSubClass (interfaceSubClass)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getLowestSampleRate (UInt32 * sampleRate, UInt8 interfaceNum, UInt8 altSettingID) 
{
	OSArray *				sampleRates = NULL;
	OSObject *				arrayValue = NULL;
	OSNumber *				sampleRateNumber = NULL;
	IOReturn				result = kIOReturnError;
	UInt32					sampleRateIndex;
	UInt8					numSampleRates;
	
	FailIf (NULL == (sampleRates = getSampleRates (interfaceNum, altSettingID)), Exit);
	numSampleRates = sampleRates->getCount ();
	
	* sampleRate = 0xFFFFFFFF;
	
	for (sampleRateIndex = 0; sampleRateIndex < numSampleRates; sampleRateIndex++) 
	{
		FailIf (NULL == (arrayValue = sampleRates->getObject (sampleRateIndex)), Exit);
		FailIf (NULL == (sampleRateNumber = OSDynamicCast (OSNumber, arrayValue)), Exit);
		
		* sampleRate = (sampleRateNumber->unsigned32BitValue () < ( * sampleRate ) ? sampleRateNumber->unsigned32BitValue () : ( * sampleRate ) );
	}

	result = kIOReturnSuccess;

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getMaxBitRate (UInt16 * maxBitRate, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
    IOReturn					result = kIOReturnError;
	
	* maxBitRate = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getMaxBitRate (maxBitRate)), Exit);
	
Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumAltSettings (UInt8 * numAltSettings, UInt8 interfaceNum) 
{
    AUAStreamDictionary * 	thisStream = NULL;
	OSObject *				dictionaryValue = NULL;
	OSArray *				streamDictionaries = NULL;
	IOReturn				result = kIOReturnError;
    UInt16					streamIndex;
	UInt8					streamInterfaceNum;

    * numAltSettings = 0;
	
	FailIf (NULL == (dictionaryValue = getObject (kStreamDictionaries)), Exit);
	FailIf (NULL == (streamDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);
	
	for (streamIndex = 0; streamIndex < streamDictionaries->getCount (); streamIndex++) 
	{
		FailIf (NULL == (thisStream = OSDynamicCast (AUAStreamDictionary, streamDictionaries->getObject (streamIndex))), Exit);
		FailIf (kIOReturnSuccess != (result = thisStream->getInterfaceNumber (&streamInterfaceNum)), Exit);
		if (streamInterfaceNum == interfaceNum) 
		{
			(* numAltSettings)++;
		}
	}

	result = kIOReturnSuccess;
	
Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getNumChannels (UInt8 * numChannels, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

    * numChannels = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisStream->getNumChannels (numChannels)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getNumControls (UInt8 * numControls, UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numControls = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumControls (numControls, featureUnitID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getNumSampleRates (UInt8 * numSampleRates, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

    * numSampleRates = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisStream->getNumSampleRates (numSampleRates)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getNumInputTerminals (UInt8 * numInputTerminals, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numInputTerminals = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumInputTerminals (numInputTerminals)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumOutputTerminals (UInt8 * numOutputTerminals, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numOutputTerminals = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumOutputTerminals (numOutputTerminals)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumSelectorUnits (UInt8 * numSelectorUnits, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numSelectorUnits = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumSelectorUnits (numSelectorUnits)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumClockSources (UInt8 * numClockSources, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numClockSources = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumClockSources (numClockSources)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumClockSelectors (UInt8 * numClockSelectors, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numClockSelectors = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumClockSelectors (numClockSelectors)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumClockMultipliers (UInt8 * numClockMultipliers, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numClockMultipliers = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumClockMultipliers (numClockMultipliers)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumSources (UInt8 * numSources, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	* numSources = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getNumSources (numSources, unitID)), Exit);

Exit:
	return result;
}

IOReturn AUAConfigurationDictionary::getNumStreamInterfaces (UInt8 * numStreamInterfaces) 
{
    AUAStreamDictionary * 	thisStream = NULL;
	OSObject *				dictionaryValue = NULL;
	OSArray *				streamDictionaries = NULL;
	IOReturn				result = kIOReturnError;
    UInt8					streamInterfaceNum;
	UInt8					thisInterfaceNum;
    UInt16					streamIndex;

    * numStreamInterfaces = 0;
    thisInterfaceNum = 0;
	FailIf (NULL == (dictionaryValue = getObject (kStreamDictionaries)), Exit);
	FailIf (NULL == (streamDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);
	
	// Each time the stream interface changes, increment the number of stream interfaces. Begin with 0 since it is impossible to have
	// a class-complaint device with a USB audio streaming interface at interface 0 since it must be preceded by at least one control
	// interface.
	
	for (streamIndex = 0; streamIndex < streamDictionaries->getCount (); streamIndex++) 
	{
		FailIf (NULL == (thisStream = OSDynamicCast (AUAStreamDictionary, streamDictionaries->getObject (streamIndex))), Exit);
		FailIf (kIOReturnSuccess != (result = thisStream->getInterfaceNumber (&streamInterfaceNum)), Exit);
		if (streamInterfaceNum != thisInterfaceNum) 
		{
			thisInterfaceNum = streamInterfaceNum;
			(* numStreamInterfaces)++;
		}
	}
	
	result = kIOReturnSuccess;
	
Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getOutputTerminalType (UInt16 * terminalType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 terminalID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * terminalType = USBToHostWord (OUTPUT_UNDEFINED);
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisControl->getOutputTerminalType (terminalType, terminalID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getSamplesPerFrame (UInt16 * samplesPerFrame, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;
	
	* samplesPerFrame = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getMaxBitRate (samplesPerFrame)), Exit);

Exit:	
	return result;
}

OSArray * AUAConfigurationDictionary::getSampleRates (UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	OSArray *					sampleRates = NULL;
	
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	sampleRates = thisStream->getSampleRates ();
Exit:
    return sampleRates;
}

IOReturn AUAConfigurationDictionary::getBitResolution (UInt8 * sampleSize, UInt8 interfaceNum, UInt8 altSettingID) {
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

    * sampleSize = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getBitResolution (sampleSize)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getSelectorSources (OSArray ** selectorSources, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * selectorSources = NULL;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getSelectorSources (selectorSources, unitID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getClockSelectorSources (OSArray ** clockSelectorSources, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * clockSelectorSources = NULL;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getClockSelectorSources (clockSelectorSources, unitID)), Exit);

Exit:
    return result;
}

//	<rdar://5811247>
IOReturn AUAConfigurationDictionary::getClockSourceClockType (UInt8 * clockType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID)
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * clockType = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getClockSourceClockType (clockType, unitID)), Exit);

Exit:
    return result;
}

//	<rdar://5811247>
IOReturn AUAConfigurationDictionary::getClockSourceAssocTerminal (UInt8 * assocTerminal, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID)
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * assocTerminal = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getClockSourceAssocTerminal (assocTerminal, unitID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getSubframeSize (UInt8 * subframeSize, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

    * subframeSize = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getSubframeSize (subframeSize)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getSubType (UInt8 * subType, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * subType = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getSubType (subType, unitID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getSourceID (UInt8 * sourceID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * sourceID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getSourceID (sourceID, unitID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getSourceIDs (OSArray ** sourceIDs, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * sourceIDs = NULL;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getSourceIDs (sourceIDs, unitID)), Exit);

Exit:
    return result;
}

OSArray * AUAConfigurationDictionary::getTerminalClockEntities ( UInt8 interfaceNum, UInt8 altSettingID )
{
	AUAControlDictionary *		thisControl = NULL;
	OSArray *					clockEntities = NULL;
	
	FailIf ( NULL == ( thisControl = getControlDictionary ( interfaceNum, altSettingID ) ), Exit );
	clockEntities = thisControl->getTerminalClockEntities ();

Exit:
	return clockEntities;
}

IOReturn AUAConfigurationDictionary::getClockSourceID (UInt8 * clockSourceID, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * clockSourceID = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getClockSourceID (clockSourceID, unitID)), Exit);

Exit:
    return result;
}

// <rdar://6394629>
IOReturn AUAConfigurationDictionary::getStringIndex (UInt8 * stringIndex, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

    * stringIndex = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getStringIndex (stringIndex, unitID)), Exit);

Exit:
    return result;
}

// <rdar://6430836>
IOReturn AUAConfigurationDictionary::getAudioClusterDescriptor (AudioClusterDescriptor * clusterDescriptor, UInt8 interfaceNum, UInt8 altSettingID, UInt8 unitID) 
{
    AUAControlDictionary *		thisControl = NULL;
	IOReturn					result = kIOReturnError;

	clusterDescriptor->bNrChannels = 0;
	clusterDescriptor->bmChannelConfig = 0;
	clusterDescriptor->iChannelNames = 0;
    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
    FailIf (kIOReturnSuccess != (result = thisControl->getAudioClusterDescriptor (clusterDescriptor, unitID)), Exit);

Exit:
    return result;
}

IOReturn AUAConfigurationDictionary::getTerminalLink (UInt8 * terminalLink, UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 		thisStream = NULL;
	IOReturn					result = kIOReturnError;

    * terminalLink = 0;
    FailIf (NULL == (thisStream = getStreamDictionary (interfaceNum, altSettingID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisStream->getTerminalLink (terminalLink)), Exit);

Exit:
    return result;
}

bool AUAConfigurationDictionary::init (const IOUSBConfigurationDescriptor * newConfigurationDescriptor, UInt8 controlInterfaceNum) 
{
	IOUSBConfigurationDescriptor *	mutableDescriptor = NULL;
    bool							result = false;
	
	debugIOLog ("+ AUAConfigurationDictionary[%p]::init (%p, %d)", this, newConfigurationDescriptor, controlInterfaceNum);
	FailIf (false == initDictionaryForUse (), Exit);
    FailIf (NULL == newConfigurationDescriptor, Exit);

	FailIf (kIOReturnSuccess != setDictionaryValue (kControlInterfaceNumber, controlInterfaceNum), Exit);

	debugIOLog ("? AUAConfigurationDictionary[%p]::init () - Allocating %d bytes for mutable config descriptor.", this, USBToHostWord (newConfigurationDescriptor->wTotalLength));
    mutableDescriptor = (IOUSBConfigurationDescriptor *)IOMalloc (USBToHostWord (newConfigurationDescriptor->wTotalLength) + 1);
    FailIf (NULL == mutableDescriptor, Exit);

    memcpy (mutableDescriptor, newConfigurationDescriptor, USBToHostWord (newConfigurationDescriptor->wTotalLength));
    ( (UInt8 *) mutableDescriptor) [USBToHostWord (newConfigurationDescriptor->wTotalLength)] = 0;
	#if DEBUGLOGGING
	dumpConfigMemoryToIOLog (mutableDescriptor);
	#endif

    FailIf (kIOReturnSuccess != parseConfigurationDescriptor (mutableDescriptor), Exit);
	result = true;

Exit:
	if (NULL != mutableDescriptor) 
	{
		IOFree (mutableDescriptor, USBToHostWord (newConfigurationDescriptor->wTotalLength) + 1);
		mutableDescriptor = NULL;
	}
	debugIOLog ("- AUAConfigurationDictionary[%p]::init () = 0x%x", this, result);
    return result;
}

bool AUAConfigurationDictionary::masterHasMuteControl (UInt8 interfaceNum, UInt8 altSettingID, UInt8 featureUnitID) 
{
    AUAControlDictionary *		thisControl = NULL;
    bool						result = false;

    FailIf (NULL == (thisControl = getControlDictionary (interfaceNum, altSettingID)), Exit);
	result = thisControl->masterHasMuteControl (featureUnitID);

Exit:
    return result;
}

bool AUAConfigurationDictionary::verifySampleRateIsSupported (UInt8 interfaceNum, UInt8 altSettingID, UInt32 verifyRate) 
{
	bool							result = false;
	OSObject *						arrayValue = NULL;
	OSArray *						sampleRates = NULL;
	OSNumber *						sampleRateNumber = NULL;
	OSNumber *						sampleRateNumberHigh = NULL;
	UInt8							sampleRateIndex;
	UInt8							numSampleRates;

	FailIf (kIOReturnSuccess != getNumSampleRates (&numSampleRates, interfaceNum, altSettingID), Exit);
	FailIf (NULL == (sampleRates = getSampleRates (interfaceNum, altSettingID)), Exit);
	if (numSampleRates) 
	{
		// There are a discrete number of sample rates supported, so check for the desired sample rate.
		for (sampleRateIndex = 0; sampleRateIndex < numSampleRates && result == false; sampleRateIndex++) 
		{
			FailIf (NULL == (arrayValue = sampleRates->getObject (sampleRateIndex)), Exit);
			FailIf (NULL == (sampleRateNumber = OSDynamicCast (OSNumber, arrayValue)), Exit); 
			if (sampleRateNumber->unsigned32BitValue () == verifyRate) 
			{
				result = true;
			}
		}
	} 
	else
	{
		// There is a range of sample rates supported, so check for the desired sample rate within that range.
		FailIf (NULL == (arrayValue = sampleRates->getObject (0)), Exit);
		FailIf (NULL == (sampleRateNumber = OSDynamicCast (OSNumber, arrayValue)), Exit); 
		FailIf (NULL == (arrayValue = sampleRates->getObject (1)), Exit);
		FailIf (NULL == (sampleRateNumberHigh = OSDynamicCast (OSNumber, arrayValue)), Exit); 
		if	(		(sampleRateNumber->unsigned32BitValue () <= verifyRate)
				&&	(sampleRateNumberHigh->unsigned32BitValue () >= verifyRate))
		{
			result = true;
		}
	}
	
Exit:
	return result;
}

bool AUAConfigurationDictionary::hasAudioStreamingInterfaces (void) 
{
	bool result = false;
	
	OSArray * controlDictionaries = getControlDictionaries ();
	
	if ( NULL != controlDictionaries && 0 != controlDictionaries->getCount () )
	{
		result = true;
	}
	
	return result;
}

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
IOReturn AUAConfigurationDictionary::getInterruptEndpointAddress ( UInt8 * address, UInt8 interfaceNum, UInt8 altSettingID ) 
{
	AUAControlDictionary * 		thisControl = NULL;
	IOReturn					result = kIOReturnError;
	
	* address = 0;
	FailIf ( NULL == ( thisControl = getControlDictionary ( interfaceNum, altSettingID ) ), Exit );		
	result = thisControl->getInterruptEndpointAddress ( address );

Exit:
	return result;
}

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
IOReturn AUAConfigurationDictionary::getInterruptEndpointInterval ( UInt8 * interval, UInt8 interfaceNum, UInt8 altSettingID )
{
	AUAControlDictionary * 		thisControl = NULL;
	IOReturn					result = kIOReturnError;
	
	* interval = 0;
	FailIf ( NULL == ( thisControl = getControlDictionary ( interfaceNum, altSettingID ) ), Exit );		
	result = thisControl->getInterruptEndpointInterval ( interval );

Exit:
	return result;
}

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
bool AUAConfigurationDictionary::hasInterruptEndpoint ( UInt8 interfaceNum, UInt8 altSettingID ) 
{
	AUAControlDictionary * 		thisControl = NULL;
	bool						result = false;
	
	FailIf ( NULL == ( thisControl = getControlDictionary ( interfaceNum, altSettingID ) ), Exit );		
	result = thisControl->hasInterruptEndpoint ();

Exit:
	return result;
}

// Private methods

AUAStreamDictionary * AUAConfigurationDictionary::getStreamDictionary (UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAStreamDictionary * 	thisStream = NULL;
	OSObject *				dictionaryValue = NULL;
	OSArray *				streamDictionaries = NULL;
    UInt8					streamIndex = 0;
	UInt8					streamInterfaceNum;
	UInt8					streamAltSettingID;
    bool					found = false;

	FailIf (NULL == (dictionaryValue = getObject (kStreamDictionaries)), Exit);
	FailIf (NULL == (streamDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);
	
	while (!found && streamIndex < streamDictionaries->getCount ()) 
	{
		FailIf (NULL == (thisStream = OSDynamicCast (AUAStreamDictionary, streamDictionaries->getObject (streamIndex))), Exit);
		FailIf (kIOReturnSuccess != thisStream->getInterfaceNumber (&streamInterfaceNum), Exit);
		FailIf (kIOReturnSuccess != thisStream->getAlternateSetting (&streamAltSettingID), Exit);
		
		if	(		(interfaceNum == streamInterfaceNum)
				&&	(altSettingID == streamAltSettingID)) 
		{
			found = true;
		}
//		else
//		{
//			debugIOLog ( "? AUAConfigurationDictionary::getStreamDictionary () - ( %d, %d ) != ( %d, %d )", interfaceNum, altSettingID, streamInterfaceNum, streamAltSettingID );
//		}
		streamIndex++;
	}

Exit:
	if (found != true)
	{
		thisStream = NULL;
	}
    return thisStream;
}

AUAControlDictionary * AUAConfigurationDictionary::getControlDictionary (UInt8 interfaceNum, UInt8 altSettingID) 
{
    AUAControlDictionary * 		thisControl = NULL;
	OSObject *					dictionaryValue = NULL;
	OSArray *					controlDictionaries = NULL;
    UInt8						controlIndex = 0;
	UInt8						controlInterfaceNum;
	UInt8						controlAltSettingID;
    bool						found = false;

    
	FailIf (NULL == (dictionaryValue = getObject (kControlDictionaries)), Exit);
	FailIf (NULL == (controlDictionaries = OSDynamicCast (OSArray, dictionaryValue)), Exit);

	while (!found && controlIndex < controlDictionaries->getCount ()) 
	{
		FailIf (NULL == (thisControl = OSDynamicCast (AUAControlDictionary, controlDictionaries->getObject (controlIndex))), Exit);
		FailIf (kIOReturnSuccess != thisControl->getInterfaceNumber (&controlInterfaceNum), Exit);
		FailIf (kIOReturnSuccess != thisControl->getAlternateSetting (&controlAltSettingID), Exit);
		if	(		(interfaceNum == controlInterfaceNum)
				&&	(altSettingID == controlAltSettingID)) 
		{
			found = true;
		}
		controlIndex++;
	}

Exit:
	if (found != true)
	{
		thisControl = NULL;
	}
    return thisControl;
}

IOReturn AUAConfigurationDictionary::parseConfigurationDescriptor (IOUSBConfigurationDescriptor * configurationDescriptor) 
{
	IOReturn								result = kIOReturnError;
    USBInterfaceDescriptorPtr				theInterfacePtr = NULL;
	USBInterfaceAssociationDescriptorPtr	theInterfaceAssociationPtr = NULL;
    AUAControlDictionary *					controlDictionary = NULL;
    AUAStreamDictionary *					streamDictionary = NULL;
	OSArray *								streamInterfaceNumbers = NULL;
	OSArray *								controlDictionaries = NULL;
	OSArray *								streamDictionaries = NULL;
	OSObject *								arrayObject = NULL;
	OSNumber *								streamInterfaceNumber = NULL;
	UInt8									controlInterfaceNum;
	UInt8									thisInterfaceNumber;
	UInt8									lastInterfaceNumber;
	UInt8									numStreamInterfaces;
    UInt8									interfaceClass;
    UInt8									interfaceSubClass;
	UInt8									interfaceProtocol;
	UInt8									numParsedInterfaces;
	UInt8									streamInterfaceIndex;
	// [rdar://5346021] Keep strack of the descriptor length to guard against malformed descriptors.
	UInt16									totalLength;
	UInt16									parsedLength;
	
	bool									haveControlInterface;
	bool									foundStreamInterface;

	debugIOLog ("+ AUAConfigurationDictionary[%p]::parseConfigurationDescriptor (%p)", this, configurationDescriptor);
    FailIf (NULL == configurationDescriptor, Exit);
    FailIf (0 == configurationDescriptor->bLength, Exit);
    FailIf (CONFIGURATION != configurationDescriptor->bDescriptorType, Exit);
	FailIf (kIOReturnSuccess != (result = getControlInterfaceNum (&controlInterfaceNum)), Exit);
	FailIf ( 0 == ( totalLength = USBToHostWord ( configurationDescriptor->wTotalLength ) ), Exit );
	
	theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)configurationDescriptor + configurationDescriptor->bLength);
	// [rdar://5346021] In keeping track of the parsed length, we add the length of the descriptor before actually parsing it. Then we check
	// to ensure that the total length is not exceeded before attempting to parse.
	parsedLength = ( theInterfacePtr ? ( configurationDescriptor->bLength + theInterfacePtr->bLength ) : 0 );
	numParsedInterfaces = 0;
	numStreamInterfaces = 0;
	lastInterfaceNumber = 0;
	haveControlInterface = false;
	foundStreamInterface = false;
	
    while	(		( theInterfacePtr )
				&&	( 0 != theInterfacePtr->bLength )
				&&  ( parsedLength <= totalLength ) ) 
	{
		logDescriptor ((UInt8 *) theInterfacePtr, theInterfacePtr->bLength);
    	if (INTERFACE_ASSOCIATION == theInterfacePtr->bDescriptorType)
		{
			debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - @ INTERFACE_ASSOCIATION (4.6)", this);
				
			if ((((USBInterfaceAssociationDescriptorPtr)theInterfacePtr)->bFunctionClass == USBAUDIO_0200::AUDIO_FUNCTION) &&
			    (((USBInterfaceAssociationDescriptorPtr)theInterfacePtr)->bFunctionSubClass == USBAUDIO_0200::FUNCTION_SUBCLASS_UNDEFINED) &&
				(((USBInterfaceAssociationDescriptorPtr)theInterfacePtr)->bFunctionProtocol == USBAUDIO_0200::AF_VERSION_02_00))
			{
				debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - interfaceClass = first interface = %d, interface count = %d", this, ((USBInterfaceAssociationDescriptorPtr)theInterfacePtr)->bFirstInterface, ((USBInterfaceAssociationDescriptorPtr)theInterfacePtr)->bInterfaceCount);
				
				theInterfaceAssociationPtr = (USBInterfaceAssociationDescriptorPtr)theInterfacePtr;
			}
			
			theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
			parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );			
		} // if (INTERFACE_ASSOCIATION == (theInterfacePtr)->bDescriptorType)
	    else 
		if (INTERFACE == ((ACInterfaceDescriptorPtr)theInterfacePtr)->bDescriptorType) 
		{
			debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - @ INTERFACE (4.3.1/4.5.1)", this);
			thisInterfaceNumber = ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceNumber;
			if (AUDIO == ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceClass) 
			{
				theInterfacePtr = parseInterfaceDescriptor (theInterfacePtr, &interfaceClass, &interfaceSubClass, &interfaceProtocol);
				parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
				debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - controlInterfaceNum = %d, thisInterfaceNumber = %d", this, controlInterfaceNum, thisInterfaceNumber);
				if	(		(AUDIOCONTROL == interfaceSubClass)
						&&	(controlInterfaceNum == thisInterfaceNumber)) 
				{
					debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Found an AUDIOCONTROL CS_INTERFACE (4.3.2)", this);
					if (NULL != (controlDictionaries = getControlDictionaries())) 
					{
						controlDictionary = OSDynamicCast (AUAControlDictionary, controlDictionaries->getLastObject ());
					}
					FailIf (NULL == controlDictionary, Exit);
					// Safeguard against USB-Audio 2.0 descriptors for rdar://4798933
					if (INTERFACE_PROTOCOL_UNDEFINED == interfaceProtocol)
					{
						UInt8 numEndpoints;	// <rdar://problem/6021475>
						theInterfacePtr = controlDictionary->parseACInterfaceDescriptor (theInterfacePtr, ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceNumber, &parsedLength, totalLength);
						FailIf (kIOReturnSuccess != (result = getControlledStreamNumbers (&streamInterfaceNumbers, &numStreamInterfaces)), Exit);
						haveControlInterface = true;

						// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
						debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Finished parsing AC Interface Descriptor", this);
						if ( kIOReturnSuccess == controlDictionary->getNumEndpoints ( &numEndpoints ) )
						{
							if ( 1 == numEndpoints )
							{
								logDescriptor ( (UInt8 *) theInterfacePtr, theInterfacePtr->bLength );
								theInterfacePtr = controlDictionary->parseACInterruptEndpointDescriptor ( theInterfacePtr );
							}
							else
							{
								debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - numEndpoints: %d", this, numEndpoints);
							}
						}
					}
					else if (IP_VERSION_02_00 == interfaceProtocol)
					{
						UInt8 numEndpoints;	// <rdar://problem/6021475>
						// Parse USB-Audio 2.0 descriptors.
						theInterfacePtr = controlDictionary->parseACInterfaceDescriptor_0200 (theInterfacePtr, ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceNumber, &parsedLength, totalLength);
						// Parse the interface association descriptor to determine the interfaces used by this function.
						controlDictionary->parseInterfaceAssociationDescriptor(theInterfaceAssociationPtr);
						FailIf (kIOReturnSuccess != (result = getControlledStreamNumbers (&streamInterfaceNumbers, &numStreamInterfaces)), Exit);
						haveControlInterface = true;

						// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
						// <rdar://problem/5811247> Clock support
						debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Finished parsing AC Interface Descriptor", this);
						if ( kIOReturnSuccess == controlDictionary->getNumEndpoints ( &numEndpoints ) )
						{
							if ( 1 == numEndpoints )
							{
								logDescriptor ( (UInt8 *) theInterfacePtr, theInterfacePtr->bLength );
								theInterfacePtr = controlDictionary->parseACInterruptEndpointDescriptor ( theInterfacePtr );
							}
							else
							{
								debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - numEndpoints: %d", this, numEndpoints);
							}
						}
					}
					else
					{
						// currently don't support this interface protocol, so abort out of the while loop
						break;
					}
				} 
				else if	(haveControlInterface && AUDIOSTREAMING == interfaceSubClass) 
				{
					debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - @ AUDIOSTREAMING CS_INTERFACE (4.5.2)", this);
					FailIf (kIOReturnSuccess != (result = getControlledStreamNumbers (&streamInterfaceNumbers, &numStreamInterfaces)), Exit);
					for (streamInterfaceIndex = 0; streamInterfaceIndex < numStreamInterfaces; streamInterfaceIndex++) 
					{
						FailIf (NULL == (arrayObject = streamInterfaceNumbers->getObject (streamInterfaceIndex)), Exit);
						FailIf (NULL == (streamInterfaceNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
						debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Comparing thisInterfaceNum = %d with %d", thisInterfaceNumber, streamInterfaceNumber->unsigned8BitValue());
						if (thisInterfaceNumber == streamInterfaceNumber->unsigned8BitValue()) 
						{
							debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Found a AUDIOSTREAMING CS_INTERFACE", this);
							if (NULL != (streamDictionaries = getStreamDictionaries())) 
							{
								streamDictionary = OSDynamicCast (AUAStreamDictionary, streamDictionaries->getLastObject ());
							}
							FailIf (NULL == streamDictionary, Exit);
							if (INTERFACE_PROTOCOL_UNDEFINED == interfaceProtocol)
							{
								theInterfacePtr = streamDictionary->parseASInterfaceDescriptor (theInterfacePtr, ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceNumber, &parsedLength, totalLength);
							}
							else if (IP_VERSION_02_00 == interfaceProtocol)
							{
								theInterfacePtr = streamDictionary->parseASInterfaceDescriptor_0200 (theInterfacePtr, ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceNumber, &parsedLength, totalLength);
							}
							foundStreamInterface = true;
							break;			// Get out of for loop
						}
					}
					if (thisInterfaceNumber != lastInterfaceNumber) 
					{
						lastInterfaceNumber = thisInterfaceNumber;
						numParsedInterfaces++;
						if (numParsedInterfaces > numStreamInterfaces) 
						{
							break;		// Get out of while loop, we've parsed everything associated with this control interface
						}
					}
				} 
				else if (MIDISTREAMING == interfaceSubClass) 
				{
					debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - MIDI, jumping forward %d bytes", this, theInterfacePtr->bLength);
					// We should prune the stream interfaces since this driver does not handle MIDI.
					for (streamInterfaceIndex = 0; streamInterfaceIndex < numStreamInterfaces; streamInterfaceIndex++) 
					{
						FailIf (NULL == (arrayObject = streamInterfaceNumbers->getObject (streamInterfaceIndex)), Exit);
						FailIf (NULL == (streamInterfaceNumber = OSDynamicCast (OSNumber, arrayObject)), Exit);
						if (thisInterfaceNumber == streamInterfaceNumber->unsigned8BitValue()) 
						{
							streamInterfaceNumbers->removeObject (streamInterfaceIndex);
						}
					}
					theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
				} 
				else if (AUDIOCONTROL == interfaceSubClass) 
				{
					if (INTERFACE_PROTOCOL_UNDEFINED == interfaceProtocol)
					{
						debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Found a control interface that we don't care about. Skipping %d bytes ...", 
									this, ((((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength[1] << 8) | (((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength[0])));
						parsedLength = ( theInterfacePtr ? ( parsedLength - theInterfacePtr->bLength + ((((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength[1] << 8) | (((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength[0]))) : totalLength );
						theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + ((((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength[1] << 8) | (((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength[0])));
						parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
					}
					else if (IP_VERSION_02_00 == interfaceProtocol)
					{
						debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Found a control interface that we don't care about. Skipping %d bytes ...", 
									this, (USBToHostWord(((USBAUDIO_0200::ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength)));
						parsedLength = ( theInterfacePtr ? ( parsedLength - theInterfacePtr->bLength + (USBToHostWord(((USBAUDIO_0200::ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength))) : totalLength );
						theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + (USBToHostWord(((USBAUDIO_0200::ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->wTotalLength)));
						parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
					}
					else
					{
						// currently don't support this interface protocol, so abort out of the while loop
						break;
					}
				} 
				else 
				{
					debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Unknown, skipping %d bytes", this, theInterfacePtr->bLength);
					theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
				}
			} // if (AUDIO == ((ACInterfaceDescriptorPtr)theInterfacePtr)->bInterfaceClass)
			else 
			{
				debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Not an audio interface, skipping %d bytes", this, theInterfacePtr->bLength);
				theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
				parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
			}
		} // if (INTERFACE == ((ACInterfaceDescriptorPtr)theInterfacePtr)->bDescriptorType)
		else 
		{
			debugIOLog ("? AUAConfigurationDictionary[%p]::parseConfigurationDescriptor () - Default, skipping %d bytes", this, theInterfacePtr->bLength);
			theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
			parsedLength = ( theInterfacePtr ? ( parsedLength + theInterfacePtr->bLength) : totalLength );
        }
    } //     while (theInterfacePtr && 0 != theInterfacePtr->bLength) 

	if ( parsedLength > totalLength )
	{
		IOLog ( "AppleUSBAudio encountered an invalid descriptor on an attached USB audio device. The device may not function properly.\n" );
		debugIOLog ( "! AUAConfigurationDictionary::parseConfigurationDescriptor () - Encountered a bad descriptor. Halting the parser ..." );
	}

	if (controlDictionary && false == foundStreamInterface) 
	{
		controlDictionaries->removeObject (controlDictionaries->getCount () - 1);
	}

	result = kIOReturnSuccess;
Exit:
    return result;
}

USBInterfaceDescriptorPtr AUAConfigurationDictionary::parseInterfaceDescriptor (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 * interfaceClass, UInt8 * interfaceSubClass, UInt8 * interfaceProtocol) 
{
	AUAControlDictionary *			controlDictionary = NULL;
	AUAStreamDictionary *			streamDictionary = NULL;
	OSArray *						controlDictionaries = NULL;
	OSArray *						streamDictionaries = NULL;
	UInt8							controlInterfaceNum;

	debugIOLog ("+ AUAConfigurationDictionary[%p]::parseInterfaceDescriptor (%p, %p, %p)", this, theInterfacePtr, interfaceClass, interfaceSubClass);

	FailIf (NULL == theInterfacePtr, Exit);
	FailIf (0 == theInterfacePtr->bLength, Exit);
	FailIf (kIOReturnSuccess != getControlInterfaceNum (&controlInterfaceNum), Exit);

	if (NULL != interfaceClass)
	{
		*interfaceClass = theInterfacePtr->bInterfaceClass;
	}
	if (NULL != interfaceSubClass)
	{
		*interfaceSubClass = theInterfacePtr->bInterfaceSubClass;
	}
	if (NULL != interfaceProtocol)
	{
		*interfaceProtocol = theInterfacePtr->bInterfaceProtocol;
	}

	if (AUDIOCONTROL == theInterfacePtr->bInterfaceSubClass) 
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::parseInterfaceDescriptor () - Found an AUDIOCONTROL interface", this);
		if (controlInterfaceNum == theInterfacePtr->bDescriptorSubtype) 
		{
			controlDictionary = AUAControlDictionary::create ();

			FailIf (NULL == controlDictionary, Exit);
			controlDictionary->setInterfaceNumber (theInterfacePtr->bDescriptorSubtype);
			controlDictionary->setAlternateSetting (theInterfacePtr->bAlternateSetting);
			controlDictionary->setNumEndpoints (theInterfacePtr->bNumEndpoints);
			controlDictionary->setInterfaceClass (theInterfacePtr->bInterfaceClass);
			controlDictionary->setInterfaceSubClass (theInterfacePtr->bInterfaceSubClass);
			controlDictionary->setInterfaceProtocol (theInterfacePtr->bInterfaceProtocol);

			controlDictionaries = getControlDictionaries ();
			if (NULL == controlDictionaries) 
			{
				controlDictionaries = OSArray::withObjects ((const OSObject **)&controlDictionary, 1);
				FailIf (NULL == controlDictionaries, Exit);
				FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlDictionaries, controlDictionaries), Exit);
				controlDictionaries = NULL;
			} 
			else 
			{
				controlDictionaries->setObject (controlDictionary);
			}

			controlDictionary->release ();
			controlDictionary = NULL;
		}
	} 
	else if (AUDIOSTREAMING == theInterfacePtr->bInterfaceSubClass) 
	{
		debugIOLog ("? AUAConfigurationDictionary[%p]::parseInterfaceDescriptor () - Found an AUDIOSTREAMING interface");
		streamDictionary = AUAStreamDictionary::create ();

		FailIf (NULL == streamDictionary, Exit);
		streamDictionary->setInterfaceNumber (theInterfacePtr->bDescriptorSubtype);
		streamDictionary->setAlternateSetting (theInterfacePtr->bAlternateSetting);
		streamDictionary->setNumEndpoints (theInterfacePtr->bNumEndpoints);
		streamDictionary->setInterfaceClass (theInterfacePtr->bInterfaceClass);
		streamDictionary->setInterfaceSubClass (theInterfacePtr->bInterfaceSubClass);
		streamDictionary->setInterfaceProtocol (theInterfacePtr->bInterfaceProtocol);

		streamDictionaries = getStreamDictionaries ();
		if (NULL == streamDictionaries) 
		{
			streamDictionaries = OSArray::withObjects ((const OSObject **)&streamDictionary, 1);
			FailIf (NULL == streamDictionaries, Exit);
			FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kStreamDictionaries, streamDictionaries), Exit);
			streamDictionaries = NULL;
		} 
		else 
		{
			streamDictionaries->setObject (streamDictionary);
		}

		streamDictionary->release ();
		streamDictionary = NULL;
	}

	theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);

Exit:
	debugIOLog("- AUAConfigurationDictionary[%p]::parseInterfaceDescriptor () = %p", this, theInterfacePtr);
	return theInterfacePtr;
}

#pragma mark AUAControlDictionary

// AUAControlDictionary

//	Public methods

OSDefineMetaClassAndStructors (AUAControlDictionary, AppleUSBAudioDictionary);

AUAControlDictionary * AUAControlDictionary::create (void) 
{
    AUAControlDictionary *			controlDictionary;

    FailIf (NULL == (controlDictionary = new AUAControlDictionary), Exit);
	RELEASE_IF_FALSE (controlDictionary, controlDictionary->initDictionaryForUse ());
	
Exit:
    return controlDictionary;
}

IOReturn AUAControlDictionary::getNumControls (UInt8 * numControls, UInt8 featureUnitID) 
{
	AUAFeatureUnitDictionary *		featureUnitDictionary = NULL;
    IOReturn						result = kIOReturnError;

	* numControls = 0;
	FailIf (NULL == (featureUnitDictionary = getFeatureUnitDictionary (featureUnitID)), Exit);
	FailIf (kIOReturnSuccess != (result = featureUnitDictionary->getNumControls (numControls)), Exit);

Exit:
    return result;
}

// Channel #1 is front left channel, #2 is front right channel
bool AUAControlDictionary::channelHasMuteControl (UInt8 featureUnitID, UInt8 channelNum) 
{
	AUAFeatureUnitDictionary *			featureUnitDictionary = NULL;
	bool								result = false;

	FailIf (NULL == (featureUnitDictionary = getFeatureUnitDictionary (featureUnitID)), Exit);
	result = featureUnitDictionary->channelHasMuteControl (channelNum);

Exit:
	return result;
}

bool AUAControlDictionary::channelHasVolumeControl (UInt8 featureUnitID, UInt8 channelNum) 
{
	AUAFeatureUnitDictionary *			featureUnitDictionary;
	bool								result;

	result = false;
	FailIf (NULL == (featureUnitDictionary = getFeatureUnitDictionary (featureUnitID)), Exit);
	result = featureUnitDictionary->channelHasVolumeControl (channelNum);
	
Exit:
	return result;
}

bool AUAControlDictionary::clockSourceHasFrequencyControl ( UInt8 clockSourceID, bool isProgrammable )
{
	AUAClockSourceDictionary *			clockSourceDictionary;
	UInt8								controls;
	bool								result = false;
	bool								isControlPresent = false;
	bool								isControlProgrammable = false;
	
	FailIf ( NULL == ( clockSourceDictionary = getClockSourceDictionary ( clockSourceID ) ), Exit );
	FailIf ( kIOReturnSuccess != clockSourceDictionary->getControlBitmap ( &controls ), Exit );
	
	// D1..0:	Clock Frequency Control
	// D3..2:	Clock Validity Control
	// D7..4:	Reserved. Must be set to 0.
	
	isControlPresent = ( controls & 0x1 ) ? true : false;
	isControlProgrammable = ( controls & 0x2 ) ? true : false;

	if ( isProgrammable )
	{
		result = isControlPresent && isControlProgrammable;
	}
	else
	{
		result = isControlPresent;
	}
	
Exit:
	return result;
}

// <rdar://7446555>
bool AUAControlDictionary::clockSourceHasValidityControl ( UInt8 clockSourceID )
{
	AUAClockSourceDictionary *			clockSourceDictionary;
	UInt8								controls;
	bool								result = false;
	bool								isControlPresent = false;
	
	FailIf ( NULL == ( clockSourceDictionary = getClockSourceDictionary ( clockSourceID ) ), Exit );
	FailIf ( kIOReturnSuccess != clockSourceDictionary->getControlBitmap ( &controls ), Exit );
	
	// D1..0:	Clock Frequency Control
	// D3..2:	Clock Validity Control
	// D7..4:	Reserved. Must be set to 0.
	
	isControlPresent = ( controls & 0x4 ) ? true : false;
	
	result = isControlPresent;
	
Exit:
	return result;
}

AUAUnitDictionary * AUAControlDictionary::getUnitDictionary (UInt8 unitID) 
{
    AUAUnitDictionary * 			unitDictionary = NULL;

	unitDictionary = getInputTerminalDictionary (unitID);
	if (!unitDictionary)
	{
		unitDictionary = getOutputTerminalDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getMixerUnitDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getSelectorUnitDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getFeatureUnitDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getEffectUnitDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getProcessingUnitDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getExtensionUnitDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getClockSourceDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getClockSelectorDictionary (unitID);
	}
	if (!unitDictionary)
	{
		unitDictionary = getClockMultiplierDictionary (unitID);
	}

    return unitDictionary;
}

IOReturn AUAControlDictionary::getFeatureSourceID (UInt8 * sourceID, UInt8 featureUnitID) 
{
	AUAFeatureUnitDictionary *		featureUnitDictionary = NULL;
	IOReturn						result = kIOReturnError;

	* sourceID = 0;
	FailIf (NULL == (featureUnitDictionary = getFeatureUnitDictionary (featureUnitID)), Exit);
	FailIf (kIOReturnSuccess != (result = featureUnitDictionary->getSourceID (sourceID)), Exit);
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedFeatureUnitID (UInt8 * featureUnitID, UInt8 featureUnitIndex) 
{
	AUAFeatureUnitDictionary *		featureUnitDictionary = NULL;
	IOReturn						result = kIOReturnError;

	* featureUnitID = 0;
	FailIf (NULL == (featureUnitDictionary = getIndexedFeatureUnitDictionary (featureUnitIndex)), Exit);
	FailIf (kIOReturnSuccess != (result = featureUnitDictionary->getUnitID (featureUnitID)), Exit);

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedMixerUnitID (UInt8 * mixerUnitID, UInt8 mixerUnitIndex) 
{
	AUAMixerUnitDictionary *		mixerUnitDictionary = NULL;
	IOReturn						result = kIOReturnError;

	* mixerUnitID = 0;
	FailIf (NULL == (mixerUnitDictionary = getIndexedMixerUnitDictionary (mixerUnitIndex)), Exit);
	FailIf (kIOReturnSuccess != (result = mixerUnitDictionary->getUnitID (mixerUnitID)), Exit);

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedSelectorUnitID (UInt8 * selectorUnitID, UInt8 selectorUnitIndex) 
{
	AUASelectorUnitDictionary *	selectorUnitDictionary = NULL;
	IOReturn					result = kIOReturnError;

	* selectorUnitID = 0;
	FailIf (NULL == (selectorUnitDictionary = getIndexedSelectorUnitDictionary (selectorUnitIndex)), Exit);
	FailIf (kIOReturnSuccess != (result = selectorUnitDictionary->getUnitID (selectorUnitID)), Exit);

Exit:
	return result;
}

IOReturn AUAControlDictionary::getFeatureUnitIDConnectedToOutputTerminal (UInt8 * featureUnitID, UInt8 outputTerminalID) 
{
    AUAOutputTerminalDictionary *	outputTerminalDictionary = NULL;
	AUAFeatureUnitDictionary *		featureUnitDictionary = NULL;
	UInt8							outputTerminalSourceID;
	IOReturn						result = kIOReturnError;

	outputTerminalSourceID = 0;
	* featureUnitID = 0;
	FailIf (NULL == (outputTerminalDictionary = getOutputTerminalDictionary (outputTerminalID)), Exit);
    FailIf (kIOReturnSuccess != (result = outputTerminalDictionary->getSourceID (&outputTerminalSourceID)), Exit);

	if (0 != outputTerminalSourceID) 
	{
		if (NULL == (featureUnitDictionary = getFeatureUnitDictionary (outputTerminalSourceID)))
		{
			* featureUnitID = outputTerminalSourceID;
		}
		else 
		{
			// have to keep looking upstream of whatever this object is
		}
	}
	result = kIOReturnSuccess;
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedInputTerminalType (UInt16 * terminalType, UInt8 index) 
{
    AUAInputTerminalDictionary *		inputTerminalDictionary = NULL;
	IOReturn							result = kIOReturnError;
	OSArray *							inputTerminals = NULL;

    * terminalType =  USBToHostWord (INPUT_UNDEFINED);
	if (NULL != (inputTerminals = getInputTerminals ()))
	{
		FailIf (NULL == (inputTerminalDictionary = OSDynamicCast (AUAInputTerminalDictionary, inputTerminals->getObject (index))), Exit);
		FailIf (kIOReturnSuccess != (result = inputTerminalDictionary->getDictionaryValue (kTerminalType, terminalType)), Exit);
	}

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedInputTerminalID (UInt8 * inputTerminalID, UInt8 index) 
{
    AUAInputTerminalDictionary *		inputTerminalDictionary = NULL;
	IOReturn							result = kIOReturnError;
	OSArray *							inputTerminals = NULL;


    * inputTerminalID = 0;
	if (NULL != (inputTerminals = getInputTerminals ()))
	{
		FailIf (NULL == (inputTerminalDictionary = OSDynamicCast (AUAInputTerminalDictionary, inputTerminals->getObject (index))), Exit);
		FailIf (kIOReturnSuccess != (result = inputTerminalDictionary->getUnitID (inputTerminalID)), Exit);
	}

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedOutputTerminalID (UInt8 * outputTerminalID, UInt8 index) 
{
    AUAOutputTerminalDictionary *		outputTerminalDictionary = NULL;
	IOReturn							result = kIOReturnError;
	OSArray *							outputTerminals = NULL;


    * outputTerminalID = 0;
	if (NULL != (outputTerminals = getOutputTerminals ()))
	{
		FailIf (NULL == (outputTerminalDictionary = OSDynamicCast (AUAOutputTerminalDictionary, outputTerminals->getObject (index))), Exit);
		FailIf (kIOReturnSuccess != (result = outputTerminalDictionary->getUnitID (outputTerminalID)), Exit);
	}
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedOutputTerminalType (UInt16 * terminalType, UInt8 index) 
{
    AUAOutputTerminalDictionary *		outputTerminalDictionary = NULL;
	IOReturn							result = kIOReturnError;
	OSArray *							outputTerminals = NULL;

    * terminalType = USBToHostWord (OUTPUT_UNDEFINED);
	if (NULL != (outputTerminals = getOutputTerminals ()))
	{
		FailIf (NULL == (outputTerminalDictionary = OSDynamicCast (AUAOutputTerminalDictionary, outputTerminals->getObject (index))), Exit);
		FailIf (kIOReturnSuccess != (result = outputTerminalDictionary->getDictionaryValue (kTerminalType, terminalType)), Exit);
	}

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedClockSourceID (UInt8 * clockSourceID, UInt8 clockSourceIndex) 
{
	AUAClockSourceDictionary *	clockSourceDictionary = NULL;
	IOReturn					result = kIOReturnError;

	* clockSourceID = 0;
	FailIf (NULL == (clockSourceDictionary = getIndexedClockSourceDictionary (clockSourceIndex)), Exit);
	FailIf (kIOReturnSuccess != (result = clockSourceDictionary->getUnitID (clockSourceID)), Exit);

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedClockSelectorID (UInt8 * clockSelectorID, UInt8 clockSelectorIndex) 
{
	AUAClockSelectorDictionary *	clockSelectorDictionary = NULL;
	IOReturn						result = kIOReturnError;

	* clockSelectorID = 0;
	FailIf (NULL == (clockSelectorDictionary = getIndexedClockSelectorDictionary (clockSelectorIndex)), Exit);
	FailIf (kIOReturnSuccess != (result = clockSelectorDictionary->getUnitID (clockSelectorID)), Exit);

Exit:
	return result;
}

IOReturn AUAControlDictionary::getIndexedClockMultiplierID (UInt8 * clockMultiplierID, UInt8 clockMultiplierIndex) 
{
	AUAClockMultiplierDictionary *	clockMultiplierDictionary = NULL;
	IOReturn						result = kIOReturnError;

	* clockMultiplierID = 0;
	FailIf (NULL == (clockMultiplierDictionary = getIndexedClockMultiplierDictionary (clockMultiplierIndex)), Exit);
	FailIf (kIOReturnSuccess != (result = clockMultiplierDictionary->getUnitID (clockMultiplierID)), Exit);

Exit:
	return result;
}

AUAFeatureUnitDictionary * AUAControlDictionary::getIndexedFeatureUnitDictionary (UInt8 index) 
{
	AUAFeatureUnitDictionary *		indexedFeatureUnit = NULL;
	OSArray *						featureUnits = NULL;

	if (NULL != (featureUnits = getFeatureUnits())) 
	{
		indexedFeatureUnit = OSDynamicCast (AUAFeatureUnitDictionary, featureUnits->getObject (index));
	}

	return indexedFeatureUnit;
}

AUAMixerUnitDictionary * AUAControlDictionary::getIndexedMixerUnitDictionary (UInt8 index) 
{
	AUAMixerUnitDictionary *		indexedMixerUnit = NULL;
	OSArray *						mixerUnits = NULL;
	
	if (NULL != (mixerUnits = getMixerUnits())) 
	{
		indexedMixerUnit = OSDynamicCast (AUAMixerUnitDictionary, mixerUnits->getObject (index));
	}

	return indexedMixerUnit;
}

AUASelectorUnitDictionary * AUAControlDictionary::getIndexedSelectorUnitDictionary (UInt8 index) 
{
	AUASelectorUnitDictionary *		indexedSelectorUnit = NULL;
	OSArray *						selectorUnits = NULL;

	if (NULL != (selectorUnits = getSelectorUnits ())) 
	{
		indexedSelectorUnit = OSDynamicCast (AUASelectorUnitDictionary, selectorUnits->getObject (index));
	}

	return indexedSelectorUnit;
}

AUAClockSourceDictionary * AUAControlDictionary::getIndexedClockSourceDictionary (UInt8 index) 
{
	AUAClockSourceDictionary *		indexedClockSource = NULL;
	OSArray *						clockSources = NULL;

	if (NULL != (clockSources = getClockSources ())) 
	{
		indexedClockSource = OSDynamicCast (AUAClockSourceDictionary, clockSources->getObject (index));
	}

	return indexedClockSource;
}

AUAClockSelectorDictionary * AUAControlDictionary::getIndexedClockSelectorDictionary (UInt8 index) 
{
	AUAClockSelectorDictionary *	indexedClockSelector = NULL;
	OSArray *						clockSelectors = NULL;

	if (NULL != (clockSelectors = getClockSelectors ())) 
	{
		indexedClockSelector = OSDynamicCast (AUAClockSelectorDictionary, clockSelectors->getObject (index));
	}

	return indexedClockSelector;
}

AUAClockMultiplierDictionary * AUAControlDictionary::getIndexedClockMultiplierDictionary (UInt8 index) 
{
	AUAClockMultiplierDictionary *	indexedClockMultiplier = NULL;
	OSArray *						clockMultipliers = NULL;

	if (NULL != (clockMultipliers = getClockMultipliers ())) 
	{
		indexedClockMultiplier = OSDynamicCast (AUAClockMultiplierDictionary, clockMultipliers->getObject (index));
	}

	return indexedClockMultiplier;
}

AUAFeatureUnitDictionary * AUAControlDictionary::getFeatureUnitDictionary (UInt8 unitID) 
{
    AUAFeatureUnitDictionary *	 		featureUnitDictionary = NULL;
	OSArray *							featureUnits = NULL;
    UInt8								featureUnitIndex;
	UInt8								thisUnitID;
    bool								found = false;

    featureUnitIndex = 0;   
	if (NULL != (featureUnits = getFeatureUnits())) 
	{
		while (!found && featureUnitIndex < featureUnits->getCount ()) 
		{
			featureUnitDictionary = OSDynamicCast (AUAFeatureUnitDictionary, featureUnits->getObject (featureUnitIndex));
			FailIf (kIOReturnSuccess != featureUnitDictionary->getUnitID (&thisUnitID), Exit);
			if	(		(featureUnitDictionary)
					&&	(unitID == thisUnitID)) 
			{
				found = true;
			}
			featureUnitIndex++;
		}
	}
	
Exit:
	if (found != true)
	{
		featureUnitDictionary = NULL;
	}
	
	return featureUnitDictionary;
}

AUAInputTerminalDictionary * AUAControlDictionary::getInputTerminalDictionary (UInt8 unitID) 
{
    AUAInputTerminalDictionary * 	inputTerminalDictionary = NULL;
	OSArray *						inputTerminals = NULL;
    UInt8							inputTerminalIndex;
	UInt8							thisUnitID;
    bool							found  = false;

    inputTerminalIndex = 0;
	if (NULL != (inputTerminals = getInputTerminals())) 
	{
		while (!found && inputTerminalIndex < inputTerminals->getCount ()) 
		{
			FailIf (NULL == (inputTerminalDictionary = OSDynamicCast (AUAInputTerminalDictionary, inputTerminals->getObject (inputTerminalIndex))), Exit);
			FailIf (kIOReturnSuccess != inputTerminalDictionary->getUnitID (&thisUnitID), Exit);
			if (unitID == thisUnitID) 
			{
				found = true;
			}
			inputTerminalIndex++;
		}
	}

Exit:
	if (found != true)
	{
		inputTerminalDictionary = NULL;
	}
    return inputTerminalDictionary;
}

AUAOutputTerminalDictionary * AUAControlDictionary::getOutputTerminalDictionary (UInt8 unitID) 
{
    AUAOutputTerminalDictionary * 	outputTerminalDictionary = NULL;
	OSArray *						outputTerminals = NULL;
    UInt8							outputTerminalIndex;
	UInt8							thisUnitID;
    bool							found;

    outputTerminalIndex = 0;
    found = false;
	outputTerminalDictionary = NULL;
	if (NULL != (outputTerminals = getOutputTerminals ())) 
	{
		while (!found && outputTerminalIndex < outputTerminals->getCount ()) 
		{
			FailIf (NULL == (outputTerminalDictionary = OSDynamicCast (AUAOutputTerminalDictionary, outputTerminals->getObject (outputTerminalIndex))), Exit);
			FailIf (kIOReturnSuccess != outputTerminalDictionary->getUnitID (&thisUnitID), Exit);
			if (unitID == thisUnitID) 
			{
				found = true;
			}
			outputTerminalIndex++;
		}
	}

Exit:
	if (found != true)
	{
		outputTerminalDictionary = NULL;
	}
	
    return outputTerminalDictionary;
}

IOReturn AUAControlDictionary::getInputTerminalType (UInt16 * terminalType, UInt8 index) 
{
    AUAInputTerminalDictionary *	inputTerminalDictionary;
    IOReturn						result = kIOReturnError;

    * terminalType = USBToHostWord (INPUT_UNDEFINED);
    FailIf (NULL == (inputTerminalDictionary = getInputTerminalDictionary (index)), Exit);
	FailIf (kIOReturnSuccess != (result = inputTerminalDictionary->getDictionaryValue (kTerminalType, terminalType)), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getNumInputTerminals (UInt8 * numInputTerminals) 
{
	OSArray *		inputTerminals = NULL;
	IOReturn		result = kIOReturnError;
	
	* numInputTerminals = 0;
	
	FailIf (NULL == (inputTerminals = getInputTerminals()), Exit);
	* numInputTerminals = inputTerminals->getCount ();
	result = kIOReturnSuccess;
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getNumOutputTerminals (UInt8 * numOutputTerminals) 
{
	OSArray *		outputTerminals = NULL;
	IOReturn		result = kIOReturnError;
	
	* numOutputTerminals = 0;
	
	FailIf (NULL == (outputTerminals = getOutputTerminals()), Exit);
	* numOutputTerminals = outputTerminals->getCount ();
	result = kIOReturnSuccess;
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getNumSelectorUnits (UInt8 * numSelectorUnits) 
{
	OSArray *		selectorUnits = NULL;
	
	* numSelectorUnits = 0;
	
	selectorUnits = getSelectorUnits();
	if (selectorUnits)
	{
		* numSelectorUnits = selectorUnits->getCount ();
	}
	
	return kIOReturnSuccess;
}

IOReturn AUAControlDictionary::getNumClockSources (UInt8 * numClockSources) 
{
	OSArray *		clockSources = NULL;
	
	* numClockSources = 0;
	
	clockSources = getClockSources();
	if (clockSources)
	{
		* numClockSources = clockSources->getCount ();
	}
	
	return kIOReturnSuccess;
}

IOReturn AUAControlDictionary::getNumClockSelectors (UInt8 * numClockSelectors) 
{
	OSArray *		clockSelectors = NULL;
	
	* numClockSelectors = 0;
	
	clockSelectors = getClockSelectors();
	if (clockSelectors)
	{
		* numClockSelectors = clockSelectors->getCount ();
	}
	
	return kIOReturnSuccess;
}

IOReturn AUAControlDictionary::getNumClockMultipliers (UInt8 * numClockMultipliers) 
{
	OSArray *		clockMultipliers = NULL;
	
	* numClockMultipliers = 0;
	
	clockMultipliers = getClockMultipliers();
	if (clockMultipliers)
	{
		* numClockMultipliers = clockMultipliers->getCount ();
	}
	
	return kIOReturnSuccess;
}

IOReturn AUAControlDictionary::getNumSources (UInt8 * numSources, UInt8 unitID) 
{
    AUAUnitDictionary *		thisUnit = NULL;
	IOReturn				result = kIOReturnError;

    * numSources = 0;
    FailIf (NULL == (thisUnit = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = thisUnit->getNumInPins (numSources)), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getSourceID (UInt8 * sourceID, UInt8 unitID) 
{
	AUAUnitDictionary *		unitDictionary = NULL;
	IOReturn				result = kIOReturnError;

	* sourceID = 0;
	FailIf (NULL == (unitDictionary = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getSourceID (sourceID)), Exit);

Exit:
	return result;
}

IOReturn AUAControlDictionary::getSourceIDs (OSArray ** sourceIDs, UInt8 unitID) 
{
	AUAUnitDictionary *		unitDictionary = NULL;
	IOReturn				result = kIOReturnError;
	UInt8					descriptorSubType;
	UInt16					adcVersion = 0;

	* sourceIDs = NULL;
	FailIf (NULL == (unitDictionary = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = getADCVersion(&adcVersion)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getDescriptorSubType (&descriptorSubType)), Exit);

	if (kAUAUSBSpec1_0 == adcVersion)
	{
		switch (descriptorSubType) 
		{
			case MIXER_UNIT:
				FailIf (kIOReturnSuccess != (result = getMixerSources (sourceIDs, unitID)), Exit);
				break;
			case SELECTOR_UNIT:
				FailIf (kIOReturnSuccess != (result = getSelectorSources (sourceIDs, unitID)), Exit);
				break;
			case PROCESSING_UNIT:
				FailIf (kIOReturnSuccess != (result = getProcessingUnitSources (sourceIDs, unitID)), Exit);
				break;
			case EXTENSION_UNIT:
				FailIf (kIOReturnSuccess != (result = getExtensionUnitSources (sourceIDs, unitID)), Exit);
				break;
			default:
				result = kIOReturnError;
		}
	}
	else if (kAUAUSBSpec2_0 == adcVersion)
	{
		switch (descriptorSubType) 
		{
			case USBAUDIO_0200::MIXER_UNIT:
				FailIf (kIOReturnSuccess != (result = getMixerSources (sourceIDs, unitID)), Exit);
				break;
			case  USBAUDIO_0200::SELECTOR_UNIT:
				FailIf (kIOReturnSuccess != (result = getSelectorSources (sourceIDs, unitID)), Exit);
				break;
			case  USBAUDIO_0200::PROCESSING_UNIT:
				FailIf (kIOReturnSuccess != (result = getProcessingUnitSources (sourceIDs, unitID)), Exit);
				break;
			case USBAUDIO_0200::EXTENSION_UNIT:
				FailIf (kIOReturnSuccess != (result = getExtensionUnitSources (sourceIDs, unitID)), Exit);
				break;
			case USBAUDIO_0200::CLOCK_SELECTOR:
				FailIf ( kIOReturnSuccess != ( result = getClockSelectorSources ( sourceIDs, unitID ) ), Exit );
				break;
			default:
				result = kIOReturnError;
		}
	}
	else
	{
		// not supported
		result = kIOReturnUnsupported;
	}
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getSubType (UInt8 * subType, UInt8 unitID) 
{
	AUAUnitDictionary *		unitDictionary = NULL;
	IOReturn				result = kIOReturnError;

	* subType = 0;
	FailIf (NULL == (unitDictionary = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getDictionaryValue (kSubType, subType)), Exit);
	
Exit:
	return result;
}

OSArray * AUAControlDictionary::getTerminalClockEntities ()
{
	OSArray *						clockEntities = NULL;
	OSArray *						inputTerminals = NULL;
	OSArray *						outputTerminals = NULL;
	OSNumber *						clockIDNum = NULL;
	AUAInputTerminalDictionary *	inputTerminal = NULL;
	AUAOutputTerminalDictionary *	outputTerminal = NULL;
	UInt8							clockID;
	bool							isDuplicate = false;
	
	debugIOLog ( "+ AUAControlDictionary::getTerminalClockEntities ()" );
	FailIf ( NULL == ( inputTerminals = getInputTerminals () ), Exit );
	FailIf ( NULL == ( outputTerminals = getOutputTerminals () ), Exit );
	
	// Comb input terminals for unique clock entities
	for ( UInt8 terminalIndex = 0; terminalIndex < inputTerminals->getCount (); terminalIndex++ )
	{
		isDuplicate = false;
		FailIf ( NULL == ( inputTerminal = OSDynamicCast ( AUAInputTerminalDictionary, inputTerminals->getObject ( terminalIndex ) ) ), Exit );
		FailIf ( kIOReturnSuccess != inputTerminal->getClockSourceID ( &clockID ), Exit );
		for ( UInt8 entityIndex = 0; ( clockEntities && entityIndex < clockEntities->getCount () ); entityIndex++ )
		{
			FailIf ( NULL == ( clockIDNum = OSDynamicCast ( OSNumber, clockEntities->getObject ( entityIndex ) ) ), Exit );
			if ( clockIDNum->unsigned8BitValue () == clockID )
			{
				isDuplicate = true;
				break;
			} 
		}
		if ( ! isDuplicate )
		{
			debugIOLog ( "? AUAControlDictionary::getTerminalClockEntities () - Adding clockID %d ...", clockID ); 
			FailIf ( NULL == ( clockIDNum = OSNumber::withNumber ( clockID, SIZEINBITS( UInt8 ) ) ), Exit );
			if ( NULL == clockEntities )
			{
				FailIf ( NULL == ( clockEntities = OSArray::withObjects ( ( const OSObject ** ) &clockIDNum, 1 ) ), Exit );
			}
			else
			{
				FailIf ( true != clockEntities->setObject ( clockIDNum ), Exit );
			}
			clockIDNum->release ();
			clockIDNum = NULL;
		}
	}

	// Comb output terminals for unique clock entities
	for ( UInt8 terminalIndex = 0; terminalIndex < outputTerminals->getCount (); terminalIndex++ )
	{
		isDuplicate = false;
		FailIf ( NULL == ( outputTerminal = OSDynamicCast ( AUAOutputTerminalDictionary, outputTerminals->getObject ( terminalIndex ) ) ), Exit );
		FailIf ( kIOReturnSuccess != outputTerminal->getClockSourceID ( &clockID ), Exit );
		for ( UInt8 entityIndex = 0; ( clockEntities && entityIndex < clockEntities->getCount () ); entityIndex++ )
		{
			FailIf ( NULL == ( clockIDNum = OSDynamicCast ( OSNumber, clockEntities->getObject ( entityIndex ) ) ), Exit );
			if ( clockIDNum->unsigned8BitValue () == clockID )
			{
				isDuplicate = true;
				break;
			} 
		}
		if ( ! isDuplicate )
		{
			debugIOLog ( "? AUAControlDictionary::getTerminalClockEntities () - Adding clockID %d ...", clockID ); 
			FailIf ( NULL == ( clockIDNum = OSNumber::withNumber ( clockID, SIZEINBITS( UInt8 ) ) ), Exit );
			if ( NULL == clockEntities )
			{
				FailIf ( NULL == ( clockEntities = OSArray::withObjects ( ( const OSObject ** ) &clockIDNum, 1 ) ), Exit );
			}
			else
			{
				FailIf ( kIOReturnSuccess != clockEntities->setObject ( clockIDNum ), Exit );
			}
			clockIDNum->release ();
			clockIDNum = NULL;
		}
	}
Exit:
	debugIOLog ( "- AUAControlDictionary::getTerminalClockEntities () = %p", clockEntities );
	return clockEntities;
}

IOReturn AUAControlDictionary::getClockSourceID (UInt8 * clockSourceID, UInt8 unitID) 
{
	AUAUnitDictionary *		unitDictionary = NULL;
	IOReturn				result = kIOReturnError;

	* clockSourceID = 0;
	FailIf (NULL == (unitDictionary = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getClockSourceID (clockSourceID)), Exit);

Exit:
	return result;
}

// <rdar://6394629>
IOReturn AUAControlDictionary::getStringIndex (UInt8 * stringIndex, UInt8 unitID) 
{
	AUAUnitDictionary *		unitDictionary = NULL;
	IOReturn				result = kIOReturnError;

	* stringIndex = 0;
	FailIf (NULL == (unitDictionary = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getStringIndex (stringIndex)), Exit);

Exit:
	return result;
}

// <rdar://6430836>
IOReturn AUAControlDictionary::getAudioClusterDescriptor (AudioClusterDescriptor * clusterDescriptor, UInt8 unitID) 
{
	AUAUnitDictionary *		unitDictionary = NULL;
	IOReturn				result = kIOReturnError;

	clusterDescriptor->bNrChannels = 0;
	clusterDescriptor->bmChannelConfig = 0;
	clusterDescriptor->iChannelNames = 0;

	FailIf (NULL == (unitDictionary = getUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getNumChannels (&clusterDescriptor->bNrChannels)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getChannelConfig (&clusterDescriptor->bmChannelConfig)), Exit);
	FailIf (kIOReturnSuccess != (result = unitDictionary->getChannelNames (&clusterDescriptor->iChannelNames)), Exit);

Exit:
	return result;
}

AUAEffectUnitDictionary * AUAControlDictionary::getEffectUnitDictionary (UInt8 unitID) 
{
    AUAEffectUnitDictionary *			thisEffectUnit = NULL;
	OSArray *							effectUnits;
    UInt8								effectUnitIndex;
	UInt8								effectUnitID;
    bool								found = false;

    effectUnitIndex = 0;
	effectUnits = getEffectUnits();
		
	while (effectUnits && !found && effectUnitIndex < effectUnits->getCount ()) 
	{
		FailIf (NULL == (thisEffectUnit = OSDynamicCast (AUAEffectUnitDictionary, effectUnits->getObject (effectUnitIndex))), Exit);
		FailIf (kIOReturnSuccess != thisEffectUnit->getUnitID (&effectUnitID), Exit);
		if (unitID == effectUnitID) 
		{
			found = true;
		}
		effectUnitIndex++;
	}

Exit:
	if (found != true)
	{
		thisEffectUnit = NULL;
	}

    return thisEffectUnit;
}

AUAProcessingUnitDictionary * AUAControlDictionary::getProcessingUnitDictionary (UInt8 unitID) 
{
    AUAProcessingUnitDictionary * 		thisProcessingUnit = NULL;
	OSArray *							processingUnits;
    UInt8								processingUnitIndex;
	UInt8								processingUnitID;
    bool								found = false;

    processingUnitIndex = 0;
	processingUnits = getProcessingUnits();
		
	while (processingUnits && !found && processingUnitIndex < processingUnits->getCount ()) 
	{
		FailIf (NULL == (thisProcessingUnit = OSDynamicCast (AUAProcessingUnitDictionary, processingUnits->getObject (processingUnitIndex))), Exit);
		FailIf (kIOReturnSuccess != thisProcessingUnit->getUnitID (&processingUnitID), Exit);
		if (unitID == processingUnitID) 
		{
			found = true;
		}
		processingUnitIndex++;
	}

Exit:
	if (found != true)
	{
		thisProcessingUnit = NULL;
	}

    return thisProcessingUnit;
}

AUAMixerUnitDictionary * AUAControlDictionary::getMixerUnitDictionary (UInt8 unitID) 
{
    AUAMixerUnitDictionary *		 	thisMixerUnit = NULL;
	OSArray *							mixerUnits;
    UInt8								mixerUnitIndex;
	UInt8								mixerUnitID;
    bool								found = false;

    mixerUnitIndex = 0;
	mixerUnits = getMixerUnits ();
	
	while (mixerUnits && !found && mixerUnitIndex < mixerUnits->getCount ()) 
	{
		thisMixerUnit = OSDynamicCast (AUAMixerUnitDictionary, mixerUnits->getObject (mixerUnitIndex));
		FailIf (kIOReturnSuccess != thisMixerUnit->getUnitID (&mixerUnitID), Exit);
		if (unitID == mixerUnitID) 
		{
			found = true;
		}
		mixerUnitIndex++;
	}

Exit:
	if (found != true)
	{
		thisMixerUnit = NULL;
	}
    return thisMixerUnit;
}

AUAExtensionUnitDictionary * AUAControlDictionary::getExtensionUnitDictionary (UInt8 unitID) 
{
    AUAExtensionUnitDictionary *		thisExtensionUnit = NULL;
	OSArray *							extensionUnits;
    UInt8								extensionUnitIndex;
	UInt8								extensionUnitID;
    bool								found = false;

    extensionUnitIndex = 0;
	extensionUnits = getExtensionUnits ();
	
	while (extensionUnits && !found && extensionUnitIndex < extensionUnits->getCount ()) 
	{
		thisExtensionUnit = OSDynamicCast (AUAExtensionUnitDictionary, extensionUnits->getObject (extensionUnitIndex));
		FailIf (kIOReturnSuccess != thisExtensionUnit->getUnitID (&extensionUnitID), Exit);
		if (unitID == extensionUnitID) 
		{
			found = true;
		}
		extensionUnitIndex++;
	}

Exit:
	if (found != true)
	{
		thisExtensionUnit = NULL;
	}
	
    return thisExtensionUnit;
}

AUASelectorUnitDictionary * AUAControlDictionary::getSelectorUnitDictionary (UInt8 unitID) {
    AUASelectorUnitDictionary *		 	thisSelectorUnit = NULL;
	OSArray *							selectorUnits = NULL;
    UInt8								selectorUnitIndex;
	UInt8								selectorUnitID;
    bool								found = false;

    selectorUnitIndex = 0;
    
	selectorUnits = getSelectorUnits ();
	while (selectorUnits && !found && selectorUnitIndex < selectorUnits->getCount ()) 
	{
		FailIf (NULL == (thisSelectorUnit = OSDynamicCast (AUASelectorUnitDictionary, selectorUnits->getObject (selectorUnitIndex))), Exit);
		FailIf (kIOReturnSuccess != thisSelectorUnit->getUnitID (&selectorUnitID), Exit);
		if (unitID == selectorUnitID) 
		{
			found = true;
		}
		selectorUnitIndex++;
	}
	
Exit:
	if (found != true)
	{
		thisSelectorUnit = NULL;
	}
    return thisSelectorUnit;
}

AUAClockSourceDictionary * AUAControlDictionary::getClockSourceDictionary (UInt8 unitID) 
{
    AUAClockSourceDictionary *			thisClockSource = NULL;
	OSArray *							clockSources;
    UInt8								clockSourceIndex;
	UInt8								clockSourceID;
    bool								found = false;

    clockSourceIndex = 0;
	clockSources = getClockSources();
		
	while (clockSources && !found && clockSourceIndex < clockSources->getCount ()) 
	{
		FailIf (NULL == (thisClockSource = OSDynamicCast (AUAClockSourceDictionary, clockSources->getObject (clockSourceIndex))), Exit);
		FailIf (kIOReturnSuccess != thisClockSource->getUnitID (&clockSourceID), Exit);
		if (unitID == clockSourceID) 
		{
			found = true;
		}
		clockSourceIndex++;
	}

Exit:
	if (found != true)
	{
		thisClockSource = NULL;
	}

    return thisClockSource;
}

AUAClockSelectorDictionary * AUAControlDictionary::getClockSelectorDictionary (UInt8 unitID) 
{
    AUAClockSelectorDictionary *		thisClockSelector = NULL;
	OSArray *							clockSelectors;
    UInt8								clockSelectorIndex;
	UInt8								clockSelectorID;
    bool								found = false;

    clockSelectorIndex = 0;
	clockSelectors = getClockSelectors();
		
	while (clockSelectors && !found && clockSelectorIndex < clockSelectors->getCount ()) 
	{
		FailIf (NULL == (thisClockSelector = OSDynamicCast (AUAClockSelectorDictionary, clockSelectors->getObject (clockSelectorIndex))), Exit);
		FailIf (kIOReturnSuccess != thisClockSelector->getUnitID (&clockSelectorID), Exit);
		if (unitID == clockSelectorID) 
		{
			found = true;
		}
		clockSelectorIndex++;
	}

Exit:
	if (found != true)
	{
		thisClockSelector = NULL;
	}

    return thisClockSelector;
}

AUAClockMultiplierDictionary * AUAControlDictionary::getClockMultiplierDictionary (UInt8 unitID) 
{
    AUAClockMultiplierDictionary *		thisClockMultiplier = NULL;
	OSArray *							clockMultipliers;
    UInt8								clockMultiplierIndex;
	UInt8								clockMultiplierID;
    bool								found = false;

    clockMultiplierIndex = 0;
	clockMultipliers = getClockMultipliers();
		
	while (clockMultipliers && !found && clockMultiplierIndex < clockMultipliers->getCount ()) 
	{
		FailIf (NULL == (thisClockMultiplier = OSDynamicCast (AUAClockMultiplierDictionary, clockMultipliers->getObject (clockMultiplierIndex))), Exit);
		FailIf (kIOReturnSuccess != thisClockMultiplier->getUnitID (&clockMultiplierID), Exit);
		if (unitID == clockMultiplierID) 
		{
			found = true;
		}
		clockMultiplierIndex++;
	}

Exit:
	if (found != true)
	{
		thisClockMultiplier = NULL;
	}

    return thisClockMultiplier;
}

IOReturn AUAControlDictionary::getOutputTerminalType (UInt16 * terminalType, UInt8 terminalID) 
{
    AUAOutputTerminalDictionary *	outputTerminalDictionary = NULL;
    IOReturn						result = kIOReturnError;

    * terminalType = USBToHostWord (OUTPUT_UNDEFINED);
    FailIf (NULL == (outputTerminalDictionary = getOutputTerminalDictionary (terminalID)), Exit);
	FailIf (kIOReturnSuccess != (result = outputTerminalDictionary->getDictionaryValue (kTerminalType, terminalType)), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getSelectorSources (OSArray ** selectorSources, UInt8 unitID) 
{
    AUASelectorUnitDictionary *		thisSelectorUnit = NULL;
	IOReturn						result = kIOReturnError;

    * selectorSources = NULL;
    FailIf (NULL == (thisSelectorUnit = getSelectorUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisSelectorUnit->getSources (selectorSources))), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getMixerSources (OSArray ** mixerSources, UInt8 unitID) 
{
    AUAMixerUnitDictionary *		thisMixerUnit = NULL;
	IOReturn						result = kIOReturnError;

    * mixerSources = NULL;
    FailIf (NULL == (thisMixerUnit = getMixerUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisMixerUnit->getSources (mixerSources))), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getExtensionUnitSources (OSArray ** extensionUnitSources, UInt8 unitID) 
{
    AUAExtensionUnitDictionary *	thisExtensionUnit = NULL;
	IOReturn						result = kIOReturnError;

    * extensionUnitSources = NULL;
    FailIf (NULL == (thisExtensionUnit = getExtensionUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisExtensionUnit->getSources (extensionUnitSources))), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getProcessingUnitSources (OSArray ** processingUnitSources, UInt8 unitID) 
{
    AUAProcessingUnitDictionary *	thisProcessingUnit = NULL;
	IOReturn						result = kIOReturnError;

	* processingUnitSources = NULL;
    FailIf (NULL == (thisProcessingUnit = getProcessingUnitDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisProcessingUnit->getSources (processingUnitSources))), Exit);

Exit:
    return result;
}

IOReturn AUAControlDictionary::getClockSelectorSources (OSArray ** clockSelectorSources, UInt8 unitID) 
{
    AUAClockSelectorDictionary *	thisClockSelector = NULL;
	IOReturn						result = kIOReturnError;

    * clockSelectorSources = NULL;
    FailIf (NULL == (thisClockSelector = getClockSelectorDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisClockSelector->getClockSources (clockSelectorSources))), Exit);

Exit:
    return result;
}

//	<rdar://5811247>
IOReturn AUAControlDictionary::getClockSourceClockType (UInt8 * clockType, UInt8 unitID)
{
    AUAClockSourceDictionary *		thisClockSource = NULL;
	IOReturn						result = kIOReturnError;

    * clockType = 0;
    FailIf (NULL == (thisClockSource = getClockSourceDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisClockSource->getClockType (clockType))), Exit);

Exit:
    return result;
}

//	<rdar://5811247>
IOReturn AUAControlDictionary::getClockSourceAssocTerminal (UInt8 * assocTerminal, UInt8 unitID)
{
    AUAClockSourceDictionary *		thisClockSource = NULL;
	IOReturn						result = kIOReturnError;

    * assocTerminal = 0;
    FailIf (NULL == (thisClockSource = getClockSourceDictionary (unitID)), Exit);
	FailIf (kIOReturnSuccess != (result = (thisClockSource->getAssocTerminal (assocTerminal))), Exit);

Exit:
    return result;
}

bool AUAControlDictionary::masterHasMuteControl (UInt8 featureUnitID) 
{
	AUAFeatureUnitDictionary *		featureUnitDictionary = NULL;
	bool							result = false;

	FailIf (NULL == (featureUnitDictionary = getFeatureUnitDictionary (featureUnitID)), Exit);
	result = featureUnitDictionary->masterHasMuteControl ();

Exit:
	return result;
}

USBInterfaceDescriptorPtr AUAControlDictionary::parseACInterfaceDescriptor (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength) 
{
	OSArray *						inputTerminals = NULL;
	OSArray *						outputTerminals = NULL;
	OSArray *						featureUnits = NULL;
	OSArray *						mixerUnits = NULL;
	OSArray *						selectorUnits = NULL;
	OSArray *						processingUnits = NULL;
	OSArray *						extensionUnits = NULL;
	AUAInputTerminalDictionary *	inputTerminal = NULL;
	AUAOutputTerminalDictionary *	outputTerminal = NULL;
	AUAFeatureUnitDictionary *		featureUnit = NULL;
	AUAMixerUnitDictionary *		mixerUnit = NULL;
	AUASelectorUnitDictionary *		selectorUnit = NULL;
	AUAProcessingUnitDictionary *	processingUnit = NULL;
	AUAExtensionUnitDictionary *	extensionUnit = NULL;
	OSNumber *						dictionaryNumber = NULL;
	OSArray *						streamInterfaceNumbers = NULL;
	UInt8 *							streamInterfacesPtr = NULL;
	UInt16							adcVersion;
	UInt8							numStreamInterfaces;
	UInt8							index;

	debugIOLog ("+ AUAControlDictionary[%p]::parseACInterfaceDescriptor (%p, %d)", this, theInterfacePtr, currentInterface);

    FailIf (NULL == theInterfacePtr, Exit);
    FailIf (0 == theInterfacePtr->bLength, Exit);
    FailIf (CS_INTERFACE != theInterfacePtr->bDescriptorType, Exit);

    while	(		( theInterfacePtr->bLength > 0 )
				&&	( CS_INTERFACE == theInterfacePtr->bDescriptorType )
				&&	( * parsedLength <= totalLength ) )
	{
		logDescriptor ((UInt8 *) theInterfacePtr, theInterfacePtr->bLength);
        switch (theInterfacePtr->bDescriptorSubtype) 
		{
            case HEADER:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ HEADER (4.3.2)", this);
				adcVersion = USBToHostWord (* (UInt16 *) (&(((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->bcdADC[0])));
				if (adcVersion != kAUAUSBSpec1_0)
				{
					debugIOLog ("! AUAControlDictionary[%p]::parseACInterfaceDescriptor () - Encountered a descriptor version (0x%x) that AppleUSBAudio currently can't handle.", this, adcVersion);
				}
                
				// store adcVersion
				FailIf (kIOReturnSuccess != setDictionaryValue (kADCVersion, adcVersion), Exit);
				
				// Store the number of stream interfaces
				numStreamInterfaces = ((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->bInCollection;
				debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - numStreamInterfaces = %d", this, numStreamInterfaces);
				FailIf (kIOReturnSuccess != setDictionaryValue (kNumStreamInterfaces, numStreamInterfaces), Exit);
				
				// Create and store stream interface number array
				FailIf (NULL == (streamInterfaceNumbers = OSArray::withCapacity (numStreamInterfaces)), Exit);
				FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kStreamInterfaceNumbers, streamInterfaceNumbers), Exit);
				FailIf (NULL == (streamInterfaceNumbers = getStreamInterfaceNumbers ()), Exit);
				
				streamInterfacesPtr = ((ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->baInterfaceNr;
				debugIOLog ("    they are: ");
				for (index = 0; index < numStreamInterfaces; index++) 
				{
					debugIOLog ("      %d ", streamInterfacesPtr[index]);
					// Add this streamInterfaceNumber to the OSArray.
					FailIf (NULL == (dictionaryNumber = OSNumber::withNumber (streamInterfacesPtr[index], SIZEINBITS(UInt8))), Exit);
					streamInterfaceNumbers->setObject (dictionaryNumber);
					dictionaryNumber->release ();
					dictionaryNumber = NULL;
				}
				debugIOLog ("");
                break;
            case INPUT_TERMINAL:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ INPUT_TERMINAL (4.3.2.1)", this);
				FailIf (NULL == (inputTerminal = new AUAInputTerminalDictionary), Exit);
				RELEASE_IF_FALSE (inputTerminal, inputTerminal->initDictionaryForUse());
				FailIf (NULL == inputTerminal, Exit);
				
				inputTerminal->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                inputTerminal->setUnitID (((ACInputTerminalDescriptorPtr)theInterfacePtr)->bTerminalID);
                inputTerminal->setTerminalType (USBToHostWord (((ACInputTerminalDescriptorPtr)theInterfacePtr)->wTerminalType));
                inputTerminal->setAssocTerminal (((ACInputTerminalDescriptorPtr)theInterfacePtr)->bAssocTerminal);
                inputTerminal->setNumChannels (((ACInputTerminalDescriptorPtr)theInterfacePtr)->bNrChannels);
                inputTerminal->setChannelConfig (USBToHostWord (((ACInputTerminalDescriptorPtr)theInterfacePtr)->wChannelConfig));
                inputTerminal->setStringIndex (((ACInputTerminalDescriptorPtr)theInterfacePtr)->iTerminal);	// <rdar://6394629>
				inputTerminal->setChannelNames (((ACInputTerminalDescriptorPtr)theInterfacePtr)->iChannelNames);	// <rdar://6430836>

				inputTerminals = getInputTerminals ();
				if (NULL == inputTerminals) 
				{
					FailIf (NULL == (inputTerminals = OSArray::withObjects ((const OSObject **)&inputTerminal, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kInputTerminals, inputTerminals), Exit);
					inputTerminals = NULL;
				} 
				else 
				{
					inputTerminals->setObject (inputTerminal);
				}

				inputTerminal->release ();
				inputTerminal = NULL;
                break;
            case OUTPUT_TERMINAL:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ OUTPUT_TERMINAL (4.3.2.2)", this);
				FailIf (NULL == (outputTerminal = new AUAOutputTerminalDictionary), Exit);
				RELEASE_IF_FALSE (outputTerminal, outputTerminal->initDictionaryForUse());
				FailIf (NULL == outputTerminal, Exit);
				
				outputTerminal->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                outputTerminal->setUnitID (((ACOutputTerminalDescriptorPtr)theInterfacePtr)->bTerminalID);
                outputTerminal->setTerminalType (USBToHostWord (((ACOutputTerminalDescriptorPtr)theInterfacePtr)->wTerminalType));
                outputTerminal->setAssocTerminal (((ACOutputTerminalDescriptorPtr)theInterfacePtr)->bAssocTerminal);
                outputTerminal->setSourceID (((ACOutputTerminalDescriptorPtr)theInterfacePtr)->bSourceID);
                outputTerminal->setStringIndex (((ACOutputTerminalDescriptorPtr)theInterfacePtr)->iTerminal);	// <rdar://6394629>

				outputTerminals = getOutputTerminals ();
				if (NULL == outputTerminals) 
				{
					FailIf (NULL == (outputTerminals = OSArray::withObjects ((const OSObject **)&outputTerminal, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kOutputTerminals, outputTerminals), Exit);
					outputTerminals = NULL;
				} 
				else 
				{
					outputTerminals->setObject (outputTerminal);
				}

				outputTerminal->release ();
				outputTerminal = NULL;
                break;
            case FEATURE_UNIT:
				{
					UInt8						numControls;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ FEATURE_UNIT (4.3.2.5)", this);
					FailIf (NULL == (featureUnit = new AUAFeatureUnitDictionary), Exit);
					RELEASE_IF_FALSE (featureUnit, featureUnit->initDictionaryForUse());
					FailIf (NULL == featureUnit, Exit);
					
					featureUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					featureUnit->setUnitID (((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					featureUnit->setSourceID (((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bSourceID);
					featureUnit->setControlSize (((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bControlSize);
					FailIf (0 == ((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bControlSize, Exit);
					// subtract 7 because that's how many fields are guaranteed to be in the struct
					numControls = (((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bLength - 7) / ((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bControlSize;
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - There are %d controls on this feature unit", this, numControls);
					featureUnit->initControlsArray (&((ACFeatureUnitDescriptorPtr)theInterfacePtr)->bmaControls[0], numControls);
					featureUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>

					featureUnits = getFeatureUnits ();
					if (NULL == featureUnits) 
					{
						FailIf (NULL == (featureUnits = OSArray::withObjects ((const OSObject **)&featureUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kFeatureUnits, featureUnits), Exit);
						featureUnits = NULL;
					} 
					else 
					{
						featureUnits->setObject (featureUnit);
					}

					featureUnit->release ();
					featureUnit = NULL;
					break;
				}
            case MIXER_UNIT:
				{
					UInt32						controlSize;
					UInt16 *					channelConfig;
					UInt8						nrChannels;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ MIXER_UNIT (4.3.2.3)", this);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - descriptor length = %d", this, theInterfacePtr->bLength);
					FailIf (NULL == (mixerUnit = new AUAMixerUnitDictionary), Exit);
					RELEASE_IF_FALSE (mixerUnit, mixerUnit->initDictionaryForUse());
					FailIf (NULL == mixerUnit, Exit);
					
					mixerUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					mixerUnit->setUnitID (((ACMixerUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - unit ID = %d", this, ((ACMixerUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					mixerUnit->setNumInPins (((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - numInPins = %d", this, ((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					mixerUnit->initSourceIDs (&((ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					nrChannels = ((ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins];
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - nrChannels = %d", this, nrChannels);
					mixerUnit->setNumChannels (nrChannels);
					channelConfig = (UInt16 *)&((ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 1];
					*channelConfig = USBToHostWord (*channelConfig);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - channelConfig = %d", this, *channelConfig);
					mixerUnit->setChannelConfig (*channelConfig);
					controlSize = ((ACMixerUnitDescriptorPtr)theInterfacePtr)->bLength - 10 - ((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins;
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - controlSize = %d", this, controlSize);
					mixerUnit->initControlsArray (&((ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 4], controlSize);
					mixerUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
					mixerUnit->setChannelNames ( *(((UInt8*)theInterfacePtr) + (((ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 8)) );	// <rdar://problem/6706026>

					mixerUnits = getMixerUnits ();
					if (NULL == mixerUnits) 
					{
						FailIf (NULL == (mixerUnits = OSArray::withObjects ((const OSObject **)&mixerUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kMixerUnits, mixerUnits), Exit);
						mixerUnits = NULL;
					} 
					else 
					{
						mixerUnits->setObject (mixerUnit);
					}

					mixerUnit->release ();
					mixerUnit = NULL;
					break;
				}
            case SELECTOR_UNIT:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ SELECTOR_UNIT (4.3.2.4)", this);
				FailIf (NULL == (selectorUnit = new AUASelectorUnitDictionary), Exit);
				RELEASE_IF_FALSE (selectorUnit, selectorUnit->initDictionaryForUse());
				FailIf (NULL == selectorUnit, Exit);
				
				selectorUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
				selectorUnit->setNumInPins (((ACSelectorUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				selectorUnit->setUnitID (((ACSelectorUnitDescriptorPtr)theInterfacePtr)->bUnitID);
				selectorUnit->initSourceIDs (&((ACSelectorUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((ACSelectorUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - numInPins on selector = %d", this, ((ACSelectorUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				selectorUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>

				selectorUnits = getSelectorUnits ();
				if (NULL == selectorUnits) 
				{
					FailIf (NULL == (selectorUnits = OSArray::withObjects ((const OSObject **)&selectorUnit, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kSelectorUnits, selectorUnits), Exit);
					selectorUnits = NULL;
				} 
				else 
				{
					selectorUnits->setObject (selectorUnit);
				}

				selectorUnit->release ();
				selectorUnit = NULL;
				break;
            case PROCESSING_UNIT:
				{
					UInt16 *					channelConfig;
					UInt8						controlSize;
					UInt8						nrChannels;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ PROCESSING_UNIT (4.3.2.6)", this);
					FailIf (NULL == (processingUnit = new AUAProcessingUnitDictionary), Exit);
					RELEASE_IF_FALSE (processingUnit, processingUnit->initDictionaryForUse());
					FailIf (NULL == processingUnit, Exit);
					
 					processingUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					processingUnit->setUnitID (((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					processingUnit->setProcessType (((ACProcessingUnitDescriptorPtr)theInterfacePtr)->wProcessType);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - processing unit type = 0x%x", this, ((ACProcessingUnitDescriptorPtr)theInterfacePtr)->wProcessType);
					processingUnit->setNumInPins (((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					processingUnit->initSourceIDs (&((ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - numInPins = %d", this, ((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					nrChannels = ((ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins];
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - nrChannels = %d", this, nrChannels);
					processingUnit->setNumChannels (nrChannels);
					channelConfig = (UInt16 *)&((ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 1];
					*channelConfig = USBToHostWord (*channelConfig);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - channelConfig = %d", this, *channelConfig);
					processingUnit->setChannelConfig (*channelConfig);
					controlSize = ((ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 4];
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - controlSize = %d", this, controlSize);
					processingUnit->initControlsArray (&((ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 5], controlSize);
					processingUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
					processingUnit->setChannelNames (*(((UInt8*)theInterfacePtr) + (((ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 10)));	// <rdar://problem/6706026>

					processingUnits = getProcessingUnits ();
					if (NULL == processingUnits) 
					{
						FailIf (NULL == (processingUnits = OSArray::withObjects ((const OSObject **)&processingUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kProcessingUnits, processingUnits), Exit);
						processingUnits = NULL;
					} 
					else 
					{
						processingUnits->setObject (processingUnit);
					}

					processingUnit->release ();
					processingUnit = NULL;
					break;
				}
            case EXTENSION_UNIT:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ EXTENSION_UNIT (4.3.2.7)", this);
				FailIf (NULL == (extensionUnit = new AUAExtensionUnitDictionary), Exit);
				RELEASE_IF_FALSE (extensionUnit, extensionUnit->initDictionaryForUse());
				FailIf (NULL == extensionUnit, Exit);
				
				extensionUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
				extensionUnit->setUnitID (((ACExtensionUnitDescriptorPtr)theInterfacePtr)->bUnitID);
				extensionUnit->setNumInPins (((ACExtensionUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				extensionUnit->initSourceIDs (&((ACExtensionUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((ACExtensionUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				extensionUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
				extensionUnit->setChannelNames (*(((UInt8*)theInterfacePtr) + (((ACExtensionUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 10)));	// <rdar://problem/6706026>

				extensionUnits = getExtensionUnits ();
				if (NULL == extensionUnits) 
				{
					FailIf (NULL == (extensionUnits = OSArray::withObjects ((const OSObject **)&extensionUnit, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kExtensionUnits, extensionUnits), Exit);
					extensionUnits = NULL;
				} 
				else 
				{
					extensionUnits->setObject (extensionUnit);
				}

				extensionUnit->release ();
				extensionUnit = NULL;
				break;
            default:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor () - @ default. Nothing to do here.", this);
        }
		theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
		* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
    }

Exit:
	debugIOLog ("- AUAControlDictionary[%p]::parseACInterfaceDescriptor () = %p", this, theInterfacePtr);
    return theInterfacePtr;
}

USBInterfaceDescriptorPtr AUAControlDictionary::parseACInterfaceDescriptor_0200 (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength) 
{
	OSArray *							inputTerminals = NULL;
	OSArray *							outputTerminals = NULL;
	OSArray *							featureUnits = NULL;
	OSArray *							mixerUnits = NULL;
	OSArray *							selectorUnits = NULL;
	OSArray *							effectUnits = NULL;
	OSArray *							processingUnits = NULL;
	OSArray *							extensionUnits = NULL;
	OSArray *							clockSources = NULL;
	OSArray *							clockSelectors = NULL;
	OSArray *							clockMultipliers = NULL;
	AUAInputTerminalDictionary *		inputTerminal = NULL;
	AUAOutputTerminalDictionary *		outputTerminal = NULL;
	AUAFeatureUnitDictionary *			featureUnit = NULL;
	AUAMixerUnitDictionary *			mixerUnit = NULL;
	AUASelectorUnitDictionary *			selectorUnit = NULL;
	AUAEffectUnitDictionary *			effectUnit = NULL;
	AUAProcessingUnitDictionary *		processingUnit = NULL;
	AUAExtensionUnitDictionary *		extensionUnit = NULL;
	AUAClockSourceDictionary *			clockSource = NULL;
	AUAClockSelectorDictionary*			clockSelector = NULL;
	AUAClockMultiplierDictionary *		clockMultiplier = NULL;
	UInt16								adcVersion;

	debugIOLog ("+ AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 (%p, %d)", this, theInterfacePtr, currentInterface);

    FailIf (NULL == theInterfacePtr, Exit);
    FailIf (0 == theInterfacePtr->bLength, Exit);
    FailIf (CS_INTERFACE != theInterfacePtr->bDescriptorType, Exit);

    while	(		( theInterfacePtr->bLength > 0 )
				&&	( CS_INTERFACE == theInterfacePtr->bDescriptorType )
				&&	( * parsedLength <= totalLength ) )
	{
		logDescriptor ((UInt8 *) theInterfacePtr, theInterfacePtr->bLength);
        switch (theInterfacePtr->bDescriptorSubtype) 
		{
            case USBAUDIO_0200::HEADER:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ HEADER (4.7.2)", this);
				adcVersion = USBToHostWord (* (UInt16 *) (&(((USBAUDIO_0200::ACInterfaceHeaderDescriptorPtr)theInterfacePtr)->bcdADC[0])));
				if (adcVersion != kAUAUSBSpec2_0)
				{
					debugIOLog ("! AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - Encountered a descriptor version (0x%x) that AppleUSBAudio currently can't handle.", this, adcVersion);
				}
                
				// store adcVersion
				FailIf (kIOReturnSuccess != setDictionaryValue (kADCVersion, adcVersion), Exit);
				break;
            case USBAUDIO_0200::INPUT_TERMINAL:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ INPUT_TERMINAL (4.7.2.4)", this);
				FailIf (NULL == (inputTerminal = new AUAInputTerminalDictionary), Exit);
				RELEASE_IF_FALSE (inputTerminal, inputTerminal->initDictionaryForUse());
				FailIf (NULL == inputTerminal, Exit);
				
				inputTerminal->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                inputTerminal->setUnitID (((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->bTerminalID);
                inputTerminal->setTerminalType (USBToHostWord (((ACInputTerminalDescriptorPtr)theInterfacePtr)->wTerminalType));
                inputTerminal->setAssocTerminal (((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->bAssocTerminal);
                inputTerminal->setNumChannels (((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->bNrChannels);
                inputTerminal->setChannelConfig (USBToHostLong (*((UInt32 *)(&((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->bmChannelConfig[0]))));
				inputTerminal->setClockSourceID (((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->bCSourceID);
                inputTerminal->setStringIndex (((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->iTerminal);	// <rdar://6394629>
				inputTerminal->setChannelNames (((USBAUDIO_0200::ACInputTerminalDescriptorPtr)theInterfacePtr)->iChannelNames);	// <rdar://6430836>
				
				inputTerminals = getInputTerminals ();
				if (NULL == inputTerminals) 
				{
					FailIf (NULL == (inputTerminals = OSArray::withObjects ((const OSObject **)&inputTerminal, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kInputTerminals, inputTerminals), Exit);
					inputTerminals = NULL;
				} 
				else 
				{
					inputTerminals->setObject (inputTerminal);
				}

				inputTerminal->release ();
				inputTerminal = NULL;
                break;
            case USBAUDIO_0200::OUTPUT_TERMINAL:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ OUTPUT_TERMINAL (4.7.2.5)", this);
				FailIf (NULL == (outputTerminal = new AUAOutputTerminalDictionary), Exit);
				RELEASE_IF_FALSE (outputTerminal, outputTerminal->initDictionaryForUse());
				FailIf (NULL == outputTerminal, Exit);
				
				outputTerminal->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                outputTerminal->setUnitID (((USBAUDIO_0200::ACOutputTerminalDescriptorPtr)theInterfacePtr)->bTerminalID);
                outputTerminal->setTerminalType (USBToHostWord (((USBAUDIO_0200::ACOutputTerminalDescriptorPtr)theInterfacePtr)->wTerminalType));
                outputTerminal->setAssocTerminal (((USBAUDIO_0200::ACOutputTerminalDescriptorPtr)theInterfacePtr)->bAssocTerminal);
                outputTerminal->setSourceID (((USBAUDIO_0200::ACOutputTerminalDescriptorPtr)theInterfacePtr)->bSourceID);
				outputTerminal->setClockSourceID (((USBAUDIO_0200::ACOutputTerminalDescriptorPtr)theInterfacePtr)->bCSourceID);
                outputTerminal->setStringIndex (((USBAUDIO_0200::ACOutputTerminalDescriptorPtr)theInterfacePtr)->iTerminal);	// <rdar://6394629>

				outputTerminals = getOutputTerminals ();
				if (NULL == outputTerminals) 
				{
					FailIf (NULL == (outputTerminals = OSArray::withObjects ((const OSObject **)&outputTerminal, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kOutputTerminals, outputTerminals), Exit);
					outputTerminals = NULL;
				} 
				else 
				{
					outputTerminals->setObject (outputTerminal);
				}

				outputTerminal->release ();
				outputTerminal = NULL;
                break;
            case USBAUDIO_0200::FEATURE_UNIT:
				{
					UInt8						numControls;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ FEATURE_UNIT (4.7.2.8)", this);
					FailIf (NULL == (featureUnit = new AUAFeatureUnitDictionary), Exit);
					RELEASE_IF_FALSE (featureUnit, featureUnit->initDictionaryForUse());
					FailIf (NULL == featureUnit, Exit);
					
					featureUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					featureUnit->setUnitID (((USBAUDIO_0200::ACFeatureUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					featureUnit->setSourceID (((USBAUDIO_0200::ACFeatureUnitDescriptorPtr)theInterfacePtr)->bSourceID);
					featureUnit->setControlSize (4);
					// subtract 6 because that's how many fields are guaranteed to be in the struct
					numControls = (((USBAUDIO_0200::ACFeatureUnitDescriptorPtr)theInterfacePtr)->bLength - 6) / 4;
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - There are %d controls on this feature unit", this, numControls);
					featureUnit->initControlsArray (&((USBAUDIO_0200::ACFeatureUnitDescriptorPtr)theInterfacePtr)->bmaControls[0], numControls);
					featureUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>

					featureUnits = getFeatureUnits ();
					if (NULL == featureUnits) 
					{
						FailIf (NULL == (featureUnits = OSArray::withObjects ((const OSObject **)&featureUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kFeatureUnits, featureUnits), Exit);
						featureUnits = NULL;
					} 
					else 
					{
						featureUnits->setObject (featureUnit);
					}

					featureUnit->release ();
					featureUnit = NULL;
					break;
				}
            case USBAUDIO_0200::MIXER_UNIT:
				{
					UInt32						controlSize;
					UInt32 *					channelConfig;
					UInt8						nrChannels;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ MIXER_UNIT (4.7.2.6)", this);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - descriptor length = %d", this, theInterfacePtr->bLength);
					FailIf (NULL == (mixerUnit = new AUAMixerUnitDictionary), Exit);
					RELEASE_IF_FALSE (mixerUnit, mixerUnit->initDictionaryForUse());
					FailIf (NULL == mixerUnit, Exit);
					
					mixerUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					mixerUnit->setUnitID (((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - unit ID = %d", this, ((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					mixerUnit->setNumInPins (((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - numInPins = %d", this, ((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					mixerUnit->initSourceIDs (&((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					nrChannels = ((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins];
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - nrChannels = %d", this, nrChannels);
					mixerUnit->setNumChannels (nrChannels);
					channelConfig = (UInt32 *)&((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 1];
					*channelConfig = USBToHostLong (*channelConfig);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - channelConfig = %d", this, *channelConfig);
					mixerUnit->setChannelConfig (*channelConfig);
					controlSize = ((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bLength - 13 - ((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins;
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - controlSize = %d", this, controlSize);
					mixerUnit->initControlsArray (&((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->baSourceID[((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 6], controlSize);
					mixerUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - setChannelNames = 0x%X", this, *(((UInt8*)theInterfacePtr) + (((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 10)));
					mixerUnit->setChannelNames (*(((UInt8*)theInterfacePtr) + (((USBAUDIO_0200::ACMixerUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 10)));		// <rdar://problem/6706026>

					mixerUnits = getMixerUnits ();
					if (NULL == mixerUnits) 
					{
						FailIf (NULL == (mixerUnits = OSArray::withObjects ((const OSObject **)&mixerUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kMixerUnits, mixerUnits), Exit);
						mixerUnits = NULL;
					} 
					else 
					{
						mixerUnits->setObject (mixerUnit);
					}

					mixerUnit->release ();
					mixerUnit = NULL;
					break;
				}
            case USBAUDIO_0200::SELECTOR_UNIT:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ SELECTOR_UNIT (4.7.2.7)", this);
				FailIf (NULL == (selectorUnit = new AUASelectorUnitDictionary), Exit);
				RELEASE_IF_FALSE (selectorUnit, selectorUnit->initDictionaryForUse());
				FailIf (NULL == selectorUnit, Exit);
				
				selectorUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
				selectorUnit->setNumInPins (((USBAUDIO_0200::ACSelectorUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				selectorUnit->setUnitID (((USBAUDIO_0200::ACSelectorUnitDescriptorPtr)theInterfacePtr)->bUnitID);
				selectorUnit->initSourceIDs (&((USBAUDIO_0200::ACSelectorUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((USBAUDIO_0200::ACSelectorUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - numInPins on selector = %d", this, ((USBAUDIO_0200::ACSelectorUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				selectorUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>

				selectorUnits = getSelectorUnits ();
				if (NULL == selectorUnits) 
				{
					FailIf (NULL == (selectorUnits = OSArray::withObjects ((const OSObject **)&selectorUnit, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kSelectorUnits, selectorUnits), Exit);
					selectorUnits = NULL;
				} 
				else 
				{
					selectorUnits->setObject (selectorUnit);
				}

				selectorUnit->release ();
				selectorUnit = NULL;
				break;
			case USBAUDIO_0200::EFFECT_UNIT:
				{
					UInt8						numControls;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ EFFECT_UNIT (4.7.2.10)", this);
					FailIf (NULL == (effectUnit = new AUAEffectUnitDictionary), Exit);
					RELEASE_IF_FALSE (effectUnit, effectUnit->initDictionaryForUse());
					FailIf (NULL == effectUnit, Exit);
					
					effectUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					effectUnit->setUnitID (((USBAUDIO_0200::ACEffectUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					effectUnit->setEffectType (((USBAUDIO_0200::ACEffectUnitDescriptorPtr)theInterfacePtr)->wEffectType);
					effectUnit->setSourceID (((USBAUDIO_0200::ACEffectUnitDescriptorPtr)theInterfacePtr)->bSourceID);
					effectUnit->setControlSize (4);
					// subtract 8 because that's how many fields are guaranteed to be in the struct
					numControls = (((USBAUDIO_0200::ACEffectUnitDescriptorPtr)theInterfacePtr)->bLength - 8) / 4;
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - There are %d controls on this effect unit", this, numControls);
					effectUnit->initControlsArray (&((USBAUDIO_0200::ACEffectUnitDescriptorPtr)theInterfacePtr)->bmaControls[0][0], numControls);
					effectUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>

					effectUnits = getEffectUnits ();
					if (NULL == effectUnits) 
					{
						FailIf (NULL == (effectUnits = OSArray::withObjects ((const OSObject **)&effectUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kEffectUnits, effectUnits), Exit);
						effectUnits = NULL;
					} 
					else 
					{
						effectUnits->setObject (effectUnit);
					}

					effectUnit->release ();
					effectUnit = NULL;
					break;
				}
            case USBAUDIO_0200::PROCESSING_UNIT:
				{
					UInt32 *					channelConfig;
					UInt8						controlSize;
					UInt8						nrChannels;

					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ PROCESSING_UNIT (4.7.2.11)", this);
					FailIf (NULL == (processingUnit = new AUAProcessingUnitDictionary), Exit);
					RELEASE_IF_FALSE (processingUnit, processingUnit->initDictionaryForUse());
					FailIf (NULL == processingUnit, Exit);
					
 					processingUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
					processingUnit->setUnitID (((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bUnitID);
					processingUnit->setProcessType (((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->wProcessType);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - processing unit type = 0x%x", this, ((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->wProcessType);
					processingUnit->setNumInPins (((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					processingUnit->initSourceIDs (&((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - numInPins = %d", this, ((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
					nrChannels = ((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins];
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - nrChannels = %d", this, nrChannels);
					processingUnit->setNumChannels (nrChannels);
					channelConfig = (UInt32 *)&((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 1];
					*channelConfig = USBToHostLong (*channelConfig);
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - channelConfig = %d", this, *channelConfig);
					processingUnit->setChannelConfig (*channelConfig);
					controlSize = 2;
					debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - controlSize = %d", this, controlSize);
					processingUnit->initControlsArray (&((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->baSourceID[((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 6], controlSize);
					processingUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
					processingUnit->setChannelNames (*(((UInt8*)theInterfacePtr) + (((USBAUDIO_0200::ACProcessingUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 12)));	// <rdar://problem/6706026>

					processingUnits = getProcessingUnits ();
					if (NULL == processingUnits) 
					{
						FailIf (NULL == (processingUnits = OSArray::withObjects ((const OSObject **)&processingUnit, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kProcessingUnits, processingUnits), Exit);
						processingUnits = NULL;
					} 
					else 
					{
						processingUnits->setObject (processingUnit);
					}

					processingUnit->release ();
					processingUnit = NULL;
					break;
				}
            case USBAUDIO_0200::EXTENSION_UNIT:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ EXTENSION_UNIT (4.7.2.12)", this);
				FailIf (NULL == (extensionUnit = new AUAExtensionUnitDictionary), Exit);
				RELEASE_IF_FALSE (extensionUnit, extensionUnit->initDictionaryForUse());
				FailIf (NULL == extensionUnit, Exit);
				
				extensionUnit->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
				extensionUnit->setUnitID (((USBAUDIO_0200::ACExtensionUnitDescriptorPtr)theInterfacePtr)->bUnitID);
				extensionUnit->setNumInPins (((USBAUDIO_0200::ACExtensionUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				extensionUnit->initSourceIDs (&((USBAUDIO_0200::ACExtensionUnitDescriptorPtr)theInterfacePtr)->baSourceID[0], ((USBAUDIO_0200::ACExtensionUnitDescriptorPtr)theInterfacePtr)->bNrInPins);
				extensionUnit->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
				extensionUnit->setChannelNames (*(((UInt8*)theInterfacePtr) + (((USBAUDIO_0200::ACExtensionUnitDescriptorPtr)theInterfacePtr)->bNrInPins + 13)));		// <rdar://problem/6706026>

				extensionUnits = getExtensionUnits ();
				if (NULL == extensionUnits) 
				{
					FailIf (NULL == (extensionUnits = OSArray::withObjects ((const OSObject **)&extensionUnit, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kExtensionUnits, extensionUnits), Exit);
					extensionUnits = NULL;
				} 
				else 
				{
					extensionUnits->setObject (extensionUnit);
				}

				extensionUnit->release ();
				extensionUnit = NULL;
				break;
			case USBAUDIO_0200::CLOCK_SOURCE:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ CLOCK_SOURCE (4.7.2.1)", this);
				FailIf (NULL == (clockSource = new AUAClockSourceDictionary), Exit);
				RELEASE_IF_FALSE (clockSource, clockSource->initDictionaryForUse());
				FailIf (NULL == clockSource, Exit);
				
				clockSource->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                clockSource->setUnitID (((USBAUDIO_0200::ACClockSourceDescriptorPtr)theInterfacePtr)->bClockID);
				clockSource->setAttributes (((USBAUDIO_0200::ACClockSourceDescriptorPtr)theInterfacePtr)->bmAttributes);
                clockSource->setAssocTerminal (((USBAUDIO_0200::ACClockSourceDescriptorPtr)theInterfacePtr)->bAssocTerminal);
				clockSource->initControlsArray (&((USBAUDIO_0200::ACClockSourceDescriptorPtr)theInterfacePtr)->bmControls, 1);
                clockSource->setStringIndex (((USBAUDIO_0200::ACClockSourceDescriptorPtr)theInterfacePtr)->iClockSource);	// <rdar://6394629>
				
				clockSources = getClockSources ();
				if (NULL == clockSources) 
				{
					FailIf (NULL == (clockSources = OSArray::withObjects ((const OSObject **)&clockSource, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kClockSources, clockSources), Exit);
					clockSources = NULL;
				} 
				else 
				{
					clockSources->setObject (clockSource);
				}

				clockSource->release ();
				clockSource = NULL;
                break;
			case USBAUDIO_0200::CLOCK_SELECTOR:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ CLOCK_SELECTOR (4.7.2.2)", this);
				FailIf (NULL == (clockSelector = new AUAClockSelectorDictionary), Exit);
				RELEASE_IF_FALSE (clockSelector, clockSelector->initDictionaryForUse());
				FailIf (NULL == clockSelector, Exit);
				
				clockSelector->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                clockSelector->setUnitID (((USBAUDIO_0200::ACClockSelectorDescriptorPtr)theInterfacePtr)->bClockID);
				clockSelector->setNumInPins (((USBAUDIO_0200::ACClockSelectorDescriptorPtr)theInterfacePtr)->bNrInPins);
				clockSelector->initClockSourceIDs (&((USBAUDIO_0200::ACClockSelectorDescriptorPtr)theInterfacePtr)->baCSourceID[0], ((USBAUDIO_0200::ACClockSelectorDescriptorPtr)theInterfacePtr)->bNrInPins);
				clockSelector->initControlsArray (&((USBAUDIO_0200::ACClockSelectorDescriptorPtr)theInterfacePtr)->baCSourceID[((USBAUDIO_0200::ACClockSelectorDescriptorPtr)theInterfacePtr)->bNrInPins], 1);
				clockSelector->setStringIndex (*(((UInt8*)theInterfacePtr) + theInterfacePtr->bLength - 1));	// <rdar://6394629>
				
				clockSelectors = getClockSelectors ();
				if (NULL == clockSelectors) 
				{
					FailIf (NULL == (clockSelectors = OSArray::withObjects ((const OSObject **)&clockSelector, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kClockSelectors, clockSelectors), Exit);
					clockSelectors = NULL;
				} 
				else 
				{
					clockSelectors->setObject (clockSelector);
				}

				clockSelector->release ();
				clockSelector = NULL;
                break;
			case USBAUDIO_0200::CLOCK_MULTIPLIER:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ CLOCK_MULTIPLIER (4.7.2.3)", this);
				FailIf (NULL == (clockMultiplier = new AUAClockMultiplierDictionary), Exit);
				RELEASE_IF_FALSE (clockMultiplier, clockMultiplier->initDictionaryForUse());
				FailIf (NULL == clockMultiplier, Exit);
				
				clockMultiplier->setDescriptorSubType (theInterfacePtr->bDescriptorSubtype);
                clockMultiplier->setUnitID (((USBAUDIO_0200::ACClockMultiplierDescriptorPtr)theInterfacePtr)->bClockID);
				clockMultiplier->setClockSourceID (((USBAUDIO_0200::ACClockMultiplierDescriptorPtr)theInterfacePtr)->bCSourceID);
				clockMultiplier->initControlsArray (&((USBAUDIO_0200::ACClockMultiplierDescriptorPtr)theInterfacePtr)->bmControls, 1);
                clockMultiplier->setStringIndex (((USBAUDIO_0200::ACClockMultiplierDescriptorPtr)theInterfacePtr)->iClockMultiplier);	// <rdar://6394629>
				
				clockMultipliers = getClockMultipliers ();
				if (NULL == clockMultipliers) 
				{
					FailIf (NULL == (clockMultipliers = OSArray::withObjects ((const OSObject **)&clockMultiplier, 1)), Exit);
					FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kClockMultipliers, clockMultipliers), Exit);
					clockMultipliers = NULL;
				} 
				else 
				{
					clockMultipliers->setObject (clockMultiplier);
				}

				clockMultiplier->release ();
				clockMultiplier = NULL;
                break;
            default:
                debugIOLog ("? AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () - @ default. Nothing to do here.", this);
        }
		theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
		* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
    }

Exit:
	debugIOLog ("- AUAControlDictionary[%p]::parseACInterfaceDescriptor_0200 () = %p", this, theInterfacePtr);
    return theInterfacePtr;
}

void AUAControlDictionary::parseInterfaceAssociationDescriptor (USBInterfaceAssociationDescriptorPtr theInterfaceAssociationPtr)
{
	OSNumber *						dictionaryNumber = NULL;
	OSArray *						streamInterfaceNumbers = NULL;
	UInt8							numStreamInterfaces;
	UInt8							index;

	debugIOLog ("+ AUAControlDictionary[%p]::parseInterfaceAssociationDescriptor (%p, %d)", this, theInterfaceAssociationPtr);

    FailIf (NULL == theInterfaceAssociationPtr, Exit);
    FailIf (0 == theInterfaceAssociationPtr->bLength, Exit);
    FailIf (INTERFACE_ASSOCIATION != theInterfaceAssociationPtr->bDescriptorType, Exit);
    FailIf (USBAUDIO_0200::AUDIO_FUNCTION != theInterfaceAssociationPtr->bFunctionClass, Exit);
	FailIf (USBAUDIO_0200::FUNCTION_SUBCLASS_UNDEFINED != theInterfaceAssociationPtr->bFunctionSubClass, Exit);
	FailIf (USBAUDIO_0200::AF_VERSION_02_00 != theInterfaceAssociationPtr->bFunctionProtocol, Exit);

	// Store the number of stream interfaces
	numStreamInterfaces = theInterfaceAssociationPtr->bInterfaceCount;
	FailIf(0 == numStreamInterfaces, Exit);
	numStreamInterfaces -= 1; // excluding the AudioControl interface.
	debugIOLog ("? AUAControlDictionary[%p]::parseInterfaceAssociationDescriptor () - numStreamInterfaces = %d", this, numStreamInterfaces);
	FailIf (kIOReturnSuccess != setDictionaryValue (kNumStreamInterfaces, numStreamInterfaces), Exit);
	
	// Create and store stream interface number array
	FailIf (NULL == (streamInterfaceNumbers = OSArray::withCapacity (numStreamInterfaces)), Exit);
	// FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kStreamInterfaceNumbers, streamInterfaceNumbers), Exit);
	// FailIf (NULL == (streamInterfaceNumbers = getStreamInterfaceNumbers ()), Exit);
	
	debugIOLog ("    they are: ");
	for (index = 1; index <= numStreamInterfaces; index++) 
	{
		debugIOLog ("      %d ", theInterfaceAssociationPtr->bFirstInterface + index);
		// Add this streamInterfaceNumber to the OSArray.
		FailIf (NULL == (dictionaryNumber = OSNumber::withNumber (theInterfaceAssociationPtr->bFirstInterface + index, SIZEINBITS(UInt8))), Exit);
		FailIf ( false == ( streamInterfaceNumbers->setObject (dictionaryNumber) ), Exit );
		dictionaryNumber->release ();
		dictionaryNumber = NULL;
	}
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kStreamInterfaceNumbers, streamInterfaceNumbers), Exit);
			
	debugIOLog ("");

Exit:
	debugIOLog ("- AUAControlDictionary[%p]::parseInterfaceAssociationDescriptor () = %p", this, theInterfaceAssociationPtr);
}

// <rdar://problem/6021475> AppleUSBAudio: Status Interrupt Endpoint support
USBInterfaceDescriptorPtr AUAControlDictionary::parseACInterruptEndpointDescriptor ( USBInterfaceDescriptorPtr theInterfacePtr ) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = 0;

	debugIOLog ("+ AUAControlDictionary[%p]::parseACInterruptEndpointDescriptor (%p, %d)", this, theInterfacePtr);

    FailIf ( NULL == theInterfacePtr, Exit );
    FailIf ( 0 == theInterfacePtr->bLength, Exit );

	if ( ENDPOINT == theInterfacePtr->bDescriptorType ) 
	{
		if ( kInterruptType == (((USBEndpointDescriptorPtr)theInterfacePtr)->bmAttributes & kInterruptType) ) 
		{
			debugIOLog ("? AUAControlDictionary[%p]::parseACInterruptEndpointDescriptor () - @ ENDPOINT (4.6.1.1)", this);
			FailIf ( NULL == ( thisEndpoint = AUAEndpointDictionary::create () ), Exit );

			thisEndpoint->setAddress ( ((USBEndpointDescriptorPtr)theInterfacePtr)->bEndpointAddress );
			thisEndpoint->setAttributes ( ((USBEndpointDescriptorPtr)theInterfacePtr)->bmAttributes );
			
			thisEndpoint->setMaxPacketSize ( USBToHostWord (((USBEndpointDescriptorPtr)theInterfacePtr)->wMaxPacketSize) );
			thisEndpoint->setInterval ( ((USBEndpointDescriptorPtr)theInterfacePtr)->bInterval );

			endpoints = getEndpoints ();
			if ( NULL == endpoints ) 
			{
				FailIf ( NULL == ( endpoints = OSArray::withObjects ( ( const OSObject ** )&thisEndpoint, 1 ) ), Exit );
				FailIf ( kIOReturnSuccess != setDictionaryObjectAndRelease ( kEndpoints, endpoints ), Exit );
				endpoints = NULL;
			} 
			else 
			{
				endpoints->setObject ( thisEndpoint );
			}

			thisEndpoint->release ();
			thisEndpoint = NULL;
			
			theInterfacePtr = ( USBInterfaceDescriptorPtr )( ( UInt8 * )theInterfacePtr + theInterfacePtr->bLength );
		}
	}

Exit:
	debugIOLog ("- AUAControlDictionary[%p]::parseACInterruptEndpointDescriptor () = 0x%x", this, theInterfacePtr);
    return theInterfacePtr;
}

IOReturn AUAControlDictionary::getInterruptEndpointAddress ( UInt8 * address ) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    IOReturn					result = kIOReturnError;
	UInt8						endpointIndex;
	UInt8						attributes;

    endpointIndex = 0;
	* address = 0;
	FailIf (NULL == address, Exit);
	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	
	while (!(* address) && endpointIndex < endpoints->getCount ()) 
	{
		FailIf (NULL == (thisEndpoint = getIndexedEndpointDictionary (endpointIndex)), Exit);
		FailIf (kIOReturnSuccess != (result = thisEndpoint->getAttributes( &attributes )), Exit);
		if ( kInterruptType == ( attributes & kInterruptType ) )  
		{
			FailIf (kIOReturnSuccess != (result = thisEndpoint->getAddress (address)), Exit);
		}
		endpointIndex++;
	}
	
Exit:
	return result;
}

IOReturn AUAControlDictionary::getInterruptEndpointInterval ( UInt8 * interval ) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    IOReturn					result = kIOReturnError;
	UInt8						endpointIndex;
	UInt8						attributes;

    endpointIndex = 0;
	* interval = 0;
	FailIf (NULL == interval, Exit);
	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	
	while (!(* interval) && endpointIndex < endpoints->getCount ()) 
	{
		FailIf (NULL == (thisEndpoint = getIndexedEndpointDictionary (endpointIndex)), Exit);
		FailIf (kIOReturnSuccess != (result = thisEndpoint->getAttributes( &attributes )), Exit);
		if ( kInterruptType == ( attributes & kInterruptType ) )  
		{
			FailIf (kIOReturnSuccess != (result = thisEndpoint->getInterval (interval)), Exit);
		}
		endpointIndex++;
	}
	
Exit:
	return result;
}

bool AUAControlDictionary::hasInterruptEndpoint () 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    bool						result = false;
	UInt8						endpointIndex;
	UInt8						attributes;

    endpointIndex = 0;
	endpoints = getEndpoints ();
	
	if ( NULL != endpoints )
	{
		while (endpointIndex < endpoints->getCount ()) 
		{
			FailIf (NULL == (thisEndpoint = getIndexedEndpointDictionary (endpointIndex)), Exit);
			FailIf (kIOReturnSuccess != (result = thisEndpoint->getAttributes( &attributes )), Exit);
			if ( kInterruptType == ( attributes & kInterruptType ) )  
			{
				result = true;
				break;
			}
			endpointIndex++;
		}
	}
	
Exit:
	return result;
}

AUAEndpointDictionary * AUAControlDictionary::getIndexedEndpointDictionary (UInt8 index) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
	
	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	FailIf (NULL == (thisEndpoint = OSDynamicCast (AUAEndpointDictionary, endpoints->getObject (index))), Exit);
	
Exit:
	return thisEndpoint;
}

OSArray * AUAControlDictionary::getEndpoints ()
{
	OSArray *		endpoints = NULL;
	OSObject *		dictionaryValue = NULL;
	
	dictionaryValue = getObject (kEndpoints);
	if (dictionaryValue)
	{
		endpoints = OSDynamicCast (OSArray, dictionaryValue);
	}
	return endpoints;
}

#pragma mark AUAStreamDictionary

// AUAStreamDictionary

// Public methods

OSDefineMetaClassAndStructors (AUAStreamDictionary, AppleUSBAudioDictionary);

AUAStreamDictionary * AUAStreamDictionary::create (void) 
{
    AUAStreamDictionary *		streamDictionary = NULL;

    FailIf (NULL == (streamDictionary = new AUAStreamDictionary), Exit);
	RELEASE_IF_FALSE (streamDictionary, streamDictionary->initDictionaryForUse ());
	
Exit:
    return streamDictionary;
}

AUAEndpointDictionary * AUAStreamDictionary::getIndexedEndpointDictionary (UInt8 index) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
	
	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	FailIf (NULL == (thisEndpoint = OSDynamicCast (AUAEndpointDictionary, endpoints->getObject (index))), Exit);
	
Exit:
	return thisEndpoint;
}

IOReturn AUAStreamDictionary::getIsocAssociatedEndpointAddress (UInt8 * assocEndpointAddress, UInt8 address) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
    IOReturn					result = kIOReturnError;

	* assocEndpointAddress = 0;
	FailIf (NULL == (thisEndpoint = getEndpointByAddress (address)), Exit);
	FailIf (kIOReturnSuccess != (result = thisEndpoint->getSynchAddress (assocEndpointAddress)), Exit);

Exit:
    return result;
}

IOReturn AUAStreamDictionary::getIsocAssociatedEndpointMaxPacketSize (UInt16 * assocEndpointMaxPacketSize, UInt8 address) 
{
	AUAEndpointDictionary *		thisEndpoint = NULL;
    IOReturn					result = kIOReturnError;

	* assocEndpointMaxPacketSize = 0;
	FailIf (NULL == (thisEndpoint = getEndpointByAddress (address)), Exit);
	FailIf (kIOReturnSuccess != (result = thisEndpoint->getMaxPacketSize (assocEndpointMaxPacketSize)), Exit);

Exit:
    return result;
}

IOReturn AUAStreamDictionary::getIsocAssociatedEndpointRefreshInt (UInt8 * assocEndpointRefreshInt, UInt8 address) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
    IOReturn					result = kIOReturnError;

	* assocEndpointRefreshInt = 0;
	FailIf (NULL == (thisEndpoint = getEndpointByAddress (address)), Exit);
	FailIf (kIOReturnSuccess != (result = thisEndpoint->getRefreshInt (assocEndpointRefreshInt)), Exit);

Exit:
    return result;
}

IOReturn AUAStreamDictionary::getIsocEndpointAddress (UInt8 * address, UInt8 direction) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    IOReturn					result = kIOReturnError;
	UInt8						endpointIndex;
	UInt8						thisDirection;

    endpointIndex = 0;
	* address = 0;
	FailIf (NULL == address, Exit);
	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	
	while (!(* address) && endpointIndex < endpoints->getCount ()) 
	{
		FailIf (NULL == (thisEndpoint = getIndexedEndpointDictionary (endpointIndex)), Exit);
		FailIf (kIOReturnSuccess != (result = thisEndpoint->getDirection (&thisDirection)), Exit);
		if (direction == thisDirection) 
		{
			FailIf (kIOReturnSuccess != (result = thisEndpoint->getAddress (address)), Exit);
		}
		endpointIndex++;
	}
	
Exit:
	return result;
}

IOReturn AUAStreamDictionary::getIsocEndpointDirection (UInt8 * direction, UInt8 index) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    IOReturn					result = kIOReturnError;

	* direction = 0xFF;
	FailIf (NULL == (endpoints = getEndpoints()), Exit);
	FailIf (NULL == (thisEndpoint = getIndexedEndpointDictionary (index)), Exit);
	FailIf (kIOReturnSuccess != (result = thisEndpoint->getDirection (direction)), Exit);

Exit:
	return result;
}

IOReturn AUAStreamDictionary::getIsocEndpointInterval (UInt8 * interval, UInt8 direction) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    IOReturn					result = kIOReturnError;
	UInt8						endpointIndex;
	UInt8						thisDirection;

	FailIf ( NULL == interval, Exit );
	FailIf ( NULL == ( endpoints = getEndpoints () ), Exit );
	endpointIndex = 0;
	* interval = 0;
	
	while ( ! ( * interval ) && endpointIndex < endpoints->getCount ()) 
	{
		FailIf ( NULL == ( thisEndpoint = getIndexedEndpointDictionary ( endpointIndex ) ), Exit );
		FailIf ( kIOReturnSuccess != ( result = thisEndpoint->getDirection (&thisDirection) ), Exit );
		if ( direction == thisDirection ) 
		{
			// This is the isoc endpoint for which we are looking.
			FailIf ( false == thisEndpoint->isIsocStreaming (), Exit );
			FailIf ( kIOReturnSuccess != ( result = thisEndpoint->getInterval ( interval ) ), Exit );
			debugIOLog ( "? AUAStreamDictionary::getIsocEndpointInterval ( %p, %d ) = %d", interval, direction, * interval );
		}
		else
		{
			debugIOLog ( "? AUAStreamDictionary::getIsocEndpointInterval () - Endpoint at index %d has wrong direction %d (!= %d)", endpointIndex, thisDirection, direction );
		}
		endpointIndex++;
	}
	
Exit:
	return result;
}

IOReturn AUAStreamDictionary::getIsocEndpointMaxPacketSize (UInt16 * maxPacketSize, UInt8 direction)
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	OSArray *					endpoints = NULL;
    IOReturn					result = kIOReturnError;
	UInt8						endpointIndex;
	UInt8						thisDirection;

	FailIf ( NULL == maxPacketSize, Exit );
	FailIf ( NULL == ( endpoints = getEndpoints () ), Exit );
	endpointIndex = 0;
	* maxPacketSize = 0;
	
	while ( ! ( * maxPacketSize ) && endpointIndex < endpoints->getCount ()) 
	{
		FailIf ( NULL == ( thisEndpoint = getIndexedEndpointDictionary ( endpointIndex ) ), Exit );
		FailIf ( kIOReturnSuccess != ( result = thisEndpoint->getDirection (&thisDirection) ), Exit );
		if ( direction == thisDirection ) 
		{
			// This is the isoc endpoint for which we are looking.
			FailIf ( kIOReturnSuccess != ( result = thisEndpoint->getMaxPacketSize ( maxPacketSize ) ), Exit );
		}
		endpointIndex++;
	}
	
Exit:
	return result;
}

IOReturn AUAStreamDictionary::getIsocEndpointSyncType (UInt8 * syncType, UInt8 address) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
    IOReturn					result = kIOReturnError;

	* syncType = 0;
	FailIf (NULL == (thisEndpoint = getEndpointByAddress (address)), Exit);
	FailIf (kIOReturnSuccess != (result = thisEndpoint->getSyncType (syncType)), Exit);

Exit:
	return result;
}

// convert little-endian 24-bit (unsigned) to native 32-bit.  Make it a macro.
inline UInt32 ConvertSampleFreq (UInt8 *p)	
{
	UInt32 bigEndianSampleFreq = (p[2] << 16) | (p[1] << 8) | p[0];
	return (bigEndianSampleFreq);
}

USBInterfaceDescriptorPtr AUAStreamDictionary::parseASInterfaceDescriptor (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	AUAASEndpointDictionary *	asIsocEndpoint = NULL;
	UInt32						sampleRate;
	UInt16						formatTag;
	UInt8						numSampleFreqs;
	UInt8						initialBLength;
    bool						done;

	debugIOLog ("+ AUAStreamDictionary[%p]::parseASInterfaceDescriptor (%p, %d)", this, theInterfacePtr, currentInterface);

    FailIf (NULL == theInterfacePtr, Exit);
    FailIf (0 == theInterfacePtr->bLength, Exit);

    done = false;
	initialBLength = theInterfacePtr->bLength;
    while	(		( theInterfacePtr )
				&&	( theInterfacePtr->bLength > 0 )
				&&	( !done )
				&&	( * parsedLength <= totalLength ) ) 
	{
		if (theInterfacePtr->bLength < initialBLength)
		{
			logDescriptor ((UInt8 *) theInterfacePtr, theInterfacePtr->bLength);
        }
		if (CS_INTERFACE == theInterfacePtr->bDescriptorType) 
		{
            switch (theInterfacePtr->bDescriptorSubtype) 
			{
                case AS_GENERAL:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ AS_GENERAL (4.5.2)", this);
                    FailIf (kIOReturnSuccess != setDictionaryValue (kTerminalLink, ((ASInterfaceDescriptorPtr)theInterfacePtr)->bTerminalLink), Exit);
                    FailIf (kIOReturnSuccess != setDictionaryValue (kDelay, ((ASInterfaceDescriptorPtr)theInterfacePtr)->bDelay), Exit);
                    //formatTag = USBToHostWord ((((ASInterfaceDescriptorPtr)theInterfacePtr)->wFormatTag[1] << 8) | ((ASInterfaceDescriptorPtr)theInterfacePtr)->wFormatTag[0]);
					formatTag = (((ASInterfaceDescriptorPtr)theInterfacePtr)->wFormatTag[1] << 8) | ((ASInterfaceDescriptorPtr)theInterfacePtr)->wFormatTag[0];
					debugIOLog ( "? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - formatTag = 0x%x", this, formatTag );
					FailIf (kIOReturnSuccess != setDictionaryValue (kFormatTag, formatTag), Exit);

					theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                case FORMAT_TYPE:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ FORMAT_TYPE", this);
					switch (((ASFormatTypeIDescriptorPtr)theInterfacePtr)->bFormatType) 
					{
						case FORMAT_TYPE_I:
						case FORMAT_TYPE_III:
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ FORMAT_TYPE_I/FORMAT_TYPE_III (Format 2.2.5/2.4.1)", this);
							FailIf (kIOReturnSuccess != (setDictionaryValue (kNumChannels, ((ASFormatTypeIDescriptorPtr)theInterfacePtr)->bNrChannels)), Exit);
							FailIf (kIOReturnSuccess != (setDictionaryValue (kSubframeSize, ((ASFormatTypeIDescriptorPtr)theInterfacePtr)->bSubframeSize)), Exit);
							FailIf (kIOReturnSuccess != (setDictionaryValue (kBitResolution, ((ASFormatTypeIDescriptorPtr)theInterfacePtr)->bBitResolution)), Exit);
							numSampleFreqs = ((ASFormatTypeIDescriptorPtr)theInterfacePtr)->bSamFreqType;
							FailIf (kIOReturnSuccess != setDictionaryValue (kNumSampleRates, numSampleFreqs), Exit);
							
							if (0 != numSampleFreqs) 
							{
								debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - Interface has a discrete number (%d) of sample rates ", this, numSampleFreqs);
								for (UInt8 sampleRateIndex = 0; sampleRateIndex < numSampleFreqs; sampleRateIndex++) 
								{
									sampleRate = ConvertSampleFreq( &((ASFormatTypeIDescriptorPtr)theInterfacePtr)->sampleFreq[sampleRateIndex * kBytesPerSampleFrequency] );
									FailIf (kIOReturnSuccess != addSampleRate (sampleRate), Exit);
								}
							} 
							else 
							{
								debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - Device has a variable number of sample rates", this);
								for (UInt8 sampleRateIndex = 0; sampleRateIndex < 2; sampleRateIndex++) 
								{
									sampleRate = ConvertSampleFreq( &((ASFormatTypeIDescriptorPtr)theInterfacePtr)->sampleFreq[sampleRateIndex * 3] );
									FailIf (kIOReturnSuccess != addSampleRate (sampleRate), Exit);
								}
							}
							break;
						case FORMAT_TYPE_II:
							UInt16		maxBitRate;
							UInt16		samplesPerFrame;
							
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ FORMAT_TYPE_II (Format 2.3.6)", this);
							maxBitRate = USBToHostWord (((ASFormatTypeIIDescriptorPtr)theInterfacePtr)->wMaxBitRate);
							FailIf (kIOReturnSuccess != setDictionaryValue (kMaxBitRate, maxBitRate), Exit);
							samplesPerFrame = USBToHostWord (((ASFormatTypeIIDescriptorPtr)theInterfacePtr)->wSamplesPerFrame);
							FailIf (kIOReturnSuccess != setDictionaryValue (kSamplesPerFrame, samplesPerFrame), Exit);
							numSampleFreqs = ((ASFormatTypeIIDescriptorPtr)theInterfacePtr)->bSamFreqType;
							FailIf (kIOReturnSuccess != setDictionaryValue (kNumSampleRates, numSampleFreqs), Exit);

							if (0 != numSampleFreqs) 
							{
								debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - Interface has a discrete number (%d) of sample rates ", this, numSampleFreqs);
								for (UInt8 sampleRateIndex = 0; sampleRateIndex < numSampleFreqs; sampleRateIndex++) 
								{
									sampleRate = ConvertSampleFreq( &((ASFormatTypeIIDescriptorPtr)theInterfacePtr)->sampleFreq[sampleRateIndex * kBytesPerSampleFrequency] );
									FailIf (kIOReturnSuccess != addSampleRate (sampleRate), Exit);
								}
							} 
							else 
							{
								debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - Device has a variable number of sample rates", this);
								for (UInt8 sampleRateIndex = 0; sampleRateIndex < 2; sampleRateIndex++) 
								{
									sampleRate = ConvertSampleFreq( &((ASFormatTypeIIDescriptorPtr)theInterfacePtr)->sampleFreq[sampleRateIndex * 3] );
									FailIf (kIOReturnSuccess != addSampleRate (sampleRate), Exit);
								}
							}
							break;
						default:
							debugIOLog ("! AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ Unknown Format Type!", this);
					}
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
				case FORMAT_SPECIFIC:
					UInt32		bmAC3BSID;
					UInt16		bmMPEGCapabilities;
					
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ FORMAT_SPECIFIC", this);
					formatTag = USBToHostWord (((ASFormatSpecificDescriptorHeaderPtr)theInterfacePtr)->wFormatTag[1] << 8 | ((ASFormatSpecificDescriptorHeaderPtr)theInterfacePtr)->wFormatTag[0]);
					switch (formatTag) 
					{
						case MPEG:
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ MPEG (2.3.8.1.1)", this);
							bmMPEGCapabilities = USBToHostWord(
												((ASMPEGFormatSpecificDescriptorPtr)theInterfacePtr)->bmMPEGCapabilities[1] << 8 |
												((ASMPEGFormatSpecificDescriptorPtr)theInterfacePtr)->bmMPEGCapabilities[0]);
							FailIf (kIOReturnSuccess != setDictionaryValue (kMPEGCapabilities, bmMPEGCapabilities), Exit);
							FailIf (kIOReturnSuccess != setDictionaryValue (kMPEGFeatures, ((ASMPEGFormatSpecificDescriptorPtr)theInterfacePtr)->bmMPEGFeatures), Exit);
							break;
						case AC3:
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ AC3 (Format 2.3.8.2.1)", this);
							bmAC3BSID = USBToHostLong(
										((ASAC3FormatSpecificDescriptorPtr)theInterfacePtr)->bmBSID[3] << 24 |
										((ASAC3FormatSpecificDescriptorPtr)theInterfacePtr)->bmBSID[2] << 16 |
										((ASAC3FormatSpecificDescriptorPtr)theInterfacePtr)->bmBSID[1] << 8 |
										((ASAC3FormatSpecificDescriptorPtr)theInterfacePtr)->bmBSID[0]);
							FailIf (kIOReturnSuccess != setDictionaryValue (kAC3BSID, bmAC3BSID), Exit);
							FailIf (kIOReturnSuccess != setDictionaryValue (kAC3Features, ((ASAC3FormatSpecificDescriptorPtr)theInterfacePtr)->bmAC3Features), Exit);							
							break;
						default:
							debugIOLog ("! AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ Unknown format type 0x%x", this, formatTag);
							break;
					}
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                default:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ Default", this);
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
            }
        } 
		else 
		{
			OSArray * endpoints = 0;
            switch (theInterfacePtr->bDescriptorType) 
			{
                case INTERFACE:
                    // Need to make a new interface dictionary for this new interface or new alternate setting
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ INTERFACE", this);
                    done = true;
                    break;
                case ENDPOINT:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ ENDPOINT (4.6.1.1)", this);
                    FailIf (NULL == (thisEndpoint = AUAEndpointDictionary::create ()), Exit);

					thisEndpoint->setAddress (((USBEndpointDescriptorPtr)theInterfacePtr)->bEndpointAddress);
                    thisEndpoint->setAttributes (((USBEndpointDescriptorPtr)theInterfacePtr)->bmAttributes);
                    thisEndpoint->setMaxPacketSize (USBToHostWord (((USBEndpointDescriptorPtr)theInterfacePtr)->wMaxPacketSize));
					thisEndpoint->setRefreshInt (((USBEndpointDescriptorPtr)theInterfacePtr)->bRefresh);
					thisEndpoint->setSynchAddress (((USBEndpointDescriptorPtr)theInterfacePtr)->bSynchAddress);

					endpoints = getEndpoints ();
					if (NULL == endpoints) 
					{
						FailIf (NULL == (endpoints = OSArray::withObjects ((const OSObject **)&thisEndpoint, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kEndpoints, endpoints), Exit);
						endpoints = NULL;
					} 
					else 
					{
						endpoints->setObject (thisEndpoint);
					}

					thisEndpoint->release ();
					thisEndpoint = NULL;
					
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                case CS_ENDPOINT:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ CS_ENDPOINT (4.6.1.2)", this);
                    if (EP_GENERAL == ((ASEndpointDescriptorPtr)theInterfacePtr)->bDescriptorSubtype) 
					{
                        debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ EP_GENERAL", this);
                        asIsocEndpoint = new AUAASEndpointDictionary (((ASEndpointDescriptorPtr)theInterfacePtr)->bmAttributes & (1 << sampleFreqControlBit),
                                                                            ((ASEndpointDescriptorPtr)theInterfacePtr)->bmAttributes & (1 << pitchControlBit),
                                                                            ((ASEndpointDescriptorPtr)theInterfacePtr)->bmAttributes & (1 << maxPacketsOnlyBit),
                                                                            ((ASEndpointDescriptorPtr)theInterfacePtr)->bLockDelayUnits,
																			USBToHostWord (((UInt16 *)((ASEndpointDescriptorPtr)theInterfacePtr)->wLockDelay)[0]));
						FailIf (NULL == asIsocEndpoint, Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kASIsocEndpoint, asIsocEndpoint), Exit);
						asIsocEndpoint = NULL;
                    }
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                default:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor () - @ Default (else)", this);
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
            }
        }
    }

Exit:
	debugIOLog ("- AUAStreamDictionary[%p]::parseASInterfaceDescriptor () = 0x%x", this, theInterfacePtr);
    return theInterfacePtr;
}

USBInterfaceDescriptorPtr AUAStreamDictionary::parseASInterfaceDescriptor_0200 (USBInterfaceDescriptorPtr theInterfacePtr, UInt8 const currentInterface, UInt16 * parsedLength, UInt16 totalLength) 
{
    AUAEndpointDictionary *		thisEndpoint = NULL;
	AUAASEndpointDictionary *	asIsocEndpoint = NULL;
	AUAEndpointDictionary *		dataEndpoint = NULL;
	AUAEndpointDictionary *		feedbackEndpoint = NULL;
	UInt8						endpointAddress;
	UInt32						formats;
	UInt16						pseudoFormatTag;
	UInt8						initialBLength;
    bool						done;

	debugIOLog ("+ AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 (%p, %d)", this, theInterfacePtr, currentInterface);

    FailIf (NULL == theInterfacePtr, Exit);
    FailIf (0 == theInterfacePtr->bLength, Exit);

    done = false;
	initialBLength = theInterfacePtr->bLength;
    while	(		( theInterfacePtr )
				&&	( theInterfacePtr->bLength > 0 )
				&&	( !done )
				&&	( * parsedLength <= totalLength ) ) 
	{
		if (theInterfacePtr->bLength < initialBLength)
		{
			logDescriptor ((UInt8 *) theInterfacePtr, theInterfacePtr->bLength);
        }
		if (CS_INTERFACE == theInterfacePtr->bDescriptorType) 
		{
            switch (theInterfacePtr->bDescriptorSubtype) 
			{
                case USBAUDIO_0200::AS_GENERAL:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ AS_GENERAL (4.9.2)", this);
                    FailIf (kIOReturnSuccess != setDictionaryValue (kTerminalLink, ((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bTerminalLink), Exit);
					formats = (((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bmFormats[3] << 24) | (((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bmFormats[2] << 16) | 
							  (((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bmFormats[1] << 8) | ((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bmFormats[0];
					FailIf (kIOReturnSuccess != setDictionaryValue (kFormats, formats), Exit);
					// [rdar://5600018] TODO: Should more fully support Type III formats
					switch (((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bFormatType)
					{
						case USBAUDIO_0200::FORMAT_TYPE_I:
							if (formats & USBAUDIO_0200::PCM) 
							{
								pseudoFormatTag = PCM;
							}
							else if (formats & USBAUDIO_0200::PCM8) 
							{
								pseudoFormatTag = PCM8;
							}
							else if (formats & USBAUDIO_0200::IEEE_FLOAT) 
							{
								pseudoFormatTag = IEEE_FLOAT;
							}
							else if (formats & USBAUDIO_0200::ALAW) 
							{
								pseudoFormatTag = ALAW;
							}
							else
							{
								pseudoFormatTag = TYPE_I_UNDEFINED;
							}
							break;
						case USBAUDIO_0200::FORMAT_TYPE_II:
							if (formats & USBAUDIO_0200::MPEG) 
							{
								pseudoFormatTag = MPEG;
							}
							else if (formats & USBAUDIO_0200::AC3) 
							{
								pseudoFormatTag = AC3;
							}
							else
							{
								pseudoFormatTag = TYPE_II_UNDEFINED;
							}
							break;
						case USBAUDIO_0200::FORMAT_TYPE_III:
							if (formats & USBAUDIO_0200::IEC61937_AC3) 
							{
								pseudoFormatTag = IEC1937_AC3;
							}
							else if (formats & USBAUDIO_0200::IEC1937_MPEG1_Layer1) 
							{
								pseudoFormatTag = IEC1937_MPEG1_Layer1;
							}
							else if (formats & USBAUDIO_0200::IEC1937_MPEG1_Layer2or3) 
							{
								pseudoFormatTag = IEC1937_MPEG1_Layer2or3;
							}
							else if (formats & USBAUDIO_0200::IEC61937_MPEG2_EXT) 
							{
								pseudoFormatTag = IEC1937_MPEG2_EXT;
							}
							else if (formats & USBAUDIO_0200::IEC1937_MPEG2_Layer1_LS) 
							{
								pseudoFormatTag = IEC1937_MPEG2_Layer1_LS;
							}
							else if (formats & USBAUDIO_0200::IEC1937_MPEG2_Layer2or3_LS) 
							{
								pseudoFormatTag = IEC1937_MPEG2_Layer2or3_LS;
							}
							else
							{
								pseudoFormatTag = TYPE_III_UNDEFINED;
							}
							break;
						default:
							pseudoFormatTag = 0;
							break;
					}
					FailIf ( kIOReturnSuccess != setDictionaryValue ( kFormatTag, pseudoFormatTag ), Exit );
					
					FailIf (kIOReturnSuccess != (setDictionaryValue (kNumChannels, ((USBAUDIO_0200::ASInterfaceDescriptorPtr)theInterfacePtr)->bNrChannels)), Exit);

					theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                case USBAUDIO_0200::FORMAT_TYPE:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ FORMAT_TYPE", this);
					switch (((USBAUDIO_0200::ASFormatTypeIDescriptorPtr)theInterfacePtr)->bFormatType) 
					{
						case USBAUDIO_0200::FORMAT_TYPE_I:
						case USBAUDIO_0200::FORMAT_TYPE_III:
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ FORMAT_TYPE_I/FORMAT_TYPE_III (Format 2.3.1.6/2.3.3.1)", this);
							FailIf (kIOReturnSuccess != (setDictionaryValue (kSubframeSize, ((USBAUDIO_0200::ASFormatTypeIDescriptorPtr)theInterfacePtr)->bSubslotSize)), Exit);
							FailIf (kIOReturnSuccess != (setDictionaryValue (kBitResolution, ((USBAUDIO_0200::ASFormatTypeIDescriptorPtr)theInterfacePtr)->bBitResolution)), Exit);
							break;
						case USBAUDIO_0200::FORMAT_TYPE_II:
							UInt16		maxBitRate;
							UInt16		samplesPerFrame;
							
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ FORMAT_TYPE_II (Format 2.3.2.6)", this);
							maxBitRate = USBToHostWord (((USBAUDIO_0200::ASFormatTypeIIDescriptorPtr)theInterfacePtr)->wMaxBitRate);
							FailIf (kIOReturnSuccess != setDictionaryValue (kMaxBitRate, maxBitRate), Exit);
							samplesPerFrame = USBToHostWord (((USBAUDIO_0200::ASFormatTypeIIDescriptorPtr)theInterfacePtr)->wSlotsPerFrame);
							FailIf (kIOReturnSuccess != setDictionaryValue (kSamplesPerFrame, samplesPerFrame), Exit);
							break;
						default:
							debugIOLog ("! AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ Unknown/Unsupported Format Type!", this);
					}
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
				case USBAUDIO_0200::ENCODER:					
					UInt8		bEncoder;
					
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ ENCODER", this);
					bEncoder = ((USBAUDIO_0200::ASEncoderDescriptorPtr)theInterfacePtr)->bEncoder;
					switch (bEncoder) 
					{
						default:
							debugIOLog ("! AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ Unknown/unsupported encoder type 0x%x", this, bEncoder);
							break;
					}

                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
				case USBAUDIO_0200::DECODER:
					UInt32		bmAC3BSID;
					UInt16		bmMPEGCapabilities;
					UInt8		bDecoder;
					
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ DECODER", this);
					bDecoder = ((USBAUDIO_0200::ASDecoderDescriptorPtr)theInterfacePtr)->bDecoder;
					switch (bDecoder) 
					{
						case USBAUDIO_0200::MPEG_DECODER:
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ MPEG (4.9.5.1)", this);
							bmMPEGCapabilities = USBToHostWord(
												((USBAUDIO_0200::ASMPEGDecoderDescriptorPtr)theInterfacePtr)->bmMPEGCapabilities[1] << 8 |
												((USBAUDIO_0200::ASMPEGDecoderDescriptorPtr)theInterfacePtr)->bmMPEGCapabilities[0]);
							FailIf (kIOReturnSuccess != setDictionaryValue (kMPEGCapabilities, bmMPEGCapabilities), Exit);
							FailIf (kIOReturnSuccess != setDictionaryValue (kMPEGFeatures, ((USBAUDIO_0200::ASMPEGDecoderDescriptorPtr)theInterfacePtr)->bmMPEGFeatures), Exit);
							break;
						case USBAUDIO_0200::AC3_DECODER:
							debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ AC3 (4.9.5.2)", this);
							bmAC3BSID = USBToHostLong(
										((USBAUDIO_0200::ASAC3DecoderDescriptorPtr)theInterfacePtr)->bmBSID[3] << 24 |
										((USBAUDIO_0200::ASAC3DecoderDescriptorPtr)theInterfacePtr)->bmBSID[2] << 16 |
										((USBAUDIO_0200::ASAC3DecoderDescriptorPtr)theInterfacePtr)->bmBSID[1] << 8 |
										((USBAUDIO_0200::ASAC3DecoderDescriptorPtr)theInterfacePtr)->bmBSID[0]);
							FailIf (kIOReturnSuccess != setDictionaryValue (kAC3BSID, bmAC3BSID), Exit);
							FailIf (kIOReturnSuccess != setDictionaryValue (kAC3Features, ((ASAC3FormatSpecificDescriptorPtr)theInterfacePtr)->bmAC3Features), Exit);							
							break;
						default:
							debugIOLog ("! AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ Unknown/unsupported decoder type 0x%x", this, bDecoder);
							break;
					}
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                default:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ Default", this);
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
            }
        } 
		else 
		{
			OSArray * endpoints = 0;
            switch (theInterfacePtr->bDescriptorType) 
			{
                case INTERFACE:
                    // Need to make a new interface dictionary for this new interface or new alternate setting
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ INTERFACE", this);
                    done = true;
                    break;
                case ENDPOINT:
					UInt16 maxPacketSize;	// <rdar://problem/6789044>
					UInt16 numTransactions;	// <rdar://problem/6789044>
					
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ ENDPOINT (4.10.1.1)", this);
                    FailIf (NULL == (thisEndpoint = AUAEndpointDictionary::create ()), Exit);

					thisEndpoint->setAddress (((USBAUDIO_0200::USBEndpointDescriptorPtr)theInterfacePtr)->bEndpointAddress);
                    thisEndpoint->setAttributes (((USBAUDIO_0200::USBEndpointDescriptorPtr)theInterfacePtr)->bmAttributes);
					// <rdar://problem/6789044>
					maxPacketSize = ((USBAUDIO_0200::USBEndpointDescriptorPtr)theInterfacePtr)->wMaxPacketSize;
					numTransactions = ((maxPacketSize & kTransactionsPerMicroframe_Mask) >> 11) + 1;
                    thisEndpoint->setMaxPacketSize (USBToHostWord ((maxPacketSize & kMaxPacketSize_Mask) * numTransactions));	// (Bits 10..0 of wMaxPacketSize) * (number of transactions per microframe)
					thisEndpoint->setInterval (((USBAUDIO_0200::USBEndpointDescriptorPtr)theInterfacePtr)->bInterval);
					
					endpoints = getEndpoints ();
					if (NULL == endpoints) 
					{
						FailIf (NULL == (endpoints = OSArray::withObjects ((const OSObject **)&thisEndpoint, 1)), Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kEndpoints, endpoints), Exit);
						endpoints = NULL;
					} 
					else 
					{
						endpoints->setObject (thisEndpoint);
					}

					if (thisEndpoint->isIsocStreaming ())
					{
						dataEndpoint = thisEndpoint;
					}
					else if (thisEndpoint->isIsocFeedback ())
					{
						thisEndpoint->setRefreshInt (((USBAUDIO_0200::USBEndpointDescriptorPtr) theInterfacePtr)->bInterval - 1 - 3);

						feedbackEndpoint = thisEndpoint;
					}
					
					if ((NULL != feedbackEndpoint) && (NULL != dataEndpoint))
					{
						if (kIOReturnSuccess == feedbackEndpoint->getAddress (&endpointAddress))
						{
							dataEndpoint->setSynchAddress (endpointAddress);
						}
					}
					
					thisEndpoint->release ();
					thisEndpoint = NULL;
					
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                case CS_ENDPOINT:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ CS_ENDPOINT (4.10.1.2)", this);
                    if (EP_GENERAL == ((ASEndpointDescriptorPtr)theInterfacePtr)->bDescriptorSubtype) 
					{
                        debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ EP_GENERAL", this);
                        asIsocEndpoint = new AUAASEndpointDictionary (0,
						                                              (((USBAUDIO_0200::ASEndpointDescriptorPtr)theInterfacePtr)->bmControls & 0x3) == 0x3,
                                                                      ((USBAUDIO_0200::ASEndpointDescriptorPtr)theInterfacePtr)->bmAttributes & (1 << maxPacketsOnlyBit),
                                                                      ((USBAUDIO_0200::ASEndpointDescriptorPtr)theInterfacePtr)->bLockDelayUnits,
																	  ((USBAUDIO_0200::ASEndpointDescriptorPtr)theInterfacePtr)->wLockDelay);
						FailIf (NULL == asIsocEndpoint, Exit);
						FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kASIsocEndpoint, asIsocEndpoint), Exit);
						asIsocEndpoint = NULL;
                    }
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
                    break;
                default:
                    debugIOLog ("? AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () - @ Default (else)", this);
                    theInterfacePtr = (USBInterfaceDescriptorPtr)((UInt8 *)theInterfacePtr + theInterfacePtr->bLength);
					* parsedLength = ( theInterfacePtr ? ( * parsedLength + theInterfacePtr->bLength ) : totalLength );
            }
        }
    }

Exit:
	debugIOLog ("- AUAStreamDictionary[%p]::parseASInterfaceDescriptor_0200 () = 0x%x", this, theInterfacePtr);
    return theInterfacePtr;
}

// Private methods

IOReturn AUAStreamDictionary::addSampleRate (UInt32 sampleRate)
{
	OSArray *		sampleRates = NULL;
	OSNumber *		sampleRateNumber = NULL;
	IOReturn		result = kIOReturnError;
	
	FailIf (NULL == (sampleRateNumber = OSNumber::withNumber (sampleRate, SIZEINBITS(UInt32))), Exit);
	sampleRates = getSampleRates ();
	if (NULL == sampleRates)
	{
		FailIf (NULL == (sampleRates = OSArray::withObjects ((const OSObject **)&sampleRateNumber, 1)), Exit);
		FailIf (kIOReturnSuccess != (result = setDictionaryObjectAndRelease (kSampleRates, sampleRates)), Exit);
		sampleRates = NULL;
	}
	else
	{
		FailIf (false == sampleRates->setObject (sampleRateNumber), Exit);
	}
	result = kIOReturnSuccess;
	
Exit:
	if (NULL != sampleRateNumber)
	{
		sampleRateNumber->release ();
		sampleRateNumber = NULL;
	}
	return result;
}

IOReturn AUAStreamDictionary::addSampleRatesToStreamDictionary ( OSArray * sampleRates )
{
	IOReturn		result = kIOReturnError;
	OSArray *		existingSampleRates = NULL;
	OSNumber *		sampleRateNumber = NULL;
	OSNumber *		existingSampleRateNumber = NULL;
	UInt8			numSampleFreqs = 0;
	bool			isDuplicate;
	
	FailIf ( NULL == sampleRates, Exit );
	existingSampleRates = getSampleRates ();
	
	for ( UInt16 sampleRateIndex = 0; sampleRateIndex < sampleRates->getCount (); sampleRateIndex++ )
	{
		FailIf ( NULL == ( sampleRateNumber = OSDynamicCast ( OSNumber, sampleRates->getObject ( sampleRateIndex ) ) ), Exit );

		isDuplicate = false;

		if ( existingSampleRates )
		{
			// Check to make sure we aren't adding a duplicate
			for ( UInt16 rateIndex= 0; rateIndex < existingSampleRates->getCount (); rateIndex++ )
			{
				FailIf ( NULL == ( existingSampleRateNumber = OSDynamicCast ( OSNumber, existingSampleRates->getObject ( rateIndex ) ) ), Exit );
				if ( existingSampleRateNumber->isEqualTo ( sampleRateNumber ) )
				{
					isDuplicate = true;
					break;
				}
			}
		}
		
		if ( ! isDuplicate )
		{
			if ( sampleRateNumber->unsigned32BitValue () )
			{
				OSArray *						endpoints = NULL;
				AUAEndpointDictionary *			endpoint = NULL;
				UInt32							averageFrameSamples;
				UInt16							averageFrameSize;
				UInt16							maxPacketSize;
				UInt8							interval;
				UInt8							transactionsPerUSBFrame;
				UInt8							direction;
				UInt8							numChannels;
				UInt8							bitResolution;
				bool							found = false;

				//[rdar://4801012] Only add the sample rate if the average frame size doesn't exceed the max packet size
				// We can cheat to get the isoc endpoint in UAC2.0
				FailIf ( NULL == ( endpoints = getEndpoints() ), Exit );
				for ( UInt8 endpointIndex = 0; endpointIndex < endpoints->getCount (); endpointIndex++ )
				{
					FailIf ( NULL == ( endpoint = OSDynamicCast ( AUAEndpointDictionary, endpoints->getObject ( endpointIndex ) ) ), Exit );
					if ( endpoint->isIsocStreaming () )
					{
						found = true;
						FailIf ( kIOReturnSuccess != endpoint->getDirection ( &direction ), Exit );
						break;
					}
				}
				
				FailIf ( true != found, Exit );
				FailIf ( kIOReturnSuccess != getIsocEndpointMaxPacketSize ( &maxPacketSize, direction ), Exit );
				FailIf ( kIOReturnSuccess != getNumChannels( &numChannels ), Exit );
				FailIf ( kIOReturnSuccess != getBitResolution( &bitResolution ), Exit );
								
				// Must determine the number of transfer opportunities per millisecond.
				FailIf ( kIOReturnSuccess != getIsocEndpointInterval ( &interval, direction ), Exit );
				if ( 0 == interval )
				{
					debugIOLog ( "! AUAStreamDictionary[%p]::addSampleRatesToStreamDictionary () - ERROR! Isoc endpoint has a refresh interval of 0! Treating as 4 ...", this );
					transactionsPerUSBFrame = 1;
				}
				else
				{
					FailIf ( interval > 4, Exit );
					transactionsPerUSBFrame = 8 >> ( interval - 1 );
				}
				
				averageFrameSamples = sampleRateNumber->unsigned32BitValue() / ( 1000 * transactionsPerUSBFrame );
				averageFrameSize = ( averageFrameSamples * numChannels * ( bitResolution / 8 ) );
				
				// Continue the loop if this alternate setting can't add this sample rate
				if ( averageFrameSize > maxPacketSize )
				{
					debugIOLog ( "! AUAStreamDictionary::addSampleRatesToStreamDictionary () - cannot add sample rate %lu due to packet size constraints!", sampleRateNumber->unsigned32BitValue () );
					continue;
				}
				else
				{
					debugIOLog ( "? AUAStreamDictionary::addSampleRatesToStreamDictionary () - adding sample rate %lu", sampleRateNumber->unsigned32BitValue () );
					FailIf ( kIOReturnSuccess != addSampleRate ( sampleRateNumber->unsigned32BitValue () ), Exit );
				}
			}
		}
	}
	
	existingSampleRates = getSampleRates ();
	
	if ( existingSampleRates ) 
	{
		numSampleFreqs = existingSampleRates->getCount();	
	}
	
	FailIf (kIOReturnSuccess != setDictionaryValue (kNumSampleRates, numSampleFreqs), Exit);
	
	result = kIOReturnSuccess;

Exit:
	return result;
}

AUAASEndpointDictionary * AUAStreamDictionary::getASEndpointDictionary ()
{
	OSObject *					dictionaryValue = NULL;
	AUAASEndpointDictionary *	asEndpoint = NULL;
	
	FailIf (NULL == (dictionaryValue = getObject (kASIsocEndpoint)), Exit);
	FailIf (NULL == (asEndpoint = OSDynamicCast (AUAASEndpointDictionary, dictionaryValue)), Exit);

Exit:
	return asEndpoint;
}

OSArray * AUAStreamDictionary::getEndpoints ()
{
	OSArray *		endpoints = NULL;
	OSObject *		dictionaryValue = NULL;
	
	dictionaryValue = getObject (kEndpoints);
	if (dictionaryValue)
	{
		endpoints = OSDynamicCast (OSArray, dictionaryValue);
	}
	return endpoints;
}

AUAEndpointDictionary * AUAStreamDictionary::getEndpointByAddress (UInt8 address) 
{
    AUAEndpointDictionary *		 	thisEndpoint = NULL;
	OSArray *						endpoints = NULL;
    UInt8							endpointIndex;
	UInt8							thisAddress;
    bool							found = false;

    endpointIndex = 0;
	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	while (!found && endpointIndex < endpoints->getCount ()) 
	{
		FailIf (NULL == (thisEndpoint = getIndexedEndpointDictionary (endpointIndex)), Exit);
		FailIf (kIOReturnSuccess != thisEndpoint->getAddress (&thisAddress), Exit);
		if (thisAddress == address) 
		{
			found = true;
		}
		endpointIndex++;
	}
	
Exit:
	if (found != true)
	{
		thisEndpoint = NULL;
	}
	
    return thisEndpoint;
}

AUAEndpointDictionary * AUAStreamDictionary::getEndpointDictionaryByAddress (UInt8 address) 
{
    AUAEndpointDictionary *		 	thisEndpoint = NULL;
	OSArray *						endpoints = NULL;
    UInt8							endpointIndex = 0;
	UInt8							thisAddress;
    bool							found = false;

	FailIf (NULL == (endpoints = getEndpoints ()), Exit);
	while (!found && endpointIndex < endpoints->getCount ()) 
	{
		FailIf (NULL == (thisEndpoint = OSDynamicCast (AUAEndpointDictionary, endpoints->getObject (endpointIndex))), Exit);
		FailIf (kIOReturnSuccess != thisEndpoint->getAddress (&thisAddress), Exit);
		if (address == thisAddress) 
		{
			found = true;
		}
		endpointIndex++;
	}

Exit:
    if (found != true)
	{
		thisEndpoint = NULL;
	}
    return thisEndpoint;
}

bool AUAStreamDictionary::asEndpointHasMaxPacketsOnly ()
{
	AUAASEndpointDictionary *	asEndpoint = NULL;
	bool						result = false;
	
	FailIf (NULL == (asEndpoint = getASEndpointDictionary ()), Exit);
	FailIf (kIOReturnSuccess != asEndpoint->hasMaxPacketsOnly (&result), Exit);

Exit:
	return result;
}

IOReturn AUAStreamDictionary::asEndpointGetLockDelay (UInt8 * lockDelay)
{
	AUAASEndpointDictionary *	asEndpoint = NULL;
	IOReturn					result = kIOReturnError;
	
	FailIf (NULL == (asEndpoint = getASEndpointDictionary ()), Exit);
	FailIf (kIOReturnSuccess != (result = asEndpoint->getLockDelay (lockDelay)), Exit);

Exit:
	return result;
}

IOReturn AUAStreamDictionary::asEndpointGetLockDelayUnits (UInt8 * lockDelayUnits)
{
	AUAASEndpointDictionary *	asEndpoint = NULL;
	IOReturn					result = kIOReturnError;
	
	FailIf (NULL == (asEndpoint = getASEndpointDictionary ()), Exit);
	FailIf (kIOReturnSuccess != (result = asEndpoint->getLockDelayUnits (lockDelayUnits)), Exit);

Exit:
	return result;

}

bool AUAStreamDictionary::asEndpointHasPitchControl ()
{
	AUAASEndpointDictionary *	asEndpoint = NULL;
	bool						result = false;
	
	FailIf (NULL == (asEndpoint = getASEndpointDictionary ()), Exit);
	FailIf (kIOReturnSuccess != asEndpoint->hasPitchControl (&result), Exit);

Exit:
	return result;
}

bool AUAStreamDictionary::asEndpointHasSampleFreqControl ()
{
	AUAASEndpointDictionary *	asEndpoint = NULL;
	bool						result = false;
	
	FailIf (NULL == (asEndpoint = getASEndpointDictionary ()), Exit);
	FailIf (kIOReturnSuccess != asEndpoint->hasSampleFreqControl (&result), Exit);

Exit:
	return result;
}

#pragma mark AUAEndpointDictionary

//    AUAEndpointDictionary

OSDefineMetaClassAndStructors (AUAEndpointDictionary, AppleUSBAudioDictionary);

AUAEndpointDictionary * AUAEndpointDictionary::create (void) 
{
    AUAEndpointDictionary *		endpointDictionary = NULL;

    FailIf (NULL == (endpointDictionary = new AUAEndpointDictionary), Exit);
	RELEASE_IF_FALSE (endpointDictionary, endpointDictionary->initDictionaryForUse ());

Exit:
    return endpointDictionary;
}

IOReturn AUAEndpointDictionary::getDirection (UInt8 * direction)
{
	IOReturn		result = kIOReturnError;
	UInt8			address;
	
	FailIf (NULL == direction, Exit);
	FailIf (kIOReturnSuccess != (result = getAddress (&address)), Exit);
	* direction = (address & 0x80) >> 7;
	result = kIOReturnSuccess;
Exit:
	return result;
}

IOReturn AUAEndpointDictionary::getSyncType (UInt8 * syncType) 
{
	IOReturn		result = kIOReturnError;
	UInt8			attributes;
	
	FailIf ( NULL == syncType, Exit );
	FailIf ( kIOReturnSuccess != ( result = getAttributes ( &attributes ) ), Exit );
	* syncType = ( attributes & 0xC ) >> 2;

Exit:
	return result;
}

// For USB 2.0 Audio Class

bool AUAEndpointDictionary::isIsocStreaming ()
{
	bool			result = false;
	UInt8			attributes;
	
	FailIf ( kIOReturnSuccess != getAttributes ( &attributes ), Exit );
	
	result = ( 0x1 == ( 0x1 & attributes ) ) && ( 0 != ( ( attributes >> 2 ) & 0x3 ) ) && ( 0x1 != ( ( attributes >> 4 ) & 0x3 ) );
Exit:
	return result;
}

bool AUAEndpointDictionary::isIsocFeedback ()
{
	bool			result = false;
	UInt8			attributes;
	
	FailIf ( kIOReturnSuccess != getAttributes ( &attributes ), Exit );
	
	result = ( 0x1 == ( 0x1 & attributes ) ) && ( 0 == ( ( attributes >> 2 ) & 0x3 ) ) && ( 0x1 == ( ( attributes >> 4 ) & 0x3 ) );

Exit:
	return result;
}

#pragma mark AUAASEndpointDictionary

// AUAASEndpointDictionary

AUAASEndpointDictionary::AUAASEndpointDictionary (bool theSampleFreqControl, bool thePitchControl, bool theMaxPacketsOnly, UInt8 theLockDelayUnits, UInt16 theLockDelay)
{
	bool	result = false;
	FailIf (false == (result = initDictionaryForUse ()), Exit);
    
	setDictionaryValue (kHasSampleFreqControl, theSampleFreqControl);
	setDictionaryValue (kHasPitchControl, thePitchControl);
	setDictionaryValue (kHasMaxPacketsOnly, theMaxPacketsOnly);
	setDictionaryValue (kLockDelayUnits, theLockDelayUnits);
	setDictionaryValue (kLockDelay, theLockDelay);
	
Exit:
	debugIOLog ("? AUAASEndpointDictionary::AUAASEndpointDictionary (%d, %d, %d, %d, %d) = %d", theSampleFreqControl, thePitchControl, theMaxPacketsOnly, theLockDelayUnits, theLockDelay, result);
}

#pragma mark AUAInputTerminalDictionary

// AUAInputTerminalDictionary

#pragma mark AUAOutputTerminalDictionary

// AUAOutputTerminalDictionary

#pragma mark AUAMixerUnitDictionary

// AUAMixerUnitDictionary

void AUAMixerUnitDictionary::initControlsArray (UInt8 * bmCntrls, UInt8 bmControlSize) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (controlsArray = OSArray::withCapacity (bmControlSize)), Exit); 
	
	for (UInt8 controlNumber = 0; controlNumber < bmControlSize; controlNumber++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmCntrls[controlNumber], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
}

void AUAMixerUnitDictionary::initSourceIDs (UInt8 * baSrcIDs, UInt8 nrInPins) 
{
	OSArray *		sourceIDs = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (sourceIDs = OSArray::withCapacity (nrInPins)), Exit);
	
	for (UInt8 sourceIndex = 0; sourceIndex < nrInPins; sourceIndex++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (baSrcIDs[sourceIndex], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (sourceIDs->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kSourceIDs, sourceIDs), Exit);
	sourceIDs = NULL;

Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != sourceIDs)
	{
		sourceIDs->release ();
		sourceIDs = NULL;
	}

	return;
}

#pragma mark AUASelectorUnitDictionary

//    AUASelectorUnitDictionary

void AUASelectorUnitDictionary::initSourceIDs (UInt8 * baSrcIDs, UInt8 nrInPins) 
{
	OSArray *		sourceIDs = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (sourceIDs = OSArray::withCapacity (nrInPins)), Exit);
	
	for (UInt8 sourceIndex = 0; sourceIndex < nrInPins; sourceIndex++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (baSrcIDs[sourceIndex], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (sourceIDs->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kSourceIDs, sourceIDs), Exit);
	sourceIDs = NULL;

Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != sourceIDs)
	{
		sourceIDs->release ();
		sourceIDs = NULL;
	}

	return;
}

#pragma mark AUAEffectUnitDictionary

//    AUAEffectUnitDictionary

void AUAEffectUnitDictionary::initControlsArray (UInt8 * bmaControlsArray, UInt8 numCntrls) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	UInt32 *		bmaControlsArray_32 = NULL;
	UInt8			controlSize;
	
	debugIOLog ("+ AUAEffectUnitDictionary[%p]::initControlsArray (%p, %d)", this, bmaControlsArray, numCntrls);
	FailIf (kIOReturnSuccess != setDictionaryValue (kNumControls, numCntrls), Exit);
	FailIf (kIOReturnSuccess != getControlSize (&controlSize), Exit);
	FailIf (NULL == (controlsArray = OSArray::withCapacity(numCntrls)), Exit); 
	bmaControlsArray_32 = (UInt32 *) bmaControlsArray;
	
	for (UInt8 controlIndex = 0; controlIndex < numCntrls; controlIndex++)
	{
		if (1 == controlSize)
		{
			FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmaControlsArray[controlIndex], SIZEINBITS(UInt8))), Exit);
		}
		else
		{
			FailIf (NULL == (arrayNumber = OSNumber::withNumber (USBToHostWord (bmaControlsArray_32[controlIndex]), controlSize * SIZEINBITS(UInt8))), Exit);
		}
		
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
	debugIOLog ("- AUAEffectUnitDictionary[%p]::initControlsArray (%p, %d)", this, bmaControlsArray, numCntrls);
}

#pragma mark AUAProcessingUnitDictionary

//    AUAProcessingUnitDictionary

void AUAProcessingUnitDictionary::initSourceIDs (UInt8 * baSrcIDs, UInt8 nrInPins) 
{
	OSArray *		sourceIDs = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (sourceIDs = OSArray::withCapacity (nrInPins)), Exit);
	
	for (UInt8 sourceIndex = 0; sourceIndex < nrInPins; sourceIndex++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (baSrcIDs[sourceIndex], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (sourceIDs->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kSourceIDs, sourceIDs), Exit);
	sourceIDs = NULL;

Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != sourceIDs)
	{
		sourceIDs->release ();
		sourceIDs = NULL;
	}

	return;
}

void AUAProcessingUnitDictionary::initControlsArray (UInt8 * bmCntrls, UInt8 bmControlSize) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (controlsArray = OSArray::withCapacity (bmControlSize)), Exit); 
	
	for (UInt8 controlNumber = 0; controlNumber < bmControlSize; controlNumber++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmCntrls[controlNumber], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
}

#pragma mark AUAFeatureUnitDictionary

//    AUAFeatureUnitDictionary

bool AUAFeatureUnitDictionary::masterHasMuteControl (void) 
{
	return channelHasMuteControl (0);		// Master channel is always bmaControls[0]
}

// Channel #1 is left channel, #2 is right channel
bool AUAFeatureUnitDictionary::channelHasMuteControl (UInt8 channelNum) 
{
	OSArray *		controls = NULL;
	OSObject *		arrayValue = NULL;
	OSNumber *		controlNumber = NULL;
	UInt8			numControls;
	UInt8			controlSize;
	bool			result = false;

	FailIf (NULL == (controls = getControls ()), Exit);
	FailIf (kIOReturnSuccess != getNumControls (&numControls), Exit);
	
	if (numControls >= channelNum + 1) 
	{
		FailIf (kIOReturnSuccess != getControlSize (&controlSize), Exit);
		FailIf (NULL == (arrayValue = controls->getObject (channelNum)), Exit);
		FailIf (NULL == (controlNumber = OSDynamicCast (OSNumber, arrayValue)), Exit);
		
		if (1 == controlSize) 
		{
			result = controlNumber->unsigned8BitValue () & (1 << kMuteBit);
		} 
		else 
		if (2 == controlSize) 
		{
			result = controlNumber->unsigned16BitValue () & (1 << kMuteBit);
		}
		else
		//if (4 == controlSize) 
		{
			result = (controlNumber->unsigned32BitValue () & (0x3 << (kMuteBit*2))) == (0x3 << (kMuteBit*2)); // must be present & programmable
		}
	}
	
Exit:
	return result;
}

bool AUAFeatureUnitDictionary::channelHasVolumeControl (UInt8 channelNum) 
{
	OSArray *		controls = NULL;
	OSObject *		arrayValue = NULL;
	OSNumber *		controlNumber = NULL;
	UInt8			numControls;
	UInt8			controlSize;
	bool			result = false;

	FailIf (NULL == (controls = getControls ()), Exit);
	FailIf (kIOReturnSuccess != getNumControls (&numControls), Exit);
	
	if (numControls >= channelNum + 1) 
	{
		FailIf (kIOReturnSuccess != getControlSize (&controlSize), Exit);
		FailIf (NULL == (arrayValue = controls->getObject (channelNum)), Exit);
		FailIf (NULL == (controlNumber = OSDynamicCast (OSNumber, arrayValue)), Exit);
		
		if (1 == controlSize) 
		{
			result = controlNumber->unsigned8BitValue () & (1 << kVolumeBit);
		} 
		else 
		if (2 == controlSize) 
		{
			result = controlNumber->unsigned16BitValue () & (1 << kVolumeBit);
		}
		else
		//if (4 == controlSize) 
		{
			result = (controlNumber->unsigned32BitValue () & (0x3 << (kVolumeBit*2))) == (0x3 << (kVolumeBit*2)); // must be present & programmable
		}
	}
	
Exit:
	debugIOLog ("? AUAFeatureUnitDictionary::channelHasVolumeControl ( %d ) = %s", channelNum, (result ? "true" : "false" ) );
	return result;
}

void AUAFeatureUnitDictionary::initControlsArray (UInt8 * bmaControlsArray, UInt8 numCntrls) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	UInt16 *		bmaControlsArray_16 = NULL;
	UInt32 *		bmaControlsArray_32 = NULL;
	UInt8			controlSize;
	
	debugIOLog ("+ AUAFeatureUnit[%p]::initControlsArray (%p, %d)", this, bmaControlsArray, numCntrls);
	FailIf (kIOReturnSuccess != setDictionaryValue (kNumControls, numCntrls), Exit);
	FailIf (kIOReturnSuccess != getControlSize (&controlSize), Exit);
	FailIf (NULL == (controlsArray = OSArray::withCapacity(numCntrls)), Exit); 
	bmaControlsArray_16 = (UInt16 *) bmaControlsArray;
	bmaControlsArray_32 = (UInt32 *) bmaControlsArray;
	
	for (UInt8 controlIndex = 0; controlIndex < numCntrls; controlIndex++)
	{
		if (1 == controlSize)
		{
			FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmaControlsArray[controlIndex], SIZEINBITS(UInt8))), Exit);
		}
		else
		if (2 == controlSize)
		{
			FailIf (NULL == (arrayNumber = OSNumber::withNumber (USBToHostWord (bmaControlsArray_16[controlIndex]), controlSize * SIZEINBITS(UInt8))), Exit);
		}
		else
		{
			FailIf (NULL == (arrayNumber = OSNumber::withNumber (USBToHostLong (bmaControlsArray_32[controlIndex]), controlSize * SIZEINBITS(UInt8))), Exit);
		}
		
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
	debugIOLog ("- AUAFeatureUnit[%p]::initControlsArray (%p, %d)", this, bmaControlsArray, numCntrls);
}

#pragma mark AUAExtensionUnitDictionary

 // AUAExtensionUnitDictionary

void AUAExtensionUnitDictionary::initControlsArray (UInt8 * bmCntrls, UInt8 bmControlSize) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (controlsArray = OSArray::withCapacity (bmControlSize)), Exit); 
	
	for (UInt8 controlNumber = 0; controlNumber < bmControlSize; controlNumber++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmCntrls[controlNumber], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
}

void AUAExtensionUnitDictionary::initSourceIDs (UInt8 * baSrcIDs, UInt8 nrInPins) {
	OSArray *		sourceIDs = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (sourceIDs = OSArray::withCapacity (nrInPins)), Exit);
	
	for (UInt8 sourceIndex = 0; sourceIndex < nrInPins; sourceIndex++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (baSrcIDs[sourceIndex], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (sourceIDs->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kSourceIDs, sourceIDs), Exit);
	sourceIDs = NULL;

Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != sourceIDs)
	{
		sourceIDs->release ();
		sourceIDs = NULL;
	}

	return;
}

# pragma mark AUAClockSourceDictionary

// AUAClockSourceDictionary

IOReturn AUAClockSourceDictionary::getClockType (UInt8 * clockType)
{
	IOReturn		result = kIOReturnError;
	UInt8			attributes;
	
	FailIf ( NULL == clockType, Exit );
	FailIf ( kIOReturnSuccess != ( result = getAttributes ( &attributes ) ), Exit );
	* clockType = ( attributes & 0x3 );

Exit:
	return result;
}

IOReturn AUAClockSourceDictionary::getControlBitmap (UInt8 * bmControls)
{
	IOReturn		result = kIOReturnError;
	OSArray *		controls = NULL;
	OSNumber *		bitmapNumber = NULL;
	
	FailIf ( NULL == bmControls, Exit );
	FailIf ( NULL == ( controls = getControls () ), Exit );
	FailIf ( NULL == ( bitmapNumber = OSDynamicCast ( OSNumber, controls->getObject ( 0 ) ) ), Exit );
	* bmControls = bitmapNumber->unsigned8BitValue ();
	
	result = kIOReturnSuccess;
Exit:
	return result;
}

void AUAClockSourceDictionary::initControlsArray (UInt8 * bmCntrls, UInt8 bmControlSize) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (controlsArray = OSArray::withCapacity (bmControlSize)), Exit); 
	
	for (UInt8 controlNumber = 0; controlNumber < bmControlSize; controlNumber++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmCntrls[controlNumber], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
}

# pragma mark AUAClockSelectorDictionary

// AUAClockSelectorDictionary

void AUAClockSelectorDictionary::initControlsArray (UInt8 * bmCntrls, UInt8 bmControlSize) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (controlsArray = OSArray::withCapacity (bmControlSize)), Exit); 
	
	for (UInt8 controlNumber = 0; controlNumber < bmControlSize; controlNumber++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmCntrls[controlNumber], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
}

void AUAClockSelectorDictionary::initClockSourceIDs (UInt8 * baCSrcIDs, UInt8 nrInPins) {
	OSArray *		cSourceIDs = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (cSourceIDs = OSArray::withCapacity (nrInPins)), Exit);
	
	for (UInt8 cSourceIndex = 0; cSourceIndex < nrInPins; cSourceIndex++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (baCSrcIDs[cSourceIndex], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (cSourceIDs->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kCSourceIDs, cSourceIDs), Exit);
	cSourceIDs = NULL;

Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != cSourceIDs)
	{
		cSourceIDs->release ();
		cSourceIDs = NULL;
	}

	return;
}

# pragma mark AUAClockMultiplierDictionary

// AUAClockMultiplierDictionary

void AUAClockMultiplierDictionary::initControlsArray (UInt8 * bmCntrls, UInt8 bmControlSize) 
{
	OSArray *		controlsArray = NULL;
	OSNumber *		arrayNumber = NULL;
	
	FailIf (NULL == (controlsArray = OSArray::withCapacity (bmControlSize)), Exit); 
	
	for (UInt8 controlNumber = 0; controlNumber < bmControlSize; controlNumber++)
	{
		FailIf (NULL == (arrayNumber = OSNumber::withNumber (bmCntrls[controlNumber], SIZEINBITS(UInt8))), Exit);
		FailIf (false == (controlsArray->setObject (arrayNumber)), Exit);
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	
	FailIf (kIOReturnSuccess != setDictionaryObjectAndRelease (kControlsArray, controlsArray), Exit);
	controlsArray = NULL;
	
Exit:
	if (NULL != arrayNumber)
	{
		arrayNumber->release ();
		arrayNumber = NULL;
	}
	if (NULL != controlsArray)
	{
		controlsArray->release ();
		controlsArray = NULL;
	}
}

# pragma mark AUAUnitDictionary

//    AUAUnitDictionary

OSDefineMetaClassAndStructors (AUAUnitDictionary, AppleUSBAudioDictionary);


