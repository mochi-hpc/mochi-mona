/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

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

