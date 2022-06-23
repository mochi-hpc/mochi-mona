/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_TYPES_H
#define __MONA_TYPES_H

#include "mona.h"
#include <stdlib.h>

typedef struct cached_op_id* cached_op_id_t;
typedef struct cached_op_id {
    na_op_id_t*    op_id;
    cached_op_id_t next;
} cached_op_id;

// cached_msg and pending_msg are used in the message queue
// used by unexpected send/recv

typedef struct cached_msg* cached_msg_t;
typedef struct cached_msg {
    char* buffer;
    void* plugin_data;
    void* next; // may point to a cached_msg or to a pending_msg depending on
                // context
} cached_msg;

typedef struct pending_msg* pending_msg_t;
typedef struct pending_msg {
    cached_msg_t cached_msg;
    size_t       recv_size;
    na_addr_t    recv_addr;
    na_tag_t     recv_tag;
} pending_msg;

typedef struct mona_instance {
    // NA structures
    na_class_t*   na_class;
    na_context_t* na_context;
    // ABT structures
    ABT_pool    progress_pool;
    ABT_xstream progress_xstream;
    ABT_thread  progress_thread;
    // ownership information
    bool owns_progress_pool;
    bool owns_progress_xstream;
    bool owns_na_class_and_context;
    // finalization
    bool finalize_flag;
    // operation id cache
    cached_op_id_t op_id_cache;
    ABT_mutex      op_id_cache_mtx;
    // request cache
    mona_request_t req_cache;
    ABT_mutex      req_cache_mtx;
    // unexpected send/recv data
    struct {
        // message cache for high-level functions
        cached_msg_t msg_cache;
        ABT_mutex    msg_cache_mtx;
        // pending messages received in high-level mona_urecv
        pending_msg_t pending_msg_oldest; // head of the queue
        pending_msg_t pending_msg_newest; // last of the queue
        ABT_mutex     pending_msg_mtx;
        ABT_cond      pending_msg_cv;
        bool          pending_msg_queue_active; // a thread is queuing messages
        // pending request and data from a probe for new messages
        na_addr_t      prob_addr;
        size_t         prob_size;
        na_tag_t       prob_tag;
        mona_request_t prob_req;
        cached_msg_t   prob_msg;
        cached_op_id_t prob_id;
    } unexpected;
    // expected send/recv data
    struct {
        // message cache for high-level functions
        cached_msg_t msg_cache;
        ABT_mutex    msg_cache_mtx;
    } expected;
    // hints
    struct {
        size_t rdma_threshold;
    } hints;
} mona_instance;

typedef struct mona_request {
    ABT_eventual    eventual;
    mona_instance_t mona;
    na_addr_t*      source_addr;
    na_tag_t*       tag;
    size_t*         size;
    mona_request_t  next; // for the request cache
} mona_request;

#define MONA_REQUEST_INITIALIZER                        \
    {                                                   \
        ABT_EVENTUAL_NULL, NULL, NULL, NULL, NULL, NULL \
    }

typedef enum hl_msg_type
{
    HL_MSG_SMALL,
    HL_MSG_LARGE
} hl_msg_type;

// Operation ID cache -------------------------------------------------------

static inline cached_op_id_t get_op_id_from_cache(mona_instance_t mona)
{
    cached_op_id_t id;
    ABT_mutex_lock(mona->op_id_cache_mtx);
    if (mona->op_id_cache) {
        id                = mona->op_id_cache;
        mona->op_id_cache = id->next;
        id->next          = NULL;
    } else {
        na_op_id_t* op_id = NA_Op_create(mona->na_class);
        id                = (cached_op_id_t)calloc(1, sizeof(*id));
        id->op_id         = op_id;
    }
    ABT_mutex_unlock(mona->op_id_cache_mtx);
    return id;
}

static inline void return_op_id_to_cache(mona_instance_t mona,
                                         cached_op_id_t  id)
{
    ABT_mutex_lock(mona->op_id_cache_mtx);
    cached_op_id_t head = mona->op_id_cache;
    id->next            = head;
    mona->op_id_cache   = id;
    ABT_mutex_unlock(mona->op_id_cache_mtx);
}

static inline void clear_op_id_cache(mona_instance_t mona)
{
    ABT_mutex_lock(mona->op_id_cache_mtx);
    cached_op_id_t cached_op = mona->op_id_cache;
    mona->op_id_cache        = NULL;
    while (cached_op) {
        cached_op_id_t tmp = cached_op->next;
        NA_Op_destroy(mona->na_class, cached_op->op_id);
        free(cached_op);
        cached_op = tmp;
    }
    ABT_mutex_unlock(mona->op_id_cache_mtx);
}

// Request cache ----------------------------------------------------------

static inline mona_request_t get_req_from_cache(mona_instance_t mona)
{
    mona_request_t req;
    ABT_mutex_lock(mona->req_cache_mtx);
    if (mona->req_cache) {
        req             = mona->req_cache;
        mona->req_cache = req->next;
        req->next       = NULL;
        ABT_eventual_reset(req->eventual);
    } else {
        req = (mona_request_t)calloc(1, sizeof(*req));
        ABT_eventual_create(sizeof(na_return_t), &(req->eventual));
    }
    ABT_mutex_unlock(mona->req_cache_mtx);
    return req;
}

static inline void return_req_to_cache(mona_instance_t mona, mona_request_t req)
{
    ABT_mutex_lock(mona->req_cache_mtx);
    mona_request_t head = mona->req_cache;
    req->next           = head;
    mona->req_cache     = req;
    ABT_mutex_unlock(mona->req_cache_mtx);
}

static inline void clear_req_cache(mona_instance_t mona)
{
    ABT_mutex_lock(mona->req_cache_mtx);
    mona_request_t cached_req = mona->req_cache;
    mona->req_cache           = NULL;
    while (cached_req) {
        mona_request_t tmp = cached_req->next;
        ABT_eventual_free(&(cached_req->eventual));
        free(cached_req);
        cached_req = tmp;
    }
    ABT_mutex_unlock(mona->req_cache_mtx);
}

// Message cache -------------------------------------------------------

static inline cached_msg_t get_msg_from_cache(mona_instance_t mona,
                                              bool            expected)
{
    cached_msg_t msg;
    if (expected) {
        ABT_mutex_lock(mona->expected.msg_cache_mtx);
        if (mona->expected.msg_cache) {
            msg                      = mona->expected.msg_cache;
            mona->expected.msg_cache = msg->next;
            msg->next                = NULL;
        } else {
            msg         = (cached_msg_t)calloc(1, sizeof(*msg));
            msg->buffer = (char*)mona_msg_buf_alloc(
                mona, mona_msg_get_max_expected_size(mona),
                &(msg->plugin_data));
        }
        ABT_mutex_unlock(mona->expected.msg_cache_mtx);
    } else {
        ABT_mutex_lock(mona->unexpected.msg_cache_mtx);
        if (mona->unexpected.msg_cache) {
            msg                        = mona->unexpected.msg_cache;
            mona->unexpected.msg_cache = msg->next;
            msg->next                  = NULL;
        } else {
            msg         = (cached_msg_t)calloc(1, sizeof(*msg));
            msg->buffer = (char*)mona_msg_buf_alloc(
                mona, mona_msg_get_max_unexpected_size(mona),
                &(msg->plugin_data));
        }
        ABT_mutex_unlock(mona->unexpected.msg_cache_mtx);
    }
    return msg;
}

static inline void
return_msg_to_cache(mona_instance_t mona, cached_msg_t msg, bool expected)
{
    if (expected) {
        ABT_mutex_lock(mona->expected.msg_cache_mtx);
        cached_msg_t head        = mona->expected.msg_cache;
        msg->next                = head;
        mona->expected.msg_cache = msg;
        ABT_mutex_unlock(mona->expected.msg_cache_mtx);
    } else {
        ABT_mutex_lock(mona->unexpected.msg_cache_mtx);
        cached_msg_t head          = mona->unexpected.msg_cache;
        msg->next                  = head;
        mona->unexpected.msg_cache = msg;
        ABT_mutex_unlock(mona->unexpected.msg_cache_mtx);
    }
}

static inline void clear_msg_cache(mona_instance_t mona)
{
    ABT_mutex_lock(mona->unexpected.msg_cache_mtx);
    cached_msg_t msg           = mona->unexpected.msg_cache;
    mona->unexpected.msg_cache = NULL;
    while (msg) {
        cached_msg_t tmp = msg->next;
        mona_msg_buf_free(mona, msg->buffer, msg->plugin_data);
        free(msg);
        msg = tmp;
    }
    ABT_mutex_unlock(mona->unexpected.msg_cache_mtx);
    ABT_mutex_lock(mona->expected.msg_cache_mtx);
    msg                      = mona->expected.msg_cache;
    mona->expected.msg_cache = NULL;
    while (msg) {
        cached_msg_t tmp = msg->next;
        mona_msg_buf_free(mona, msg->buffer, msg->plugin_data);
        free(msg);
        msg = tmp;
    }
    ABT_mutex_unlock(mona->expected.msg_cache_mtx);
}

// Wait --------------------------------------------------------------

static inline na_return_t mona_wait_internal(mona_request_t req)
{
    na_return_t* waited_na_ret = NULL;
    na_return_t  na_ret        = NA_SUCCESS;

    if (req == MONA_REQUEST_NULL) return NA_INVALID_ARG;

    ABT_eventual_wait(req->eventual, (void**)&waited_na_ret);
    na_ret = *waited_na_ret;
    ABT_eventual_free(&(req->eventual));

    return na_ret;
}

#endif
