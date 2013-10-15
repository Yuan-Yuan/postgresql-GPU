#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

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

    *num = 1;
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp,0,SEEK_SET);

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
    
    clGetPlatformIDs(0, NULL, &numP);
    if (numP <1){
        printf("Failed to get platform ID\n");
        return;
    }

    pid = (cl_platform_id*)palloc(numP * sizeof(cl_platform_id));
    clGetPlatformIDs(numP, pid, NULL);
    error = clGetDeviceIDs(pid[0], CL_DEVICE_TYPE_CPU, 1,&device, NULL);
    if(error != CL_SUCCESS){
        printf("Failed to get OpenCL device ID %d\n",error);
    }

    error = clGetDeviceInfo(device,CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(cl_ulong),&(context->gl_mem_size),NULL);
    if(error != CL_SUCCESS){
        printf("Failed to get OpenCL device memory size %d\n",error);
    }

    error = clGetDeviceInfo(device,CL_DEVICE_MAX_MEM_ALLOC_SIZE,sizeof(cl_ulong),&(context->max_alloc_size),NULL);
    if(error != CL_SUCCESS){
        printf("Failed to get OpenCL max alloc memory size %d\n",error);
    }

    context->context = clCreateContext(0,1,&device,NULL,NULL,&error);
    prop |= CL_QUEUE_PROFILING_ENABLE;
    context->queue = clCreateCommandQueue(context->context, device, prop,&error);
    if(error != CL_SUCCESS){
        printf("Failed to create OpenCL command queue %d\n",error);
    }

    context->ps = createProgram(&psc);

    context->program = clCreateProgramWithSource(context->context,psc,(const char**)&(context->ps), 0 , &error);

    if(error != CL_SUCCESS){
        printf("Failed to create OpenCL program %d\n",error);
    }

    error = clBuildProgram(context->program, 0,0, "-I .", 0, 0);

    if(error != CL_SUCCESS){
        printf("Failed to build OpenCL program %d\n",error);
    }

    context->table = NULL;
    context->totalSize = 0;

}

/*
 * @gpuStart: must be called before offloading query to GPU.
 */

void gpuStart(struct clContext * context){

    /*
     * Intialize GPU devices.
     * Currently we simply use the first device to process queries.
     * It May be an OpenCL-supported CPU.
     */ 

    deviceInit(context);
}

/*
 * @gpuStop: release OpenCL context. Called when query ends.
 */

void gpuStop(struct clContext * context){
    int i = 0;
    assert(context != NULL);

    if(context->table != NULL){
        for(i = 0;i<context->tableNum;i++){
            clReleaseMemObject(context->table[i].memory);
            clReleaseMemObject(context->table[i].gpuMemory);
        }
        pfree(context->table);
    }
    clFinish(context->queue);
    clReleaseCommandQueue(context->queue);
    clReleaseContext(context->context);
    clReleaseProgram(context->program);
    pfree(context->ps);
}
