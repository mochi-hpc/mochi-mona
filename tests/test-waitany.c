#include <mpi.h>
#include "munit/munit.h"
#include "mona.h"
#include "mona-coll.h"

typedef struct {
    mona_instance_t mona;
    mona_comm_t     comm;
} test_context;

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;

    int ret;

    MPI_Init(NULL, NULL);
    ABT_init(0, NULL);
    mona_instance_t mona = mona_init("ofi+tcp", NA_TRUE, NULL);

    na_addr_t self_addr;
    ret = mona_addr_self(mona, &self_addr);
    munit_assert_int(ret, ==, NA_SUCCESS);

    char self_addr_str[128];
    na_size_t self_addr_size = 128;
    ret = mona_addr_to_string(mona, self_addr_str, &self_addr_size, self_addr);
    munit_assert_int(ret, ==, NA_SUCCESS);

    int num_procs;
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    char* other_addr_str = malloc(128*num_procs);

    MPI_Allgather(self_addr_str, 128, MPI_BYTE,
                  other_addr_str, 128, MPI_BYTE,
                  MPI_COMM_WORLD);

    na_addr_t* other_addr = malloc(num_procs*sizeof(*other_addr));

    int i;
    for(i = 0; i < num_procs; i++) {
        ret = mona_addr_lookup(mona, other_addr_str + 128*i, other_addr + i);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    free(other_addr_str);

    mona_comm_t comm;
    ret = mona_comm_create(mona, num_procs, other_addr, &comm);
    munit_assert_int(ret, ==, NA_SUCCESS);

    for(i = 0; i < num_procs; i++) {
        ret = mona_addr_free(mona, other_addr[i]);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    free(other_addr);

    test_context* context = (test_context*)calloc(1, sizeof(*context));
    context->mona = mona;
    context->comm = comm;

    return context;
}

static void test_context_tear_down(void* fixture)
{
    MPI_Barrier(MPI_COMM_WORLD);

    test_context* context = (test_context*)fixture;
    mona_comm_free(context->comm);
    mona_finalize(context->mona);
    free(context);

    ABT_finalize();
    MPI_Finalize();
}

static MunitResult test_waitany(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    na_return_t ret;

    ret = mona_comm_barrier(context->comm, 1234);
    munit_assert_int(ret, ==, NA_SUCCESS);

    int rank, size;
    ret = mona_comm_rank(context->comm, &rank);
    munit_assert_int(ret, ==, NA_SUCCESS);
    ret = mona_comm_size(context->comm, &size);
    munit_assert_int(ret, ==, NA_SUCCESS);

    char sendbuf[256];
    for(unsigned i=0; i < 256; i++) {
        sendbuf[i] = 'a' + ((rank+i) % 26);
    }

    mona_request_t sendreqs[size];
    for(int i=0; i < size; i++) {
        if(i == rank) {
            sendreqs[i] = MONA_REQUEST_NULL;
            continue;
        }
        ret = mona_comm_isend(context->comm, sendbuf, 256, i, 0, sendreqs+i);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    char recvbuf[256*size];
    memset(recvbuf, 0, 256*size);

    mona_request_t recvreqs[size];
    for(int i=0; i < size; i++) {
        if(i == rank) {
            recvreqs[i] = MONA_REQUEST_NULL;
            memcpy(recvbuf+i*256, sendbuf, 256);
            continue;
        }
        ret = mona_comm_irecv(context->comm, recvbuf+i*256, 256, i, 0,
                              NULL, recvreqs+i);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    for(int i=0; i < size-1; i++) {
        size_t index;
        ret = mona_wait_any(size, sendreqs, &index);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    for(int i=0; i < size-1; i++) {
        size_t index;
        ret = mona_wait_any(size, recvreqs, &index);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    for(int i=0; i < size; i++) {
        for(int j=0; j < 256; j++) {
            munit_assert_int(recvbuf[i*256+j], ==, 'a' + ((i+j) % 26));
        }
    }

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/waitany", test_waitany, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/mona/collectives", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}

