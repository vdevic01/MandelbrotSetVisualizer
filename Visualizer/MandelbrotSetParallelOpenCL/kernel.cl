typedef struct {
	double real;
	double imag;
} Complex;

__kernel void calculateIters(__global Complex* IN, __global int* OUT, const unsigned int max_iter)
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
	for (int i = 0; i < max_iter; i++) {
		if (idx == 0) {
			printf("=================\n");
			printf("X: %.15f\n", x);
			printf("Y: %.15f\n ", y);
		}
		y = (x + x) * y + y0;
		x = x2 - y2 + x0;
		x2 = x * x;
		y2 = y * y;
		if (idx == 0) {
			printf("=================\n");
			printf("x2: %.15f\n ", x2);

			printf("y2: %.15f\n ", y2);

			printf("temp: %.15f \n", x2 +  y2);
		}
		if (x2 + y2 > 4) {
			result = i;
			break;
		}
	}

	OUT[idx] = result;

	return;
}