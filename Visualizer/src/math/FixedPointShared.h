#pragma once

// The part of the fixed-point arithmetic that is shared between the CPU build and the GPU kernels
// (CUDAUnifiedKernel.cu includes this header). It only does plain integer arithmetic on stack arrays:
// no exceptions, no heap, and no calls into the C++ standard library, so it is valid device code.
//
// Everything is a template on the number of words N or fraction words F. Do not add anything here that
// a GPU kernel cannot run; CPU-only operations (division, conversions, ...) live in FixedPointCore.h.

#include "FixedPointArithmetics.h"

// Under nvcc these functions are usable from both host and device code.
#if defined(__CUDACC__)
#define FPA_HD __host__ __device__
#else
#define FPA_HD
#endif

namespace fpa::core {

	// ---------------------------------------------------------------------------------------------
	// Word-level primitives. N is the number of 32-bit words; all buffers live on the stack.
	// ---------------------------------------------------------------------------------------------

	template<int N>
	FPA_HD void addWords(const uint* a, const uint* b, uint* c) {
		uint carry = 0;
		for (int i = N - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + b[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = temp;
		}
	}

	template<int N>
	FPA_HD void incWords(const uint* a, uint* c) {
		uint carry = 1;
		for (int i = N - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = (uint)temp;
		}
	}

	template<int N>
	FPA_HD void cmplWords(const uint* a, uint* c) {
		for (int i = 0; i < N; i++) {
			c[i] = ~a[i];
		}
		incWords<N>(c, c);
	}

	// a - b == a + ~b + 1. Computed word by word with an initial carry of 1, so the output may be
	// the same buffer as either input.
	template<int N>
	FPA_HD void subWords(const uint* a, const uint* b, uint* c) {
		uint carry = 1;
		for (int i = N - 1; i >= 0; i--) {
			ulong temp = (ulong)a[i] + (uint)~b[i] + carry;
			carry = temp >> WORD_BITS;
			c[i] = temp;
		}
	}

	template<int N>
	FPA_HD bool gtWords(const uint* a, const uint* b) {
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

	template<int F>
	FPA_HD void mulCmplImpl(const uint* a, const uint* b, uint* c) {
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

	// ---------------------------------------------------------------------------------------------
	// Format-level entry points. F is the number of fraction words and must be a compile-time constant:
	// there is no runtime dispatch, so the compiler can inline the whole operation.
	// ---------------------------------------------------------------------------------------------

	template<int F> FPA_HD inline void add(const uint* a, const uint* b, uint* c) { addWords<WHOLE_PART + F>(a, b, c); }
	template<int F> FPA_HD inline void inc(const uint* a, uint* c) { incWords<WHOLE_PART + F>(a, c); }
	template<int F> FPA_HD inline void cmpl(const uint* a, uint* c) { cmplWords<WHOLE_PART + F>(a, c); }
	template<int F> FPA_HD inline void sub(const uint* a, const uint* b, uint* c) { subWords<WHOLE_PART + F>(a, b, c); }
	template<int F> FPA_HD inline bool gt(const uint* a, const uint* b) { return gtWords<WHOLE_PART + F>(a, b); }
	template<int F> FPA_HD inline void mul(const uint* a, const uint* b, uint* c) { mulCmplImpl<F>(a, b, c); }

} // namespace fpa::core
