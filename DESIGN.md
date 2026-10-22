# Dynamic Partial Repartitioning of DAG Workflows After Resource Failures in Federated Fog Environments

**Design Document — System Model, Method, and Evaluation Framework**

---

## 1. Problem Statement

A workflow is modeled as a directed acyclic graph (DAG) of tasks placed on a
federated fog infrastructure, itself modeled as a graph. When a fog resource
fails mid-execution, the unfinished tasks assigned to it can no longer run.
Repartitioning and rescheduling the *entire* workflow is slow, migrates tasks
that were running correctly, and inflates communication and migration costs.

**Goal.** Repartition and reschedule *only the affected region* of the task
DAG, while preserving all unaffected assignments, minimizing recovery time,
communication cost, task movement, and workflow completion time (makespan),
and maximizing the deadline success rate.

---

## 2. System Model

### 2.1 Workflow model (task DAG)

`G_t = (V_t, E_t)` where:

- Each node `v ∈ V_t` is a task with:
  - computational demand `w(v)` (million instructions, MI),
  - memory requirement `mem(v)` (MB),
  - migration context size `ctx(v)` (MB) — the state that must be moved if the
    task is reassigned.
- Each edge `e = (u, v) ∈ E_t` is a data dependency carrying `d(e)` MB from
  `u` to `v`. `v` cannot start until every parent's output has been delivered
  to `v`'s resource.

### 2.2 Infrastructure model (infra graph)

`G_i = (V_i, E_i)` where:

- Each node `r ∈ V_i` is a fog resource with processing capacity `mips(r)`
  (MIPS), memory `mem(r)` (MB), availability flag `alive(r)`, and power
  parameters `P_busy(r)`, `P_idle(r)` (W).
- Each undirected link `l = (a, b) ∈ E_i` has latency `lat(l)` (ms),
  bandwidth `bw(l)` (Mbps), and transfer cost `cost(l)` (per MB).

The transfer time of `m` MB along route `ρ(a,b)` is

```
T(a, b, m) = Σ_{l∈ρ} lat(l) + m · 8 / min_{l∈ρ} bw(l) · 1000   [ms]
```

and its cost is `m · Σ_{l∈ρ} cost(l)`. Routes are least-latency paths.

### 2.3 Execution model (documented simplifications)

- Each fog resource executes **one task at a time** (single execution slot);
  queued tasks are dispatched in list-scheduling order.
- The network is modeled as shortest-path latency plus a bandwidth
  bottleneck; concurrent flows do not contend.
- A task running on a resource that fails loses all progress (no
  checkpointing) and reverts to *waiting*.
- **Data staging:** when a task finishes, its outputs are pushed to the
  resources hosting its children. If a staging location itself fails, the
  output is recovered from a durable federation store (control plane) with
  fixed latency/bandwidth/cost parameters. Completed tasks are therefore
  *never re-executed*.
- Edge communication cost is charged when the consuming task starts, based on
  the producing data's actual location and the consumer's resource at that
  time.

### 2.4 Failure model

At a chosen simulation time `τ_fail`, resource `f` is marked dead. Its running
task (if any) is lost, its queued tasks become stranded, and all routes
through `f` become invalid. The recovery method is invoked at `τ_fail`; the
rest of the workflow continues executing while replanning is in progress.

---

## 3. Architecture

```
generators ──► TaskDAG, InfraGraph
                   │
        heft_schedule ──► initial assignment  (HEFT list scheduling)
                   │
                 Sim  ──► discrete-event engine
                   │        failure event at τ_fail
                   ▼
        on_failure(strategy) ──► RecoveryResult { moves, replan cost }
                   │
                 Sim resumes ──► RunMetrics (makespan, comm cost, energy, ...)
```

| Component | File | Role |
|---|---|---|
| `TaskDAG` | `src/task_graph.*` | workflow DAG, topo order, closures |
| `InfraGraph` | `src/infra_graph.*` | resources, links, lazy incremental routing |
| `heft_schedule` | `src/scheduler.*` | initial partitioning (HEFT) |
| `Sim` | `src/sim.*` | event-driven execution + failure handling |
| `recover_*` | `src/recovery.*` | partial / greedy / static / full strategies |
| `recover_optimal` | `src/optimal.*` | branch-and-bound exact solver (small cases) |
| `gen_dag/gen_infra` | `src/generators.*` | random scenario generation |
| `experiment` | `src/main.cpp` | experiment sweep → CSV |

---

## 4. The Proposed Method (Partial Repartitioning)

### 4.1 Step 1 — Identify the affected set `F`

```
F = { v ∈ V_t : assign(v) = f  AND  status(v) = WAITING }
```

This includes the task that was *running* on `f` (progress lost, so it reverts
to WAITING) and all queued tasks. DONE tasks on `f` are never re-executed —
their outputs were already staged at their consumers' resources (§2.3).

### 4.2 Step 2 — Boundary determination: the affected subgraph `S`

The minimal affected region is `F`. We optionally expand it by a
**dependency-aware boundary rule** — a waiting task `v` on a *healthy* resource
joins `S` iff **every unfinished parent of `v` is already inside `S`**:

```
S ← F
repeat
    for each v ∉ S with status(v) = WAITING:
        if v has ≥1 unfinished parent AND all unfinished parents ∈ S:
            S ← S ∪ {v}
until fixpoint
```

Rationale: such a `v` would otherwise receive *all* of its remaining input
over new inter-resource routes; co-migrating it can keep partition locality.
Inclusion only makes `v` *eligible* to move — the repartitioner (Step 4)
decides, penalized by its migration cost, so it stays unless moving pays off.
A task with any unfinished parent *outside* `S` is anchored in place — its
assignment cannot change. This yields the smallest region that is still
self-consistent under dependency closure.

### 4.3 Step 3 — Replacement resource shortlist `C`

Anchors are the tasks adjacent to `S` that stay put (completed, running, or
waiting on healthy nodes). Each alive candidate resource `c` is scored by
proximity to the anchors, weighted by the data volume crossing the boundary:

```
score(c) = Σ_{(a→t), t∈S, a∉S} d(a,t)·lat(node(a), c)          // parents in
         + Σ_{(t→b), t∈S, b∉S} d(t,b)·lat(c, node(b))          // children out
         + ε · avail(c)                                        // spare capacity
```

`C` = top-`k` resources by score (default `k = 4`), **plus** the nodes already
hosting healthy members of `S` (the "stay" option).

### 4.4 Step 4 — Repartition `S` onto `C`

Constrained HEFT-style list scheduling restricted to `S` and `C`:

- Order `S` by upward rank, where children outside `S` are fixed sinks on
  their current resources.
- For each task in rank order, place it on the candidate minimizing

```
EFT(t, c) = max{ avail(c),  max_{p∈par(t)} [ fin(p) + T(loc(p), c, d(p,t)) ] }
            + exec(t, c)  +  move_penalty(t, c)
```

  where `fin(p)`/`loc(p)` are known exactly for anchor parents (actual finish
  time and staged-data location) and estimated for parents inside `S` from the
  partial plan being built, and

```
move_penalty(t, c) = 0                                    if node(t) dead
                   = migration_ms(t) + λ_move             if c ≠ node(t)
                   = 0                                    if c = node(t)
```

- Tasks on the failed resource *must* move; tasks on healthy resources move
  only if the benefit exceeds the migration penalty.

### 4.5 Step 5 — Incremental communication-path update

`InfraGraph` keeps all-pairs latency routes as `(dist, next_hop)` caches built
once by Floyd–Warshall. On failure of `f` **nothing is recomputed eagerly**.
Each route query `(i,j)` walks the cached next-hop chain; only when a cached
hop is dead/missing is that *single source row's* shortest-path tree rebuilt
by Dijkstra over alive nodes (Bhowmick-style incremental SPT update). A clean
re-verification pass then refreshes the source row so `dist` stays optimal.
Only the SPTs actually traversed by post-failure traffic are ever updated —
the metric `spt_updates` counts them.

### 4.6 Step 6 — Resume

Moved tasks get a redeploy timestamp `deploy_at = τ_fail + replan + migration`.
The workflow continues from the last successful execution point; completed
tasks are not restarted and unaffected assignments are untouched.

---

## 5. Baselines Compared

| Strategy | Scope | Placement rule |
|---|---|---|
| **partial (ours)** | affected subgraph `S` | Steps 1–6 above |
| **greedy** | `F` only (no boundary expansion) | each task → alive resource minimizing estimated finish time |
| **full** | all unfinished tasks | complete re-list-schedule over all alive resources, no stay penalty |
| **static** | `F` | all tasks → one pre-designated backup resource |
| **optimal** | `S` (small cases only) | branch-and-bound minimizing estimated subgraph makespan + movement penalties |
| **milp** | `S` (small cases only) | exact MILP (assignment + big-M start-time + disjunctive ordering constraints) solved offline with HiGHS via `scipy.optimize.milp`; the C++ sim exports each failure instance to JSON (`--export-milp`) and `python/milp_recovery.py` solves it |

Replanning cost is modeled as `replan_ms = base + per_task · tasks_considered`,
so `full` pays a delay proportional to *all* unfinished tasks while `partial`
pays only for `|S|`. The exact solver counts objective evaluations and runs
only when `|S|` is small.

---

## 6. Metrics

Per (scenario, strategy) run we record:

| Metric | Definition |
|---|---|
| `recovery_time_ms` | replanning delay + max migration/redeploy delay |
| `makespan_ms` | total workflow completion time after failure |
| `tasks_moved` | tasks whose assignment changed after the failure |
| `tasks_considered` | size of the replanning problem (algorithmic effort) |
| `comm_cost` | Σ data·route cost over inter-resource transfers + migrations |
| `data_mb` | total MB transferred (dependencies + migration contexts) |
| `energy_j` | Σ P_busy·busy + P_idle·idle over resources |
| `spt_updates` | incremental shortest-path-tree recomputations |
| `algo_wall_ms` | measured wall-clock time of the recovery planner |
| `deadline_met` | makespan ≤ `deadline_factor` × no-failure makespan |
| `quality` | makespan / no-failure makespan |

All strategies run on *identical cloned pre-failure states* (same DAG, same
infra, same failure node and time), so differences are attributable to the
recovery method alone.

---

## 7. Experimental Setup

`experiment` sweeps: `seeds × task_counts × fail_fracs`, each scenario with a
freshly generated layered random DAG and a connected random infra graph.
Failure is injected on a node that hosts tasks at `τ_fail = frac·makespan`.

```
./experiment --seeds 20 --tasks 20,40,60 --nodes 10 \
             --fail-fracs 0.3,0.6 --deadline-factor 1.3 --out results.csv

# scenario variants
--dag-mode heavy       # communication-dominated workflows (data x12)
--infra-mode skewed    # ~20% strong + ~80% weak fog nodes
--n-failures K         # K distinct resource failures per run
--export-milp DIR      # dump recovery instance JSON for the MILP baseline
```

MILP baseline workflow:
```
./experiment --export-milp milp_inst --out results_small.csv ...
python3 python/milp_recovery.py milp_inst milp_results.csv 120
python3 python/compare.py results_small.csv milp_results.csv
```

## Results snapshot (120 scenarios per mode, seeds=20)

**Normal workload.** Full repartition wins makespan (7.76s vs 8.40s) but at
~10x communication cost, ~3x data transferred, and ~6x the replanning scope.
Partial matches the B&B optimum within ~0.1% and beats static by ~8% makespan
(80% vs 60% deadline rate).

**Data-heavy + skewed.** Partial edges ahead of greedy (17.12s vs 17.25s
makespan) as boundary co-location starts paying off; full still best on
makespan at ~5.7x the communication cost.

**Multi-failure (3 failures/run).** The differentiator sharpens: partial
moves ~10 tasks vs full's ~35, and considers 11 vs 70 tasks per run.
Static collapses to a 7% deadline rate.

**Scale sweep** — replanning problem size vs workflow size:

| strategy | n=20 | n=60 | n=100 | n=150 |
|---|---|---|---|---|
| full | 10.3 | 33.9 | 57.9 | 87.9 |
| partial | 2.2 | 4.2 | 5.6 | 8.4 |
| greedy/static | 1.8 | 3.5 | 5.4 | 7.4 |

`full`'s replanning scope grows linearly with workflow size; `partial`'s
stays proportional to the failure's blast radius — the core claim of the
method. At n=150 the replanned region is ~10x smaller.

Expected hypotheses the experiments test:

1. `partial` ≈ `optimal` in makespan for small cases (validates the heuristic).
2. `partial` < `full` in `recovery_time`, `tasks_considered`, `tasks_moved`
   and `data_mb`, with comparable makespan.
3. `partial` < `greedy` and `static` in makespan/comm cost (dependency-aware
   placement beats per-task and single-node fallbacks).
4. `spt_updates` ≪ `n²` — incremental path update touches few SPTs.

---

## 8. Novelty (as implemented)

1. **Affected-region extraction**: boundary rule connecting a node failure in
   the infra graph to the smallest dependency-consistent region of the task
   DAG (§4.2).
2. **Anchor-aware repartitioning**: replacement resources are shortlisted by
   shortest-path proximity to the *unchanged* neighbors of the region (§4.3),
   and repartitioning charges a movement penalty to any healthy task (§4.4).
3. **Lazy route repair**: shortest paths are updated incrementally, only for
   routes actually queried after the failure (§4.5) — the application of
   Bhowmick-style dynamic SPT updates to workflow-DAG recovery.

---

## 9. Build & Layout

```
Makefile / CMakeLists.txt     build (see README.md for toolchain notes)
src/task_graph.*              workflow DAG
src/infra_graph.*             infra graph + lazy incremental routing
src/scheduler.*               HEFT initial partition
src/sim.*                     discrete-event simulator
src/recovery.*                partial / greedy / static / full recovery
src/optimal.*                 branch-and-bound optimal (small cases)
src/generators.*              random scenario generation
src/main.cpp                  experiment sweep → CSV
tests/test_main.cpp           sanity tests
```
