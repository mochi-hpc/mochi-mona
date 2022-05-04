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
    mona_instance_t mona = mona_init("ofi+tcp", true, NULL);

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

static MunitResult test_isend_irecv(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    na_return_t ret;
    mona_instance_t mona = context->mona;

    char* buf = malloc(8192);
    size_t msg_len = 8192;

    if(context->rank == 0) { // sender
        int i;
        for(i = 0; i < (int)msg_len; i++) {
            buf[i] = i % 32;
        }

        mona_request_t req;

        ret = mona_isend(mona, buf, msg_len, context->other_addr, 0, 1234, &req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        ret = mona_wait(req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        size_t recv_size;
        ret = mona_irecv(mona, buf, 64, context->other_addr, 1234, &recv_size, &req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        ret = mona_wait(req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        for(i = 0; i < 64; i++) {
            munit_assert_int(buf[i], ==, (i+1) % 32);
        }

    } else { // receiver
        int i;

        mona_request_t req;
        size_t recv_size;
        ret = mona_irecv(mona, buf, msg_len, context->other_addr, 1234, &recv_size, &req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        ret = mona_wait(req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        for(i = 0; i < (int)msg_len; i++) {
            munit_assert_int(buf[i], ==, i % 32);
        }

        for(i=0; i < 64; i++) {
            buf[i] = (i+1) % 32;
        }

        ret = mona_isend(mona, buf, 64, context->other_addr, 0, 1234, &req);
        munit_assert_int(ret, ==, NA_SUCCESS);

        ret = mona_wait(req);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/hl", test_isend_irecv, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/mona/isend-irecv", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

