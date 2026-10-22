#!/usr/bin/env python3
"""Aggregate experiment results and compare strategies against the MILP
optimum.

Usage:
  python3 compare.py results.csv [milp_results.csv]

Prints per-strategy means +/- std for every metric, and — when MILP results
are given — the average optimality gap of each heuristic relative to the
MILP estimated makespan for the same failure instance.
"""

import csv
import sys
from collections import defaultdict

import numpy as np


def read_csv(path):
    with open(path) as fh:
        return list(csv.DictReader(fh))


def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return float("nan")


def main():
    rows = read_csv(sys.argv[1])
    milp = {}
    if len(sys.argv) > 2:
        for r in read_csv(sys.argv[2]):
            if r["status"] == "optimal":
                milp[int(r["scenario"])] = fnum(r["est_makespan_ms"])

    milp_feas = {}
    milp_dl = {}
    if len(sys.argv) > 2:
        for r in read_csv(sys.argv[2]):
            if r.get("deadline_feasible") not in ("", None):
                milp_feas[int(r["scenario"])] = fnum(r["deadline_feasible"])
            if r.get("deadline_ms"):
                milp_dl[int(r["scenario"])] = fnum(r["deadline_ms"])

    by_strat = defaultdict(list)
    for r in rows:
        if r["completed"] == "1":
            by_strat[r["strategy"]].append(r)

    metrics = ["makespan_ms", "recovery_time_ms", "tasks_moved",
               "tasks_considered", "comm_cost", "data_mb", "energy_j",
               "spt_updates", "algo_wall_ms", "deadline_met", "quality"]

    print(f"{'strategy':<9}" + "".join(f"{m[:14]:>16}" for m in metrics))
    for s, rs in sorted(by_strat.items()):
        vals = []
        for m in metrics:
            v = [fnum(r[m]) for r in rs]
            v = [x for x in v if np.isfinite(x) and x >= 0]
            if not v:
                vals.append("n/a")
            elif m in ("deadline_met",):
                vals.append(f"{np.mean(v)*100:6.0f}%")
            else:
                vals.append(f"{np.mean(v):8.1f}+-{np.std(v):<6.1f}")
        print(f"{s:<9}" + "".join(f"{v:>16}" for v in vals))

    # Optimality gap vs MILP on matching scenarios.
    if milp:
        print("\nGap vs MILP est. makespan (mean+-std, %, matched scenarios):")
        for s, rs in sorted(by_strat.items()):
            gaps = []
            for r in rs:
                sc = int(r["scenario"])
                mk = fnum(r["makespan_ms"])
                if sc in milp and np.isfinite(mk) and milp[sc] > 0:
                    gaps.append((mk / milp[sc] - 1.0) * 100.0)
            if gaps:
                print(f"  {s:<9} {np.mean(gaps):7.2f} +- {np.std(gaps):5.2f} "
                      f"  (n={len(gaps)})")

    # Deadline feasibility on the MILP-matched instances: each strategy's
    # simulated deadline-met rate vs the MILP optimum's (estimated).
    if milp_feas:
        print("\nDeadline-met rate on MILP-solved instances "
              "(MILP = est. feasibility of the optimal recovered region):")
        matched = sorted(milp_feas)
        for s, rs in sorted(by_strat.items()):
            v = [fnum(r["deadline_met"]) for r in rs
                 if int(r["scenario"]) in milp_feas]
            if v:
                print(f"  {s:<9} {np.mean(v)*100:5.1f}%  (n={len(v)})")
        print(f"  MILP      {np.mean(list(milp_feas.values()))*100:5.1f}%  "
              f"(n={len(milp_feas)})")

    # Completion / deadline rates incl. failures.
    allr = read_csv(sys.argv[1])
    tot = defaultdict(int)
    done = defaultdict(int)
    for r in allr:
        tot[r["strategy"]] += 1
        done[r["strategy"]] += int(r["completed"] == "1")
    print("\ncompletion rate:")
    for s in sorted(tot):
        print(f"  {s:<9} {done[s]}/{tot[s]}")


if __name__ == "__main__":
    main()
