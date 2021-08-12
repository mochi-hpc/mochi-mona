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
// AllReduce
// -----------------------------------------------------------------------

na_return_t mona_comm_allreduce(mona_comm_t comm,
                                const void* sendbuf,
                                void*       recvbuf,
                                na_size_t   typesize,
                                na_size_t   count,
                                mona_op_t   op,
                                void*       uargs,
                                na_tag_t    tag)
{
    // TODO this is a simplistic algorithm (reduce followed by bcasy),
    // ideally we would want to implement the algorithms provided in Mpich
    // and even some designed specifically for Mona

    // reduce
    na_return_t na_ret;
    int         root = 0;
    na_ret = mona_comm_reduce(comm, sendbuf, recvbuf, typesize, count, op,
                              uargs, root, tag);
    if (na_ret != NA_SUCCESS) { return na_ret; }

    // bcast
    na_ret = mona_comm_bcast(comm, recvbuf, typesize * count, root, tag);
    return na_ret;
}

typedef struct iallreduce_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    void*          recvbuf;
    na_size_t      typesize;
    na_size_t      count;
    mona_op_t      op;
    void*          uargs;
    na_tag_t       tag;
    mona_request_t req;
} iallreduce_args;

static void iallreduce_thread(void* x)
{
    iallreduce_args* args   = (iallreduce_args*)x;
    na_return_t      na_ret = mona_comm_allreduce(
        args->comm, args->sendbuf, args->recvbuf, args->typesize, args->count,
        args->op, args->uargs, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_iallreduce(mona_comm_t     comm,
                                 const void*     sendbuf,
                                 void*           recvbuf,
                                 na_size_t       typesize,
                                 na_size_t       count,
                                 mona_op_t       op,
                                 void*           uargs,
                                 na_tag_t        tag,
                                 mona_request_t* req)
{
    NB_OP_INIT(iallreduce_args);
    args->comm     = comm;
    args->sendbuf  = sendbuf;
    args->recvbuf  = recvbuf;
    args->typesize = typesize;
    args->count    = count;
    args->op       = op;
    args->uargs    = uargs;
    args->tag      = tag;
    NB_OP_POST(iallreduce_thread);
}

