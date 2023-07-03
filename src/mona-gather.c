/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

// -----------------------------------------------------------------------
// Gather
// -----------------------------------------------------------------------

na_return_t mona_comm_gather(mona_comm_t comm,
                             const void* sendbuf,
                             size_t      size,
                             void*       recvbuf,
                             int         root,
                             na_tag_t    tag)
{
    na_return_t     na_ret    = NA_SUCCESS;
    int             comm_size = comm->all.size;
    int             rank      = comm->all.rank;
    int             i;
    mona_request_t* reqs = NULL;

    if (rank == root) {
        mona_request_t* reqs = malloc(comm_size * sizeof(*reqs));
        for (i = 0; i < comm_size; i++) {
            // recv from other process
            if (i == rank) {
                // do the local copy for the root process
                if (sendbuf != MONA_IN_PLACE) {
                    memcpy((char*)recvbuf + i * size, sendbuf, size);
                }
                continue;
            }
            na_ret = mona_comm_irecv(comm, (char*)recvbuf + i * size, size, i,
                                     tag, NULL, NULL, NULL, reqs + i);
            if (na_ret != NA_SUCCESS) goto finish;
        }
        for (i = 0; i < comm_size; i++) {
            if (i == rank) continue;
            na_ret = mona_wait(reqs[i]);
            if (na_ret != NA_SUCCESS) goto finish;
        }
    } else {
        // send to the root process
        na_ret = mona_comm_send(comm, sendbuf, size, root, tag);
        if (na_ret != NA_SUCCESS) goto finish;
    }
finish:
    free(reqs);
    return na_ret;
}

typedef struct igather_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    size_t         size;
    void*          recvbuf;
    int            root;
    na_tag_t       tag;
    mona_request_t req;
} igather_args;

static void igather_thread(void* x)
{
    igather_args* args = (igather_args*)x;
    na_return_t na_ret = mona_comm_gather(args->comm, args->sendbuf, args->size,
                                          args->recvbuf, args->root, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_igather(mona_comm_t     comm,
                              const void*     sendbuf,
                              size_t          size,
                              void*           recvbuf,
                              int             root,
                              na_tag_t        tag,
                              mona_request_t* req)
{
    NB_OP_INIT(igather_args);
    args->comm    = comm;
    args->sendbuf = sendbuf;
    args->size    = size;
    args->recvbuf = recvbuf;
    args->root    = root;
    args->tag     = tag;
    NB_OP_POST(igather_thread);
}

// -----------------------------------------------------------------------
// Gatherv
// -----------------------------------------------------------------------

na_return_t mona_comm_gatherv(mona_comm_t   comm,
                              const void*   sendbuf,
                              size_t        sendsize,
                              void*         recvbuf,
                              const size_t* recvsizes,
                              const size_t* offsets,
                              int           root,
                              na_tag_t      tag)
{
    na_return_t     na_ret    = NA_SUCCESS;
    int             comm_size = comm->all.size;
    int             rank      = comm->all.rank;
    mona_request_t* reqs      = NULL;
    int             i;

    if (rank == root) {
        reqs = malloc(comm_size * sizeof(*reqs));
        for (i = 0; i < comm_size; i++) {
            reqs[i] = MONA_REQUEST_NULL;
            // recv from other process
            if (i == rank) {
                // do the local copy for the root process
                // offset[i] represents the number of the element relative to
                // the recvBuffer for rank i
                if (sendbuf != MONA_IN_PLACE) {
                    memcpy((char*)recvbuf + offsets[i], sendbuf, recvsizes[i]);
                }
                continue;
            }
            // recv from the other processes if the send account is not zero
            if (recvsizes[i] != 0) {
                na_ret = mona_comm_irecv(comm, (char*)recvbuf + offsets[i],
                                         recvsizes[i], i, tag, NULL, NULL, NULL,
                                         reqs + i);
                if (na_ret != NA_SUCCESS) { goto finish; }
            }
        }
        for (i = 0; i < comm_size; i++) {
            if (i == rank) continue;
            if (reqs[i] != MONA_REQUEST_NULL) {
                na_ret = mona_wait(reqs[i]);
                if (na_ret != NA_SUCCESS) { goto finish; }
            }
        }
    } else {
        // send to the root process
        if (sendsize == 0) { goto finish; }
        na_ret = mona_comm_send(comm, sendbuf, sendsize, root, tag);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }
finish:
    free(reqs);
    return na_ret;
}

typedef struct igatherv_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    size_t         sendsize;
    void*          recvbuf;
    const size_t*  recvsizes;
    const size_t*  offsets;
    int            root;
    na_tag_t       tag;
    mona_request_t req;
} igatherv_args;

static void igatherv_thread(void* x)
{
    igatherv_args* args   = (igatherv_args*)x;
    na_return_t    na_ret = mona_comm_gatherv(
        args->comm, args->sendbuf, args->sendsize, args->recvbuf,
        args->recvsizes, args->offsets, args->root, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_igatherv(mona_comm_t     comm,
                               const void*     sendbuf,
                               size_t          sendsize,
                               void*           recvbuf,
                               const size_t*   recvsizes,
                               const size_t*   offsets,
                               int             root,
                               na_tag_t        tag,
                               mona_request_t* req)
{
    NB_OP_INIT(igatherv_args);
    args->comm      = comm;
    args->sendbuf   = sendbuf;
    args->sendsize  = sendsize;
    args->recvbuf   = recvbuf;
    args->recvsizes = recvsizes;
    args->offsets   = offsets;
    args->root      = root;
    args->tag       = tag;
    NB_OP_POST(igatherv_thread);
}
