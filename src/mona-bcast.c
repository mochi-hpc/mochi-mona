/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

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

    comm_size = comm->all.size;
    rank      = comm->all.rank;

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
