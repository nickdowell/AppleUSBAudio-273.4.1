#include "BigNum.h"

#pragma mark -Comparison Operations-

SInt32 cmp128 ( U128 A,  U128 B )
{
	SInt32		result;
	
	if ( ( A.hi == B.hi ) && ( A.lo == B.lo ) ) // A == B
	{
		result = 0;
	}
	else if ( ( A.hi > B.hi ) || ( ( A.hi == B.hi ) && ( A.lo > B.lo ) ) ) // A > B
	{
		result = 1;
	}
	else // A < B
	{
		result = -1;
	}
	
	return result;
}

SInt32 cmp256 ( U256 A,  U256 B )
{
	SInt32		result;
	
	if ( eq128 ( A.hi, B.hi ) && eq128 ( A.lo, B.lo ) ) // A == B
	{
		result = 0;
	}
	else if ( gt128 ( A.hi, B.hi ) || ( eq128 ( A.hi, B.hi ) && gt128 ( A.lo, B.lo ) ) ) // A > B
	{
		result = 1;
	}
	else // A < B
	{
		result = -1;
	}
	
	return result;
}

SInt32 cmp512 ( U512 A,  U512 B )
{
	SInt32		result;
	
	if ( eq256 ( A.hi, B.hi ) && eq256 ( A.lo, B.lo ) ) // A == B
	{
		result = 0;
	}
	else if ( gt256 ( A.hi, B.hi ) || ( eq256 ( A.hi, B.hi ) && gt256 ( A.lo, B.lo ) ) ) // A > B
	{
		result = 1;
	}
	else // A < B
	{
		result = -1;
	}
	
	return result;
}

bool eq128 ( U128 A, U128 B )
{
	bool	result;
	
	result = ( cmp128 ( A, B ) == 0 );
	
	return result;
}

bool eq256 ( U256 A, U256 B )
{
	bool	result;
	
	result = ( cmp256 ( A, B ) == 0 );
	
	return result;
}

bool eq512 ( U512 A, U512 B )
{
	bool	result;
	
	result = ( cmp512 ( A, B ) == 0 );
	
	return result;
}

bool lt128 ( U128 A, U128 B )
{
	bool	result;
	
	result = ( cmp128 ( A, B ) < 0 );
	
	return result;
}

bool lt256 ( U256 A, U256 B )
{
	bool	result;
	
	result = ( cmp256 ( A, B ) < 0 );
	
	return result;
}

bool lt512 ( U512 A, U512 B )
{
	bool	result;
	
	result = ( cmp512 ( A, B ) < 0 );
	
	return result;
}

bool gt128 ( U128 A, U128 B )
{
	bool	result;
	
	result = ( cmp128 ( A, B ) > 0 );
	
	return result;
}

bool gt256 ( U256 A, U256 B )
{
	bool	result;
	
	result = ( cmp256 ( A, B ) > 0 );
	
	return result;
}

bool gt512 ( U512 A, U512 B )
{
	bool	result;
	
	result = ( cmp512 ( A, B ) > 0 );
	
	return result;
}

#pragma mark -Shift Operations-

#define MSB64	0x8000000000000000ULL

void shl128 ( U128 * A )
{
	A->hi <<= 1;
	A->hi |= ( ( A->lo & MSB64 ) ? 1 : 0 );
	A->lo <<= 1;
}

void shl256 ( U256 * A )
{
	shl128 ( &A->hi );
	A->hi.lo |= ( ( A->lo.hi & MSB64 ) ? 1 : 0 );
	shl128 ( &A->lo );
}

void shl512 ( U512 * A )
{
	shl256 ( &A->hi );
	A->hi.lo.lo |= ( ( A->lo.hi.hi & MSB64 ) ? 1 : 0 );
	shl256 ( &A->lo );
}

void shl1024 ( U1024 * A )
{
	shl512 ( &A->hi );
	A->hi.lo.lo.lo |= ( ( A->lo.hi.hi.hi & MSB64 ) ? 1 : 0 );
	shl512 ( &A->lo );
}

void shr128 ( U128 * A )
{
	A->lo >>= 1;
	A->lo |= ( ( A->hi & 1 ) ? MSB64 : 0 );
	A->hi >>= 1;
}

void shr256 ( U256 * A )
{
	shr128 ( &A->lo );
	A->lo.hi |= ( ( A->hi.lo & 1 ) ? MSB64 : 0 );
	shr128 ( &A->hi );
}

void shr512 ( U512 * A )
{
	shr256 ( &A->lo );
	A->lo.hi.hi |= ( ( A->hi.lo.lo & 1 ) ? MSB64 : 0 );
	shr256 ( &A->hi );
}

void shr1024 ( U1024 * A )
{
	shr512 ( &A->lo );
	A->lo.hi.hi.hi |= ( ( A->hi.lo.lo.lo & 1 ) ? MSB64 : 0 );
	shr512 ( &A->hi );
}

#pragma mark -Increment Operations-

void inc128 ( U128 * A )
{
	A->lo++;
	
	if ( 0 == A->lo ) // carry ?
	{
		A->hi++;
	}
}

void inc256 ( U256 * A )
{
	inc128 ( &A->lo );
	
	if ( ( 0 == A->lo.hi ) && ( 0 == A->lo.lo ) ) // carry ?
	{
		inc128 ( &A->hi );
	}
}

#pragma mark -Decrement Operations-

void dec128 ( U128 * A )
{
	if ( 0 == A->lo ) // borrow ?
	{
		A->hi--;
	}
	A->lo--;	
}

void dec256 ( U256 * A )
{
	if ( ( 0 == A->lo.hi ) && ( 0 == A->lo.lo ) ) // borrow ?
	{
		dec128 ( &A->hi );
	}
	dec128 ( &A->lo );
}

#pragma mark -Addition Operations-

U128 add128 ( U128 A,  U128 B )
{
	U128	result;
	
	result.hi = A.hi + B.hi;
	result.lo = A.lo + B.lo;
	
	if ( ( result.lo < A.lo ) || ( result.lo < B.lo ) ) // carry ?
	{
		result.hi++;
	}
	
	return result;
}

U256 add256 ( U256 A,  U256 B )
{
	U256	result;
	
	result.hi = add128 ( A.hi, B.hi );
	result.lo = add128 ( A.lo, B.lo );
	
	if ( lt128 ( result.lo, A.lo ) || lt128 ( result.lo, B.lo ) ) // carry ?
	{
		inc128 ( &result.hi );
	}
	
	return result;
}

U512 add512 ( U512 A,  U512 B )
{
	U512	result;
	
	result.hi = add256 ( A.hi, B.hi );
	result.lo = add256 ( A.lo, B.lo );
	
	if ( lt256 ( result.lo, A.lo ) || lt256 ( result.lo, B.lo ) ) // carry ?
	{
		inc256 ( &result.hi );
	}
	
	return result;
}

#pragma mark -Subtraction Operations-

U128 sub128 ( U128 A,  U128 B ) // assumes A >= B
{
	U128	result;
	
	result.hi = A.hi - B.hi;
	result.lo = A.lo - B.lo;
	
	if ( result.lo > A.lo ) // borrow ?
	{
		result.hi--;
	}
	
	return result;
}

U256 sub256 ( U256 A,  U256 B ) // assumes A >= B
{
	U256	result;
	
	result.hi = sub128 ( A.hi, B.hi );
	result.lo = sub128 ( A.lo, B.lo );
	
	if ( gt128 ( result.lo, A.lo ) ) // borrow ?
	{
		dec128 ( &result.hi );
	}
	
	return result;
}

U256 sub256 ( U256 A,  U128 B ) // assumes A >= B
{
	U256 B_; B_.hi.hi = B_.hi.lo = 0; B_.lo = B;
	
	return sub256 ( A, B_ );
}

U512 sub512 ( U512 A,  U512 B ) // assumes A >= B
{
	U512	result;
	
	result.hi = sub256 ( A.hi, B.hi );
	result.lo = sub256 ( A.lo, B.lo );
	
	if ( gt256 ( result.lo, A.lo ) ) // borrow ?
	{
		dec256 ( &result.hi );
	}
	
	return result;
}

#pragma mark -Multiplication Operations-

U128 mul64 ( U64 A, U64 B )
{
	U128	result;
	
	// Karatsuba multiplication
	// Suppose we want to multiply two 2 numbers A * B, where A = a1 << 32 + a0, B = b1 << 32 + b0:
	// 1. compute a1 * b1, call the result X
	// 2. compute a0 * b0, call the result Y
	// 3. compute Z, this number is equal to a1 * b0 + a0 * b1.
	// 4. compute X << 64 + Z << 32 + Y
	
	UInt64 a1, a0, b1, b0;
	a1 = A >> 32;
	a0 = A - (a1 << 32);
	b1 = B >> 32;
	b0 = B - (b1 << 32);
	
	UInt64 X, Y, Z;
	X = a1 * b1;
	Y = a0 * b0;
	Z = a1 * b0 + a0 * b1;
	
	UInt64 z1, z0;
	z1 = Z >> 32;
	z0 = Z - (z1 << 32);
	
	U128 P, Q, R; 
	P.hi = X; P.lo = 0; // X << 64
	Q.hi = z1; Q.lo = (UInt64)(z0) << 32; // Z << 32
	R.hi = 0; R.lo = Y; // Y

	result = add128 ( P, Q );
	result = add128 ( result, R );
	
	return result;
}

U256 mul128 ( U128 A, U128 B )
{
	U256	result;
	
	// Karatsuba multiplication
	// Suppose we want to multiply two 2 numbers A * B, where A = a1 << 64 + a0, B = b1 << 64 + b0:
	// 1. compute a1 * b1, call the result X
	// 2. compute a0 * b0, call the result Y
	// 3. compute Z, this number is equal to a1 * b0 + a0 * b1.
	// 4. compute X << 128 + Z << 64 + Y
	
	U64 a1, a0, b1, b0;
	a1 = A.hi;
	a0 = A.lo;
	b1 = B.hi;
	b0 = B.lo;
	
	U128 X, Y, Z;
	X = mul64 ( a1, b1 ); // a1 * b1;
	Y = mul64 ( a0, b0 ); // a0 * b0;
	Z = add128 ( mul64 ( a1, b0 ), mul64 ( a0, b1 ) ); // a1 * b0 + a0 * b1;
	
	U128 zero; zero.hi = 0; zero.lo = 0;

	U256 P, Q, R;
	P.hi = X; P.lo = zero; // X << 128
	Q.hi.hi = 0; Q.hi.lo = Z.hi; Q.lo.hi = Z.lo; Q.lo.lo = 0; // Z << 64
	R.hi = zero; R.lo = Y; // Y

	result = add256 ( P, Q );
	result = add256 ( result, R );
	
	return result;
}

U256 mul128 ( U128 A, U64 B )
{
	U128 B_; B_.hi = 0; B_.lo = B;
	
	return mul128 ( A, B_ );
}

U256 mul128 ( U64 A, U128 B )
{
	U128 A_; A_.hi = 0; A_.lo = A;
	
	return mul128 ( A_, B );
}

U512 mul256 ( U256 A, U256 B )
{
	U512	result;
	
	// Karatsuba multiplication
	// Suppose we want to multiply two 2 numbers A * B, where A = a1 << 128 + a0, B = b1 << 128 + b0:
	// 1. compute a1 * b1, call the result X
	// 2. compute a0 * b0, call the result Y
	// 3. compute Z, this number is equal to a1 * b0 + a0 * b1.
	// 4. compute X << 256 + Z << 128 + Y
	
	U128 a1, a0, b1, b0;
	a1 = A.hi;
	a0 = A.lo;
	b1 = B.hi;
	b0 = B.lo;
	
	U256 X, Y, Z;
	X = mul128 ( a1, b1 ); // a1 * b1;
	Y = mul128 ( a0, b0 ); // a0 * b0;
	Z = add256 ( mul128 ( a1, b0 ), mul128 ( a0, b1 ) ); // a1 * b0 + a0 * b1;
	
	U256 zero; bzero ( &zero, sizeof ( U256 ) );

	U512 P, Q, R;
	P.hi = X; P.lo = zero; // X << 256
	Q.hi.hi = zero.lo; Q.hi.lo = Z.hi; Q.lo.hi = Z.lo; Q.lo.lo = zero.lo; // Z << 128
	R.hi = zero; R.lo = Y; // Y

	result = add512 ( P, Q );
	result = add512 ( result, R );
	
	return result;
}

U512 mul256 ( U256 A, U128 B )
{
	U256 B_; B_.hi.hi = B_.hi.lo = 0; B_.lo = B;
	
	return mul256 ( A, B_ );
}

U512 mul256 ( U128 A, U256 B )
{
	U256 A_; A_.hi.hi = A_.hi.lo = 0; A_.lo = A;
	
	return mul256 ( A_, B );
}

U512 mul256 ( U256 A, U64 B )
{
	U256 B_; B_.hi.hi = B_.hi.lo = 0; B_.lo.hi = 0; B_.lo.lo = B;
	
	return mul256 ( A, B_ );
}

U512 mul256 ( U64 A, U256 B )
{
	U256 A_; A_.hi.hi = A_.hi.lo = 0; A_.lo.hi = 0; A_.lo.lo = A;
	
	return mul256 ( A_, B );
}

#pragma mark -Division Operations-

U128 div128 ( U128 N, U128 D )
{
	// From http://en.wikipedia.org/wiki/Division_(digital):
	// The basic algorithm for binary (radix 2) restoring division is:
	// P := N
	// D := D << n              * P and D need twice the word width of N and Q
	// for i = n-1..0 do        * for example 31..0 for 32 bits
	//   P := 2P - D            * trial subtraction from shifted value
	//   if P >= 0 then
	//     q(i) := 1            * result-bit 1
	//   else
	//     q(i) := 0            * result-bit 0
	//     P := P + D           * new partial remainder is (restored) shifted value
	//   end
	// end
	//
	// where N=Numerator, D=Denominator, n=#bits, P=Partial remainder, q(i)=bit #i of quotient
	
	// In the above algorithm, P is twice the size of the N & Q. The remainder is stored in P.hi (only
	// 64-bits part of the P.hi is needed to store the remainder) while Q is stored in P.lo. 
	
	U256 P;
	bzero ( &P.hi, sizeof ( U128 ) );
	P.lo = N; // P := N
	
	// Don't shift D. Do the subtraction later with P.hi
	
	for ( UInt32 i = 0; i < 128; i++ )
	{
		shl256 ( &P ); // P := 2P
		
		// Only subtract D from P if P >= D. Otherwise, don't do it so that we don't have to perform the else case.
		if ( gt128 ( P.hi, D ) || eq128 ( P.hi, D ) ) // P >= D
		{
			P.hi = sub128 ( P.hi, D ); // P := P - D
			P.lo.lo |= 1; // result-bit 1
		}
	}
	
	return P.lo;
}

U128 div128 ( U128 N, U64 D )
{
	U128 D_;
	D_.hi = 0; D_.lo = D;
	
	return div128 ( N, D_ );
}

U256 div256 ( U256 N, U256 D )
{
	// From http://en.wikipedia.org/wiki/Division_(digital):
	// The basic algorithm for binary (radix 2) restoring division is:
	// P := N
	// D := D << n              * P and D need twice the word width of N and Q
	// for i = n-1..0 do        * for example 31..0 for 32 bits
	//   P := 2P - D            * trial subtraction from shifted value
	//   if P >= 0 then
	//     q(i) := 1            * result-bit 1
	//   else
	//     q(i) := 0            * result-bit 0
	//     P := P + D           * new partial remainder is (restored) shifted value
	//   end
	// end
	//
	// where N=Numerator, D=Denominator, n=#bits, P=Partial remainder, q(i)=bit #i of quotient
	
	// In the above algorithm, P is twice the size of the N & Q. The remainder is stored in P.hi (only
	// 128-bits part of the P.hi is needed to store the remainder) while Q is stored in P.lo. 
	
	U512 P;
	bzero ( &P.hi, sizeof ( U256 ) );
	P.lo = N; // P := N
	
	// Don't shift D. Do the subtraction later with P.hi
	
	for ( UInt32 i = 0; i < 256; i++ )
	{
		shl512 ( &P ); // P := 2P
		
		// Only subtract D from P if P >= D. Otherwise, don't do it so that we don't have to perform the else case.
		if ( gt256 ( P.hi, D ) || eq256 ( P.hi, D ) ) // P >= D
		{
			P.hi = sub256 ( P.hi, D ); // P := P - D
			P.lo.lo.lo |= 1; // result-bit 1
		}
	}
	
	return P.lo;
}

U256 div256 ( U256 N, U128 D )
{
	U256 D_;
	D_.hi.hi = D_.hi.lo = 0;
	D_.lo = D;
	
	return div256 ( N, D_ );
}

U512 div512 ( U512 N, U512 D )
{
	// From http://en.wikipedia.org/wiki/Division_(digital):
	// The basic algorithm for binary (radix 2) restoring division is:
	// P := N
	// D := D << n              * P and D need twice the word width of N and Q
	// for i = n-1..0 do        * for example 31..0 for 32 bits
	//   P := 2P - D            * trial subtraction from shifted value
	//   if P >= 0 then
	//     q(i) := 1            * result-bit 1
	//   else
	//     q(i) := 0            * result-bit 0
	//     P := P + D           * new partial remainder is (restored) shifted value
	//   end
	// end
	//
	// where N=Numerator, D=Denominator, n=#bits, P=Partial remainder, q(i)=bit #i of quotient
	
	// In the above algorithm, P is twice the size of the N & Q. The remainder is stored in P.hi (only
	// 256-bits part of the P.hi is needed to store the remainder) while Q is stored in P.lo. 
	
	U1024 P;
	bzero ( &P.hi, sizeof ( U512 ) );
	P.lo = N; // P := N
	
	// Don't shift D. Do the subtraction later with P.hi
	
	for ( UInt32 i = 0; i < 512; i++ )
	{
		shl1024 ( &P ); // P := 2P
		
		// Only subtract D from P if P >= D. Otherwise, don't do it so that we don't have to perform the else case.
		if ( gt512 ( P.hi, D ) || eq512 ( P.hi, D ) ) // P >= D
		{
			P.hi = sub512 ( P.hi, D ); // P := P - D
			P.lo.lo.lo.lo |= 1; // result-bit 1
		}
	}
	
	return P.lo;
}

U512 div512 ( U512 N, U256 D )
{
	U512 D_;
	bzero ( &D_.hi, sizeof ( U256 ) );
	D_.lo = D;
	
	return div512 ( N, D_ );
}