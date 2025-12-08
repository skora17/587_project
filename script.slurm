#!/bin/bash
#SBATCH --job-name=test_priority
#SBATCH --account=cse587f25s001_class
#SBATCH --partition=standard
#SBATCH --nodes=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=output_%j.txt
#SBATCH --error=error_%j.txt

# Load GCC with OpenMP (standard environment)
module purge
module load gcc/11.2.0

# Compile
echo "Compiling..."
g++ -O3 -fopenmp -std=c++17 priority_struct.cpp -o ps_exe

# set thread count
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

echo "Running..."
./ps_exe
