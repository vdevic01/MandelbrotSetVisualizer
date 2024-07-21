typedef struct {
	uint real[4]; // 4 bytes for whole part and 12 bytes for fraction part, using big endian
	uint imag[4];
} ComplexHP;

#define WHOLE_PART 1
#define FRACTION_PART 3
#define WHOLE_BITS WHOLE_PART * 32
#define FRACTION_BITS FRACTION_PART * 32
#define FP_SIZE (WHOLE_PART + FRACTION_PART)
#define FP_BUFFER_SIZE FP_SIZE * 2


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

void mulCmplFixed(const uint* a, const uint* b, uint c[4]) {
	ulong result[FP_BUFFER_SIZE];
	for (int i = 0; i < FP_BUFFER_SIZE; i++) {
		result[i] = 0;
	}

	const uint* aAbs = a;
	const uint* bAbs = b;
	char aSign = a[0] >> 31;
	char bSign = b[0] >> 31;
	bool negate = false;
	uint tempA[4];
	uint tempB[4];
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
			ulong tempLSB = temp & 0x00000000FFFFFFFF;
			ulong tempMSB = temp >> 32;
			result[i + j + 1] += tempLSB;
			result[i + j] += tempMSB;
		}
	}
	for (int i = FP_BUFFER_SIZE - 1; i >= 1; i--) {
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

__kernel void calculateIters(__global ComplexHP* IN, __global int* OUT, const unsigned int max_iter)
{
	int idx = get_global_id(0);
	ComplexHP c = IN[idx];

	uint x0[FP_SIZE];
	uint y0[FP_SIZE];
	for (int i = 0; i < FP_SIZE; i++) {
		x0[i] = c.real[i];
		y0[i] = c.imag[i];
	}

	uint x2[FP_SIZE] = { 0,0,0,0 };
	uint y2[FP_SIZE] = { 0,0,0,0 };

	uint x[FP_SIZE] = { 0,0,0,0 };
	uint y[FP_SIZE] = { 0,0,0,0 };

	uint temp[FP_SIZE];
	uint fourFixed[FP_SIZE] = { 4, 0, 0, 0 };

	int result = -1;
	for (int i = 0; i < max_iter; i++) {
		addFixed(x, x, temp);
		mulCmplFixed(temp, y, temp);
		addFixed(temp, y0, y);
		//y = (x + x) * y + y0;
		
		subFixed(x2, y2, temp);
		addFixed(temp, x0, x);
		//x = x2 - y2 + x0;

		mulCmplFixed(x, x, x2); // THIS PART DOES NOT WORK, X is right but product X*X is wrong
		//x2 = x * x;

		mulCmplFixed(y, y, y2);
		//y2 = y * y;

		addFixed(x2, y2, temp);
		if (gtFixed(temp, fourFixed)) {
			result = i;
			break;
		}
		//if (x2 + y2 > 4) {
		//	result = i;
		//	break;
		//}
	}

	OUT[idx] = result;

	return;
}