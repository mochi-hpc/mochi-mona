/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include "mona-coll.h"

typedef struct mona_comm {
    mona_instance_t mona;
    na_size_t       size;
    na_size_t       rank;
    na_addr_t       addrs[1];
} mona_comm;

#define NB_OP_INIT(__argtype__) \
    ABT_eventual eventual; \
int ret = ABT_eventual_create(sizeof(na_return_t), &eventual); \
if(ret != 0) \
return NA_NOMEM; \
__argtype__* args = (__argtype__*)malloc(sizeof(*args))

#define NB_OP_POST(__thread__) \
    mona_request_t tmp_req = get_req_from_cache(comm->mona); \
tmp_req->eventual = eventual; \
args->req = tmp_req; \
ret = ABT_thread_create(comm->mona->progress_pool, \
        __thread__, args, ABT_THREAD_ATTR_NULL, NULL); \
if(ret != ABT_SUCCESS) { \
    return_req_to_cache(comm->mona, tmp_req); \
    return NA_NOMEM; \
} else { \
    *req = tmp_req; \
    ABT_thread_yield(); \
} \
return NA_SUCCESS

na_return_t mona_comm_create(
        mona_instance_t mona,
        na_size_t count,
        const na_addr_t* peers,
        mona_comm_t* comm)
{
    na_return_t na_ret;
    unsigned i = 0, j = 0, k = 0;
    if(count == 0)
        return NA_INVALID_ARG;
    na_size_t s = sizeof(mona_comm) - 1 + count*sizeof(na_addr_t);
    mona_comm_t tmp = calloc(1, s);
    if(!tmp)
        return NA_NOMEM;
    tmp->mona   = mona;
    tmp->size   = count;
    tmp->rank   = count;
    // copy array of addresses and find rank of self
    for(i = 0; i < count; i++) {
        na_ret = mona_addr_dup(mona, peers[i], tmp->addrs + i);
        if(na_ret != NA_SUCCESS)
            goto error;
        if(mona_addr_is_self(mona, peers[i])) {
            if(tmp->rank == count) tmp->rank = i;
            else {
                na_ret = NA_INVALID_ARG;
                i += 1;
                goto error;
            }
        }
    }
    if(tmp->rank == count) {
        na_ret = NA_INVALID_ARG;
        goto error;
    }
    // check that there is not twice the same address
    for(j = 0; j < count; j++) {
        for(k = j + 1; k < count; k++) {
            if(mona_addr_cmp(mona, peers[j], peers[k])) {
                na_ret = NA_INVALID_ARG;
                goto error;
            }
        }
    }
    *comm = tmp;

finish:
    return na_ret;

error:
    for(j = 0; j < i; j++) {
        mona_addr_free(mona, tmp->addrs[i]);
    }
    free(tmp);
    goto finish;
}

na_return_t mona_comm_free(mona_comm_t comm)
{
    unsigned i;
    na_return_t na_ret = NA_SUCCESS;
    for(i = 0; i < comm->size; i++) {
        na_ret = mona_addr_free(comm->mona, comm->addrs[i]);
        if(na_ret != NA_SUCCESS)
            return na_ret;
    }
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

na_return_t mona_comm_addr(mona_comm_t comm, int rank, na_addr_t* addr, na_bool_t copy)
{
    if(rank < 0 || (unsigned)rank >= comm->size)
        return NA_INVALID_ARG;
    if(copy) {
        return mona_addr_dup(comm->mona, comm->addrs[rank], addr);
    } else {
        *addr = comm->addrs[rank];
    }
    return NA_SUCCESS;
}

na_return_t mona_comm_dup(
        mona_comm_t comm,
        mona_comm_t* new_comm)
{
    return mona_comm_create(comm->mona, comm->size, comm->addrs, new_comm);
}

na_return_t mona_comm_subset(
        mona_comm_t comm,
        int* ranks,
        na_size_t size,
        mona_comm_t* new_comm)
{
    if(size > comm->size)
        return NA_INVALID_ARG;
    na_addr_t* addrs = alloca(size*sizeof(*addrs));
    unsigned i;
    for(i = 0; i < size; i++) {
        addrs[i] = comm->addrs[ranks[i]];
    }
    return mona_comm_create(comm->mona, size, addrs, new_comm);
}

// -----------------------------------------------------------------------
// Send/Recv
// -----------------------------------------------------------------------

na_return_t mona_comm_send(
        mona_comm_t comm,
        const void *buf,
        na_size_t size,
        int dest,
        na_tag_t tag)
{
    if(dest < 0 || (unsigned)dest >= comm->size)
        return NA_INVALID_ARG;
    return mona_send(comm->mona, buf, size, comm->addrs[dest], 0, tag);
}

na_return_t mona_comm_isend(
        mona_comm_t comm,
        const void *buf,
        na_size_t size,
        int dest,
        na_tag_t tag,
        mona_request_t* req)
{
    if(dest < 0 || (unsigned)dest >= comm->size)
        return NA_INVALID_ARG;
    return mona_isend(comm->mona, buf, size, comm->addrs[dest], 0, tag, req);
}

na_return_t mona_comm_recv(
        mona_comm_t comm,
        void* buf,
        na_size_t size,
        int src,
        na_tag_t tag,
        na_size_t* actual_size,
        int* actual_src,
        na_tag_t* actual_tag)
{
    if(src < 0 || (unsigned)src >= comm->size)
        return NA_INVALID_ARG;
    na_addr_t actual_src_addr;
    na_return_t na_ret = mona_recv(
            comm->mona, buf, size, comm->addrs[src],
            tag, actual_size, &actual_src_addr, actual_tag);
    if(na_ret != NA_SUCCESS)
        return na_ret;
    unsigned i;
    for(i = 0; i < comm->size; i++) {
        if(mona_addr_cmp(comm->mona, actual_src_addr, comm->addrs[i])) {
            if(actual_src) *actual_src = i;
            break;
        }
    }
    if(i == comm->size) {
        na_ret = NA_PROTOCOL_ERROR;
    }
    mona_addr_free(comm->mona, actual_src_addr);
    return na_ret;
}

typedef struct irecv_args {
    mona_comm_t    comm;
    void*          buf;
    na_size_t      size;
    int            src;
    na_tag_t       tag;
    na_size_t*     actual_size;
    int*           actual_src;
    na_tag_t*      actual_tag;
    mona_request_t req;
} irecv_args;

static void irecv_thread(void* x)
{
    irecv_args* args = (irecv_args*)x;
    na_return_t na_ret = mona_comm_recv(
            args->comm,
            args->buf,
            args->size,
            args->src,
            args->tag,
            args->actual_size,
            args->actual_src,
            args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_irecv(
        mona_comm_t comm,
        void* buf,
        na_size_t size,
        int src,
        na_tag_t tag,
        na_size_t* actual_size,
        int* actual_src,
        na_tag_t* actual_tag,
        mona_request_t* req)
{
    NB_OP_INIT(irecv_args);
    args->comm = comm;
    args->buf  = buf;
    args->size = size;
    args->src  = src;
    args->tag  = tag;
    args->actual_size = actual_size;
    args->actual_src = actual_src;
    args->actual_tag = actual_tag;
    NB_OP_POST(irecv_thread);
}

na_return_t mona_comm_sendrecv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        int dest,
        na_tag_t sendtag,
        void *recvbuf,
        na_size_t recvsize,
        int source,
        na_tag_t recvtag,
        na_size_t* actual_recvsize,
        int* actual_recv_src,
        na_tag_t* actual_recv_tag)
{
    mona_request_t sendreq;
    na_return_t na_ret;

    na_ret = mona_comm_isend(comm, sendbuf, sendsize, dest, sendtag, &sendreq);
    if(na_ret != NA_SUCCESS)
        return na_ret;

    na_ret = mona_comm_recv(comm, recvbuf, recvsize, source, recvtag,
            actual_recvsize, actual_recv_src, actual_recv_tag);
    if(na_ret != NA_SUCCESS) {
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
    size = (int)comm->size;
    rank = (int)comm->rank;
    na_return_t na_ret = NA_SUCCESS;

    if(size == 1)
        return na_ret;

    mask = 0x1;
    while(mask < size) {
        dst = (rank + mask) % size;
        src = (rank - mask + size) % size;
        na_ret = mona_comm_sendrecv(
                comm,
                NULL, 0, dst, tag,
                NULL, 0, src, tag,
                NULL, NULL, NULL);
        if(na_ret) break;
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
    ibarrier_args* args = (ibarrier_args*)x;
    na_return_t na_ret = mona_comm_barrier(
            args->comm,
            args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_ibarrier(
        mona_comm_t comm,
        na_tag_t tag,
        mona_request_t* req)
{
    NB_OP_INIT(ibarrier_args);
    args->comm = comm;
    args->tag = tag;
    NB_OP_POST(ibarrier_thread);
}

// -----------------------------------------------------------------------
// Broadcast
// -----------------------------------------------------------------------

na_return_t mona_comm_bcast(
        mona_comm_t comm,
        void *buf,
        na_size_t size,
        int root,
        na_tag_t tag)
{
    // TODO Mpich provides 2 other implementations:
    // - scatter recursive doubling allgather
    // - scatter ring allgather
    // These could be added as alternatives

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
                    na_ret = mona_comm_recv(comm, buf, size, src, tag, NULL, NULL, NULL);
                    if(na_ret != NA_SUCCESS) goto fn_exit;
                }
                if(r == relative_rank) {
                    dst = (relative_dst + root) % comm_size;
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
    ibcast_args* args = (ibcast_args*)x;
    na_return_t na_ret = mona_comm_bcast(
            args->comm,
            args->buf,
            args->size,
            args->root,
            args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_ibcast(
        mona_comm_t comm,
        void *buf,
        na_size_t size,
        int root,
        na_tag_t tag,
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

na_return_t mona_comm_gather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root, 
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_igather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root, 
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// Gatherv
// -----------------------------------------------------------------------

na_return_t mona_comm_gatherv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        const na_size_t* recvsizes,
        const na_size_t* displ,
        int root,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_igatherv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        const na_size_t* recvsizes,
        const na_size_t* displ,
        int root,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// Scatter
// -----------------------------------------------------------------------

na_return_t mona_comm_scatter(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_iscatter(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// Scatterv
// -----------------------------------------------------------------------

na_return_t mona_comm_scatterv(
        mona_comm_t comm,
        const void *sendbuf,
        const na_size_t *sendsizes,
        const na_size_t *displs,
        void *recvbuf,
        na_size_t recvsize,
        int root,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_iscatterv(
        mona_comm_t comm,
        const void *sendbuf,
        const na_size_t *sendsizes,
        const na_size_t *displs,
        void *recvbuf,
        na_size_t recvsize,
        int root,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// AllGather
// -----------------------------------------------------------------------

na_return_t mona_comm_allgather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_iallgather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// Reduce
// -----------------------------------------------------------------------

na_return_t mona_comm_reduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t size,
        mona_op_t op,
        void* uargs,
        int root,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_ireduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t size,
        mona_op_t op,
        void* uargs,
        int root,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// AllReduce
// -----------------------------------------------------------------------

na_return_t mona_comm_allreduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t size,
        mona_op_t op,
        void* uargs,
        int root,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_iallreduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t size,
        mona_op_t op,
        void* uargs,
        int root,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}

// -----------------------------------------------------------------------
// AllToAll
// -----------------------------------------------------------------------

na_return_t mona_comm_alltoall(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        na_size_t recvsize,
        na_tag_t tag)
{
    // TODO
}

na_return_t mona_comm_ialltoall(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        na_size_t recvsize,
        na_tag_t tag,
        mona_request_t* req)
{
    // TODO
}
