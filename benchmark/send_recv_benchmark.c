#include <mpi.h>
#include <mona.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT_MESSAGE(__cond__, __msg__) \
    if(!(__cond__)) { \
        fprintf(stderr, "[%d] Assertion failed (%s): %s\n", rank, #__cond__, __msg__); \
        exit(-1); \
    }

typedef struct options_t {
    char*    method;
    char*    transport;
    unsigned iterations;
    unsigned msg_size;
    na_bool_t use_progress_thread;
} options_t;

static void run_mpi_benchmark(options_t* options) {

    int rank, size;
    unsigned i;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    double t_start, t_end;

    char* buf = malloc(options->msg_size);

    // warm up
    if(rank % 2 == 0) {
        MPI_Send(buf, options->msg_size, MPI_BYTE, (rank+1)%2, 0, MPI_COMM_WORLD);
        MPI_Recv(buf, options->msg_size, MPI_BYTE, (rank+1)%2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        MPI_Recv(buf, options->msg_size, MPI_BYTE, (rank+1)%2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(buf, options->msg_size, MPI_BYTE, (rank+1)%2, 0, MPI_COMM_WORLD);
    }

    // benchmark
    MPI_Barrier(MPI_COMM_WORLD);
    t_start = MPI_Wtime();

    for(i = 0; i < options->iterations; i++) {
        if(i % 2 == (unsigned)rank) {
            MPI_Send(buf, options->msg_size, MPI_BYTE, (rank+1)%2, 0, MPI_COMM_WORLD);
        } else {
            MPI_Recv(buf, options->msg_size, MPI_BYTE, (rank+1)%2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    t_end = MPI_Wtime();

    free(buf);
    if(rank == 0) printf("MPI: %d send/recv pairs of %d bytes executed in %lf sec\n",
            options->iterations, options->msg_size, (t_end-t_start));
}

static void run_mona_benchmark(options_t* options) {

    na_return_t ret;
    double t_start, t_end;
    int rank;
    unsigned i;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    mona_instance_t mona = mona_init_thread(options->transport, NA_TRUE, NULL, options->use_progress_thread);
    ASSERT_MESSAGE(mona != MONA_INSTANCE_NULL, "Could not initialize Mona instance");

    char addr_str[128];
    na_size_t addr_size = 128;
    na_addr_t addr = NA_ADDR_NULL;
    ret = mona_addr_self(mona, &addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not get self address");

    ret = mona_addr_to_string(mona, addr_str, &addr_size, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not convert address to string");

    ret = mona_addr_free(mona, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");


    char other_addr_str[128];
    MPI_Sendrecv(addr_str, 128, MPI_BYTE, (rank+1)%2, 0,
                 other_addr_str, 128, MPI_BYTE, (rank+1)%2, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    ret = mona_addr_lookup(mona, other_addr_str, &addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not lookup address");

    char* buf = malloc(options->msg_size);

    // warm up
    if(rank % 2 == 0) {
        mona_send(mona, buf, options->msg_size, addr, 0, 0);
        mona_recv(mona, buf, options->msg_size, addr, 0, NULL, NULL, NULL);
    } else {
        mona_recv(mona, buf, options->msg_size, addr, 0, NULL, NULL, NULL);
        mona_send(mona, buf, options->msg_size, addr, 0, 0);
    }

    // benchmark
    MPI_Barrier(MPI_COMM_WORLD);
    t_start = MPI_Wtime();

    for(i = 0; i < options->iterations; i++) {
        if(i % 2 == (unsigned)rank) {
            mona_send(mona, buf, options->msg_size, addr, 0, 0);
        } else {
            mona_recv(mona, buf, options->msg_size, addr, 0, NULL, NULL, NULL);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    t_end = MPI_Wtime();

    free(buf);
    ret = mona_addr_free(mona, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");

    mona_finalize(mona);
    if(rank == 0) printf("MoNA: %d send/recv pairs of %d bytes executed in %lf sec\n",
            options->iterations, options->msg_size, (t_end-t_start));
}

static void parse_options(int argc, char** argv, options_t* options) {

    int c;

    static const char* default_transport = "na+sm";

    opterr = 0;
    options->iterations = 1024;
    options->msg_size = 128;
    options->transport = (char*)default_transport;
    options->method = NULL;
    options->use_progress_thread = NA_FALSE;

    while((c = getopt(argc, argv, "i:s:m:t:p")) != -1) {
        switch (c)
        {
            case 'i':
                options->iterations = atoi(optarg);
                break;
            case 's':
                options->msg_size = atoi(optarg);
                break;
            case 'm':
                options->method = optarg;
                break;
            case 't':
                options->transport = optarg;
                break;
            case 'p':
                options->use_progress_thread = NA_TRUE;
                break;
            case '?':
                if(optopt == 'i' || optopt == 's' || optopt == 'm')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if(isprint (optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr,
                           "Unknown option character `\\x%x'.\n",
                           optopt);
                abort();
            default:
                abort();
        }
    }
}

int main(int argc, char** argv) {

    int rank, size;

    options_t options = { 0 };
    parse_options(argc, argv, &options);

    MPI_Init(&argc, &argv);
    ABT_init(argc, argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if(size != 2) {
        if(rank == 0) fprintf(stderr, "This benchmark is meant to run with 2 processes\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    if(options.method && strcmp(options.method, "mpi") == 0) {
        run_mpi_benchmark(&options);
    } else if(options.method && strcmp(options.method, "mona") == 0) {
        run_mona_benchmark(&options);
    } else {
        if(rank == 0) {
            fprintf(stderr, "Unknown benchmark method %s\n", options.method);
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
    }

    ABT_finalize();
    MPI_Finalize();

    return 0;
}
