#!/bin/bash
#SBATCH --job-name=MoNA-Bench
#SBATCH --qos=debug
#SBATCH --time=30:00
#SBATCH --constraint=haswell
#SBATCH --output="benchmark-%j.out"

export MPICH_GNI_NDREG_ENTRIES=1024

HERE=$SLURM_SUBMIT_DIR
source $HERE/settings.sh

NUM_NODES=$SLURM_JOB_NUM_NODES
NUM_PROCS=$(($NUM_NODES*32))

TRANSPORT=ofi+gni

LOG_DIR=logs-$SLURM_JOB_ID
mkdir $LOG_DIR

LOG=$LOG_DIR/result.$SLURM_JOB_ID

function print_log() {
    MSG=$1
    NOW=`date +"%Y-%m-%d %T.%N"`
    echo "[$NOW] $MSG"
}

print_log "Loading spack"
. $MONA_SPACK_LOCATION/share/spack/setup-env.sh

print_log "Loading spack environment"
spack env activate $MONA_SPACK_ENV

# Send/recv benchmarks

for MSG_SIZE in ${MONA_SEND_RECV_MSG_SIZES[@]}; do

    print_log "Starting Send/Recv benchmark using MPI with message size = ${MSG_SIZE}"
    srun -C haswell -N 2 -n 2 -c 1 -l --cpu_bind=cores \
        mona-send-recv-benchmark \
        -m mpi -s $MSG_SIZE -i $MONA_SEND_RECV_ITERATIONS >> $LOG.pt2pt.mpi

    print_log "Starting Send/Recv benchmark using MoNA with message size = ${MSG_SIZE}"
    srun -C haswell -N 2 -n 2 -c 1 -l --cpu_bind=cores \
        mona-send-recv-benchmark \
        -m mona -t $TRANSPORT -s $MSG_SIZE -i $MONA_SEND_RECV_ITERATIONS >> $LOG.pt2pt.mona
done

# Broadcast benchmarks

for MSG_SIZE in ${MONA_BCAST_MSG_SIZES[@]}; do

    print_log "Starting Broadcast benchmark using MPI with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-bcast-benchmark \
        -m mpi -s $MSG_SIZE -i $MONA_BCAST_ITERATIONS >> $LOG.bcast.mpi

    print_log "Starting Broadcast benchmark using MoNA with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-bcast-benchmark \
        -m mona -t $TRANSPORT -s $MSG_SIZE -i $MONA_BCAST_ITERATIONS >> $LOG.bcast.mona
done

# Allreduce benchmarks

for MSG_SIZE in ${MONA_ALLREDUCE_MSG_SIZES[@]}; do

    print_log "Starting Allreduce benchmark using MPI with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-allreduce-benchmark \
        -m mpi -s $MSG_SIZE -i $MONA_ALLREDUCE_ITERATIONS >> $LOG.allreduce.mpi

    print_log "Starting Allreduce benchmark using MoNA with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-allreduce-benchmark \
        -m mona -t $TRANSPORT -s $MSG_SIZE -i $MONA_ALLREDUCE_ITERATIONS >> $LOG.allreduce.mona
done

# Gather benchmarks

for MSG_SIZE in ${MONA_GATHER_MSG_SIZES[@]}; do

    print_log "Starting Gather benchmark using MPI with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-gather-benchmark \
        -m mpi -s $MSG_SIZE -i $MONA_GATHER_ITERATIONS >> $LOG.gather.mpi

    print_log "Starting Gather benchmark using MoNA with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-gather-benchmark \
        -m mona -t $TRANSPORT -s $MSG_SIZE -i $MONA_GATHER_ITERATIONS >> $LOG.gather.mona
done

# Allgather benchmarks

for MSG_SIZE in ${MONA_ALLGATHER_MSG_SIZES[@]}; do

    print_log "Starting Allgather benchmark using MPI with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-allgather-benchmark \
        -m mpi -s $MSG_SIZE -i $MONA_ALLGATHER_ITERATIONS >> $LOG.allgather.mpi

    print_log "Starting Allgather benchmark using MoNA with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-allgather-benchmark \
        -m mona -t $TRANSPORT -s $MSG_SIZE -i $MONA_ALLGATHER_ITERATIONS >> $LOG.allgather.mona
done

for MSG_SIZE in ${MONA_SCATTER_MSG_SIZES[@]}; do

    print_log "Starting Scatter benchmark using MPI with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-scatter-benchmark \
        -m mpi -s $MSG_SIZE -i $MONA_SCATTER_ITERATIONS >> $LOG.scatter.mpi

    print_log "Starting Scatter benchmark using MoNA with message size = ${MSG_SIZE}"
    srun -C haswell -N $NUM_NODES -n $NUM_PROCS -c 1 -l --cpu_bind=cores \
        mona-scatter-benchmark \
        -m mona -t $TRANSPORT -s $MSG_SIZE -i $MONA_SCATTER_ITERATIONS >> $LOG.scatter.mona
done

print_log "Benchmarks completed"
