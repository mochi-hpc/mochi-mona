/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mona-comm.h"

na_return_t mona_comm_sendrecv(mona_comm_t comm,
                               const void* sendbuf,
                               size_t      sendsize,
                               int         dest,
                               na_tag_t    sendtag,
                               void*       recvbuf,
                               size_t      recvsize,
                               int         source,
                               na_tag_t    recvtag,
                               size_t*     actual_recvsize)
{
    mona_request_t sendreq;
    na_return_t    na_ret;

    na_ret = mona_comm_irecv(comm, recvbuf, recvsize, source, recvtag,
                             actual_recvsize, &sendreq);
    if (na_ret != NA_SUCCESS) return na_ret;

    na_ret = mona_comm_send(comm, sendbuf, sendsize, dest, sendtag);

    if (na_ret != NA_SUCCESS) {
        mona_wait(sendreq);
        return na_ret;
    }

    na_ret = mona_wait(sendreq);
    return na_ret;
}
