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

typedef struct mona_comm* mona_comm_t;

typedef void (*mona_op_t)(const void*, void*, na_size_t, na_size_t, void*);

na_return_t mona_comm_create(
        mona_instance_t mona,
        na_size_t count,
        const na_addr_t* peers,
        mona_comm_t* comm);

na_return_t mona_comm_free(mona_comm_t comm);

na_return_t mona_comm_size(mona_comm_t comm, int* size);

na_return_t mona_comm_rank(mona_comm_t comm, int* rank);

na_return_t mona_comm_addr(
        mona_comm_t comm,
        int rank,
        na_addr_t* addr,
        na_bool_t copy);

na_return_t mona_comm_dup(
        mona_comm_t comm,
        mona_comm_t* new_comm);

na_return_t mona_comm_subset(
        mona_comm_t comm,
        int* ranks,
        na_size_t size,
        mona_comm_t* new_comm);

na_return_t mona_comm_send(
        mona_comm_t comm,
        const void *buf,
        na_size_t size,
        int dest,
        na_tag_t tag);

na_return_t mona_comm_isend(
        mona_comm_t comm,
        const void *buf,
        na_size_t size,
        int dest,
        na_tag_t tag,
        mona_request_t* req);

na_return_t mona_comm_recv(
        mona_comm_t comm,
        void* buf,
        na_size_t size,
        int src,
        na_tag_t tag,
        na_size_t* actual_size,
        int* actual_src,
        na_tag_t* actual_tag);

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

na_return_t mona_comm_barrier(
        mona_comm_t comm,
        na_tag_t tag);

na_return_t mona_comm_ibarrier(
        mona_comm_t comm,
        na_tag_t tag,
        mona_request_t* req);

na_return_t mona_comm_bcast(
        mona_comm_t comm,
        void *buf,
        na_size_t size,
        int root,
        na_tag_t tag);

na_return_t mona_comm_ibcast(
        mona_comm_t comm,
        void *buf,
        na_size_t size,
        int root,
        na_tag_t tag,
        mona_request_t* req);

na_return_t mona_comm_gather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root, 
        na_tag_t tag);

na_return_t mona_comm_igather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root, 
        na_tag_t tag,
        mona_request_t* req);

na_return_t mona_comm_gatherv(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        const na_size_t* recvsizes,
        const na_size_t* displ,
        int root,
        na_tag_t tag);

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

na_return_t mona_comm_scatter(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root,
        na_tag_t tag);

na_return_t mona_comm_iscatter(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        int root,
        na_tag_t tag,
        mona_request_t* req);

na_return_t mona_comm_scatterv(
        mona_comm_t comm,
        const void *sendbuf,
        const na_size_t *sendsizes,
        const na_size_t *displs,
        void *recvbuf,
        na_size_t recvsize,
        int root,
        na_tag_t tag);

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

na_return_t mona_comm_allgather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag);

na_return_t mona_comm_iallgather(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t size,
        void *recvbuf,
        na_tag_t tag,
        mona_request_t* req);

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

na_return_t mona_comm_allreduce(
        mona_comm_t comm,
        const void *sendbuf,
        void *recvbuf,
        na_size_t typesize,
        na_size_t count,
        mona_op_t op,
        void* uargs,
        na_tag_t tag);

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

na_return_t mona_comm_alltoall(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        na_size_t recvsize,
        na_tag_t tag);

na_return_t mona_comm_ialltoall(
        mona_comm_t comm,
        const void *sendbuf,
        na_size_t sendsize,
        void *recvbuf,
        na_size_t recvsize,
        na_tag_t tag,
        mona_request_t* req);

#ifdef __cplusplus
}
#endif

#endif
