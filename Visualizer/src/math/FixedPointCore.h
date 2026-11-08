#pragma once

// Header-only core of the fixed-point arithmetic: every operation as a template on the number of fraction
// words F (or on the word count N). Nothing here allocates; all scratch buffers are stack arrays sized at
// compile time. Include this directly to use a format chosen at compile time (no dispatch cost); the
// runtime-dispatching API is in FixedPointArithmetics.h.

#include "FixedPointArithmetics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace fpa {
	namespace core {

		// ---------------------------------------------------------------------------------------------
		// Word-level primitives. N is the number of 32-bit words; all buffers live on the stack.
		// ---------------------------------------------------------------------------------------------

		template<int N>
		void addWords(const uint* a, const uint* b, uint* c) {
			uint carry = 0;
			for (int i = N - 1; i >= 0; i--) {
				ulong temp = (ulong)a[i] + b[i] + carry;
				carry = temp >> WORD_BITS;
				c[i] = temp;
			}
		}

		template<int N>
		void incWords(const uint* a, uint* c) {
			uint carry = 1;
			for (int i = N - 1; i >= 0; i--) {
				ulong temp = (ulong)a[i] + carry;
				carry = temp >> WORD_BITS;
				c[i] = (uint)temp;
			}
		}

		template<int N>
		void cmplWords(const uint* a, uint* c) {
			for (int i = 0; i < N; i++) {
				c[i] = ~a[i];
			}
			incWords<N>(c, c);
		}

		// a - b == a + ~b + 1. Computed word by word with an initial carry of 1, so the output may be
		// the same buffer as either input.
		template<int N>
		void subWords(const uint* a, const uint* b, uint* c) {
			uint carry = 1;
			for (int i = N - 1; i >= 0; i--) {
				ulong temp = (ulong)a[i] + (uint)~b[i] + carry;
				carry = temp >> WORD_BITS;
				c[i] = temp;
			}
		}

		template<int N>
		bool gtWords(const uint* a, const uint* b) {
			uint signA = a[0] >> SIGN_SHIFT;
			uint signB = b[0] >> SIGN_SHIFT;
			if (signA != signB) {
				return signA == 0;
			}
			uint diff[N];
			subWords<N>(b, a, diff);
			uint sign = diff[0] >> SIGN_SHIFT;
			return sign == 1;
		}

		template<int N>
		bool gteWords(const uint* a, const uint* b) {
			uint signA = a[0] >> SIGN_SHIFT;
			uint signB = b[0] >> SIGN_SHIFT;
			if (signA != signB) {
				return signA == 0;
			}
			uint diff[N];
			subWords<N>(a, b, diff);
			uint sign = diff[0] >> SIGN_SHIFT;
			return sign == 0;
		}

		template<int N>
		bool isZeroWords(const uint* a) {
			for (int i = 0; i < N; i++) {
				if (a[i] != 0)
					return false;
			}
			return true;
		}

		// Sets bit n (counted from the least significant bit) of the N-word number a.
		template<int N>
		void power(const int n, uint* a) {
			int idx = N - 1 - n / WORD_BITS;
			int offset = n % WORD_BITS;
			a[idx] = 1U << offset;
		}

		template<int N>
		void rightShiftWords(const uint* a, const int n, uint* result) {
			if (n == 0) {
				for (int i = 0; i < N; i++) result[i] = a[i];
				return;
			}
			if (n >= N * WORD_BITS) {
				for (int i = 0; i < N; i++) result[i] = 0;
				return;
			}

			int wholeWords = n / WORD_BITS;
			int bitShift = n % WORD_BITS;

			// Zero-initialize result
			for (int i = 0; i < N; i++) result[i] = 0;

			// Copy and shift whole words
			for (int i = wholeWords; i < N; i++) {
				result[i] = a[i - wholeWords];
			}

			// Bitwise shift with carry
			if (bitShift > 0) {
				for (int i = N - 1; i > 0; i--) {
					result[i] >>= bitShift;
					result[i] |= (result[i - 1] << (WORD_BITS - bitShift));
				}
				result[0] >>= bitShift;
			}
		}

		template<int N>
		void leftShiftWords(const uint* a, const int n, uint* result) {
			if (n == 0) {
				for (int i = 0; i < N; i++) result[i] = a[i];
				return;
			}
			if (n >= N * WORD_BITS) {
				for (int i = 0; i < N; i++) result[i] = 0;
				return;
			}

			int wholeWords = n / WORD_BITS;
			int bitShift = n % WORD_BITS;

			// Zero-initialize result
			for (int i = 0; i < N; i++) result[i] = 0;

			// Shift whole words
			for (int i = 0; i < N - wholeWords; i++) {
				result[i] = a[i + wholeWords];
			}

			// Bitwise shift with carry
			if (bitShift > 0) {
				for (int i = 0; i < N - 1; i++) {
					result[i] <<= bitShift;
					result[i] |= (result[i + 1] >> (WORD_BITS - bitShift));
				}
				result[N - 1] <<= bitShift;
			}
		}

		// ---------------------------------------------------------------------------------------------
		// Format-level operations. F is the number of fraction words.
		// ---------------------------------------------------------------------------------------------

		template<int F>
		void mulCmplImpl(const uint* a, const uint* b, uint* c) {
			constexpr int SIZE = WHOLE_PART + F;
			constexpr int MUL_BUFFER_SIZE = SIZE * 2;

			ulong result[MUL_BUFFER_SIZE] = {};

			uint tempA[SIZE];
			uint tempB[SIZE];
			const uint* aAbs = a;
			const uint* bAbs = b;
			uint aSign = a[0] >> SIGN_SHIFT;
			uint bSign = b[0] >> SIGN_SHIFT;
			bool negate = false;
			if (aSign != bSign) {
				if (aSign == 1) {
					cmplWords<SIZE>(a, tempA);
					aAbs = tempA;
				}
				else {
					cmplWords<SIZE>(b, tempB);
					bAbs = tempB;
				}
				negate = true;
			}
			else if (aSign == 1 && bSign == 1) {
				cmplWords<SIZE>(a, tempA);
				cmplWords<SIZE>(b, tempB);
				aAbs = tempA;
				bAbs = tempB;
			}

			for (int i = SIZE - 1; i >= 0; i--) {
				if (aAbs[i] == 0)
					continue;
				for (int j = SIZE - 1; j >= 0; j--) {
					if (bAbs[j] == 0)
						continue;
					ulong temp = (ulong)aAbs[i] * bAbs[j];
					ulong tempLSB = temp & WORD_MASK;
					ulong tempMSB = temp >> WORD_BITS;
					result[i + j + 1] += tempLSB;
					result[i + j] += tempMSB;
				}
			}
			for (int i = MUL_BUFFER_SIZE - 1; i >= 1; i--) {
				result[i - 1] += result[i] >> WORD_BITS;
			}
			// The product has 2 * WHOLE_PART whole words; keep the window that starts at its last whole word.
			const int leftBound = WHOLE_PART * 2 - 1;
			const int rightBound = leftBound + SIZE;
			for (int i = leftBound; i < rightBound; i++) {
				c[i - leftBound] = result[i];
			}
			if (negate)
				cmplWords<SIZE>(c, c);
		}

		template<int F>
		void divImpl(const uint* a, const uint* b, uint* c) {
			constexpr int SIZE = WHOLE_PART + F;
			constexpr int DIV_BUFFER_SIZE = SIZE + F;

			if (isZeroWords<SIZE>(b)) throw std::invalid_argument("Division by zero");

			uint tempA[SIZE], tempB[SIZE];
			const uint* aAbs = a;
			const uint* bAbs = b;
			bool negate = false;

			bool aNeg = (a[0] >> SIGN_SHIFT) != 0;
			bool bNeg = (b[0] >> SIGN_SHIFT) != 0;

			if (aNeg != bNeg) {
				if (aNeg) {
					cmplWords<SIZE>(a, tempA);
					aAbs = tempA;
				}
				else {
					cmplWords<SIZE>(b, tempB);
					bAbs = tempB;
				}
				negate = true;
			}
			else if (aNeg && bNeg) {
				cmplWords<SIZE>(a, tempA);
				cmplWords<SIZE>(b, tempB);
				aAbs = tempA;
				bAbs = tempB;
			}

			uint buffA[DIV_BUFFER_SIZE] = { 0 };
			uint buffB[DIV_BUFFER_SIZE] = { 0 };
			std::copy(aAbs, aAbs + SIZE, buffA);
			std::copy(bAbs, bAbs + SIZE, buffB + F);

			// Build the quotient in a local, zero-initialised buffer and only write the output at the end.
			// This makes the result independent of the output's previous contents and lets the output be
			// the same buffer as either input.
			uint quotient[SIZE] = { 0 };

			uint tempBuffer[DIV_BUFFER_SIZE];
			for (int i = DIV_BUFFER_SIZE * WORD_BITS - 1; i >= 0; i--) {
				rightShiftWords<DIV_BUFFER_SIZE>(buffA, i, tempBuffer);
				if (gteWords<DIV_BUFFER_SIZE>(tempBuffer, buffB)) {
					// A quotient bit beyond the top of the number: the result cannot fit.
					if (i >= SIZE * WORD_BITS) throw std::overflow_error("Division overflow: quotient does not fit the number format");
					uint temp[SIZE] = { 0 };
					power<SIZE>(i, temp);
					addWords<SIZE>(quotient, temp, quotient);
					leftShiftWords<DIV_BUFFER_SIZE>(buffB, i, tempBuffer);
					subWords<DIV_BUFFER_SIZE>(buffA, tempBuffer, tempBuffer);
					std::copy(tempBuffer, tempBuffer + DIV_BUFFER_SIZE, buffA);
				}
			}

			// quotient is the magnitude. A positive result must be at most max (sign bit clear); a negative
			// one may reach magnitude 2^(B-1), which is exactly min.
			bool fits = quotient[0] < SIGN_BIT;
			if (negate && quotient[0] == SIGN_BIT) {
				fits = true;
				for (int k = 1; k < SIZE; k++) if (quotient[k] != 0) fits = false;
			}
			if (!fits) throw std::overflow_error("Division overflow: quotient does not fit the number format");

			if (negate)
				cmplWords<SIZE>(quotient, quotient);
			std::copy(quotient, quotient + SIZE, c);
		}

		template<int F>
		void floatingToFixedImpl(double decimal, uint* out) {
			constexpr int SIZE = WHOLE_PART + F;

			bool isNegative = false;
			if (decimal < 0) {
				decimal = -decimal;
				isNegative = true;
			}

			out[0] = static_cast<uint>(std::trunc(decimal));
			double remaining = decimal - static_cast<double>(out[0]);

			// Peel off one fraction word at a time: scale the remainder up by one word, take the integer part.
			for (int i = WHOLE_PART; i < SIZE; i++) {
				const double scaled = remaining * WORD_SCALE;
				out[i] = static_cast<uint>(std::trunc(scaled));
				remaining = scaled - static_cast<double>(out[i]);
			}

			if (isNegative)
				cmplWords<SIZE>(out, out);
		}

		template<int F>
		double fixedToFloatingImpl(const uint* fpNum) {
			constexpr int SIZE = WHOLE_PART + F;

			uint temp[SIZE];
			for (int i = 0; i < SIZE; ++i)
				temp[i] = fpNum[i];

			const bool isNegative = (temp[0] >> SIGN_SHIFT) != 0;
			if (isNegative)
				cmplWords<SIZE>(temp, temp);

			auto result = static_cast<double>(temp[0]);
			double divisor = 1.0;

			for (int i = WHOLE_PART; i < SIZE; ++i) {
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

		template<int F>
		void randomFromRangeImpl(const uint* min, const uint* max, uint* result) {
			constexpr int SIZE = WHOLE_PART + F;

			const double r = std::rand() / (RAND_MAX + 1.0);

			uint randHP[SIZE] = { 0 };
			floatingToFixedImpl<F>(r, randHP);

			uint temp[SIZE] = { 0 };
			subWords<SIZE>(max, min, temp);
			mulCmplImpl<F>(randHP, temp, temp);
			addWords<SIZE>(min, temp, result);
		}

		// ---------------------------------------------------------------------------------------------
		// Format-level entry points. F is the number of fraction words and must be a compile-time constant.
		// Use these from performance-critical code: there is no runtime dispatch, so the compiler can inline
		// the whole operation. The runtime fractionWords API in FixedPointArithmetics.h dispatches to them.
		// ---------------------------------------------------------------------------------------------

		template<int F> inline void add(const uint* a, const uint* b, uint* c) { addWords<WHOLE_PART + F>(a, b, c); }
		template<int F> inline void inc(const uint* a, uint* c) { incWords<WHOLE_PART + F>(a, c); }
		template<int F> inline void cmpl(const uint* a, uint* c) { cmplWords<WHOLE_PART + F>(a, c); }
		template<int F> inline void sub(const uint* a, const uint* b, uint* c) { subWords<WHOLE_PART + F>(a, b, c); }
		template<int F> inline bool gt(const uint* a, const uint* b) { return gtWords<WHOLE_PART + F>(a, b); }
		template<int F> inline bool gte(const uint* a, const uint* b) { return gteWords<WHOLE_PART + F>(a, b); }
		template<int F> inline void mul(const uint* a, const uint* b, uint* c) { mulCmplImpl<F>(a, b, c); }
		template<int F> inline void div(const uint* a, const uint* b, uint* c) { divImpl<F>(a, b, c); }
		template<int F> inline void floatingToFixed(double decimal, uint* out) { floatingToFixedImpl<F>(decimal, out); }
		template<int F> inline double fixedToFloating(const uint* fpNum) { return fixedToFloatingImpl<F>(fpNum); }
		template<int F> inline void randomFromRange(const uint* min, const uint* max, uint* result) { randomFromRangeImpl<F>(min, max, result); }

	} // namespace core
} // namespace fpa
