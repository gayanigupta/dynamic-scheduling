"""Recreate the partial-repartitioning overview figure with uniform fonts."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch
from matplotlib.lines import Line2D

FS = 8.5          # uniform body font size
FS_T = 11         # panel titles
FS_L = 9.5        # row labels

C_DONE = "#9e9e9e"      # completed task
C_AFF = "#f4b6b6"       # affected task (red-ish)
C_NEW = "#aecbeb"       # reassigned task (blue-ish)
C_OK = "#bfe6bf"        # unaffected task (green-ish)
RED = "#d92626"
BLUE = "#1f6fd6"
GREEN = "#2e9e44"
GREY = "#555555"


def node(ax, x, y, label, r=6, fc="#d9dce0", ec="black", fs=FS,
         check=False):
    ax.add_patch(Circle((x, y), r, facecolor=fc, edgecolor=ec,
                        linewidth=1.2, zorder=3))
    ax.text(x, y + 0.4, label, ha="center", va="center", fontsize=fs,
            zorder=4)
    if check:
        ax.text(x + 3.6, y - 1.2, "\u2713", ha="center", va="center",
                fontsize=fs + 1.5, zorder=4)


def link(ax, p0, p1, color=GREY, lw=1.3, ls="-", rad=0.0, arrow=False,
         sh=8):
    ax.add_patch(FancyArrowPatch(p0, p1, connectionstyle=f"arc3,rad={rad}",
                 arrowstyle="-|>" if arrow else "-", mutation_scale=9,
                 color=color, lw=lw, linestyle=ls, zorder=2,
                 shrinkA=sh, shrinkB=sh))


def part_box(ax, x, y, w, h, color, edge=RED):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=1.5", facecolor=color, alpha=0.22,
                 edgecolor=edge, linewidth=2.2,
                 linestyle=":", zorder=1))


def plabel(ax, x_edge, y, text, side, color="#333333"):
    ax.text(x_edge, y, text, ha="left" if side == "r" else "right",
            va="center", fontsize=FS - 1, color=color, zorder=5)


fig, ax = plt.subplots(figsize=(9.6, 5.2))
ax.set_xlim(0, 200)
ax.set_ylim(0, 110)
ax.axis("off")

ax.plot([100, 100], [4, 108], color="#bbbbbb", lw=0.9, ls=(0, (4, 4)))
ax.plot([0, 200], [60, 60], color="#bbbbbb", lw=0.9, ls=(0, (4, 4)))

ax.text(50, 107, "Before failure", ha="center", fontsize=FS_T,
        fontweight="bold")
ax.text(150, 107, "After failure and repair", ha="center", fontsize=FS_T,
        fontweight="bold")
ax.text(1, 84, "Resource\ngraph", ha="left", va="center", fontsize=FS_L,
        fontweight="bold")
ax.text(1, 17, "Task graph\nsplit into\npartitions", ha="left", va="center",
        fontsize=FS_L, fontweight="bold")

# ---------------- left: resource graph ----------------
res = {"A": (30, 88), "B": (50, 93), "C": (70, 88), "D": (50, 70),
       "E": (84, 70)}
for u, v, rad in [("A", "B", 0), ("B", "C", 0), ("A", "C", -0.32),
                  ("B", "D", 0), ("C", "D", 0), ("D", "E", 0)]:
    link(ax, res[u], res[v], rad=rad, lw=1.4)
for k in ("A", "C", "D", "E"):
    node(ax, *res[k], k, r=7)
node(ax, *res["B"], "B", r=7, fc=C_AFF, ec=RED)
ax.text(50, 101.5, "B fails", color=RED, fontsize=FS, fontweight="bold",
        ha="center")

# ---------------- right: resource graph ----------------
res2 = {k: (x + 100, y) for k, (x, y) in res.items()}
for u, v in [("A", "B"), ("B", "C"), ("B", "D")]:
    link(ax, res2[u], res2[v], lw=1.2, color=RED, ls=(0, (3, 3)))
link(ax, res2["D"], res2["E"], lw=1.4)
link(ax, res2["A"], res2["C"], rad=-0.32, lw=1.7, color=BLUE, arrow=True)
link(ax, res2["C"], res2["D"], lw=1.7, color=BLUE, arrow=True)
for k in ("A", "C", "D", "E"):
    node(ax, *res2[k], k, r=7)
ax.add_patch(Circle(res2["B"], 7, facecolor=C_AFF, edgecolor=RED,
                    lw=1.2, zorder=3))
bx, by = res2["B"]
ax.plot([bx - 5.5, bx + 5.5], [by - 5.5, by + 5.5], color=RED, lw=1.8,
        zorder=4)
ax.plot([bx - 5.5, bx + 5.5], [by + 5.5, by - 5.5], color=RED, lw=1.8,
        zorder=4)
ax.text(bx, 101.5, "B failed", color=RED, fontsize=FS, fontweight="bold",
        ha="center")
ax.text(199, 104, "Communication rerouted\non surviving links", color=BLUE,
        fontsize=FS - 0.5, ha="right", va="top")

# ---------------- left: task DAG ----------------
node(ax, 50, 47, "T1", fc=C_DONE, ec=GREY, check=True)
part_box(ax, 50, 47, 13, 11, GREY)
plabel(ax, 43, 47, "Partition A\n(resource A)", "l")

node(ax, 36, 38, "T2", fc=C_AFF, ec=RED)
node(ax, 36, 25, "T4", fc=C_AFF, ec=RED)
part_box(ax, 36, 31.5, 15, 25, RED)
plabel(ax, 27.5, 31.5, "Partition B\n(resource B)", "l", color=RED)

node(ax, 68, 38, "T3", fc=C_OK, ec=GREEN)
part_box(ax, 68, 38, 13, 11, GREEN)
plabel(ax, 75, 38, "Partition C\n(resource C)", "r")
node(ax, 68, 23, "T5", fc=C_OK, ec=GREEN)
part_box(ax, 68, 23, 13, 11, GREEN)
plabel(ax, 75, 23, "Partition D\n(resource D)", "r")
node(ax, 52, 12, "T6", fc=C_OK, ec=GREEN)
part_box(ax, 52, 12, 13, 11, GREEN)
plabel(ax, 59, 12, "Partition E\n(resource E)", "r")

link(ax, (46, 43), (39.5, 40.5), arrow=True, sh=6)
link(ax, (54, 43), (64.5, 40.5), arrow=True, sh=6)
link(ax, (36, 32), (36, 31), arrow=True, sh=6)
link(ax, (40, 20.5), (48, 15.5), arrow=True, sh=6)
link(ax, (64, 18.5), (56, 15.5), arrow=True, sh=6)

ax.add_patch(FancyArrowPatch((54, 64), (29, 42),
             connectionstyle="arc3,rad=0.35", arrowstyle="-|>",
             mutation_scale=10, color=RED, lw=1.2,
             linestyle=(0, (4, 3))))
ax.text(8, 55.5, "Failure maps to\naffected region", color=RED,
        fontsize=FS, ha="left", va="center")
ax.text(45, 4.5, "Affected region: unfinished tasks T2 and T4 on B",
        color=RED, fontsize=FS, style="italic", ha="center",
        fontweight="bold")

ax.add_patch(FancyArrowPatch((80, 30), (97, 30), arrowstyle="-|>",
             mutation_scale=15, color="black", lw=3))
ax.text(88.5, 45, "Repartition\naffected tasks", ha="center",
        fontsize=FS - 0.5)

# ---------------- right: task DAG ----------------
node(ax, 150, 47, "T1", fc=C_DONE, ec=GREY, check=True)
part_box(ax, 150, 47, 13, 11, GREY)
plabel(ax, 143, 47, "Partition A\n(resource A)", "l")

# T2 joins Partition C (with T3), T4 joins Partition D (with T5):
# partitions unchanged, only task assignment changed
part_box(ax, 150, 38, 44, 11, GREEN, edge=BLUE)
node(ax, 136, 38, "T2", fc=C_NEW, ec=BLUE)
node(ax, 164, 38, "T3", fc=C_OK, ec=GREEN)
plabel(ax, 173, 38, "Partition C\n(resource C)", "r")
part_box(ax, 150, 23, 44, 11, GREEN, edge=BLUE)
node(ax, 136, 23, "T4", fc=C_NEW, ec=BLUE)
node(ax, 164, 23, "T5", fc=C_OK, ec=GREEN)
plabel(ax, 173, 23, "Partition D\n(resource D)", "r")
node(ax, 152, 12, "T6", fc=C_OK, ec=GREEN)
part_box(ax, 152, 12, 13, 11, GREEN)
plabel(ax, 159, 12, "Partition E\n(resource E)", "r")

link(ax, (146, 43), (137.5, 40.5), color=BLUE, arrow=True, sh=6)
link(ax, (154, 43), (160.5, 40.5), arrow=True, sh=6)
link(ax, (136, 32), (136, 31), color=BLUE, arrow=True, sh=6)
link(ax, (139, 19), (149, 15.5), color=BLUE, arrow=True, sh=6)
link(ax, (161, 18.5), (155, 15.5), arrow=True, sh=6)

ax.text(186, 53, "T2: B $\\rightarrow$ C\nT4: B $\\rightarrow$ D",
        color=BLUE, fontsize=FS, ha="center", va="top", fontweight="bold")
ax.text(150, 4.5, "Other task assignments and completed work stay "
        "unchanged", fontsize=FS, ha="center", fontweight="bold")

# ---------------- legend ----------------
handles = [
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_DONE,
           markeredgecolor=GREY, markersize=8, label="Completed task"),
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_AFF,
           markeredgecolor=RED, markersize=8,
           label="Affected task (on failed resource)"),
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_NEW,
           markeredgecolor=BLUE, markersize=8,
           label="Reassigned task (updated resource)"),
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_OK,
           markeredgecolor=GREEN, markersize=8,
           label="Unaffected task (assignment preserved)"),
    Line2D([0], [0], color=GREY, lw=1.4,
           label="Resource link (undirected)"),
    Line2D([0], [0], color=GREY, lw=1.4, marker=">", markersize=6,
           label="Task dependency (directed)"),
]
ax.legend(handles=handles, loc="lower center", bbox_to_anchor=(0.5, -0.09),
          ncol=3, fontsize=FS - 0.5, frameon=False,
          columnspacing=1.2, handletextpad=0.4)

fig.savefig("figures/fig0_overview.png", dpi=220,
            bbox_inches="tight", facecolor="white")
fig.savefig("paper/figures/fig0_overview.png", dpi=220,
            bbox_inches="tight", facecolor="white")
print("wrote fig0_overview.png")
