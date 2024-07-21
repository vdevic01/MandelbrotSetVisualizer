#pragma once

#ifndef FIXED_POINT_H
#define FIXED_POINT_H

namespace fpa {

	typedef unsigned int uint;
	typedef unsigned long long ulong;

	// Constants
	constexpr int WHOLE_PART = 1;
	constexpr int FRACTION_PART = 3;
	constexpr int WHOLE_BITS = WHOLE_PART * 32;
	constexpr int FRACTION_BITS = FRACTION_PART * 32;
	constexpr int FP_SIZE = WHOLE_PART + FRACTION_PART;
	constexpr int FP_BUFFER_SIZE = FP_SIZE * 2;

	// Function declarations
	void addFixed(const uint* a, const uint* b, uint c[FP_SIZE]);
	void incFixed(const uint* a, uint c[FP_SIZE]);
	void cmplFixed(const uint* a, uint c[FP_SIZE]);
	void subFixed(const uint* a, const uint* b, uint c[FP_SIZE]);
	bool gtFixed(const uint* a, const uint* b);
	bool gteFixed(const uint* a, const uint* b);
	void mulCmplFixed(const uint* a, const uint* b, uint c[FP_SIZE]);

} // namespace FixedPoint

#endif // FIXED_POINT_H
