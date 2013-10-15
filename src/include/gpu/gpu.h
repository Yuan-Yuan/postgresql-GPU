#ifndef POSTGRES_GPU_H
#define POSTGRES_GPU_H
#include <CL/cl.h>

struct clContext{
    /*
     * OpenCL related parameters
     */ 
    cl_context context;         /* OpenCL context */
    cl_command_queue queue;     /* OpenCL command queue */
    cl_program program;         /* OpenCL program */
    cl_kernel kernel;           /* OpenCL kernel */
    const char * ps;            /* pointing to the memory space allocated for OpenCL kernel file */

    /*
     * OpenCL memory related parameters
     */

    char *  memory;     /* pointing to the allocated memory space */
    long size;          /* the total size of the memory*/
};

enum{
    ONGPU = 1123   /* a random number */
};

#endif
