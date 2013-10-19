#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "utils/rel.h"
#include "gpu/gpu.h"

/*
 * The current dir is datadir, and the kernel file is located in sharedir.
 */

static char * kernelPath = "../share/postgresql/kernel.cl";


/*
 * CreateProgram: load the OpenCL kernel file.
 */
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

/*
 * deviceInit: create OpenCL context.
 */

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
 * gpuExprInit: initialize an expression which will be evaluted on GPU.
 */

static void gpuExprInit(Expr *cpuExpr, struct gpuTargetEntry *gpuEntry){

    switch(nodeTag(cpuExpr)){
        case T_Var:
            {
                Var * var = (Var *)cpuExpr;
                printf("Expression Type T_Var\n");
                break;
            }

        case T_Const:
            {
                Const * expr = (Const *)cpuExpr;
                printf("Expression type T_Const\n");
                break;
            }
        case T_OpExpr:
            {
                OpExpr *expr = (OpExpr *)cpuExpr;
                printf("Expression type T_OpExpr\n");
                break;
            }

        default:
            printf("Expression type not supported yet %d\n", nodeTag(cpuExpr));
            break;
    }
}

/*
 * gpuInitScan: initialize gpu Scan node.
 *  1) initialize target list
 *  2) initialize where condition
 *
 * Input:
 *    @planstate: query node state from postgresql
 *
 * Return:
 *    A new gpuScanNode.
 */

static struct gpuScanNode* gpuInitScan(PlanState *planstate){

    struct gpuScanNode *scannode = NULL;
    ScanState * scanstate = (ScanState *)planstate;
    ProjectionInfo *pi = scanstate->ps.ps_ProjInfo;
    struct gpuTargetEntry *targetlist = NULL;
    ListCell *l;
    int i;

    scannode = (struct gpuScanNode*)palloc(sizeof(struct gpuScanNode));
    scannode->plan.type = GPU_SCAN;
    scannode->plan.colNum = (list_length(pi->pi_targetlist) - 1) + pi->pi_numSimpleVars; 
    targetlist = (struct gpuTargetEntry *) palloc(sizeof(struct gpuTargetEntry*) * scannode->plan.colNum);
    scannode->plan.leftPlan = NULL;
    scannode->plan.rightPlan = NULL;

    scannode->table.tid = RelationGetRelid(scanstate->ss_currentRelation);

    /* 
     * Initialize scannode target list.
     * The last entry in the targetlist is a junk entry.
     * The length of the projected list is caculated as:
     *      number of simple attributes + number of expressions.
     * resno attribute in each target entry indicates the position in the projected list.
     */

    foreach(l, pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var){
        /*
         * This means this is a junk entry. 
         */
            continue;
        }

    }

    for(i = 0;i<pi->pi_numSimpleVars;i++){

    }

    scannode->plan.targetlist = targetlist; 

    /* TBD: initialize scan node where condition */

    return scannode;
}

/*
 * gpuInitPlan: initialize gpu query plan.
 */

static struct gpuPlan * gpuInitPlan(PlanState *planstate){

    struct gpuPlan * result = NULL;

    switch(nodeTag(planstate)){
        case T_SeqScanState:
            {
                result = (struct gpuPlan*) gpuInitScan(planstate);
                printf("Initialize gpu scan: finished\n");
                break;
            }

        default:
            printf("Query node type not supported yet:%d\n",nodeTag(planstate));
            break;
    }

    return result;
}

/*
 * gpuInitSnapshot: initialize the snapshot data on GPU.
 *
 * intput:
 *  @gpuSp: the GPU snapshot data to be initialized.
 *  @cpuSp: the CPU snapshot data.
 */

static void gpuInitSnapshot(struct gpuSnapshot *gpuSp, Snapshot cpuSp){
    int i = 0;

    gpuSp->xmin = cpuSp->xmin;
    gpuSp->xmax = cpuSp->xmax;
    gpuSp->xcid = cpuSp->curcid;
    gpuSp->xcnt = cpuSp->xcnt;
    gpuSp->xip = (int *)palloc(sizeof(int)*gpuSp->xcnt);
    for(i=0;i<gpuSp->xcnt;i++){
        gpuSp->xip[i] = cpuSp->xip[i];
    }
}

/*
 * Free the space of the gpu snapshot.
 */

static void gpuFreeSnapshot(struct gpuSnapshot *gpuSp){
    pfree(gpuSp->xip);
}

static void gpuQueryPlanInit(QueryDesc * querydesc){

    PlanState *outerPlan = outerPlanState(querydesc->planstate);
    struct gpuQueryDesc *gpuQuery;

    assert(nodeTag(querydesc->planstate) == T_LockRowsState);

    gpuQuery = (struct gpuQueryDesc *)palloc(sizeof(struct gpuQueryDesc));

    /*
     * First step: copy snapshot related data from CPU query description.
     */
    gpuInitSnapshot(&(gpuQuery->snapshot), querydesc->snapshot);

    /*
     * Second step: initialize the query plan.
     */ 
    gpuQuery->plan = gpuInitPlan(outerPlan);

}

static void gpuQueryPlanFinish(struct clContext * context){

}

/*
 * @gpuStart: must be called before offloading query to GPU.
 */

void gpuStart(QueryDesc * querydesc){

    /*
     * Intialize GPU devices.
     * Currently we simply use the first device to process queries.
     * It May be an OpenCL-supported CPU.
     */ 

    deviceInit(querydesc->context);

    /*
     * Initialize the query plan on GPU.
     * The query plan no GPU is not as complex as it is on CPU.
     */

    gpuQueryPlanInit(querydesc);
}

/*
 * @gpuStop: release OpenCL context. Called when query ends.
 */

void gpuStop(struct clContext * context){
    int i = 0;
    assert(context != NULL);
    return ;

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
