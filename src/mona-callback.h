/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_CALLBACK_H
#define __MONA_CALLBACK_H

static inline void mona_callback(const struct na_cb_info* info)
{
    na_return_t    na_ret = info->ret;
    mona_request_t req    = (mona_request_t)(info->arg);

    if (na_ret == NA_SUCCESS && info->type == NA_CB_RECV_UNEXPECTED) {
        mona_addr_t source = info->info.recv_unexpected.source;
        na_tag_t  tag      = info->info.recv_unexpected.tag;
        size_t    size     = info->info.recv_unexpected.actual_buf_size;
        if (req->source_addr) {
            mona_addr_dup(req->mona, source, req->source_addr);
        }
        if (req->tag) { *(req->tag) = tag; }
        if (req->size) { *(req->size) = size; }
    }
    ABT_eventual_set(req->eventual, &na_ret, sizeof(na_ret));
}

#endif
