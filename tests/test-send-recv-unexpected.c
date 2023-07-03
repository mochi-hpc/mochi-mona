#include <mpi.h>
#include "munit/munit.h"
#include "mona.h"

typedef struct {
    mona_instance_t mona;
    int             rank;
    mona_addr_t     self_addr;
    mona_addr_t     other_addr;
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
    context->mona         = mona;
    MPI_Comm_rank(MPI_COMM_WORLD, &(context->rank));

    ret = mona_addr_self(mona, &(context->self_addr));
    munit_assert_int(ret, ==, NA_SUCCESS);

    char   self_addr_str[128];
    size_t self_addr_size = 128;
    ret = mona_addr_to_string(mona, self_addr_str, &self_addr_size,
                              context->self_addr);
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

static MunitResult test_send_recv_unexpected(const MunitParameter params[],
                                             void*                data)
{
    (void)params;
    test_context*   context = (test_context*)data;
    na_return_t     ret;
    mona_instance_t mona        = context->mona;
    void*           plugin_data = NULL;
    size_t          msg_len     = mona_msg_get_max_unexpected_size(mona);
    char* buf = (char*)mona_msg_buf_alloc(mona, msg_len, &plugin_data);

    if (context->rank == 0) { // sender
        int i;
        for (i = mona_msg_get_unexpected_header_size(mona); i < (int)msg_len;
             i++) {
            buf[i] = i % 32;
        }
        ret = mona_msg_init_unexpected(mona, buf, msg_len);
        munit_assert_int(ret, ==, NA_SUCCESS);

        ret = mona_msg_send_unexpected(mona, buf, msg_len, plugin_data,
                                       context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);
    } else { // receiver
        na_tag_t    tag         = 0;
        size_t      actual_size = 0;
        mona_addr_t source_addr = MONA_ADDR_NULL;

        ret = mona_msg_recv_unexpected(mona, buf, msg_len, plugin_data,
                                       &source_addr, &tag, &actual_size);
        munit_assert_int(ret, ==, NA_SUCCESS);
        int i;
        for (i = mona_msg_get_unexpected_header_size(mona); i < (int)msg_len;
             i++) {
            munit_assert_int(buf[i], ==, i % 32);
        }

        ret = mona_addr_free(mona, source_addr);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    ret = mona_msg_buf_free(mona, buf, plugin_data);
    munit_assert_int(ret, ==, NA_SUCCESS);

    return MUNIT_OK;
}

static MunitTest test_suite_tests[]
    = {{(char*)"/unexpected", test_send_recv_unexpected, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/mona/send-recv", test_suite_tests, NULL, 1,
       MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, (void*)"mona", argc, argv);
}
