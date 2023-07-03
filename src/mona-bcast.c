/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

// TODO Mpich provides 2 other implementations:
// - scatter recursive doubling allgather
// - scatter ring allgather
// These could be added as alternatives

static na_return_t mona_comm_bcast_binomial(mona_comm_t  comm,
                                            void*        buf,
                                            size_t       size,
                                            int          root,
                                            na_tag_t     tag,
                                            mona_team_t* team)
{

    int         rank, comm_size, src, dst;
    int         relative_rank, mask;
    na_return_t na_ret;

    comm_size = team->size;
    rank      = team->rank;

    // If there is only one process, return
    if (comm_size == 1) return NA_SUCCESS;

    relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    mask = 0x1;
    while (mask < comm_size) {
        if (relative_rank & mask) {
            src = rank - mask;
            if (src < 0) src += comm_size;
            MONA_COMM_RECV(na_ret, comm, team, buf, size, src, tag, NULL);
            if (na_ret != NA_SUCCESS) { return na_ret; }
            break;
        }
        mask <<= 1;
    }
    mask >>= 1;

    while (mask > 0) {
        if (relative_rank + mask < comm_size) {
            dst = rank + mask;
            if (dst >= comm_size) dst -= comm_size;
            MONA_COMM_SEND(na_ret, comm, team, buf, size, dst, tag);
            if (na_ret != NA_SUCCESS) { return na_ret; }
        }
        mask >>= 1;
    }
    return na_ret;
}

na_return_t mona_comm_bcast(
    mona_comm_t comm, void* buf, size_t size, int root, na_tag_t tag)
{
    return mona_comm_bcast_binomial(comm, buf, size, root, tag, &comm->all);
}

typedef struct ibcast_args {
    mona_comm_t    comm;
    void*          buf;
    size_t         size;
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
                             size_t          size,
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
