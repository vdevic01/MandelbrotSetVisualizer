#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <fstream>

#include <CL/cl.h>

#include "errors.h"
#include "OpenCLWrapper.h"

using namespace std;


typedef struct _opencl_dev_info {
	cl_device_type type;
	const char* name;
	cl_uint count;
} T_opencl_dev_info;

typedef struct {
	cl_context context;
	cl_int err;
	cl_command_queue cmd_queue;
	cl_device_id* devices;
} OpenclDeviceSetupInfo;

// For now only Intel and AMD are supported
//#ifdef OPENCL_ARCH_INTEL
//#define OPENCL_TARGET_PLATFORM "Intel"
//#else
//#define OPENCL_TARGET_PLATFORM "AMD Accelerated Parallel Processing"
//#endif
#define OPENCL_TARGET_PLATFORM "NVIDIA CUDA"

#define UTILIZE_OPENCL_CPU 0
#define UTILIZE_OPENCL_GPU 1
#define UTILIZE_OPENCL_ACC 2

// Error handling strategy for this example is fairly simple -- just print
// a message and terminate the application if something goes wrong
#define SIMPLE_CHECK_ERRORS(ERR)        \
	if(ERR != CL_SUCCESS)                  \
{                                      \
	cerr                                   \
	<< "OpenCL error with code " << ERR    \
	<< " happened in file " << __FILE__    \
	<< " at line " << __LINE__             \
	<< ". Exiting...\n";                   \
	exit(1);                               \
}

OpenclDeviceSetupInfo setupOpenclDevices(){
	// The following variable stores return codes for all OpenCL calls
// In the code it is used with SIMPLE_CHECK_ERRORS macro
	cl_int err = CL_SUCCESS;

	//-----------------------------------------------------------------------
	// 2. Query for all available OpenCL platforms on the system

	cl_uint num_of_platforms = 0;
	// Get total number of available platforms
	err = clGetPlatformIDs(0, 0, &num_of_platforms);
	SIMPLE_CHECK_ERRORS(err);
	cout << "Number of available platforms: " << num_of_platforms << endl;

	cl_platform_id* platforms = new cl_platform_id[num_of_platforms];
	// Get IDs for all platforms
	err = clGetPlatformIDs(num_of_platforms, platforms, 0);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 3. List all platforms and select one
	// We use platform name to select needed platform

	// Default substring for platform name
	const char* required_platform_subname = OPENCL_TARGET_PLATFORM;

	cl_uint selected_platform_index = num_of_platforms;

	cout << "Platform names:\n";

	for (cl_uint i = 0; i < num_of_platforms; ++i)
	{
		// Get the length for the i-th platform name
		size_t platform_name_length = 0;
		err = clGetPlatformInfo(
			platforms[i],				/* platform */
			CL_PLATFORM_NAME,			/* param_name */
			NULL,						/* param_value_size */
			NULL,						/* param_value */
			&platform_name_length		/* param_value_size_ret */
		);
		SIMPLE_CHECK_ERRORS(err);

		// Get the name itself for the i-th platform
		char* platform_name = new char[platform_name_length];
		err = clGetPlatformInfo(
			platforms[i],
			CL_PLATFORM_NAME,
			platform_name_length,
			platform_name,
			NULL
		);
		SIMPLE_CHECK_ERRORS(err);

		cout << "    [" << i << "] " << platform_name;

		// Decide if this i-th platform is what we are looking for
		// We select the first one matched skipping the next one if any
		if (
			strstr(platform_name, required_platform_subname) &&
			selected_platform_index == num_of_platforms // Have not selected yet
			)
		{
			cout << " [Selected]";
			selected_platform_index = i;
			// Do not stop here, just see all available platforms
		}

		cout << endl;
		delete[] platform_name;
	}

	if (selected_platform_index == num_of_platforms)
	{
		cerr
			<< "There is no found platform with name containing \""
			<< required_platform_subname << "\" as a substring.\n";
		exit(1);
	}

	cl_platform_id platform = platforms[selected_platform_index];

	// -----------------------------------------------------------------------
	// 4. Let us see how many devices of each type are provided for the
	// selected platform

	// Use the following handy array to store all device types of your interest
	// The array helps to build simple loop queries in the code below

	T_opencl_dev_info
		all_devices[] =
	{
		{ CL_DEVICE_TYPE_CPU, "CL_DEVICE_TYPE_CPU", 0 },
		{ CL_DEVICE_TYPE_GPU, "CL_DEVICE_TYPE_GPU", 0 },
		{ CL_DEVICE_TYPE_ACCELERATOR, "CL_DEVICE_TYPE_ACCELERATOR", 0 }
	};

	const int NUM_OF_DEVICE_TYPES = sizeof(all_devices) / sizeof(all_devices[0]);

	cout << "Number of devices available for each type:\n";

	// Now iterate over all device types picked above and initialize count
	for (int i = 0; i < NUM_OF_DEVICE_TYPES; ++i)
	{
		err = clGetDeviceIDs(
			platform,					/* platform */
			all_devices[i].type,		/* device_type */
			NULL,						/* num_entries */
			NULL,						/* devices */
			&all_devices[i].count		/* num_devices */
		);

		if (CL_DEVICE_NOT_FOUND == err)
		{
			// That's OK to fall here, because not all types of devices, which
			// you query for may be available for a particular system
			all_devices[i].count = 0;
			err = CL_SUCCESS;
		}

		SIMPLE_CHECK_ERRORS(err);

		cout
			<< "    " << all_devices[i].name << ": "
			<< all_devices[i].count << endl;
	}

	// -----------------------------------------------------------------------
	// 5. Get all devices IDs of specific type: GPU/CPU/ACCELERATOR
	// We are going to use the GPU, CPU, or ACCELERATOR, NOT ALL at the same time

	const unsigned int device_type = UTILIZE_OPENCL_GPU;
	cl_uint device_num = all_devices[device_type].count;
	cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * device_num);

	err = clGetDeviceIDs(
		platform,
		all_devices[device_type].type,
		device_num,
		devices,
		&device_num
	);

	SIMPLE_CHECK_ERRORS(err);

	for (cl_uint j = 0; j < device_num; j++) {
		char deviceName[128];
		clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 128, deviceName, nullptr);
		std::cout << "Device: " << deviceName << std::endl;

		char openclVersion[128];
		clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, 128, openclVersion, nullptr);
		std::cout << "    OpenCL Version: " << openclVersion << std::endl;
	}

	// -----------------------------------------------------------------------
	// 6. Create OpenCL context
	// We are going to use the GPU or CPU, NOT both

	cl_context context;
	context = clCreateContext(
		NULL,					/* properties */
		device_num,				/* num_devices */
		devices,				/* devices */
		NULL,					/* pfn_notify */
		NULL,					/* user_data */
		&err					/* errcode_ret */
	);

	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 7. Create command queue(s) and add device(s)

	cl_command_queue cmd_queue;
	cmd_queue = clCreateCommandQueueWithProperties(
		context,				/* context */
		*devices,				/* devices[0] */
		NULL,					/* properties */
		&err					/* errcode_ret */
	);

	SIMPLE_CHECK_ERRORS(err);
	delete[] platforms;
	OpenclDeviceSetupInfo output;
	output.cmd_queue = cmd_queue;
	output.context = context;
	output.err = err;
	output.devices = devices;
	return output;
}

// Used to print log file content in case of error
// Log file may be empty despite error happening
void printError(const cl_program& program, const cl_device_id& device) {
	// Determine the size of the log
	size_t log_size;
	clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);

	// Allocate memory for the log
	char* log = (char*)malloc(log_size);

	// Get the log
	clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);

	// Print the log
	printf("%s\n", log);
}

int calculateIters(Complex* points, int* iters, unsigned int size, unsigned int max_iter)
{
	OpenclDeviceSetupInfo deviceInfo = setupOpenclDevices();
	cl_int err = deviceInfo.err;

	// -----------------------------------------------------------------------
	// 8. Create memory buffers

	cl_mem device_buffer_input;
	cl_mem device_buffer_output;

	device_buffer_input = clCreateBuffer(
		deviceInfo.context,			/* context */
		CL_MEM_READ_ONLY,			/* flags */
		sizeof(Complex) * size,		/* size */
		NULL,						/* host_ptr */
		&err						/* errcode_ret */
	);

	SIMPLE_CHECK_ERRORS(err);

	device_buffer_output = clCreateBuffer(
		deviceInfo.context,
		CL_MEM_WRITE_ONLY,
		sizeof(int) * size,
		NULL,
		&err
	);

	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 9. Tranfer data from the host memory to the device memory

	// Transfer data from host_buffer_A to device_buffer_A
	err = clEnqueueWriteBuffer(
		deviceInfo.cmd_queue,		/* command_queue */
		device_buffer_input,		/* buffer */
		CL_TRUE,					/* blocking_write */
		0,							/* offset */
		sizeof(Complex) * size,		/* size */
		points,						/* ptr */
		NULL,						/* num_events_in_wait_list */
		NULL,						/* event_wait_list */
		NULL						/* event */
	);

	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 10. Create and compile OpenCL program

	ifstream kernelFileStream("kernel.cl");
	std::string kernelSrcFileContent((std::istreambuf_iterator<char>(kernelFileStream)), std::istreambuf_iterator<char>());
	const char* kernelSrc = kernelSrcFileContent.c_str();

	// Create Progam object
	cl_program program = clCreateProgramWithSource(
		deviceInfo.context,					/* context */
		1,									/* count */
		&kernelSrc,							/* strings */
		NULL,								/* lengths */
		&err								/* errcode_ret */
	);
	SIMPLE_CHECK_ERRORS(err);

	// Compile Program object
	err = clBuildProgram(
		program,			/* program */
		1,					/* num_devices */
		deviceInfo.devices,	/* device_list */
		NULL,				/* options */
		NULL,				/* pfn_notify */
		NULL				/* user_data */
	);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 11. Create kernel

	cl_kernel kernel = NULL;
	kernel = clCreateKernel(
		program,					/* program */
		"calculateIters",			/* kernel_name - needs to match function name inside kernel */
		&err						/* errcode_ret */
	);	
	
	SIMPLE_CHECK_ERRORS(err);
	// -----------------------------------------------------------------------
	// 12. Set kernel function argument list

	err = clSetKernelArg(
		kernel,					/* kernel */
		0,						/* arg_index */
		sizeof(cl_mem),			/* arg_size */
		&device_buffer_input	/* arg_value */
	);
	SIMPLE_CHECK_ERRORS(err);

	err = clSetKernelArg(
		kernel,
		1,
		sizeof(cl_mem),
		&device_buffer_output
	);
	SIMPLE_CHECK_ERRORS(err);

	cl_int max_iter_kernel = max_iter;
	err = clSetKernelArg(
		kernel,
		2,
		sizeof(unsigned int),
		&max_iter_kernel
	);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------	
	// 13. Define work-item and work-group

	//size_t n_dim = 3;
	//size_t global_work_size[3] = {data_size, 1, 1};
	//size_t local_work_size[3]= {64, 1, 1};

	size_t n_dim = 1;
	size_t global_work_size[1] = { size };
	size_t local_work_size[1] = { 100 };	// Maximum work size is 1024

	// -----------------------------------------------------------------------
	// 14. Enqueue (run) the kernel(s)

	err = clEnqueueNDRangeKernel(
		deviceInfo.cmd_queue,	/* command_queue */
		kernel,					/* kernel */
		n_dim,					/* work_dim */
		NULL,					/* global_work_offset */
		global_work_size,		/* global_work_size */
		local_work_size,		/* local_work_size, also referred to as the size of the work-group */
		NULL,					/* num_events_in_wait_list */
		NULL,					/* event_wait_list */
		NULL					/* event */
	);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 15. Get results (output buffer) from global device memory

	err = clEnqueueReadBuffer(
		deviceInfo.cmd_queue,	/* command_queue */
		device_buffer_output,	/* buffer */
		CL_TRUE,				/* blocking_read */
		0,						/* offset */
		sizeof(int) * size,		/* size */
		iters,					/* ptr */
		NULL,					/* num_events_in_wait_list */
		NULL,					/* event_wait_list */
		NULL					/* event */
	);


	// -----------------------------------------------------------------------
	// 17. Free alocated resources
	free(deviceInfo.devices);

	return CL_SUCCESS;
}

int calculateItersHighPrecision(ComplexHP* points, int* iters, unsigned int size, unsigned int max_iter) {
	OpenclDeviceSetupInfo deviceInfo = setupOpenclDevices();
	cl_int err = deviceInfo.err;

	// -----------------------------------------------------------------------
	// 8. Create memory buffers

	cl_mem device_buffer_input;
	cl_mem device_buffer_output;

	device_buffer_input = clCreateBuffer(
		deviceInfo.context,			/* context */
		CL_MEM_READ_ONLY,			/* flags */
		sizeof(ComplexHP) * size,	/* size */
		NULL,						/* host_ptr */
		&err						/* errcode_ret */
	);

	SIMPLE_CHECK_ERRORS(err);

	device_buffer_output = clCreateBuffer(
		deviceInfo.context,
		CL_MEM_WRITE_ONLY,
		sizeof(int) * size,
		NULL,
		&err
	);

	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 9. Tranfer data from the host memory to the device memory

	// Transfer data from host_buffer_A to device_buffer_A
	err = clEnqueueWriteBuffer(
		deviceInfo.cmd_queue,		/* command_queue */
		device_buffer_input,		/* buffer */
		CL_TRUE,					/* blocking_write */
		0,							/* offset */
		sizeof(ComplexHP) * size,	/* size */
		points,						/* ptr */
		NULL,						/* num_events_in_wait_list */
		NULL,						/* event_wait_list */
		NULL						/* event */
	);

	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 10. Create and compile OpenCL program

	ifstream kernelFileStream("kernelHP.cl");
	std::string kernelSrcFileContent((std::istreambuf_iterator<char>(kernelFileStream)), std::istreambuf_iterator<char>());
	const char* kernelSrc = kernelSrcFileContent.c_str();

	// Create Progam object
	cl_program program = clCreateProgramWithSource(
		deviceInfo.context,					/* context */
		1,									/* count */
		&kernelSrc,							/* strings */
		NULL,								/* lengths */
		&err								/* errcode_ret */
	);
	SIMPLE_CHECK_ERRORS(err);

	// Compile Program object
	err = clBuildProgram(
		program,			/* program */
		1,					/* num_devices */
		deviceInfo.devices,	/* device_list */
		NULL,				/* options */
		NULL,				/* pfn_notify */
		NULL				/* user_data */
	);
	printError(program, deviceInfo.devices[0]);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 11. Create kernel

	cl_kernel kernel = NULL;
	kernel = clCreateKernel(
		program,					/* program */
		"calculateIters",			/* kernel_name - needs to match function name inside kernel */
		&err						/* errcode_ret */
	);

	SIMPLE_CHECK_ERRORS(err);
	// -----------------------------------------------------------------------
	// 12. Set kernel function argument list

	err = clSetKernelArg(
		kernel,					/* kernel */
		0,						/* arg_index */
		sizeof(cl_mem),			/* arg_size */
		&device_buffer_input	/* arg_value */
	);
	SIMPLE_CHECK_ERRORS(err);

	err = clSetKernelArg(
		kernel,
		1,
		sizeof(cl_mem),
		&device_buffer_output
	);
	SIMPLE_CHECK_ERRORS(err);

	cl_int max_iter_kernel = max_iter;
	err = clSetKernelArg(
		kernel,
		2,
		sizeof(unsigned int),
		&max_iter_kernel
	);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------	
	// 13. Define work-item and work-group

	//size_t n_dim = 3;
	//size_t global_work_size[3] = {data_size, 1, 1};
	//size_t local_work_size[3]= {64, 1, 1};

	size_t n_dim = 1;
	size_t global_work_size[1] = { size };
	size_t local_work_size[1] = { 100 };	// Maximum work size is 1024

	// -----------------------------------------------------------------------
	// 14. Enqueue (run) the kernel(s)

	err = clEnqueueNDRangeKernel(
		deviceInfo.cmd_queue,	/* command_queue */
		kernel,					/* kernel */
		n_dim,					/* work_dim */
		NULL,					/* global_work_offset */
		global_work_size,		/* global_work_size */
		local_work_size,		/* local_work_size, also referred to as the size of the work-group */
		NULL,					/* num_events_in_wait_list */
		NULL,					/* event_wait_list */
		NULL					/* event */
	);
	SIMPLE_CHECK_ERRORS(err);

	// -----------------------------------------------------------------------
	// 15. Get results (output buffer) from global device memory

	err = clEnqueueReadBuffer(
		deviceInfo.cmd_queue,	/* command_queue */
		device_buffer_output,	/* buffer */
		CL_TRUE,				/* blocking_read */
		0,						/* offset */
		sizeof(int) * size,		/* size */
		iters,					/* ptr */
		NULL,					/* num_events_in_wait_list */
		NULL,					/* event_wait_list */
		NULL					/* event */
	);


	// -----------------------------------------------------------------------
	// 17. Free alocated resources
	free(deviceInfo.devices);

	return CL_SUCCESS;
}