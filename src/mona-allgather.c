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
// AllGather
// -----------------------------------------------------------------------

na_return_t mona_comm_allgather(mona_comm_t comm,
                                const void* sendbuf,
                                na_size_t   size,
                                void*       recvbuf,
                                na_tag_t    tag)
{
    // TODO use a smarter algorithm
    na_return_t na_ret;
    int         comm_size = comm->size;

    na_ret = mona_comm_gather(comm, sendbuf, size, recvbuf, 0, tag);
    if (na_ret != NA_SUCCESS) { return na_ret; }

    na_ret = mona_comm_bcast(comm, recvbuf, comm_size * size, 0, tag);

    return na_ret;
}

typedef struct iallgather_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    na_size_t      size;
    void*          recvbuf;
    na_tag_t       tag;
    mona_request_t req;
} iallgather_args;

static void iallgather_thread(void* x)
{
    iallgather_args* args   = (iallgather_args*)x;
    na_return_t      na_ret = mona_comm_allgather(
        args->comm, args->sendbuf, args->size, args->recvbuf, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_iallgather(mona_comm_t     comm,
                                 const void*     sendbuf,
                                 na_size_t       size,
                                 void*           recvbuf,
                                 na_tag_t        tag,
                                 mona_request_t* req)
{
    NB_OP_INIT(iallgather_args);
    args->comm    = comm;
    args->sendbuf = sendbuf;
    args->size    = size;
    args->recvbuf = recvbuf;
    args->tag     = tag;
    NB_OP_POST(iallgather_thread);
}

