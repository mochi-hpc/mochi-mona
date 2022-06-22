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

static na_return_t mona_comm_allgather_gather_bcast(mona_comm_t comm,
                                                    const void* sendbuf,
                                                    size_t      size,
                                                    void*       recvbuf,
                                                    na_tag_t    tag)
{
    // TODO use a smarter algorithm
    na_return_t na_ret;
    int         comm_size = comm->all.size;

    na_ret = mona_comm_gather(comm, sendbuf, size, recvbuf, 0, tag);
    if (na_ret != NA_SUCCESS) { return na_ret; }

    na_ret = mona_comm_bcast(comm, recvbuf, comm_size * size, 0, tag);

    return na_ret;
}

na_return_t mona_comm_allgather(mona_comm_t comm,
                                const void* sendbuf,
                                size_t      size,
                                void*       recvbuf,
                                na_tag_t    tag)
{
    return mona_comm_allgather_gather_bcast(comm, sendbuf, size, recvbuf, tag);
}

typedef struct iallgather_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    size_t         size;
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
                                 size_t          size,
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

// -----------------------------------------------------------------------
// AllGatherv
// -----------------------------------------------------------------------

static na_return_t mona_comm_allgatherv_gather_bcast(mona_comm_t   comm,
                                                     const void*   sendbuf,
                                                     size_t        sendsize,
                                                     void*         recvbuf,
                                                     const size_t* recvsizes,
                                                     const size_t* displs,
                                                     na_tag_t      tag)
{
    // TODO use a smarter algorithm
    na_return_t na_ret;
    int         comm_size = comm->all.size;
    int         my_rank   = comm->all.rank;
    char* tmp_buf = NULL;

    // start by doing a gatherv to rank 0
    na_ret = mona_comm_gatherv(comm, sendbuf, sendsize, recvbuf, recvsizes,
                               displs, 0, tag);
    if(na_ret != NA_SUCCESS) goto finish;

    // compute total size and allocate contiguous buffer
    size_t total_recv_size = 0;
    for (int i=0; i < comm_size; i++) {
        total_recv_size += recvsizes[i];
    }
    tmp_buf = (char*)malloc(total_recv_size);

    // now the data is fragmented in rank 0, copy into a contiguous buffer
    if(my_rank == 0) {
        int position = 0;
        for(int i=0; i < comm_size; i++) {
            memcpy(tmp_buf + position, recvbuf + displs[i], recvsizes[i]);
            position += recvsizes[i];
        }
    }

    // do a bcast from rank 0
    na_ret = mona_comm_bcast(comm, tmp_buf, total_recv_size, 0, tag);
    if (na_ret != NA_SUCCESS) goto finish;

    // in non-0 ranks, copy back to recvbuf
    if(my_rank != 0) {
        int position = 0;
        for (int i=0; i < comm_size; i++) {
            memcpy((char*)recvbuf + displs[i], tmp_buf+position, recvsizes[i]);
            position += recvsizes[i];
        }
    }

finish:
    free(tmp_buf);
    return na_ret;
}

na_return_t mona_comm_allgatherv(mona_comm_t  comm,
                                const void*   sendbuf,
                                size_t        sendsize,
                                void*         recvbuf,
                                const size_t* recvsizes,
                                const size_t* displs,
                                na_tag_t      tag)
{
    return mona_comm_allgatherv_gather_bcast(comm, sendbuf, sendsize, recvbuf, recvsizes, displs, tag);
}

typedef struct iallgatherv_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    size_t         sendsize;
    void*          recvbuf;
    const size_t*  recvsizes;
    const size_t*  displs;
    na_tag_t       tag;
    mona_request_t req;
} iallgatherv_args;

static void iallgatherv_thread(void* x)
{
    iallgatherv_args* args   = (iallgatherv_args*)x;
    na_return_t      na_ret = mona_comm_allgatherv(
        args->comm, args->sendbuf, args->sendsize,
        args->recvbuf, args->recvsizes, args->displs, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_iallgatherv(mona_comm_t     comm,
                                  const void*     sendbuf,
                                  size_t          sendsize,
                                  void*           recvbuf,
                                  const size_t*   recvsizes,
                                  const size_t*   displs,
                                  na_tag_t        tag,
                                  mona_request_t* req)
{
    NB_OP_INIT(iallgatherv_args);
    args->comm      = comm;
    args->sendbuf   = sendbuf;
    args->sendsize  = sendsize;
    args->recvbuf   = recvbuf;
    args->recvsizes = recvsizes;
    args->displs   = displs;
    args->tag       = tag;
    NB_OP_POST(iallgatherv_thread);
}
