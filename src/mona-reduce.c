/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include "mona-coll.h"
#include "mona-comm.h"
#include <string.h>

// -----------------------------------------------------------------------
// Reduce
// -----------------------------------------------------------------------

na_return_t mona_comm_reduce(mona_comm_t comm,
                             const void* sendbuf,
                             void*       recvbuf,
                             na_size_t   typesize,
                             na_size_t   count,
                             mona_op_t   op,
                             void*       uargs,
                             int         root,
                             na_tag_t    tag)
{
    // TODO the bellow algorithm is a binomial algorithm.
    // We should try implementing the reduce_scatter_gather algorithm,
    // for large data sizes, and also enable n-ary trees instead of
    // binomial.
    na_return_t na_ret = NA_SUCCESS;
    int         comm_size, rank, lroot, relrank;
    int         mask, source;
    // the temp is to store the intermediate results recieved from the source
    void* tempSrc         = NULL;
    int   mallocRcvbuffer = 0;

    if (count == 0) return na_ret;

    comm_size = comm->size;
    rank      = comm->rank;

    // only support commutative operation for the current implementation

    tempSrc = (void*)malloc(typesize * count);

    // If I'm not the root, then my recvbuf may not be valid, therefore
    // I have to allocate a temporary one
    if (rank != root && recvbuf == NULL) {
        mallocRcvbuffer = 1;
        recvbuf         = (void*)malloc(typesize * count);
    }
    // recv buffer should be reinnitilized by sendBuffer if it is not the
    // MONA_IN_PLACE for all ranks this aims to avoid the init value of the
    // recvbuffer to influence the results
    if ((rank != root) || sendbuf != MONA_IN_PLACE) {
        memcpy(recvbuf, sendbuf, typesize * count);
    }

    mask  = 0x1;
    lroot = root;
    // adjusted rank, the relrank for the root is 0
    relrank = (rank - lroot + comm_size) % comm_size;

    while (mask < comm_size) {
        // receive
        if ((mask & relrank) == 0) {
            source = (relrank | mask);
            if (source < comm_size) {
                source = (source + lroot) % comm_size;

                na_ret = mona_comm_recv(comm, tempSrc, typesize * count, source,
                                        tag, NULL);
                if (na_ret != NA_SUCCESS) { goto finish; }
                // for the first iteration, the recv buffer have already stored
                // the value from the send buffer
                op(tempSrc, recvbuf, typesize, count, uargs);
            }
        } else {
            /* I've received all that I'm going to.  Send my result to
             * my parent */
            source = ((relrank & (~mask)) + lroot) % comm_size;

            na_ret
                = mona_comm_send(comm, recvbuf, typesize * count, source, tag);

            if (na_ret != NA_SUCCESS) { goto finish; }
            break;
        }
        mask <<= 1;
    }

finish:
    if (tempSrc != NULL) { free(tempSrc); }
    if (mallocRcvbuffer) { free(recvbuf); }
    return na_ret;
}

typedef struct ireduce_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    void*          recvbuf;
    na_size_t      typesize;
    na_size_t      count;
    mona_op_t      op;
    void*          uargs;
    int            root;
    na_tag_t       tag;
    mona_request_t req;
} ireduce_args;

static void ireduce_thread(void* x)
{
    ireduce_args* args   = (ireduce_args*)x;
    na_return_t   na_ret = mona_comm_reduce(
        args->comm, args->sendbuf, args->recvbuf, args->typesize, args->count,
        args->op, args->uargs, args->root, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_ireduce(mona_comm_t     comm,
                              const void*     sendbuf,
                              void*           recvbuf,
                              na_size_t       typesize,
                              na_size_t       count,
                              mona_op_t       op,
                              void*           uargs,
                              int             root,
                              na_tag_t        tag,
                              mona_request_t* req)
{
    NB_OP_INIT(ireduce_args);
    args->comm     = comm;
    args->sendbuf  = sendbuf;
    args->recvbuf  = recvbuf;
    args->typesize = typesize;
    args->count    = count;
    args->op       = op;
    args->uargs    = uargs;
    args->root     = root;
    args->tag      = tag;
    NB_OP_POST(ireduce_thread);
}

#define DEFINE_MAX_OPERATOR(__name__, __type__)                       \
    void __name__(const void* in, void* inout, na_size_t typesize,    \
                  na_size_t count, void* uargs)                       \
    {                                                                 \
        (void)uargs;                                                  \
        const __type__* in_t    = (const __type__*)in;                \
        __type__*       inout_t = (__type__*)inout;                   \
        na_size_t       i;                                            \
        for (i = 0; i < count; i++) {                                 \
            inout_t[i] = inout_t[i] < in_t[i] ? in_t[i] : inout_t[i]; \
        }                                                             \
    }

DEFINE_MAX_OPERATOR(mona_op_max_u64, uint64_t)
DEFINE_MAX_OPERATOR(mona_op_max_u32, uint32_t)
DEFINE_MAX_OPERATOR(mona_op_max_u16, uint16_t)
DEFINE_MAX_OPERATOR(mona_op_max_u8, uint8_t)
DEFINE_MAX_OPERATOR(mona_op_max_i64, int64_t)
DEFINE_MAX_OPERATOR(mona_op_max_i32, int32_t)
DEFINE_MAX_OPERATOR(mona_op_max_i16, int16_t)
DEFINE_MAX_OPERATOR(mona_op_max_i8, int8_t)
DEFINE_MAX_OPERATOR(mona_op_max_f32, float)
DEFINE_MAX_OPERATOR(mona_op_max_f64, double)

#define DEFINE_MIN_OPERATOR(__name__, __type__)                       \
    void __name__(const void* in, void* inout, na_size_t typesize,    \
                  na_size_t count, void* uargs)                       \
    {                                                                 \
        (void)uargs;                                                  \
        const __type__* in_t    = (const __type__*)in;                \
        __type__*       inout_t = (__type__*)inout;                   \
        na_size_t       i;                                            \
        for (i = 0; i < count; i++) {                                 \
            inout_t[i] = inout_t[i] > in_t[i] ? in_t[i] : inout_t[i]; \
        }                                                             \
    }

DEFINE_MIN_OPERATOR(mona_op_min_u64, uint64_t)
DEFINE_MIN_OPERATOR(mona_op_min_u32, uint32_t)
DEFINE_MIN_OPERATOR(mona_op_min_u16, uint16_t)
DEFINE_MIN_OPERATOR(mona_op_min_u8, uint8_t)
DEFINE_MIN_OPERATOR(mona_op_min_i64, int64_t)
DEFINE_MIN_OPERATOR(mona_op_min_i32, int32_t)
DEFINE_MIN_OPERATOR(mona_op_min_i16, int16_t)
DEFINE_MIN_OPERATOR(mona_op_min_i8, int8_t)
DEFINE_MIN_OPERATOR(mona_op_min_f32, float)
DEFINE_MIN_OPERATOR(mona_op_min_f64, double)

#define DEFINE_SUM_OPERATOR(__name__, __type__)                            \
    void __name__(const void* in, void* inout, na_size_t typesize,         \
                  na_size_t count, void* uargs)                            \
    {                                                                      \
        (void)uargs;                                                       \
        const __type__* in_t    = (const __type__*)in;                     \
        __type__*       inout_t = (__type__*)inout;                        \
        na_size_t       i;                                                 \
        for (i = 0; i < count; i++) { inout_t[i] = inout_t[i] + in_t[i]; } \
    }

DEFINE_SUM_OPERATOR(mona_op_sum_u64, uint64_t)
DEFINE_SUM_OPERATOR(mona_op_sum_u32, uint32_t)
DEFINE_SUM_OPERATOR(mona_op_sum_u16, uint16_t)
DEFINE_SUM_OPERATOR(mona_op_sum_u8, uint8_t)
DEFINE_SUM_OPERATOR(mona_op_sum_i64, int64_t)
DEFINE_SUM_OPERATOR(mona_op_sum_i32, int32_t)
DEFINE_SUM_OPERATOR(mona_op_sum_i16, int16_t)
DEFINE_SUM_OPERATOR(mona_op_sum_i8, int8_t)
DEFINE_SUM_OPERATOR(mona_op_sum_f32, float)
DEFINE_SUM_OPERATOR(mona_op_sum_f64, double)

#define DEFINE_PROD_OPERATOR(__name__, __type__)                           \
    void __name__(const void* in, void* inout, na_size_t typesize,         \
                  na_size_t count, void* uargs)                            \
    {                                                                      \
        (void)uargs;                                                       \
        const __type__* in_t    = (const __type__*)in;                     \
        __type__*       inout_t = (__type__*)inout;                        \
        na_size_t       i;                                                 \
        for (i = 0; i < count; i++) { inout_t[i] = inout_t[i] * in_t[i]; } \
    }

DEFINE_PROD_OPERATOR(mona_op_prod_u64, uint64_t)
DEFINE_PROD_OPERATOR(mona_op_prod_u32, uint32_t)
DEFINE_PROD_OPERATOR(mona_op_prod_u16, uint16_t)
DEFINE_PROD_OPERATOR(mona_op_prod_u8, uint8_t)
DEFINE_PROD_OPERATOR(mona_op_prod_i64, int64_t)
DEFINE_PROD_OPERATOR(mona_op_prod_i32, int32_t)
DEFINE_PROD_OPERATOR(mona_op_prod_i16, int16_t)
DEFINE_PROD_OPERATOR(mona_op_prod_i8, int8_t)
DEFINE_PROD_OPERATOR(mona_op_prod_f32, float)
DEFINE_PROD_OPERATOR(mona_op_prod_f64, double)

#define DEFINE_LAND_OPERATOR(__name__, __type__)                            \
    void __name__(const void* in, void* inout, na_size_t typesize,          \
                  na_size_t count, void* uargs)                             \
    {                                                                       \
        (void)uargs;                                                        \
        const __type__* in_t    = (const __type__*)in;                      \
        __type__*       inout_t = (__type__*)inout;                         \
        na_size_t       i;                                                  \
        for (i = 0; i < count; i++) { inout_t[i] = inout_t[i] && in_t[i]; } \
    }

DEFINE_LAND_OPERATOR(mona_op_land_u64, uint64_t)
DEFINE_LAND_OPERATOR(mona_op_land_u32, uint32_t)
DEFINE_LAND_OPERATOR(mona_op_land_u16, uint16_t)
DEFINE_LAND_OPERATOR(mona_op_land_u8, uint8_t)
DEFINE_LAND_OPERATOR(mona_op_land_i64, int64_t)
DEFINE_LAND_OPERATOR(mona_op_land_i32, int32_t)
DEFINE_LAND_OPERATOR(mona_op_land_i16, int16_t)
DEFINE_LAND_OPERATOR(mona_op_land_i8, int8_t)
DEFINE_LAND_OPERATOR(mona_op_land_f32, float)
DEFINE_LAND_OPERATOR(mona_op_land_f64, double)

#define DEFINE_LOR_OPERATOR(__name__, __type__)                             \
    void __name__(const void* in, void* inout, na_size_t typesize,          \
                  na_size_t count, void* uargs)                             \
    {                                                                       \
        (void)uargs;                                                        \
        const __type__* in_t    = (const __type__*)in;                      \
        __type__*       inout_t = (__type__*)inout;                         \
        na_size_t       i;                                                  \
        for (i = 0; i < count; i++) { inout_t[i] = inout_t[i] || in_t[i]; } \
    }

DEFINE_LOR_OPERATOR(mona_op_lor_u64, uint64_t)
DEFINE_LOR_OPERATOR(mona_op_lor_u32, uint32_t)
DEFINE_LOR_OPERATOR(mona_op_lor_u16, uint16_t)
DEFINE_LOR_OPERATOR(mona_op_lor_u8, uint8_t)
DEFINE_LOR_OPERATOR(mona_op_lor_i64, int64_t)
DEFINE_LOR_OPERATOR(mona_op_lor_i32, int32_t)
DEFINE_LOR_OPERATOR(mona_op_lor_i16, int16_t)
DEFINE_LOR_OPERATOR(mona_op_lor_i8, int8_t)
DEFINE_LOR_OPERATOR(mona_op_lor_f32, float)
DEFINE_LOR_OPERATOR(mona_op_lor_f64, double)

#define DEFINE_OPERATOR(__name__, __base__, __typesize__)          \
    void __name__(const void* in, void* inout, na_size_t typesize, \
                  na_size_t count, void* uargs)                    \
    {                                                              \
        __base__(in, inout, __typesize__, count, uargs);           \
    }

static inline void mona_op_band(const void* in,
                                void*       inout,
                                na_size_t   typesize,
                                na_size_t   count,
                                void*       uargs)
{
    (void)uargs;
    const char* in_char    = (const char*)in;
    char*       inout_char = (char*)inout;
    na_size_t   i;
    for (i = 0; i < typesize * count; i++) {
        inout_char[i] = inout_char[i] & in_char[i];
    }
}

DEFINE_OPERATOR(mona_op_band_u64, mona_op_band, 8)
DEFINE_OPERATOR(mona_op_band_u32, mona_op_band, 4)
DEFINE_OPERATOR(mona_op_band_u16, mona_op_band, 3)
DEFINE_OPERATOR(mona_op_band_u8, mona_op_band, 1)
DEFINE_OPERATOR(mona_op_band_i64, mona_op_band, 8)
DEFINE_OPERATOR(mona_op_band_i32, mona_op_band, 4)
DEFINE_OPERATOR(mona_op_band_i16, mona_op_band, 2)
DEFINE_OPERATOR(mona_op_band_i8, mona_op_band, 1)
DEFINE_OPERATOR(mona_op_band_f32, mona_op_band, 4)
DEFINE_OPERATOR(mona_op_band_f64, mona_op_band, 8)

static inline void mona_op_bor(const void* in,
                               void*       inout,
                               na_size_t   typesize,
                               na_size_t   count,
                               void*       uargs)
{
    (void)uargs;
    const char* in_char    = (const char*)in;
    char*       inout_char = (char*)inout;
    na_size_t   i;
    for (i = 0; i < typesize * count; i++) {
        inout_char[i] = inout_char[i] | in_char[i];
    }
}

DEFINE_OPERATOR(mona_op_bor_u64, mona_op_bor, 8)
DEFINE_OPERATOR(mona_op_bor_u32, mona_op_bor, 4)
DEFINE_OPERATOR(mona_op_bor_u16, mona_op_bor, 3)
DEFINE_OPERATOR(mona_op_bor_u8, mona_op_bor, 1)
DEFINE_OPERATOR(mona_op_bor_i64, mona_op_bor, 8)
DEFINE_OPERATOR(mona_op_bor_i32, mona_op_bor, 4)
DEFINE_OPERATOR(mona_op_bor_i16, mona_op_bor, 2)
DEFINE_OPERATOR(mona_op_bor_i8, mona_op_bor, 1)
DEFINE_OPERATOR(mona_op_bor_f32, mona_op_bor, 4)
DEFINE_OPERATOR(mona_op_bor_f64, mona_op_bor, 8)
