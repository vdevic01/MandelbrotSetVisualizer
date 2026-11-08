#pragma once

#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <climits>

namespace fpa {

	typedef unsigned int uint;
	typedef unsigned long long ulong;

	// Constants
	constexpr int WORD_BITS = sizeof(uint) * CHAR_BIT;
	static_assert(sizeof(ulong) == 2 * sizeof(uint), "ulong must be exactly twice as wide as uint");

	constexpr int WHOLE_PART = 1;
	constexpr int FRACTION_PART = 3;
	constexpr int WHOLE_BITS = WHOLE_PART * WORD_BITS;
	constexpr int FRACTION_BITS = FRACTION_PART * WORD_BITS;
	constexpr int FP_SIZE = WHOLE_PART + FRACTION_PART;
	constexpr int FP_MUL_BUFFER_SIZE = FP_SIZE * 2;
	constexpr int FP_DIV_BUFFER_SIZE = FP_SIZE + FRACTION_PART;

	// The word-level helpers below assume a single whole word.
	static_assert(WHOLE_PART == 1, "only a single whole word is supported");

	constexpr int SIGN_SHIFT = WORD_BITS - 1;             // bit position of the sign in the most significant word
	constexpr ulong WORD_MASK = (ulong(1) << WORD_BITS) - 1; // low WORD_BITS bits of an ulong
	constexpr double WORD_SCALE = (double)WORD_MASK + 1.0; // 2^WORD_BITS as a double (exact)

	// Function declarations
	void addFixed(const uint* a, const uint* b, uint c[FP_SIZE]);
	void incFixed(const uint* a, uint c[FP_SIZE]);
	void cmplFixed(const uint* a, uint c[FP_SIZE]);
	void subFixed(const uint* a, const uint* b, uint c[FP_SIZE]);
	bool gtFixed(const uint* a, const uint* b);
	bool gteFixed(const uint* a, const uint* b);
	void mulCmplFixed(const uint* a, const uint* b, uint c[FP_SIZE]);
	void divFixed(const uint* a, const uint* b, uint c[FP_SIZE]);

	void floatingToFixedPoint(double decimal, uint out[FP_SIZE]);
	double fixedToFloatingPoint(const uint fpNum[FP_SIZE]);

	void randomFromRange(const uint min[FP_SIZE], const uint max[FP_SIZE], uint result[FP_SIZE]);
} // namespace FixedPoint

#endif // FIXED_POINT_H
