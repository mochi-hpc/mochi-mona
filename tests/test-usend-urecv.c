#include <unistd.h>
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

    mona_instance_t mona = mona_init("ofi+tcp", true, NULL);

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
}

static MunitResult test_usend_urecv(const MunitParameter params[], void* data)
{
    (void)params;
    test_context*   context = (test_context*)data;
    na_return_t     ret;
    mona_instance_t mona = context->mona;

    char*  buf     = malloc(8192);
    size_t msg_len = 8192;

    if (context->rank == 0) { // sender
        int i;
        for (i = 0; i < (int)msg_len; i++) { buf[i] = i % 32; }

        ret = mona_usend(mona, buf, msg_len, context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);

        size_t recv_size;
        ret = mona_urecv(mona, buf, 64, context->other_addr, 1234, &recv_size,
                         NULL, NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);

        for (i = 0; i < 64; i++) { munit_assert_int(buf[i], ==, (i + 1) % 32); }

    } else { // receiver
        int i;

        size_t recv_size;
        ret = mona_urecv(mona, buf, msg_len, context->other_addr, 1234,
                         &recv_size, NULL, NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);
        for (i = 0; i < (int)msg_len; i++) {
            munit_assert_int(buf[i], ==, i % 32);
        }

        for (i = 0; i < 64; i++) { buf[i] = (i + 1) % 32; }

        ret = mona_usend(mona, buf, 64, context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    return MUNIT_OK;
}

static MunitResult test_usend_urecv_probe(const MunitParameter params[],
                                          void*                data)
{
    (void)params;
    test_context*   context = (test_context*)data;
    na_return_t     ret;
    mona_instance_t mona = context->mona;

    char*  buf     = malloc(8192);
    size_t msg_len = 8192;

    if (context->rank == 0) { // sender
        int i;
        for (i = 0; i < (int)msg_len; i++) { buf[i] = i % 32; }
        int flag = 0;
        ret = mona_uiprobe(mona, MONA_ANY_ADDR, MONA_ANY_TAG, &flag, NULL, NULL,
                           NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);
        munit_assert_int(flag, ==, 0);

        ret = mona_usend(mona, buf, msg_len, context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);

        size_t      probe_size = 0;
        na_tag_t    probe_tag  = 0;
        mona_addr_t probe_addr = MONA_ADDR_NULL;
        while (flag == 0) {
            ret = mona_uiprobe(mona, MONA_ANY_ADDR, MONA_ANY_TAG, &flag,
                               &probe_size, &probe_addr, &probe_tag);
            munit_assert_int(ret, ==, NA_SUCCESS);
        }
        munit_assert_int(probe_size, ==, 64);
        munit_assert_int(probe_tag, ==, 1234);
        munit_assert_int(mona_addr_cmp(mona, probe_addr, context->other_addr),
                         ==, 1);
        mona_addr_free(mona, probe_addr);

        size_t recv_size;
        ret = mona_urecv(mona, buf, 64, context->other_addr, 1234, &recv_size,
                         NULL, NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);

        for (i = 0; i < 64; i++) { munit_assert_int(buf[i], ==, (i + 1) % 32); }

    } else { // receiver
        int i;

        size_t recv_size;
        ret = mona_urecv(mona, buf, msg_len, context->other_addr, 1234,
                         &recv_size, NULL, NULL);
        munit_assert_int(ret, ==, NA_SUCCESS);
        for (i = 0; i < (int)msg_len; i++) {
            munit_assert_int(buf[i], ==, i % 32);
        }

        for (i = 0; i < 64; i++) { buf[i] = (i + 1) % 32; }
        sleep(1);

        ret = mona_usend(mona, buf, 64, context->other_addr, 0, 1234);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    return MUNIT_OK;
}

static MunitResult test_only_probe(const MunitParameter params[], void* data)
{
    (void)params;
    test_context*   context = (test_context*)data;
    na_return_t     ret;
    mona_instance_t mona = context->mona;

    int flag = 0;
    ret = mona_uiprobe(mona, MONA_ANY_ADDR, MONA_ANY_TAG, &flag, NULL, NULL,
                       NULL);
    munit_assert_int(ret, ==, NA_SUCCESS);
    munit_assert_int(flag, ==, 0);

    return MUNIT_OK;
}

static MunitTest test_suite_tests[]
    = {{(char*)"/hl", test_usend_urecv, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {(char*)"/send_recv_probe", test_usend_urecv_probe, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {(char*)"/only_probe", test_only_probe, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/mona/usend-urecv", test_suite_tests, NULL, 1,
       MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    MPI_Init(NULL, NULL);
    ABT_init(0, NULL);
    int ret = munit_suite_main(&test_suite, (void*)"mona", argc, argv);
    ABT_finalize();
    MPI_Finalize();
    return ret;
}
