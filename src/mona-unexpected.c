/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include "mona-callback.h"
#include <string.h>

// ------------------------------------------------------------------------------------
// Mona high-level send/recv logic using unexpected messages
// ------------------------------------------------------------------------------------

na_return_t mona_usend(mona_instance_t mona,
                       const void*     buf,
                       size_t          buf_size,
                       na_addr_t       dest,
                       uint8_t         dest_id,
                       na_tag_t        tag)
{
    return mona_usend_nc(mona, 1, &buf, &buf_size, dest, dest_id, tag);
}

struct uisend_args {
    mona_instance_t mona;
    const void*     buf;
    size_t          buf_size;
    na_addr_t       dest;
    uint8_t         dest_id;
    na_tag_t        tag;
    mona_request_t  req;
};

static void uisend_thread(void* x)
{
    struct uisend_args* args = (struct uisend_args*)x;
    na_return_t na_ret       = mona_usend(args->mona, args->buf, args->buf_size,
                                    args->dest, args->dest_id, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_uisend(mona_instance_t mona,
                        const void*     buf,
                        size_t          buf_size,
                        na_addr_t       dest,
                        uint8_t         dest_id,
                        na_tag_t        tag,
                        mona_request_t* req)
{
    struct uisend_args* args = (struct uisend_args*)malloc(sizeof(*args));
    args->mona               = mona;
    args->buf                = buf;
    args->buf_size           = buf_size;
    args->dest               = dest;
    args->dest_id            = dest_id;
    args->tag                = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, uisend_thread, args,
                                ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_usend_nc(mona_instance_t    mona,
                          size_t             count,
                          const void* const* buffers,
                          const size_t*      buf_sizes,
                          na_addr_t          dest,
                          uint8_t            dest_id,
                          na_tag_t           tag)
{
    na_return_t     na_ret      = NA_SUCCESS;
    na_mem_handle_t mem_handle  = NA_MEM_HANDLE_NULL;
    size_t          header_size = mona_msg_get_unexpected_header_size(mona) + 1;
    size_t          msg_size    = header_size;
    size_t          data_size   = 0;
    cached_msg_t    msg         = get_msg_from_cache(mona, false);
    unsigned        i;

    for (i = 0; i < count; i++) { data_size += buf_sizes[i]; }
    msg_size += data_size;

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
    size_t rdma_threshold = MIN(mona_msg_get_max_unexpected_size(mona),
                                mona->hints.rdma_threshold);

    if (msg_size <= rdma_threshold) {

        na_ret = mona_msg_init_unexpected(mona, msg->buffer, msg_size);
        if (na_ret != NA_SUCCESS) goto finish;

        char* p = msg->buffer + mona_msg_get_unexpected_header_size(mona);
        *p      = HL_MSG_SMALL;
        p += 1;

        for (i = 0; i < count; i++) {
            memcpy(p, buffers[i], buf_sizes[i]);
            p += buf_sizes[i];
        }

        na_ret = mona_msg_send_unexpected(mona, msg->buffer, msg_size,
                                          msg->plugin_data, dest, dest_id, tag);

    } else {

        // Expose user memory for RDMA
        if (count == 1) {
            na_ret
                = mona_mem_handle_create(mona, (void*)buffers[0], buf_sizes[0],
                                         NA_MEM_READ_ONLY, &mem_handle);
        } else {
            struct na_segment* segments = alloca(sizeof(*segments) * count);
            for (i = 0; i < count; i++) {
                segments[i].base = (void*)buffers[i];
                segments[i].len  = buf_sizes[i];
            }
            na_ret = mona_mem_handle_create_segments(
                mona, segments, count, NA_MEM_READ_ONLY, &mem_handle);
        }
        if (na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_mem_register(mona, mem_handle);
        if (na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_usend_mem(mona, mem_handle, data_size, 0, dest, dest_id,
                                tag);
        mona_mem_deregister(mona, mem_handle);
    }

finish:
    if (mem_handle != NA_MEM_HANDLE_NULL) {
        mona_mem_handle_free(mona, mem_handle);
    }
    return_msg_to_cache(mona, msg, false);
    return na_ret;
}

struct uisend_nc_args {
    mona_instance_t    mona;
    size_t             count;
    const void* const* buffers;
    const size_t*      buf_sizes;
    na_addr_t          dest;
    uint8_t            dest_id;
    na_tag_t           tag;
    mona_request_t     req;
};

static void uisend_nc_thread(void* x)
{
    struct uisend_nc_args* args = (struct uisend_nc_args*)x;
    na_return_t            na_ret
        = mona_usend_nc(args->mona, args->count, args->buffers, args->buf_sizes,
                        args->dest, args->dest_id, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_uisend_nc(mona_instance_t    mona,
                           size_t             count,
                           const void* const* buffers,
                           const size_t*      buf_sizes,
                           na_addr_t          dest,
                           uint8_t            dest_id,
                           na_tag_t           tag,
                           mona_request_t*    req)
{
    struct uisend_nc_args* args = (struct uisend_nc_args*)malloc(sizeof(*args));
    args->mona                  = mona;
    args->count                 = count;
    args->buffers               = buffers;
    args->buf_sizes             = buf_sizes;
    args->dest                  = dest;
    args->dest_id               = dest_id;
    args->tag                   = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, uisend_nc_thread, args,
                                ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_usend_mem(mona_instance_t mona,
                           na_mem_handle_t mem,
                           size_t          size,
                           size_t          offset,
                           na_addr_t       dest,
                           uint8_t         dest_id,
                           na_tag_t        tag)
{
    na_return_t  na_ret   = NA_SUCCESS;
    size_t       msg_size = 0;
    cached_msg_t msg      = get_msg_from_cache(mona, false);

    size_t mem_handle_size = mona_mem_handle_get_serialize_size(mona, mem);

    // Initialize message to send
    msg_size = mona_msg_get_unexpected_header_size(mona) // NA header
             + 1              // type of message (HL_MSG_*)
             + sizeof(size_t) // size of the serialized handle
             + sizeof(size_t) // size of the data
             + sizeof(size_t) // offset in handle
             + mem_handle_size;

    na_ret = mona_msg_init_unexpected(mona, msg->buffer, msg_size);
    if (na_ret != NA_SUCCESS) goto finish;

    // Fill in the message
    char* p = msg->buffer + mona_msg_get_unexpected_header_size(mona);
    *p      = HL_MSG_LARGE;
    p += 1;
    // size of serialized handle
    memcpy(p, &mem_handle_size, sizeof(mem_handle_size));
    p += sizeof(mem_handle_size);
    // size of the data
    memcpy(p, &size, sizeof(size));
    p += sizeof(size);
    // offset in the handle
    memcpy(p, &offset, sizeof(offset));
    p += sizeof(offset);
    // serialized handle
    na_ret = mona_mem_handle_serialize(mona, p, mem_handle_size, mem);
    if (na_ret != NA_SUCCESS) goto finish;

    // Initialize ack message to receive
    cached_msg_t   ack_msg      = get_msg_from_cache(mona, false);
    size_t         ack_msg_size = mona_msg_get_unexpected_header_size(mona) + 1;
    mona_request_t ack_req      = MONA_REQUEST_NULL;
    cached_op_id_t ack_cache_id = get_op_id_from_cache(mona);
    na_op_id_t*    ack_op_id    = ack_cache_id->op_id;

    // Issue non-blocking receive for ACK
    na_ret = mona_msg_irecv_expected(mona, ack_msg->buffer, ack_msg_size,
                                     ack_msg->plugin_data, dest, dest_id, tag,
                                     ack_op_id, &ack_req);
    if (na_ret != NA_SUCCESS) {
        return_op_id_to_cache(mona, ack_cache_id);
        goto finish;
    }

    // Issue send of message with mem handle
    na_ret = mona_msg_send_unexpected(mona, msg->buffer, msg_size,
                                      msg->plugin_data, dest, dest_id, tag);
    if (na_ret != NA_SUCCESS) {
        mona_cancel(mona, ack_op_id);
        return_op_id_to_cache(mona, ack_cache_id);
        goto finish;
    }

    // Wait for acknowledgement
    na_ret = mona_wait(ack_req);
    return_op_id_to_cache(mona, ack_cache_id);

finish:
    return_msg_to_cache(mona, msg, false);
    return na_ret;
}

struct uisend_mem_args {
    mona_instance_t mona;
    na_mem_handle_t mem;
    size_t          size;
    size_t          offset;
    na_addr_t       dest;
    uint8_t         dest_id;
    na_tag_t        tag;
    mona_request_t  req;
};

static void uisend_mem_thread(void* x)
{
    struct uisend_mem_args* args = (struct uisend_mem_args*)x;
    na_return_t             na_ret
        = mona_usend_mem(args->mona, args->mem, args->size, args->offset,
                         args->dest, args->dest_id, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_uisend_mem(mona_instance_t mona,
                            na_mem_handle_t mem,
                            size_t          size,
                            size_t          offset,
                            na_addr_t       dest,
                            uint8_t         dest_id,
                            na_tag_t        tag,
                            mona_request_t* req)
{
    struct uisend_mem_args* args
        = (struct uisend_mem_args*)malloc(sizeof(*args));
    args->mona    = mona;
    args->mem     = mem;
    args->size    = size;
    args->offset  = offset;
    args->dest    = dest;
    args->dest_id = dest_id;
    args->tag     = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, uisend_mem_thread, args,
                                ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

static cached_msg_t wait_for_matching_unexpected_message(mona_instance_t mona,
                                                         na_addr_t       src,
                                                         na_tag_t        tag,
                                                         size_t*    actual_size,
                                                         na_addr_t* actual_src,
                                                         na_tag_t*  actual_tag)
{
    cached_msg_t msg      = NULL; /* result */
    size_t       msg_size = mona_msg_get_max_unexpected_size(mona);
    na_return_t  na_ret   = NA_SUCCESS;

    // lock the queue of pending messages
    ABT_mutex_lock(mona->unexpected.pending_msg_mtx);

    // search in the queue of pending messages for one matching
search_in_queue : {
    pending_msg_t p_msg      = mona->unexpected.pending_msg_oldest;
    pending_msg_t p_prev_msg = NULL;
    while (p_msg) {
        if ((tag == MONA_ANY_TAG || p_msg->recv_tag == tag)
            && (src == MONA_ANY_SOURCE
                || mona_addr_cmp(mona, src, p_msg->recv_addr))) {
            break;
        } else {
            p_prev_msg = p_msg;
            p_msg      = p_msg->cached_msg->next;
        }
    }
    if (p_msg) { // matching message was found
        msg = p_msg->cached_msg;
        // remove it from the queue of pending messages
        if (p_prev_msg) p_prev_msg->cached_msg->next = p_msg->cached_msg->next;
        if (p_msg == mona->unexpected.pending_msg_oldest)
            mona->unexpected.pending_msg_oldest = p_msg->cached_msg->next;
        if (p_msg == mona->unexpected.pending_msg_newest)
            mona->unexpected.pending_msg_newest = p_prev_msg;
        // unlock the queue
        ABT_mutex_unlock(mona->unexpected.pending_msg_mtx);
        // copy size, source, and tag
        if (actual_size) *actual_size = p_msg->recv_size;
        if (actual_src) mona_addr_dup(mona, p_msg->recv_addr, actual_src);
        if (actual_tag) *actual_tag = p_msg->recv_tag;
        // free the pending message object
        mona_addr_free(mona, p_msg->recv_addr);
        free(p_msg);
        // return the message
        return msg;
    }
}
    // here the matching message wasn't found in the queue
    {
        // if another thread is actively issuing unexpected recv, wait for the
        // queue to update
        if (mona->unexpected.pending_msg_queue_active) {
            ABT_cond_wait(mona->unexpected.pending_msg_cv,
                          mona->unexpected.pending_msg_mtx);
            if (mona->unexpected.pending_msg_queue_active) goto search_in_queue;
        }
    }
    // here no matching message was found and there isn't any other threads
    // updating the queue so this thread will take the responsibility for
    // actively listening for messages
    mona->unexpected.pending_msg_queue_active = true;
    ABT_mutex_unlock(mona->unexpected.pending_msg_mtx);
recv_new_message : {
    size_t    recv_size = 0;
    na_addr_t recv_addr = NA_ADDR_NULL;
    na_tag_t  recv_tag  = 0;
    // get message from cache
    msg = get_msg_from_cache(mona, false);
    // issue unexpected recv
    na_ret = mona_msg_recv_unexpected(mona, msg->buffer, msg_size,
                                      msg->plugin_data, &recv_addr, &recv_tag,
                                      &recv_size);
    if (na_ret != NA_SUCCESS) goto error;
    // check is received message is matching
    if ((tag == MONA_ANY_TAG || recv_tag == tag)
        && (src == MONA_ANY_SOURCE || mona_addr_cmp(mona, src, recv_addr))) {
        // received message matches
        // notify other threads that this thread won't be updating the queue
        // anymore
        ABT_mutex_lock(mona->unexpected.pending_msg_mtx);
        mona->unexpected.pending_msg_queue_active = false;
        ABT_mutex_unlock(mona->unexpected.pending_msg_mtx);
        ABT_cond_broadcast(mona->unexpected.pending_msg_cv);
        // copy size, source, and tag
        if (actual_size) *actual_size = recv_size;
        if (actual_src)
            *actual_src = recv_addr;
        else
            mona_addr_free(mona, recv_addr);
        if (actual_tag) *actual_tag = recv_tag;
        // return the message
        return msg;

    } else {
        // received message doesn't match, create a pending message...
        pending_msg_t p_msg = (pending_msg_t)malloc(sizeof(*p_msg));
        p_msg->cached_msg   = msg;
        p_msg->recv_size    = recv_size;
        p_msg->recv_addr    = recv_addr;
        p_msg->recv_tag     = recv_tag;
        msg->next           = NULL;
        // ... and put it in the queue
        ABT_mutex_lock(mona->unexpected.pending_msg_mtx);
        if (mona->unexpected.pending_msg_oldest == NULL) {
            mona->unexpected.pending_msg_oldest = p_msg;
            mona->unexpected.pending_msg_newest = p_msg;
        } else {
            mona->unexpected.pending_msg_newest->cached_msg->next = p_msg;
            mona->unexpected.pending_msg_newest                   = p_msg;
        }
        // notify other threads that the queue has been updated
        ABT_mutex_unlock(mona->unexpected.pending_msg_mtx);
        ABT_cond_broadcast(mona->unexpected.pending_msg_cv);
        goto recv_new_message;
    }
}
    // error handling
error:
    if (msg) return_msg_to_cache(mona, msg, false);
    ABT_mutex_unlock(mona->unexpected.pending_msg_mtx);
    ABT_cond_broadcast(mona->unexpected.pending_msg_cv);
    return NULL;
}

na_return_t mona_urecv(mona_instance_t mona,
                       void*           buf,
                       size_t          size,
                       na_addr_t       src,
                       na_tag_t        tag,
                       size_t*         actual_size,
                       na_addr_t*      actual_src,
                       na_tag_t*       actual_tag)
{
    return mona_urecv_nc(mona, 1, &buf, &size, src, tag, actual_size,
                         actual_src, actual_tag);
}

struct uirecv_args {
    mona_instance_t mona;
    void*           buf;
    size_t          size;
    na_addr_t       src;
    na_tag_t        tag;
    size_t*         actual_size;
    na_addr_t*      actual_src;
    na_tag_t*       actual_tag;
    mona_request_t  req;
};

static void uirecv_thread(void* x)
{
    struct uirecv_args* args = (struct uirecv_args*)x;
    na_return_t         na_ret
        = mona_urecv(args->mona, args->buf, args->size, args->src, args->tag,
                     args->actual_size, args->actual_src, args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_uirecv(mona_instance_t mona,
                        void*           buf,
                        size_t          size,
                        na_addr_t       src,
                        na_tag_t        tag,
                        size_t*         actual_size,
                        na_addr_t*      actual_src,
                        na_tag_t*       actual_tag,
                        mona_request_t* req)
{
    struct uirecv_args* args = (struct uirecv_args*)malloc(sizeof(*args));
    args->mona               = mona;
    args->buf                = buf;
    args->size               = size;
    args->src                = src;
    args->actual_size        = actual_size;
    args->actual_src         = actual_src;
    args->actual_tag         = actual_tag;
    args->tag                = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, uirecv_thread, args,
                                ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_urecv_nc(mona_instance_t mona,
                          size_t          count,
                          void**          buffers,
                          const size_t*   buf_sizes,
                          na_addr_t       src,
                          na_tag_t        tag,
                          size_t*         actual_size,
                          na_addr_t*      actual_src,
                          na_tag_t*       actual_tag)
{

    na_return_t     na_ret        = NA_SUCCESS;
    na_mem_handle_t mem_handle    = NA_MEM_HANDLE_NULL;
    na_mem_handle_t remote_handle = NA_MEM_HANDLE_NULL;
    size_t          header_size   = mona_msg_get_unexpected_header_size(mona);
    cached_msg_t    msg           = NULL;
    size_t          recv_size     = 0;
    na_addr_t       recv_addr     = NA_ADDR_NULL;
    na_tag_t        recv_tag      = 0;
    size_t          max_data_size = 0;
    unsigned        i;

    for (i = 0; i < count; i++) { max_data_size += buf_sizes[i]; }

    // wait for a matching unexpected message to come around
    msg = wait_for_matching_unexpected_message(mona, src, tag, &recv_size,
                                               &recv_addr, &recv_tag);
    if (!msg) return NA_PROTOCOL_ERROR;

    // At this point, we know msg is the message we are looking for
    // and the attributes are recv_size, recv_tag, and recv_addr

    char* p = msg->buffer + header_size;

    if (*p == HL_MSG_SMALL) { // small message, embedded data

        p += 1;
        recv_size -= header_size + 1;
        size_t remaining_size = recv_size;
        for (i = 0; i < count && remaining_size != 0; i++) {
            size_t s
                = remaining_size < buf_sizes[i] ? remaining_size : buf_sizes[i];
            memcpy(buffers[i], p, s);
            remaining_size -= s;
        }
        recv_size = recv_size < max_data_size ? recv_size : max_data_size;

    } else if (*p == HL_MSG_LARGE) { // large message, using RDMA transfer

        p += 1;
        size_t mem_handle_size;
        size_t data_size;
        size_t remote_offset;
        // read the size of the serialize mem handle
        memcpy(&mem_handle_size, p, sizeof(mem_handle_size));
        p += sizeof(mem_handle_size);
        // read the size of the data associated with the mem handle
        memcpy(&data_size, p, sizeof(data_size));
        p += sizeof(data_size);
        // read the offset
        memcpy(&remote_offset, p, sizeof(remote_offset));
        p += sizeof(remote_offset);

        // expose user memory for RDMA
        if (count == 1) {
            na_ret
                = mona_mem_handle_create(mona, (void*)buffers[0], buf_sizes[0],
                                         NA_MEM_WRITE_ONLY, &mem_handle);
        } else {
            struct na_segment* segments = alloca(sizeof(*segments) * count);
            for (i = 0; i < count; i++) {
                segments[i].base = (void*)buffers[i];
                segments[i].len  = buf_sizes[i];
            }
            na_ret = mona_mem_handle_create_segments(
                mona, segments, count, NA_MEM_WRITE_ONLY, &mem_handle);
        }
        if (na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_mem_register(mona, mem_handle);
        if (na_ret != NA_SUCCESS) goto finish;

        // Deserialize remote memory handle
        na_ret = mona_mem_handle_deserialize(mona, &remote_handle, p,
                                             mem_handle_size);
        if (na_ret != NA_SUCCESS) goto finish;

        // Issue RDMA operation
        // XXX how do we support a source id different from 0 ?
        data_size = data_size < max_data_size ? data_size : max_data_size;
        if (data_size) {
            na_ret = mona_get(mona, mem_handle, 0, remote_handle, remote_offset,
                              data_size, recv_addr, 0);
            if (na_ret != NA_SUCCESS) goto finish;
        }
        recv_size = data_size;

        // Send ACK
        size_t msg_size           = header_size + 1;
        msg->buffer[msg_size - 1] = 0;
        na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
        if (na_ret != NA_SUCCESS) goto finish;

        // XXX how do we support a source id different from 0 ?
        na_ret
            = mona_msg_send_expected(mona, msg->buffer, msg_size,
                                     msg->plugin_data, recv_addr, 0, recv_tag);
        if (na_ret != NA_SUCCESS) goto finish;
    }

    if (actual_size) *actual_size = recv_size;
    if (actual_tag) *actual_tag = recv_tag;
    if (actual_src)
        *actual_src = recv_addr;
    else
        mona_addr_free(mona, recv_addr);
    recv_addr = NA_ADDR_NULL;

finish:
    if (recv_addr != NA_ADDR_NULL) mona_addr_free(mona, recv_addr);
    if (mem_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, mem_handle);
    if (remote_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, remote_handle);
    return_msg_to_cache(mona, msg, false);
    return na_ret;
}

struct uirecv_nc_args {
    mona_instance_t mona;
    size_t          count;
    void**          buffers;
    const size_t*   buf_sizes;
    na_addr_t       src;
    na_tag_t        tag;
    size_t*         actual_size;
    na_addr_t*      actual_src;
    na_tag_t*       actual_tag;
    mona_request_t  req;
};

static void uirecv_nc_thread(void* x)
{
    struct uirecv_nc_args* args   = (struct uirecv_nc_args*)x;
    na_return_t            na_ret = mona_urecv_nc(
        args->mona, args->count, args->buffers, args->buf_sizes, args->src,
        args->tag, args->actual_size, args->actual_src, args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_uirecv_nc(mona_instance_t mona,
                           size_t          count,
                           void**          buffers,
                           const size_t*   buf_sizes,
                           na_addr_t       src,
                           na_tag_t        tag,
                           size_t*         actual_size,
                           na_addr_t*      actual_src,
                           na_tag_t*       actual_tag,
                           mona_request_t* req)
{
    struct uirecv_nc_args* args = (struct uirecv_nc_args*)malloc(sizeof(*args));
    args->mona                  = mona;
    args->count                 = count;
    args->buffers               = buffers;
    args->buf_sizes             = buf_sizes;
    args->src                   = src;
    args->actual_size           = actual_size;
    args->actual_src            = actual_src;
    args->actual_tag            = actual_tag;
    args->tag                   = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, uirecv_nc_thread, args,
                                ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_urecv_mem(mona_instance_t mona,
                           na_mem_handle_t mem,
                           size_t          size,
                           size_t          offset,
                           na_addr_t       src,
                           na_tag_t        tag,
                           size_t*         actual_size,
                           na_addr_t*      actual_src,
                           na_tag_t*       actual_tag)
{

    na_return_t     na_ret        = NA_SUCCESS;
    na_mem_handle_t remote_handle = NA_MEM_HANDLE_NULL;
    size_t          header_size   = mona_msg_get_unexpected_header_size(mona);
    cached_msg_t    msg           = NULL;
    size_t          recv_size     = 0;
    na_addr_t       recv_addr     = NA_ADDR_NULL;
    na_tag_t        recv_tag      = 0;

    // wait for a matching unexpected message to come around
    msg = wait_for_matching_unexpected_message(mona, src, tag, &recv_size,
                                               &recv_addr, &recv_tag);
    if (!msg) return NA_PROTOCOL_ERROR;

    // At this point, we know msg is the message we are looking for
    // and the attributes are recv_size, recv_tag, and recv_addr

    char* p = msg->buffer + header_size + 1;

    size_t mem_handle_size;
    size_t remote_data_size;
    size_t remote_offset;
    // read the size of the serialize mem handle
    memcpy(&mem_handle_size, p, sizeof(mem_handle_size));
    p += sizeof(mem_handle_size);
    // read the size of the data associated with the mem handle
    memcpy(&remote_data_size, p, sizeof(remote_data_size));
    p += sizeof(remote_data_size);
    // read the remote offset
    memcpy(&remote_offset, p, sizeof(remote_offset));
    p += sizeof(remote_offset);

    // Deserialize remote memory handle
    na_ret
        = mona_mem_handle_deserialize(mona, &remote_handle, p, mem_handle_size);
    if (na_ret != NA_SUCCESS) goto finish;

    // Issue RDMA operation
    // XXX how do we support a source id different from 0 ?
    recv_size = remote_data_size < size ? remote_data_size : size;
    if (recv_size) {
        na_ret
            = mona_get(mona, mem, 0, remote_handle, 0, recv_size, recv_addr, 0);
        if (na_ret != NA_SUCCESS) goto finish;
    }

    // Send ACK
    size_t msg_size           = header_size + 1;
    msg->buffer[msg_size - 1] = 0;
    na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
    if (na_ret != NA_SUCCESS) goto finish;

    // XXX how do we support a source id different from 0 ?
    na_ret = mona_msg_send_expected(mona, msg->buffer, msg_size,
                                    msg->plugin_data, recv_addr, 0, recv_tag);
    if (na_ret != NA_SUCCESS) goto finish;

    if (actual_size) *actual_size = recv_size;
    if (actual_tag) *actual_tag = recv_tag;
    if (actual_src)
        *actual_src = recv_addr;
    else
        mona_addr_free(mona, recv_addr);
    recv_addr = NA_ADDR_NULL;

finish:
    if (recv_addr != NA_ADDR_NULL) mona_addr_free(mona, recv_addr);
    if (remote_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, remote_handle);
    return_msg_to_cache(mona, msg, false);
    return na_ret;
}

struct uirecv_mem_args {
    mona_instance_t mona;
    na_mem_handle_t mem;
    size_t          size;
    size_t          offset;
    na_addr_t       src;
    na_tag_t        tag;
    size_t*         actual_size;
    na_addr_t*      actual_src;
    na_tag_t*       actual_tag;
    mona_request_t  req;
};

static void uirecv_mem_thread(void* x)
{
    struct uirecv_mem_args* args   = (struct uirecv_mem_args*)x;
    na_return_t             na_ret = mona_urecv_mem(
        args->mona, args->mem, args->size, args->offset, args->src, args->tag,
        args->actual_size, args->actual_src, args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_uirecv_mem(mona_instance_t mona,
                            na_mem_handle_t mem,
                            size_t          size,
                            size_t          offset,
                            na_addr_t       src,
                            na_tag_t        tag,
                            size_t*         actual_size,
                            na_addr_t*      actual_src,
                            na_tag_t*       actual_tag,
                            mona_request_t* req)
{
    struct uirecv_mem_args* args
        = (struct uirecv_mem_args*)malloc(sizeof(*args));
    args->mona        = mona;
    args->mem         = mem;
    args->size        = size;
    args->offset      = offset;
    args->src         = src;
    args->actual_size = actual_size;
    args->actual_src  = actual_src;
    args->actual_tag  = actual_tag;
    args->tag         = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, uirecv_mem_thread, args,
                                ABT_THREAD_ATTR_NULL, NULL);
    if (ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}
