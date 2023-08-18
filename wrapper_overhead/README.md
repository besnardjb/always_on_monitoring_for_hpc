# Wrapper Overhead Measurement

This code is used to compute the overhead of wrapping function with PMPI. As it is a very small value long measurements are needed.

## Requirements

- MPI compiler in path
- Python mathplotlib

## Run

The main script is `measure_interpos.sh` this script will :
- build the tests (run make)
- Run the various configurations 100 times
- call the plotting program (plot_interpos.py)

## Configuration

The number of averaging is located in interpos.c if you want a faster run.

## Paper Data

Measurements done for the paper are in PAPER_DATA. You may generate the plots with:

```
cd ./PAPER_DATA
python3 ../plot_interpos.py
python3 ../plot_interpos_time_avg.py
```