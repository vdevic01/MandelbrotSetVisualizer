typedef struct {
	double real;
	double imag;
} Complex;

__kernel void calculateIters(__global Complex* IN, __global int* OUT)
{
	int idx = get_global_id(0);
	Complex c = IN[idx];
	
	double x0 = c.real;
	double y0 = c.imag;

	double x2 = 0;
	double y2 = 0;

	double x = 0;
	double y = 0;

	int iteration = 0;
	
	int result = -1;
	for (int i = 0; i < 650; i++) {
		y = (x + x) * y + y0;
		x = x2 - y2 + x0;
		x2 = x * x;
		y2 = y * y;
		if (x2 + y2 > 4) {
			result = i;
			break;
		}
	}

	OUT[idx] = result;

	return;
}