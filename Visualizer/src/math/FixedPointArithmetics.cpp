#include "FixedPointArithmetics.h"
#include "FixedPointCore.h"

#include <stdexcept>
#include <string>
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

	void divFixed(const uint* a, const uint* b, uint* c, int fractionWords) {
		withFormat(fractionWords, [&](auto f) { core::div<decltype(f)::value>(a, b, c); });
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
