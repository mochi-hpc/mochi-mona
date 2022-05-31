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

int MPI_Register_mona_comm(mona_comm_t comm, MPI_Comm* newcomm);

#ifdef __cplusplus
}
#endif

#endif
