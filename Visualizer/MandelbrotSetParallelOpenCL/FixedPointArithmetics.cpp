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
			carry = temp >> 32;
			c[i] = temp;
		}
	};

	void incFixed(const uint* a, uint c[FP_SIZE]) {
		uint carry = 1;
		for (int i = FP_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + carry;
			carry = temp >> 32;
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
		uint signA = a[0] >> 31;
		uint signB = b[0] >> 31;
		if (signA != signB) {
			return signA == 0;
		}
		uint diff[4];
		subFixed(b, a, diff);
		uint sign = diff[0] >> 31;
		return sign == 1;
	}

	bool gteFixed(const uint* a, const uint* b) {
		uint signA = a[0] >> 31;
		uint signB = b[0] >> 31;
		if (signA != signB) {
			return signA == 0;
		}
		uint diff[4];
		subFixed(a, b, diff);
		uint sign = diff[0] >> 31;
		return sign == 0;
	}

	void power(const int n, uint a[FP_SIZE]) {
		int idx = FP_SIZE - 1 - n / 32;
		int offset = n % 32;
		a[idx] = 1 << offset;
	}

	void addFixedLong(const uint* a, const uint* b, uint c[FP_DIV_BUFFER_SIZE]) {
		uint carry = 0;
		for (int i = FP_DIV_BUFFER_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + b[i] + carry;
			carry = temp >> 32;
			c[i] = temp;
		}
	};

	void incFixedLong(const uint* a, uint c[FP_DIV_BUFFER_SIZE]) {
		uint carry = 1;
		for (int i = FP_DIV_BUFFER_SIZE - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + carry;
			carry = temp >> 32;
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
		uint signA = a[0] >> 31;
		uint signB = b[0] >> 31;
		if (signA != signB) {
			return signA == 0;
		}
		uint diff[FP_DIV_BUFFER_SIZE];
		subFixedLong(a, b, diff);
		uint sign = diff[0] >> 31;
		return sign == 0;
	}

	void rightShiftLong(const uint a[FP_DIV_BUFFER_SIZE], const int n, uint result[FP_DIV_BUFFER_SIZE]) {
		if (n == 0 || FP_DIV_BUFFER_SIZE <= 0) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = a[i];
			return;
		}
		if (n >= FP_DIV_BUFFER_SIZE * 32) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = 0;
			return;
		}

		int wholeWords = n / 32;
		int bitShift = n % 32;

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
				result[i] |= (result[i - 1] << (32 - bitShift));
			}
			result[0] >>= bitShift;
		}
	}

	void leftShiftLong(const uint a[FP_DIV_BUFFER_SIZE], const int n, uint result[FP_DIV_BUFFER_SIZE]) {
		if (n == 0 || FP_DIV_BUFFER_SIZE <= 0) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = a[i];
			return;
		}
		if (n >= FP_DIV_BUFFER_SIZE * 32) {
			for (int i = 0; i < FP_DIV_BUFFER_SIZE; i++) result[i] = 0;
			return;
		}

		int wholeWords = n / 32;
		int bitShift = n % 32;

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
				result[i] |= (result[i + 1] >> (32 - bitShift));
			}
			result[FP_DIV_BUFFER_SIZE - 1] <<= bitShift;
		}
	}

	void mulCmplFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
		ulong result[FP_MUL_BUFFER_SIZE];
		for (int i = 0; i < FP_MUL_BUFFER_SIZE; i++) {
			result[i] = 0;
		}

		const uint* aAbs = a;
		const uint* bAbs = b;
		char aSign = a[0] >> 31;
		char bSign = b[0] >> 31;
		bool negate = false;
		if (aSign != bSign) {
			if (aSign == 1) {
				uint temp[4];
				cmplFixed(a, temp);
				aAbs = temp;
			}
			else {
				uint temp[4];
				cmplFixed(b, temp);
				bAbs = temp;
			}
			negate = true;
		}
		else if (aSign == 1 && bSign == 1) {
			uint tempA[4];
			uint tempB[4];
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
				ulong tempLSB = temp & 0x00000000FFFFFFFF;
				ulong tempMSB = temp >> 32;
				result[i + j + 1] += tempLSB;
				result[i + j] += tempMSB;
			}
		}
		for (int i = FP_MUL_BUFFER_SIZE - 1; i >= 1; i--) {
			result[i - 1] += result[i] >> 32;
		}
		const int leftBound = WHOLE_PART * 2 - 1;
		const int rightBound = leftBound + FP_SIZE;
		for (int i = leftBound; i < rightBound; i++) {
			c[i - leftBound] = result[i];
		}
		if (negate)
			cmplFixed(c, c);
	}

	void floatingToFixedPoint(float decimal, uint out[4]) {
		bool isNegative = false;
		if (decimal < 0) {
			decimal = -decimal;
			isNegative = true;
		}

		float scaleFactor = pow(2.0, 32);

		uint integerPart = static_cast<uint>(trunc(decimal));
		float fractionalPart = decimal - static_cast<float>(integerPart);

		uint fractionHigh = static_cast<uint>(trunc(fractionalPart * scaleFactor));
		float remainingFraction = fractionalPart * scaleFactor - static_cast<float>(fractionHigh);

		uint fractionLow1 = static_cast<uint>(trunc(remainingFraction * scaleFactor));
		float remainingFraction2 = remainingFraction * scaleFactor - static_cast<float>(fractionLow1);

		uint fractionLow2 = static_cast<uint>(trunc(remainingFraction2 * scaleFactor));

		out[0] = integerPart;
		out[1] = fractionHigh;
		out[2] = fractionLow1;
		out[3] = fractionLow2;

		if (isNegative) {
			ulong bigNums[4];
			for (int i = 0; i < 4; i++) {
				bigNums[i] = (~static_cast<ulong>(out[i])) & 0xFFFFFFFF;
			}

			bigNums[3] += 1;
			for (int i = 3; i >= 1; --i) {
				ulong carry = bigNums[i] >> 32;
				bigNums[i] &= 0xFFFFFFFF;
				bigNums[i - 1] += carry;
			}
			bigNums[0] &= 0xFFFFFFFF;

			for (int i = 0; i < 4; ++i) {
				out[i] = static_cast<ulong>(bigNums[i]);
			}
		}
	}

	float fixedToFloatingPoint(const uint fpNum[4]) {
		uint temp[4];
		for (int i = 0; i < 4; ++i)
			temp[i] = fpNum[i];

		bool isNegative = false;

		if ((temp[0] & 0x80000000) != 0) {
			isNegative = true;

			ulong bigNums[4];
			for (int i = 0; i < 4; i++) {
				bigNums[i] = (~static_cast<ulong>(temp[i])) & 0xFFFFFFFF;
			}

			bigNums[3] += 1;
			for (int i = 3; i >= 1; --i) {
				ulong carry = bigNums[i] >> 32;
				bigNums[i] &= 0xFFFFFFFF;
				bigNums[i - 1] += carry;
			}
			bigNums[0] &= 0xFFFFFFFF;

			for (int i = 0; i < 4; ++i) {
				temp[i] = static_cast<uint>(bigNums[i]);
			}
		}

		float result = static_cast<float>(temp[0]);
		float divisor = 1.0;

		for (int i = 1; i < 4; ++i) {
			uint part = temp[i];
			for (int j = 0; j < 32; ++j) {
				divisor *= 2.0;
				if (part & (1U << (31 - j))) {
					result += 1.0 / divisor;
				}
			}
		}

		if (isNegative) {
			result = -result;
		}

		return result;
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

		uint tempA[4], tempB[4];
		const uint* aAbs = a;
		const uint* bAbs = b;
		bool negate = false;

		bool aNeg = (a[0] >> 31) != 0;
		bool bNeg = (b[0] >> 31) != 0;

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
		for (int i = FP_DIV_BUFFER_SIZE * 32 - 1; i >= 0; i--) {
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
		const float r = rand() / (RAND_MAX + 1.0);

		uint randHP[FP_SIZE] = { 0 };
		floatingToFixedPoint(r, randHP);

		uint temp[FP_SIZE] = { 0 };
		subFixed(max, min, temp);
		mulCmplFixed(randHP, temp, temp);
		addFixed(min, temp, result);		
	}
}