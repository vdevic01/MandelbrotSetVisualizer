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

	// Multiplication of two magnitudes. The word products are summed column by column, from the least
	// significant column of the full 2 * SIZE-word product upward: column K holds the products
	// a[p] * b[K - p], with the words counted from the least significant one. K is a template parameter,
	// so every loop bound is a compile-time constant and the compiler emits straight-line code; the
	// running sum stays in two registers and every product word is written once.
	//
	// `out` receives the product least significant word first. Nothing overflows: a column gets at most
	// SIZE products, so both sums stay far below 2^(2 * WORD_BITS).
	template<int SIZE, int K>
	FPA_HD inline void mulColumns(const uint* a, const uint* b, uint* out, ulong carry) {
		if constexpr (K < 2 * SIZE - 1) {
			constexpr int FIRST = K < SIZE ? 0 : K - SIZE + 1;   // first word of a that has a partner in this column
			constexpr int LAST = K < SIZE ? K : SIZE - 1;        // last one
			ulong low = carry;    // low halves of the products, plus the carry from the column below
			ulong high = 0;       // high halves: they belong to the next column
			for (int p = FIRST; p <= LAST; p++) {
				const ulong temp = (ulong)a[SIZE - 1 - p] * b[SIZE - 1 - (K - p)];
				low += temp & WORD_MASK;
				high += temp >> WORD_BITS;
			}
			out[K] = (uint)low;
			mulColumns<SIZE, K + 1>(a, b, out, (low >> WORD_BITS) + high);
		}
		else {
			out[K] = (uint)carry;
		}
	}

	// c = a * b, truncated toward zero. c may be the same buffer as a or b.
	template<int F>
	FPA_HD void mulCmplImpl(const uint* a, const uint* b, uint* c) {
		constexpr int SIZE = WHOLE_PART + F;

		uint tempA[SIZE];
		uint tempB[SIZE];
		const uint* aAbs = a;
		const uint* bAbs = b;
		const uint aSign = a[0] >> SIGN_SHIFT;
		const uint bSign = b[0] >> SIGN_SHIFT;
		if (aSign) {
			cmplWords<SIZE>(a, tempA);
			aAbs = tempA;
		}
		if (bSign) {
			cmplWords<SIZE>(b, tempB);
			bAbs = tempB;
		}

		uint product[SIZE * 2];
		mulColumns<SIZE, 0>(aAbs, bAbs, product, 0);

		// The product has 2 * WHOLE_PART whole words; keep the window that starts at its last whole word,
		// which is word 2 * SIZE - 1 - (2 * WHOLE_PART - 1) counted from the least significant end.
		const int top = SIZE * 2 - WHOLE_PART * 2;
		for (int i = 0; i < SIZE; i++) {
			c[i] = product[top - i];
		}
		if (aSign != bSign)
			cmplWords<SIZE>(c, c);
	}

	// Squaring. A square needs about half the word products of a general multiplication: a product
	// a[i] * a[j] with i != j appears twice, so it is computed once and added doubled. And the sign needs
	// no handling beyond taking |a|, because a square is never negative.
	//
	// The products are summed column by column, from the least significant column of the full
	// 2 * SIZE-word square upward (column K holds the products of the words p and K - p, counted from the
	// least significant word). K is a template parameter, so every loop bound is a compile-time constant
	// and the compiler emits straight-line code; that is a large part of the speed.
	//
	// `out` receives the square least significant word first. Nothing overflows: a column gets at most
	// SIZE / 2 doubled products and one square, so both sums stay far below 2^(2 * WORD_BITS).
	template<int SIZE, int K>
	FPA_HD inline void sqrColumns(const uint* a, uint* out, ulong carry) {
		if constexpr (K < 2 * SIZE - 1) {
			constexpr int FIRST = K < SIZE ? 0 : K - SIZE + 1;   // first word that has a partner in this column
			ulong low = carry;    // low halves of the products, plus the carry from the column below
			ulong high = 0;       // high halves: they belong to the next column
			for (int p = FIRST; 2 * p < K; p++) {
				const ulong temp = (ulong)a[SIZE - 1 - p] * a[SIZE - 1 - (K - p)];
				low += (temp & WORD_MASK) << 1;
				high += (temp >> WORD_BITS) << 1;
			}
			if constexpr (K % 2 == 0) {
				const ulong temp = (ulong)a[SIZE - 1 - K / 2] * a[SIZE - 1 - K / 2];
				low += temp & WORD_MASK;
				high += temp >> WORD_BITS;
			}
			out[K] = (uint)low;
			sqrColumns<SIZE, K + 1>(a, out, (low >> WORD_BITS) + high);
		}
		else {
			out[K] = (uint)carry;
		}
	}

	// c = a * a, with exactly the same words as mulCmplImpl(a, a, c). c may be the same buffer as a.
	template<int F>
	FPA_HD void sqrCmplImpl(const uint* a, uint* c) {
		constexpr int SIZE = WHOLE_PART + F;

		uint tempA[SIZE];
		const uint* aAbs = a;
		if (a[0] >> SIGN_SHIFT) {
			cmplWords<SIZE>(a, tempA);
			aAbs = tempA;
		}

		uint square[SIZE * 2];
		sqrColumns<SIZE, 0>(aAbs, square, 0);

		// Same window as in mulCmplImpl.
		const int top = SIZE * 2 - WHOLE_PART * 2;
		for (int i = 0; i < SIZE; i++) {
			c[i] = square[top - i];
		}
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
	// c = a * a. Same result as mul(a, a, c), faster.
	template<int F> FPA_HD inline void sqr(const uint* a, uint* c) { sqrCmplImpl<F>(a, c); }

} // namespace fpa::core
