/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include "mona-coll.h"
#include <string.h>

typedef struct mona_comm {
    mona_instance_t mona;
    na_size_t       size;
    na_size_t       rank;
    na_addr_t*      addrs;
} mona_comm;

#define NB_OP_INIT(__argtype__)                                             \
    ABT_eventual eventual;                                                  \
    int          ret = ABT_eventual_create(sizeof(na_return_t), &eventual); \
    if (ret != 0) return NA_NOMEM;                                          \
    __argtype__* args = (__argtype__*)malloc(sizeof(*args))

#define NB_OP_POST(__thread__)                                           \
    mona_request_t tmp_req = get_req_from_cache(comm->mona);             \
    tmp_req->eventual      = eventual;                                   \
    args->req              = tmp_req;                                    \
    ret = ABT_thread_create(comm->mona->progress_pool, __thread__, args, \
                            ABT_THREAD_ATTR_NULL, NULL);                 \
    if (ret != ABT_SUCCESS) {                                            \
        return_req_to_cache(comm->mona, tmp_req);                        \
        return NA_NOMEM;                                                 \
    } else {                                                             \
        *req = tmp_req;                                                  \
        ABT_thread_yield();                                              \
    }                                                                    \
    return NA_SUCCESS

na_return_t mona_comm_create(mona_instance_t  mona,
                             na_size_t        count,
                             const na_addr_t* peers,
                             mona_comm_t*     comm)
{
    na_return_t na_ret;
    unsigned    i = 0, j = 0, k = 0;
    if (count == 0) { return NA_INVALID_ARG; }
    mona_comm_t tmp = calloc(1, sizeof(mona_comm));
    if (!tmp) return NA_NOMEM;
    tmp->mona  = mona;
    tmp->size  = count;
    tmp->rank  = count;
    tmp->addrs = calloc(sizeof(na_addr_t), count);
    if (!tmp->addrs) {
        na_ret = NA_NOMEM;
        goto error;
    }
    // copy array of addresses and find rank of self
    for (i = 0; i < count; i++) {
        na_ret = mona_addr_dup(mona, peers[i], tmp->addrs + i);
        if (na_ret != NA_SUCCESS) goto error;
        if (mona_addr_is_self(mona, peers[i])) {
            if (tmp->rank == count)
                tmp->rank = i;
            else {
                na_ret = NA_INVALID_ARG;
                i += 1;
                goto error;
            }
        }
    }
    if (tmp->rank == count) {
        na_ret = NA_INVALID_ARG;
        goto error;
    }
    // check that there is not twice the same address
    for (j = 0; j < count; j++) {
        for (k = j + 1; k < count; k++) {
            if (mona_addr_cmp(mona, peers[j], peers[k])) {
                na_ret = NA_INVALID_ARG;
                goto error;
            }
        }
    }
    *comm = tmp;

finish:
    return na_ret;

error:
    for (j = 0; j < i; j++) { mona_addr_free(mona, tmp->addrs[i]); }
    free(tmp->addrs);
    free(tmp);
    goto finish;
}

na_return_t mona_comm_free(mona_comm_t comm)
{
    unsigned    i;
    na_return_t na_ret = NA_SUCCESS;
    for (i = 0; i < comm->size; i++) {
        na_ret = mona_addr_free(comm->mona, comm->addrs[i]);
        if (na_ret != NA_SUCCESS) return na_ret;
    }
    free(comm->addrs);
    free(comm);
    return na_ret;
}

na_return_t mona_comm_size(mona_comm_t comm, int* size)
{
    *size = comm->size;
    return NA_SUCCESS;
}

na_return_t mona_comm_rank(mona_comm_t comm, int* rank)
{
    *rank = comm->rank;
    return NA_SUCCESS;
}

na_return_t
mona_comm_addr(mona_comm_t comm, int rank, na_addr_t* addr, na_bool_t copy)
{
    if (rank < 0 || (unsigned)rank >= comm->size) return NA_INVALID_ARG;
    if (copy) {
        return mona_addr_dup(comm->mona, comm->addrs[rank], addr);
    } else {
        *addr = comm->addrs[rank];
    }
    return NA_SUCCESS;
}

na_return_t mona_comm_dup(mona_comm_t comm, mona_comm_t* new_comm)
{
    return mona_comm_create(comm->mona, comm->size, comm->addrs, new_comm);
}

na_return_t mona_comm_subset(mona_comm_t  comm,
                             const int*   ranks,
                             na_size_t    size,
                             mona_comm_t* new_comm)
{
    if (size > comm->size) return NA_INVALID_ARG;
    na_addr_t* addrs = alloca(size * sizeof(*addrs));
    unsigned   i;
    for (i = 0; i < size; i++) { addrs[i] = comm->addrs[ranks[i]]; }
    return mona_comm_create(comm->mona, size, addrs, new_comm);
}

// -----------------------------------------------------------------------
// Send/Recv
// -----------------------------------------------------------------------

na_return_t mona_comm_send(
    mona_comm_t comm, const void* buf, na_size_t size, int dest, na_tag_t tag)
{
    if (dest < 0 || (unsigned)dest >= comm->size) return NA_INVALID_ARG;
    return mona_send(comm->mona, buf, size, comm->addrs[dest], 0, tag);
}

na_return_t mona_comm_isend(mona_comm_t     comm,
                            const void*     buf,
                            na_size_t       size,
                            int             dest,
                            na_tag_t        tag,
                            mona_request_t* req)
{
    if (dest < 0 || (unsigned)dest >= comm->size) return NA_INVALID_ARG;
    return mona_isend(comm->mona, buf, size, comm->addrs[dest], 0, tag, req);
}

na_return_t mona_comm_recv(mona_comm_t comm,
                           void*       buf,
                           na_size_t   size,
                           int         src,
                           na_tag_t    tag,
                           na_size_t*  actual_size)
{
    if (src < 0 || (unsigned)src >= comm->size) return NA_INVALID_ARG;
    na_return_t na_ret = mona_recv(comm->mona, buf, size, comm->addrs[src], tag,
                                   actual_size);
    return na_ret;
}

typedef struct irecv_args {
    mona_comm_t    comm;
    void*          buf;
    na_size_t      size;
    int            src;
    na_tag_t       tag;
    na_size_t*     actual_size;
    mona_request_t req;
} irecv_args;

static void irecv_thread(void* x)
{
    irecv_args* args   = (irecv_args*)x;
    na_return_t na_ret = mona_comm_recv(args->comm, args->buf, args->size,
                                        args->src, args->tag, args->actual_size);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_irecv(mona_comm_t     comm,
                            void*           buf,
                            na_size_t       size,
                            int             src,
                            na_tag_t        tag,
                            na_size_t*      actual_size,
                            mona_request_t* req)
{
    NB_OP_INIT(irecv_args);
    args->comm        = comm;
    args->buf         = buf;
    args->size        = size;
    args->src         = src;
    args->tag         = tag;
    args->actual_size = actual_size;
    NB_OP_POST(irecv_thread);
}

na_return_t mona_comm_sendrecv(mona_comm_t comm,
                               const void* sendbuf,
                               na_size_t   sendsize,
                               int         dest,
                               na_tag_t    sendtag,
                               void*       recvbuf,
                               na_size_t   recvsize,
                               int         source,
                               na_tag_t    recvtag,
                               na_size_t*  actual_recvsize)
{
    mona_request_t sendreq;
    na_return_t    na_ret;

    na_ret = mona_comm_irecv(comm, recvbuf, recvsize, source, recvtag,
                             actual_recvsize, &sendreq);
    if (na_ret != NA_SUCCESS) return na_ret;

    na_ret = mona_comm_send(comm, sendbuf, sendsize, dest, sendtag);

    if (na_ret != NA_SUCCESS) {
        mona_wait(sendreq);
        return na_ret;
    }

    na_ret = mona_wait(sendreq);
    return na_ret;
}

// -----------------------------------------------------------------------
// Barrier
// -----------------------------------------------------------------------

na_return_t mona_comm_barrier(mona_comm_t comm, na_tag_t tag)
{
    int size, rank, src, dst, mask;
    size               = (int)comm->size;
    rank               = (int)comm->rank;
    na_return_t na_ret = NA_SUCCESS;

    if (size == 1) return na_ret;

    mask = 0x1;
    while (mask < size) {
        dst    = (rank + mask) % size;
        src    = (rank - mask + size) % size;
        na_ret = mona_comm_sendrecv(comm, NULL, 0, dst, tag, NULL, 0, src, tag, NULL);
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

// -----------------------------------------------------------------------
// Broadcast
// -----------------------------------------------------------------------

na_return_t mona_comm_bcast(
    mona_comm_t comm, void* buf, na_size_t size, int root, na_tag_t tag)
{
    // TODO Mpich provides 2 other implementations:
    // - scatter recursive doubling allgather
    // - scatter ring allgather
    // These could be added as alternatives
    int         rank, comm_size, src, dst;
    int         relative_rank, mask;
    na_return_t na_ret;

    comm_size = comm->size;
    rank      = comm->rank;

    // If there is only one process, return
    if (comm_size == 1) return NA_SUCCESS;

    relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    // Use short message algorithm, namely, binomial tree
    // operations to recieve the data
    mask = 0x1;
    while (mask < comm_size) {
        if (relative_rank & mask) {
            src = rank - mask;
            if (src < 0) src += comm_size;
            na_ret
                = mona_comm_recv(comm, buf, size, src, tag, NULL);
            if (na_ret != NA_SUCCESS) { return na_ret; }
            break;
        }
        mask <<= 1;
    }
    mask >>= 1;

    // operations to send the data
    while (mask > 0) {
        if (relative_rank + mask < comm_size) {
            dst = rank + mask;
            if (dst >= comm_size) dst -= comm_size;
            na_ret = mona_comm_send(comm, buf, size, dst, tag);
            if (na_ret != NA_SUCCESS) { return na_ret; }
        }
        mask >>= 1;
    }
    return na_ret;

#if 0
    na_return_t na_ret;
    int i = 0, r = 0;
    int comm_size = (int)comm->size;
    int rank = (int)comm->rank;

    int relative_rank = (rank - root) % size;
    int relative_dst, dst, src;

    int b = 1; // number of processes to have the data
    int N = 2; // TODO make radix configurable
    int n = 1; // current power of N

    mona_request_t* reqs = calloc(size, sizeof(mona_request_t));
    int req_count = 0;

    while(b < comm_size) {
        for(i = 1; i < N; i++) {
            relative_dst = i*n;
            for(r = 0; r < b; r++) {
                if(relative_dst >= comm_size)
                    continue;
                if(relative_dst == relative_rank) {
                    src = (r + root) % comm_size;
                    printf("Rank %d receives from rank %d\n", rank, src);
                    na_ret = mona_comm_recv(comm, buf, size, src, tag, NULL, NULL, NULL);
                    if(na_ret != NA_SUCCESS) goto fn_exit;
                }
                if(r == relative_rank) {
                    dst = (relative_dst + root) % comm_size;
                    printf("Rank %d sends to rank %d\n", rank, dst);
                    na_ret = mona_comm_isend(comm, buf, size, dst, tag, reqs + req_count);
                    if(na_ret != NA_SUCCESS) goto fn_exit;
                }
                relative_dst += 1;
            }
        }
        b = relative_dst;
        n *= N;
    }

    for(i = 0; i < req_count; i++) {
        na_ret = mona_wait(reqs[i]);
        if(na_ret != NA_SUCCESS) goto fn_exit;
    }

fn_exit:
    free(reqs);
    return na_ret;
#endif
}

typedef struct ibcast_args {
    mona_comm_t    comm;
    void*          buf;
    na_size_t      size;
    int            root;
    na_tag_t       tag;
    mona_request_t req;
} ibcast_args;

static void ibcast_thread(void* x)
{
    ibcast_args* args   = (ibcast_args*)x;
    na_return_t  na_ret = mona_comm_bcast(args->comm, args->buf, args->size,
                                         args->root, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_ibcast(mona_comm_t     comm,
                             void*           buf,
                             na_size_t       size,
                             int             root,
                             na_tag_t        tag,
                             mona_request_t* req)
{
    NB_OP_INIT(ibcast_args);
    args->comm = comm;
    args->buf  = buf;
    args->size = size;
    args->root = root;
    args->tag  = tag;
    NB_OP_POST(ibcast_thread);
}

// -----------------------------------------------------------------------
// Gather
// -----------------------------------------------------------------------

na_return_t mona_comm_gather(mona_comm_t comm,
                             const void* sendbuf,
                             na_size_t   size,
                             void*       recvbuf,
                             int         root,
                             na_tag_t    tag)
{
    na_return_t     na_ret    = NA_SUCCESS;
    int             comm_size = comm->size;
    int             rank      = comm->rank;
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
                                     tag, NULL, reqs + i);
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
    na_size_t      size;
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
                              na_size_t       size,
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

na_return_t mona_comm_gatherv(mona_comm_t      comm,
                              const void*      sendbuf,
                              na_size_t        sendsize,
                              void*            recvbuf,
                              const na_size_t* recvsizes,
                              const na_size_t* offsets,
                              int              root,
                              na_tag_t         tag)
{
    na_return_t     na_ret    = NA_SUCCESS;
    int             comm_size = comm->size;
    int             rank      = comm->rank;
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
                                         recvsizes[i], i, tag, NULL, reqs + i);
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
    mona_comm_t      comm;
    const void*      sendbuf;
    na_size_t        sendsize;
    void*            recvbuf;
    const na_size_t* recvsizes;
    const na_size_t* offsets;
    int              root;
    na_tag_t         tag;
    mona_request_t   req;
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

na_return_t mona_comm_igatherv(mona_comm_t      comm,
                               const void*      sendbuf,
                               na_size_t        sendsize,
                               void*            recvbuf,
                               const na_size_t* recvsizes,
                               const na_size_t* offsets,
                               int              root,
                               na_tag_t         tag,
                               mona_request_t*  req)
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
    int             comm_size = comm->size;
    int             rank      = comm->rank;
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
    int             comm_size = comm->size;
    int             rank      = comm->rank;
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
