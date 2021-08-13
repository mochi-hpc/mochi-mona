/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_COMM_H
#define __MONA_COMM_H

#include "mona-types.h"
#include "mona-coll.h"
#include <string.h>

typedef struct mona_comm {
    mona_instance_t mona;
    na_size_t       size;
    na_size_t       rank;
    na_addr_t*      addrs;
    na_bool_t       use_unexpected_msg;
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

#endif
