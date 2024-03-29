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

static na_return_t alltoall_mem(mona_comm_t comm,
                                const void* sendbuf,
                                size_t      blocksize,
                                void*       recvbuf,
                                na_tag_t    tag)
{
    mona_team_t* team      = &comm->all;
    na_return_t  na_ret    = NA_SUCCESS;
    int          comm_size = team->size;
    int          rank      = team->rank;
    size_t       offset    = 0;

    mona_mem_handle_t sendmem = MONA_MEM_HANDLE_NULL;
    mona_mem_handle_t recvmem = MONA_MEM_HANDLE_NULL;

    mona_request_t* reqs = malloc(2 * comm_size * sizeof(*reqs));

    struct na_segment sendseg = {(void*)sendbuf, blocksize * comm_size};
    na_ret = mona_mem_handle_create_segments(comm->mona, &sendseg, 1,
                                             NA_MEM_READ_ONLY, &sendmem);
    if (na_ret != NA_SUCCESS) { goto finish; }
    na_ret = mona_mem_register(comm->mona, sendmem);
    if (na_ret != NA_SUCCESS) { goto finish; }

    struct na_segment recvseg = {recvbuf, blocksize * comm_size};
    na_ret = mona_mem_handle_create_segments(comm->mona, &recvseg, 1,
                                             NA_MEM_READWRITE, &recvmem);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_mem_register(comm->mona, recvmem);
    if (na_ret != NA_SUCCESS) goto finish;

    for (int i = 0; i < comm_size; i++) {
        offset = i * blocksize;
        if (i == rank) {
            memcpy(recvbuf + offset, sendbuf + offset, blocksize);
            reqs[i] = MONA_REQUEST_NULL;
            continue;
        }
        MONA_COMM_IRECV_MEM(na_ret, comm, team, recvmem, blocksize, offset, i,
                            tag, NULL, reqs + i);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }

    for (int i = 0; i < comm_size; i++) {
        offset = i * blocksize;
        if (i == rank) {
            reqs[comm_size + i] = MONA_REQUEST_NULL;
            continue;
        }
        MONA_COMM_ISEND_MEM(na_ret, comm, team, sendmem, blocksize, offset, i,
                            tag, reqs + comm_size + i);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }

    for (int i = 0; i < 2 * comm_size; i++) {
        if (reqs[i] == MONA_REQUEST_NULL) continue;
        na_ret = mona_wait(reqs[i]);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }

finish:
    if (sendmem != MONA_MEM_HANDLE_NULL) {
        mona_mem_deregister(comm->mona, sendmem);
        mona_mem_handle_free(comm->mona, sendmem);
    }
    if (recvmem != MONA_MEM_HANDLE_NULL) {
        mona_mem_deregister(comm->mona, recvmem);
        mona_mem_handle_free(comm->mona, recvmem);
    }
    free(reqs);
    return na_ret;
}

static na_return_t alltoall(mona_comm_t comm,
                            const void* sendbuf,
                            size_t      blocksize,
                            void*       recvbuf,
                            na_tag_t    tag)
{
    mona_team_t* team      = &comm->all;
    na_return_t  na_ret    = NA_SUCCESS;
    int          comm_size = team->size;
    int          rank      = team->rank;
    size_t       offset    = 0;

    mona_request_t* reqs = malloc(2 * comm_size * sizeof(*reqs));

    for (int i = 0; i < comm_size; i++) {
        offset = i * blocksize;
        if (i == rank) {
            memcpy(recvbuf + offset, sendbuf + offset, blocksize);
            reqs[i] = MONA_REQUEST_NULL;
            continue;
        }
        char* recvaddr = (char*)recvbuf + offset;
        MONA_COMM_IRECV(na_ret, comm, team, recvaddr, blocksize, i, tag, NULL,
                        reqs + i);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }

    for (int i = 0; i < comm_size; i++) {
        offset = i * blocksize;
        if (i == rank) {
            reqs[comm_size + i] = MONA_REQUEST_NULL;
            continue;
        }
        char* sendaddr = (char*)sendbuf + offset;
        MONA_COMM_ISEND(na_ret, comm, team, sendaddr, blocksize, i, tag,
                        reqs + comm_size + i);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }

    for (int i = 0; i < 2 * comm_size; i++) {
        if (reqs[i] == MONA_REQUEST_NULL) continue;
        na_ret = mona_wait(reqs[i]);
        if (na_ret != NA_SUCCESS) { goto finish; }
    }

finish:
    free(reqs);
    return na_ret;
}

na_return_t mona_comm_alltoall(mona_comm_t comm,
                               const void* sendbuf,
                               size_t      blocksize,
                               void*       recvbuf,
                               na_tag_t    tag)
{
    return alltoall(comm, sendbuf, blocksize, recvbuf, tag);
}

typedef struct ialltoall_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    size_t         blocksize;
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
                                size_t          blocksize,
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
