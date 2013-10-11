#ifndef POSTGRES_GPU_H
#define POSTGRES_GPU_H
#include <CL/cl.h>

struct clContext{
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
};

enum{
    ONGPU = 1123   /* a random number */
};

#endif
