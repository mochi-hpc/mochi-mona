/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

// -----------------------------------------------------------------------
// Barrier
// -----------------------------------------------------------------------

na_return_t mona_comm_barrier(mona_comm_t comm, na_tag_t tag)
{
    int size, rank, src, dst, mask;
    size               = (int)comm->all.size;
    rank               = (int)comm->all.rank;
    na_return_t na_ret = NA_SUCCESS;

    if (size == 1) return na_ret;

    mask = 0x1;
    while (mask < size) {
        dst    = (rank + mask) % size;
        src    = (rank - mask + size) % size;
        na_ret = mona_comm_sendrecv(comm, NULL, 0, dst, tag, NULL, 0, src, tag,
                                    NULL);
        if (na_ret) break;
        mask <<= 1;
    }

    return na_ret;
}

typedef struct ibarrier_args {
    mona_comm_t    comm;
    na_tag_t       tag;
    mona_request_t req;
} ibarrier_args;

static void ibarrier_thread(void* x)
{
    ibarrier_args* args   = (ibarrier_args*)x;
    na_return_t    na_ret = mona_comm_barrier(args->comm, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t
mona_comm_ibarrier(mona_comm_t comm, na_tag_t tag, mona_request_t* req)
{
    NB_OP_INIT(ibarrier_args);
    args->comm = comm;
    args->tag  = tag;
    NB_OP_POST(ibarrier_thread);
}
