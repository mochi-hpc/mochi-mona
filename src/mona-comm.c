/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

static na_return_t setup_teams(mona_comm_t comm)
{
    na_return_t na_ret      = NA_SUCCESS;
    char**      known_nodes = calloc(sizeof(char*), comm->all.size);

    comm->leaders.size  = 0;
    comm->leaders.rank  = 0;
    comm->leaders.addrs = calloc(sizeof(mona_addr_t), comm->all.size);
    comm->local.size    = 0;
    comm->local.rank    = 0;
    comm->local.addrs   = calloc(sizeof(mona_addr_t), comm->all.size);
    comm->leader_rank   = 0;
    comm->use_unexpected_msg = true;
    comm->hints.reduce_radix = 2;

    char   addr_str[256];
    char   self_addr_str[256];
    size_t addr_str_size = 256;

    na_ret = mona_addr_to_string(comm->mona, self_addr_str, &addr_str_size,
                                 comm->all.addrs[comm->all.rank]);
    if (na_ret != NA_SUCCESS) goto error;
    char* column = strrchr(self_addr_str, ':');
    if (column) *column = '\0';

    for (size_t i = 0; i < comm->all.size; i++) {

        addr_str_size = 256;
        na_ret = mona_addr_to_string(comm->mona, addr_str, &addr_str_size,
                                     comm->all.addrs[i]);
        if (na_ret != NA_SUCCESS) goto error;
        column = strrchr(addr_str, ':');
        if (column) *column = '\0';

        bool is_known = false;
        for (size_t j = 0; j < comm->leaders.size; j++) {
            if (strcmp(addr_str, known_nodes[comm->leaders.size - 1]) == 0) {
                is_known = true;
                break;
            }
        }

        if (!is_known) {
            known_nodes[comm->leaders.size]         = strdup(addr_str);
            comm->leaders.addrs[comm->leaders.size] = comm->all.addrs[i];
            if (i == comm->all.rank) {
                comm->leaders.rank = comm->leaders.size;
            }
            comm->leaders.size += 1;
        }

        if (strcmp(self_addr_str, addr_str) == 0) {
            comm->local.addrs[comm->local.size] = comm->all.addrs[i];
            if (i == comm->all.rank) { comm->local.rank = comm->local.size; }
            if (!is_known) { comm->leader_rank = i; }
            comm->local.size += 1;
        }
    }

finish:
    comm->leaders.addrs
        = realloc(comm->leaders.addrs, sizeof(mona_addr_t) * comm->leaders.size);
    comm->local.addrs
        = realloc(comm->local.addrs, sizeof(mona_addr_t) * comm->local.size);
    free(known_nodes);
    return na_ret;

error:
    free(comm->leaders.addrs);
    free(comm->local.addrs);
    goto finish;
}

na_return_t mona_comm_create(mona_instance_t  mona,
                             size_t           count,
                             const mona_addr_t* peers,
                             mona_comm_t*     comm)
{
    na_return_t na_ret;
    unsigned    i = 0, j = 0, k = 0;
    if (count == 0) { return NA_INVALID_ARG; }
    mona_comm_t tmp = calloc(1, sizeof(mona_comm));
    if (!tmp) return NA_NOMEM;
    tmp->mona      = mona;
    tmp->all.size  = count;
    tmp->all.rank  = count;
    tmp->all.addrs = calloc(sizeof(mona_addr_t), count);
    if (!tmp->all.addrs) {
        na_ret = NA_NOMEM;
        goto error;
    }
    // copy array of addresses and find rank of self
    for (i = 0; i < count; i++) {
        na_ret = mona_addr_dup(mona, peers[i], tmp->all.addrs + i);
        if (na_ret != NA_SUCCESS) goto error;
        if (mona_addr_is_self(mona, peers[i])) {
            if (tmp->all.rank == count)
                tmp->all.rank = i;
            else {
                na_ret = NA_INVALID_ARG;
                i += 1;
                goto error;
            }
        }
    }
    if (tmp->all.rank == count) {
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

    na_ret = setup_teams(tmp);
    if (na_ret != NA_SUCCESS) goto error;

    *comm = tmp;

finish:
    return na_ret;

error:
    for (j = 0; j < i; j++) { mona_addr_free(mona, tmp->all.addrs[j]); }
    free(tmp->all.addrs);
    free(tmp);
    goto finish;
}

na_return_t mona_comm_set_use_unexpected_msg(mona_comm_t comm, bool flag)
{
    comm->use_unexpected_msg = flag;
    return NA_SUCCESS;
}

na_return_t mona_comm_free(mona_comm_t comm)
{
    unsigned    i;
    na_return_t na_ret = NA_SUCCESS;
    for (i = 0; i < comm->all.size; i++) {
        na_ret = mona_addr_free(comm->mona, comm->all.addrs[i]);
        if (na_ret != NA_SUCCESS) return na_ret;
    }
    free(comm->all.addrs);
    free(comm->leaders.addrs);
    free(comm->local.addrs);
    free(comm);
    return na_ret;
}

na_return_t mona_comm_size(mona_comm_t comm, int* size)
{
    *size = comm->all.size;
    return NA_SUCCESS;
}

na_return_t mona_comm_rank(mona_comm_t comm, int* rank)
{
    *rank = comm->all.rank;
    return NA_SUCCESS;
}

na_return_t
mona_comm_addr(mona_comm_t comm, int rank, mona_addr_t* addr, bool copy)
{
    if (rank < 0 || (unsigned)rank >= comm->all.size) return NA_INVALID_ARG;
    if (copy) {
        return mona_addr_dup(comm->mona, comm->all.addrs[rank], addr);
    } else {
        *addr = comm->all.addrs[rank];
    }
    return NA_SUCCESS;
}

na_return_t mona_comm_dup(mona_comm_t comm, mona_comm_t* new_comm)
{
    return mona_comm_create(comm->mona, comm->all.size, comm->all.addrs,
                            new_comm);
}

na_return_t mona_comm_subset(mona_comm_t  comm,
                             const int*   ranks,
                             size_t       size,
                             mona_comm_t* new_comm)
{
    if (size > comm->all.size) return NA_INVALID_ARG;
    mona_addr_t* addrs = alloca(size * sizeof(*addrs));
    unsigned   i;
    for (i = 0; i < size; i++) { addrs[i] = comm->all.addrs[ranks[i]]; }
    return mona_comm_create(comm->mona, size, addrs, new_comm);
}

// -----------------------------------------------------------------------
// Send/Recv
// -----------------------------------------------------------------------

na_return_t mona_comm_send(
    mona_comm_t comm, const void* buf, size_t size, int dest, na_tag_t tag)
{
    if (dest < 0 || (unsigned)dest >= comm->all.size) return NA_INVALID_ARG;
    if (comm->use_unexpected_msg) {
        return mona_usend(comm->mona, buf, size, comm->all.addrs[dest], 0, tag);
    } else {
        return mona_send(comm->mona, buf, size, comm->all.addrs[dest], 0, tag);
    }
}

na_return_t mona_comm_isend(mona_comm_t     comm,
                            const void*     buf,
                            size_t          size,
                            int             dest,
                            na_tag_t        tag,
                            mona_request_t* req)
{
    if (dest < 0 || (unsigned)dest >= comm->all.size) return NA_INVALID_ARG;
    if (comm->use_unexpected_msg) {
        return mona_uisend(comm->mona, buf, size, comm->all.addrs[dest], 0, tag,
                          req);
    } else {
        return mona_isend(comm->mona, buf, size, comm->all.addrs[dest], 0, tag,
                          req);
    }
}

na_return_t mona_comm_recv(mona_comm_t comm,
                           void*       buf,
                           size_t      size,
                           int         src,
                           na_tag_t    tag,
                           size_t*     actual_size,
                           int*        actual_src,
                           na_tag_t*   actual_tag)
{
    if (src < MONA_ANY_SOURCE || src >= (int)comm->all.size) return NA_INVALID_ARG;
    na_return_t na_ret;
    if (comm->use_unexpected_msg) {
        mona_addr_t actual_addr = MONA_ADDR_NULL;
        mona_addr_t* actual_addr_ptr = (src == MONA_ANY_SOURCE && actual_src) ? &actual_addr : NULL;
        mona_addr_t source_addr = (src == MONA_ANY_SOURCE) ? MONA_ANY_ADDR : comm->all.addrs[src];

        na_ret = mona_urecv(comm->mona, buf, size, source_addr, tag,
                            actual_size, actual_addr_ptr, actual_tag);
        if(na_ret != NA_SUCCESS) return na_ret;
        if(actual_addr_ptr) {
            *actual_src = MONA_ANY_SOURCE;
            for(int i=0; i < (int)comm->all.size; i++) {
                if(mona_addr_cmp(comm->mona, comm->all.addrs[i], actual_addr)) {
                    *actual_src = i;
                    break;
                }
            }
            mona_addr_free(comm->mona, actual_addr);
        }
    } else {
        if(tag == MONA_ANY_TAG || src == MONA_ANY_SOURCE)
            return NA_PROTOCOL_ERROR;
        na_ret = mona_recv(comm->mona, buf, size, comm->all.addrs[src], tag,
                           actual_size);
    }
    return na_ret;
}

typedef struct irecv_args {
    mona_comm_t    comm;
    void*          buf;
    size_t         size;
    int            src;
    na_tag_t       tag;
    size_t*        actual_size;
    int*           actual_src;
    na_tag_t*      actual_tag;
    mona_request_t req;
} irecv_args;

static void irecv_thread(void* x)
{
    irecv_args* args = (irecv_args*)x;
    na_return_t na_ret
        = mona_comm_recv(args->comm, args->buf, args->size, args->src,
                         args->tag, args->actual_size, args->actual_src,
                         args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_comm_irecv(mona_comm_t     comm,
                            void*           buf,
                            size_t          size,
                            int             src,
                            na_tag_t        tag,
                            size_t*         actual_size,
                            int*            actual_src,
                            na_tag_t*       actual_tag,
                            mona_request_t* req)
{
    NB_OP_INIT(irecv_args);
    args->comm        = comm;
    args->buf         = buf;
    args->size        = size;
    args->src         = src;
    args->tag         = tag;
    args->actual_size = actual_size;
    args->actual_src  = actual_src;
    args->actual_tag  = actual_tag;
    NB_OP_POST(irecv_thread);
}

na_return_t mona_comm_probe(mona_comm_t comm,
                            int         src,
                            na_tag_t    tag,
                            size_t*     actual_size,
                            int*        actual_src,
                            na_tag_t*   actual_tag)
{
    if (src < MONA_ANY_SOURCE || src >= (int)comm->all.size) return NA_INVALID_ARG;
    na_return_t na_ret;
    if (comm->use_unexpected_msg) {

        mona_addr_t actual_addr = MONA_ADDR_NULL;
        mona_addr_t* actual_addr_ptr = (src == MONA_ANY_SOURCE && actual_src) ? &actual_addr : NULL;
        mona_addr_t source_addr = (src == MONA_ANY_SOURCE) ? MONA_ANY_ADDR : comm->all.addrs[src];

        na_ret = mona_uprobe(comm->mona, source_addr, tag,
                             actual_size, actual_addr_ptr, actual_tag);
        if(na_ret != NA_SUCCESS) return na_ret;
        if(actual_addr_ptr) {
            *actual_src = MONA_ANY_SOURCE;
            for(int i=0; i < (int)comm->all.size; i++) {
                if(mona_addr_cmp(comm->mona, comm->all.addrs[i], actual_addr)) {
                    *actual_src = i;
                    break;
                }
            }
            mona_addr_free(comm->mona, actual_addr);
        }
    } else {
        return NA_PROTOCOL_ERROR;
    }
    return na_ret;
}

na_return_t mona_comm_iprobe(mona_comm_t comm,
                             int         src,
                             na_tag_t    tag,
                             int*        flag,
                             size_t*     actual_size,
                             int*        actual_src,
                             na_tag_t*   actual_tag)
{
    if (src < MONA_ANY_SOURCE || src >= (int)comm->all.size) return NA_INVALID_ARG;
    na_return_t na_ret;
    if (comm->use_unexpected_msg) {

        mona_addr_t actual_addr = MONA_ADDR_NULL;
        mona_addr_t* actual_addr_ptr = (src == MONA_ANY_SOURCE && actual_src) ? &actual_addr : NULL;
        mona_addr_t source_addr = (src == MONA_ANY_SOURCE) ? MONA_ANY_ADDR : comm->all.addrs[src];

        int uflag = 0;
        na_ret = mona_uiprobe(comm->mona, source_addr, tag, &uflag,
                              actual_size, actual_addr_ptr, actual_tag);
        if(na_ret != NA_SUCCESS) return na_ret;
        if(flag) *flag = uflag;
        if(!uflag) return NA_SUCCESS;

        if(actual_addr_ptr) {
            *actual_src = MONA_ANY_SOURCE;
            for(int i=0; i < (int)comm->all.size; i++) {
                if(mona_addr_cmp(comm->mona, comm->all.addrs[i], actual_addr)) {
                    *actual_src = i;
                    break;
                }
            }
            mona_addr_free(comm->mona, actual_addr);
        }
    } else {
        return NA_PROTOCOL_ERROR;
    }
    return na_ret;
}
