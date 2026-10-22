# dynamic-scheduling

Simulation framework for **dynamic partial repartitioning of DAG workflows
after resource failures in federated fog environments**. See `DESIGN.md` for
the full system model, method description, and evaluation plan.

## Build

```sh
make            # needs a C++17 compiler
# or, with cmake:
cmake -B build && cmake --build build
```

> macOS note: this project was built with Homebrew GCC 14
> (`aarch64-apple-darwin24-g++-14`) because the Command Line Tools SDK here
> lacks libc++ headers. The `-D'_Alignof(x)=alignof(x)'` flag in the Makefile
> works around GCC rejecting C11 `_Alignof` in newer macOS SDK headers.

## Test

```sh
make test
```

## Run experiments

```sh
./experiment --seeds 20 --tasks 20,40,60 --nodes 10 \
             --fail-fracs 0.3,0.6 --deadline-factor 1.3 \
             --out results.csv
```

Options: `--seeds N`, `--tasks a,b,c`, `--nodes N`, `--fail-fracs x,y`,
`--deadline-factor F`, `--no-optimal`, `--seed S`, `--out FILE`,
`--dag-mode heavy`, `--infra-mode skewed`, `--n-failures K`,
`--export-milp DIR`. Ablations: `--no-steiner` (plain scored shortlist
instead of the MST/Steiner region) and `--full-repair` (eager rebuild of
all SPT rows on every failure instead of lazy incremental repair).

## MILP baseline (Python, scipy/HiGHS)

```sh
./experiment --export-milp milp_inst --seeds 15 --tasks 20 --out r.csv
python3 python/milp_recovery.py milp_inst milp_results.csv 120
python3 python/compare.py r.csv milp_results.csv
```

Each row of the CSV is one (scenario, strategy) run with metrics: makespan,
recovery time, tasks moved/considered, communication cost, data transferred,
energy, incremental SPT updates, planner wall time, deadline success, and
schedule quality (makespan relative to the no-failure run).

## Strategies compared

- `partial` — proposed method: affected-subgraph extraction, Steiner-region
  replacement candidates (MST over the anchor metric closure, Prim),
  constrained repartition, lazy SPT repair. Built on the three canonical
  graph primitives: APSP (Floyd-Warshall), incremental SSSP (Dijkstra),
  MST (Prim)
- `full` — complete repartition/reschedule of all unfinished tasks
- `greedy` — per-task best-EFT placement of failed-node tasks
- `static` — all failed-node tasks to one designated backup resource
- `optimal` — branch-and-bound exact placement (small affected regions)
