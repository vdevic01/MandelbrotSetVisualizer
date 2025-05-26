#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>

#include <CL/cl.h>

#include "errors.h"
#include "ParallelIterationCalculator.h"

using namespace std;

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