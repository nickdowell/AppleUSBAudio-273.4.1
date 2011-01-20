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

#ifndef __APPLEUSBAUDIOPLUGIN__
#define __APPLEUSBAUDIOPLUGIN__

#include <IOKit/IOService.h>
#include <IOKit/audio/IOAudioStream.h>
#include <IOKit/usb/USB.h>

class AppleUSBAudioEngine;
class AppleUSBAudioStream;

class AppleUSBAudioPlugin : public IOService {

	OSDeclareDefaultStructors (AppleUSBAudioPlugin)

private:
	AppleUSBAudioEngine *			mOurProvider;

protected:
	struct ExpansionData { 
		AppleUSBAudioStream *	streamProvider;
	};

	ExpansionData *reserved;

public:
	// OSMetaClassDeclareReservedUsed (AppleUSBAudioPlugin, 0);
	virtual IOReturn	pluginProcessInput (float * destBuf, UInt32 numSampleFrames, UInt32 numChannels);
	// OSMetaClassDeclareReservedUsed (AppleUSBAudioPlugin, 1);
	virtual IOReturn	pluginSetDirection (IOAudioStreamDirection direction);

private:
	OSMetaClassDeclareReservedUsed (AppleUSBAudioPlugin, 0);
	OSMetaClassDeclareReservedUsed (AppleUSBAudioPlugin, 1);

	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 2);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 3);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 4);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 5);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 6);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 7);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 8);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 9);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 10);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 11);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 12);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 13);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 14);
	OSMetaClassDeclareReservedUnused (AppleUSBAudioPlugin, 15);

public:
	virtual	bool		start (IOService * provider);
	virtual	void		stop (IOService * provider);

	IOReturn			pluginDeviceRequest (IOUSBDevRequest * request, IOUSBCompletion * completion = NULL);
	void				pluginSetConfigurationApp (const char * bundleID);

	virtual	IOReturn	pluginInit (IOService * provider, UInt16 vendorID, UInt16 modelID);
	virtual	IOReturn	pluginStart ();
	virtual	IOReturn	pluginReset ();
	virtual	IOReturn	pluginSetFormat (const IOAudioStreamFormat * const newFormat, const IOAudioSampleRate * const newSampleRate);
	virtual	IOReturn	pluginProcess (float * mixBuf, UInt32 numSampleFrames, UInt32 numChannels);
	virtual	IOReturn	pluginStop ();
};

#endif
