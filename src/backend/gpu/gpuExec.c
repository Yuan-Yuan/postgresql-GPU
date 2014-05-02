#include <stdio.h>
#include <assert.h>
#include <time.h>

#include "postgres.h"
#include "executor/executor.h"
#include "access/htup_details.h"
#include "storage/bufmgr.h"
#include "utils/tqual.h"
#include "utils/builtins.h"
#include "gpu/gpu.h"

#include "gpu/common.h"
#include "gpu/hashJoin.h"
#include "gpu/cpuOpenclLib.h"

/*
 * countWhereAttr: given a whereExpr in plan node, count how many attributes are used
 */

static void countWhereAttr(struct gpuExpr * expr, int *attr, int* exprCount){
    int i;

    switch(expr->type){
        case GPU_AND:
        case GPU_OR:
        case GPU_NOT:
            {
                struct gpuBoolExpr * boolexpr = (struct gpuBoolExpr*)expr;
                for(i = 0;i<boolexpr->argNum;i++){
                    countWhereAttr(boolexpr->args[i], attr, exprCount);
                }
                break;
            }

        case GPU_GT:
        case GPU_GEQ:
        case GPU_EQ:
        case GPU_LEQ:
        case GPU_LT:
            {
                struct gpuOpExpr *opexpr = (struct gpuOpExpr*)expr;
                *exprCount = * exprCount + 1;
                countWhereAttr(opexpr->left, attr, exprCount);
                countWhereAttr(opexpr->right, attr,exprCount);
                break;
            }

        case GPU_VAR:
            {
                struct gpuVar * var = (struct gpuVar *)expr;
                attr[var->index] = 1;
                break;
            }

        case GPU_CONST:
            return;

        default:
            printf("GPU where expression type not supported yet:%d\n", expr->type);
            break;
    }

}

/*
 * Setup the whereExp in scanNode based on gpuExpr.
 * @index: points to the whereExp that need to be initialized.
 * @sn: the scanNode structure that controls scan execution on GPU
 * @expr: input expr from query plan tree
 */

static void setupGpuWhere(int * index, struct scanNode *sn, struct gpuExpr * expr ){
    int i;

    switch(expr->type){
        case GPU_AND:
        case GPU_OR:
        case GPU_NOT:
            {
                struct gpuBoolExpr * boolexpr = (struct gpuBoolExpr*)expr;
                for(i = 0;i<boolexpr->argNum;i++){
                    setupGpuWhere(index, sn, boolexpr->args[i]);
                    *index = *index + 1;
                }
                break;
            }

        /*
         * FIXME: we assume that the where condition is in the format var =const format.
         * More complex where expressions are not supported yet.
         */ 

        case GPU_GT:
            {
                sn->filter->exp[*index].relation = GTH;
                struct gpuOpExpr * opexpr = (struct gpuOpExpr*)expr;
                struct gpuVar * var = (struct gpuVar*)opexpr->left;
                struct gpuConst * gpuconst = (struct gpuConst*)opexpr->right;
                for(i=0;i<sn->whereAttrNum;i++){
                    if(var->index == sn->tn->attrIndex[sn->whereIndex[i]])
                        break;
                }
                sn->filter->exp[*index].index = i;
                memcpy(sn->filter->exp[*index].content, gpuconst->value, gpuconst->length);
                break;
            }
        case GPU_GEQ:
            {
                sn->filter->exp[*index].relation = GEQ;
                struct gpuOpExpr * opexpr = (struct gpuOpExpr*)expr;
                struct gpuVar * var = (struct gpuVar*)opexpr->left;
                struct gpuConst * gpuconst = (struct gpuConst*)opexpr->right;
                for(i=0;i<sn->whereAttrNum;i++){
                    if(var->index == sn->tn->attrIndex[sn->whereIndex[i]])
                        break;
                }
                sn->filter->exp[*index].index = i; 
                memcpy(sn->filter->exp[*index].content, gpuconst->value, gpuconst->length);
                break;
            }
        case GPU_EQ:
            {
                sn->filter->exp[*index].relation = EQ;
                struct gpuOpExpr * opexpr = (struct gpuOpExpr*)expr;
                struct gpuVar * var = (struct gpuVar*)opexpr->left;
                struct gpuConst * gpuconst = (struct gpuConst*)opexpr->right;
                for(i=0;i<sn->whereAttrNum;i++){
                    if(var->index == sn->tn->attrIndex[sn->whereIndex[i]])
                        break;
                }
                sn->filter->exp[*index].index = i; 
                memcpy(sn->filter->exp[*index].content, gpuconst->value, gpuconst->length);
                break;
            }
        case GPU_LEQ:
            {
                sn->filter->exp[*index].relation = LEQ;
                struct gpuOpExpr * opexpr = (struct gpuOpExpr*)expr;
                struct gpuVar * var = (struct gpuVar*)opexpr->left;
                struct gpuConst * gpuconst = (struct gpuConst*)opexpr->right;
                for(i=0;i<sn->whereAttrNum;i++){
                    if(var->index == sn->tn->attrIndex[sn->whereIndex[i]])
                        break;
                }
                sn->filter->exp[*index].index = i; 
                memcpy(sn->filter->exp[*index].content, gpuconst->value, gpuconst->length);
                break;
            }
        case GPU_LT:
            {
                sn->filter->exp[*index].relation = LTH;
                struct gpuOpExpr * opexpr = (struct gpuOpExpr*)expr;
                struct gpuVar * var = (struct gpuVar*)opexpr->left;
                struct gpuConst * gpuconst = (struct gpuConst*)opexpr->right;
                for(i=0;i<sn->whereAttrNum;i++){
                    if(var->index == sn->tn->attrIndex[sn->whereIndex[i]])
                        break;
                }
                sn->filter->exp[*index].index = i; 
                memcpy(sn->filter->exp[*index].content, gpuconst->value, gpuconst->length);
                break;
            }

        default:
            printf("GPU where expression type not supprted yet:%d\n",expr->type);
            break;
    }
}


/*
 * Execute SCAN opeartion on GPU.
 * Need to handle MVCC check and Serialization check when getting tuples from the storage.
 *
 * Return: 0 means more data need processing.
 *         1 means all data have been processed.
 */

static int gpuExecuteScan(struct gpuScanNode* node, QueryDesc * querydesc){

    int i = 0, j = 0, k = 0;
    int relid = node->tid;
    int nattr = node->table.attrNum;
    int attrLen = 0, attrIndex = 0;
    long tupleNum = 0, totalTupleNum = 0;
    int index, offset = 0, tupleSize = 0;
    struct gpuTable * table = &(node->table);
    int res = 1;
    int blockNum;

    HeapTupleData tupledata;
    ItemId lpp;
    Buffer buffer;
    Page page;
    PageHeader ph;

    struct clContext * context = querydesc->context;
    cl_int error = 0;
    void * clTmp;

    tupleSize = table->tupleSize;
    Relation r = RelationIdGetRelation(relid);

    /*
     * Calculate table size.
     * Allocate the needed memory using CL_MEM_ALLOC_HOST_PTR flag.
     */

    BlockNumber bln = node->blockNum;

    if(node->scanPos + BLOCK > bln){
        blockNum = bln - node->scanPos;
        res = 1;
    }else{
        blockNum = BLOCK;
        res = 0;
    }

    tupleNum = (blockNum * BLCKSZ + tupleSize) / tupleSize;

    /*
     * FIXME: how to determine the size of the allocated space.
     * 1. bln * BLCKSZ / tupleSize to estimate the number of tuples.
     */ 

    for(i=0;i<table->usedAttr;i++){
        index = table->attrIndex[i];
        table->cpuCol[i] = (char*)palloc(tupleNum * table->attrSize[index]);
    }


    /*
     * The relation must have been opened with lock already.
     * Here we simply get Relation for the relation id.
     * The logic here is similar to heap_fetch.
     * We first get a relation page, and for each tuple do MVCC check and serialization check.
     * If the tuple pass both checks, we will put the buffer into OpenCL managed space.
     *
     * Potential optimizations:
     *  1) do predicate evaluation before MVCC check and serialization check.
     *  2) put MVCC check and serialization check on GPU to accelerate the speed.
     *  3) optimize MVCC check and serialization check conditions.
     */


    for(i=0, totalTupleNum = 0; i<blockNum; i++){
        buffer = ReadBuffer(r,i + node->scanPos);
        LockBuffer(buffer, BUFFER_LOCK_SHARE);
        page = BufferGetPage(buffer);

        ph = (PageHeader) (page);
        tupleNum = (ph->pd_lower - (long)(&(((PageHeader)0)->pd_linp)))/sizeof(ItemIdData);

        for(k=0;k<tupleNum;k++){
                lpp = &(ph->pd_linp[k]);

                if(!ItemIdIsNormal(lpp))
                    continue;

                tupledata.t_data = (HeapTupleHeader)((char *)ph + lpp->lp_off);
                bool visible = HeapTupleSatisfiesMVCC(tupledata.t_data, querydesc->snapshot, buffer);
                CheckForSerializableConflictOut(visible, r, &tupledata, buffer, querydesc->snapshot);

                if(visible){

                    /*
                     * If the tuple passes both check, copy the actual data to OpenCL memory (from row to column).
                     * When copying data, we need first skip the HeapTupleHeader offset.
                     * For each variable length attribute, we store it in a fixed length memory when processing on gpu. 
                     * FIXME: how to handle header of the variable length data.
                     * FIXME: how to accelerate seriliaztion check.
                     */

                    offset = tupledata.t_data->t_hoff, attrIndex = 0;

                    for(j = 0; j < nattr; j ++){
                        char * tp = (char*)tupledata.t_data + offset;

                        if(attrIndex >= table->usedAttr)
                            break;

                        if(j == table->attrIndex[attrIndex]){

                            if(table->variLen[j] == 1){

                                /*
                                 * For variable length data, it may be varchar or numeric.
                                 * We check the attrType.
                                 * If it is double, it means the type is numeric.
                                 */ 

                                if(table->attrType[j] == GPU_DOUBLE){
                                    double value = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow, PointerGetDatum(tp)));
                                    memcpy(table->cpuCol[attrIndex] + totalTupleNum*table->attrSize[j] , &value, sizeof(double));

                                }else{
                                    memcpy(table->cpuCol[attrIndex] + totalTupleNum *table->attrSize[j], tp, VARSIZE_ANY(tp));
                                }

                            }else{
                                memcpy(table->cpuCol[attrIndex] + totalTupleNum * table->attrSize[j], tp, table->attrSize[j]);
                            }

                            attrIndex ++ ;
                        }

                        if(table->variLen[j] == 1){
                            offset += VARSIZE_ANY(tp);
                        }else{
                            offset += table->attrSize[j];
                        }

                    }

                    totalTupleNum ++;
                }
        }

        LockBuffer(buffer,BUFFER_LOCK_UNLOCK);
        ReleaseBuffer(buffer);
    }

    RelationClose(r);

    table->tupleNum = totalTupleNum;

    node->scanPos += blockNum;

    /*
     * Execute the Scan query on GPU.
     * Initialize the data structure and call the gpudb primitives.
     */

    struct tableNode * tnRes;
    struct tableNode * tn = (struct tableNode *)malloc(sizeof(struct tableNode));
    struct statistic pp;
    pp.total = pp.kernel = pp.pcie = 0;

    initTable(tn);
    tn->totalAttr = table->usedAttr;
    tn->attrSize = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->attrType = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->attrIndex = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->attrTotalSize = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->dataPos = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->dataFormat = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->content = (char **)palloc(sizeof(char*) * tn->totalAttr);
    tn->tupleSize = 0;

    for(i = 0; i<tn->totalAttr;i++){
        index = table->attrIndex[i];
        tn->attrSize[i] = table->attrSize[index];
        tn->attrType[i] = table->attrType[index];
        tn->attrIndex[i] = index;
        tn->attrTotalSize[i] = totalTupleNum * table->attrSize[index];

        tn->dataPos[i] = MEM;
        tn->dataFormat[i] = UNCOMPRESSED;
        tn->content[i] = (char *) table->cpuCol[i];
        tn->tupleSize += table->attrSize[index];
    }

    tn->tupleNum = totalTupleNum;
    
    struct scanNode sn;
    sn.tn = tn;
    sn.outputNum = node->plan.attrNum;
    sn.outputIndex = (int*)palloc(sizeof(int) * node->plan.attrNum);
    sn.keepInGpu = 1;

    /*
     * FIXME: currently we don't support complex where conditions.
     * We only support variable = const format.
     */

    if(node->plan.whereNum != 0){

        int attrCount = 0, exprCount = 0;
        int * attr = (int*)palloc(sizeof(int)* table->attrNum);
        memset(attr,0,sizeof(int) * table->attrNum);

        sn.hasWhere = 1;
        countWhereAttr(node->plan.whereexpr[0], attr, &exprCount);

        for(i=0;i<table->attrNum;i++){
            if(attr[i] != 0)
                attrCount ++;
        }

        sn.whereAttrNum = attrCount;

        sn.whereIndex = (int*)palloc(sizeof(int) * attrCount);

        for(i=0, index=0;i<table->attrNum;i++){
            if(attr[i] !=0){
                int j;
                for(j=0;j<tn->totalAttr;j++){
                    if(tn->attrIndex[j] == i)
                        sn.whereIndex[index++] = j;
                }
            }
        }

        sn.filter = (struct whereCondition*)palloc(sizeof(struct whereCondition));
        sn.filter->nested = 0;
        sn.filter->expNum = exprCount;
        sn.filter->exp = (struct whereExp *)malloc(sizeof(struct whereExp) * exprCount);

        index = 0;
        setupGpuWhere(&index, &sn, node->plan.whereexpr[0]);
    }

    for(i=0;i<node->plan.attrNum;i++){
        struct gpuExpr * expr = node->plan.targetlist[i];

        switch(expr->type){
            case GPU_VAR:
                {
                    struct gpuVar * var = (struct gpuVar*)expr;
                    sn.outputIndex[i] = table->indexIndirect[var->index];
                    break;
                }

            default:
                printf("GPU output expression not supported yet!\n");
                break;
        }
    }


    if(node->plan.whereNum != 0){
        tnRes = tableScan(&sn,context,&pp);

    }else{

        tnRes = (struct tableNode*)palloc(sizeof(struct tableNode));
        tnRes->tupleNum = tn->tupleNum;
        tnRes->content = (char **)palloc(sizeof(char*)*node->plan.attrNum);

        for(i=0;i<node->plan.attrNum;i++){
            index = sn.outputIndex[i];
            tnRes->content[i] = (char*) clCreateBuffer(context->context,CL_MEM_READ_WRITE, tn->attrTotalSize[index],NULL,&error);
            if(error != CL_SUCCESS){
                printf("Failed to allocate gpu memory\n");
            }
            clEnqueueWriteBuffer(context->queue,(cl_mem)tnRes->content[i],CL_TRUE,0, tn->attrTotalSize[index], tn->content[index], 0,0,0);
        }
    }

    node->plan.tupleNum = tnRes->tupleNum;

    for(i = 0; i<node->plan.attrNum;i++){
        node->plan.gpuCol[i] = tnRes->content[i];
    }

    return res;
}

/*
 * Executing the join query.
 */

static int gpuExecuteJoin(struct gpuJoinNode * node, QueryDesc * querydesc){
    int res = 1;

    int i = 0, j = 0, k = 0;
    int nattr = node->plan.attrNum;

    struct clContext * context = querydesc->context;
    cl_int error = 0;
    void * clTmp;

    struct joinNode jnode;
    struct tableNode lnode, rnode, *joinRes;
    struct statistic pp;
    pp.total = pp.kernel = pp.pcie = 0;

    /*
     * If any one of the children don't have any tuple, return immedialately.
     */

    if(node->plan.leftPlan->tupleNum == 0 || node->plan.rightPlan->tupleNum == 0){
        node->plan.tupleNum = 0;
        return res;
    }

    /*
     * Initialize two input table nodes.
     */ 

    lnode.totalAttr = node->plan.leftPlan->attrNum;
    lnode.attrType = (int*)palloc(sizeof(int)*lnode.totalAttr);
    lnode.attrSize = (int*)palloc(sizeof(int)*lnode.totalAttr);
    lnode.attrIndex = (int*)palloc(sizeof(int)*lnode.totalAttr);
    lnode.attrTotalSize = (int*)palloc(sizeof(int)*lnode.totalAttr);
    lnode.dataPos = (int*)palloc(sizeof(int)*lnode.totalAttr);
    lnode.dataFormat = (int*)palloc(sizeof(int)*lnode.totalAttr);
    lnode.content = (char**)palloc(sizeof(char*)*lnode.totalAttr);

    for(i=0;i<lnode.totalAttr;i++){
        lnode.attrType[i] = node->plan.leftPlan->attrType[i];
        lnode.attrSize[i] = node->plan.leftPlan->attrSize[i];
        lnode.attrTotalSize[i] = lnode.attrSize[i] * node->plan.leftPlan->tupleNum;
        lnode.dataPos[i] = GPU;
        lnode.dataFormat[i] = UNCOMPRESSED;
        lnode.content[i] = node->plan.leftPlan->gpuCol[i];
    }

    rnode.totalAttr = node->plan.rightPlan->attrNum;
    rnode.attrType = (int*)palloc(sizeof(int)*rnode.totalAttr);
    rnode.attrSize = (int*)palloc(sizeof(int)*rnode.totalAttr);
    rnode.attrIndex = (int*)palloc(sizeof(int)*rnode.totalAttr);
    rnode.attrTotalSize = (int*)palloc(sizeof(int)*rnode.totalAttr);
    rnode.dataPos = (int*)palloc(sizeof(int)*rnode.totalAttr);
    rnode.dataFormat = (int*)palloc(sizeof(int)*rnode.totalAttr);
    rnode.content = (char**)palloc(sizeof(char*)*rnode.totalAttr);

    for(i=0;i<rnode.totalAttr;i++){
        rnode.attrType[i] = node->plan.rightPlan->attrType[i];
        rnode.attrSize[i] = node->plan.rightPlan->attrSize[i];
        rnode.attrTotalSize[i] = rnode.attrSize[i] * node->plan.rightPlan->tupleNum;
        rnode.dataPos[i] = GPU;
        rnode.dataFormat[i] = UNCOMPRESSED;
        rnode.content[i] = node->plan.rightPlan->gpuCol[i];
    }

    /*
     * Initialize join node.
     */ 

    jnode.leftTable = &lnode;
    jnode.rightTable = &rnode;

    jnode.totalAttr = node->plan.attrNum;
    jnode.tupleSize = node->plan.tupleSize;
    jnode.keepInGpu = (int *)palloc(sizeof(int) * jnode.totalAttr);
    for(i=0;i<jnode.totalAttr;i++)
        jnode.keepInGpu[i] = 1;

    jnode.rightOutputAttrNum = node->rightAttrNum;
    jnode.leftOutputAttrNum = node->leftAttrNum;

    jnode.leftOutputAttrType = (int*)palloc(sizeof(int)*jnode.leftOutputAttrNum);
    jnode.leftOutputIndex = (int*)palloc(sizeof(int)*jnode.leftOutputAttrNum);
    jnode.leftPos = (int*)palloc(sizeof(int)*jnode.leftOutputAttrNum);
    jnode.rightOutputAttrType = (int*)palloc(sizeof(int)*jnode.rightOutputAttrNum);
    jnode.rightOutputIndex = (int*)palloc(sizeof(int)*jnode.rightOutputAttrNum);
    jnode.rightPos = (int*)palloc(sizeof(int)*jnode.rightOutputAttrNum);

    for(i=0;i<jnode.leftOutputAttrNum;i++){
        jnode.leftOutputAttrType[i] = node->plan.leftPlan->attrType[node->leftAttrIndex[i]];
        jnode.leftOutputIndex[i] = node->leftAttrIndex[i];
        jnode.leftPos[i] = node->leftPos[i];
    }

    for(i=0;i<jnode.rightOutputAttrNum;i++){
        jnode.rightOutputAttrType[i] = node->plan.rightPlan->attrType[node->rightAttrIndex[i]];
        jnode.rightOutputIndex[i] = node->rightAttrIndex[i];
        jnode.rightPos[i] = node->rightPos[i];
    }

    /*
     * FIXME: we only support foreign key join.
     */

    jnode.leftKeyIndex = node->leftJoinIndex;
    jnode.rightKeyIndex = node->rightJoinIndex;

    joinRes = hashJoin(&jnode, context, &pp);

    node->plan.tupleNum = joinRes->tupleNum;

    /*
     * Release the unused OpenCL memory
     */ 

    return res;
}

static int gpuExecuteAgg(struct gpuAggNode * aggnode, QueryDesc * querydesc){

    int res = 1;
    return res;
}

static int gpuExecuteSort(struct gpuSortNode * sortnode, QueryDesc * querydesc){

    int res = 1;
    return res;
}


/*
 * gpuExecutePlan: execute the query plan on the GPU device.
 * Return: 0 the node has more data to process
 *         1 the node has finished processing all data.
 */
static int gpuExecutePlan(struct gpuPlan *plan, QueryDesc *querydesc){

    int res = 1;

    switch(plan->type){
        case GPU_SCAN:
            {
                struct gpuScanNode * scannode = (struct gpuScanNode *)plan;
                res = gpuExecuteScan(scannode, querydesc);
                break;
            }

        case GPU_JOIN:
            {
                struct gpuJoinNode * joinnode = (struct gpuJoinNode*) plan;
                res = gpuExecuteJoin(joinnode, querydesc);
                break;
            }
        case GPU_AGG:
            {
                struct gpuAggNode * aggnode = (struct gpuAggNode *)plan;
                res = gpuExecuteAgg(aggnode, querydesc);
                break;
            }
        case GPU_SORT:
            {
                struct gpuSortNode * sortnode = (struct gpuSortNode *)plan;
                res = gpuExecuteSort(sortnode, querydesc);
                break;
            }

        default:
            printf("Query operation not supported yet\n");
            break;
    }

    return res;
}

/*
 * Fill up the execution queue.
 * The query plan tree is traversed in post order sequence.
 * @index points the next queue node that needs to be filled.
 */

static void gpuExecQueue(struct gpuPlan * plan, struct gpuPlan ** queue, int *index){

    switch(plan->type){
        case GPU_SCAN:
            {
                queue[*index] = plan;
                *index = *index + 1;
                break;
            }
        case GPU_JOIN:
            {
                gpuExecQueue(plan->leftPlan, queue, index);
                gpuExecQueue(plan->rightPlan, queue, index);
                queue[*index] = plan;
                *index = *index + 1;
                break;
            }
        case GPU_AGG:
            {
                gpuExecQueue(plan->leftPlan, queue, index);
                *index = *index + 1;
                break;
            }
        case GPU_SORT:
            {
                gpuExecQueue(plan->leftPlan, queue, index);
                *index = *index + 1;
                break;
            }

        default:
            printf("gpuExecQueue: Query operation not supported yet!\n");
            break;
    }
}


/*
 * @gpuExec: Executing query on the opencl supported device
 */

void gpuExec(QueryDesc * querydesc){

    struct timespec startTime, endTime;
    clock_gettime(CLOCK_REALTIME, &startTime);
    struct clContext * context = querydesc->context;
    struct gpuQueryDesc * gpuquerydesc = context->querydesc;
    struct gpuPlan ** execQueue = NULL;

    int queueIndex = 0, i,k, start, finish = 0 , res;

    execQueue = (struct gpuPlan **)palloc(sizeof(struct gpuPlan*)*gpuquerydesc->nodeNum);

    gpuExecQueue(gpuquerydesc->plan, execQueue, &queueIndex);

    i = 0, start = 0, k = 0;

    while(1){
        /*
         * Currently we consider aggregation and sort as blocking operators.
         */
        for(k = start;k< queueIndex;k++){
            if(execQueue[k]->type == GPU_SORT || execQueue[k]->type == GPU_AGG)
                break;
        }

        /*
         * Each operator only only processes BLOCK pages at most.
         * This process continues until all the operators finish.
         */ 


        while(1){

            finish = 1;
            for(i=start;i<k;i++){
                res = gpuExecutePlan(execQueue[i], querydesc);
                finish &= res;
            }

            if(finish == 1)
                break;

        }

        if(k>= queueIndex)
            break;

        /*
         * Executing the blocking operator.
         */ 

        gpuExecutePlan(execQueue[k], querydesc);
        start = k + 1;
    }

    clock_gettime(CLOCK_REALTIME, &endTime);
    double time = (endTime.tv_sec - startTime.tv_sec) * 1e3 + (endTime.tv_nsec - startTime.tv_nsec) * 1e-6;
    printf("GPU query execution time :%lf\n",time);

}
