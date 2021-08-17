/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

#if 0
static na_return_t setup_teams(mona_comm_t comm) {
    na_return_t na_ret = NA_SUCCESS;
    char** known_nodes = calloc(sizeof(char*), comm->all.size);
    na_size_t* team_leaders = calloc(sizeof(na_size_t), comm->all.size);
    na_size_t num_nodes = 0;
    char addr_str[256];
    char self_addr_str = 256;
    na_size_t addr_str_size;

    comm->teams.count      = 0;
    comm->teams.id         = 0;
    comm->teams.leaders    = NULL;
    comm->teams.is_leader  = NA_FALSE;
    comm->teams.team_ranks = NULL;
    comm->teams.team_size  = 0;

    na_ret = mona_addr_to_string(comm->mona, addr_str, &addr_str_size, comm->all.addrs[comm->all.rank]);
    if(na_ret != NA_SUCCESS) goto error;
    char* column = strrchr(addr_str, ':');
    if(column) *column = '\0';

    for(na_size_t i = 0; i < comm->all.size; i++) {
        addr_str_size = 256;
        na_ret = mona_addr_to_string(comm->mona, addr_str, &addr_str_size, comm->all.addrs[i]);
        if(na_ret != NA_SUCCESS) goto error;
        // remove port number from address
        column = strrchr(addr_str, ':');
        if(column) *column = '\0';
        // check if the node address is know
        na_bool_t is_known = NA_FALSE;
        for(na_size_t j = 0; j < num_nodes; j++) {
            if(strcmp(addr_str, known_nodes[num_nodes-1]) == 0) {
                is_known = NA_TRUE;
                break;
            }
        }
        // if it's a new node
        if(!is_known) {
            // add to known nodes
            known_nodes[num_nodes] = strdup(addr_str);
            team_leaders[num_nodes] = i;
            num_nodes += 1;
            if(i == comm->all.rank) {
                comm->teams.is_leader = NA_TRUE;
            }
        }
        // if it's in the same node as this process
        if(strcmp(self_addr_str, addr_str) == 0) {
            // add to the list of ranks
            comm->teams.team_size += 1;
            comm->teams.team_ranks = realloc(comm->teams.team_ranks,
                    comm->teams.team_size*sizeof(na_size_t));
            comm->teams.team_ranks[comm->teams.team_size-1] = i;
        }
    }

    comm->teams.leaders = realloc(team_leaders, num_nodes*sizeof(na_size_t));
    comm->teams.count = num_nodes;

finish:
    for(na_size_t i = 0; i < comm->all.size; i++) {
        if(known_nodes[i])
            free(known_nodes[i]);
        else
            break;
    }
    free(known_nodes);
    return na_ret;
error:
    free(comm->teams.team_ranks);
    free(team_leaders);
    goto finish;
}
#endif

static na_return_t setup_teams(mona_comm_t comm) {
    na_return_t na_ret = NA_SUCCESS;
    char** known_nodes = calloc(sizeof(char*), comm->all.size);

    comm->leaders.size = 0;
    comm->leaders.rank = 0;
    comm->leaders.addrs = calloc(sizeof(na_addr_t), comm->all.size);
    comm->local.size = 0;
    comm->local.rank = 0;
    comm->local.addrs = calloc(sizeof(na_addr_t), comm->all.size);
    comm->is_leader = NA_FALSE;

    char addr_str[256];
    char self_addr_str[256];
    na_size_t addr_str_size = 256;

    na_ret = mona_addr_to_string(comm->mona, addr_str, &addr_str_size, comm->all.addrs[comm->all.rank]);
    if(na_ret != NA_SUCCESS) goto error;
    char* column = strrchr(addr_str, ':');
    if(column) *column = '\0';

    for(na_size_t i=0; i < comm->all.size; i++) {

        addr_str_size = 256;
        na_ret = mona_addr_to_string(comm->mona, addr_str, &addr_str_size, comm->all.addrs[i]);
        if(na_ret != NA_SUCCESS) goto error;
        column = strrchr(addr_str, ':');
        if(column) *column = '\0';

        na_bool_t is_known = NA_FALSE;
        for(na_size_t j = 0; j < comm->leaders.size; j++) {
            if(strcmp(addr_str, known_nodes[comm->leaders.size-1]) == 0) {
                is_known = NA_TRUE;
                break;
            }
        }

        if(!is_known) {
            known_nodes[comm->leaders.size] = strdup(addr_str);
            comm->leaders.addrs[comm->leaders.size] = comm->all.addrs[i];
            if(i == comm->all.rank) {
                comm->is_leader = NA_TRUE;
                comm->leaders.rank = comm->leaders.size;
            }
            comm->leaders.size += 1;
        }

        if(strcmp(self_addr_str, addr_str) == 0) {
            comm->local.addrs[comm->local.size] = comm->all.addrs[i];
            if(i == comm->all.rank) {
                comm->local.rank = comm->local.size;
            }
            comm->local.size += 1;
        }
    }

finish:
    comm->leaders.addrs = realloc(comm->leaders.addrs, sizeof(na_addr_t)*comm->leaders.size);
    comm->local.addrs = realloc(comm->local.addrs, sizeof(na_addr_t)*comm->local.size);
    free(known_nodes);
    return na_ret;

error:
    free(comm->leaders.addrs);
    free(comm->local.addrs);
    goto finish;
}

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
    tmp->all.size  = count;
    tmp->all.rank  = count;
    tmp->all.addrs = calloc(sizeof(na_addr_t), count);
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
    if(na_ret != NA_SUCCESS)
        goto error;

    *comm = tmp;

finish:
    return na_ret;

error:
    for (j = 0; j < i; j++) { mona_addr_free(mona, tmp->all.addrs[i]); }
    free(tmp->all.addrs);
    free(tmp);
    goto finish;
}

na_return_t mona_comm_set_use_unexpected_msg(mona_comm_t comm, na_bool_t flag)
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
mona_comm_addr(mona_comm_t comm, int rank, na_addr_t* addr, na_bool_t copy)
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
    return mona_comm_create(comm->mona, comm->all.size, comm->all.addrs, new_comm);
}

na_return_t mona_comm_subset(mona_comm_t  comm,
                             const int*   ranks,
                             na_size_t    size,
                             mona_comm_t* new_comm)
{
    if (size > comm->all.size) return NA_INVALID_ARG;
    na_addr_t* addrs = alloca(size * sizeof(*addrs));
    unsigned   i;
    for (i = 0; i < size; i++) { addrs[i] = comm->all.addrs[ranks[i]]; }
    return mona_comm_create(comm->mona, size, addrs, new_comm);
}

// -----------------------------------------------------------------------
// Send/Recv
// -----------------------------------------------------------------------

na_return_t mona_comm_send(
    mona_comm_t comm, const void* buf, na_size_t size, int dest, na_tag_t tag)
{
    if (dest < 0 || (unsigned)dest >= comm->all.size) return NA_INVALID_ARG;
    if(comm->use_unexpected_msg) {
        return mona_usend(comm->mona, buf, size, comm->all.addrs[dest], 0, tag);
    } else {
        return mona_send(comm->mona, buf, size, comm->all.addrs[dest], 0, tag);
    }
}

na_return_t mona_comm_isend(mona_comm_t     comm,
                            const void*     buf,
                            na_size_t       size,
                            int             dest,
                            na_tag_t        tag,
                            mona_request_t* req)
{
    if (dest < 0 || (unsigned)dest >= comm->all.size) return NA_INVALID_ARG;
    return mona_isend(comm->mona, buf, size, comm->all.addrs[dest], 0, tag, req);
}

na_return_t mona_comm_recv(mona_comm_t comm,
                           void*       buf,
                           na_size_t   size,
                           int         src,
                           na_tag_t    tag,
                           na_size_t*  actual_size)
{
    if (src < 0 || (unsigned)src >= comm->all.size) return NA_INVALID_ARG;
    na_return_t na_ret;
    if(comm->use_unexpected_msg) {
        na_ret = mona_urecv(comm->mona, buf, size, comm->all.addrs[src], tag, actual_size, NULL, NULL);
    } else {
        na_ret = mona_recv(comm->mona, buf, size, comm->all.addrs[src], tag, actual_size);
    }
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

