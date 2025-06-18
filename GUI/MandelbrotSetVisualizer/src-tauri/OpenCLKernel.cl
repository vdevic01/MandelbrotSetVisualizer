typedef struct {
	float real;
	float imag;
} Complex;

__kernel void calculateIters(__global Complex* IN, __global int* OUT, const unsigned int max_iter)
{
	int idx = get_global_id(0);
	Complex c = IN[idx];
	
	float x0 = c.real;
	float y0 = c.imag;

	float x2 = 0;
	float y2 = 0;

	float x = 0;
	float y = 0;
	
	int result = -1;
	for (int i = 0; i < max_iter; i++) {
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