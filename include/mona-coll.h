/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_COLL_H
#define __MONA_COLL_H

#include <mona.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONA_IN_PLACE  ((void *) -1)

/**
 * @brief Handle for a group of processes. Contrary to MPI, messages
 * in Mona are not specific to a communicator. Bad design may lead
 * to a process receiving a message in a communicator that is different
 * from the communicator that was used by the sender. In Mona, distinct
 * communication sequences should be identified using distinct tags rather
 * than by creating distinct communicators.
 */
typedef struct mona_comm* mona_comm_t;

/**
 * @brief Type of operation to be passed to mona_comm_reduce and
 * mona_comm_allreduce.
 *
 * @param in pointer to first input data
 * @param inout pointer to second input data, also used for output
 * @param typesize size of the type being processed
 * @param count number of elements in the input arrays
 */
typedef void (*mona_op_t)(const void*, void*, na_size_t, na_size_t, void*);

/**
 * @brief Create a communicator from an array of addresses.
 * The provided addresses are copied into the communicator,
 * so it is safe to free them after a call to this function.
 *
 * Created communicators should be destroyed using mona_comm_free().
 *
 * @param mona [IN] Mona instance
 * @param count [IN] Number of addresses
 * @param peers [IN] Addresses of processes in the communicator
 * @param comm [OUT] Communicator
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_create(
        mona_instance_t mona,
        na_size_t count,
        const na_addr_t* peers,
        mona_comm_t* comm);

/**
 * @brief Free a communicator.
 *
 * @param comm [INOUT] Communicator to free.
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_free(mona_comm_t comm);

/**
 * @brief Get the number of processes in the communicator.
 *
 * @param comm [IN] Communicator
 * @param size [OUT] Size of the communicator
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_size(mona_comm_t comm, int* size);

/**
 * @brief Get the rank of this process within the communicator.
 *
 * @param comm [IN] Communicator
 * @param rank [OUT] Rank of this process
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_rank(mona_comm_t comm, int* rank);

/**
 * @brief Get the address corresponding to a rank.
 * If copy is set to NA_TRUE, the address will be copied
 * and it is the caller's responsibility to free it.
 * If copy is set to NA_FALSE, the address will remain
 * valid until the communicator is destroyed, and the caller
 * should not free it.
 *
 * @param comm [IN] Communicator
 * @param rank [IN] Rank
 * @param addr [OUT] Address of the rank
 * @param copy [IN] Whether to copy the address
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_addr(
        mona_comm_t comm,
        int rank,
        na_addr_t* addr,
        na_bool_t copy);

/**
 * @brief Duplicate the communicator.
 *
 * @param comm [IN] Communicator
 * @param new_comm [OUT] Duplicated communicator
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_dup(
        mona_comm_t comm,
        mona_comm_t* new_comm);

/**
 * @brief Build a new communicator from a subset of the
 * ranks from the provided communicator.
 *
 * @param comm [IN] Communicator
 * @param ranks [IN] Ranks to use from the parent communicator
 * @param size [IN] Number of processes in the new communicator
 * @param new_comm [OU] New communicator
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_subset(
        mona_comm_t comm,
        const int* ranks,
        na_size_t size,
        mona_comm_t* new_comm);

/**
 * @brief Send data to a destination process. Contrary to
 * mona_send(), mona_comm_send() identifies the destination
 * using a rank within the provided communicato.
 *
 * @param comm [IN] Communicator
 * @param buf [IN] Data to send
 * @param size [IN] Size of the data
 * @param dest [IN] Target rank
 * @param tag [IN] Tag
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_send(
        mona_comm_t comm,
        const void *buf,
        na_size_t size,
        int dest,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_send. 
 */
na_return_t mona_comm_isend(
        mona_comm_t comm,
        const void *buf,
        na_size_t size,
        int dest,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Receive a message from a source rank.
 *
 * MONA_ANY_SOURCE and MONA_ANY_TAG may be used
 * to receive from any process or with any tag.
 * 
 * actual_size, actual_src, actual_tag may be
 * ignored by passing NULL.
 *
 * @param comm [IN] Communicator
 * @param buf [IN] Buffer in which to receive the data
 * @param size [IN] Size of the buffer
 * @param src [IN] Source rank
 * @param tag [IN] Tag
 * @param actual_size [OUT] Size received
 * @param actual_src [OUT] Actual source of the message
 * @param actual_tag [OUT] Actual tag of the message
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_recv(
        mona_comm_t comm,
        void* buf,
        na_size_t size,
        int src,
        na_tag_t tag,
        na_size_t* actual_size,
        int* actual_src,
        na_tag_t* actual_tag);

/**
 * @see Non-blocking version of mona_comm_recv. 
 */
na_return_t mona_comm_irecv(
        mona_comm_t comm,
        void* buf,
        na_size_t size,
        int src,
        na_tag_t tag,
        na_size_t* actual_size,
        int* actual_src,
        na_tag_t* actual_tag,
        mona_request_t* req);

/**
 * @brief Issue a send and a receive in parallel.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN]  Send buffer
 * @param sendsize [IN] Send size
 * @param dest [IN] Destination
 * @param sendtag [IN] Send tag
 * @param recvbuf [OUT] Receiving buffer
 * @param recvsize [IN] Receiving size
 * @param source [IN] Source
 * @param recvtag [IN] Receiving tag
 * @param actual_recvsize [OUT] Actual received size
 * @param actual_recv_src [OUT] Actual received source
 * @param actual_recv_tag [OUT] Actual received tag
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_sendrecv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        int dest,
        na_tag_t sendtag,
        void *recvbuf,
        na_size_t recvsize,
        int source,
        na_tag_t recvtag,
        na_size_t* actual_recvsize,
        int* actual_recv_src,
        na_tag_t* actual_recv_tag);

/**
 * @brief Perform a barrier across the processes of the communicator.
 *
 * @param comm [IN] Communicator
 * @param tag [IN] Tag
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_barrier(
        mona_comm_t comm,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_barrier.
 */
na_return_t mona_comm_ibarrier(
        mona_comm_t comm,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Broadcast data from the specified root process to
 * the rest of the processes in the communicator.
 *
 * @param comm [IN] Communicator
 * @param buf [INOUT] Buffer to send (at root) and to receive (at non-root)
 * @param size [IN] Data size
 * @param root [IN] Root process
 * @param tag [IN] Tag to send messages with
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_bcast(
        mona_comm_t comm,
        void *buf,
        na_size_t size,
        int root,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_bcast.
 */
na_return_t mona_comm_ibcast(
        mona_comm_t comm,
        void *buf,
        na_size_t size,
        int root,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Gather data from processes to the root.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Send buffer
 * @param size [IN] Size of data each process sends
 * @param recvbuf [OUT] Receiving buffer (relevant only at root)
 * @param root [IN] Root process
 * @param tag [IN] Tag to use for messages
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_gather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root, 
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_gather.
 */
na_return_t mona_comm_igather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root, 
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Version of mona_comm_gather where each process
 * can send data of a different size.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Send buffer
 * @param sendsize [IN] Size of data to send from this process
 * @param recvbuf [OUT] Receiving buffer (relevant only at root)
 * @param recvsizes [IN] recvsizes[r] represents the size to receive from rank r
 * @param displ [IN] displ[r] represents the offset at which to place data from rank r in recvbuf
 * @param root [IN] Root process
 * @param tag [IN] Tag to use for messages
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_gatherv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        const na_size_t* recvsizes,
        const na_size_t* displ,
        int root,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_gatherv.
 */
na_return_t mona_comm_igatherv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        const na_size_t* recvsizes,
        const na_size_t* displ,
        int root,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Scatter data from the root process to the rest of the processes.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Data to send
 * @param size [IN] Size of the data to send
 * @param recvbuf [OUT] Receice buffer
 * @param root [IN] Root process
 * @param tag [IN] Tag
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_scatter(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_scatter.
 */
na_return_t mona_comm_iscatter(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Version of mona_comm_scatter allowing to send
 * data of different sizes to each process.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Send buffer
 * @param sendsizes [IN] sendsizes[r] is the size to send to rank r
 * @param displs [IN] displs[r] is the offset of the data for rank r in sendbuf
 * @param recvbuf [OUT] Buffer in which to receive data
 * @param recvsize [IN] Size to receive
 * @param root [IN] Root process
 * @param tag Tag
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_scatterv(
        mona_comm_t comm,
        const void *sendbuf,
        const na_size_t *sendsizes,
        const na_size_t *displs,
        void *recvbuf,
        na_size_t recvsize,
        int root,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_scatterv.
 */
na_return_t mona_comm_iscatterv(
        mona_comm_t comm,
        const void *sendbuf,
        const na_size_t *sendsizes,
        const na_size_t *displs,
        void *recvbuf,
        na_size_t recvsize,
        int root,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Gather data from all processes to all processes.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Data to send
 * @param size [IN] Size of the data to send
 * @param recvbuf [OUT] Buffer in which to receive data
 * @param tag [IN] Tag of the operation
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_allgather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_allgather.
 */
na_return_t mona_comm_iallgather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Reduce data down to the root process using
 * the specified operation. Because operations are type-sensitive,
 * the data size is split into a count and a type size.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Data to send
 * @param recvbuf [OUT] Buffer in which to receive data
 * @param typesize [IN] Type size
 * @param count [IN] Number of elements in send buffer
 * @param op [IN] Operation to carry out
 * @param uargs [INOUT] User-provided context fot eh operation
 * @param root [IN] Root process
 * @param tag [IN] Tag of the operation
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_reduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t typesize,
        na_size_t count,
        mona_op_t op,
        void* uargs,
        int root,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_reduce.
 */
na_return_t mona_comm_ireduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t typesize,
        na_size_t count,
        mona_op_t op,
        void* uargs,
        int root,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Reduce data and broadcast the result to all ranks.
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Data to send
 * @param recvbuf [OU] Buffer in which to receive data
 * @param typesize [IN] Type size
 * @param count [IN] Number of elements
 * @param op [IN] Operation
 * @param uargs [INOUT] Context for the operation
 * @param tag [IN] Tag
 *
 * @return NA_SUCCESS or corresponding error code (see na.h)
 */
na_return_t mona_comm_allreduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t typesize,
        na_size_t count,
        mona_op_t op,
        void* uargs,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_allreduce.
 */
na_return_t mona_comm_iallreduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t typesize,
        na_size_t count,
        mona_op_t op,
        void* uargs,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief Perform an all-to-all operation, that is,
 * each rank i sends to rank j its data block sendbuf[j*size:(j+1)*size-1]
 * and receive data from rank j at recvbuf[j*size:(j+1)*size-1].
 *
 * @param comm [IN] Communicator
 * @param sendbuf [IN] Send buffer
 * @param size [IN] Block size to send/receive
 * @param recvbuf [OUT] Receive buffer
 * @param tag [IN] Tag of the operation
 *
 * @return NA_SUCCESS or corresponding error code (see na.h) 
 */
na_return_t mona_comm_alltoall(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag);

/**
 * @see Non-blocking version of mona_comm_alltoall.
 */
na_return_t mona_comm_ialltoall(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t blocksize,
        void *recvbuf,
        na_tag_t tag,
        mona_request_t* req);

/**
 * @brief List of built-in operations for mona_comm_{i}{all}reduce.
 * The suffix of the operation indicates the type of data it operates on
 * (u64 = uint64_t, i32 = int32_t, f32 = float, f64 = double, etc.)
 */
void mona_op_max_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_max_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_min_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_min_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_sum_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_sum_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_prod_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_prod_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_land_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_land_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_lor_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_lor_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_band_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_band_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

void mona_op_bor_u64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_u32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_u16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_u8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_i64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_i32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_i16(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_i8(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_f32(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);
void mona_op_bor_f64(const void* in, void* inout, na_size_t typesize, na_size_t count, void* uargs);

#ifdef __cplusplus
}
#endif

#endif
