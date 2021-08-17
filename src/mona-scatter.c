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
// Scatter
// -----------------------------------------------------------------------

na_return_t mona_comm_scatter(mona_comm_t comm,
                              const void* sendbuf,
                              na_size_t   size,
                              void*       recvbuf,
                              int         root,
                              na_tag_t    tag)
{
    // TODO use try something more efficient like binomial
    int             i;
    na_return_t     na_ret    = NA_SUCCESS;
    int             comm_size = comm->all.size;
    int             rank      = comm->all.rank;
    mona_request_t* reqs      = NULL;

    if (root == rank) {
        reqs = malloc(comm_size * sizeof(*reqs));
        for (i = 0; i < comm_size; i++) {
            const char* sendaddr = (const char*)sendbuf + i * size;
            if (i == rank) {
                memcpy(recvbuf, sendaddr, size);
                continue;
            }
            na_ret = mona_comm_isend(comm, sendaddr, size, i, tag, reqs + i);
            if (na_ret != NA_SUCCESS) goto finish;
        }
        for (i = 0; i < comm_size; i++) {
            if (i == rank) continue;
            na_ret = mona_wait(reqs[i]);
            if (na_ret != NA_SUCCESS) goto finish;
        }
    } else {
        na_ret
            = mona_comm_recv(comm, recvbuf, size, root, tag, NULL);
    }
finish:
    free(reqs);
    return na_ret;
}

typedef struct iscatter_args {
    mona_comm_t    comm;
    const void*    sendbuf;
    na_size_t      size;
    void*          recvbuf;
    int            root;
    na_tag_t       tag;
    mona_request_t req;
} iscatter_args;

static void iscatter_thread(void* x)
{
    iscatter_args* args = (iscatter_args*)x;
    na_return_t    na_ret
        = mona_comm_scatter(args->comm, args->sendbuf, args->size,
                            args->recvbuf, args->root, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_iscatter(mona_comm_t     comm,
                               const void*     sendbuf,
                               na_size_t       size,
                               void*           recvbuf,
                               int             root,
                               na_tag_t        tag,
                               mona_request_t* req)
{
    NB_OP_INIT(iscatter_args);
    args->comm    = comm;
    args->sendbuf = sendbuf;
    args->size    = size;
    args->recvbuf = recvbuf;
    args->root    = root;
    args->tag     = tag;
    NB_OP_POST(iscatter_thread);
}

// -----------------------------------------------------------------------
// Scatterv
// -----------------------------------------------------------------------

na_return_t mona_comm_scatterv(mona_comm_t      comm,
                               const void*      sendbuf,
                               const na_size_t* sendsizes,
                               const na_size_t* displs,
                               void*            recvbuf,
                               na_size_t        recvsize,
                               int              root,
                               na_tag_t         tag)
{
    // TODO use try something more efficient like binomial
    int             i;
    na_return_t     na_ret    = NA_SUCCESS;
    int             comm_size = comm->all.size;
    int             rank      = comm->all.rank;
    mona_request_t* reqs      = NULL;

    if (root == rank) {
        reqs = malloc(comm_size * sizeof(*reqs));
        for (i = 0; i < comm_size; i++) {
            const char* sendaddr = (const char*)sendbuf + displs[i];
            if (i == rank) {
                memcpy(recvbuf, sendaddr,
                       recvsize < sendsizes[i] ? recvsize : sendsizes[i]);
                continue;
            }
            na_ret = mona_comm_isend(comm, sendaddr, sendsizes[i], i, tag,
                                     reqs + i);
            if (na_ret != NA_SUCCESS) goto finish;
        }
        for (i = 0; i < comm_size; i++) {
            if (i == rank) continue;
            na_ret = mona_wait(reqs[i]);
            if (na_ret != NA_SUCCESS) goto finish;
        }
    } else {
        na_ret = mona_comm_recv(comm, recvbuf, recvsize, root, tag, NULL);
    }
finish:
    free(reqs);
    return na_ret;
}

typedef struct iscatterv_args {
    mona_comm_t      comm;
    const void*      sendbuf;
    const na_size_t* sendsizes;
    const na_size_t* displs;
    void*            recvbuf;
    na_size_t        recvsize;
    int              root;
    na_tag_t         tag;
    mona_request_t   req;
} iscatterv_args;

static void iscatterv_thread(void* x)
{
    iscatterv_args* args   = (iscatterv_args*)x;
    na_return_t     na_ret = mona_comm_scatterv(
        args->comm, args->sendbuf, args->sendsizes, args->displs, args->recvbuf,
        args->recvsize, args->root, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_iscatterv(mona_comm_t      comm,
                                const void*      sendbuf,
                                const na_size_t* sendsizes,
                                const na_size_t* displs,
                                void*            recvbuf,
                                na_size_t        recvsize,
                                int              root,
                                na_tag_t         tag,
                                mona_request_t*  req)
{
    NB_OP_INIT(iscatterv_args);
    args->comm      = comm;
    args->sendbuf   = sendbuf;
    args->sendsizes = sendsizes;
    args->displs    = displs;
    args->recvbuf   = recvbuf;
    args->recvsize  = recvsize;
    args->root      = root;
    args->tag       = tag;
    NB_OP_POST(iscatterv_thread);
}
