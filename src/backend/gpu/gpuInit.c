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
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#include "mb/pg_wchar.h"
#include "gpu/gpu.h"


static struct gpuExpr* gpuExprInit(Expr *, int *);
static struct gpuExpr** gpuWhere(Expr *, int *);
static struct gpuPlan * gpuInitPlan(PlanState *, int *);

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
    error = clGetDeviceIDs(pid[1], CL_DEVICE_TYPE_CPU, 1,&device, NULL);
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

        case FLOAT4OID:
            gputype = GPU_FLOAT;
            break;

        case FLOAT8OID:
            gputype = GPU_DOUBLE;
            break;

        case NUMERICOID:
            gputype = GPU_FLOAT;
            break;

        case TEXTOID:
        case BPCHAROID:
        case VARCHAROID:
            gputype = GPU_STRING;
            break;

        case TIMESTAMPOID:
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

static void gpuInitOpExpr(struct gpuOpExpr * gpuopexpr, OpExpr *opexpr, int * attrArray){

    char *opname = get_opname(opexpr->opno);
    ListCell *l;

    /* Currently we only support binary operators */

    assert(list_length(opexpr->args)== 2);

    if(strcmp(opname, "=") == 0){
        gpuopexpr->expr.type = GPU_GT;
    }else if (strcmp(opname,">=")){
        gpuopexpr->expr.type = GPU_GEQ;
    }else if (strcmp(opname,">")){
        gpuopexpr->expr.type = GPU_EQ;
    }else if (strcmp(opname,"<=")){
        gpuopexpr->expr.type = GPU_LEQ;
    }else if (strcmp(opname,"<")){
        gpuopexpr->expr.type = GPU_LT;
    }else if (strcmp(opname,"+")){
        gpuopexpr->expr.type = GPU_ADD;
    }else if (strcmp(opname,"-")){
        gpuopexpr->expr.type = GPU_MINUS;
    }else if (strcmp(opname,"*")){
        gpuopexpr->expr.type = GPU_MULTIPLY;
    }else if (strcmp(opname,"/")){
        gpuopexpr->expr.type = GPU_DIVIDE;
    }

    l = list_head(opexpr->args);
    gpuopexpr->left = gpuExprInit((Expr*)lfirst(l),attrArray);

    l = l->next;
    gpuopexpr->right = gpuExprInit((Expr*)lfirst(l),attrArray);

    printf("Expression type T_OpExpr\n");

}

/*
 * Initialize gpuAggExpr.
 */

static void gpuInitAggExpr(struct gpuAggExpr * gpuaggexpr, Aggref * aggref, int*attrArray){

    char * aggfunc = get_func_name(aggref->aggfnoid);
    ListCell *l;

    if(strcmp(aggfunc,"sum") == 0){
        gpuaggexpr->aggType = GPU_SUM;
    }else if (strcmp(aggfunc, "min") == 0){
        gpuaggexpr->aggType = GPU_MIN;
    }else if (strcmp(aggfunc, "max") == 0){
        gpuaggexpr->aggType = GPU_MAX;
    }else if (strcmp(aggfunc, "count") == 0){
        gpuaggexpr->aggType = GPU_COUNT;
    }else if (strcmp(aggfunc, "avg") == 0){
        gpuaggexpr->aggType = GPU_AVG;
    }

    /* FIXME: currently we don't support complex group by functions */

    assert(list_length(aggref->args) == 1);

    l = list_head(aggref->args);
    TargetEntry * te = (TargetEntry *) lfirst(l);
    gpuaggexpr->aggexpr = gpuExprInit(te->expr, attrArray);

};

/*
 * gpuExprInit: initialize an expression which will be evaluted on GPU.
 */

static struct gpuExpr* gpuExprInit(Expr *cpuExpr, int *attrArray){

    struct gpuExpr * gpuexpr = NULL;

    switch(nodeTag(cpuExpr)){
        case T_Var:
            {
                Var * var = (Var *)cpuExpr;

                struct gpuVar * gpuvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));
                gpuInitVarExpr(gpuvar, var);

                if(attrArray)
                    attrArray[gpuvar->index] = 1;

                if(var->varno == OUTER_VAR){
                    gpuvar->index = -gpuvar->index; 
                }

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

                gpuInitOpExpr(gpuopexpr,opexpr, attrArray);

                gpuexpr = (struct gpuExpr *)gpuopexpr;
                break;
            }

        case T_Aggref:
            {
                Aggref * aggref = (Aggref *)cpuExpr;
                struct gpuAggExpr * gpuaggexpr = (struct gpuAggExpr*)palloc(sizeof(struct gpuAggExpr));

                gpuInitAggExpr(gpuaggexpr, aggref,attrArray);
                gpuexpr = (struct gpuExpr *)gpuaggexpr;
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
 * The input must be Expr, not ExprState
 */

static struct gpuExpr ** gpuWhere(Expr * expr, int * attrArray){

    struct gpuExpr ** gpuexpr = (struct gpuExpr **)palloc(sizeof(struct gpuExpr *));;
    ListCell *l;
    int i = 0;

    switch(nodeTag(expr)){
        case T_List:
            {
                List * qual = (List*)expr;
                struct gpuBoolExpr * gpuboolexpr = (struct gpuBoolExpr*)palloc(sizeof(struct gpuBoolExpr));

                gpuboolexpr->expr.type = GPU_AND;
                gpuboolexpr->argNum = list_length(qual);
                gpuboolexpr->args = (struct gpuExpr**)palloc(gpuboolexpr->argNum * sizeof(struct gpuExpr*));

                foreach(l,qual){
                    gpuboolexpr->args[i] = *(gpuWhere((Expr*)lfirst(l), attrArray));
                    i+=1;
                }
                *gpuexpr = (struct gpuExpr *)gpuboolexpr;
                break;
            }

        case T_BoolExpr:
            {
                struct gpuBoolExpr *gpuboolexpr = (struct gpuBoolExpr *)palloc(sizeof(struct gpuBoolExpr));
                BoolExpr   *boolexpr = (BoolExpr *) expr;

                switch(boolexpr->boolop){
                    case AND_EXPR:
                        {
                            gpuboolexpr->expr.type = GPU_AND;
                            break;
                        }
                    case OR_EXPR:
                        {
                            gpuboolexpr->expr.type = GPU_OR;
                            break;
                        }
                    case NOT_EXPR:
                        {
                            gpuboolexpr->expr.type = GPU_NOT;
                            break;
                        }
                    default:
                        printf("Where expression type not supported yet!\n");
                        break;
                }

                gpuboolexpr->argNum = list_length(boolexpr->args);
                gpuboolexpr->args = (struct gpuExpr **)palloc(gpuboolexpr->argNum * sizeof(struct gpuExpr*));

                foreach(l,boolexpr->args){
                    gpuboolexpr->args[i] = *gpuWhere((Expr*)lfirst(l),attrArray);
                }

                *gpuexpr = (struct gpuExpr*)gpuboolexpr;
                break;
            }

        case T_OpExpr:
            {
                struct gpuOpExpr *gpuopexpr = (struct gpuOpExpr *)palloc(sizeof(struct gpuOpExpr));
                OpExpr   *opexpr = (OpExpr *) expr;

                gpuInitOpExpr(gpuopexpr, opexpr, attrArray);

                *gpuexpr = (struct gpuExpr *)gpuopexpr;
                break;
            }

        case T_Var:
            {
                struct gpuVar *gpuvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));
                Var * var = (Var*) expr;

                gpuInitVarExpr(gpuvar, var);

                if(var->varno == OUTER_VAR)
                    gpuvar->index = - gpuvar->index;

                *gpuexpr = (struct gpuExpr *)gpuvar;
                break;
            }

        case T_Const:
            {
                struct gpuConst *gpuconst = (struct gpuConst*)palloc(sizeof(struct gpuConst));
                Const * cpuconst = (Const *)expr;

                gpuInitConstExpr(gpuconst, cpuconst);

                *gpuexpr = (struct gpuExpr *)gpuconst;
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
    int i, k, *attrArray, usedAttr;
    int typid, typmod, attrlen;
    int tupleSize = 0;

    scannode = (struct gpuScanNode*)palloc(sizeof(struct gpuScanNode));
    scannode->scanPos = 0;
    scannode->plan.type = GPU_SCAN;
    scannode->plan.leftPlan = NULL;
    scannode->plan.rightPlan = NULL;

    /*
     * Initialize the scan table info.
     */ 

    scannode->plan.table.tid = RelationGetRelid(scanstate->ss_currentRelation);
    scannode->plan.table.attrNum = get_relnatts(scannode->plan.table.tid);
    scannode->plan.table.attrType = (int*)palloc(sizeof(int)*scannode->plan.table.attrNum);
    scannode->plan.table.attrSize = (int*)palloc(sizeof(int)*scannode->plan.table.attrNum);
    scannode->plan.table.variLen = (int*)palloc(sizeof(int)*scannode->plan.table.attrNum);
    scannode->plan.table.indexIndirect = (int*)palloc(sizeof(int)*scannode->plan.table.attrNum);
    attrArray = (int *)palloc(sizeof(int)*scannode->plan.table.attrNum);

    /* The postgresql attrNum starts from 1 */

    for(i=1;i<=scannode->plan.table.attrNum;i++){
        typid = get_atttype(scannode->plan.table.tid, i);
        typmod = get_atttypmod(scannode->plan.table.tid,i);

        switch(typid){

            case BPCHAROID:
                attrlen = type_maximum_size(typid,typmod) - VARHDRSZ;
                attrlen /= pg_encoding_max_length(GetDatabaseEncoding());
                scannode->plan.table.variLen[i-1] = 0;
                break;

            case VARCHAROID:
                attrlen = type_maximum_size(typid,typmod) - VARHDRSZ;
                attrlen /= pg_encoding_max_length(GetDatabaseEncoding());
                scannode->plan.table.variLen[i-1] = 1;
                break;

            case NUMERICOID:
                attrlen = sizeof(float); 
                scannode->plan.table.variLen[i-1] = 1;
                break;

            case TIMESTAMPOID:
            case INT2OID:
            case INT4OID:
            case INT8OID:
            case FLOAT4OID:
            case FLOAT8OID:
                attrlen = get_typlen(typid);
                scannode->plan.table.variLen[i-1] = 0;
                break;

            default:
                printf("Typid:%d not supported yet!\n",typid);
                break;
     
        }

        tupleSize += attrlen;
        scannode->plan.table.attrSize[i-1] = attrlen; 
        scannode->plan.table.attrType[i-1] = pgtypeToGPUType(typid);
        attrArray[i-1] = -1;
        scannode->plan.table.indexIndirect[i-1] = -1;
    }

    scannode->plan.table.tupleSize = tupleSize;

    assert(pi != NULL);

    /* 
     * Initialize scannode target list.
     * The length of the projected list is caculated as:
     *      number of simple attributes + number of expressions.
     * resno attribute in each target entry indicates the position in the projected list.
     */

    i = 0;
    foreach(l,pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var)
            continue;
        i ++ ;
    }

    scannode->plan.attrNum = i + pi->pi_numSimpleVars; 

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

        targetlist[te->resno - 1] = gpuExprInit(te->expr,attrArray);

    }

    for(i = 0;i<pi->pi_numSimpleVars;i++){

        /* 
         * pi_varOutputCols : position in the outputlist, start from 1
         * pi_varNumbers: input column index, start from 1 
         */
        int offset = pi->pi_varOutputCols[i] - 1;
        struct gpuVar *gvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));

        gvar->index = pi->pi_varNumbers[i] - 1;
        gvar->expr.type = GPU_VAR;

        targetlist[offset] = (struct gpuExpr *) gvar;
        attrArray[gvar->index] = 1;
    }

    scannode->plan.targetlist = targetlist;

    if(qual){
        scannode->plan.whereNum = list_length(qual);
        scannode->plan.whereexpr = gpuWhere((Expr*)qual, attrArray);

    }else{
        scannode->plan.whereNum = 0;
        scannode->plan.whereexpr = NULL;
    }

    for(i=0, usedAttr = 0;i<scannode->plan.table.attrNum;i++){
        if(attrArray[i]!= -1)
            usedAttr ++;
    }

    scannode->plan.table.usedAttr = usedAttr;
    scannode->plan.table.attrIndex = (int *)palloc(sizeof(int)*usedAttr);
    for(i=0, k = 0; i < scannode->plan.table.attrNum; i++){
        if(attrArray[i] != -1){
            scannode->plan.table.indexIndirect[i] = k;
            scannode->plan.table.attrIndex[k++] = i; 
        }
    }

    scannode->plan.table.cpuCol = (char*)palloc(sizeof(char*)*usedAttr);
    scannode->plan.table.gpuCol = (cl_mem*)palloc(sizeof(cl_mem)*usedAttr);

    return scannode;
}

/*
 * gpuInitJoin Initialize GPU join node.
 * Currently we only support simple inner join.
 */

static struct gpuJoinNode* gpuInitJoin(PlanState *planstate, int *nodeNum){

    struct gpuJoinNode *joinnode = NULL;
    JoinState * joinstate = (JoinState *) planstate;

    ProjectionInfo *pi = joinstate->ps.ps_ProjInfo;
    struct gpuExpr **targetlist = NULL;

    List * wherequal = joinstate->ps.plan->qual;
    List * joinqual = joinstate->joinqual;
    ListCell *l;

    int i,k;
    int leftAttrNum, rightAttrNum;

    *nodeNum = *nodeNum + 1;
    joinnode = (struct gpuJoinNode*)palloc(sizeof(struct gpuJoinNode));
    joinnode->plan.type = GPU_JOIN;
    joinnode->plan.leftPlan = (struct gpuPlan *) gpuInitPlan(planstate->lefttree, nodeNum);
    joinnode->plan.rightPlan = (struct gpuPlan *) gpuInitPlan(planstate->righttree, nodeNum);

    leftAttrNum = joinnode->plan.leftPlan->attrNum;
    rightAttrNum = joinnode->plan.rightPlan->attrNum;

    /*
     * Count the number of projected results,
     */

    k = 0;
    foreach(l,pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var)
            continue;
        k++;
    }

    for(i=0;i<pi->pi_numSimpleVars;i++){

        /*
         * Decide left or right or junk 
         * Using varSlotsOffsets to decide whether an attribute is from left child or right child.
         * If the index is larger than the largest index, then it is a junk entry.
         *
         * innertuple is left child and outer tuple is right child.
         */

        if(pi->pi_varSlotOffsets[i] == offsetof(ExprContext,ecxt_innertuple)){

            /*
             * This var comes from the left child.
             * varNumbers start from 1, not 0.
             */

            if(pi->pi_varNumbers[i] <= leftAttrNum)
                k++;

        }else{

            /*
             * This var coms from the right child.
             * varNumbers start from 1, not 0.
             */ 

            if(pi->pi_varNumbers[i] <= rightAttrNum)
                k++;
        }
    }

    joinnode->plan.attrNum = k;

    /*
     * Initialize the target list.
     */

    targetlist = (struct gpuExpr **) palloc(sizeof(struct gpuExpr *) * joinnode->plan.attrNum);

    foreach(l, pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var){

        /*
         * This means this is a junk entry.
         */
            continue;
        }

        targetlist[te->resno - 1] = gpuExprInit(te->expr, NULL);
    }

    for(i = 0;i<pi->pi_numSimpleVars;i++){
        int offset = pi->pi_varOutputCols[i] - 1;

        if(pi->pi_varSlotOffsets[i] == offsetof(ExprContext,ecxt_innertuple)){

            if(pi->pi_varNumbers[i] > leftAttrNum){
                continue;
            }

            struct gpuVar *gvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));
            gvar->expr.type = GPU_VAR;
            gvar->index = pi->pi_varNumbers[i] - 1;
            targetlist[offset] = (struct gpuExpr *) gvar;

        }else{

            if(pi->pi_varNumbers[i] <= rightAttrNum){
                continue;
            }

            struct gpuVar *gvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));
            gvar->expr.type = GPU_VAR;
            gvar->index = pi->pi_varNumbers[i] - 1;
            gvar->index = - gvar->index;
            targetlist[offset] = (struct gpuExpr *) gvar;

        }

    }

    joinnode->plan.targetlist = targetlist;

    if(wherequal){
        joinnode->plan.whereNum = list_length(wherequal);
        joinnode->plan.whereexpr = gpuWhere((Expr*)wherequal, NULL);
    }else{
        joinnode->plan.whereNum = 0;
        joinnode->plan.whereexpr = NULL;
    }

    if(joinqual){

        assert(list_length(joinqual) == 1);

        joinnode->joinNum = list_length(joinqual);
        foreach(l, joinqual){
            
            ExprState * es = (ExprState*)lfirst(l);
            joinnode->joinexpr = gpuWhere(es->expr, NULL);
        }

    }else{
        joinnode->joinNum = 0;
        joinnode->joinexpr = NULL;
    }

    return joinnode;

}

static struct gpuAggNode * gpuInitAgg(struct PlanState * planstate, int * nodeNum){

    struct gpuAggNode * aggnode = NULL;
    struct AggState * aggstate = (struct AggState*)planstate;
    List * qual = aggstate->ss.ps.plan->qual;
    ProjectionInfo *pi = aggstate->ss.ps.ps_ProjInfo;
    struct gpuExpr **targetlist = NULL;
    Agg * node = (Agg*) aggstate->ss.ps.plan;
    ListCell *l;
    int i;

    *nodeNum = *nodeNum + 1;
    aggnode = (struct gpuAggNode*)palloc(sizeof(struct gpuAggNode));
    aggnode->plan.type = GPU_AGG;
    aggnode->plan.leftPlan = (struct gpuPlan *) gpuInitPlan(planstate->lefttree, nodeNum);
    aggnode->plan.rightPlan = NULL;

    /* 
     * Initialize aggnode target list.
     * The length of the projected list is caculated as:
     *      number of simple attributes + number of expressions.
     * resno attribute in each target entry indicates the position in the projected list.
     */

    i = 0;
    foreach(l,pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var)
            continue;
        i ++ ;
    }

    aggnode->plan.attrNum = i + pi->pi_numSimpleVars;

    targetlist = (struct gpuExpr **) palloc(sizeof(struct gpuExpr *) * aggnode->plan.attrNum);

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

        targetlist[te->resno - 1] = gpuExprInit(te->expr, NULL);

    }

    for(i = 0;i<pi->pi_numSimpleVars;i++){
        int offset = pi->pi_varOutputCols[i];
        struct gpuVar *gvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));

        gvar->expr.type = GPU_VAR;
        gvar->index = pi->pi_varNumbers[i];
        targetlist[offset] = (struct gpuExpr *) gvar;
    }

    aggnode->plan.targetlist = targetlist;

    /*
     * Initialize where expression for aggregation
     */ 

    if(qual){
        aggnode->plan.whereNum = list_length(qual);
        aggnode->plan.whereexpr = gpuWhere((Expr*)qual, NULL);
    }else{
        aggnode->plan.whereNum = 0;
        aggnode->plan.whereexpr = NULL;
    }

    /*
     * Initialize group by information.
     */ 

    aggnode->gbNum = node->numCols;

    aggnode->gbIndex = (int *)palloc(sizeof(int) * node->numCols);

    for(i=0;i<node->numCols;i++){
        aggnode->gbIndex[i] = node->grpColIdx[i];
    }

    return aggnode;

}

static struct gpuSortNode * gpuInitSort(struct PlanState * planstate, int * nodeNum){

    struct gpuSortNode * sortnode = NULL;
    struct SortState * sortstate = (struct SortState*)planstate;
    List * qual = sortstate->ss.ps.plan->qual;
    ProjectionInfo *pi = sortstate->ss.ps.ps_ProjInfo;
    struct gpuExpr **targetlist = NULL;
    Sort * node = (Sort*) sortstate->ss.ps.plan;
    ListCell *l;
    int i;

    *nodeNum = *nodeNum + 1;
    sortnode = (struct gpuSortNode*)palloc(sizeof(struct gpuSortNode));
    sortnode->plan.type = GPU_SORT;
    sortnode->plan.leftPlan = (struct gpuPlan *) gpuInitPlan(planstate->lefttree, nodeNum);
    sortnode->plan.rightPlan = NULL;

    /* 
     * Initialize aggnode target list.
     * The length of the projected list is caculated as:
     *      number of simple attributes + number of expressions.
     * resno attribute in each target entry indicates the position in the projected list.
     */

    i = 0;
    foreach(l,pi->pi_targetlist){
        GenericExprState * gestate = (GenericExprState *) lfirst(l);
        TargetEntry * te = (TargetEntry *) gestate->xprstate.expr;
        if(nodeTag(te->expr) == T_Var)
            continue;
        i ++ ;
    }

    sortnode->plan.attrNum = i + pi->pi_numSimpleVars; 

    targetlist = (struct gpuExpr **) palloc(sizeof(struct gpuExpr *) * sortnode->plan.attrNum);

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

        targetlist[te->resno - 1] = gpuExprInit(te->expr, NULL);

    }

    for(i = 0;i<pi->pi_numSimpleVars;i++){
        int offset = pi->pi_varOutputCols[i];
        struct gpuVar *gvar = (struct gpuVar*)palloc(sizeof(struct gpuVar));

        gvar->expr.type = GPU_VAR;
        gvar->index = pi->pi_varNumbers[i];
        targetlist[offset] = (struct gpuExpr *) gvar;
    }

    sortnode->plan.targetlist = targetlist;

    /*
     * Initialize where expression for aggregation
     */ 

    if(qual){
        sortnode->plan.whereNum = list_length(qual);
        sortnode->plan.whereexpr = gpuWhere((Expr*)qual, NULL);
    }else{
        sortnode->plan.whereNum = 0;
        sortnode->plan.whereexpr = NULL;
    }

    /*
     * Initialize order by information.
     */ 

    sortnode->sortNum = node->numCols;

    sortnode->sortIndex = (int *)palloc(sizeof(int) * node->numCols);

    for(i=0;i<node->numCols;i++){
        sortnode->sortIndex[i] = node->sortColIdx[i];
    }

    return sortnode;

} 

/*
 * Free the scan node.
 */

static void gpuFreeScan(struct gpuScanNode * node){

    /* FIXME: release of each expression */
    //pfree(node->plan.targetlist);

    /* FIXME: release of other table meta data */

    //clReleaseMemObject(node->plan.table.memory);
    //clReleaseMemObject(node->plan.table.gpuMemory);

    //pfree(node);
}

static void gpuFreeJoin(struct gpuJoinNode *node){
    return;
}

static void gpuFreeAgg(struct gpuAggNode * node){
    return;
}

static void gpuFreeSort(struct gpuSortNode * sort){
    return;
}

/*
 * gpuInitPlan: initialize gpu query plan.
 */

static struct gpuPlan * gpuInitPlan(PlanState *planstate, int * nodeNum){

    struct gpuPlan * result = NULL;

    switch(nodeTag(planstate)){

        case T_IndexScanState:
        case T_IndexOnlyScanState:
        case T_BitmapHeapScanState:
        case T_TidScanState:
        case T_SubqueryScanState:
        case T_FunctionScanState:
        case T_ValuesScanState:
        case T_CteScanState:
        case T_WorkTableScanState:
        case T_ForeignScanState:
            {
                printf("Only Sequential scan plan is supported when executing on GPUs:%d\n",nodeTag(planstate));
                break;
            }
        case T_SeqScanState:
            {
                *nodeNum = *nodeNum + 1;
                result = (struct gpuPlan*) gpuInitScan(planstate);
                break;
            }

        case T_MergeJoinState:
        case T_HashJoinState:
            {
                printf("Only nested loop join plan is supported when executing on GPUs:%d\n",nodeTag(planstate));
                break;
            }
        case T_NestLoopState:
            {
                result = (struct gpuPlan*) gpuInitJoin(planstate, nodeNum);
                break;
            }

        /*
         * Aggregation node.
         */ 

        case T_AggState:
            {
                result = (struct gpuPlan*) gpuInitAgg(planstate, nodeNum);
                break;
            }

        /*
         * Sort node.
         */ 
        case T_SortState:
            {
                result = (struct gpuPlan*) gpuInitSort(planstate, nodeNum);
                break;
            }

        /*
         * The rest states: we simply ignore them
         */ 

        case T_MaterialState:
        case T_GroupState:
        case T_UniqueState:
        case T_LimitState:
        case T_HashState:
        case T_SetOpState:
            {
                result = (struct gpuPlan *)gpuInitPlan(outerPlanState(planstate), nodeNum);
                break;
            }

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

        case GPU_JOIN:
            {
                gpuFreeJoin((struct gpuJoinNode*)plan);
                break;
            }
        case GPU_AGG:
            {
                gpuFreeAgg((struct gpuAggNode*)plan);
                break;
            }
        case GPU_SORT:
            {
                gpuFreeSort((struct gpuSortNode*)plan);
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
    int nodeNum = 0;

    assert(nodeTag(querydesc->planstate) == T_LockRowsState);

    gpuQuery = (struct gpuQueryDesc *)palloc(sizeof(struct gpuQueryDesc));

    /*
     * First step: copy snapshot related data from CPU query description.
     */
    gpuInitSnapshot(&(gpuQuery->snapshot), querydesc->snapshot);

    /*
     * Second step: initialize the query plan.
     */ 

    gpuQuery->plan = gpuInitPlan(outerPlan, &nodeNum);
    gpuQuery->nodeNum = nodeNum;

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
