#include <mpi.h>
#include <mona.h>
#include <stdlib.h>

#define ASSERT_MESSAGE(__cond__, __msg__) \
    if(!(__cond__)) { \
        fprintf(stderr, "[%d] Assertion failed (%s): %s\n", rank, #__cond__, __msg__); \
        exit(-1); \
    }

int main(int argc, char** argv) {

    int rank, size, i;
    na_return_t ret;

    MPI_Init(&argc, &argv);
    ABT_init(argc, argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    mona_instance_t mona = mona_init("na+sm", true, NULL);
    ASSERT_MESSAGE(mona != MONA_INSTANCE_NULL, "Could not initialize Mona instance");
    printf("[%d] Correctly instanciated Mona\n", rank);

    char addr_str[128];
    size_t addr_size = 128;
    na_addr_t addr = NA_ADDR_NULL;
    ret = mona_addr_self(mona, &addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not get self address");

    ret = mona_addr_to_string(mona, addr_str, &addr_size, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not convert address to string");
    printf("[%d] My address is %s\n", rank, addr_str);

    ret = mona_addr_free(mona, addr);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");

    size_t msg_len = mona_msg_get_max_unexpected_size(mona);
    printf("[%d] Message size: %ld\n", rank, msg_len);

    void* plugin_data = NULL;
    char* buf = (char*)mona_msg_buf_alloc(mona, msg_len, &plugin_data);
    ASSERT_MESSAGE(buf != NULL, "Could not allocate message buffer");

    if(rank == 0) {
        MPI_Recv(addr_str, 128, MPI_CHAR, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        ret = mona_addr_lookup(mona, addr_str, &addr);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not lookup address");

        printf("[0] Sending message to rank 1\n");

        for(i = mona_msg_get_unexpected_header_size(mona); i < (int)msg_len; i++) {
            buf[i] = i % 32;
        }

        ret = mona_msg_init_unexpected(mona, buf, msg_len);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not initialize message");

        ret = mona_msg_send_unexpected(
                mona, buf, msg_len, plugin_data, addr, 0, 1234);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not send message");

        ret = mona_addr_free(mona, addr);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free address");

    } else {
        MPI_Send(addr_str, 128, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

        na_addr_t source_addr = NA_ADDR_NULL;
        na_tag_t  tag = 0;
        size_t actual_size = 0;

        ret = mona_msg_recv_unexpected(
                mona, buf, msg_len, plugin_data,
                &source_addr, &tag, &actual_size);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not receive message");

        printf("[1] Receiving message from rank 0 with tag %d and size %ld\n", tag, actual_size);

        ret = mona_addr_free(mona, source_addr);
        ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free source address");

        for(i = mona_msg_get_unexpected_header_size(mona); i < (int)msg_len; i++) {
            ASSERT_MESSAGE(buf[i] == i % 32, "Incorrect byte received");
        }
    }

    ret = mona_msg_buf_free(mona, buf, plugin_data);
    ASSERT_MESSAGE(ret == NA_SUCCESS, "Could not free buffer");

    mona_finalize(mona);

    ABT_finalize();
    MPI_Finalize();

    return 0;
}
