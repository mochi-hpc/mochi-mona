#include <mpi.h>
#include <mona.h>
#include <mona-coll.h>
#include <stdlib.h>

#define ASSERT_MESSAGE(__cond__, __msg__)                                    \
    if (!(__cond__)) {                                                       \
        fprintf(stderr, "[%d] Assertion failed (%s): %s\n", rank, #__cond__, \
                __msg__);                                                    \
        exit(-1);                                                            \
    }

int main(int argc, char** argv)
{

    int         rank, size, i;
    na_return_t ret;

    MPI_Init(&argc, &argv);
    ABT_init(argc, argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    mona_instance_t mona = mona_init("ofi+tcp", true, NULL);
    ASSERT_MESSAGE(mona != MONA_INSTANCE_NULL,
                   "Could not initialize Mona instance");
    printf("[%d] Correctly instanciated Mona\n", rank);

    char        addr_str[128];
    size_t      addr_size = 128;
    mona_addr_t addr      = MONA_ADDR_NULL;
    ret                   = mona_addr_self(mona, &addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not get self address");

    ret = mona_addr_to_string(mona, addr_str, &addr_size, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not convert address to string");
    printf("[%d] My address is %s\n", rank, addr_str);

    ret = mona_addr_free(mona, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");

    char*        all_addr_str = malloc(128 * size);
    mona_addr_t* all_addr     = malloc(sizeof(mona_addr_t) * size);

    MPI_Allgather(addr_str, 128, MPI_BYTE, all_addr_str, 128, MPI_BYTE,
                  MPI_COMM_WORLD);

    for (i = 0; i < size; i++) {
        ret = mona_addr_lookup(mona, all_addr_str + (128 * i), all_addr + i);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not lookup address");
    }

    mona_comm_t comm;
    ret = mona_comm_create(mona, size, all_addr, &comm);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not create communicator");
    printf("[%d] Communicator created successfully\n", rank);

    ret = mona_comm_barrier(comm, 0);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Barrier failed");

    ret = mona_comm_free(comm);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free communicator");

    for (i = 0; i < size; i++) { mona_addr_free(mona, all_addr[i]); }

    mona_finalize(mona);

    free(all_addr);
    free(all_addr_str);

    ABT_finalize();
    MPI_Finalize();

    return 0;
}
