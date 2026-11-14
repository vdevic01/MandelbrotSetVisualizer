typedef struct {
	double real;
	double imag;
} Complex;

// Interior check: a point of the main cardioid or of the period-2 bulb is in the set and never escapes, so its
// iteration is skipped. The same test as isInteriorPoint in calculators/InteriorCheck.h (keep the two in step);
// the regions are shrunk by INTERIOR_MARGIN so that rounding cannot put a point on the wrong side of a boundary.
#define INTERIOR_MARGIN 1e-9

bool isInteriorPoint(double x, double y) {
	double y2 = y * y;
	double dx = x - 0.25;
	double q = dx * dx + y2;
	if (q * (q + dx) < 0.25 * y2 - INTERIOR_MARGIN) return true;   // main cardioid
	double bx = x + 1.0;
	return bx * bx + y2 < 0.0625 - INTERIOR_MARGIN;                 // period-2 bulb
}

// OUT: escape iteration (-1 if the point never escaped). OUT_MAG: |z|^2 at that iteration.
// escape_sq: the squared escape radius.
__kernel void calculateIters(__global Complex* IN, __global int* OUT, const unsigned int max_iter,
	__global float* OUT_MAG, const double escape_sq)
{
	int idx = get_global_id(0);
	Complex c = IN[idx];
	
	double x0 = c.real;
	double y0 = c.imag;

	if (isInteriorPoint(x0, y0)) {
		OUT[idx] = -1;
		OUT_MAG[idx] = 0;
		return;
	}

	double x2 = 0;
	double y2 = 0;

	double x = 0;
	double y = 0;
	
	// Periodicity check (Brent), the same as in the CPU calculator: the next state is a function of (x, y) alone, so a state exactly equal to the saved one means the orbit repeats and never escapes. The saved state is renewed at iterations 1, 2, 4, 8, ...
	double savedX = 0;
	double savedY = 0;

	int result = -1;
	float escapeValue = 0;
	for (int i = 0; i < max_iter; i++) {
		y = (x + x) * y + y0;
		x = x2 - y2 + x0;
		x2 = x * x;
		y2 = y * y;
		if (x2 + y2 > escape_sq) {
			result = i;
			escapeValue = (float)(x2 + y2);
			break;
		}
		if (x == savedX && y == savedY) break;
		if (((i + 1) & i) == 0) { savedX = x; savedY = y; }
	}

	OUT[idx] = result;
	OUT_MAG[idx] = escapeValue;

	return;
}