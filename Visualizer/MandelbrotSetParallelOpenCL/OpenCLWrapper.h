#pragma once

#include <vector>
#include <string> // For std::string
#include <fstream> // For std::ifstream, std::istreambuf_iterator
#include <iostream> // For std::cerr, std::cout
#include <stdlib.h> // For exit
#include <CL/cl.h> // OpenCL headers

#include "errors.h"
#include "OpenCLWrapper.h" 

#ifndef CALCULATE_ITERS_H
#define CALCULATE_ITERS_H

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

struct Complex {
    double real;
    double imag;
};

struct ComplexHP {
    unsigned int real[4]; // 4 bytes for whole part and 12 bytes for fraction part, using big endian
    unsigned int imag[4];
};

OpenclDeviceSetupInfo setupOpenclDevices();

void printError(const cl_program& program, const cl_device_id& device);

// Define the templated calculateIters function directly in the header
template<typename T_ComplexType>
int calculateIters(const std::vector<T_ComplexType>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter, const char* kernelFilename, const char* compile_options = NULL)
{
    // Use the macro from errors.h, assuming it's available
    // Otherwise, you'd need to define it or include its actual definition here
#define SIMPLE_CHECK_ERRORS(ERR)        \
        if(ERR != CL_SUCCESS)                  \
    {                                      \
        std::cerr                                   \
        << "OpenCL error with code " << ERR    \
        << " happened in file " << __FILE__    \
        << " at line " << __LINE__             \
        << ". Exiting...\n";                   \
        exit(1);                               \
    }

    OpenclDeviceSetupInfo deviceInfo = setupOpenclDevices();
    cl_int err = deviceInfo.err;

    // -----------------------------------------------------------------------
    // 8. Create memory buffers

    cl_mem device_buffer_input;
    cl_mem device_buffer_output;

    device_buffer_input = clCreateBuffer(
        deviceInfo.context,         /* context */
        CL_MEM_READ_ONLY,           /* flags */
        sizeof(T_ComplexType) * size, /* size */
        NULL,                       /* host_ptr */
        &err                        /* errcode_ret */
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
    // 9. Transfer data from the host memory to the device memory

    err = clEnqueueWriteBuffer(
        deviceInfo.cmd_queue,       /* command_queue */
        device_buffer_input,        /* buffer */
        CL_TRUE,                    /* blocking_write */
        0,                          /* offset */
        sizeof(T_ComplexType) * size, /* size */
        points.data(),              /* ptr */
        NULL,                       /* num_events_in_wait_list */
        NULL,                       /* event_wait_list */
        NULL                        /* event */
    );

    SIMPLE_CHECK_ERRORS(err);

    // -----------------------------------------------------------------------
    // 10. Create and compile OpenCL program

    std::ifstream kernelFileStream(kernelFilename);
    std::string kernelSrcFileContent((std::istreambuf_iterator<char>(kernelFileStream)), std::istreambuf_iterator<char>());
    const char* kernelSrc = kernelSrcFileContent.c_str();

    cl_program program = clCreateProgramWithSource(
        deviceInfo.context,         /* context */
        1,                          /* count */
        &kernelSrc,                 /* strings */
        NULL,                       /* lengths */
        &err                        /* errcode_ret */
    );
    SIMPLE_CHECK_ERRORS(err);

    // Compile Program object
    err = clBuildProgram(
        program,            /* program */
        1,                  /* num_devices */
        deviceInfo.devices, /* device_list */
        compile_options,    /* options - now configurable */
        NULL,               /* pfn_notify */
        NULL                /* user_data */
    );
    // You might want to print build errors only if an error occurred, or always for debugging
    // printError(program, deviceInfo.devices[0]); // Keep this line for debugging build issues
    SIMPLE_CHECK_ERRORS(err);


    // -----------------------------------------------------------------------
    // 11. Create kernel

    cl_kernel kernel = NULL;
    kernel = clCreateKernel(
        program,                    /* program */
        "calculateIters",           /* kernel_name - needs to match function name inside kernel */
        &err                        /* errcode_ret */
    );

    SIMPLE_CHECK_ERRORS(err);
    // -----------------------------------------------------------------------
    // 12. Set kernel function argument list

    err = clSetKernelArg(
        kernel,                 /* kernel */
        0,                      /* arg_index */
        sizeof(cl_mem),         /* arg_size */
        &device_buffer_input    /* arg_value */
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

    size_t n_dim = 1;
    size_t global_work_size[1] = { size };
    size_t local_work_size[1] = { 100 };    // Maximum work size is 1024

    // -----------------------------------------------------------------------
    // 14. Enqueue (run) the kernel(s)

    err = clEnqueueNDRangeKernel(
        deviceInfo.cmd_queue,   /* command_queue */
        kernel,                 /* kernel */
        n_dim,                  /* work_dim */
        NULL,                   /* global_work_offset */
        global_work_size,       /* global_work_size */
        local_work_size,        /* local_work_size, also referred to as the size of the work-group */
        NULL,                   /* num_events_in_wait_list */
        NULL,                   /* event_wait_list */
        NULL                    /* event */
    );
    SIMPLE_CHECK_ERRORS(err);

    // -----------------------------------------------------------------------
    // 15. Get results (output buffer) from global device memory

    err = clEnqueueReadBuffer(
        deviceInfo.cmd_queue,   /* command_queue */
        device_buffer_output,   /* buffer */
        CL_TRUE,                /* blocking_read */
        0,                      /* offset */
        sizeof(int) * size,     /* size */
        iters.data(),           /* ptr */
        NULL,                   /* num_events_in_wait_list */
        NULL,                   /* event_wait_list */
        NULL                    /* event */
    );


    // -----------------------------------------------------------------------
    // 16. Clean up OpenCL resources
    err = clReleaseMemObject(device_buffer_input); SIMPLE_CHECK_ERRORS(err);
    err = clReleaseMemObject(device_buffer_output); SIMPLE_CHECK_ERRORS(err);
    err = clReleaseKernel(kernel); SIMPLE_CHECK_ERRORS(err);
    err = clReleaseProgram(program); SIMPLE_CHECK_ERRORS(err);
    err = clReleaseCommandQueue(deviceInfo.cmd_queue); SIMPLE_CHECK_ERRORS(err);
    err = clReleaseContext(deviceInfo.context); SIMPLE_CHECK_ERRORS(err);
    free(deviceInfo.devices);

    return CL_SUCCESS;
}

#endif // CALCULATE_ITERS_H