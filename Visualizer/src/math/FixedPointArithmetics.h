#pragma once

#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <climits>

namespace fpa {

	typedef unsigned int uint;
	typedef unsigned long long ulong;

	// Word-level constants
	constexpr int WORD_BITS = sizeof(uint) * CHAR_BIT;
	static_assert(sizeof(ulong) == 2 * sizeof(uint), "ulong must be exactly twice as wide as uint");

	constexpr int SIGN_SHIFT = WORD_BITS - 1;             // bit position of the sign in the most significant word
	constexpr ulong WORD_MASK = (ulong(1) << WORD_BITS) - 1; // low WORD_BITS bits of an ulong
	constexpr double WORD_SCALE = (double)WORD_MASK + 1.0; // 2^WORD_BITS as a double (exact)

	// Number format: WHOLE_PART whole words followed by a configurable number of fraction words.
	// Numbers are stored most significant word first, in two's complement.
	constexpr int WHOLE_PART = 1;
	// The word-level helpers assume a single whole word.
	static_assert(WHOLE_PART == 1, "only a single whole word is supported");

	// Fraction sizes (in words) the library is instantiated for. Every function below takes the fraction
	// size as its last argument; passing anything outside [MIN_FRACTION_PART, MAX_FRACTION_PART] throws
	// std::invalid_argument. All arithmetic uses stack buffers sized at compile time, never the heap.
	constexpr int MIN_FRACTION_PART = 1;
	constexpr int MAX_FRACTION_PART = 6;
	constexpr int MAX_FP_SIZE = WHOLE_PART + MAX_FRACTION_PART; // use to size caller buffers that must fit any format

	// Default format, used when the fraction size argument is omitted (and by the GPU paths, which are fixed to it).
	constexpr int FRACTION_PART = 3;
	constexpr int FP_SIZE = WHOLE_PART + FRACTION_PART;
	constexpr int FP_MUL_BUFFER_SIZE = FP_SIZE * 2;
	constexpr int FP_DIV_BUFFER_SIZE = FP_SIZE + FRACTION_PART;

	constexpr bool isSupportedFractionPart(int fractionWords) {
		return fractionWords >= MIN_FRACTION_PART && fractionWords <= MAX_FRACTION_PART;
	}

	// Number of words of a number with the given fraction size (WHOLE_PART + fractionWords).
	constexpr int fpSize(int fractionWords) { return WHOLE_PART + fractionWords; }

	// Function declarations. All number arguments are arrays of fpSize(fractionWords) words.
	void addFixed(const uint* a, const uint* b, uint* c, int fractionWords = FRACTION_PART);
	void incFixed(const uint* a, uint* c, int fractionWords = FRACTION_PART);
	void cmplFixed(const uint* a, uint* c, int fractionWords = FRACTION_PART);
	void subFixed(const uint* a, const uint* b, uint* c, int fractionWords = FRACTION_PART);
	bool gtFixed(const uint* a, const uint* b, int fractionWords = FRACTION_PART);
	bool gteFixed(const uint* a, const uint* b, int fractionWords = FRACTION_PART);
	void mulCmplFixed(const uint* a, const uint* b, uint* c, int fractionWords = FRACTION_PART);
	void divFixed(const uint* a, const uint* b, uint* c, int fractionWords = FRACTION_PART);

	void floatingToFixedPoint(double decimal, uint* out, int fractionWords = FRACTION_PART);
	double fixedToFloatingPoint(const uint* fpNum, int fractionWords = FRACTION_PART);

	void randomFromRange(const uint* min, const uint* max, uint* result, int fractionWords = FRACTION_PART);
} // namespace FixedPoint

#endif // FIXED_POINT_H
