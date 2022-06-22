/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MONA_MPI_H
#define __MONA_MPI_H

#include <mona-coll.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int MPI_Register_mona_comm(mona_comm_t comm, MPI_Comm* newcomm);

int MPI_Mona_enable_logging();

#ifdef __cplusplus
}
#endif

#endif
