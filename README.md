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
`--deadline-factor F`, `--no-optimal`, `--seed S`, `--out FILE`.

Each row of the CSV is one (scenario, strategy) run with metrics: makespan,
recovery time, tasks moved/considered, communication cost, data transferred,
energy, incremental SPT updates, planner wall time, deadline success, and
schedule quality (makespan relative to the no-failure run).

## Strategies compared

- `partial` — proposed method: affected-subgraph extraction, anchor-aware
  replacement-resource shortlist, constrained repartition, lazy SPT repair
- `full` — complete repartition/reschedule of all unfinished tasks
- `greedy` — per-task best-EFT placement of failed-node tasks
- `static` — all failed-node tasks to one designated backup resource
- `optimal` — branch-and-bound exact placement (small affected regions)
