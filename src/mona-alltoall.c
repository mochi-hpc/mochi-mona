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
// AllToAll
// -----------------------------------------------------------------------

na_return_t mona_comm_alltoall(mona_comm_t comm,
                               const void* sendbuf,
                               na_size_t   blocksize,
                               void*       recvbuf,
                               na_tag_t    tag)
{
    na_return_t na_ret    = NA_SUCCESS;
    int         comm_size = comm->size;
    int         rank      = comm->rank;
    char*       recvaddr  = NULL;
    char*       sendaddr  = NULL;

    mona_request_t* reqs = malloc(comm_size * sizeof(*reqs));

    for (int i = 0; i < comm_size; i++) {
        recvaddr = (char*)recvbuf + i * blocksize;
        if (i == rank) {
            sendaddr = (char*)sendbuf + i * blocksize;
            memcpy(recvaddr, sendaddr, blocksize);
            continue;
        }
        na_ret = mona_comm_irecv(comm, recvaddr, blocksize, i, tag, NULL, reqs + i);
        if (na_ret != NA_SUCCESS) {
            goto finish;
        }
    }

    for (int i = 0; i < comm_size; i++) {
        if (i == rank) {
            reqs[i] = MONA_REQUEST_NULL;
            continue;
        }
        sendaddr = (char*)sendbuf + i * blocksize;
        na_ret   = mona_comm_send(comm, sendaddr, blocksize, i, tag);
        if (na_ret != NA_SUCCESS) {
            goto finish;
        }
    }

    for (int i = 0; i < comm_size; i++) {
        if (i == rank) continue;
        na_ret = mona_wait(reqs[i]);
        if (na_ret != NA_SUCCESS) {
            goto finish;
        }
    }

finish:
    free(reqs);
    return na_ret;
}

typedef struct ialltoall_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    na_size_t      blocksize;
    void*          recvbuf;
    na_tag_t       tag;
    mona_request_t req;
} ialltoall_args;

static void ialltoall_thread(void* x)
{
    ialltoall_args* args   = (ialltoall_args*)x;
    na_return_t     na_ret = mona_comm_alltoall(
        args->comm, args->sendbuf, args->blocksize, args->recvbuf, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_ialltoall(mona_comm_t     comm,
                                const void*     sendbuf,
                                na_size_t       blocksize,
                                void*           recvbuf,
                                na_tag_t        tag,
                                mona_request_t* req)
{
    NB_OP_INIT(ialltoall_args);
    args->comm      = comm;
    args->sendbuf   = sendbuf;
    args->blocksize = blocksize;
    args->recvbuf   = recvbuf;
    args->tag       = tag;
    NB_OP_POST(ialltoall_thread);
}
