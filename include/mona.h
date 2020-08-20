/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_H
#define __MONA_H

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <abt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mona_instance* mona_instance_t;
typedef struct mona_request*  mona_request_t;

#define MONA_INSTANCE_NULL ((mona_instance_t)NULL)
#define MONA_REQUEST_NULL  ((mona_request_t)NULL)

mona_instance_t mona_init(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info);

mona_instance_t mona_init_thread(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info,
        na_bool_t use_progress_es);

mona_instance_t mona_init_pool(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info,
        ABT_pool progress_pool);

mona_instance_t mona_init_na_pool(
        na_class_t *na_class,
        na_context_t *na_context,
        ABT_pool progress_pool);

na_return_t mona_finalize(mona_instance_t mona);

const char* mona_get_class_name(mona_instance_t mona);

const char* mona_get_class_protocol(mona_instance_t mona);

na_bool_t mona_is_listening(mona_instance_t mona);

na_return_t mona_addr_lookup(
        mona_instance_t mona,
        const char *name,
        na_addr_t *addr);

na_return_t mona_addr_free(
        mona_instance_t mona,
        na_addr_t addr);

na_return_t mona_addr_set_remove(
        mona_instance_t mona,
        na_addr_t addr);

na_return_t mona_addr_self(
        mona_instance_t mona,
        na_addr_t* addr);

na_return_t mona_addr_dup(
        mona_instance_t mona,
        na_addr_t addr,
        na_addr_t* dup_addr);

na_bool_t mona_addr_cmp(
        mona_instance_t mona,
        na_addr_t addr1,
        na_addr_t addr2);

na_bool_t mona_addr_is_self(
        mona_instance_t mona,
        na_addr_t addr);

na_return_t mona_addr_to_string(
        mona_instance_t mona,
        char *buf,
        na_size_t *buf_size,
        na_addr_t addr);

na_size_t mona_addr_get_serialize_size(
        mona_instance_t mona,
        na_addr_t addr);

na_return_t mona_addr_serialize(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        na_addr_t addr);

na_return_t mona_addr_deserialize(
        mona_instance_t mona,
        na_addr_t *addr,
        const void *buf,
        na_size_t buf_size);

na_size_t mona_msg_get_max_unexpected_size(
        mona_instance_t mona);

na_size_t mona_msg_get_max_expected_size(
        mona_instance_t mona);

na_size_t mona_msg_get_unexpected_header_size(
        mona_instance_t mona);

na_size_t mona_msg_get_expected_header_size(
        mona_instance_t mona);

na_tag_t mona_msg_get_max_tag(mona_instance_t mona);

na_op_id_t mona_op_create(mona_instance_t mona);

na_return_t mona_op_destroy(
        mona_instance_t mona,
        na_op_id_t op_id);

void* mona_msg_buf_alloc(
        mona_instance_t mona,
        na_size_t buf_size,
        void **plugin_data);

na_return_t mona_msg_buf_free(
        mona_instance_t mona,
        void *buf,
        void *plugin_data);

na_return_t mona_msg_init_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size);

na_return_t mona_msg_send_unexpected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id);

na_return_t mona_msg_isend_unexpected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t* req);

na_return_t mona_msg_recv_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_op_id_t *op_id);

na_return_t mona_msg_irecv_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_op_id_t *op_id,
        mona_request_t* req);

na_return_t mona_msg_init_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size);

na_return_t mona_msg_send_expected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id);

na_return_t mona_msg_isend_expected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t* req);

na_return_t mona_msg_recv_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag,
        na_op_id_t *op_id);

na_return_t mona_msg_irecv_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag,
        na_op_id_t *op_id,
        mona_request_t* req);

na_return_t mona_mem_handle_create(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        unsigned long flags,
        na_mem_handle_t *mem_handle);

na_return_t mona_mem_handle_create_segments(
        mona_instance_t mona,
        struct na_segment *segments,
        na_size_t segment_count,
        unsigned long flags,
        na_mem_handle_t *mem_handle);

na_return_t mona_mem_handle_free(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

na_return_t mona_mem_register(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

na_return_t mona_mem_deregister(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

na_return_t mona_mem_publish(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

na_return_t mona_mem_unpublish(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

na_size_t mona_mem_handle_get_serialize_size(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

na_return_t mona_mem_handle_serialize(
        mona_instance_t mona,
        void *buf, na_size_t buf_size,
        na_mem_handle_t mem_handle);

na_return_t mona_mem_handle_deserialize(
        mona_instance_t mona,
        na_mem_handle_t *mem_handle,
        const void *buf,
        na_size_t buf_size);

na_return_t mona_put(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t *op_id);

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
        mona_request_t* req);

na_return_t mona_get(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t *op_id);

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
        mona_request_t* req);

int mona_poll_get_fd(mona_instance_t mona);

na_bool_t mona_poll_try_wait(mona_instance_t mona);

na_return_t mona_cancel(
        mona_instance_t mona,
        na_op_id_t op_id);

const char* mona_error_to_string(int errnum);

na_return_t mona_wait(mona_request_t req);

int mona_test(mona_request_t req, int* flag);

#ifdef __cplusplus
}
#endif

#endif
