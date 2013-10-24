#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "utils/datum.h"
#include "catalog/pg_type.h"
#include "gpu/gpu.h"


static struct gpuExpr* gpuExprInit(Expr *);
static struct gpuExpr** gpuWhere(Expr *);

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

}
/*
 * Convert postgresql type to supported gpu type.
 */

static int pgtypeToGPUType(int typid){

    int gputype = -1;

    switch(typid){

        case INT4OID:
            gputype = GPU_INT;
            break;

        case NUMERICOID:
            gputype = GPU_DECIMAL;
            break;

        case TEXTOID:
            gputype = GPU_STRING;
            break;

        default:
            printf("Type not supported yet %d\n",typid);
    }

    return gputype;
}

/*
 * Initialize Var expression.
 */

static void gpuInitVarExpr(struct gpuVar *gpuvar, Var * var){
    gpuvar->expr.type = GPU_VAR;
    gpuvar->index = var->varattno - 1;

    assert(gpuvar->index >= 0);

    printf("Expression type T_Var\n");
}


/*
 * Initialize const expression.
 */

static void gpuInitConstExpr(struct gpuConst *gpuconst, Const * cpuconst){

    gpuconst->type = pgtypeToGPUType(cpuconst->consttype);
    gpuconst->expr.type = GPU_CONST;
    gpuconst->length = cpuconst->constlen;

    if(cpuconst->constbyval){
        gpuconst->value = (char *) palloc(gpuconst->length);
        memcpy(gpuconst->value, &(cpuconst->constvalue), gpuconst->length);

    }else{

        /* FIXME: how to handle variable length data */

        if(gpuconst->length == -1){
            gpuconst->length = datumGetSize(cpuconst->constvalue,false, -1);
        }
        gpuconst->value = (char*)palloc(gpuconst->length);
        memcpy(gpuconst->value, DatumGetPointer(cpuconst->constvalue), gpuconst->length);
    }

    printf("Expression type T_Const\n");

}

/*
 * Initialize gpuOpExpr.
 */

static void gpuInitOpExpr(struct gpuOpExpr * gpuopexpr, OpExpr *opexpr){

    char *opname = get_opname(opexpr->opno);
    ListCell *l;

    /* Currently we only support binary operators */

    assert(list_length(opexpr->args)== 2);

    if(strcmp(opname, "=") == 0){
        gpuopexpr->opType = GPU_GT;
    }else if (strcmp(opname,">=")){
        gpuopexpr->opType = GPU_GEQ;
    }else if (strcmp(opname,">")){
        gpuopexpr->opType = GPU_EQ;
    }else if (strcmp(opname,"<=")){
        gpuopexpr->opType = GPU_LEQ;
    }else if (strcmp(opname,"<")){
        gpuopexpr->opType = GPU_LT;
    }else if (strcmp(opname,"+")){
        gpuopexpr->opType = GPU_ADD;
    }else if (strcmp(opname,"-")){
        gpuopexpr->opType = GPU_MINUS;
    }else if (strcmp(opname,"*")){
        gpuopexpr->opType = GPU_MULTIPLY;
    }else if (strcmp(opname,"/")){
        gpuopexpr->opType = GPU_DIVIDE;
    }

    l = list_head(opexpr->args);
    gpuopexpr->left = gpuExprInit((Expr*)lfirst(l));

    l = l->next;
    gpuopexpr->right = gpuExprInit((Expr*)lfirst(l));
    gpuopexpr->expr.type = GPU_OPEXPR;

    printf("Expression type T_OpExpr\n");

}

/*
 * gpuExprInit: initialize an expression which will be evaluted on GPU.
 */

static struct gpuExpr* gpuExprInit(Expr *cpuExpr){

    struct gpuExpr * gpuexpr = NULL;

    switch(nodeTag(cpuExpr)){
        case T_Var:
            {
                Var * var = (Var *)cpuExpr;
                struct gpuVar * gpuvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));
                gpuInitVarExpr(gpuvar, var);

                gpuexpr = (struct gpuExpr *)gpuvar;
                break;
            }

        case T_Const:
            {
                Const * cpuconst = (Const *)cpuExpr;
                struct gpuConst *gpuconst = (struct gpuConst*)palloc(sizeof(struct gpuConst));

                gpuInitConstExpr(gpuconst, cpuconst);
                gpuexpr = (struct gpuExpr *)gpuconst;

                break;
            }
        case T_OpExpr:
            {
                OpExpr *opexpr = (OpExpr *)cpuExpr;
                struct gpuOpExpr * gpuopexpr = (struct gpuOpExpr*)palloc(sizeof(struct gpuOpExpr));

                gpuInitOpExpr(gpuopexpr,opexpr);

                gpuexpr = (struct gpuExpr *)gpuopexpr;
                break;
            }

        default:
            printf("Expression type not supported yet %d\n", nodeTag(cpuExpr));
            break;
    }

    return gpuexpr;
}

/*
 * Initialize where condition for query executed on GPUs.
 */

static struct gpuExpr ** gpuWhere(Expr * expr){

    struct gpuExpr ** gpuexpr = NULL;
    ListCell *l;
    int i = 0;

    switch(nodeTag(expr)){
        case T_List:
            {
                List * qual = (List*)expr;
                struct gpuBoolExpr * gpuboolexpr = (struct gpuBoolExpr*)palloc(sizeof(struct gpuBoolExpr));

                gpuboolexpr->opType = GPU_AND;
                gpuboolexpr->argNum = list_length(qual);
                gpuboolexpr->args = (struct gpuExpr**)palloc(gpuboolexpr->argNum * sizeof(struct gpuExpr*));

                foreach(l,qual){
                    gpuboolexpr->args[i] = *gpuWhere((Expr*)lfirst(l));
                    i+=1;
                }
                gpuexpr = (struct gpuExpr **)&gpuboolexpr;
                break;
            }

        case T_BoolExpr:
            {
                struct gpuBoolExpr *gpuboolexpr = (struct gpuBoolExpr *)palloc(sizeof(struct gpuBoolExpr));
                BoolExpr   *boolexpr = (BoolExpr *) expr;

                switch(boolexpr->boolop){
                    case AND_EXPR:
                        {
                            gpuboolexpr->opType = GPU_AND;
                            break;
                        }
                    case OR_EXPR:
                        {
                            gpuboolexpr->opType = GPU_OR;
                            break;
                        }
                    case NOT_EXPR:
                        {
                            gpuboolexpr->opType = GPU_NOT;
                            break;
                        }
                    default:
                        printf("Where expression type not supported yet!\n");
                        break;
                }

                gpuboolexpr->argNum = list_length(boolexpr->args);
                gpuboolexpr->args = (struct gpuExpr **)palloc(gpuboolexpr->argNum * sizeof(struct gpuExpr*));

                foreach(l,boolexpr->args){
                    gpuboolexpr->args[i] = * gpuWhere((Expr*)lfirst(l));
                }

                gpuexpr = (struct gpuExpr**)&gpuboolexpr;
                break;
            }

        case T_OpExpr:
            {
                struct gpuOpExpr *gpuopexpr = (struct gpuOpExpr *)palloc(sizeof(struct gpuOpExpr));
                OpExpr   *opexpr = (OpExpr *) expr;

                gpuInitOpExpr(gpuopexpr, opexpr);

                gpuexpr = (struct gpuExpr **)&gpuopexpr;
                break;
            }

        case T_Var:
            {
                struct gpuVar *gpuvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));
                Var * var = (Var*) expr;

                gpuInitVarExpr(gpuvar, var);

                gpuexpr = (struct gpuExpr **)&gpuvar;
                break;
            }

        case T_Const:
            {
                struct gpuConst *gpuconst = (struct gpuConst*)palloc(sizeof(struct gpuConst));
                Const * cpuconst = (Const *)expr;

                gpuInitConstExpr(gpuconst, cpuconst);

                gpuexpr = (struct gpuExpr **)&gpuconst;
                break;
            }

        default:
            printf("Initialize scan where: expression type not suppored yet :%d\n",nodeTag(expr));
    }

    return gpuexpr;
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
    struct gpuExpr **targetlist = NULL;
    List * qual = scanstate->ps.plan->qual;
    ListCell *l;
    int i;

    scannode = (struct gpuScanNode*)palloc(sizeof(struct gpuScanNode));
    scannode->plan.type = GPU_SCAN;
    scannode->plan.leftPlan = NULL;
    scannode->plan.rightPlan = NULL;

    /*
     * Initialize the scan table info.
     */ 

    scannode->table.tid = RelationGetRelid(scanstate->ss_currentRelation);
    scannode->table.attrNum = get_relnatts(scannode->table.tid);
    scannode->table.attrType = (int*)palloc(sizeof(int)*scannode->table.attrNum);
    scannode->table.attrSize = (int*)palloc(sizeof(int)*scannode->table.attrNum);

    /* The attrNum must start from 1 */

    for(i=1;i<=scannode->table.attrNum;i++){
        int typid = get_atttype(scannode->table.tid, i);
        scannode->table.attrSize[i] = get_typlen(typid);
        scannode->table.attrType[i] = pgtypeToGPUType(typid);
    }

    assert(pi != NULL);

    /* 
     * Initialize scannode target list.
     * The last entry in the targetlist is a junk entry.
     * The length of the projected list is caculated as:
     *      number of simple attributes + number of expressions - 1.
     * resno attribute in each target entry indicates the position in the projected list.
     */

    scannode->plan.attrNum = (list_length(pi->pi_targetlist) - 1) + pi->pi_numSimpleVars; 
    targetlist = (struct gpuExpr **) palloc(sizeof(struct gpuExpr *) * scannode->plan.attrNum);

    foreach(l, pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var){

        /*
         * This means this is a junk entry.
         */
            continue;
        }

        /* FIXME: support of complex expressions */

        targetlist[te->resno] = gpuExprInit(te->expr);

    }

    for(i = 0;i<pi->pi_numSimpleVars;i++){
        int offset = pi->pi_varOutputCols[i];
        struct gpuVar *gvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));

        gvar->expr.type = GPU_VAR;
        gvar->index = pi->pi_varNumbers[i];
        targetlist[offset] = (struct gpuExpr *) gvar;
    }

    scannode->plan.targetlist = targetlist;

    /* FIXME: support of where condition */

    if(qual){
        scannode->plan.whereNum = list_length(qual);
        scannode->plan.whereexpr = gpuWhere((Expr*)qual);
    }else{
        scannode->plan.whereNum = 0;
        scannode->plan.whereexpr = NULL;
    }

    return scannode;
}

/*
 * Free the scan node.
 */

static void gpuFreeScan(struct gpuScanNode * node){

    /* FIXME: release of each expression */
    //pfree(node->plan.targetlist);

    /* FIXME: release of other table meta data */

    clReleaseMemObject(node->table.memory);
    clReleaseMemObject(node->table.gpuMemory);

    //pfree(node);
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
                break;
            }

        /* FIXME: support other query operations */

        default:
            printf("Query node type not supported yet:%d\n",nodeTag(planstate));
            break;
    }

    return result;
}

static void gpuFreePlan(struct gpuPlan * plan){

    switch(plan->type){
        case GPU_SCAN:
            {
                gpuFreeScan((struct gpuScanNode*)plan);
                break;
            }

        default:
            printf("Query node type not suppored yet in gpuFreePlan :%d\n",plan->type);
            break;
    }

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
    //pfree(gpuSp->xip);
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

    querydesc->context->querydesc = gpuQuery;

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

static void deviceFinish(struct clContext *context){

    clFinish(context->queue);
    clReleaseCommandQueue(context->queue);
    clReleaseContext(context->context);
    clReleaseProgram(context->program);
    //pfree(context->ps);
}

static void gpuQueryPlanFinish(struct clContext * context){

    /* FIXME: release the memory of gpu query plan */

    struct gpuQueryDesc * querydesc = context->querydesc;

    /*
     * First step: free snapshot related space.
     */ 

    gpuFreeSnapshot( &(querydesc->snapshot));

    gpuFreePlan(querydesc->plan);

    //pfree(querydesc);
}

/*
 * @gpuStop: release OpenCL context. Called when query ends.
 */

void gpuStop(struct clContext * context){
    int i = 0;
    assert(context != NULL);

    //gpuQueryPlanFinish(context);
    //deviceFinish(context);

    //pfree(context);

}
