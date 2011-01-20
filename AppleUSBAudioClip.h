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
 
#ifndef _APPLEUSBAUDIOCLIP_H
#define _APPLEUSBAUDIOCLIP_H

#include <libkern/OSTypes.h>

extern "C" {
//	floating point types
typedef	float				Float32;
typedef double				Float64;

UInt32 CalculateOffset (UInt64 nanoseconds, UInt32 sampleRate);

IOReturn	clipAppleUSBAudioToOutputStream (const void *mixBuf,
											void *sampleBuf,
											UInt32 firstSampleFrame,
											UInt32 numSampleFrames,
											const IOAudioStreamFormat *streamFormat);

IOReturn	convertFromAppleUSBAudioInputStream_NoWrap (const void *sampleBuf,
														void *destBuf,
														UInt32 firstSampleFrame,
														UInt32 numSampleFrames,
														const IOAudioStreamFormat *streamFormat);
}

#endif
