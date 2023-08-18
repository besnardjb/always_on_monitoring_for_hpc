# Proxy Overhead Measurement

This code measures the overhead of the metric proxy instrumentation.

## Requirements

- MPI C compiler
- Metric proxy in the path (see the proxy sources)

## Run

The main script is `measure_ovh.sh` this script will :
- build the tests (run make)
- Run the various configurations 100 times
- call the plotting program (plot_interpos.py)


## Paper Data

Measurements done for the paper are in PAPER_DATA. You may generate the plots with:

```
cd ./PAPER_DATA
python3 ../plot_ovh.py
```