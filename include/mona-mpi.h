/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_MPI_H
#define __MONA_MPI_H

#include <mona-mpi.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

MPI_Comm MPI_Register_mona_comm(mona_comm_t comm);

#ifdef __cplusplus
}
#endif

#endif
