#ifndef __BIGNUM_H__
#define __BIGNUM_H__

#include <libkern/OSTypes.h>
#include <libkern/libkern.h>

typedef UInt64	U64;

typedef struct 
{
	#ifdef __BIG_ENDIAN__
	UInt64		hi;
	UInt64		lo;
	#else // little-endian
	UInt64		lo;
	UInt64		hi;
	#endif
} U128;

typedef struct
{
	#ifdef __BIG_ENDIAN__
	U128		hi;
	U128		lo;
	#else // little-endian
	U128		lo;
	U128		hi;
	#endif
} U256;

typedef struct
{
	#ifdef __BIG_ENDIAN__
	U256		hi;
	U256		lo;
	#else // little-endian
	U256		lo;
	U256		hi;
	#endif
} U512;

typedef struct
{
	#ifdef __BIG_ENDIAN__
	U512		hi;
	U512		lo;
	#else // little-endian
	U512		lo;
	U512		hi;
	#endif
} U1024;

#pragma mark -Comparison Operations-

SInt32 cmp128 ( U128 A,  U128 B );
SInt32 cmp256 ( U256 A,  U256 B );
SInt32 cmp512 ( U512 A,  U512 B );

bool eq128 ( U128 A, U128 B );
bool eq256 ( U256 A, U256 B );
bool eq512 ( U512 A, U512 B );

bool lt128 ( U128 A, U128 B );
bool lt256 ( U256 A, U256 B );
bool lt512 ( U512 A, U512 B );

bool gt128 ( U128 A, U128 B );
bool gt256 ( U256 A, U256 B );
bool gt512 ( U512 A, U512 B );

#pragma mark -Shift Operations-

void shl128 ( U128 * A );
void shl256 ( U256 * A );
void shl512 ( U512 * A );
void shl1024 ( U1024 * A );

void shr128 ( U128 * A );
void shr256 ( U256 * A );
void shr512 ( U512 * A );
void shr1024 ( U1024 * A );

#pragma mark -Increment Operations-

void inc128 ( U128 * A );
void inc256 ( U256 * A );

#pragma mark -Decrement Operations-

void dec128 ( U128 * A );
void dec256 ( U256 * A );

#pragma mark -Addition Operations-

U128 add128 ( U128 A,  U128 B );
U256 add256 ( U256 A,  U256 B );
U512 add512 ( U512 A,  U512 B );

#pragma mark -Subtraction Operations-

U128 sub128 ( U128 A,  U128 B ); // assumes A >= B
U256 sub256 ( U256 A,  U256 B ); // assumes A >= B
U256 sub256 ( U256 A,  U128 B ); // assumes A >= B
U512 sub512 ( U512 A,  U512 B ); // assumes A >= B

#pragma mark -Multiplication Operations-

U128 mul64 ( U64 A, U64 B );
U256 mul128 ( U128 A, U128 B );
U256 mul128 ( U128 A, U64 B );
U256 mul128 ( U64 A, U128 B );
U512 mul256 ( U256 A, U256 B );
U512 mul256 ( U256 A, U128 B );
U512 mul256 ( U128 A, U256 B );
U512 mul256 ( U256 A, U64 B );
U512 mul256 ( U64 A, U256 B );

#pragma mark -Division Operations-

U128 div128 ( U128 N, U128 D );
U128 div128 ( U128 N, U64 D );
U256 div256 ( U256 N, U256 D );
U256 div256 ( U256 N, U128 D );
U512 div512 ( U512 N, U512 D );
U512 div512 ( U512 N, U256 D );

#endif //__BIGNUM_H__