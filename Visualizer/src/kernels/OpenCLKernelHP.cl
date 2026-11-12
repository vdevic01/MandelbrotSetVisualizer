#define WHOLE_PART 1
// Number of 32-bit fraction words. The host passes -DFRACTION_PART=N when it builds the program.
#ifndef FRACTION_PART
#define FRACTION_PART 3
#endif
#define WHOLE_BITS WHOLE_PART * 32
#define FRACTION_BITS FRACTION_PART * 32
#define FP_SIZE (WHOLE_PART + FRACTION_PART)
#define FP_BUFFER_SIZE (FP_SIZE * 2)


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
	// a - b == a + ~b + 1, computed word by word with an initial carry of 1, so the output
	// may be the same buffer as either input (negating b into c first would destroy a).
	uint carry = 1;
	for (int i = FP_SIZE - 1; i >= 0; i--) {
		ulong temp = (ulong)a[i] + (uint)~b[i] + carry;
		carry = temp >> 32;
		c[i] = temp;
	}
}

bool gtFixed(const uint* a, const uint* b) {
	uint signA = a[0] >> 31;
	uint signB = b[0] >> 31;
	if (signA != signB) {
		return signA == 0;
	}
	uint diff[FP_SIZE];
	subFixed(b, a, diff);
	uint sign = diff[0] >> 31;
	return sign == 1;
}

// The same algorithms as mulColumns / sqrColumns in math/FixedPointShared.h (keep them in sync).
//
// Full product of two magnitudes, summed column by column from the least significant column upward:
// column k holds the products a[p] * b[k - p], with the words counted from the least significant one.
// The running sum stays in two variables and every product word is written once. `out` receives the
// product least significant word first. Nothing overflows: a column gets at most FP_SIZE products.
void mulWords(const uint* a, const uint* b, uint out[FP_BUFFER_SIZE]) {
	ulong carry = 0;
	for (int k = 0; k < FP_BUFFER_SIZE - 1; k++) {
		const int first = k < FP_SIZE ? 0 : k - FP_SIZE + 1;   // first word of a that has a partner in this column
		const int last = k < FP_SIZE ? k : FP_SIZE - 1;        // last one
		ulong low = carry;    // low halves of the products, plus the carry from the column below
		ulong high = 0;       // high halves: they belong to the next column
		for (int p = first; p <= last; p++) {
			ulong temp = (ulong)a[FP_SIZE - 1 - p] * b[FP_SIZE - 1 - (k - p)];
			low += temp & 0x00000000FFFFFFFF;
			high += temp >> 32;
		}
		out[k] = (uint)low;
		carry = (low >> 32) + high;
	}
	out[FP_BUFFER_SIZE - 1] = (uint)carry;
}

// Full square of a magnitude. A product a[i] * a[j] with i != j appears twice in a square, so it is
// computed once and added doubled: about half the word products of mulWords.
void sqrWords(const uint* a, uint out[FP_BUFFER_SIZE]) {
	ulong carry = 0;
	for (int k = 0; k < FP_BUFFER_SIZE - 1; k++) {
		const int first = k < FP_SIZE ? 0 : k - FP_SIZE + 1;
		ulong low = carry;
		ulong high = 0;
		for (int p = first; 2 * p < k; p++) {
			ulong temp = (ulong)a[FP_SIZE - 1 - p] * a[FP_SIZE - 1 - (k - p)];
			low += (temp & 0x00000000FFFFFFFF) << 1;
			high += (temp >> 32) << 1;
		}
		if ((k & 1) == 0) {
			ulong temp = (ulong)a[FP_SIZE - 1 - k / 2] * a[FP_SIZE - 1 - k / 2];
			low += temp & 0x00000000FFFFFFFF;
			high += temp >> 32;
		}
		out[k] = (uint)low;
		carry = (low >> 32) + high;
	}
	out[FP_BUFFER_SIZE - 1] = (uint)carry;
}

// The product has 2 * WHOLE_PART whole words; keep the window that starts at its last whole word.
// `product` is least significant word first, c is most significant word first.
void productWindow(const uint* product, uint c[FP_SIZE]) {
	const int top = FP_BUFFER_SIZE - WHOLE_PART * 2;
	for (int i = 0; i < FP_SIZE; i++) {
		c[i] = product[top - i];
	}
}

// c = a * b, truncated toward zero. c may be the same buffer as a or b.
void mulCmplFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
	const uint* aAbs = a;
	const uint* bAbs = b;
	uint aSign = a[0] >> 31;
	uint bSign = b[0] >> 31;
	uint tempA[FP_SIZE];
	uint tempB[FP_SIZE];
	if (aSign) {
		cmplFixed(a, tempA);
		aAbs = tempA;
	}
	if (bSign) {
		cmplFixed(b, tempB);
		bAbs = tempB;
	}

	uint product[FP_BUFFER_SIZE];
	mulWords(aAbs, bAbs, product);
	productWindow(product, c);
	if (aSign != bSign)
		cmplFixed(c, c);
}

// c = a * a, with the same words as mulCmplFixed(a, a, c). A square is never negative, so the sign
// needs no handling beyond taking |a|. c may be the same buffer as a.
void sqrCmplFixed(const uint* a, uint c[FP_SIZE]) {
	const uint* aAbs = a;
	uint tempA[FP_SIZE];
	if (a[0] >> 31) {
		cmplFixed(a, tempA);
		aAbs = tempA;
	}

	uint product[FP_BUFFER_SIZE];
	sqrWords(aAbs, product);
	productWindow(product, c);
}

// IN holds, per point, the real words followed by the imaginary words (FP_SIZE words each, most significant first).
// OUT: escape iteration (-1 if the point never escaped). OUT_MAG: |z|^2 at that iteration.
// threshold_whole / threshold_fraction: the squared escape radius, as the whole word and the first fraction word.
__kernel void calculateIters(__global const uint* IN, __global int* OUT, const unsigned int max_iter,
	__global float* OUT_MAG, const uint threshold_whole, const uint threshold_fraction)
{
	int idx = get_global_id(0);
	__global const uint* c = IN + (size_t)idx * 2 * FP_SIZE;

	uint x0[FP_SIZE];
	uint y0[FP_SIZE];
	for (int i = 0; i < FP_SIZE; i++) {
		x0[i] = c[i];
		y0[i] = c[FP_SIZE + i];
	}

	uint x2[FP_SIZE] = { 0 };
	uint y2[FP_SIZE] = { 0 };

	uint x[FP_SIZE] = { 0 };
	uint y[FP_SIZE] = { 0 };

	uint temp[FP_SIZE];
	uint threshold[FP_SIZE] = { threshold_whole, threshold_fraction };   // R^2; the other fraction words are 0

	int result = -1;
	float escapeValue = 0;
	for (int i = 0; i < max_iter; i++) {
		addFixed(x, x, temp);
		mulCmplFixed(temp, y, temp);
		addFixed(temp, y0, y);
		//y = (x + x) * y + y0;

		subFixed(x2, y2, temp);
		addFixed(temp, x0, x);
		//x = x2 - y2 + x0;

		sqrCmplFixed(x, x2);
		//x2 = x * x;

		sqrCmplFixed(y, y2);
		//y2 = y * y;

		addFixed(x2, y2, temp);
		if (gtFixed(temp, threshold)) {
			result = i;
			// |z|^2 as a float: the whole word and the first fraction word carry more than enough bits
			escapeValue = (float)temp[0] + (float)temp[1] * (1.0f / 4294967296.0f);
			break;
		}
	}

	OUT[idx] = result;
	OUT_MAG[idx] = escapeValue;

	return;
}
