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
    na_bool_t no_wait;
    na_size_t rdma_threshold;
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

    struct na_init_info info = {0};
    if(options->no_wait)
        info.progress_mode = NA_NO_BLOCK;

    mona_instance_t mona = mona_init_thread(options->transport, NA_TRUE, &info, options->use_progress_thread);
    ASSERT_MESSAGE(mona != MONA_INSTANCE_NULL, "Could not initialize Mona instance");

    mona_hint_set_rdma_threshold(mona, options->rdma_threshold);

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
        mona_request_t req;
        mona_irecv(mona, buf, options->msg_size, addr, 0, NULL, &req);
        mona_send(mona, buf, options->msg_size, addr, 0, 0);
        mona_wait(req);
    } else {
        mona_recv(mona, buf, options->msg_size, addr, 0, NULL);
        mona_send(mona, buf, options->msg_size, addr, 0, 0);
    }

    // benchmark
    MPI_Barrier(MPI_COMM_WORLD);
    t_start = MPI_Wtime();

    for(i = 0; i < options->iterations; i++) {
        if(i % 2 == (unsigned)rank) {
            mona_send(mona, buf, options->msg_size, addr, 0, 0);
        } else {
            mona_recv(mona, buf, options->msg_size, addr, 0, NULL);
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

struct na_benchmark_state {
    int           rank;
    na_class_t*   na_class;
    na_context_t* na_context;
    na_size_t     header_size;
    na_size_t     max_msg_size;
    na_addr_t     peer_addr;
    na_op_id_t*   op_id;
    na_size_t     data_size;
    char*         data;
    char*         msg;
    void*         plugin_data;
    na_bool_t     should_stop;
    uint64_t      remaining;
};

static int na_send_cb(const struct na_cb_info* info);
static int na_recv_cb(const struct na_cb_info* info);

static void na_post_send(struct na_benchmark_state* state) {
    na_return_t ret;
    int rank = state->rank;
    na_size_t msg_size = state->data_size + state->header_size;
    if(msg_size > state->max_msg_size)
        msg_size = state->max_msg_size;
    memcpy(state->msg + state->header_size, state->data, msg_size - state->header_size);
    ret = NA_Msg_init_expected(state->na_class, state->msg, state->data_size + state->header_size);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "NA_Msg_init_expected failed");
    ret = NA_Msg_send_expected(state->na_class, state->na_context, na_send_cb, state,
                               state->msg, msg_size, state->plugin_data, state->peer_addr,
                               0, 0, state->op_id);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "NA_Msg_send_expected failed");
}

static void na_post_recv(struct na_benchmark_state* state) {
    na_return_t ret;
    int rank = state->rank;
    na_size_t msg_size = state->data_size + state->header_size;
    if(msg_size > state->max_msg_size)
        msg_size = state->max_msg_size;
    ret = NA_Msg_init_expected(state->na_class, state->msg, state->data_size + state->header_size);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "NA_Msg_init_expected failed");
    ret = NA_Msg_recv_expected(state->na_class, state->na_context, na_recv_cb, state,
                               state->msg, msg_size, state->plugin_data, state->peer_addr,
                               0, 0, state->op_id);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "NA_Msg_recv_expected failed");
}

static int na_send_cb(const struct na_cb_info* info) {
    struct na_benchmark_state* state = (struct na_benchmark_state*)info->arg;
    state->remaining -= 1;
    if(state->remaining > 0) {
        na_post_recv(state);
    } else {
        state->should_stop = NA_TRUE;
    }
}

static int na_recv_cb(const struct na_cb_info* info) {
    struct na_benchmark_state* state = (struct na_benchmark_state*)info->arg;
    state->remaining -= 1;
    na_size_t to_copy = state->data_size;
    if(to_copy > state->max_msg_size - state->header_size)
        to_copy = state->max_msg_size - state->header_size;
    memcpy(state->data, state->msg + state->header_size, to_copy);
    if(state->remaining > 0) {
        na_post_send(state);
    } else {
        state->should_stop = NA_TRUE;
    }
}

static void run_na_benchmark(options_t* options) {

    na_return_t ret;
    double t_start, t_end;
    struct na_benchmark_state state = {0};
    MPI_Comm_rank(MPI_COMM_WORLD, &state.rank);
    int rank = state.rank;

    struct na_init_info info = {0};
    if(options->no_wait)
        info.progress_mode = NA_NO_BLOCK;

    state.na_class = NA_Initialize_opt(options->transport, NA_TRUE, &info);
    ASSERT_MESSAGE(state.na_class, "Could not initialize NA class");

    state.na_context = NA_Context_create(state.na_class);
    ASSERT_MESSAGE(state.na_context, "Could not initialize NA context");

    char addr_str[128];
    na_size_t addr_size = 128;
    na_addr_t addr = NA_ADDR_NULL;
    ret = NA_Addr_self(state.na_class, &addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not get self address");

    ret = NA_Addr_to_string(state.na_class, addr_str, &addr_size, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not convert address to string");

    ret = NA_Addr_free(state.na_class, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");

    char other_addr_str[128];
    MPI_Sendrecv(addr_str, 128, MPI_BYTE, (rank+1)%2, 0,
                 other_addr_str, 128, MPI_BYTE, (rank+1)%2, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    ret = NA_Addr_lookup(state.na_class, other_addr_str, &addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not lookup address");

    state.header_size  = NA_Msg_get_expected_header_size(state.na_class);
    state.max_msg_size = NA_Msg_get_max_expected_size(state.na_class);
    state.data         = malloc(options->msg_size);
    state.remaining    = options->iterations;
    state.data_size    = options->msg_size;
    state.peer_addr    = addr;
    state.op_id        = NA_Op_create(state.na_class);
    state.msg          = NA_Msg_buf_alloc(state.na_class, state.max_msg_size, &state.plugin_data);

    // benchmark
    MPI_Barrier(MPI_COMM_WORLD);
    t_start = MPI_Wtime();

    if(rank == 0) {
        na_post_send(&state);
    } else {
        na_post_recv(&state);
    }

    while(!state.should_stop) {
        unsigned int actual_count;
        do {
            ret = NA_Trigger(state.na_context, 0, 1, NULL, &actual_count);
        } while((ret == NA_SUCCESS) && actual_count && !state.should_stop);

        ret = NA_Progress(state.na_class, state.na_context, 0);
        ASSERT_MESSAGE(ret == NA_SUCCESS || ret == NA_TIMEOUT,
            "NA_Progress failed");
    }

    MPI_Barrier(MPI_COMM_WORLD);
    t_end = MPI_Wtime();

    free(state.data);
    ret = NA_Op_destroy(state.na_class, state.op_id);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not destroy operation id");
    ret = NA_Msg_buf_free(state.na_class, state.msg, state.plugin_data);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free message buffer");
    ret = NA_Addr_free(state.na_class, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");

    NA_Context_destroy(state.na_class, state.na_context);
    NA_Finalize(state.na_class);

    if(rank == 0) printf("NA: %d send/recv pairs of %d bytes executed in %lf sec\n",
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
    options->no_wait = NA_FALSE;
    options->rdma_threshold = (na_size_t)(-1);

    while((c = getopt(argc, argv, "r:i:s:m:t:pn")) != -1) {
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
            case 'r':
                options->rdma_threshold = atoi(optarg);
                break;
            case 'n':
                options->no_wait = NA_TRUE;
                break;
            case '?':
                if(optopt == 'i' || optopt == 's' || optopt == 'm' || optopt == 'r')
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
    } else if(options.method && strcmp(options.method, "na") == 0) {
        run_na_benchmark(&options);
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
