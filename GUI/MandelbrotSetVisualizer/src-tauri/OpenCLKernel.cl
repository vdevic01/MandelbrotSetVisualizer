typedef struct {
	double real;
	double imag;
} Complex;

// OUT: escape iteration (-1 if the point never escaped). OUT_MAG: |z|^2 at that iteration.
// escape_sq: the squared escape radius.
__kernel void calculateIters(__global Complex* IN, __global int* OUT, const unsigned int max_iter,
	__global float* OUT_MAG, const double escape_sq)
{
	int idx = get_global_id(0);
	Complex c = IN[idx];
	
	double x0 = c.real;
	double y0 = c.imag;

	double x2 = 0;
	double y2 = 0;

	double x = 0;
	double y = 0;
	
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
	}

	OUT[idx] = result;
	OUT_MAG[idx] = escapeValue;

	return;
}