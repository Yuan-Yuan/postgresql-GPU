#ifndef POSTGRES_GPU_H
#define POSTGRES_GPU_H
#include <CL/cl.h>

/*
 * GPU table related info.
 */

struct gpuTable{
    long size;      /* Total table size */
    int colNum;     /* Number of columns in the table */

    char * memory;   /* host pinned memory to hold all table data (row-store) */
    char * gpuMemory; /* GPU memory to hold all the table data */
};

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
     * Device related info.
     */ 

    cl_ulong gl_mem_size;
    cl_ulong max_alloc_size;

    /*
     * OpenCL table related parameters. 
     */

    struct gpuTable * table;    /* The table data */
    int tableNum;               /* Number of tables */
    long totalSize;             /* the total size of all the table data */
};

enum{
    ONGPU = 1123   /* a random number */
};

#endif
