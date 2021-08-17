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

typedef struct mona_team {
    na_size_t  size;
    na_size_t  rank;
    na_addr_t* addrs;
} mona_team_t;

typedef struct mona_comm {
    mona_instance_t mona;
    mona_team_t     all;
    mona_team_t     leaders;
    mona_team_t     local;
    na_bool_t       is_leader;
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

#define MONA_COMM_SEND(__ret__, __comm__, __team__, \
                       __buf__, __size__, __dest__, __tag__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_usend((__comm__)->mona, (__buf__), (__size__), \
                             (__team__)->addrs[__dest__], 0, (__tag__)); \
    } else { \
        __ret__ = mona_send((__comm__)->mona, (__buf__), (__size__), \
                            (__team__)->addrs[__dest__], 0, (__tag__)); \
    } \
    } while(0)

#define MONA_COMM_RECV(__ret__, __comm__, __team__, \
                       __buf__, __size__, __src__, \
                       __tag__, __actual_size__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_urecv((__comm__)->mona, (__buf__), (__size__), \
                             (__team__)->addrs[__src__], (__tag__), \
                             (__actual_size__), NULL, NULL); \
    } else { \
        __ret__ = mona_recv((__comm__)->mona, (__buf__), (__size__), \
                            (__team__)->addrs[__src__], (__tag__), (__actual_size__)); \
    } \
    } while(0)

#define MONA_COMM_SEND_MEM(__ret__, __comm__, __team__, \
                           __mem__, __size__, __offset__, \
                           __dest__, __tag__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_usend_mem((__comm__)->mona, (__mem__), (__size__), (__offset__), \
                                 (__team__)->addrs[__dest__], 0, (__tag__)); \
    } else { \
        __ret__ = mona_send_mem((__comm__)->mona, (__mem__), (__size__), (__offset__), \
                                (__team__)->addrs[__dest__], 0, (__tag__)); \
    } \
    } while(0)

#define MONA_COMM_RECV_MEM(__ret__, __comm__, __team__, \
                           __mem__, __size__, __offset__, \
                           __src__, __tag__, __actual_size__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_urecv_mem((__comm__)->mona, (__mem__), (__size__), \
                                 (__offset__), (__team__)->addrs[__src__], \
                                 (__tag__), (__actual_size__), NULL, NULL); \
    } else { \
        __ret__ = mona_recv_mem((__comm__)->mona, (__mem__), (__size__), \
                                (__offset__), (__team__)->addrs[__src__], \
                                (__tag__), (__actual_size__)); \
    } \
    } while(0)

#define MONA_COMM_ISEND(__ret__, __comm__, __team__, \
                        __buf__, __size__, __dest__, \
                        __tag__, __req__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_uisend((__comm__)->mona, (__buf__), (__size__), \
                              (__team__)->addrs[__dest__], \
                              0, (__tag__), (__req__)); \
    } else { \
        __ret__ = mona_isend((__comm__)->mona, (__buf__), (__size__), \
                             (__team__)->addrs[__dest__], 0, (__tag__), \
                             (__req__)); \
    } \
    } while(0)

#define MONA_COMM_IRECV(__ret__, __comm__, __team__, \
                        __buf__, __size__, __src__, \
                        __tag__, __actual_size__, __req__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_uirecv((__comm__)->mona, (__buf__), (__size__), \
                              (__team__)->addrs[__src__], (__tag__), \
                              (__actual_size__), NULL, NULL, (__req__)); \
    } else { \
        __ret__ = mona_irecv((__comm__)->mona, (__buf__), (__size__), \
                             (__team__)->addrs[__src__], (__tag__), \
                             (__actual_size__), (__req__)); \
    } \
    } while(0)

#define MONA_COMM_ISEND_MEM(__ret__, __comm__, __team__, \
                            __mem__, __size__, __offset__, \
                            __dest__, __tag__, __req__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_uisend_mem((__comm__)->mona, (__mem__), (__size__), (__offset__), \
                                  (__team__)->addrs[__dest__], 0, (__tag__), (__req__)); \
    } else { \
        __ret__ = mona_isend_mem((__comm__)->mona, (__mem__), (__size__), (__offset__), \
                                 (__team__)->addrs[__dest__], 0, (__tag__), (__req__)); \
    } \
    } while(0)

#define MONA_COMM_IRECV_MEM(__ret__, __comm__, __team__, \
                            __mem__, __size__, __offset__, \
                            __src__, __tag__, __actual_size__, __req__) \
    do { \
    if((__comm__)->use_unexpected_msg) { \
        __ret__ = mona_uirecv_mem((__comm__)->mona, (__mem__), (__size__), \
                                  (__offset__), (__team__)->addrs[__src__], \
                                  (__tag__), (__actual_size__), NULL, NULL, \
                                  (__req__)); \
    } else { \
        __ret__ = mona_irecv_mem((__comm__)->mona, (__mem__), (__size__), \
                                 (__offset__), (__team__)->addrs[__src__], \
                                 (__tag__), (__actual_size__), (__req__)); \
    } \
    } while(0)
#endif
