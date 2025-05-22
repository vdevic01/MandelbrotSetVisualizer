#include "FixedPointArithmetics.h"
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

using namespace std;
using namespace boost::multiprecision;

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

	void mulCmplFixed(const uint* a, const uint* b, uint c[FP_SIZE]) {
		ulong result[FP_BUFFER_SIZE];
		for (int i = 0; i < FP_BUFFER_SIZE; i++) {
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

	void convertToFixedPoint(const cpp_dec_float_50& num, unsigned int res[4]) {
		cpp_dec_float_50 temp = num < 0 ? -num : num;
		cpp_int whole_int = floor(temp).convert_to<cpp_int>();
		cpp_dec_float_50 fractional_part = temp - cpp_dec_float_50(whole_int);
		res[0] = whole_int.convert_to<unsigned int>();

		cpp_dec_float_50 scale("79228162514264337593543950336");
		cpp_int fractional_int = (fractional_part * scale).convert_to<cpp_int>();

		for (int i = 3; i >= 1; --i) {
			res[i] = static_cast<unsigned int>(fractional_int & 0xFFFFFFFF);
			fractional_int >>= 32;
		}

		if (num < 0) {
			fpa::cmplFixed(res, res);
		}
	}
}