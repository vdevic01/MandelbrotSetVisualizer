#include "FixedPointArithmetics.h"

#include <cmath>
#include <algorithm>
#include <stdexcept> 

using namespace std;

namespace fpa {
	void addFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
		uint carry = 0;
		for (int i = FP_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + b[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = temp;
		}
	}

	void incFixed(const uint* a, uint c[FP_SIZE]) {
		uint carry = 1;
		for (int i = FP_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = (uint)temp;
		}
	}

	void cmplFixed(const uint* a, uint c[FP_SIZE]) {
		for (int i = 0; i < FP_SIZE; i++) {
			c[i] = ~a[i];
		}
		incFixed(c, c);
	}

	void subFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
		cmplFixed(b, c);
		addFixed(a, c, c);
	}

	bool gtFixed(const uint* a, const uint* b) {
		uint signA = a[0] >> SIGN_SHIFT;
		uint signB = b[0] >> SIGN_SHIFT;
		if (signA != signB) {
			return signA == 0;
		}
		uint diff[FP_SIZE];
		subFixed(b, a, diff);
		uint sign = diff[0] >> SIGN_SHIFT;
		return sign == 1;
	}

	bool gteFixed(const uint* a, const uint* b) {
		uint signA = a[0] >> SIGN_SHIFT;
		uint signB = b[0] >> SIGN_SHIFT;
		if (signA != signB) {
			return signA == 0;
		}
		uint diff[FP_SIZE];
		subFixed(a, b, diff);
		uint sign = diff[0] >> SIGN_SHIFT;
		return sign == 0;
	}

	void power(const int n, uint a[FP_SIZE]) {
		int idx = FP_SIZE - 1 - n / WORD_BITS;
		int offset = n % WORD_BITS;
		a[idx] = 1U << offset;
	}

	void addFixedLong(const uint* a, const uint* b, uint c[FP_DIV_BUFFER_SIZE]) {
		uint carry = 0;
		for (int i = FP_DIV_BUFFER_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + b[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = temp;
		}
	}

	void incFixedLong(const uint* a, uint c[FP_DIV_BUFFER_SIZE]) {
		uint carry = 1;
		for (int i = FP_DIV_BUFFER_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = (uint)temp;
		}
	}

	void cmplFixedLong(const uint* a, uint c[FP_DIV_BUFFER_SIZE]) {
		for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) {
			c[i] = ~a[i];
		}
		incFixedLong(c, c);
	}

	void subFixedLong(const uint* a, const uint* b, uint c[FP_DIV_BUFFER_SIZE]) {
		cmplFixedLong(b, c);
		addFixedLong(a, c, c);
	}

	bool gteFixedLong(const uint* a, const uint* b) {
		uint signA = a[0] >> SIGN_SHIFT;
		uint signB = b[0] >> SIGN_SHIFT;
		if (signA != signB) {
			return signA == 0;
		}
		uint diff[FP_DIV_BUFFER_SIZE];
		subFixedLong(a, b, diff);
		uint sign = diff[0] >> SIGN_SHIFT;
		return sign == 0;
	}

	void rightShiftLong(const uint a[FP_DIV_BUFFER_SIZE], const int n, uint result[FP_DIV_BUFFER_SIZE]) {
		if (n == 0) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = a[i];
			return;
		}
		if (n >= FP_DIV_BUFFER_SIZE * WORD_BITS) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = 0;
			return;
		}

		int wholeWords = n / WORD_BITS;
		int bitShift = n % WORD_BITS;

		// Zero-initialize result
		for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = 0;

		// Copy and shift whole words
		for (int i = wholeWords; i < FP_DIV_BUFFER_SIZE; i++) {
			result[i] = a[i - wholeWords];
		}

		// Bitwise shift with carry
		if (bitShift > 0) {
			for (int i = FP_DIV_BUFFER_SIZE - 1; i > 0; i--) {
				result[i] >>= bitShift;
				result[i] |= (result[i - 1] << (WORD_BITS - bitShift));
			}
			result[0] >>= bitShift;
		}
	}

	void leftShiftLong(const uint a[FP_DIV_BUFFER_SIZE], const int n, uint result[FP_DIV_BUFFER_SIZE]) {
		if (n == 0) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = a[i];
			return;
		}
		if (n >= FP_DIV_BUFFER_SIZE * WORD_BITS) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = 0;
			return;
		}

		int wholeWords = n / WORD_BITS;
		int bitShift = n % WORD_BITS;

		// Zero-initialize result
		for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = 0;

		// Shift whole words
		for (int i = 0; i < FP_DIV_BUFFER_SIZE - wholeWords; i++) {
			result[i] = a[i + wholeWords];
		}

		// Bitwise shift with carry
		if (bitShift > 0) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE - 1; i++) {
				result[i] <<= bitShift;
				result[i] |= (result[i + 1] >> (WORD_BITS - bitShift));
			}
			result[FP_DIV_BUFFER_SIZE - 1] <<= bitShift;
		}
	}

	void mulCmplFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
		ulong result[FP_MUL_BUFFER_SIZE] = {};

		uint tempA[FP_SIZE];
		uint tempB[FP_SIZE];
		const uint* aAbs = a;
		const uint* bAbs = b;
		uint aSign = a[0] >> SIGN_SHIFT;
		uint bSign = b[0] >> SIGN_SHIFT;
		bool negate = false;
		if (aSign != bSign) {
			if (aSign == 1) {
				cmplFixed(a, tempA);
				aAbs = tempA;
			}
			else {
				cmplFixed(b, tempB);
				bAbs = tempB;
			}
			negate = true;
		}
		else if (aSign == 1 && bSign == 1) {
			cmplFixed(a, tempA);
			cmplFixed(b, tempB);
			aAbs = tempA;
			bAbs = tempB;
		}

		for (int i = FP_SIZE - 1; i >= 0; i--) {
			if (aAbs[i] == 0)
				continue;
			for (int j = FP_SIZE - 1; j >= 0; j--) {
				if (bAbs[j] == 0)
					continue;
				ulong temp = (ulong)aAbs[i] * bAbs[j];
				ulong tempLSB = temp & WORD_MASK;
				ulong tempMSB = temp >> WORD_BITS;
				result[i + j + 1] += tempLSB;
				result[i + j] += tempMSB;
			}
		}
		for (int i = FP_MUL_BUFFER_SIZE - 1; i >= 1; i--) {
			result[i - 1] += result[i] >> WORD_BITS;
		}
		const int leftBound = WHOLE_PART * 2 - 1;
		const int rightBound = leftBound + FP_SIZE;
		for (int i = leftBound; i < rightBound; i++) {
			c[i - leftBound] = result[i];
		}
		if (negate)
			cmplFixed(c, c);
	}

	void floatingToFixedPoint(double decimal, uint out[FP_SIZE]) {
		bool isNegative = false;
		if (decimal < 0) {
			decimal = -decimal;
			isNegative = true;
		}

		out[0] = static_cast<uint>(trunc(decimal));
		double remaining = decimal - static_cast<double>(out[0]);

		// Peel off one fraction word at a time: scale the remainder up by one word, take the integer part.
		for (int i = WHOLE_PART; i < FP_SIZE; i++) {
			const double scaled = remaining * WORD_SCALE;
			out[i] = static_cast<uint>(trunc(scaled));
			remaining = scaled - static_cast<double>(out[i]);
		}

		if (isNegative)
			cmplFixed(out, out);
	}

	double fixedToFloatingPoint(const uint fpNum[FP_SIZE]) {
		uint temp[FP_SIZE];
		for (int i = 0; i < FP_SIZE; ++i)
			temp[i] = fpNum[i];

		const bool isNegative = (temp[0] >> SIGN_SHIFT) != 0;
		if (isNegative)
			cmplFixed(temp, temp);

		auto result = static_cast<double>(temp[0]);
		double divisor = 1.0;

		for (int i = WHOLE_PART; i < FP_SIZE; ++i) {
			const uint part = temp[i];
			for (int j = 0; j < WORD_BITS; ++j) {
				divisor *= 2.0;
				if (part & (1U << (WORD_BITS - 1 - j))) {
					result += 1.0 / divisor;
				}
			}
		}

		return isNegative ? -result : result;
	}

	bool isZero(const uint* a) {
		for (int i = 0; i < FP_SIZE; i++) {
			if (a[i] != 0)
				return false;
		}
		return true;
	}

	void divFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
		if (isZero(b)) throw invalid_argument("Division by zero");

		uint tempA[FP_SIZE], tempB[FP_SIZE];
		const uint* aAbs = a;
		const uint* bAbs = b;
		bool negate = false;

		bool aNeg = (a[0] >> SIGN_SHIFT) != 0;
		bool bNeg = (b[0] >> SIGN_SHIFT) != 0;

		if (aNeg != bNeg) {
			if (aNeg) {
				cmplFixed(a, tempA);
				aAbs = tempA;
			}
			else {
				cmplFixed(b, tempB);
				bAbs = tempB;
			}
			negate = true;
		}
		else if (aNeg && bNeg) {
			cmplFixed(a, tempA);
			cmplFixed(b, tempB);
			aAbs = tempA;
			bAbs = tempB;
		}

		uint buffA[FP_DIV_BUFFER_SIZE] = { 0 };
		uint buffB[FP_DIV_BUFFER_SIZE] = { 0 };
		copy(aAbs, aAbs + FP_SIZE, buffA);
		copy(bAbs, bAbs + FP_SIZE, buffB + FRACTION_PART);

		uint tempBuffer[FP_DIV_BUFFER_SIZE];
		for (int i = FP_DIV_BUFFER_SIZE * WORD_BITS - 1; i >= 0; i--) {
			rightShiftLong(buffA, i, tempBuffer);
			if (gteFixedLong(tempBuffer, buffB)) {
				uint temp[FP_SIZE] = { 0 };
				power(i, temp);
				addFixed(c, temp, c);
				leftShiftLong(buffB, i, tempBuffer);
				subFixedLong(buffA, tempBuffer, tempBuffer);
				copy(tempBuffer, tempBuffer + FP_DIV_BUFFER_SIZE, buffA);
			}
		}

		if (negate)
			cmplFixed(c, c);
	}

	void randomFromRange(const uint min[FP_SIZE], const uint max[FP_SIZE], uint result[FP_SIZE]) {
		const double r = rand() / (RAND_MAX + 1.0);

		uint randHP[FP_SIZE] = { 0 };
		floatingToFixedPoint(r, randHP);

		uint temp[FP_SIZE] = { 0 };
		subFixed(max, min, temp);
		mulCmplFixed(randHP, temp, temp);
		addFixed(min, temp, result);		
	}
}