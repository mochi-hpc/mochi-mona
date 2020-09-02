#include "munit/munit.h"
#include "mona.h"

typedef struct {
    mona_instance_t mona;
    na_addr_t self_addr;
} test_context;

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;

    int ret;

    ABT_init(0, NULL);
    mona_instance_t mona = mona_init("ofi+tcp", NA_TRUE, NULL);

    test_context* context = (test_context*)calloc(1, sizeof(*context));
    context->mona = mona;

    ret = mona_addr_self(mona, &(context->self_addr));
    munit_assert_int(ret, ==, NA_SUCCESS);

    return context;
}

static void test_context_tear_down(void* fixture)
{

    test_context* context = (test_context*)fixture;
    mona_addr_free(context->mona, context->self_addr);
    mona_finalize(context->mona);
    free(context);

    ABT_finalize();
}

static MunitResult test_send_recv_self(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    na_return_t ret;
    mona_instance_t mona = context->mona;

    na_size_t msg_len = 8192;
    char* buf1 = malloc(msg_len);
    char* buf2 = malloc(msg_len);

    mona_request_t req;

    ret = mona_irecv(mona, buf1, msg_len, context->self_addr, 1234, NULL, NULL, NULL, &req);
    munit_assert_int(ret, ==, NA_SUCCESS);

    ret = mona_send(mona, buf2, msg_len, context->self_addr, 0, 1234);
    munit_assert_int(ret, ==, NA_SUCCESS);

    ret = mona_wait(req);
    munit_assert_int(ret, ==, NA_SUCCESS);

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/hl", test_send_recv_self, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = { 
    (char*) "/mona/send-recv-self", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

