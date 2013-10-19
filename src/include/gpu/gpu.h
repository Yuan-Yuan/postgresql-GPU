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

    int tid;          /* relation id to find the table data */

    long blockNum;      /* Number of blocks in the table */
    int colNum;         /* Number of columns in the table */
    int *colType;       /* Type of each column */
    int *colSize;       /* Size of each column */

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
    int dataType;           /* type of the const */
    int length;             /* legnth of the const */
    char *value;            /* point to the memory space where the const value is stored */
};

/*
 * gpuOpExp: an expression in the output.
 */

struct gpuOpExp{
    struct gpuExpr expr;        /* the general expression */
    int opType;                 /* the operation type */

    struct gpuOpExp *left;      /* the first operand */
    struct gpuOpExp *right;     /* the second operand */
};

struct gpuTargetEntry{
    struct gpuExpr * expr;      /* the expression tree */
    int length;                 /* length of this entry */
    int type;                   /* type of this entry */
    char *value;                /* value of the evaluted expr */
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

    int colNum;
    struct gpuTargetEntry * targetlist;     /* all the projected results */
    struct gpuPlan *leftPlan;               /* Point to the child plan or the left child plan */
    struct gpuPlan *rightPlan;              /* Point to the right child plan */
};

struct gpuScanNode{
    struct gpuPlan plan;            /* points to the query plan */
    struct gpuTable table;          /* table to be scanned */
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

    struct gpuTable * table;            /* The table data */
    struct gpuQueryDesc * querydesc;    /* Query execution plan on GPU */
    int tableNum;                       /* Number of tables */
    long totalSize;                     /* the total size of all the table data */
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

    /* Node type in the expression tree */
    GPU_OPEXP = 5000,
    GPU_CONST,
    GPU_VAR,
    GPU_AGGREF,
    GPU_TARGETENTRY
};

#endif
