#include <mpi.h>
#include "munit/munit.h"
#include <na.h>
#include <stdio.h>
#include <stdbool.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef struct {
    na_class_t*   na_class;
    na_context_t* na_context;
    int           rank;
    na_addr_t*    self_addr;
    na_addr_t*    other_addr;
    na_op_id_t*   op_id;
    size_t        msg_len;
    char*         buf;
    void*         plugin_data;
    int           stop;
} test_context;

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;

    na_return_t ret;

    MPI_Init(NULL, NULL);

    test_context* context = (test_context*)calloc(1, sizeof(*context));
    context->na_class = NA_Initialize_opt("ofi+tcp", true, NULL);
    context->na_context = NA_Context_create(context->na_class);

    MPI_Comm_rank(MPI_COMM_WORLD, &(context->rank));

    ret = NA_Addr_self(context->na_class, &(context->self_addr));
    munit_assert_int(ret, ==, NA_SUCCESS);

    char self_addr_str[128];
    size_t self_addr_size = 128;
    ret = NA_Addr_to_string(context->na_class, self_addr_str, &self_addr_size, context->self_addr);
    munit_assert_int(ret, ==, NA_SUCCESS);

    char other_addr_str[128];

    MPI_Sendrecv(self_addr_str, 128, MPI_BYTE, (context->rank + 1) % 2, 0,
                 other_addr_str, 128, MPI_BYTE, (context->rank + 1) % 2, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    ret = NA_Addr_lookup(context->na_class, other_addr_str, &(context->other_addr));
    munit_assert_int(ret, ==, NA_SUCCESS);

    context->msg_len = MIN(NA_Msg_get_max_expected_size(context->na_class),
                         2*NA_Msg_get_max_unexpected_size(context->na_class));
    context->buf = (char*)NA_Msg_buf_alloc(context->na_class,
            context->msg_len, 0, &(context->plugin_data));

    context->op_id = NA_Op_create(context->na_class, 0);

    return context;
}

static void test_context_tear_down(void* fixture)
{
    MPI_Barrier(MPI_COMM_WORLD);

    test_context* context = (test_context*)fixture;
    NA_Addr_free(context->na_class, context->self_addr);
    NA_Addr_free(context->na_class, context->other_addr);

    NA_Msg_buf_free(context->na_class, context->buf, context->plugin_data);

    NA_Op_destroy(context->na_class, context->op_id);

    NA_Context_destroy(context->na_class, context->na_context);
    NA_Finalize(context->na_class);
    free(context);

    MPI_Finalize();
}

static void sender_callback(const struct na_cb_info *info) {
    test_context* context = (test_context*)(info->arg);
    context->stop = 1;
}

static void receiver_callback(const struct na_cb_info *info) {
    test_context* context = (test_context*)(info->arg);
    context->stop = 1;
}

static MunitResult test_send_recv_expected(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    na_return_t ret;

    if(context->rank == 0) { // sender

        int i;
        for(i = NA_Msg_get_expected_header_size(context->na_class); i < (int)context->msg_len; i++) {
            context->buf[i] = i % 32;
        }
        ret = NA_Msg_init_expected(context->na_class, context->buf, context->msg_len);
        munit_assert_int(ret, ==, NA_SUCCESS);

        MPI_Barrier(MPI_COMM_WORLD);

        ret = NA_Msg_send_expected(
                context->na_class,
                context->na_context,
                sender_callback,
                context,
                context->buf,
                context->msg_len,
                context->plugin_data,
                context->other_addr,
                0, 0, context->op_id);
        munit_assert_int(ret, ==, NA_SUCCESS);

    } else { // receiver

        ret = NA_Msg_recv_expected(
                context->na_class,
                context->na_context,
                receiver_callback,
                context,
                context->buf,
                context->msg_len,
                context->plugin_data,
                context->other_addr,
                0, 0, context->op_id);
        munit_assert_int(ret, ==, NA_SUCCESS);

        MPI_Barrier(MPI_COMM_WORLD);

    }

    // progress loop
    while(!context->stop) {
        unsigned int actual_count = 0;
        na_return_t trigger_ret;
        do {
            trigger_ret = NA_Trigger(context->na_context, 1, &actual_count);
        } while ((trigger_ret == NA_SUCCESS) && actual_count && !context->stop);

        ret = NA_Progress(context->na_class, context->na_context, 0);
        if (ret != NA_SUCCESS && ret != NA_TIMEOUT) {
            fprintf(stderr, "WARNING: unexpected return value from NA_Progress (%d)\n", ret);
        }
    }

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/expected", test_send_recv_expected, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = { 
    (char*) "/mona/na", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

