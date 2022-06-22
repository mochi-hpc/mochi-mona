#include <mpi.h>
#include "munit/munit.h"
#include "mona.h"
#include "mona-coll.h"
#include "mona-mpi.h"

typedef struct {
    mona_instance_t mona;
    mona_comm_t     mona_comm;
    MPI_Comm        mpi_comm;
} test_context;

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;

    int ret;

    MPI_Init(NULL, NULL);
    ABT_init(0, NULL);
    mona_instance_t mona = mona_init("ofi+tcp", true, NULL);

    na_addr_t self_addr;
    ret = mona_addr_self(mona, &self_addr);
    munit_assert_int(ret, ==, NA_SUCCESS);

    char self_addr_str[128];
    size_t self_addr_size = 128;
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

    mona_comm_t mona_comm;
    ret = mona_comm_create(mona, num_procs, other_addr, &mona_comm);
    munit_assert_int(ret, ==, NA_SUCCESS);

    for(i = 0; i < num_procs; i++) {
        ret = mona_addr_free(mona, other_addr[i]);
        munit_assert_int(ret, ==, NA_SUCCESS);
    }

    free(other_addr);

    MPI_Comm mpi_comm;
    MPI_Register_mona_comm(mona_comm, &mpi_comm);
    MPI_Mona_enable_logging();

    test_context* context = (test_context*)calloc(1, sizeof(*context));
    context->mona = mona;
    context->mona_comm = mona_comm;
    context->mpi_comm = mpi_comm;

    return context;
}

static void test_context_tear_down(void* fixture)
{
    MPI_Barrier(MPI_COMM_WORLD);

    test_context* context = (test_context*)fixture;
    MPI_Comm_free(&context->mpi_comm);
    mona_comm_free(context->mona_comm);
    mona_finalize(context->mona);
    free(context);

    ABT_finalize();
    MPI_Finalize();
}

static MunitResult test_waitany(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    int ret;

    ret = MPI_Barrier(context->mpi_comm);
    munit_assert_int(ret, ==, 0);

    int rank, size;
    ret = MPI_Comm_rank(context->mpi_comm, &rank);
    munit_assert_int(ret, ==, 0);
    ret = MPI_Comm_size(context->mpi_comm, &size);
    munit_assert_int(ret, ==, 0);

    char sendbuf[256];
    for(unsigned i=0; i < 256; i++) {
        sendbuf[i] = 'a' + ((rank+i) % 26);
    }

    MPI_Request sendreqs[size];
    for(int i=0; i < size; i++) {
        if(i == rank) {
            sendreqs[i] = MPI_REQUEST_NULL;
            continue;
        }
        ret = MPI_Isend(sendbuf, 256, MPI_BYTE, i, 0, context->mpi_comm, sendreqs+i);
        munit_assert_int(ret, ==, 0);
    }

    char recvbuf[256*size];
    memset(recvbuf, 0, 256*size);

    MPI_Request recvreqs[size];
    for(int i=0; i < size; i++) {
        if(i == rank) {
            recvreqs[i] = MPI_REQUEST_NULL;
            memcpy(recvbuf+i*256, sendbuf, 256);
            continue;
        }
        ret = MPI_Irecv(recvbuf+i*256, 256, MPI_BYTE, i, 0,
                        context->mpi_comm, recvreqs+i);
        munit_assert_int(ret, ==, 0);
    }

    MPI_Request sendreqs2[size];
    MPI_Request recvreqs2[size];
    memcpy(sendreqs2, sendreqs, sizeof(MPI_Request)*size);
    memcpy(recvreqs2, recvreqs, sizeof(MPI_Request)*size);

    for(int i=0; i < size-1; i++) {
        int index = 0;
        ret = MPI_Waitany(size, sendreqs, &index, MPI_STATUS_IGNORE);
        munit_assert_int(ret, ==, 0);
        sendreqs[index] = MPI_REQUEST_NULL;
    }

    for(int i=0; i < size-1; i++) {
        int index = 0;
        ret = MPI_Waitany(size, recvreqs, &index, MPI_STATUS_IGNORE);
        munit_assert_int(ret, ==, NA_SUCCESS);
        recvreqs[index] = MPI_REQUEST_NULL;
    }

    for(int i=0; i < size-1; i++) {
        if(sendreqs2[i] != MPI_REQUEST_NULL)
            MPI_Request_free(sendreqs2+i);
        if(recvreqs2[i] != MPI_REQUEST_NULL)
            MPI_Request_free(recvreqs2+i);
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

