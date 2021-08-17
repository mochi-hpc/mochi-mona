/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include "mona-callback.h"
#include <string.h>

// ------------------------------------------------------------------------------------
// Mona high-level send/recv logic using expected messages
// ------------------------------------------------------------------------------------

na_return_t mona_send(mona_instance_t mona,
                      const void*     buf,
                      na_size_t       buf_size,
                      na_addr_t       dest,
                      na_uint8_t      dest_id,
                      na_tag_t        tag)
{
    return mona_send_nc(mona, 1, &buf, &buf_size, dest, dest_id, tag);
}

struct isend_args {
    mona_instance_t mona;
    const void*     buf;
    na_size_t       buf_size;
    na_addr_t       dest;
    na_uint8_t      dest_id;
    na_tag_t        tag;
    mona_request_t  req;
};

static void isend_thread(void* x)
{
    struct isend_args* args  = (struct isend_args*)x;
    na_return_t        na_ret = mona_send(args->mona, args->buf, args->buf_size,
                                   args->dest, args->dest_id, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_isend(mona_instance_t mona,
                       const void*     buf,
                       na_size_t       buf_size,
                       na_addr_t       dest,
                       na_uint8_t      dest_id,
                       na_tag_t        tag,
                       mona_request_t* req)
{
    struct isend_args* args = (struct isend_args*)malloc(sizeof(*args));
    args->mona              = mona;
    args->buf               = buf;
    args->buf_size          = buf_size;
    args->dest              = dest;
    args->dest_id           = dest_id;
    args->tag               = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, isend_thread, args,
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

na_return_t mona_send_nc(mona_instance_t    mona,
                         na_size_t          count,
                         const void* const* buffers,
                         const na_size_t*   buf_sizes,
                         na_addr_t          dest,
                         na_uint8_t         dest_id,
                         na_tag_t           tag)
{
    na_return_t     na_ret      = NA_SUCCESS;
    na_mem_handle_t mem_handle  = NA_MEM_HANDLE_NULL;
    na_size_t       header_size = mona_msg_get_expected_header_size(mona) + 1 + sizeof(na_size_t);
    na_size_t       msg_size    = header_size;
    na_size_t       data_size   = 0;
    cached_msg_t    msg         = get_msg_from_cache(mona, NA_TRUE);
    unsigned        i;

    for (i = 0; i < count; i++) { data_size += buf_sizes[i]; }
    msg_size += data_size;

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
    na_size_t rdma_threshold = MIN(mona_msg_get_max_expected_size(mona) - header_size,
                                   mona->hints.rdma_threshold);
    if (data_size <= rdma_threshold) {

        na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
        if (na_ret != NA_SUCCESS) goto finish;

        char* p = msg->buffer + mona_msg_get_expected_header_size(mona);
        *p      = HL_MSG_SMALL;
        p += 1;
        memcpy(p, &data_size, sizeof(data_size));
        p += sizeof(na_size_t);

        for (i = 0; i < count; i++) {
            memcpy(p, buffers[i], buf_sizes[i]);
            p += buf_sizes[i];
        }

        na_ret = mona_msg_send_expected(mona, msg->buffer, msg_size,
                                        msg->plugin_data, dest, dest_id, tag*2);
        if(na_ret != NA_SUCCESS) goto finish;

    } else {
        // Expose user memory for RDMA
        if (count == 1) {
            na_ret
                = mona_mem_handle_create(mona, (void*)buffers[0], buf_sizes[0],
                                         NA_MEM_READ_ONLY, &mem_handle);
        } else {
            struct na_segment* segments = alloca(sizeof(*segments) * count);
            for (i = 0; i < count; i++) {
                segments[i].base = (na_ptr_t)buffers[i];
                segments[i].len  = buf_sizes[i];
            }
            na_ret = mona_mem_handle_create_segments(
                mona, segments, count, NA_MEM_READ_ONLY, &mem_handle);
        }
        if (na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_mem_register(mona, mem_handle);
        if (na_ret != NA_SUCCESS) goto finish;

        na_ret
            = mona_send_mem(mona, mem_handle, data_size, 0, dest, dest_id, tag);
        mona_mem_deregister(mona, mem_handle);
    }

finish:
    if (mem_handle != NA_MEM_HANDLE_NULL) {
        mona_mem_handle_free(mona, mem_handle);
    }
    return_msg_to_cache(mona, msg, NA_TRUE);
    return na_ret;
}

struct isend_nc_args {
    mona_instance_t    mona;
    na_size_t          count;
    const void* const* buffers;
    const na_size_t*   buf_sizes;
    na_addr_t          dest;
    na_uint8_t         dest_id;
    na_tag_t           tag;
    mona_request_t     req;
};

static void isend_nc_thread(void* x)
{
    struct isend_nc_args* args = (struct isend_nc_args*)x;
    na_return_t           na_ret
        = mona_send_nc(args->mona, args->count, args->buffers, args->buf_sizes,
                       args->dest, args->dest_id, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_isend_nc(mona_instance_t    mona,
                          na_size_t          count,
                          const void* const* buffers,
                          const na_size_t*   buf_sizes,
                          na_addr_t          dest,
                          na_uint8_t         dest_id,
                          na_tag_t           tag,
                          mona_request_t*    req)
{
    struct isend_nc_args* args = (struct isend_nc_args*)malloc(sizeof(*args));
    args->mona                 = mona;
    args->count                = count;
    args->buffers              = buffers;
    args->buf_sizes            = buf_sizes;
    args->dest                 = dest;
    args->dest_id              = dest_id;
    args->tag                  = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, isend_nc_thread, args,
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

na_return_t mona_send_mem(mona_instance_t mona,
                          na_mem_handle_t mem,
                          na_size_t       size,
                          na_size_t       offset,
                          na_addr_t       dest,
                          na_uint8_t      dest_id,
                          na_tag_t        tag)
{
    na_return_t  na_ret   = NA_SUCCESS;
    na_size_t    msg_size = 0;
    cached_msg_t msg      = get_msg_from_cache(mona, NA_TRUE);

    na_size_t mem_handle_size = mona_mem_handle_get_serialize_size(mona, mem);

    // Initialize message to send
    msg_size = mona_msg_get_expected_header_size(mona) // NA header
             + 1                 // type of message (HL_MSG_*)
             + sizeof(na_size_t) // size of the serialized handle
             + sizeof(na_size_t) // size of the data
             + sizeof(na_size_t) // offset in handle
             + mem_handle_size;

    na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
    if (na_ret != NA_SUCCESS) goto finish;

    // Fill in the message
    char* p = msg->buffer + mona_msg_get_expected_header_size(mona);
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
    cached_msg_t   ack_msg      = get_msg_from_cache(mona, NA_TRUE);
    na_size_t      ack_msg_size = mona_msg_get_expected_header_size(mona) + 1;
    mona_request_t ack_req      = MONA_REQUEST_NULL;
    cached_op_id_t ack_cache_id = get_op_id_from_cache(mona);
    na_op_id_t*    ack_op_id    = ack_cache_id->op_id;

    // Issue non-blocking receive for ACK
    na_ret = mona_msg_irecv_expected(mona, ack_msg->buffer, ack_msg_size,
                                     ack_msg->plugin_data, dest, dest_id, 2*tag+1,
                                     ack_op_id, &ack_req);
    if (na_ret != NA_SUCCESS) {
        return_op_id_to_cache(mona, ack_cache_id);
        goto finish;
    }

    // Issue send of message with mem handle
    na_ret = mona_msg_send_expected(mona, msg->buffer, msg_size,
                                    msg->plugin_data, dest, dest_id, 2*tag);
    if (na_ret != NA_SUCCESS) {
        mona_cancel(mona, ack_op_id);
        return_op_id_to_cache(mona, ack_cache_id);
        goto finish;
    }

    // Wait for acknowledgement
    na_ret = mona_wait(ack_req);
    return_op_id_to_cache(mona, ack_cache_id);

finish:
    return_msg_to_cache(mona, msg, NA_TRUE);
    return na_ret;
}

struct isend_mem_args {
    mona_instance_t mona;
    na_mem_handle_t mem;
    na_size_t       size;
    na_size_t       offset;
    na_addr_t       dest;
    na_uint8_t      dest_id;
    na_tag_t        tag;
    mona_request_t  req;
};

static void isend_mem_thread(void* x)
{
    struct isend_mem_args* args = (struct isend_mem_args*)x;
    na_return_t            na_ret
        = mona_send_mem(args->mona, args->mem, args->size, args->offset,
                        args->dest, args->dest_id, args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_isend_mem(mona_instance_t mona,
                           na_mem_handle_t mem,
                           na_size_t       size,
                           na_size_t       offset,
                           na_addr_t       dest,
                           na_uint8_t      dest_id,
                           na_tag_t        tag,
                           mona_request_t* req)
{
    struct isend_mem_args* args = (struct isend_mem_args*)malloc(sizeof(*args));
    args->mona                  = mona;
    args->mem                   = mem;
    args->size                  = size;
    args->offset                = offset;
    args->dest                  = dest;
    args->dest_id               = dest_id;
    args->tag                   = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, isend_mem_thread, args,
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

na_return_t mona_recv(mona_instance_t mona,
                      void*           buf,
                      na_size_t       size,
                      na_addr_t       src,
                      na_tag_t        tag,
                      na_size_t*      actual_size)
{
    return mona_recv_nc(mona, 1, &buf, &size, src, tag, actual_size);
}

struct irecv_args {
    mona_instance_t mona;
    void*           buf;
    na_size_t       size;
    na_addr_t       src;
    na_tag_t        tag;
    na_size_t*      actual_size;
    mona_request_t  req;
};

static void irecv_thread(void* x)
{
    struct irecv_args* args = (struct irecv_args*)x;
    na_return_t        na_ret
        = mona_recv(args->mona, args->buf, args->size, args->src, args->tag,
                    args->actual_size);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_irecv(mona_instance_t mona,
                       void*           buf,
                       na_size_t       size,
                       na_addr_t       src,
                       na_tag_t        tag,
                       na_size_t*      actual_size,
                       mona_request_t* req)
{
    struct irecv_args* args = (struct irecv_args*)malloc(sizeof(*args));
    args->mona              = mona;
    args->buf               = buf;
    args->size              = size;
    args->src               = src;
    args->actual_size       = actual_size;
    args->tag               = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, irecv_thread, args,
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

na_return_t mona_recv_nc(mona_instance_t  mona,
                         na_size_t        count,
                         void**           buffers,
                         const na_size_t* buf_sizes,
                         na_addr_t        src,
                         na_tag_t         tag,
                         na_size_t*       actual_size)
{

    na_return_t     na_ret        = NA_SUCCESS;
    na_mem_handle_t mem_handle    = NA_MEM_HANDLE_NULL;
    na_mem_handle_t remote_handle = NA_MEM_HANDLE_NULL;
    na_size_t       header_size   = mona_msg_get_expected_header_size(mona);
    cached_msg_t    msg           = NULL;
    na_size_t       msg_size      = mona_msg_get_max_expected_size(mona);
    na_size_t       data_size     = 0; // data size actually received
    na_size_t       max_data_size = 0; // data size requested by arguments
    unsigned        i;

    for (i = 0; i < count; i++) { max_data_size += buf_sizes[i]; }

    msg = get_msg_from_cache(mona, NA_TRUE);
    na_ret = mona_msg_recv_expected(mona, msg->buffer, msg_size,
                                    msg->plugin_data, src, 0, 2*tag);
    if (na_ret != NA_SUCCESS)
        goto finish;

    char* p = msg->buffer + header_size;

    if (*p == HL_MSG_SMALL) { // small message, embedded data

        p += 1;
        memcpy(&data_size, p, sizeof(data_size));
        p += sizeof(data_size);

        na_size_t remaining_size = data_size;
        na_size_t size_copied = 0;
        for (i = 0; i < count && remaining_size != 0; i++) {
            na_size_t s
                = remaining_size < buf_sizes[i] ? remaining_size : buf_sizes[i];
            memcpy(buffers[i], p, s);
            remaining_size -= s;
            size_copied += s;
        }
        if(actual_size) *actual_size = size_copied;

    } else if (*p == HL_MSG_LARGE) { // large message, using RDMA transfer

        p += 1;
        na_size_t mem_handle_size;
        na_size_t remote_offset;
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
                segments[i].base = (na_ptr_t)buffers[i];
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
                              data_size, src, 0);
            if (na_ret != NA_SUCCESS) goto finish;
        }
        if(actual_size) *actual_size = data_size;

        // Send ACK
        na_size_t msg_size        = header_size + 1;
        msg->buffer[msg_size - 1] = 0;
        na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
        if (na_ret != NA_SUCCESS) goto finish;

        // XXX how do we support a source id different from 0 ?
        na_ret
            = mona_msg_send_expected(mona, msg->buffer, msg_size,
                                     msg->plugin_data, src, 0, 2*tag+1);
        if (na_ret != NA_SUCCESS) goto finish;
    }

finish:
    if (mem_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, mem_handle);
    if (remote_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, remote_handle);
    return_msg_to_cache(mona, msg, NA_TRUE);
    return na_ret;
}

struct irecv_nc_args {
    mona_instance_t  mona;
    na_size_t        count;
    void**           buffers;
    const na_size_t* buf_sizes;
    na_addr_t        src;
    na_tag_t         tag;
    na_size_t*       actual_size;
    mona_request_t   req;
};

static void irecv_nc_thread(void* x)
{
    struct irecv_nc_args* args  = (struct irecv_nc_args*)x;
    na_return_t           na_ret = mona_recv_nc(
        args->mona, args->count, args->buffers, args->buf_sizes, args->src,
        args->tag, args->actual_size);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_irecv_nc(mona_instance_t  mona,
                          na_size_t        count,
                          void**           buffers,
                          const na_size_t* buf_sizes,
                          na_addr_t        src,
                          na_tag_t         tag,
                          na_size_t*       actual_size,
                          mona_request_t*  req)
{
    struct irecv_nc_args* args = (struct irecv_nc_args*)malloc(sizeof(*args));
    args->mona                 = mona;
    args->count                = count;
    args->buffers              = buffers;
    args->buf_sizes            = buf_sizes;
    args->src                  = src;
    args->actual_size          = actual_size;
    args->tag                  = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, irecv_nc_thread, args,
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

na_return_t mona_recv_mem(mona_instance_t mona,
                          na_mem_handle_t mem,
                          na_size_t       size,
                          na_size_t       offset,
                          na_addr_t       src,
                          na_tag_t        tag,
                          na_size_t*      actual_size)
{
    na_return_t     na_ret        = NA_SUCCESS;
    na_mem_handle_t remote_handle = NA_MEM_HANDLE_NULL;
    na_size_t       header_size   = mona_msg_get_expected_header_size(mona);
    cached_msg_t    msg           = NULL;
    na_size_t       recv_size     = mona_msg_get_max_expected_size(mona);

    msg = get_msg_from_cache(mona, NA_TRUE);
    na_ret = mona_msg_recv_expected(mona, msg->buffer, recv_size,
                                    msg->plugin_data, src, 0, 2*tag);
    if (na_ret != NA_SUCCESS) goto finish;

    char* p = msg->buffer + header_size + 1;

    na_size_t mem_handle_size;
    na_size_t remote_data_size;
    na_size_t remote_offset;
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
            = mona_get(mona, mem, offset, remote_handle, remote_offset, recv_size, src, 0);
        if (na_ret != NA_SUCCESS) goto finish;
    }

    // Send ACK
    na_size_t msg_size        = header_size + 1;
    msg->buffer[msg_size - 1] = 0;
    na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
    if (na_ret != NA_SUCCESS) goto finish;

    // XXX how do we support a source id different from 0 ?
    na_ret = mona_msg_send_expected(mona, msg->buffer, msg_size,
                                    msg->plugin_data, src, 0, 2*tag+1);
    if (na_ret != NA_SUCCESS) goto finish;

    if (actual_size) *actual_size = size;

finish:
    if (remote_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, remote_handle);
    return_msg_to_cache(mona, msg, NA_TRUE);
    return na_ret;
}

struct irecv_mem_args {
    mona_instance_t mona;
    na_mem_handle_t mem;
    na_size_t       size;
    na_size_t       offset;
    na_addr_t       src;
    na_tag_t        tag;
    na_size_t*      actual_size;
    mona_request_t  req;
};

static void irecv_mem_thread(void* x)
{
    struct irecv_mem_args* args   = (struct irecv_mem_args*)x;
    na_return_t            na_ret = mona_recv_mem(
        args->mona, args->mem, args->size, args->offset, args->src, args->tag,
        args->actual_size);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_irecv_mem(mona_instance_t mona,
                           na_mem_handle_t mem,
                           na_size_t       size,
                           na_size_t       offset,
                           na_addr_t       src,
                           na_tag_t        tag,
                           na_size_t*      actual_size,
                           mona_request_t* req)
{
    struct irecv_mem_args* args = (struct irecv_mem_args*)malloc(sizeof(*args));
    args->mona                  = mona;
    args->mem                   = mem;
    args->size                  = size;
    args->offset                = offset;
    args->src                   = src;
    args->actual_size           = actual_size;
    args->tag                   = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req              = tmp_req;

    int ret = ABT_thread_create(mona->progress_pool, irecv_mem_thread, args,
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
