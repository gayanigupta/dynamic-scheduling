#!/usr/bin/env python3
"""Generate all evaluation figures from results CSVs.

Usage: python3 make_figures.py [outdir]
Reads results_normal.csv, results_heavy.csv, results_multifail.csv,
results_scale.csv and milp_small.csv from the repo root (missing files are
skipped) and writes PNGs into `outdir` (default: figures/).
"""

import csv
import os
import sys
from collections import defaultdict

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "figures")

ORDER = ["static", "greedy", "full", "partial", "optimal"]
LABEL = {"static": "Static", "greedy": "Greedy", "full": "Full repart.",
         "partial": "Partial (ours)", "optimal": "Exact B&B"}
COLOR = {"static": "#8c8c8c", "greedy": "#5b9bd5", "full": "#e8a33d",
         "partial": "#c0392b", "optimal": "#27ae60"}


def read(path):
    p = os.path.join(ROOT, path)
    if not os.path.exists(p):
        print(f"  (missing {path}, skipping dependent figures)")
        return []
    with open(p) as fh:
        return list(csv.DictReader(fh))


def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return np.nan


def by_strat(rows, metric):
    d = {s: [] for s in ORDER}
    for r in rows:
        if r.get("completed") == "1" and r["strategy"] in d:
            v = fnum(r[metric])
            if np.isfinite(v) and v >= 0:
                d[r["strategy"]].append(v)
    return d


def grouped_bars(ax, series_by_mode, ylabel, title, pct=False):
    """series_by_mode: {mode_label: {strategy: [values]}}"""
    modes = list(series_by_mode)
    strats = [s for s in ORDER
              if any(s in series_by_mode[m] and series_by_mode[m][s]
                     for m in modes)]
    w = 0.8 / len(strats)
    x = np.arange(len(modes))
    for i, s in enumerate(strats):
        means, errs = [], []
        for m in modes:
            v = series_by_mode[m].get(s, [])
            means.append(np.mean(v) * (100 if pct else 1) if v else 0)
            errs.append(np.std(v) * (100 if pct else 1) if v else 0)
        ax.bar(x + i * w - 0.4 + w / 2, means, w * 0.9, yerr=errs,
               label=LABEL[s], color=COLOR[s], edgecolor="black",
               linewidth=0.4, capsize=2, error_kw={"lw": 0.8})
    ax.set_xticks(x)
    ax.set_xticklabels(modes)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name), dpi=200)
    plt.close(fig)
    print("  wrote", name)


def main():
    os.makedirs(OUT, exist_ok=True)
    normal, heavy = read("results_normal.csv"), read("results_heavy.csv")
    multi, scale = read("results_multifail.csv"), read("results_scale.csv")
    milp = {}
    mp = os.path.join(ROOT, "milp_small.csv")
    if os.path.exists(mp):
        for r in read("milp_small.csv"):
            if r["status"] == "optimal":
                milp[int(r["scenario"])] = fnum(r["est_makespan_ms"])

    both = {"Normal": None, "Data-heavy + skewed": None}
    modes = {}
    if normal:
        modes["Normal"] = normal
    if heavy:
        modes["Heavy+skewed"] = heavy
    if multi:
        modes["Multi-failure x3"] = multi

    # ---- Fig 1: makespan ------------------------------------------------
    if modes:
        fig, ax = plt.subplots(figsize=(6.5, 3.4))
        grouped_bars(ax, {m: by_strat(r, "makespan_ms")
                          for m, r in modes.items()},
                     "Makespan (ms)", "Workflow completion time by strategy")
        save(fig, "fig1_makespan.png")

    # ---- Fig 2: tasks moved + replanning scope --------------------------
    if modes:
        fig, axs = plt.subplots(1, 2, figsize=(9, 3.4))
        grouped_bars(axs[0], {m: by_strat(r, "tasks_moved")
                              for m, r in modes.items()},
                     "Tasks moved", "Post-failure migrations")
        grouped_bars(axs[1], {m: by_strat(r, "tasks_considered")
                              for m, r in modes.items()},
                     "Tasks in replan", "Replanning problem size")
        save(fig, "fig2_disruption.png")

    # ---- Fig 3: communication cost + data transferred -------------------
    if modes:
        fig, axs = plt.subplots(1, 2, figsize=(9, 3.4))
        grouped_bars(axs[0], {m: by_strat(r, "comm_cost")
                              for m, r in modes.items()},
                     "Comm. cost", "Total communication cost")
        grouped_bars(axs[1], {m: by_strat(r, "data_mb")
                              for m, r in modes.items()},
                     "MB", "Data transferred")
        save(fig, "fig3_comm.png")

    # ---- Fig 4 (headline): replanning scope vs workflow size ------------
    if scale:
        fig, ax = plt.subplots(figsize=(6, 3.6))
        ns = sorted({int(r["n_tasks"]) for r in scale})
        for s in ORDER:
            xs, ys, ye = [], [], []
            for n in ns:
                v = [fnum(r["tasks_considered"]) for r in scale
                     if r["strategy"] == s and int(r["n_tasks"]) == n
                     and r["completed"] == "1"]
                if v:
                    xs.append(n); ys.append(np.mean(v))
                    ye.append(np.std(v))
            if xs:
                ax.errorbar(xs, ys, yerr=ye, marker="o", ms=4,
                            label=LABEL[s], color=COLOR[s], capsize=3)
        ax.set_xlabel("Workflow size (tasks)")
        ax.set_ylabel("Tasks in replanned region")
        ax.set_title("Replanning scope vs. workflow size")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.3)
        save(fig, "fig4_scale_scope.png")

    # ---- Fig 5: optimality gap vs MILP ----------------------------------
    if milp and normal:
        fig, ax = plt.subplots(figsize=(5.5, 3.2))
        gaps = {}
        for s in ORDER:
            g = [(fnum(r["makespan_ms"]) / milp[int(r["scenario"])] - 1) * 100
                 for r in normal
                 if r["strategy"] == s and r["completed"] == "1"
                 and int(r["scenario"]) in milp]
            if g:
                gaps[s] = g
        xs = list(gaps)
        means = [np.mean(gaps[s]) for s in xs]
        errs = [np.std(gaps[s]) for s in xs]
        ax.bar([LABEL[s] for s in xs], means, yerr=errs, capsize=4,
               color=[COLOR[s] for s in xs], edgecolor="black",
               linewidth=0.4)
        ax.set_ylabel("Gap to MILP bound (%)")
        ax.set_title("Distance from MILP-estimated optimum")
        ax.grid(axis="y", alpha=0.3)
        plt.setp(ax.get_xticklabels(), rotation=15, ha="right")
        save(fig, "fig5_milp_gap.png")

    # ---- Fig 6: deadline success rate -----------------------------------
    if modes:
        fig, ax = plt.subplots(figsize=(6.5, 3.2))
        grouped_bars(ax, {m: by_strat(r, "deadline_met")
                          for m, r in modes.items()},
                     "Deadline met (%)", "Deadline success rate", pct=True)
        ax.set_ylim(0, 105)
        save(fig, "fig6_deadline.png")

    # ---- Fig 7: recovery time -------------------------------------------
    if modes:
        fig, ax = plt.subplots(figsize=(6.5, 3.2))
        grouped_bars(ax, {m: by_strat(r, "recovery_time_ms")
                          for m, r in modes.items()},
                     "ms", "Failure-recovery time")
        save(fig, "fig7_recovery.png")

    # ---- Fig 8: quality (normalized makespan) distribution --------------
    if modes:
        fig, axs = plt.subplots(1, len(modes), figsize=(3.4 * len(modes), 3.4),
                                sharey=True)
        if len(modes) == 1:
            axs = [axs]
        for ax, (m, rows) in zip(axs, modes.items()):
            data = by_strat(rows, "quality")
            strs = [s for s in ORDER if data[s]]
            bp = ax.boxplot([data[s] for s in strs], tick_labels=[LABEL[s]
                            for s in strs], patch_artist=True, showfliers=False)
            for patch, s in zip(bp["boxes"], strs):
                patch.set_facecolor(COLOR[s])
                patch.set_alpha(0.8)
            ax.set_title(m)
            ax.grid(axis="y", alpha=0.3)
            plt.setp(ax.get_xticklabels(), rotation=20, ha="right",
                     fontsize=8)
        axs[0].set_ylabel("Makespan / no-failure makespan")
        save(fig, "fig8_quality_box.png")

    # ---- Fig 9: energy + spt updates ------------------------------------
    if modes:
        fig, axs = plt.subplots(1, 2, figsize=(9, 3.4))
        grouped_bars(axs[0], {m: by_strat(r, "energy_j")
                              for m, r in modes.items()},
                     "Joules", "Total energy")
        grouped_bars(axs[1], {m: by_strat(r, "spt_updates")
                              for m, r in modes.items()},
                     "SPT rebuilds", "Incremental path updates")
        save(fig, "fig9_energy_paths.png")

    # ---- Fig 10: CDF of schedule quality --------------------------------
    if normal:
        fig, ax = plt.subplots(figsize=(6, 3.6))
        for s in ORDER:
            v = sorted(by_strat(normal, "quality")[s])
            if v:
                ax.plot(v, np.linspace(0, 1, len(v)), label=LABEL[s],
                        color=COLOR[s], lw=2)
        ax.set_xlabel("Makespan / no-failure makespan")
        ax.set_ylabel("CDF")
        ax.set_title("Distribution of repaired-schedule quality")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.3)
        save(fig, "fig10_quality_cdf.png")

    # ---- Fig 11: multi-failure disruption detail ------------------------
    if multi:
        fig, axs = plt.subplots(1, 2, figsize=(9, 3.4))
        grouped_bars(axs[0], {"3 failures": by_strat(multi, "tasks_moved")},
                     "Tasks moved", "Migrations under cascaded failures")
        grouped_bars(axs[1], {"3 failures": by_strat(multi, "deadline_met")},
                     "Deadline met (%)", "Deadline rate under cascaded failures",
                     pct=True)
        save(fig, "fig11_multifail.png")

    # ---- Fig 12: scatter makespan vs disruption -------------------------
    if normal:
        fig, ax = plt.subplots(figsize=(6, 4))
        for s in ORDER:
            xs = [fnum(r["tasks_moved"]) for r in normal
                  if r["strategy"] == s and r["completed"] == "1"]
            ys = [fnum(r["makespan_ms"]) for r in normal
                  if r["strategy"] == s and r["completed"] == "1"]
            ax.scatter(xs, ys, s=18, alpha=0.6, label=LABEL[s],
                       color=COLOR[s], edgecolor="none")
        ax.set_xlabel("Tasks moved")
        ax.set_ylabel("Makespan (ms)")
        ax.set_title("Quality vs. disruption trade-off")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.3)
        save(fig, "fig12_tradeoff_scatter.png")

    # ---- Fig 13: deadline-factor sensitivity sweep ----------------------
    factors = [("0.9", 0.9), ("1.0", 1.0), ("1.1", 1.1),
               ("1.3", 1.3), ("1.6", 1.6)]
    dls = {}
    for tag, f in factors:
        rows = read(f"results_dl_{tag}.csv")
        if not rows:
            continue
        dls[f] = {}
        for r in rows:
            if r["completed"] == "1":
                dls[f].setdefault(r["strategy"], []).append(
                    fnum(r["deadline_met"]))
    if dls:
        fig, ax = plt.subplots(figsize=(6, 4))
        xs = sorted(dls)
        for s in ORDER:
            ys = [np.mean(dls[f].get(s, [np.nan])) * 100 for f in xs]
            ax.plot(xs, ys, marker="o", ms=4, lw=1.5,
                    label=LABEL[s], color=COLOR[s])
        ax.set_xlabel("Deadline (x no-failure makespan)")
        ax.set_ylabel("Deadline met (%)")
        ax.set_title("Deadline sensitivity of recovery strategies")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.3)
        save(fig, "fig13_deadline_sweep.png")

    # ---- Fig 14: algorithmic ablation -----------------------------------
    ns = read("results_nosteiner.csv")
    nsh = read("results_nosteiner_heavy.csv")
    if ns and normal:
        fig, axs = plt.subplots(1, 2, figsize=(9, 3.4))
        on, off = [], []
        for with_st, no_st in ((normal, ns), (heavy, nsh)):
            on.append(np.mean([fnum(r["makespan_ms"]) for r in with_st
                               if r["strategy"] == "partial"
                               and r["completed"] == "1"]))
            off.append(np.mean([fnum(r["makespan_ms"]) for r in no_st
                                if r["strategy"] == "partial"
                                and r["completed"] == "1"]))
        xs = np.arange(2)
        axs[0].bar(xs - 0.17, on, 0.32, label="MST/Steiner region",
                   color=COLOR["partial"], edgecolor="black", linewidth=0.4)
        axs[0].bar(xs + 0.17, off, 0.32, label="shortlist only",
                   color="grey", edgecolor="black", linewidth=0.4)
        axs[0].set_xticks(xs)
        axs[0].set_xticklabels(["Normal", "Heavy+skewed"])
        axs[0].set_ylabel("Makespan (ms)")
        axs[0].set_title("Steiner-region candidates (MST) on vs off")
        axs[0].legend(fontsize=8)
        axs[0].grid(axis="y", alpha=0.3)

        upd = np.mean([fnum(r["spt_updates"]) for r in normal
                       if r["strategy"] == "partial"
                       and r["completed"] == "1"])
        axs[1].bar([0, 1], [upd, 10], 0.5,
                   color=[COLOR["partial"], "grey"],
                   edgecolor="black", linewidth=0.4)
        axs[1].set_xticks([0, 1])
        axs[1].set_xticklabels(["Lazy SSSP\n(ours)", "Full APSP\nrebuild"])
        axs[1].set_ylabel("SPT rows recomputed per failure")
        axs[1].set_title("Incremental vs full route repair")
        axs[1].grid(axis="y", alpha=0.3)
        save(fig, "fig14_ablation.png")


if __name__ == "__main__":
    main()
