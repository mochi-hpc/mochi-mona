#include "munit/munit.h"
#include "mona.h"

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;

    ABT_init(0, NULL);

    return NULL;
}

static void test_context_tear_down(void* fixture)
{
    (void)fixture;

    ABT_finalize();
}

static MunitResult test_init_finalize(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;

    mona_instance_t mona = mona_init("na+sm", true, NULL);
    munit_assert_not_null(mona);

    mona_finalize(mona);
    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/init-finalize", test_init_finalize, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/mona/init", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

