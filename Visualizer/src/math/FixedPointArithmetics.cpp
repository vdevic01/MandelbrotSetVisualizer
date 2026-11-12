#include "FixedPointArithmetics.h"
#include "FixedPointCore.h"

#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

using namespace std;

namespace fpa {
	namespace {

		// ---------------------------------------------------------------------------------------------
		// Runtime fraction size -> compile-time instantiation. Every size in
		// [MIN_FRACTION_PART, MAX_FRACTION_PART] is instantiated here; keep the cases in sync with them.
		// ---------------------------------------------------------------------------------------------

		static_assert(MIN_FRACTION_PART == 1 && MAX_FRACTION_PART == 6, "update withFormat() when the supported range changes");

		template<typename Fn>
		decltype(auto) withFormat(int fractionWords, Fn&& fn) {
			switch (fractionWords) {
				case 1: return fn(integral_constant<int, 1>{});
				case 2: return fn(integral_constant<int, 2>{});
				case 3: return fn(integral_constant<int, 3>{});
				case 4: return fn(integral_constant<int, 4>{});
				case 5: return fn(integral_constant<int, 5>{});
				case 6: return fn(integral_constant<int, 6>{});
				default:
					throw invalid_argument("Unsupported fraction size: " + to_string(fractionWords) +
						" words (supported: " + to_string(MIN_FRACTION_PART) + ".." + to_string(MAX_FRACTION_PART) + ")");
			}
		}

	} // anonymous namespace

	void addFixed(const uint* a, const uint* b, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::add<decltype(f)::value>(a, b, c); });
	}

	void incFixed(const uint* a, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::inc<decltype(f)::value>(a, c); });
	}

	void cmplFixed(const uint* a, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::cmpl<decltype(f)::value>(a, c); });
	}

	void subFixed(const uint* a, const uint* b, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::sub<decltype(f)::value>(a, b, c); });
	}

	bool gtFixed(const uint* a, const uint* b, int fractionWords) {
		return withFormat(fractionWords, [&](auto f) { return core::gt<decltype(f)::value>(a, b); });
	}

	bool gteFixed(const uint* a, const uint* b, int fractionWords) {
		return withFormat(fractionWords, [&](auto f) { return core::gte<decltype(f)::value>(a, b); });
	}

	void mulCmplFixed(const uint* a, const uint* b, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::mul<decltype(f)::value>(a, b, c); });
	}

	void sqrCmplFixed(const uint* a, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::sqr<decltype(f)::value>(a, c); });
	}

	void divFixed(const uint* a, const uint* b, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::div<decltype(f)::value>(a, b, c); });
	}

	namespace {
		// Little-endian big integer helpers for decimalToFixedPoint.
		void mulAdd(vector<uint>& n, uint factor, uint add) {
			ulong carry = add;
			for (uint& word : n) {
				const ulong v = ulong(word) * factor + carry;
				word = static_cast<uint>(v & WORD_MASK);
				carry = v >> WORD_BITS;
			}
			if (carry) n.push_back(static_cast<uint>(carry));
		}

		void divSmall(vector<uint>& n, uint divisor) {
			ulong rest = 0;
			for (size_t i = n.size(); i-- > 0;) {
				const ulong v = (rest << WORD_BITS) | n[i];
				n[i] = static_cast<uint>(v / divisor);
				rest = v % divisor;
			}
		}
	}

	void decimalToFixedPoint(const string& text, uint* out, int fractionWords) {
		if (!isSupportedFractionPart(fractionWords))
			throw invalid_argument("fraction size " + to_string(fractionWords) + " is not supported");
		const auto bad = [&]() { return invalid_argument("'" + text + "' is not a decimal number"); };

		size_t i = 0;
		bool negative = false;
		if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';

		// All digits as one integer, and how many of them are after the decimal point.
		vector<uint> magnitude(1, 0);
		int digits = 0;
		long scale = 0;
		bool seenPoint = false;
		for (; i < text.size(); i++) {
			const char c = text[i];
			if (c == '.' && !seenPoint) { seenPoint = true; continue; }
			if (c < '0' || c > '9') break;
			mulAdd(magnitude, 10, static_cast<uint>(c - '0'));
			digits++;
			if (seenPoint) scale++;
		}
		if (digits == 0) throw bad();
		if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
			size_t j = i + 1;
			bool negativeExp = false;
			if (j < text.size() && (text[j] == '+' || text[j] == '-')) negativeExp = text[j++] == '-';
			if (j == text.size() || text.size() - j > 4) throw bad();
			long exponent = 0;
			for (; j < text.size(); j++) {
				if (text[j] < '0' || text[j] > '9') throw bad();
				exponent = exponent * 10 + (text[j] - '0');
			}
			scale -= negativeExp ? -exponent : exponent;
			i = text.size();
		}
		if (i != text.size()) throw bad();
		if (scale > 4000 || scale < -4000) throw out_of_range("'" + text + "' has too large an exponent");

		// value = magnitude / 10^scale. Fixed point raw = trunc(value * 2^(32 * fractionWords)).
		for (; scale < 0; scale++) mulAdd(magnitude, 10, 0);
		magnitude.insert(magnitude.begin(), static_cast<size_t>(fractionWords), 0u);
		for (; scale > 0; scale--) divSmall(magnitude, 10);

		const int words = fpSize(fractionWords);
		for (size_t j = static_cast<size_t>(words); j < magnitude.size(); j++)
			if (magnitude[j] != 0) throw out_of_range("'" + text + "' does not fit the fixed-point format");
		magnitude.resize(static_cast<size_t>(words), 0u);
		if (magnitude[words - 1] & SIGN_BIT) throw out_of_range("'" + text + "' does not fit the fixed-point format");

		for (int j = 0; j < words; j++) out[j] = magnitude[words - 1 - j];
		if (negative) {   // two's complement: invert and add one
			ulong carry = 1;
			for (int j = words - 1; j >= 0; j--) {
				const ulong v = ulong(~out[j]) + carry;
				out[j] = static_cast<uint>(v & WORD_MASK);
				carry = v >> WORD_BITS;
			}
		}
	}

	void floatingToFixedPoint(double decimal, uint* out, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::floatingToFixed<decltype(f)::value>(decimal, out); });
	}

	double fixedToFloatingPoint(const uint* fpNum, int fractionWords) {
		return withFormat(fractionWords, [&](auto f) { return core::fixedToFloating<decltype(f)::value>(fpNum); });
	}

	void randomFromRange(const uint* min, const uint* max, uint* result, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::randomFromRange<decltype(f)::value>(min, max, result); });
	}
}
