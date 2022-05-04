#include <mpi.h>
#include "munit/munit.h"
#include "mona.h"

typedef struct {
    mona_instance_t mona;
    int rank;
    na_addr_t self_addr;
    na_addr_t other_addr;
} test_context;

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;

    int ret;

    MPI_Init(NULL, NULL);
    ABT_init(0, NULL);
    mona_instance_t mona = mona_init("na+sm", true, NULL);

    test_context* context = (test_context*)calloc(1, sizeof(*context));
    context->mona = mona;
    MPI_Comm_rank(MPI_COMM_WORLD, &(context->rank));

    ret = mona_addr_self(mona, &(context->self_addr));
    munit_assert_int(ret, ==, NA_SUCCESS);

    char self_addr_str[128];
    size_t self_addr_size = 128;
    ret = mona_addr_to_string(mona, self_addr_str, &self_addr_size, context->self_addr);
    munit_assert_int(ret, ==, NA_SUCCESS);

    char other_addr_str[128];

    MPI_Sendrecv(self_addr_str, 128, MPI_BYTE, (context->rank + 1) % 2, 0,
                 other_addr_str, 128, MPI_BYTE, (context->rank + 1) % 2, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    ret = mona_addr_lookup(mona, other_addr_str, &(context->other_addr));
    munit_assert_int(ret, ==, NA_SUCCESS);

    return context;
}

static void test_context_tear_down(void* fixture)
{
    MPI_Barrier(MPI_COMM_WORLD);

    test_context* context = (test_context*)fixture;
    mona_addr_free(context->mona, context->self_addr);
    mona_addr_free(context->mona, context->other_addr);
    mona_finalize(context->mona);
    free(context);

    ABT_finalize();
    MPI_Finalize();
}

static MunitResult test_put_get(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    na_return_t ret;
    mona_instance_t mona = context->mona;
    void* plugin_data = NULL;
    size_t msg_len = mona_msg_get_max_unexpected_size(mona);
    char* msg_buf = (char*)mona_msg_buf_alloc(mona, msg_len, &plugin_data);
    char* bulk_buf = (char*)calloc(1024, 1);
    na_mem_handle_t mem_handle = NA_MEM_HANDLE_NULL;

    ret = mona_mem_handle_create(mona, bulk_buf, 1024, NA_MEM_READWRITE, &mem_handle);
    munit_assert_int(ret, ==, NA_SUCCESS);

    ret = mona_mem_register(mona, mem_handle);
    munit_assert_int(ret, ==, NA_SUCCESS);

    size_t mem_handle_size = mona_mem_handle_get_serialize_size(mona, mem_handle);

    if(context->rank == 0) { // sender

        // set the buffer content
        int i;
        for(i=0; i < 1024; i++) {
            bulk_buf[i] = i % 32;
        }

        // initialize unexpected message
        ret = mona_msg_init_unexpected(mona, msg_buf, msg_len);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // serialize local memory handle into message
        ret = mona_mem_handle_serialize(mona,
                msg_buf + mona_msg_get_unexpected_header_size(mona),
                mem_handle_size,
                mem_handle);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // send unexpected message
        ret = mona_msg_send_unexpected(
                mona, msg_buf, msg_len, plugin_data, context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // receive unexpected message
        ret = mona_msg_recv_unexpected(
                mona, msg_buf, msg_len, plugin_data,
                NULL, NULL, NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // check the new content of the buffer
        for(i=0; i < 1024; i++) {
            munit_assert_int(bulk_buf[i], ==, (i+1) % 32);
        }

    } else { // receiver

        // receive unexpected message
        ret = mona_msg_recv_unexpected(
                mona, msg_buf, msg_len, plugin_data,
                NULL, NULL, NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // deserialize remote memory handle
        na_mem_handle_t remote_mem_handle = NA_MEM_HANDLE_NULL;
        ret = mona_mem_handle_deserialize(mona, &remote_mem_handle,
                msg_buf + mona_msg_get_unexpected_header_size(mona),
                mem_handle_size);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // issue GET
        ret = mona_get(mona, mem_handle, 0, remote_mem_handle, 0, 1024, context->other_addr, 0);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // check the content of the buffer
        int i;
        for(i=0; i < 1024; i++) {
            munit_assert_int(bulk_buf[i], ==, i % 32);
        }

        // modify buffer
        for(i=0; i < 1024; i++) {
            bulk_buf[i] = (i+1) % 32;
        }

        // issue PUT
        ret = mona_put(mona, mem_handle, 0, remote_mem_handle, 0, 1024, context->other_addr, 0);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // initialize unexpected message
        ret = mona_msg_init_unexpected(mona, msg_buf, msg_len);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // send unexpected message
        ret = mona_msg_send_unexpected(
                mona, msg_buf, msg_len, plugin_data, context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);

        // free remote handle
        ret = mona_mem_handle_free(mona, remote_mem_handle);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }
    
    ret = mona_msg_buf_free(mona, msg_buf, plugin_data);
    munit_assert_int(ret, ==, NA_SUCCESS);

    ret = mona_mem_deregister(mona, mem_handle);
    munit_assert_int(ret, ==, NA_SUCCESS);

    ret = mona_mem_handle_free(mona, mem_handle);
    munit_assert_int(ret, ==, NA_SUCCESS);

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/put-get", test_put_get, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = { 
    (char*) "/mona/rma", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

