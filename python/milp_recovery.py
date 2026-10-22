#!/usr/bin/env python3
"""MILP baseline for the failure-recovery repartitioning problem.

Solves, for each instance JSON exported by the C++ simulator
(`./experiment --export-milp`), the optimal reassignment of the affected
subgraph onto alive fog resources, minimizing the estimated subgraph finish
time plus movement penalties. Solved with HiGHS via scipy.optimize.milp —
same toolchain as the data-aware-scheduling project.

Model
-----
Decision variables:
  x[t,c]  in {0,1}   task t -> candidate node c
  st[t], f[t] >= 0   estimated start / finish time of task t
  Cmax     >= 0      estimated subgraph makespan
  delta[t,u] in {0,1} ordering of task pair {t,u} when co-located

Constraints (big-M):
  sum_c x[t,c] = 1
  st[t] >= avail_c - M(1-x[t,c])                        node availability
  f[t]  >= st[t] + exec[t,c] - M(1-x[t,c])              execution
  st[t] >= fin_p + comm(p->c) - M(1-x[t,c])             anchor parents
  st[t] >= f[p] + sum_b x[p,b]*comm(b->c) - M(1-x[t,c]) subgraph parents
  st[u] >= f[t] - M*delta - M(2-x[t,c]-x[u,c])          disjunction
  st[t] >= f[u] - M(1-delta) - M(2-x[t,c]-x[u,c])
  Cmax  >= f[t]

Objective: Cmax + sum_t sum_c move_cost[t,c] * x[t,c]
where move_cost = 0 for the (alive) home node, else migration + penalty.
"""

import csv
import glob
import json
import os
import sys
import time

import numpy as np
from scipy.optimize import milp, LinearConstraint, Bounds
from scipy.sparse import lil_matrix


def load_instance(path):
    with open(path) as fh:
        return json.load(fh)


def transfer(inst, src, dst, mb):
    """Transfer ms of `mb` MB from `src` (node id or -1 = durable store)
    to candidate node `dst` (node id). Same node -> 0."""
    if src == dst and src >= 0:
        return 0.0
    p = inst["params"]
    if src < 0:
        return p["store_lat_ms"] + mb * 8.0 / p["store_bw_mbps"] * 1000.0
    lat = inst["lat"].get(f"{src},{dst}", -1.0)
    if lat < 0:
        return np.inf
    bw = inst["bw"].get(f"{src},{dst}", 1e18)
    return lat + mb * 8.0 / bw * 1000.0


def build_and_solve(inst, time_limit=120.0):
    tasks = inst["tasks"]
    cands = inst["candidates"]
    n, m = len(tasks), len(cands)
    if n == 0:
        return dict(status="empty", obj=0.0, est_ms=0.0, assign=[])

    work = np.array([t["work_mi"] for t in tasks])
    mem_t = np.array([t["mem_mb"] for t in tasks])
    ctx = np.array([t["context_mb"] for t in tasks])
    home = np.array([t["home"] for t in tasks])
    home_alive = np.array([t["home_alive"] for t in tasks])
    mips = np.array([c["mips"] for c in cands])
    mem_c = np.array([c["mem_mb"] for c in cands])
    avail = np.array([c["avail"] for c in cands])
    gid_c = [c["gid"] for c in cands]

    exec_tc = work[:, None] / mips[None, :] * 1000.0            # ms
    feasible = mem_t[:, None] <= mem_c[None, :]
    p = inst["params"]
    mig = ctx * 8.0 / p["deploy_bw_mbps"] * 1000.0
    # move cost: 0 on alive home node; migration + penalty elsewhere
    home_idx = {g: j for j, g in enumerate(gid_c)}
    move_cost = np.tile(mig[:, None] + p["move_penalty_ms"], (1, m))
    for i in range(n):
        if home_alive[i] and home[i] in home_idx:
            move_cost[i, home_idx[home[i]]] = 0.0
        if not home_alive[i]:
            move_cost[i, :] = mig[i]  # forced move: pay migration only

    # comm[b->c] per sub edge weight data_mb; anchor comm const[t,c].
    lat = np.array([[inst["lat"].get(f"{gid_c[b]},{gid_c[c]}", -1.0)
                     for c in range(m)] for b in range(m)])
    bw = np.array([[inst["bw"].get(f"{gid_c[b]},{gid_c[c]}", 1e18)
                    for c in range(m)] for b in range(m)])
    np.fill_diagonal(lat, 0.0)
    comm_bc = np.where(lat < 0, np.inf, lat)  # + data*8/bw per edge

    sub_edges = inst["sub_edges"]
    anchor_edges = inst["anchor_edges"]

    # comm coefficient for a subgraph edge p->t when p on b, t on c:
    # comm(p,t,b,c) = lat[b,c] + d*8/bw[b,c]
    # anchor edge into t on c: fin + transfer(src,c,d) (constant)
    anchor_comm = np.zeros((n, m))   # max over anchor parents of fin+comm
    for ae in anchor_edges:
        t, d, fin, src = ae["t"], ae["data_mb"], ae["fin"], ae["src"]
        for c in range(m):
            v = fin + transfer(inst, src, gid_c[c], d)
            if np.isinf(v):
                anchor_comm[t, c] = np.inf
            elif np.isfinite(anchor_comm[t, c]):
                anchor_comm[t, c] = max(anchor_comm[t, c], v)

    M = 1e6

    # Variable layout: x[n*m], st[n], f[n], Cmax, delta[pairs]
    pairs = [(i, j) for i in range(n) for j in range(i + 1, n)]
    npair = len(pairs)
    nv = n * m + 2 * n + 1 + npair

    def x(i, c): return i * m + c
    def st(i): return n * m + i
    def fn(i): return n * m + n + i
    CMAX = n * m + 2 * n
    def dl(k): return n * m + 2 * n + 1 + k

    rows, lo, hi = [], [], []

    def add(coefs, lb=-np.inf, ub=np.inf):
        rows.append(coefs)
        lo.append(lb)
        hi.append(ub)

    for i in range(n):
        add({x(i, c): 1.0 for c in range(m)}, 1.0, 1.0)          # one node

    for i in range(n):
        for c in range(m):
            if not feasible[i, c]:
                continue  # ub=0 via bounds
            # st[i] >= avail[c] iff x[i,c]=1: st - M*x >= avail - M
            add({st(i): 1.0, x(i, c): -M}, lb=avail[c] - M)
            # f[i] >= st[i] + exec[i,c] iff x=1: f - st - M*x >= exec - M
            add({fn(i): 1.0, st(i): -1.0, x(i, c): -M},
                lb=exec_tc[i, c] - M)
            ac = anchor_comm[i, c]
            if np.isfinite(ac):
                # st[i] >= anchor ready time iff x=1
                add({st(i): 1.0, x(i, c): -M}, lb=ac - M)
            else:
                add({x(i, c): 1.0}, ub=0.0)                      # unreachable
            add({CMAX: 1.0, fn(i): -1.0}, lb=0.0)                # makespan

    # Subgraph edges: st[t] >= f[p] + comm(x[p], c) iff x[t,c]=1.
    #   st_t - f_p - M*x_tc - sum_b comm_{b->c} * x_pb >= -M
    for e in sub_edges:
        pi, t, d = e["p"], e["t"], e["data_mb"]
        for c in range(m):
            coefs = {st(t): 1.0, fn(pi): -1.0, x(t, c): -M}
            for b in range(m):
                cc = comm_bc[b, c]
                if np.isinf(cc):
                    # p on b and t on c => unreachable: forbid the pair
                    add({x(pi, b): 1.0, x(t, c): 1.0}, ub=1.0)
                    continue
                coefs[x(pi, b)] = coefs.get(x(pi, b), 0.0) \
                    - (cc + d * 8.0 / bw[b, c] * 1000.0)
            add(coefs, lb=-M)

    # Disjunctive ordering on shared nodes:
    #   delta=1 => i before j:  st_j - f_i - M*d - M*x_ic - M*x_jc >= -3M
    #   delta=0 => j before i:  st_i - f_j + M*d - M*x_ic - M*x_jc >= -2M
    for k, (i, j) in enumerate(pairs):
        for c in range(m):
            add({st(j): 1.0, fn(i): -1.0, dl(k): -M,
                 x(i, c): -M, x(j, c): -M}, lb=-3 * M)
            add({st(i): 1.0, fn(j): -1.0, dl(k): M,
                 x(i, c): -M, x(j, c): -M}, lb=-2 * M)

    A = lil_matrix((len(rows), nv))
    for k, coefs in enumerate(rows):
        for col, v in coefs.items():
            A[k, col] = v
    con = LinearConstraint(A.tocsr(), np.array(lo), np.array(hi))

    cobj = np.zeros(nv)
    cobj[CMAX] = 1.0
    for i in range(n):
        for c in range(m):
            cobj[x(i, c)] = move_cost[i, c]

    lb = np.zeros(nv)
    ub = np.full(nv, np.inf)
    for i in range(n):
        for c in range(m):
            ub[x(i, c)] = 1.0 if feasible[i, c] else 0.0
    ub[CMAX] = np.inf

    integ = np.zeros(nv)
    integ[:n * m] = 1.0
    integ[n * m + 2 * n + 1:] = 1.0

    t0 = time.time()
    res = milp(cobj, constraints=con, integrality=integ,
               bounds=Bounds(lb, ub),
               options={"time_limit": time_limit, "mip_rel_gap": 1e-4})
    wall = time.time() - t0
    if res.x is None:
        return dict(status=str(res.message), obj=np.nan, est_ms=np.nan,
                    assign=[], wall_s=wall)

    xv = res.x[: n * m].reshape(n, m)
    assign = [gid_c[int(np.argmax(xv[i]))] for i in range(n)]
    est_ms = float(res.x[CMAX])
    return dict(status="optimal" if res.success else str(res.message),
                obj=float(res.fun), est_ms=est_ms, assign=assign,
                gap=getattr(res, "mip_gap", np.nan), wall_s=wall)


def main():
    inst_dir = sys.argv[1] if len(sys.argv) > 1 else "milp_inst"
    out = sys.argv[2] if len(sys.argv) > 2 else "milp_results.csv"
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 120.0

    files = sorted(glob.glob(os.path.join(inst_dir, "inst_*.json")))
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["instance", "scenario", "n_sub", "n_cand", "status",
                    "est_makespan_ms", "deadline_ms", "deadline_feasible",
                    "objective", "mip_gap", "wall_s", "assign"])
        for path in files:
            inst = load_instance(path)
            name = os.path.basename(path)
            deadline = float(inst.get("deadline_ms", -1))
            if len(inst["tasks"]) > 15:
                w.writerow([name, inst["scenario"], len(inst["tasks"]),
                            len(inst["candidates"]), "skipped_too_large",
                            "", deadline, "", "", "", "", ""])
                print(f"{name}: skipped ({len(inst['tasks'])} tasks)")
                continue
            r = build_and_solve(inst, time_limit=limit)
            feasible = ""
            if r["status"] == "optimal" and deadline > 0:
                feasible = int(r["est_ms"] <= deadline + 1e-6)
            w.writerow([name, inst["scenario"], len(inst["tasks"]),
                        len(inst["candidates"]), r["status"],
                        r["est_ms"], deadline, feasible,
                        r["obj"], r.get("gap", ""),
                        r.get("wall_s", ""), ";".join(map(str, r["assign"]))])
            print(f"{name}: {r['status']} est={r['est_ms']:.1f} "
                  f"wall={r.get('wall_s', 0):.2f}s")


if __name__ == "__main__":
    main()
