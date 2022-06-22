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

static MunitResult test_gatherv(const MunitParameter params[], void* data)
{
    (void)params;
    test_context* context = (test_context*)data;
    int ret;

    int rank, size, i;
    ret = MPI_Comm_size(context->mpi_comm, &size);
    munit_assert_int(ret, ==, 0);
    ret = MPI_Comm_rank(context->mpi_comm, &rank);
    munit_assert_int(ret, ==, 0);

    char* send_buf = malloc(8096+rank*64);
    char* recv_buf = malloc(8096*size+32*size*(size-1));
    int* recv_sizes = malloc(size*sizeof(int));
    int* offsets = malloc(size*sizeof(int));

    for(i = 0; i < size; i++) {
        recv_sizes[i] = 8096+i*64;
        offsets[i] = i == 0 ? 0 : offsets[i-1] + recv_sizes[i-1];
    }

    for(i = 0; i < 8096+rank*64; i++) {
        send_buf[i] = rank;
    }

    // gatherv to rank 0
    ret = MPI_Gatherv(send_buf, 8096+rank*64, MPI_BYTE,
                      recv_buf, recv_sizes, offsets, MPI_BYTE,
                      0, context->mpi_comm);
    munit_assert_int(ret, ==, 0);

    if(rank == 0) {
        int j = 0, k = 0;
        for(i = 0; i < size; i++) {
            for(j = 0; j < 8096+i*64; j++, k++) {
                munit_assert_int(recv_buf[k], ==, i);
            }
        }
    }

    // gatherv to another root
    int root = size/2;
    ret = MPI_Gatherv(send_buf, 8096+rank*64, MPI_BYTE,
                      recv_buf, recv_sizes, offsets, MPI_BYTE,
                      root, context->mpi_comm);
    munit_assert_int(ret, ==, 0);

    if(rank == root) {
        int j = 0, k = 0;
        for(i = 0; i < size; i++) {
            for(j = 0; j < 8096+i*64; j++, k++) {
                munit_assert_int(recv_buf[k], ==, i);
            }
        }
    }

    free(send_buf);
    free(recv_buf);
    free(recv_sizes);
    free(offsets);

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/gatherv", test_gatherv, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/mona/mpi/collectives", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, (void*) "mona", argc, argv);
}
