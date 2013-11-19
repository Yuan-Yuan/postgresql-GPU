#ifndef POSTGRES_GPU_H
#define POSTGRES_GPU_H
#include <CL/cl.h>

/*
 * GPU table related info.
 */

struct gpuTable{

    /*
     * Table meta data will be initialized in query execution step.
     */ 

    int tid;            /* relation id to find the table data */

    long blockNum;      /* Number of blocks in the table */
    int attrNum;        /* Number of columns in the table */
    int *attrType;      /* Type of each column */
    int *attrSize;      /* Size of each column */

    cl_mem memory;      /* host pinned memory to hold all table data (row-store) */
    cl_mem gpuMemory;   /* GPU memory to hold all the table data */
};

/*
 * Information relataed to expression processing on GPU.
 */

struct gpuExpr{
    int type;               /* The operation type of the root node in the expression */
};

/*
 * gpuVar: a simple attribute from the input table.
 */
struct gpuVar{
    struct gpuExpr expr;    /* the general expression */
    int index;              /* index position in the input table */
};

/*
 * gpuConst: a const value.
 */

struct gpuConst{
    struct gpuExpr expr;    /* the general expression */
    int type;           /* type of the const */
    int length;             /* legnth of the const */
    char *value;            /* point to the memory space where the const value is stored */
};

struct gpuBoolExpr{
    struct gpuExpr expr;        /* the general expression */
    int opType;                 /* and , or */

    int argNum;                 /* Number of bool expressions */

    struct gpuExpr ** args;     /* arguments */
};

/*
 * gpuOpExp: an math expression in the projection list.
 */

struct gpuOpExpr{
    struct gpuExpr expr;        /* the general expression */
    int opType;                 /* the operation type */

    struct gpuExpr *left;      /* the first operand */
    struct gpuExpr *right;     /* the second operand */
};

/*
 * gpuAggExpr: an aggregation expression in the projection list. 
 */

struct gpuAggExpr{
    struct gpuExpr expr;        /* the general expression */

    int aggType;                /* aggregation type */
    struct gpuExpr *aggexpr;       /* aggregation expression */
};


/*
 * Simplified snapshot data on GPU.
 * We only consider satisfyMVCC on GPU. This may not be needed (we can do MVCC on CPU side).
 */
struct gpuSnapshot{
    int xmin;
    int xmax;
    int xcid;
    int *xip;
    int xcnt;
};

/*
 * The Query plan on GPU.
 * TBD: add where condition.
 */

struct gpuPlan{
    int type;                               /* the type of the current node */

    int attrNum;
    struct gpuExpr **  targetlist;          /* all the projected results */
    int whereNum;                           /* length of where expression list */
    struct gpuExpr **whereexpr;             /* for where conditions */
    struct gpuPlan *leftPlan;               /* Point to the child plan or the left child plan */
    struct gpuPlan *rightPlan;              /* Point to the right child plan */
};

struct gpuScanNode{
    struct gpuPlan plan;            /* points to the query plan */
    struct gpuTable table;          /* table to be scanned */
};

struct gpuJoinNode{
    struct gpuPlan plan;            /* points to the query plan */
    int joinNum;                    /* number of join expressions */
    struct gpuExpr **joinexpr;      /* join expression */
};

struct gpuAggNode{
    struct gpuPlan plan;            /* points to the query plan */
    int gbNum;                      /* number of group by expressions */
    int * gbIndex;                  /* the index array of group by columns */
};

struct gpuSortNode{
    struct gpuPlan plan;            /* points to the query plan */
    int sortNum;                    /* number of order by expressions */
    int * sortIndex;                /* the index array of order by columns */
    int * direction;                /* sort descending or ascending */
};

/*
 * Information need to execute query on GPU device.
 */

struct gpuQueryDesc{
    struct gpuSnapshot snapshot;            /* snapshot data for MVCC check */
    struct gpuPlan * plan;                  /* The query plan on GPU */
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
     * OpenCL Device related info.
     */ 

    cl_ulong gl_mem_size;
    cl_ulong max_alloc_size;

    /*
     * OpenCL query related parameters.
     */

    struct gpuQueryDesc * querydesc;    /* Query execution plan on GPU */
};

enum{
    ONGPU = 1123,   /* a random number */

    /* Supported data type on GPU */
    GPU_INT = 2000,
    GPU_DECIMAL,
    GPU_STRING,

    /* Node type in the query plan tree */
    GPU_SCAN = 3000,
    GPU_JOIN,
    GPU_AGG,
    GPU_SORT,

    /* Supported expression operation */
    GPU_ADD = 4000,
    GPU_MINUS,
    GPU_MULTIPLY,
    GPU_DIVIDE,
    GPU_AND,
    GPU_OR,
    GPU_NOT,
    GPU_GT,
    GPU_GEQ,
    GPU_EQ,
    GPU_LT,
    GPU_LEQ,

    /* Node type in the expression tree */
    GPU_OPEXPR = 5000,
    GPU_CONST,
    GPU_VAR,
    GPU_AGGREF,
    GPU_TARGETENTRY,

    /* supported AGG Function*/
    GPU_SUM = 7000,
    GPU_MAX,
    GPU_MIN,
    GPU_AVG,
    GPU_COUNT,

    GPU_ASC = 8000,
    GPU_DSC,

    GPU_END
};

#endif
