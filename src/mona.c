/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include <string.h>

// ------------------------------------------------------------------------------------
// Mona progress loop logic
// ------------------------------------------------------------------------------------

static void mona_progress_loop(void* uarg) {
    mona_instance_t mona = (mona_instance_t)uarg;
    na_return_t trigger_ret, na_ret;
    unsigned int actual_count = 0;
    size_t size;

    while(!mona->finalize_flag) {

        do {
            trigger_ret = NA_Trigger(mona->na_context, 0, 1, NULL, &actual_count);
        } while ((trigger_ret == NA_SUCCESS) && actual_count && !mona->finalize_flag);

        ABT_pool_get_size(mona->progress_pool, &size);
        if(size)
            ABT_thread_yield();

        // TODO put a high timeout value to avoid busy-spinning
        // if there is no other ULT in the pool that could run
        na_ret = NA_Progress(mona->na_class, mona->na_context, 0);
        if (na_ret != NA_SUCCESS && na_ret != NA_TIMEOUT) {
            fprintf(stderr, "WARNING: unexpected return value from NA_Progress (%d)\n", na_ret);
        }
    }
}

// ------------------------------------------------------------------------------------
// Mona initialization logic
// ------------------------------------------------------------------------------------

mona_instance_t mona_init(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info)
{
    return mona_init_thread(
            info_string,
            listen,
            na_init_info,
            NA_FALSE);
}

mona_instance_t mona_init_thread(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info,
        na_bool_t use_progress_es)
{
    int ret;
    ABT_xstream xstream = ABT_XSTREAM_NULL;
    ABT_pool progress_pool = ABT_POOL_NULL;
    mona_instance_t mona = MONA_INSTANCE_NULL;

    if(use_progress_es == NA_TRUE) {

        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC, ABT_FALSE, &progress_pool);
        if(ret != ABT_SUCCESS) goto error;

        ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &progress_pool, ABT_SCHED_CONFIG_NULL, &xstream);
        if(ret != ABT_SUCCESS) goto error;

    } else {

        ret = ABT_xstream_self(&xstream);
        if(ret != ABT_SUCCESS) goto error;

        ret = ABT_xstream_get_main_pools(xstream, 1, &progress_pool);
        if(ret != ABT_SUCCESS) goto error;
    }

    mona = mona_init_pool(
            info_string,
            listen,
            na_init_info,
            progress_pool);
    if(!mona) goto error;

    if(use_progress_es == NA_TRUE) {
        mona->owns_progress_pool = NA_TRUE;
        mona->owns_progress_xstream = NA_TRUE;
    }

    mona->progress_xstream = xstream;

finish:
    return mona;

error:
    if(progress_pool != ABT_POOL_NULL && use_progress_es == NA_TRUE)
        ABT_pool_free(&progress_pool);
    if(xstream != ABT_XSTREAM_NULL && use_progress_es == NA_TRUE)
        ABT_xstream_free(&xstream);
    mona = MONA_INSTANCE_NULL;
    goto finish;
}

mona_instance_t mona_init_pool(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info,
        ABT_pool progress_pool)
{
    na_class_t* na_class = NULL;
    na_context_t* na_context = NULL;
    mona_instance_t mona = MONA_INSTANCE_NULL;

    na_class = NA_Initialize_opt(info_string, listen, na_init_info);
    if(!na_class) goto error;

    na_context = NA_Context_create(na_class);
    if(!na_context) goto error;

    mona = mona_init_na_pool(na_class, na_context, progress_pool);
    if(!mona) goto error;

    mona->owns_na_class_and_context = NA_TRUE;

finish:
    return mona;

error:
    if(na_context) NA_Context_destroy(na_class, na_context);
    if(na_class) NA_Finalize(na_class);
    mona = MONA_INSTANCE_NULL;
    goto finish;
}

mona_instance_t mona_init_na_pool(
        na_class_t *na_class,
        na_context_t *na_context,
        ABT_pool progress_pool)
{
    int ret, i;
    mona_instance_t mona = (mona_instance_t)calloc(1, sizeof(*mona));
    if(!mona) return MONA_INSTANCE_NULL;
    mona->na_class         = na_class;
    mona->na_context       = na_context;
    mona->progress_pool    = progress_pool;
    mona->progress_xstream = ABT_XSTREAM_NULL;
    mona->progress_thread  = ABT_THREAD_NULL;
    mona->op_id_cache_mtx  = ABT_MUTEX_NULL;
    mona->req_cache_mtx    = ABT_MUTEX_NULL;
    mona->msg_cache_mtx    = ABT_MUTEX_NULL;
    mona->pending_msg_mtx  = ABT_MUTEX_NULL;
    mona->pending_msg_cv   = ABT_COND_NULL;
    ret = ABT_mutex_create(&(mona->op_id_cache_mtx));
    if(ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->req_cache_mtx));
    if(ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->msg_cache_mtx));
    if(ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->pending_msg_mtx));
    if(ret != ABT_SUCCESS) goto error;
    ret = ABT_cond_create(&(mona->pending_msg_cv));
    if(ret != ABT_SUCCESS) goto error;

    mona->op_id_cache = (cached_op_id_t)calloc(1, sizeof(*(mona->op_id_cache)));
    mona->op_id_cache->op_id = NA_Op_create(na_class);

    cached_op_id_t current = mona->op_id_cache;
    for(i=0; i < 15; i++) {
        current->next = (cached_op_id_t)calloc(1, sizeof(*current));
        current = current->next;
        current->op_id = NA_Op_create(na_class);
    }

    ret = ABT_thread_create(mona->progress_pool, mona_progress_loop, 
            (void*)mona, ABT_THREAD_ATTR_NULL, &(mona->progress_thread));
    if(ret != ABT_SUCCESS) goto error;

finish:
    return mona;

error:
    if(mona->op_id_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->op_id_cache_mtx));
    if(mona->req_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->req_cache_mtx));
    if(mona->msg_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->msg_cache_mtx));
    if(mona->pending_msg_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->pending_msg_mtx));
    if(mona->pending_msg_cv != ABT_COND_NULL)
        ABT_cond_free(&(mona->pending_msg_cv));
    free(mona);
    mona = MONA_INSTANCE_NULL;
    goto finish;
}

na_return_t mona_finalize(mona_instance_t mona)
{
    mona->finalize_flag = NA_TRUE;
    ABT_thread_join(mona->progress_thread);

    if(mona->owns_progress_xstream) {
        ABT_xstream_join(mona->progress_xstream);
        ABT_xstream_free(&(mona->progress_xstream));
    }
    if(mona->owns_progress_pool)
        ABT_pool_free(&(mona->progress_pool));

    clear_op_id_cache(mona);
    ABT_mutex_free(&(mona->op_id_cache_mtx));

    clear_req_cache(mona);
    ABT_mutex_free(&(mona->req_cache_mtx));

    clear_msg_cache(mona);
    ABT_mutex_free(&(mona->msg_cache_mtx));

    ABT_mutex_free(&(mona->pending_msg_mtx));
    ABT_cond_free(&(mona->pending_msg_cv));

    if(mona->owns_na_class_and_context) {
        NA_Context_destroy(
                mona->na_class,
                mona->na_context);
        NA_Finalize(mona->na_class);
    }
    free(mona);

    return NA_SUCCESS;
}

// ------------------------------------------------------------------------------------
// Mona info access logic
// ------------------------------------------------------------------------------------

const char* mona_get_class_name(mona_instance_t mona)
{
    return NA_Get_class_name(mona->na_class);
}

const char* mona_get_class_protocol(mona_instance_t mona)
{
    return NA_Get_class_protocol(mona->na_class);
}

na_bool_t mona_is_listening(mona_instance_t mona)
{
    return NA_Is_listening(mona->na_class);
}

// ------------------------------------------------------------------------------------
// Mona addresses logic
// ------------------------------------------------------------------------------------

na_return_t mona_addr_lookup(
        mona_instance_t mona,
        const char *name,
        na_addr_t *addr)
{
    return NA_Addr_lookup(mona->na_class, name, addr);
}

na_return_t mona_addr_free(
        mona_instance_t mona,
        na_addr_t addr)
{
    return NA_Addr_free(mona->na_class, addr);
}

na_return_t mona_addr_set_remove(
        mona_instance_t mona,
        na_addr_t addr)
{
    return NA_Addr_set_remove(mona->na_class, addr);
}

na_return_t mona_addr_self(
        mona_instance_t mona,
        na_addr_t* addr)
{
    return NA_Addr_self(mona->na_class, addr);
}

na_return_t mona_addr_dup(
        mona_instance_t mona,
        na_addr_t addr,
        na_addr_t* dup_addr)
{
    return NA_Addr_dup(mona->na_class, addr, dup_addr);
}

na_bool_t mona_addr_cmp(
        mona_instance_t mona,
        na_addr_t addr1,
        na_addr_t addr2)
{
    if(addr1 == NA_ADDR_NULL && addr2 == NA_ADDR_NULL)
        return NA_TRUE;
    if(addr1 == NA_ADDR_NULL || addr2 == NA_ADDR_NULL)
        return NA_FALSE;
    char str1[256], str2[256];
    na_size_t s = 256;
    mona_addr_to_string(mona, str1, &s, addr1);
    s = 256;
    mona_addr_to_string(mona, str2, &s, addr2);
    return strcmp(str1, str2) == 0 ? NA_TRUE : NA_FALSE;
}

na_bool_t mona_addr_is_self(
        mona_instance_t mona,
        na_addr_t addr)
{
    return NA_Addr_is_self(mona->na_class, addr);
}

na_return_t mona_addr_to_string(
        mona_instance_t mona,
        char *buf,
        na_size_t *buf_size,
        na_addr_t addr)
{
    return NA_Addr_to_string(mona->na_class, buf, buf_size, addr);
}

na_size_t mona_addr_get_serialize_size(
        mona_instance_t mona,
        na_addr_t addr)
{
    return NA_Addr_get_serialize_size(mona->na_class, addr);
}

na_return_t mona_addr_serialize(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        na_addr_t addr)
{
    return NA_Addr_serialize(mona->na_class, buf, buf_size, addr);
}

na_return_t mona_addr_deserialize(
        mona_instance_t mona,
        na_addr_t *addr,
        const void *buf,
        na_size_t buf_size)
{
    return NA_Addr_deserialize(mona->na_class, addr, buf, buf_size);
}

// ------------------------------------------------------------------------------------
// Mona message information logic
// ------------------------------------------------------------------------------------

na_size_t mona_msg_get_max_unexpected_size(
        mona_instance_t mona)
{
    return NA_Msg_get_max_unexpected_size(mona->na_class);
}

na_size_t mona_msg_get_max_expected_size(
        mona_instance_t mona)
{
    return NA_Msg_get_max_expected_size(mona->na_class);
}

na_size_t mona_msg_get_unexpected_header_size(
        mona_instance_t mona)
{
    return NA_Msg_get_unexpected_header_size(mona->na_class);
}

na_size_t mona_msg_get_expected_header_size(
        mona_instance_t mona)
{
    return NA_Msg_get_expected_header_size(mona->na_class);
}

na_tag_t mona_msg_get_max_tag(mona_instance_t mona)
{
    return NA_Msg_get_max_tag(mona->na_class);
}

// ------------------------------------------------------------------------------------
// Mona operation logic
// ------------------------------------------------------------------------------------

na_op_id_t mona_op_create(mona_instance_t mona)
{
    return NA_Op_create(mona->na_class);
}

na_return_t mona_op_destroy(
        mona_instance_t mona,
        na_op_id_t op_id)
{
    return NA_Op_destroy(mona->na_class, op_id);
}

// ------------------------------------------------------------------------------------
// Mona message buffer logic
// ------------------------------------------------------------------------------------

void* mona_msg_buf_alloc(
        mona_instance_t mona,
        na_size_t buf_size,
        void **plugin_data)
{
    return NA_Msg_buf_alloc(mona->na_class, buf_size, plugin_data);
}

na_return_t mona_msg_buf_free(
        mona_instance_t mona,
        void *buf,
        void *plugin_data)
{
    return NA_Msg_buf_free(mona->na_class, buf, plugin_data);
}

na_return_t mona_msg_init_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size)
{
    return NA_Msg_init_unexpected(mona->na_class, buf, buf_size);
}

// ------------------------------------------------------------------------------------
// Mona request logic
// ------------------------------------------------------------------------------------

na_return_t mona_wait(mona_request_t req)
{
    na_return_t na_ret = mona_wait_internal(req);
    free(req);
    return na_ret;
}

int mona_test(mona_request_t req, int* flag)
{
    return ABT_eventual_test(req->eventual, NULL, flag);
}

static int mona_callback(const struct na_cb_info *info)
{
    na_return_t na_ret = info->ret;
    mona_request_t req = (mona_request_t)(info->arg);

    if(na_ret == NA_SUCCESS && info->type == NA_CB_RECV_UNEXPECTED) {
        na_addr_t source = info->info.recv_unexpected.source;
        na_tag_t tag     = info->info.recv_unexpected.tag;
        na_size_t size   = info->info.recv_unexpected.actual_buf_size;
        if(req->source_addr) {
            mona_addr_dup(req->mona, source, req->source_addr);
        }
        if(req->tag) {
            *(req->tag) = tag;
        }
        if(req->size) {
            *(req->size) = size;
        }
    }
    ABT_eventual_set(req->eventual, &na_ret, sizeof(na_ret));
    return NA_SUCCESS;
}

// ------------------------------------------------------------------------------------
// Mona high-level send/recv logic
// ------------------------------------------------------------------------------------

na_return_t mona_send(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag)
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
    struct isend_args* args = (struct isend_args*)x;
    na_return_t na_ret = mona_send(
        args->mona,
        args->buf,
        args->buf_size,
        args->dest,
        args->dest_id,
        args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_isend(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag,
        mona_request_t* req)
{
    ABT_eventual eventual;
    int ret = ABT_eventual_create(sizeof(na_return_t), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    struct isend_args* args = (struct isend_args*)malloc(sizeof(*args));
    args->mona     = mona;
    args->buf      = buf;
    args->buf_size = buf_size;
    args->dest     = dest;
    args->dest_id  = dest_id;
    args->tag      = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual = eventual;
    args->req = tmp_req;

    ret = ABT_thread_create(mona->progress_pool, isend_thread, args, ABT_THREAD_ATTR_NULL, NULL);
    if(ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_send_nc(
        mona_instance_t mona,
        na_size_t count,
        const void * const *buffers,
        const na_size_t* buf_sizes,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag)
{
    na_return_t na_ret         = NA_SUCCESS;
    na_mem_handle_t mem_handle = NA_MEM_HANDLE_NULL;
    na_size_t msg_size         = mona_msg_get_unexpected_header_size(mona) + 1;
    na_size_t data_size        = 0;
    cached_msg_t msg           = get_msg_from_cache(mona);
    unsigned i;

    for(i = 0; i < count; i++) {
        data_size += buf_sizes[i];
    }
    msg_size += data_size;

    if(msg_size <= mona_msg_get_max_unexpected_size(mona)) {

        na_ret = mona_msg_init_unexpected(mona, msg->buffer, msg_size);
        if(na_ret != NA_SUCCESS) goto finish;

        char* p = msg->buffer + mona_msg_get_unexpected_header_size(mona);
        *p = HL_MSG_SMALL;
        p += 1;

        for(i = 0; i < count; i++) {
            memcpy(p, buffers[i], buf_sizes[i]);
            p += buf_sizes[i];
        }

        na_ret = mona_msg_send_unexpected(
                mona, msg->buffer, msg_size,
                msg->plugin_data, dest,
                dest_id, tag);

    } else {

        // Expose user memory for RDMA
        if(count == 1) {
            na_ret = mona_mem_handle_create(mona, (void*)buffers[0], buf_sizes[0], NA_MEM_READ_ONLY, &mem_handle);
        } else {
            struct na_segment* segments = alloca(sizeof(*segments)*count);
            for(i = 0; i < count; i++) {
                segments[i].address = (na_ptr_t)buffers[i];
                segments[i].size = buf_sizes[i];
            }
            na_ret = mona_mem_handle_create_segments(mona, segments, count, NA_MEM_READ_ONLY, &mem_handle);
        }
        if(na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_mem_register(mona, mem_handle);
        if(na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_send_mem(mona, mem_handle, data_size, 0, dest, dest_id, tag);
        mona_mem_deregister(mona, mem_handle);
    }

finish:
    if(mem_handle != NA_MEM_HANDLE_NULL) {
        mona_mem_handle_free(mona, mem_handle);
    }
    return_msg_to_cache(mona, msg);
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
    na_return_t na_ret = mona_send_nc(
        args->mona,
        args->count,
        args->buffers,
        args->buf_sizes,
        args->dest,
        args->dest_id,
        args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_isend_nc(
        mona_instance_t mona,
        na_size_t count,
        const void * const* buffers,
        const na_size_t* buf_sizes,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag,
        mona_request_t* req)
{
    ABT_eventual eventual;
    int ret = ABT_eventual_create(sizeof(na_return_t), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    struct isend_nc_args* args = (struct isend_nc_args*)malloc(sizeof(*args));
    args->mona      = mona;
    args->count     = count;
    args->buffers   = buffers;
    args->buf_sizes = buf_sizes;
    args->dest      = dest;
    args->dest_id   = dest_id;
    args->tag       = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual = eventual;
    args->req = tmp_req;

    ret = ABT_thread_create(mona->progress_pool, isend_nc_thread, args, ABT_THREAD_ATTR_NULL, NULL);
    if(ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_send_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag)
{
    na_return_t na_ret = NA_SUCCESS;
    na_size_t msg_size = 0;
    cached_msg_t msg   = get_msg_from_cache(mona);

    na_size_t mem_handle_size = mona_mem_handle_get_serialize_size(mona, mem);

    // Initialize message to send
    msg_size = mona_msg_get_unexpected_header_size(mona) // NA header
        + 1                                         // type of message (HL_MSG_*)
        + sizeof(na_size_t)                         // size of the serialized handle
        + sizeof(na_size_t)                         // size of the data
        + sizeof(na_size_t)                         // offset in handle
        + mem_handle_size;

    na_ret = mona_msg_init_unexpected(mona, msg->buffer, msg_size);
    if(na_ret != NA_SUCCESS)
        goto finish;

    // Fill in the message
    char* p = msg->buffer + mona_msg_get_unexpected_header_size(mona);
    *p = HL_MSG_LARGE;
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
    if(na_ret != NA_SUCCESS)
        goto finish;

    // Initialize ack message to receive
    cached_msg_t   ack_msg      = get_msg_from_cache(mona);
    na_size_t      ack_msg_size = mona_msg_get_unexpected_header_size(mona) + 1;
    mona_request_t ack_req      = MONA_REQUEST_NULL;
    cached_op_id_t ack_cache_id = get_op_id_from_cache(mona);
    na_op_id_t     ack_op_id    = ack_cache_id->op_id;

    // Issue non-blocking receive for ACK
    na_ret = mona_msg_irecv_expected(mona, ack_msg->buffer, ack_msg_size,
            ack_msg->plugin_data, dest, dest_id, tag, &ack_op_id, &ack_req);
    if(na_ret != NA_SUCCESS) {
        return_op_id_to_cache(mona, ack_cache_id);
        goto finish;
    }

    // Issue send of message with mem handle
    na_ret = mona_msg_send_unexpected(
            mona, msg->buffer, msg_size,
            msg->plugin_data, dest,
            dest_id, tag);
    if(na_ret != NA_SUCCESS) {
        mona_cancel(mona, ack_op_id);
        return_op_id_to_cache(mona, ack_cache_id);
        goto finish;
    }

    // Wait for acknowledgement
    na_ret = mona_wait(ack_req);
    return_op_id_to_cache(mona, ack_cache_id);

finish:
    return_msg_to_cache(mona, msg);
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
    na_return_t na_ret = mona_send_mem(
        args->mona,
        args->mem,
        args->size,
        args->offset,
        args->dest,
        args->dest_id,
        args->tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_isend_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag,
        mona_request_t* req)
{
    ABT_eventual eventual;
    int ret = ABT_eventual_create(sizeof(na_return_t), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    struct isend_mem_args* args = (struct isend_mem_args*)malloc(sizeof(*args));
    args->mona     = mona;
    args->mem      = mem;
    args->size     = size;
    args->offset   = offset;
    args->dest     = dest;
    args->dest_id  = dest_id;
    args->tag      = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual = eventual;
    args->req = tmp_req;

    ret = ABT_thread_create(mona->progress_pool, isend_mem_thread, args, ABT_THREAD_ATTR_NULL, NULL);
    if(ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

static cached_msg_t wait_for_matching_unexpected_message(
        mona_instance_t mona,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag)
{
    cached_msg_t msg       = NULL; /* result */
    na_size_t    msg_size  = mona_msg_get_max_unexpected_size(mona);
    na_return_t  na_ret    = NA_SUCCESS;

    // lock the queue of pending messages
    ABT_mutex_lock(mona->pending_msg_mtx);

    // search in the queue of pending messages for one matching
search_in_queue:
    {
        pending_msg_t p_msg = mona->pending_msg_oldest;
        pending_msg_t p_prev_msg = NULL;
        while(p_msg) {
            if((tag == MONA_ANY_TAG || p_msg->recv_tag == tag)
            && (src == MONA_ANY_SOURCE || mona_addr_cmp(mona, src, p_msg->recv_addr))) {
                break;
            } else {
                p_prev_msg = p_msg;
                p_msg = p_msg->cached_msg->next;
            }
        }
        if(p_msg) { // matching message was found
            msg = p_msg->cached_msg;
            // remove it from the queue of pending messages
            if(p_prev_msg) p_prev_msg->cached_msg->next = p_msg->cached_msg->next;
            if(p_msg == mona->pending_msg_oldest)
                mona->pending_msg_oldest = p_msg->cached_msg->next;
            if(p_msg == mona->pending_msg_newest)
                mona->pending_msg_newest = p_prev_msg;
            // unlock the queue
            ABT_mutex_unlock(mona->pending_msg_mtx);
            // copy size, source, and tag
            if(actual_size) *actual_size = p_msg->recv_size;
            if(actual_src) mona_addr_dup(mona, p_msg->recv_addr, actual_src);
            if(actual_tag) *actual_tag = p_msg->recv_tag;
            // free the pending message object
            mona_addr_free(mona, p_msg->recv_addr);
            free(p_msg);
            // return the message
            return msg;
        }
    }
    // here the matching message wasn't found in the queue
    {
        // if another thread is actively issuing unexpected recv, wait for the queue to update
        if(mona->pending_msg_queue_active) {
            ABT_cond_wait(mona->pending_msg_cv, mona->pending_msg_mtx);
            if(mona->pending_msg_queue_active)
                goto search_in_queue;
        }
    }
    // here no matching message was found and there isn't any other threads updating the queue
    // so this thread will take the responsibility for actively listening for messages
    mona->pending_msg_queue_active = NA_TRUE;
    ABT_mutex_unlock(mona->pending_msg_mtx);
recv_new_message:
    {
        na_size_t recv_size = 0;
        na_addr_t recv_addr = NA_ADDR_NULL;
        na_tag_t  recv_tag  = 0;
        // get message from cache
        msg = get_msg_from_cache(mona);
        // issue unexpected recv
        na_ret = mona_msg_recv_unexpected(
            mona, msg->buffer, msg_size, msg->plugin_data,
            &recv_addr, &recv_tag, &recv_size);
        if(na_ret != NA_SUCCESS)
            goto error;
        // check is received message is matching
        if((tag == MONA_ANY_TAG || recv_tag == tag)
        && (src == MONA_ANY_SOURCE || mona_addr_cmp(mona, src, recv_addr))) {
            // received message matches
            // notify other threads that this thread won't be updating the queue anymore
            ABT_mutex_lock(mona->pending_msg_mtx);
            mona->pending_msg_queue_active = NA_FALSE;
            ABT_mutex_unlock(mona->pending_msg_mtx);
            ABT_cond_broadcast(mona->pending_msg_cv);
            // copy size, source, and tag
            if(actual_size) *actual_size = recv_size;
            if(actual_src) *actual_src = recv_addr;
            else mona_addr_free(mona, recv_addr);
            if(actual_tag) *actual_tag = recv_tag;
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
            ABT_mutex_lock(mona->pending_msg_mtx);
            if(mona->pending_msg_oldest == NULL) {
                mona->pending_msg_oldest = p_msg;
                mona->pending_msg_newest = p_msg;
            } else {
                mona->pending_msg_newest->cached_msg->next = p_msg;
                mona->pending_msg_newest = p_msg;
            }
            // notify other threads that the queue has been updated
            ABT_mutex_unlock(mona->pending_msg_mtx);
            ABT_cond_broadcast(mona->pending_msg_cv);
            goto recv_new_message;
        }
    }
    // error handling
error:
    if(msg) return_msg_to_cache(mona, msg);
    ABT_mutex_unlock(mona->pending_msg_mtx);
    ABT_cond_broadcast(mona->pending_msg_cv);
    return NULL;
}

na_return_t mona_recv(
        mona_instance_t mona,
        void* buf,
        na_size_t size,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag)
{
    return mona_recv_nc(mona, 1, &buf, &size, src, tag, actual_size, actual_src, actual_tag);
}

struct irecv_args {
    mona_instance_t mona;
    void*           buf;
    na_size_t       size;
    na_addr_t       src;
    na_tag_t        tag;
    na_size_t*      actual_size;
    na_addr_t*      actual_src;
    na_tag_t*       actual_tag;
    mona_request_t  req;
};

static void irecv_thread(void* x)
{
    struct irecv_args* args = (struct irecv_args*)x;
    na_return_t na_ret = mona_recv(
        args->mona,
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

na_return_t mona_irecv(
        mona_instance_t mona,
        void* buf,
        na_size_t size,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag,
        mona_request_t* req)
{
    ABT_eventual eventual;
    int ret = ABT_eventual_create(sizeof(na_return_t), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    struct irecv_args* args = (struct irecv_args*)malloc(sizeof(*args));
    args->mona        = mona;
    args->buf         = buf;
    args->size        = size;
    args->src         = src;
    args->actual_size = actual_size;
    args->actual_src  = actual_src;
    args->actual_tag  = actual_tag;
    args->tag         = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req = tmp_req;
    tmp_req->eventual = eventual;

    ret = ABT_thread_create(mona->progress_pool, irecv_thread, args, ABT_THREAD_ATTR_NULL, NULL);
    if(ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_recv_nc(
        mona_instance_t mona,
        na_size_t count,
        void** buffers,
        const na_size_t* buf_sizes,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag)
{

    na_return_t na_ret            = NA_SUCCESS;
    na_mem_handle_t mem_handle    = NA_MEM_HANDLE_NULL;
    na_mem_handle_t remote_handle = NA_MEM_HANDLE_NULL;
    na_size_t header_size         = mona_msg_get_unexpected_header_size(mona);
    cached_msg_t msg              = NULL;
    na_size_t recv_size           = 0;
    na_addr_t recv_addr           = NA_ADDR_NULL;
    na_tag_t  recv_tag            = 0;
    na_size_t max_data_size       = 0;
    unsigned i;

    for(i = 0; i < count; i++){
        max_data_size += buf_sizes[i];
    }

    // wait for a matching unexpected message to come around
    msg = wait_for_matching_unexpected_message(mona, src, tag, &recv_size, &recv_addr, &recv_tag);
    if(!msg) return NA_PROTOCOL_ERROR;

    // At this point, we know msg is the message we are looking for
    // and the attributes are recv_size, recv_tag, and recv_addr

    char* p = msg->buffer + header_size;

    if(*p == HL_MSG_SMALL) { // small message, embedded data
        
        p += 1;
        recv_size -= header_size + 1;
        na_size_t remaining_size = recv_size;
        for(i = 0; i < count && remaining_size != 0; i++) {
            na_size_t s = remaining_size < buf_sizes[i] ? remaining_size : buf_sizes[i];
            memcpy(buffers[i], p, s);
            remaining_size -= s;
        }
        recv_size = recv_size < max_data_size ? recv_size : max_data_size;

    } else if(*p == HL_MSG_LARGE) { // large message, using RDMA transfer

        p += 1;
        na_size_t mem_handle_size;
        na_size_t data_size;
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
        if(count == 1) {
            na_ret = mona_mem_handle_create(
                mona, (void*)buffers[0], buf_sizes[0], NA_MEM_WRITE_ONLY, &mem_handle);
        } else {
            struct na_segment* segments = alloca(sizeof(*segments)*count);
            for(i = 0; i < count; i++) {
                segments[i].address = (na_ptr_t)buffers[i];
                segments[i].size = buf_sizes[i];
            }
            na_ret = mona_mem_handle_create_segments(mona, segments, count, NA_MEM_WRITE_ONLY, &mem_handle); 
        }
        if(na_ret != NA_SUCCESS) goto finish;

        na_ret = mona_mem_register(mona, mem_handle);
        if(na_ret != NA_SUCCESS) goto finish;

        // Deserialize remote memory handle
        na_ret = mona_mem_handle_deserialize(
                mona, &remote_handle, p, mem_handle_size);
        if(na_ret != NA_SUCCESS) goto finish;

        // Issue RDMA operation
        // XXX how do we support a source id different from 0 ?
        data_size = data_size < max_data_size ? data_size : max_data_size;
        if(data_size) {
            na_ret = mona_get(mona, mem_handle, 0, remote_handle, remote_offset, data_size, recv_addr, 0);
            if(na_ret != NA_SUCCESS) goto finish;
        }
        recv_size = data_size;

        // Send ACK
        na_size_t msg_size = header_size + 1;
        msg->buffer[msg_size-1] = 0;
        na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
        if(na_ret != NA_SUCCESS) goto finish;

        // XXX how do we support a source id different from 0 ?
        na_ret = mona_msg_send_expected(mona, msg->buffer, msg_size,
                msg->plugin_data, recv_addr, 0, recv_tag);
        if(na_ret != NA_SUCCESS) goto finish;
    }

    if(actual_size)
        *actual_size = recv_size;
    if(actual_tag)
        *actual_tag = recv_tag;
    if(actual_src)
        *actual_src = recv_addr;
    else
        mona_addr_free(mona, recv_addr);
    recv_addr = NA_ADDR_NULL;

finish:
    if(recv_addr != NA_ADDR_NULL)
        mona_addr_free(mona, recv_addr);
    if(mem_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, mem_handle);
    if(remote_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, remote_handle);
    return_msg_to_cache(mona, msg);
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
    na_addr_t*       actual_src;
    na_tag_t*        actual_tag;
    mona_request_t   req;
};

static void irecv_nc_thread(void* x)
{
    struct irecv_nc_args* args = (struct irecv_nc_args*)x;
    na_return_t na_ret = mona_recv_nc(
        args->mona,
        args->count,
        args->buffers,
        args->buf_sizes,
        args->src,
        args->tag,
        args->actual_size,
        args->actual_src,
        args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_irecv_nc(
        mona_instance_t mona,
        na_size_t count,
        void** buffers,
        const na_size_t* buf_sizes,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag,
        mona_request_t* req)
{
    ABT_eventual eventual;
    int ret = ABT_eventual_create(sizeof(na_return_t), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    struct irecv_nc_args* args = (struct irecv_nc_args*)malloc(sizeof(*args));
    args->mona        = mona;
    args->count       = count;
    args->buffers     = buffers;
    args->buf_sizes   = buf_sizes;
    args->src         = src;
    args->actual_size = actual_size;
    args->actual_src  = actual_src;
    args->actual_tag  = actual_tag;
    args->tag         = tag;

    mona_request_t tmp_req = get_req_from_cache(mona);
    args->req = tmp_req;
    tmp_req->eventual = eventual;

    ret = ABT_thread_create(mona->progress_pool, irecv_nc_thread, args, ABT_THREAD_ATTR_NULL, NULL);
    if(ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

na_return_t mona_recv_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag)
{

    na_return_t na_ret            = NA_SUCCESS;
    na_mem_handle_t remote_handle = NA_MEM_HANDLE_NULL;
    na_size_t header_size         = mona_msg_get_unexpected_header_size(mona);
    cached_msg_t msg              = NULL;
    na_size_t recv_size           = 0;
    na_addr_t recv_addr           = NA_ADDR_NULL;
    na_tag_t  recv_tag            = 0;

    // wait for a matching unexpected message to come around
    msg = wait_for_matching_unexpected_message(mona, src, tag, &recv_size, &recv_addr, &recv_tag);
    if(!msg) return NA_PROTOCOL_ERROR;

    // At this point, we know msg is the message we are looking for
    // and the attributes are recv_size, recv_tag, and recv_addr

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
    na_ret = mona_mem_handle_deserialize(
            mona, &remote_handle, p, mem_handle_size);
    if(na_ret != NA_SUCCESS) goto finish;

    // Issue RDMA operation
    // XXX how do we support a source id different from 0 ?
    recv_size = remote_data_size < size ? remote_data_size : size;
    if(recv_size) {
        na_ret = mona_get(mona, mem, 0, remote_handle, 0, recv_size, recv_addr, 0);
        if(na_ret != NA_SUCCESS) goto finish;
    }

    // Send ACK
    na_size_t msg_size = header_size + 1;
    msg->buffer[msg_size-1] = 0;
    na_ret = mona_msg_init_expected(mona, msg->buffer, msg_size);
    if(na_ret != NA_SUCCESS) goto finish;

    // XXX how do we support a source id different from 0 ?
    na_ret = mona_msg_send_expected(mona, msg->buffer, msg_size,
            msg->plugin_data, recv_addr, 0, recv_tag);
    if(na_ret != NA_SUCCESS) goto finish;

    if(actual_size)
        *actual_size = recv_size;
    if(actual_tag)
        *actual_tag = recv_tag;
    if(actual_src)
        *actual_src = recv_addr;
    else
        mona_addr_free(mona, recv_addr);
    recv_addr = NA_ADDR_NULL;

finish:
    if(recv_addr != NA_ADDR_NULL)
        mona_addr_free(mona, recv_addr);
    if(remote_handle != NA_MEM_HANDLE_NULL)
        mona_mem_handle_free(mona, remote_handle);
    return_msg_to_cache(mona, msg);
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
    na_addr_t*      actual_src;
    na_tag_t*       actual_tag;
    mona_request_t  req;
};

static void irecv_mem_thread(void* x)
{
    struct irecv_mem_args* args = (struct irecv_mem_args*)x;
    na_return_t na_ret = mona_recv_mem(
        args->mona,
        args->mem,
        args->size,
        args->offset,
        args->src,
        args->tag,
        args->actual_size,
        args->actual_src,
        args->actual_tag);
    ABT_eventual_set(args->req->eventual, &na_ret, sizeof(na_ret));
    free(args);
}

na_return_t mona_irecv_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t* actual_tag,
        mona_request_t* req)
{
    ABT_eventual eventual;
    int ret = ABT_eventual_create(sizeof(na_return_t), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    struct irecv_mem_args* args = (struct irecv_mem_args*)malloc(sizeof(*args));
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
    args->req = tmp_req;
    tmp_req->eventual = eventual;

    ret = ABT_thread_create(mona->progress_pool, irecv_mem_thread, args, ABT_THREAD_ATTR_NULL, NULL);
    if(ret != ABT_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
        return NA_NOMEM;
    } else {
        *req = tmp_req;
        ABT_thread_yield();
    }
    return NA_SUCCESS;
}

// ------------------------------------------------------------------------------------
// Mona low-level unexpected send/recv logic
// ------------------------------------------------------------------------------------

static na_return_t mona_msg_isend_unexpected_internal(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t req)
{
    int ret;
    na_return_t na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    req->eventual = eventual;
    return NA_Msg_send_unexpected(
            mona->na_class, mona->na_context,
            mona_callback, (void*)req,
            buf, buf_size, plugin_data,
            dest_addr, dest_id, tag, op_id);
}

na_return_t mona_msg_send_unexpected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag)
{
    cached_op_id_t id = get_op_id_from_cache(mona);
    na_op_id_t op_id = id->op_id;
    mona_request req = MONA_REQUEST_INITIALIZER;
    na_return_t na_ret = mona_msg_isend_unexpected_internal(
            mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, &op_id, &req);
    if(na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_isend_unexpected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t na_ret = mona_msg_isend_unexpected_internal(
            mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, op_id, tmp_req);
    if(na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

static na_return_t mona_msg_irecv_unexpected_internal(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t* source_addr,
        na_tag_t* tag,
        na_size_t* size,
        na_op_id_t *op_id,
        mona_request_t req)
{
    int ret;
    na_return_t na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = source_addr;
    req->tag         = tag;
    req->size        = size;

    return NA_Msg_recv_unexpected(
            mona->na_class, mona->na_context,
            mona_callback, (void*)req,
            buf, buf_size, plugin_data,
            op_id);
}

na_return_t mona_msg_recv_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t* source_addr,
        na_tag_t* tag,
        na_size_t* size)
{
    mona_request req = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id = get_op_id_from_cache(mona);
    na_op_id_t op_id = id->op_id;
    na_return_t na_ret = mona_msg_irecv_unexpected_internal(
            mona, buf, buf_size, plugin_data,
            source_addr, tag, size, &op_id, &req);
    if(na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_irecv_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t* source_addr,
        na_tag_t* tag,
        na_size_t* size,
        na_op_id_t *op_id,
        mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t na_ret = mona_msg_irecv_unexpected_internal(
            mona, buf, buf_size, plugin_data, 
            source_addr, tag, size, op_id, tmp_req);
    if(na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

// ------------------------------------------------------------------------------------
// Mona low-level expected send/recv logic
// ------------------------------------------------------------------------------------

na_return_t mona_msg_init_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size)
{
    return NA_Msg_init_expected(mona->na_class, buf, buf_size);
}

static na_return_t mona_msg_isend_expected_internal(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t req)
{
    int ret;
    na_return_t na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Msg_send_expected(
            mona->na_class, mona->na_context,
            mona_callback, (void*)req,
            buf, buf_size, plugin_data,
            dest_addr, dest_id, tag, op_id);
}

na_return_t mona_msg_send_expected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag)
{
    mona_request req = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id = get_op_id_from_cache(mona);
    na_op_id_t op_id = id->op_id;
    na_return_t na_ret = mona_msg_isend_expected_internal(
            mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, &op_id, &req);
    if(na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_isend_expected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t na_ret = mona_msg_isend_expected_internal(
            mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, op_id, tmp_req);
    if(na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

static na_return_t mona_msg_irecv_expected_internal(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t req)
{
    int ret;
    na_return_t na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Msg_recv_expected(
            mona->na_class, mona->na_context,
            mona_callback, (void*)req,
            buf, buf_size, plugin_data,
            source_addr, source_id, tag, op_id);
}

na_return_t mona_msg_recv_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag)
{
    mona_request req = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id = get_op_id_from_cache(mona);
    na_op_id_t op_id = id->op_id;
    na_return_t na_ret = mona_msg_irecv_expected_internal(
            mona, buf, buf_size, plugin_data, source_addr, source_id, tag, &op_id, &req);
    if(na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_irecv_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona); 
    na_return_t na_ret = mona_msg_irecv_expected_internal(
            mona, buf, buf_size, plugin_data, source_addr, source_id, tag, op_id, tmp_req);
    if(na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

// ------------------------------------------------------------------------------------
// Mona RDMA logic
// ------------------------------------------------------------------------------------

na_return_t mona_mem_handle_create(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        unsigned long flags,
        na_mem_handle_t *mem_handle)
{
    return NA_Mem_handle_create(
            mona->na_class,
            buf, buf_size, flags,
            mem_handle);
}

na_return_t mona_mem_handle_create_segments(
        mona_instance_t mona,
        struct na_segment *segments,
        na_size_t segment_count,
        unsigned long flags,
        na_mem_handle_t *mem_handle)
{
    return NA_Mem_handle_create_segments(
            mona->na_class,
            segments, segment_count, flags,
            mem_handle);
}

na_return_t mona_mem_handle_free(
        mona_instance_t mona,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_handle_free(mona->na_class, mem_handle);
}

na_return_t mona_mem_register(
        mona_instance_t mona,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_register(mona->na_class, mem_handle);
}

na_return_t mona_mem_deregister(
        mona_instance_t mona,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_deregister(mona->na_class, mem_handle);
}

na_return_t mona_mem_publish(
        mona_instance_t mona,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_publish(mona->na_class, mem_handle);
}

na_return_t mona_mem_unpublish(
        mona_instance_t mona,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_unpublish(mona->na_class, mem_handle);
}

na_size_t mona_mem_handle_get_serialize_size(
        mona_instance_t mona,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_handle_get_serialize_size(
            mona->na_class, mem_handle);
}

na_return_t mona_mem_handle_serialize(
        mona_instance_t mona,
        void *buf, na_size_t buf_size,
        na_mem_handle_t mem_handle)
{
    return NA_Mem_handle_serialize(
            mona->na_class,
            buf, buf_size,
            mem_handle);
}

na_return_t mona_mem_handle_deserialize(
        mona_instance_t mona,
        na_mem_handle_t *mem_handle,
        const void *buf,
        na_size_t buf_size)
{
    return NA_Mem_handle_deserialize(
            mona->na_class,
            mem_handle, buf, buf_size);
}

static na_return_t mona_iput_internal(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t *op_id,
        mona_request_t req)
{
    int ret;
    na_return_t na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Put(mona->na_class, mona->na_context,
            mona_callback, (void*)req,
            local_mem_handle, local_offset,
            remote_mem_handle, remote_offset,
            data_size, remote_addr,
            remote_id, op_id);
}

na_return_t mona_put(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id)
{
    mona_request req = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id = get_op_id_from_cache(mona);
    na_op_id_t op_id = id->op_id;
    na_return_t na_ret = mona_iput_internal(
            mona, local_mem_handle, local_offset,
            remote_mem_handle, remote_offset,
            data_size, remote_addr, remote_id, &op_id, &req);
    if(na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_iput(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t *op_id,
        mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual = ABT_EVENTUAL_NULL;
    na_return_t na_ret = mona_iput_internal(
            mona, local_mem_handle, local_offset,
            remote_mem_handle, remote_offset,
            data_size, remote_addr, remote_id, op_id, tmp_req);
    if(na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

static na_return_t mona_iget_internal(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t *op_id,
        mona_request_t req)
{
    int ret;
    na_return_t na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if(ret != 0)
        return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Get(mona->na_class, mona->na_context,
            mona_callback, (void*)req,
            local_mem_handle, local_offset,
            remote_mem_handle, remote_offset,
            data_size, remote_addr,
            remote_id, op_id);
}

na_return_t mona_get(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id)
{
    mona_request req = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id = get_op_id_from_cache(mona);
    na_op_id_t op_id = id->op_id;
    na_return_t na_ret = mona_iget_internal(
            mona, local_mem_handle,
            local_offset, remote_mem_handle,
            remote_offset, data_size,
            remote_addr, remote_id,
            &op_id, &req);
    if(na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_iget(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t *op_id,
        mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual = ABT_EVENTUAL_NULL;
    na_return_t na_ret = mona_iget_internal(
            mona, local_mem_handle,
            local_offset, remote_mem_handle,
            remote_offset, data_size,
            remote_addr, remote_id,
            op_id, tmp_req);
    if(na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

// ------------------------------------------------------------------------------------
// Other functions 
// ------------------------------------------------------------------------------------

int mona_poll_get_fd(mona_instance_t mona)
{
    return NA_Poll_get_fd(mona->na_class, mona->na_context);
}

na_bool_t mona_poll_try_wait(mona_instance_t mona)
{
    return NA_Poll_try_wait(mona->na_class, mona->na_context);
}

na_return_t mona_cancel(
        mona_instance_t mona,
        na_op_id_t op_id)
{
    return NA_Cancel(mona->na_class, mona->na_context, op_id);
}

const char* mona_error_to_string(int errnum)
{
    return NA_Error_to_string((na_return_t)errnum);
}
