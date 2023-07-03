/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-types.h"
#include "mona-callback.h"
#include <string.h>

// ------------------------------------------------------------------------------------
// Mona progress loop logic
// ------------------------------------------------------------------------------------

static void mona_progress_loop(void* uarg)
{
    mona_instance_t mona = (mona_instance_t)uarg;
    na_return_t     trigger_ret, na_ret;
    unsigned int    actual_count = 0;
    size_t          size;

    while (!mona->finalize_flag) {

        do {
            trigger_ret = NA_Trigger(mona->na_context, 1, &actual_count);
        } while ((trigger_ret == NA_SUCCESS) && actual_count
                 && !mona->finalize_flag);

        ABT_pool_get_size(mona->progress_pool, &size);
        if (size) ABT_thread_yield();

        // TODO put a high timeout value to avoid busy-spinning
        // if there is no other ULT in the pool that could run
        na_ret = NA_Progress(mona->na_class, mona->na_context, 0);
        if (na_ret != NA_SUCCESS && na_ret != NA_TIMEOUT) {
            fprintf(stderr,
                    "WARNING: unexpected return value from NA_Progress (%d)\n",
                    na_ret);
        }
    }
}

// ------------------------------------------------------------------------------------
// Mona initialization logic
// ------------------------------------------------------------------------------------

mona_instance_t mona_init(const char*                info_string,
                          bool                       listen,
                          const struct na_init_info* na_init_info)
{
    return mona_init_thread(info_string, listen, na_init_info, false);
}

mona_instance_t mona_init_thread(const char*                info_string,
                                 bool                       listen,
                                 const struct na_init_info* na_init_info,
                                 bool                       use_progress_es)
{
    int             ret;
    ABT_xstream     xstream       = ABT_XSTREAM_NULL;
    ABT_pool        progress_pool = ABT_POOL_NULL;
    mona_instance_t mona          = MONA_INSTANCE_NULL;

    if (use_progress_es == true) {

        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC,
                                    ABT_FALSE, &progress_pool);
        if (ret != ABT_SUCCESS) goto error;

        ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &progress_pool,
                                       ABT_SCHED_CONFIG_NULL, &xstream);
        if (ret != ABT_SUCCESS) goto error;

    } else {

        ret = ABT_xstream_self(&xstream);
        if (ret != ABT_SUCCESS) goto error;

        ret = ABT_xstream_get_main_pools(xstream, 1, &progress_pool);
        if (ret != ABT_SUCCESS) goto error;
    }

    mona = mona_init_pool(info_string, listen, na_init_info, progress_pool);
    if (!mona) goto error;

    if (use_progress_es == true) {
        mona->owns_progress_pool    = true;
        mona->owns_progress_xstream = true;
    }

    mona->progress_xstream = xstream;

finish:
    return mona;

error:
    if (progress_pool != ABT_POOL_NULL && use_progress_es == true)
        ABT_pool_free(&progress_pool);
    if (xstream != ABT_XSTREAM_NULL && use_progress_es == true)
        ABT_xstream_free(&xstream);
    mona = MONA_INSTANCE_NULL;
    goto finish;
}

mona_instance_t mona_init_pool(const char*                info_string,
                               bool                       listen,
                               const struct na_init_info* na_init_info,
                               ABT_pool                   progress_pool)
{
    na_class_t*     na_class   = NULL;
    na_context_t*   na_context = NULL;
    mona_instance_t mona       = MONA_INSTANCE_NULL;

    na_class = NA_Initialize_opt(info_string, listen, na_init_info);
    if (!na_class) goto error;

    na_context = NA_Context_create(na_class);
    if (!na_context) goto error;

    mona = mona_init_na_pool(na_class, na_context, progress_pool);
    if (!mona) goto error;

    mona->owns_na_class_and_context = true;

finish:
    return mona;

error:
    if (na_context) NA_Context_destroy(na_class, na_context);
    if (na_class) NA_Finalize(na_class);
    mona = MONA_INSTANCE_NULL;
    goto finish;
}

mona_instance_t mona_init_na_pool(na_class_t*   na_class,
                                  na_context_t* na_context,
                                  ABT_pool      progress_pool)
{
    int             ret, i;
    mona_instance_t mona = (mona_instance_t)calloc(1, sizeof(*mona));
    if (!mona) return MONA_INSTANCE_NULL;
    mona->na_class                   = na_class;
    mona->na_context                 = na_context;
    mona->progress_pool              = progress_pool;
    mona->progress_xstream           = ABT_XSTREAM_NULL;
    mona->progress_thread            = ABT_THREAD_NULL;
    mona->op_id_cache_mtx            = ABT_MUTEX_NULL;
    mona->req_cache_mtx              = ABT_MUTEX_NULL;
    mona->unexpected.msg_cache_mtx   = ABT_MUTEX_NULL;
    mona->unexpected.pending_msg_mtx = ABT_MUTEX_NULL;
    mona->unexpected.pending_msg_cv  = ABT_COND_NULL;
    mona->expected.msg_cache_mtx     = ABT_MUTEX_NULL;
    mona->hints.rdma_threshold       = (size_t)(-1);
    ret = ABT_mutex_create(&(mona->op_id_cache_mtx));
    if (ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->req_cache_mtx));
    if (ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->unexpected.msg_cache_mtx));
    if (ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->expected.msg_cache_mtx));
    if (ret != ABT_SUCCESS) goto error;
    ret = ABT_mutex_create(&(mona->unexpected.pending_msg_mtx));
    if (ret != ABT_SUCCESS) goto error;
    ret = ABT_cond_create(&(mona->unexpected.pending_msg_cv));
    if (ret != ABT_SUCCESS) goto error;

    mona->op_id_cache = (cached_op_id_t)calloc(1, sizeof(*(mona->op_id_cache)));
    mona->op_id_cache->op_id = NA_Op_create(na_class, 0);

    cached_op_id_t current = mona->op_id_cache;
    for (i = 0; i < 15; i++) {
        current->next  = (cached_op_id_t)calloc(1, sizeof(*current));
        current        = current->next;
        current->op_id = NA_Op_create(na_class, 0);
    }

    ret = ABT_thread_create(mona->progress_pool, mona_progress_loop,
                            (void*)mona, ABT_THREAD_ATTR_NULL,
                            &(mona->progress_thread));
    if (ret != ABT_SUCCESS) goto error;

    mona->unexpected.prob_active = false;
    mona->unexpected.prob_addr   = MONA_ADDR_NULL;
    mona->unexpected.prob_size   = 0;
    mona->unexpected.prob_tag    = 0;
    mona->unexpected.prob_req    = MONA_REQUEST_NULL;
    mona->unexpected.prob_msg    = NULL;
    mona->unexpected.prob_id     = NULL;

finish:
    return mona;

error:
    if (mona->op_id_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->op_id_cache_mtx));
    if (mona->req_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->req_cache_mtx));
    if (mona->unexpected.msg_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->unexpected.msg_cache_mtx));
    if (mona->expected.msg_cache_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->expected.msg_cache_mtx));
    if (mona->unexpected.pending_msg_mtx != ABT_MUTEX_NULL)
        ABT_mutex_free(&(mona->unexpected.pending_msg_mtx));
    if (mona->unexpected.pending_msg_cv != ABT_COND_NULL)
        ABT_cond_free(&(mona->unexpected.pending_msg_cv));
    free(mona);
    mona = MONA_INSTANCE_NULL;
    goto finish;
}

na_return_t mona_finalize(mona_instance_t mona)
{
    if (mona->unexpected.prob_active) {
        na_return_t na_ret = NA_Cancel(mona->na_class, mona->na_context,
                                       mona->unexpected.prob_id->op_id);
        if (na_ret != NA_SUCCESS) {
            fprintf(stderr, "WARNING: MoNA could not cancel probe operation\n");
        } else {
            mona_wait(mona->unexpected.prob_req);
            return_op_id_to_cache(mona, mona->unexpected.prob_id);
            return_msg_to_cache(mona, mona->unexpected.prob_msg, false);
        }
    }

    mona->finalize_flag = true;
    ABT_thread_join(mona->progress_thread);

    if (mona->owns_progress_xstream) {
        ABT_xstream_join(mona->progress_xstream);
        ABT_xstream_free(&(mona->progress_xstream));
    }
    if (mona->owns_progress_pool) ABT_pool_free(&(mona->progress_pool));

    clear_op_id_cache(mona);
    ABT_mutex_free(&(mona->op_id_cache_mtx));

    clear_req_cache(mona);
    ABT_mutex_free(&(mona->req_cache_mtx));

    clear_msg_cache(mona);
    ABT_mutex_free(&(mona->unexpected.msg_cache_mtx));
    ABT_mutex_free(&(mona->expected.msg_cache_mtx));

    ABT_mutex_free(&(mona->unexpected.pending_msg_mtx));
    ABT_cond_free(&(mona->unexpected.pending_msg_cv));

    if (mona->owns_na_class_and_context) {
        NA_Context_destroy(mona->na_class, mona->na_context);
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

bool mona_is_listening(mona_instance_t mona)
{
    return NA_Is_listening(mona->na_class);
}

// ------------------------------------------------------------------------------------
// Mona addresses logic
// ------------------------------------------------------------------------------------

na_return_t
mona_addr_lookup(mona_instance_t mona, const char* name, mona_addr_t* addr)
{
    return NA_Addr_lookup(mona->na_class, name, addr);
}

na_return_t mona_addr_free(mona_instance_t mona, mona_addr_t addr)
{
    NA_Addr_free(mona->na_class, addr);
    return NA_SUCCESS;
}

na_return_t mona_addr_set_remove(mona_instance_t mona, mona_addr_t addr)
{
    return NA_Addr_set_remove(mona->na_class, addr);
}

na_return_t mona_addr_self(mona_instance_t mona, mona_addr_t* addr)
{
    return NA_Addr_self(mona->na_class, addr);
}

na_return_t
mona_addr_dup(mona_instance_t mona, mona_addr_t addr, mona_addr_t* dup_addr)
{
    return NA_Addr_dup(mona->na_class, addr, dup_addr);
}

bool mona_addr_cmp(mona_instance_t mona, mona_addr_t addr1, mona_addr_t addr2)
{
    if (addr1 == MONA_ADDR_NULL && addr2 == MONA_ADDR_NULL) return true;
    if (addr1 == MONA_ADDR_NULL || addr2 == MONA_ADDR_NULL) return false;
    char   str1[256], str2[256];
    size_t s = 256;
    mona_addr_to_string(mona, str1, &s, addr1);
    s = 256;
    mona_addr_to_string(mona, str2, &s, addr2);
    return strcmp(str1, str2) == 0;
}

bool mona_addr_is_self(mona_instance_t mona, mona_addr_t addr)
{
    return NA_Addr_is_self(mona->na_class, addr);
}

na_return_t mona_addr_to_string(mona_instance_t mona,
                                char*           buf,
                                size_t*         buf_size,
                                mona_addr_t     addr)
{
    return NA_Addr_to_string(mona->na_class, buf, buf_size, addr);
}

size_t mona_addr_get_serialize_size(mona_instance_t mona, mona_addr_t addr)
{
    return NA_Addr_get_serialize_size(mona->na_class, addr);
}

na_return_t mona_addr_serialize(mona_instance_t mona,
                                void*           buf,
                                size_t          buf_size,
                                mona_addr_t     addr)
{
    return NA_Addr_serialize(mona->na_class, buf, buf_size, addr);
}

na_return_t mona_addr_deserialize(mona_instance_t mona,
                                  mona_addr_t*    addr,
                                  const void*     buf,
                                  size_t          buf_size)
{
    return NA_Addr_deserialize(mona->na_class, addr, buf, buf_size);
}

// ------------------------------------------------------------------------------------
// Mona message information logic
// ------------------------------------------------------------------------------------

size_t mona_msg_get_max_unexpected_size(mona_instance_t mona)
{
    return NA_Msg_get_max_unexpected_size(mona->na_class);
}

size_t mona_msg_get_max_expected_size(mona_instance_t mona)
{
    return NA_Msg_get_max_expected_size(mona->na_class);
}

size_t mona_msg_get_unexpected_header_size(mona_instance_t mona)
{
    return NA_Msg_get_unexpected_header_size(mona->na_class);
}

size_t mona_msg_get_expected_header_size(mona_instance_t mona)
{
    return NA_Msg_get_expected_header_size(mona->na_class);
}

na_tag_t mona_msg_get_max_tag(mona_instance_t mona)
{
    return NA_Msg_get_max_tag(mona->na_class) / 2 - 1;
}

// ------------------------------------------------------------------------------------
// Mona hint logic
// ------------------------------------------------------------------------------------

na_return_t mona_hint_set_rdma_threshold(mona_instance_t mona, size_t threshold)
{
    mona->hints.rdma_threshold = threshold;
    return NA_SUCCESS;
}

// ------------------------------------------------------------------------------------
// Mona operation logic
// ------------------------------------------------------------------------------------

na_op_id_t* mona_op_create(mona_instance_t mona)
{
    return NA_Op_create(mona->na_class, 0);
}

na_return_t mona_op_destroy(mona_instance_t mona, na_op_id_t* op_id)
{
    NA_Op_destroy(mona->na_class, op_id);
    return NA_SUCCESS;
}

// ------------------------------------------------------------------------------------
// Mona message buffer logic
// ------------------------------------------------------------------------------------

void* mona_msg_buf_alloc(mona_instance_t mona,
                         size_t          buf_size,
                         void**          plugin_data)
{
    return NA_Msg_buf_alloc(mona->na_class, buf_size, 0, plugin_data);
}

na_return_t
mona_msg_buf_free(mona_instance_t mona, void* buf, void* plugin_data)
{
    NA_Msg_buf_free(mona->na_class, buf, plugin_data);
    return NA_SUCCESS;
}

na_return_t
mona_msg_init_unexpected(mona_instance_t mona, void* buf, size_t buf_size)
{
    return NA_Msg_init_unexpected(mona->na_class, buf, buf_size);
}

// ------------------------------------------------------------------------------------
// Mona request logic
// ------------------------------------------------------------------------------------

na_return_t mona_wait(mona_request_t req)
{
    na_return_t na_ret = mona_wait_internal(req);
    if (na_ret == NA_SUCCESS) free(req);
    return na_ret;
}

int mona_test(mona_request_t req, int* flag)
{
    if (req == MONA_REQUEST_NULL) return ABT_ERR_OTHER;
    int ret = ABT_eventual_test(req->eventual, NULL, flag);
    if (ret == ABT_SUCCESS && !flag) ABT_thread_yield();
    return ret;
}

na_return_t mona_wait_any(size_t count, mona_request_t* reqs, size_t* index)
{
    // XXX this is an active loop, we should change it
    // when Argobots provide an ABT_eventual_wait_any
    size_t i;
    int    ret;
    int    flag = 0;
    int    has_pending_requests;
try_again:
    has_pending_requests = 0;
    for (i = 0; i < count; i++) {
        if (reqs[i] == MONA_REQUEST_NULL)
            continue;
        else
            has_pending_requests = 1;
        ret = mona_test(reqs[i], &flag);
        if (ret != ABT_SUCCESS) {
            *index = i;
            return NA_RETURN_MAX;
        }
        if (flag) {
            *index             = i;
            mona_request_t req = reqs[i];
            reqs[i]            = MONA_REQUEST_NULL;
            return mona_wait(req);
        }
    }
    ABT_thread_yield();
    if (has_pending_requests) goto try_again;
    *index = count;
    return NA_SUCCESS;
}

// ------------------------------------------------------------------------------------
// Mona low-level unexpected send/recv logic
// ------------------------------------------------------------------------------------

static na_return_t mona_msg_isend_unexpected_internal(mona_instance_t mona,
                                                      const void*     buf,
                                                      size_t          buf_size,
                                                      void*       plugin_data,
                                                      mona_addr_t dest_addr,
                                                      uint8_t     dest_id,
                                                      na_tag_t    tag,
                                                      na_op_id_t* op_id,
                                                      mona_request_t req)
{
    int          ret;
    na_return_t  na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if (ret != 0) return NA_NOMEM;

    req->eventual = eventual;
    return NA_Msg_send_unexpected(mona->na_class, mona->na_context,
                                  mona_callback, (void*)req, buf, buf_size,
                                  plugin_data, dest_addr, dest_id, tag, op_id);
}

na_return_t mona_msg_send_unexpected(mona_instance_t mona,
                                     const void*     buf,
                                     size_t          buf_size,
                                     void*           plugin_data,
                                     mona_addr_t     dest_addr,
                                     uint8_t         dest_id,
                                     na_tag_t        tag)
{
    cached_op_id_t id     = get_op_id_from_cache(mona);
    na_op_id_t*    op_id  = id->op_id;
    mona_request   req    = MONA_REQUEST_INITIALIZER;
    na_return_t    na_ret = mona_msg_isend_unexpected_internal(
        mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, op_id, &req);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_isend_unexpected(mona_instance_t mona,
                                      const void*     buf,
                                      size_t          buf_size,
                                      void*           plugin_data,
                                      mona_addr_t     dest_addr,
                                      uint8_t         dest_id,
                                      na_tag_t        tag,
                                      na_op_id_t*     op_id,
                                      mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t    na_ret  = mona_msg_isend_unexpected_internal(
        mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, op_id,
        tmp_req);
    if (na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

static na_return_t mona_msg_irecv_unexpected_internal(mona_instance_t mona,
                                                      void*           buf,
                                                      size_t          buf_size,
                                                      void*        plugin_data,
                                                      mona_addr_t* source_addr,
                                                      na_tag_t*    tag,
                                                      size_t*      size,
                                                      na_op_id_t*  op_id,
                                                      mona_request_t req)
{
    int          ret;
    na_return_t  na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if (ret != 0) return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = source_addr;
    req->tag         = tag;
    req->size        = size;

    return NA_Msg_recv_unexpected(mona->na_class, mona->na_context,
                                  mona_callback, (void*)req, buf, buf_size,
                                  plugin_data, op_id);
}

na_return_t mona_msg_recv_unexpected(mona_instance_t mona,
                                     void*           buf,
                                     size_t          buf_size,
                                     void*           plugin_data,
                                     mona_addr_t*    source_addr,
                                     na_tag_t*       tag,
                                     size_t*         size)
{
    mona_request   req    = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id     = get_op_id_from_cache(mona);
    na_op_id_t*    op_id  = id->op_id;
    na_return_t    na_ret = mona_msg_irecv_unexpected_internal(
        mona, buf, buf_size, plugin_data, source_addr, tag, size, op_id, &req);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_irecv_unexpected(mona_instance_t mona,
                                      void*           buf,
                                      size_t          buf_size,
                                      void*           plugin_data,
                                      mona_addr_t*    source_addr,
                                      na_tag_t*       tag,
                                      size_t*         size,
                                      na_op_id_t*     op_id,
                                      mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t    na_ret  = mona_msg_irecv_unexpected_internal(
        mona, buf, buf_size, plugin_data, source_addr, tag, size, op_id,
        tmp_req);
    if (na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

// ------------------------------------------------------------------------------------
// Mona low-level expected send/recv logic
// ------------------------------------------------------------------------------------

na_return_t
mona_msg_init_expected(mona_instance_t mona, void* buf, size_t buf_size)
{
    return NA_Msg_init_expected(mona->na_class, buf, buf_size);
}

static na_return_t mona_msg_isend_expected_internal(mona_instance_t mona,
                                                    const void*     buf,
                                                    size_t          buf_size,
                                                    void*           plugin_data,
                                                    mona_addr_t     dest_addr,
                                                    uint8_t         dest_id,
                                                    na_tag_t        tag,
                                                    na_op_id_t*     op_id,
                                                    mona_request_t  req)
{
    int          ret;
    na_return_t  na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if (ret != 0) return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Msg_send_expected(mona->na_class, mona->na_context, mona_callback,
                                (void*)req, buf, buf_size, plugin_data,
                                dest_addr, dest_id, tag, op_id);
}

na_return_t mona_msg_send_expected(mona_instance_t mona,
                                   const void*     buf,
                                   size_t          buf_size,
                                   void*           plugin_data,
                                   mona_addr_t     dest_addr,
                                   uint8_t         dest_id,
                                   na_tag_t        tag)
{
    mona_request   req    = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id     = get_op_id_from_cache(mona);
    na_op_id_t*    op_id  = id->op_id;
    na_return_t    na_ret = mona_msg_isend_expected_internal(
        mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, op_id, &req);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_isend_expected(mona_instance_t mona,
                                    const void*     buf,
                                    size_t          buf_size,
                                    void*           plugin_data,
                                    mona_addr_t     dest_addr,
                                    uint8_t         dest_id,
                                    na_tag_t        tag,
                                    na_op_id_t*     op_id,
                                    mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t    na_ret  = mona_msg_isend_expected_internal(
        mona, buf, buf_size, plugin_data, dest_addr, dest_id, tag, op_id,
        tmp_req);
    if (na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

static na_return_t mona_msg_irecv_expected_internal(mona_instance_t mona,
                                                    void*           buf,
                                                    size_t          buf_size,
                                                    void*           plugin_data,
                                                    mona_addr_t     source_addr,
                                                    uint8_t         source_id,
                                                    na_tag_t        tag,
                                                    na_op_id_t*     op_id,
                                                    mona_request_t  req)
{
    int          ret;
    na_return_t  na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if (ret != 0) return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Msg_recv_expected(mona->na_class, mona->na_context, mona_callback,
                                (void*)req, buf, buf_size, plugin_data,
                                source_addr, source_id, tag, op_id);
}

na_return_t mona_msg_recv_expected(mona_instance_t mona,
                                   void*           buf,
                                   size_t          buf_size,
                                   void*           plugin_data,
                                   mona_addr_t     source_addr,
                                   uint8_t         source_id,
                                   na_tag_t        tag)
{
    mona_request   req    = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id     = get_op_id_from_cache(mona);
    na_op_id_t*    op_id  = id->op_id;
    na_return_t    na_ret = mona_msg_irecv_expected_internal(
        mona, buf, buf_size, plugin_data, source_addr, source_id, tag, op_id,
        &req);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_msg_irecv_expected(mona_instance_t mona,
                                    void*           buf,
                                    size_t          buf_size,
                                    void*           plugin_data,
                                    mona_addr_t     source_addr,
                                    uint8_t         source_id,
                                    na_tag_t        tag,
                                    na_op_id_t*     op_id,
                                    mona_request_t* req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    na_return_t    na_ret  = mona_msg_irecv_expected_internal(
        mona, buf, buf_size, plugin_data, source_addr, source_id, tag, op_id,
        tmp_req);
    if (na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

// ------------------------------------------------------------------------------------
// Mona RDMA logic
// ------------------------------------------------------------------------------------

na_return_t mona_mem_handle_create(mona_instance_t    mona,
                                   void*              buf,
                                   size_t             buf_size,
                                   unsigned long      flags,
                                   mona_mem_handle_t* mem_handle)
{
    return NA_Mem_handle_create(mona->na_class, buf, buf_size, flags,
                                mem_handle);
}

na_return_t mona_mem_handle_create_segments(mona_instance_t    mona,
                                            struct na_segment* segments,
                                            size_t             segment_count,
                                            unsigned long      flags,
                                            mona_mem_handle_t* mem_handle)
{
    return NA_Mem_handle_create_segments(mona->na_class, segments,
                                         segment_count, flags, mem_handle);
}

na_return_t mona_mem_handle_free(mona_instance_t   mona,
                                 mona_mem_handle_t mem_handle)
{
    NA_Mem_handle_free(mona->na_class, mem_handle);
    return NA_SUCCESS;
}

na_return_t mona_mem_register(mona_instance_t   mona,
                              mona_mem_handle_t mem_handle)
{
#ifdef NA_VERSION_MAJOR
    #if NA_VERSION_MAJOR >= 3
    return NA_Mem_register(mona->na_class, mem_handle, NA_MEM_TYPE_HOST, 0);
    #else
    return NA_Mem_register(mona->na_class, mem_handle);
    #endif
#endif
#ifndef NA_VERSION_MAJOR
    return NA_Mem_register(mona->na_class, mem_handle);
#endif
}

na_return_t mona_mem_deregister(mona_instance_t   mona,
                                mona_mem_handle_t mem_handle)
{
    return NA_Mem_deregister(mona->na_class, mem_handle);
}

size_t mona_mem_handle_get_serialize_size(mona_instance_t   mona,
                                          mona_mem_handle_t mem_handle)
{
    return NA_Mem_handle_get_serialize_size(mona->na_class, mem_handle);
}

na_return_t mona_mem_handle_serialize(mona_instance_t   mona,
                                      void*             buf,
                                      size_t            buf_size,
                                      mona_mem_handle_t mem_handle)
{
    return NA_Mem_handle_serialize(mona->na_class, buf, buf_size, mem_handle);
}

na_return_t mona_mem_handle_deserialize(mona_instance_t    mona,
                                        mona_mem_handle_t* mem_handle,
                                        const void*        buf,
                                        size_t             buf_size)
{
    return NA_Mem_handle_deserialize(mona->na_class, mem_handle, buf, buf_size);
}

static na_return_t mona_iput_internal(mona_instance_t   mona,
                                      mona_mem_handle_t local_mem_handle,
                                      na_offset_t       local_offset,
                                      mona_mem_handle_t remote_mem_handle,
                                      na_offset_t       remote_offset,
                                      size_t            data_size,
                                      mona_addr_t       remote_addr,
                                      uint8_t           remote_id,
                                      na_op_id_t*       op_id,
                                      mona_request_t    req)
{
    int          ret;
    na_return_t  na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if (ret != 0) return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Put(mona->na_class, mona->na_context, mona_callback, (void*)req,
                  local_mem_handle, local_offset, remote_mem_handle,
                  remote_offset, data_size, remote_addr, remote_id, op_id);
}

na_return_t mona_put(mona_instance_t   mona,
                     mona_mem_handle_t local_mem_handle,
                     na_offset_t       local_offset,
                     mona_mem_handle_t remote_mem_handle,
                     na_offset_t       remote_offset,
                     size_t            data_size,
                     mona_addr_t       remote_addr,
                     uint8_t           remote_id)
{
    mona_request   req    = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id     = get_op_id_from_cache(mona);
    na_op_id_t*    op_id  = id->op_id;
    na_return_t    na_ret = mona_iput_internal(
        mona, local_mem_handle, local_offset, remote_mem_handle, remote_offset,
        data_size, remote_addr, remote_id, op_id, &req);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_iput(mona_instance_t   mona,
                      mona_mem_handle_t local_mem_handle,
                      na_offset_t       local_offset,
                      mona_mem_handle_t remote_mem_handle,
                      na_offset_t       remote_offset,
                      size_t            data_size,
                      mona_addr_t       remote_addr,
                      uint8_t           remote_id,
                      na_op_id_t*       op_id,
                      mona_request_t*   req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual      = ABT_EVENTUAL_NULL;
    na_return_t na_ret     = mona_iput_internal(
        mona, local_mem_handle, local_offset, remote_mem_handle, remote_offset,
        data_size, remote_addr, remote_id, op_id, tmp_req);
    if (na_ret != NA_SUCCESS) {
        return_req_to_cache(mona, tmp_req);
    } else {
        *req = tmp_req;
    }
    return na_ret;
}

static na_return_t mona_iget_internal(mona_instance_t   mona,
                                      mona_mem_handle_t local_mem_handle,
                                      na_offset_t       local_offset,
                                      mona_mem_handle_t remote_mem_handle,
                                      na_offset_t       remote_offset,
                                      size_t            data_size,
                                      mona_addr_t       remote_addr,
                                      uint8_t           remote_id,
                                      na_op_id_t*       op_id,
                                      mona_request_t    req)
{
    int          ret;
    na_return_t  na_ret;
    ABT_eventual eventual = ABT_EVENTUAL_NULL;

    ret = ABT_eventual_create(sizeof(na_ret), &eventual);
    if (ret != 0) return NA_NOMEM;

    req->eventual    = eventual;
    req->mona        = mona;
    req->source_addr = NULL;
    req->tag         = NULL;
    req->size        = NULL;

    return NA_Get(mona->na_class, mona->na_context, mona_callback, (void*)req,
                  local_mem_handle, local_offset, remote_mem_handle,
                  remote_offset, data_size, remote_addr, remote_id, op_id);
}

na_return_t mona_get(mona_instance_t   mona,
                     mona_mem_handle_t local_mem_handle,
                     na_offset_t       local_offset,
                     mona_mem_handle_t remote_mem_handle,
                     na_offset_t       remote_offset,
                     size_t            data_size,
                     mona_addr_t       remote_addr,
                     uint8_t           remote_id)
{
    mona_request   req    = MONA_REQUEST_INITIALIZER;
    cached_op_id_t id     = get_op_id_from_cache(mona);
    na_op_id_t*    op_id  = id->op_id;
    na_return_t    na_ret = mona_iget_internal(
        mona, local_mem_handle, local_offset, remote_mem_handle, remote_offset,
        data_size, remote_addr, remote_id, op_id, &req);
    if (na_ret != NA_SUCCESS) goto finish;
    na_ret = mona_wait_internal(&req);
finish:
    return_op_id_to_cache(mona, id);
    return na_ret;
}

na_return_t mona_iget(mona_instance_t   mona,
                      mona_mem_handle_t local_mem_handle,
                      na_offset_t       local_offset,
                      mona_mem_handle_t remote_mem_handle,
                      na_offset_t       remote_offset,
                      size_t            data_size,
                      mona_addr_t       remote_addr,
                      uint8_t           remote_id,
                      na_op_id_t*       op_id,
                      mona_request_t*   req)
{
    mona_request_t tmp_req = get_req_from_cache(mona);
    tmp_req->eventual      = ABT_EVENTUAL_NULL;
    na_return_t na_ret     = mona_iget_internal(
        mona, local_mem_handle, local_offset, remote_mem_handle, remote_offset,
        data_size, remote_addr, remote_id, op_id, tmp_req);
    if (na_ret != NA_SUCCESS) {
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

bool mona_poll_try_wait(mona_instance_t mona)
{
    return NA_Poll_try_wait(mona->na_class, mona->na_context);
}

na_return_t mona_cancel(mona_instance_t mona, na_op_id_t* op_id)
{
    return NA_Cancel(mona->na_class, mona->na_context, op_id);
}

const char* mona_error_to_string(int errnum)
{
    return NA_Error_to_string((na_return_t)errnum);
}
