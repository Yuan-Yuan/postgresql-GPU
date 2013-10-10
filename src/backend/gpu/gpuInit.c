#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "postgres.h"
#include "gpu/gpu.h"

/*
 * The current dir is datadir, and the kernel file is located in sharedir.
 */

static char * kernelPath = "../share/postgresql/kernel.cl";

static const char * createProgram(int *num){
    char * res;
    FILE *fp;
    int fsize;

    fp = fopen(kernelPath, "r");

    if(!fp){
        printf("Failed to open OpenCL kernel file\n");
    }

    fsize = fseek(fp, SEEK_END, 0);

    res = palloc(fsize+1);
    memset(res, 0, fsize + 1);

    fread(res,fsize,1,fp);

    fclose(fp);

    return res;
}

static void deviceInit(struct clContext * context){
    cl_uint numP;
    cl_int error = 0;
    cl_platform_id * pid;
    cl_device_id device;
    cl_command_queue_properties prop = 0;
    int psc;
    const char * ps;
    
    clGetPlatformIDs(0, NULL, &numP);
    if (numP <1)
        return -1;

    pid = (cl_platform_id*)palloc(numP * sizeof(cl_platform_id));
    clGetPlatformIDs(numP, pid, NULL);
    clGetDeviceIDs(pid[0], CL_DEVICE_TYPE_CPU, 1,&device, NULL);
    context->context = clCreateContext(0,1,&device,NULL,NULL,&error);
    prop |= CL_QUEUE_PROFILING_ENABLE;
    context->queue = clCreateCommandQueue(context->context, device, prop,&error);

    ps = createProgram(&psc);

    context->program = clCreateProgramWithSource(context->context,psc,(const char**)&ps, 0 , &error);

    if(error != CL_SUCCESS){
        printf("Failed to create OpenCL program\n");
    }

    error = clBuildProgram(context->program, 0,0, "-I .", 0, 0);

    if(error != CL_SUCCESS){
        printf("Failed to build OpenCL program\n");
    }

}

int gpuStart(struct clContext * context){

    int res = 0;

    /*
     * Intialize GPU devices.
     * Currently we simply use the first device to process queries.
     * It May be an OpenCL-supported CPU.
     */ 

    deviceInit(context);
    return 0;
}
