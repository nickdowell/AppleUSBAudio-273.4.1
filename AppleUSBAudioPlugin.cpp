/*
 * Copyright (c) 1998-2008 Apple Computer, Inc. All rights reserved.
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

#include "AppleUSBAudioPlugin.h"
#include "AppleUSBAudioEngine.h"

#include <IOKit/IOLib.h>

#define super IOService

OSDefineMetaClassAndStructors (AppleUSBAudioPlugin, IOService)

OSMetaClassDefineReservedUsed(AppleUSBAudioPlugin, 0);
OSMetaClassDefineReservedUsed(AppleUSBAudioPlugin, 1);

OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 2);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 3);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 4);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 5);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 6);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 7);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 8);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 9);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 10);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 11);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 12);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 13);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 14);
OSMetaClassDefineReservedUnused(AppleUSBAudioPlugin, 15);

// Standard IOService.h function methods
bool AppleUSBAudioPlugin::start (IOService * provider) {
	if (!super::start (provider)) 
	{
		return FALSE;
	}

	reserved = (ExpansionData *)IOMalloc (sizeof(struct ExpansionData));
	if (!reserved) {
		return FALSE;
	}

	reserved->streamProvider = OSDynamicCast (AppleUSBAudioStream, provider);
	
	if (reserved->streamProvider)
	{
		// Tell AppleUSBAudioStream that we're loaded
		reserved->streamProvider->registerPlugin (this);
	}
	else
	{
		mOurProvider = OSDynamicCast (AppleUSBAudioEngine, provider);

		if (mOurProvider)
		{
			// Tell AppleUSBAudioEngine that we're loaded
			mOurProvider->registerPlugin (this);
		}
	}
	
	return TRUE;
}

void AppleUSBAudioPlugin::stop (IOService * provider) {
	// Tell the system that we're not an available resource anymore
	publishResource ("AppleUSBAudioPlugin", NULL);

	if (reserved) {
		IOFree (reserved, sizeof(struct ExpansionData));
	}

	super::stop (provider);
}

IOReturn AppleUSBAudioPlugin::pluginDeviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion) {
	IOReturn						result;

	result = kIOReturnError;
	if (reserved && reserved->streamProvider)
	{
		result = reserved->streamProvider->pluginDeviceRequest (request, completion);
	}
	else if (mOurProvider) 
	{
		result = mOurProvider->pluginDeviceRequest (request, completion);
	}

	return result;
}

void AppleUSBAudioPlugin::pluginSetConfigurationApp (const char * bundleID) {
	if (reserved && reserved->streamProvider)
	{
		reserved->streamProvider->pluginSetConfigurationApp (bundleID);
	}
	else if (mOurProvider) 
	{
		mOurProvider->pluginSetConfigurationApp (bundleID);
	}
}

// Methods that the plugin will override
IOReturn AppleUSBAudioPlugin::pluginInit (IOService * provider, UInt16 vendorID, UInt16 modelID) {
	return kIOReturnSuccess;
}

// OSMetaClassDefineReservedUsed(AppleUSBAudioPlugin, 1);
IOReturn AppleUSBAudioPlugin::pluginSetDirection (IOAudioStreamDirection direction) {
	return kIOReturnSuccess;
}

IOReturn AppleUSBAudioPlugin::pluginStart () {
	return kIOReturnSuccess;
}

IOReturn AppleUSBAudioPlugin::pluginSetFormat (const IOAudioStreamFormat * const newFormat, const IOAudioSampleRate * const newSampleRate) {
	return kIOReturnSuccess;
}

IOReturn AppleUSBAudioPlugin::pluginReset () {
	return kIOReturnSuccess;
}

IOReturn AppleUSBAudioPlugin::pluginProcess (float * mixBuf, UInt32 numSampleFrames, UInt32 numChannels) {
	return kIOReturnSuccess;
}

// OSMetaClassDefineReservedUsed(AppleUSBAudioPlugin, 0);
IOReturn AppleUSBAudioPlugin::pluginProcessInput (float * destBuf, UInt32 numSampleFrames, UInt32 numChannels) {
	return kIOReturnSuccess;
}

IOReturn AppleUSBAudioPlugin::pluginStop () {
	return kIOReturnSuccess;
}
