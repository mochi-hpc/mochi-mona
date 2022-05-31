#include <mpi.h>
#include "munit/munit.h"
#include "mona.h"
#include "mona-coll.h"
#include "mona-mpi.h"

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

static MunitResult test_send_recv(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    na_return_t ret;
    mona_instance_t mona = context->mona;

    int i;
    char* buf = malloc(8192);
    size_t msg_len = 8192;
    mona_comm_t mona_comm;
    MPI_Comm mpi_comm;
    na_addr_t comm_addrs[2];
    comm_addrs[0] = context->rank == 0 ? context->self_addr : context->other_addr;
    comm_addrs[1] = context->rank == 0 ? context->other_addr : context->self_addr;

    ret = mona_comm_create(mona, 2, comm_addrs, &mona_comm);
    munit_assert_int(ret, ==, NA_SUCCESS);

    int mpi_ret = MPI_Register_mona_comm(mona_comm, &mpi_comm);
    munit_assert_int(mpi_ret, ==, 0);

    if(context->rank == 0) { // sender
        for(i = 0; i < (int)msg_len; i++) {
            buf[i] = i % 32;
        }

        mpi_ret = MPI_Send(buf, msg_len, MPI_BYTE, 1, 1234, mpi_comm);
        munit_assert_int(mpi_ret, ==, 0);

        MPI_Status status;
        mpi_ret = MPI_Recv(buf, 64, MPI_BYTE, 1, 1234, mpi_comm, &status);
        munit_assert_int(mpi_ret, ==, 0);

        for(i = 0; i < 64; i++) {
            munit_assert_int(buf[i], ==, (i+1) % 32);
        }

    } else { // receiver

        MPI_Status status;
        mpi_ret = MPI_Recv(buf, msg_len, MPI_BYTE, 0, 1234, mpi_comm, &status);
        munit_assert_int(mpi_ret, ==, 0);
        for(i = 0; i < (int)msg_len; i++) {
            munit_assert_int(buf[i], ==, i % 32);
        }

        for(i=0; i < 64; i++) {
            buf[i] = (i+1) % 32;
        }

        mpi_ret = MPI_Send(buf, 64, MPI_BYTE, 0, 1234, mpi_comm);
        munit_assert_int(mpi_ret, ==,0);
    }

    // test sendrecv function
    memset(buf, 0, msg_len);
    for(i = 0; i < (int)msg_len/2; i++) {
        int j = i + context->rank*msg_len/2;
        buf[j] = 'A' + context->rank;
    }

    MPI_Status status;
    mpi_ret = MPI_Sendrecv(buf + context->rank*msg_len/2, // sendbuf
                           msg_len/2,                     // sendcount
                           MPI_BYTE,                      // sendtype
                           (context->rank + 1) % 2,       // dest
                           1234,                          // sendtag
                           buf + ((context->rank + 1)%2)*msg_len/2, // recvbuf
                           msg_len/2,                     // recvcount
                           MPI_BYTE,                      // recvtype
                           (context->rank + 1) % 2,       // source
                           1234,                          // recvtag
                           mpi_comm,                      // comm
                           &status);                      // status
    munit_assert_int(mpi_ret, ==, 0);

    for(i = 0; i < (int)msg_len; i++) {
        char expected = i < (int)msg_len/2 ? 'A' : 'B';
        munit_assert_int(buf[i], ==, expected);
    }

    mpi_ret = MPI_Comm_free(&mpi_comm);
    munit_assert_int(mpi_ret, ==, 0);

    ret = mona_comm_free(mona_comm);
    munit_assert_int(ret, ==, NA_SUCCESS);
    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/send-recv", test_send_recv, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/mona/mpi/collectives", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

