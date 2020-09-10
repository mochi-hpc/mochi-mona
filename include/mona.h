/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_H
#define __MONA_H

#include <na.h>
#include <abt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mona_instance* mona_instance_t;
typedef struct mona_request*  mona_request_t;

#define MONA_INSTANCE_NULL ((mona_instance_t)NULL)
#define MONA_REQUEST_NULL  ((mona_request_t)NULL)

#define MONA_ANY_SOURCE NA_ADDR_NULL
#define MONA_ANY_TAG    0xFFFFFFFF

/**
 * @brief Initialize a Mona instance.
 *
 * @param info_string [IN]  protocol (e.g. "na+sm", "ofi+tcp", etc.)
 * @param listen [IN]       whether to listen for messages or not
 * @param na_init_info [IN] additional information to pass to the NA layer
 *
 * @return a valid mona_instance_t or MONA_INSTANCE_NULL in case of an error
 */
mona_instance_t mona_init(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info);

/**
 * @brief Same as mona_init() but allows users to specify whether
 * to create a dedicated execution stream for the progress loop.
 *
 * @param info_string [IN]      protocol (e.g. "na+sm", "ofi+tcp", etc.)
 * @param listen [IN]           whether to listen for messages or not
 * @param na_init_info [IN]     additional information to pass to the NA layer
 * @param use_progress_es [IN]  whether to create an ES for progress
 *
 * @return a valid mona_instance_t or MONA_INSTANCE_NULL in case of an error
 */
mona_instance_t mona_init_thread(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info,
        na_bool_t use_progress_es);

/**
 * @brief Same as mona_init() but allows users to specify the
 * Argobots pool in which the progress loop will run.
 *
 * @param info_string [IN]      protocol (e.g. "na+sm", "ofi+tcp", etc.)
 * @param listen [IN]           whether to listen for messages or not
 * @param na_init_info [IN]     additional information to pass to the NA layer
 * @param progress_pool [IN]    Argobots pool for the progress loop
 *
 * @return a valid mona_instance_t or MONA_INSTANCE_NULL in case of an error
 */
mona_instance_t mona_init_pool(
        const char *info_string,
        na_bool_t listen,
        const struct na_init_info *na_init_info,
        ABT_pool progress_pool);

/**
 * @brief Initialize a Mona instance from existing na_class, na_context,
 * and Argobots progress pool. Note that the na_context should not be used
 * by another library (e.g. it is incorrect to extrat an na_context from
 * an hg_context that is actively being used, and use this na_context with
 * Mona, since messages sent by Mona will interfere with messages sent by
 * Mercury, and vice-versa).
 *
 * @param na_class [IN]         NA class
 * @param na_context [IN]       NA context
 * @param progress_pool [IN]    Argobots pool for the progress thread
 *
 * @return  a valid mona_instance_t or MONA_INSTANCE_NULL in case of an error
 */
mona_instance_t mona_init_na_pool(
        na_class_t *na_class,
        na_context_t *na_context,
        ABT_pool progress_pool);

/**
 * Finalize the Mona instance. If operations are still
 * pending (e.g. from other threads), this function will
 * block until they are completed.
 *
 * \param mona [IN/OUT] Mona instance
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_finalize(mona_instance_t mona);

/**
 * Return the name of the Mona instance's underlying NA class.
 *
 * \param mona [IN] Mona instance
 *
 * \return Pointer to NA class name or NULL in case of failure
 */
const char* mona_get_class_name(mona_instance_t mona);


/**
 * Return the protocol of the Mona instance's underlying NA class.
 *
 * \param mona [IN] Mona instance
 *
 * \return Pointer to NA class protocol or NULL in case of failure
 */
const char* mona_get_class_protocol(mona_instance_t mona);

/**
 * Test whether the Mona instance is listening or not.
 *
 * \param mona [IN] Mona instance
 *
 * \return NA_TRUE if listening or NA_FALSE if not
 */
na_bool_t mona_is_listening(mona_instance_t mona);

/**
 * Lookup an addr from a peer address/name. Addresses need to be
 * freed by calling mona_addr_free().
 *
 * \param mona [IN/OUT] Mona instance
 * \param name [IN]     lookup name
 * \param addr [OUT]    pointer to abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_lookup(
        mona_instance_t mona,
        const char *name,
        na_addr_t *addr);

/**
 * Free the addr from the list of peers.
 *
 * \param mona [IN/OUT] Mona instance
 * \param addr [IN]     abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_free(
        mona_instance_t mona,
        na_addr_t addr);

/**
 * Hint that the address is no longer valid. This may happen if the peer is
 * no longer responding. This can be used to force removal of the
 * peer address from the list of the peers, before freeing it and reclaim
 * resources.
 *
 * \param mona [IN/OUT] Mona instance
 * \param addr [IN]     abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_set_remove(
        mona_instance_t mona,
        na_addr_t addr);

/**
 * Access self address.
 *
 * \param mona [IN/OUT] Mona instance 
 * \param addr [OUT]    pointer to abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_self(
        mona_instance_t mona,
        na_addr_t* addr);

/**
 * Duplicate an existing NA abstract address. The duplicated address can be
 * stored for later use and the origin address be freed safely. The duplicated
 * address must be freed with mona_addr_free().
 *
 * \param mona [IN/OUT]  Mona instance
 * \param addr [IN]      abstract address
 * \param new_addr [OUT] pointer to abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_dup(
        mona_instance_t mona,
        na_addr_t addr,
        na_addr_t* dup_addr);

/**
 * Compare two addresses.
 *
 * \param mona [IN/OUT] Mona instance
 * \param addr1 [IN]    abstract address
 * \param addr2 [IN]    abstract address
 *
 * \return NA_TRUE if addresses are determined to be equal, NA_FALSE otherwise
 */
na_bool_t mona_addr_cmp(
        mona_instance_t mona,
        na_addr_t addr1,
        na_addr_t addr2);

/**
 * Test whether address is self or not.
 *
 * \param mona[IN/OUT] Mona instance
 * \param addr [IN]    abstract address
 *
 * \return NA_TRUE if self or NA_FALSE if not
 */
na_bool_t mona_addr_is_self(
        mona_instance_t mona,
        na_addr_t addr);

/**
 * Convert an addr to a string (returned string includes the terminating
 * null byte '\0'). If buf is NULL, the address is not converted and only
 * the required size of the buffer is returned. If the input value passed
 * through buf_size is too small, NA_SIZE_ERROR is returned and the buf_size
 * output is set to the minimum size required.
 *
 * \param mona [IN/OUT]         Mona instance 
 * \param buf [IN/OUT]          pointer to destination buffer
 * \param buf_size [IN/OUT]     pointer to buffer size
 * \param addr [IN]             abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_to_string(
        mona_instance_t mona,
        char *buf,
        na_size_t *buf_size,
        na_addr_t addr);

/**
 * Get size required to serialize address.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param addr [IN]             abstract address
 *
 * \return Non-negative value
 */
na_size_t mona_addr_get_serialize_size(
        mona_instance_t mona,
        na_addr_t addr);

/**
 * Serialize address into a buffer.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN/OUT]          pointer to buffer used for serialization
 * \param buf_size [IN]         buffer size
 * \param addr [IN]             abstract address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_serialize(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        na_addr_t addr);

/**
 * Deserialize address from a buffer. The returned address must be freed with
 * mona addr_free().
 *
 * \param mona [IN/OUT]         Mona instance
 * \param addr [OUT]            pointer to abstract address
 * \param buf [IN]              pointer to buffer used for deserialization
 * \param buf_size [IN]         buffer size
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_addr_deserialize(
        mona_instance_t mona,
        na_addr_t *addr,
        const void *buf,
        na_size_t buf_size);

/**
 * @brief High-level blocking send function. This function will
 * appropriatly use unexpected messages or combinations of unexpected,
 * expected, and RDMA message depending on data size.
 *
 * Note: using a high-level function in conjunction with low-level
 * (mona_msg_*) functions may lead to undefined behaviors and should
 * be avoided.
 *
 * @param mona [IN/OUT]     Mona instance
 * @param buf [IN]          data to send
 * @param buf_size [IN]     buffer size
 * @param dest [IN]         destination address
 * @param dest_id [IN]      destination context id
 * @param tag [IN]          tag
 *
 * @return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_send(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag);

/**
 * @brief Non-blocking version of mona_send. The resulting mona_request_t
 * can be used in mona_wait() and mona_test.
 *
 * Note: using a high-level function in conjunction with low-level
 * (mona_msg_*) functions may lead to undefined behaviors and should
 * be avoided.
 *
 * @param mona [IN/OUT]     Mona instance
 * @param buf [IN]          data to send
 * @param buf_size [IN]     buffer size
 * @param dest [IN]         destination address
 * @param dest_id [IN]      destination context id
 * @param tag [IN]          tag
 * @param req [OUT]         request
 *
 * @return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_isend(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @see Same as mona_send but for non-contiguous data.
 * A mon_send_nc can be matched by a mona_recv or a mona_recv_nc
 * on the destination.
 */
na_return_t mona_send_nc(
        mona_instance_t mona,
        na_size_t count,
        const void * const* buffers,
        const na_size_t* buf_sizes,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag);

/**
 * @see Same as mona_isend but for non-contiguous data.
 * A mon_send_nc can be matched by a mona_recv or a mona_recv_nc
 * on the destination.
 */
na_return_t mona_isend_nc(
        mona_instance_t mona,
        na_size_t count,
        const void * const* buffers,
        const na_size_t* buf_sizes,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Send data using a memory handle.
 * This operation should be matched by a mona_recv_mem
 * on the destination.
 *
 * @param mona [IN] Mona instance
 * @param mem [IN] Memory handle to send
 * @param size [IN] Size of the data to send
 * @param offset [IN] Offset of the data in the memory handle
 * @param dest [IN] Destination address
 * @param dest_id [IN] Destination id
 * @param tag [IN] Tag
 *
 * @return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_send_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_send_mem.
 */
na_return_t mona_isend_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t dest,
        na_uint8_t dest_id,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief High-level blocking recv function. This function will
 * appropriatly use unexpected messages or combinations of unexpected,
 * expected, and RDMA message depending on data size, to match a
 * call to mona_send or mona_isend from the source.
 *
 * Note: using a high-level function in conjunction with low-level
 * (mona_msg_*) functions may lead to undefined behaviors and should
 * be avoided.
 *
 * Because the called may use MONA_ANY_SOURCE and/or MONA_ANY_TAG,
 * the actual_src and actual_tag can be used to get the actual sender
 * and tag. These parameters, as well as actual_size, may be set to
 * NULL to be ignored.
 *
 * @param mona [IN/OUT]     Mona instance
 * @param buf [OUT]         buffer in which to place the received data
 * @param size [IN]         buffer size
 * @param dest [IN]         source address
 * @param tag [IN]          tag
 * @param actual_size [OUT] actual received size
 * @param actual_src [OUT]  actual source
 * @param actual_tag [OUT]  actual tag
 *
 * @return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_recv(
        mona_instance_t mona,
        void* buf,
        na_size_t size,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t*  actual_tag);

/**
 * @brief Non-blocking equivalent of mona_recv. The resulting mona_request_t
 * can be used in mona_wait() and mona_test.
 *
 * @param mona [IN/OUT]     Mona instance
 * @param buf [OUT]         buffer in which to place the received data
 * @param buf_size [IN]     buffer size
 * @param dest [IN]         source address
 * @param tag [IN]          tag
 * @param actual_size [OUT] actual received size
 * @param actual_src [OUT]  actual source
 * @param actual_tag [OUT]  actual tag
 * @param req [OUT]         request
 *
 * @return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_irecv(
        mona_instance_t mona,
        void* buf,
        na_size_t buf_size,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t*  actual_tag,
        mona_request_t* req);

/**
 * @see Non-contiguous version of mona_recv.
 * This function can match a mona_send or a mona_send_nc.
 */
na_return_t mona_recv_nc(
        mona_instance_t mona,
        na_size_t count,
        void** buffers,
        const na_size_t* buf_sizes,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t*  actual_tag);

/**
 * @see Non-blocking version of mona_recv_nc. 
 */
na_return_t mona_irecv_nc(
        mona_instance_t mona,
        na_size_t count,
        void** buffers,
        const na_size_t* buf_sizes,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t*  actual_tag,
        mona_request_t* req);

/**
 * @brief Receives data directly into a memory handle.
 * This function should match a mona_send_mem.
 *
 * @param mona [IN] Mona instance
 * @param mem [IN] Memory handle to receive data
 * @param size [IN] Size of the data to receive
 * @param offset [IN] Offset at which to place the data in the memory handle
 * @param src [IN] Source address
 * @param tag [IN] Tag
 * @param actual_size [OUT] Actual size received
 * @param actual_src [OUT] Actual source
 * @param actual_tag [OUT] Actual tag
 *
 * @return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_recv_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t*  actual_tag);

/**
 * @see Non-blocking version of mona_recv_mem.
 */
na_return_t mona_irecv_mem(
        mona_instance_t mona,
        na_mem_handle_t mem,
        na_size_t size,
        na_size_t offset,
        na_addr_t src,
        na_tag_t tag,
        na_size_t* actual_size,
        na_addr_t* actual_src,
        na_tag_t*  actual_tag,
        mona_request_t* req);

/**
 * Get the maximum size of messages supported by unexpected send/recv.
 * Small message size.
 *
 * \param mona [IN] Mona instance
 *
 * \return Non-negative value
 */
na_size_t mona_msg_get_max_unexpected_size(
        mona_instance_t mona);

/**
 * Get the maximum size of messages supported by expected send/recv.
 * Small message size that may differ from the unexpected message size.
 *
 * \param mona [IN] Mona instance
 *
 * \return Non-negative value
 */
na_size_t mona_msg_get_max_expected_size(
        mona_instance_t mona);

/**
 * Get the header size for unexpected messages. Plugins may use that header
 * to encode specific information (such as source addr, etc).
 *
 * \param mona [IN] Mona instance
 *
 * \return Non-negative value
 */
na_size_t mona_msg_get_unexpected_header_size(
        mona_instance_t mona);

/**
 * Get the header size for expected messages. Plugins may use that header
 * to encode specific information.
 *
 * \param mona [IN] Mona instance
 *
 * \return Non-negative value
 */
na_size_t mona_msg_get_expected_header_size(
        mona_instance_t mona);

/**
 * Get the maximum tag value that can be used by send/recv (both expected and
 * unexpected).
 *
 * \param mona [IN] Mona instance
 *
 * \return Non-negative value
 */
na_tag_t mona_msg_get_max_tag(mona_instance_t mona);

/**
 * Allocate an operation ID to be used for isend, irecv, iput, and iget operations.
 * Allocating an operation ID gives ownership of that ID to the higher level
 * layer, hence it must be explicitly released with mona_op_destroy() when it
 * is no longer needed.
 *
 * \param mona [IN/OUT] Mona instance
 *
 * \return valid operation ID or NA_OP_ID_NULL
 */
na_op_id_t mona_op_create(mona_instance_t mona);

/**
 * Destroy operation ID created with mona_op_create().
 * Reference counting prevents involuntary free.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param op_id [IN]            operation ID
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_op_destroy(
        mona_instance_t mona,
        na_op_id_t op_id);
/**
 * Allocate buf_size bytes and return a pointer to the allocated memory.
 * If size is 0, mona_msg_buf_alloc() returns NULL. The plugin_data output
 * parameter can be used by the underlying plugin implementation to store
 * internal memory information.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf_size [IN]         buffer size
 * \param plugin_data [OUT]     pointer to internal plugin data
 *
 * \return Pointer to allocated memory or NULL in case of failure
 */
void* mona_msg_buf_alloc(
        mona_instance_t mona,
        na_size_t buf_size,
        void **plugin_data);

/**
 * The mona_msg_buf_free() function releases the memory space pointed to by buf,
 * which must have been returned by a previous call to mona_msg_buf_alloc().
 * If buf is NULL, no operation is performed.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to buffer
 * \param plugin_data [IN]      pointer to internal plugin data
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_buf_free(
        mona_instance_t mona,
        void *buf,
        void *plugin_data);

/**
 * Initialize a buffer so that it can be safely passed to the
 * mona_msg_send_unexpected() call. In the case the underlying plugin adds its
 * own header to that buffer, the header will be written at this time and the
 * usable buffer payload will be buf + mona_msg_get_unexpected_header_size().
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to buffer
 * \param buf_size [IN]         buffer size
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_init_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size);

/**
 * Send an unexpected message to dest_addr. Unexpected sends do not require a
 * matching receive to complete.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 * \remark Note also that unexpected messages do not require an unexpected
 * receive to be posted at the destination before sending the message and the
 * destination is allowed to drop the message without notification.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to send buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param dest_addr [IN]        abstract address of destination
 * \param dest_id [IN]          destination context ID
 * \param tag [IN]              tag attached to message
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_send_unexpected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag);

/**
 * Send an unexpected message to dest_addr, without blocking. Unexpected
 * sends do not require a matching receive to complete. The resulting
 * mona_request_t must be waited on using mona_wait().
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 * \remark Note also that unexpected messages do not require an unexpected
 * receive to be posted at the destination before sending the message and the
 * destination is allowed to drop the message without notification.
 *
 * The user is responsible for creating a new operation id using mona_op_create()
 * before calling this function, and destroy it after using mona_op_destroy.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to send buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param dest_addr [IN]        abstract address of destination
 * \param dest_id [IN]          destination context ID
 * \param tag [IN]              tag attached to message
 * \param op_id [IN]            operation ID
 * \param req [OUT]             request tracking operation completion
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_isend_unexpected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t* op_id,
        mona_request_t* req);

/**
 * Receive an unexpected message. Unexpected receives may wait on any tag and
 * any source depending on the implementation.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 *
 * The source_addr, tag, and size arguments may be NULL. If source_addr
 * is not NULL, the sender's address will be duplicated into it. It is
 * the caller's responsibility to free it using mona_addr_free().
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to send buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param source_addr [OUT]     address of the sender
 * \param tag [OUT]             tag used by the sender
 * \param size [OUT]            actual size of the data received
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_recv_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t* source_addr,
        na_tag_t* tag,
        na_size_t* size);
/**
 * Receive an unexpected message in an non-blocking manner. Unexpected receives
 * may wait on any tag and any source depending on the implementation.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 *
 * The user is responsible for creating a new operation id using mona_op_create()
 * before calling this function, and destroy it after using mona_op_destroy.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param buf [IN]              pointer to send buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param source_addr [OUT]     address of the sender
 * \param tag [OUT]             tag used by the sender
 * \param size [OUT]            actual size of the data received
 * \param op_id [IN]            operation ID
 * \param req [OUT]             request tracking operation completion
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_irecv_unexpected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t* source_addr,
        na_tag_t* tag,
        na_size_t* size,
        na_op_id_t* op_id,
        mona_request_t* req);


/**
 * Initialize a buffer so that it can be safely passed to the
 * mona_msg_send_expected() call. In the case the underlying plugin adds its
 * own header to that buffer, the header will be written at this time and the
 * usable buffer payload will be buf + mona_msg_get_expected_header_size().
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to buffer
 * \param buf_size [IN]         buffer size
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_init_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size);

/**
 * Send an expected message to dest_addr.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 * \remark Note that expected messages require an expected receive to be posted
 * at the destination before sending the message, otherwise the destination is
 * allowed to drop the message without notification.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to send buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param dest_addr [IN]        abstract address of destination
 * \param dest_id [IN]          destination context ID
 * \param tag [IN]              tag attached to message
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_send_expected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag);

/**
 * Send an expected message to dest_addr in a non-blocking manner.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 * \remark Note that expected messages require an expected receive to be posted
 * at the destination before sending the message, otherwise the destination is
 * allowed to drop the message without notification.
 *
 * The user is responsible for creating a new operation id using mona_op_create()
 * before calling this function, and destroy it after using mona_op_destroy.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to send buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param dest_addr [IN]        abstract address of destination
 * \param dest_id [IN]          destination context ID
 * \param tag [IN]              tag attached to message
 * \param op_id [IN]            operation ID
 * \param req [OUT]             request tracking operation completion
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_isend_expected(
        mona_instance_t mona,
        const void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t dest_addr,
        na_uint8_t dest_id,
        na_tag_t tag,
        na_op_id_t* op_id,
        mona_request_t* req);

/**
 * Receive an expected message from source_addr.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to receive buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param source_addr [IN]      abstract address of source
 * \param source_id [IN]        source context ID
 * \param tag [IN]              matching tag used to receive message
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_recv_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag);

/**
 * Receive an expected message from source_addr in a non-blocking mannger.
 * The plugin_data parameter returned from the mona_msg_buf_alloc() call must
 * be passed along with the buffer, it allows plugins to store and retrieve
 * additional buffer information such as memory descriptors.
 *
 * The user is responsible for creating a new operation id using mona_op_create()
 * before calling this function, and destroy it after using mona_op_destroy.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to receive buffer
 * \param buf_size [IN]         buffer size
 * \param plugin_data [IN]      pointer to internal plugin data
 * \param source_addr [IN]      abstract address of source
 * \param source_id [IN]        source context ID
 * \param tag [IN]              matching tag used to receive message
 * \param op_id [IN]            operation ID
 * \param req [OUT]             request tracking operation completion
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_msg_irecv_expected(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        void *plugin_data,
        na_addr_t source_addr,
        na_uint8_t source_id,
        na_tag_t tag,
        na_op_id_t* op_id,
        mona_request_t* req);

/**
 * Create memory handle for RMA operations.
 * For non-contiguous memory, use mona_mem_handle_create_segments() instead.
 *
 * \remark Note to plugin developers: mona_mem_handle_create() may be called
 * multiple times on the same memory region.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN]              pointer to buffer that needs to be registered
 * \param buf_size [IN]         buffer size
 * \param flags [IN]            permission flag:
 *                                - NA_MEM_READWRITE
 *                                - NA_MEM_READ_ONLY
 * \param mem_handle [OUT]      pointer to returned abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_handle_create(
        mona_instance_t mona,
        void *buf,
        na_size_t buf_size,
        unsigned long flags,
        na_mem_handle_t *mem_handle);

/**
 * Create memory handle for RMA operations.
 * Create_segments can be used to register fragmented pieces and get a single
 * memory handle.
 * \remark Implemented only if the network transport or hardware supports it.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param segments [IN]         pointer to array of segments composed of:
 *                                - address of the segment that needs to be
 *                                  registered
 *                                - size of the segment in bytes
 * \param segment_count [IN]    segment count
 * \param flags [IN]            permission flag:
 *                                - NA_MEM_READWRITE
 *                                - NA_MEM_READ_ONLY
 * \param mem_handle [OUT]      pointer to returned abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_handle_create_segments(
        mona_instance_t mona,
        struct na_segment *segments,
        na_size_t segment_count,
        unsigned long flags,
        na_mem_handle_t *mem_handle);

/**
 * Free memory handle.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [IN]       abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_handle_free(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

/**
 * Register memory for RMA operations.
 * Memory pieces must be registered before one-sided transfers can be
 * initiated.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [IN]       pointer to abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_register(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

/**
 * Unregister memory.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [IN]       abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_deregister(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

/**
 * Expose memory for RMA operations.
 * Memory pieces must be registered before one-sided transfers can be
 * initiated.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [IN]       pointer to abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_publish(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

/**
 * Unpublish memory.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [IN]       abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_unpublish(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

/**
 * Get size required to serialize handle.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [IN]       abstract memory handle
 *
 * \return Non-negative value
 */
na_size_t mona_mem_handle_get_serialize_size(
        mona_instance_t mona,
        na_mem_handle_t mem_handle);

/**
 * Serialize memory handle into a buffer.
 * One-sided transfers require prior exchange of memory handles between
 * peers, serialization callbacks can be used to "pack" a memory handle and
 * send it across the network.
 * \remark Memory handles can be variable size, therefore the space required
 * to serialize a handle into a buffer can be obtained using
 * mona_mem_handle_get_serialize_size().
 *
 * \param mona [IN/OUT]         Mona instance
 * \param buf [IN/OUT]          pointer to buffer used for serialization
 * \param buf_size [IN]         buffer size
 * \param mem_handle [IN]       abstract memory handle
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_handle_serialize(
        mona_instance_t mona,
        void *buf, na_size_t buf_size,
        na_mem_handle_t mem_handle);

/**
 * Deserialize memory handle from buffer.
 *
 * \param mona [IN/OUT]         Mona instance
 * \param mem_handle [OUT]      pointer to abstract memory handle
 * \param buf [IN]              pointer to buffer used for deserialization
 * \param buf_size [IN]         buffer size
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_mem_handle_deserialize(
        mona_instance_t mona,
        na_mem_handle_t *mem_handle,
        const void *buf,
        na_size_t buf_size);

/**
 * Put data to remote address.
 * \remark Memory must be registered and handles exchanged between peers.
 *
 * \param mona [IN/OUT]          Mona instance
 * \param local_mem_handle [IN]  abstract local memory handle
 * \param local_offset [IN]      local offset
 * \param remote_mem_handle [IN] abstract remote memory handle
 * \param remote_offset [IN]     remote offset
 * \param data_size [IN]         size of data that needs to be transferred
 * \param remote_addr [IN]       abstract address of remote destination
 * \param remote_id [IN]         target ID of remote destination
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_put(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id);

/**
 * Put data to remote address in a non-blocking mannger.
 * \remark Memory must be registered and handles exchanged between peers.
 *
 * It is the caller's responsibility to create an op_id by using
 * mona_op_create(), and destroy it when the id is not needed anymore.
 *
 * \param mona [IN/OUT]          Mona instance
 * \param local_mem_handle [IN]  abstract local memory handle
 * \param local_offset [IN]      local offset
 * \param remote_mem_handle [IN] abstract remote memory handle
 * \param remote_offset [IN]     remote offset
 * \param data_size [IN]         size of data that needs to be transferred
 * \param remote_addr [IN]       abstract address of remote destination
 * \param remote_id [IN]         target ID of remote destination
 * \param op_id [OUT]            operation ID
 * \param req [OUT]              request tracking operation completion
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_iput(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t* op_id,
        mona_request_t* req);

/**
 * Get data from remote address.
 *
 * \param mona [IN/OUT]          Mona instance
 * \param local_mem_handle [IN]  abstract local memory handle
 * \param local_offset [IN]      local offset
 * \param remote_mem_handle [IN] abstract remote memory handle
 * \param remote_offset [IN]     remote offset
 * \param data_size [IN]         size of data that needs to be transferred
 * \param remote_addr [IN]       abstract address of remote source
 * \param remote_id [IN]         target ID of remote source
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_get(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id);

/**
 * Get data from remote address in a non-blocking mannger.
 *
 * It is the caller's responsibility to create an op_id by using
 * mona_op_create(), and destroy it when the id is not needed anymore.
 *
 * \param mona [IN/OUT]          Mona instance
 * \param local_mem_handle [IN]  abstract local memory handle
 * \param local_offset [IN]      local offset
 * \param remote_mem_handle [IN] abstract remote memory handle
 * \param remote_offset [IN]     remote offset
 * \param data_size [IN]         size of data that needs to be transferred
 * \param remote_addr [IN]       abstract address of remote source
 * \param remote_id [IN]         target ID of remote source
 * \param op_id [IN]             operation ID
 * \param req [OUT]              request tracking operation completion
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_iget(
        mona_instance_t mona,
        na_mem_handle_t local_mem_handle,
        na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle,
        na_offset_t remote_offset,
        na_size_t data_size,
        na_addr_t remote_addr,
        na_uint8_t remote_id,
        na_op_id_t* op_id,
        mona_request_t* req);

/**
 * Retrieve file descriptor from NA plugin when supported. The descriptor
 * can be used by upper layers for manual polling through the usual
 * OS select/poll/epoll calls.
 *
 * \param mona [IN/OUT] Mona instance
 *
 * \return Non-negative integer if supported, 0 if not implemented and negative
 * in case of error.
 */
int mona_poll_get_fd(mona_instance_t mona);

/**
 * Used to signal when it is safe to block on the class/context poll descriptor
 * or if blocking could hang the application.
 *
 * \param mona [IN/OUT] Mona instance
 *
 * \return NA_TRUE if it is safe to block or NA_FALSE otherwise
 */
na_bool_t mona_poll_try_wait(mona_instance_t mona);

/**
 * Cancel an ongoing operation.
 *
 * \param mona [IN/OUT] Mona instance
 * \param op_id [IN]    operation ID
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_cancel(
        mona_instance_t mona,
        na_op_id_t op_id);

/**
 * Convert error return code to string (null terminated).
 *
 * \param errnum [IN]           error return code
 *
 * \return String
 */
const char* mona_error_to_string(int errnum);

/**
 * Block until the provided request completes.
 *
 * \param req [IN] request to wait on
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
na_return_t mona_wait(mona_request_t req);

/**
 * Test ifthe provided request has completed.
 *
 * \param req [IN] request to test
 * \param flag [OUT] 1 if request completed, 0 otherwise
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
int mona_test(mona_request_t req, int* flag);

#ifdef __cplusplus
}
#endif

#endif
