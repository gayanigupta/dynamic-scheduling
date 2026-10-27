"""Publication-style overview figure: failure + partial repartitioning."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch
from matplotlib.lines import Line2D

FS = 9
FS_T = 10

C_DONE = "#bdbdbd"
C_AFF = "#f1a0a0"
C_NEW = "#a8cbe8"
C_OK = "#b5ddb5"
RED = "#c62828"
BLUE = "#1565c0"
GREEN = "#2e7d32"
GREY = "#555555"


def node(ax, x, y, label, r=5.5, fc="#d9dce0", ec="black", check=False):
    ax.add_patch(Circle((x, y), r, facecolor=fc, edgecolor=ec,
                        linewidth=1.1, zorder=3))
    ax.text(x, y + 0.3, label, ha="center", va="center", fontsize=FS,
            zorder=4)
    if check:
        ax.text(x + 3.4, y - 1.1, "\u2713", ha="center", va="center",
                fontsize=FS + 1, zorder=4)


def link(ax, p0, p1, color=GREY, lw=1.3, ls="-", rad=0.0, arrow=False,
         sh=7.5):
    ax.add_patch(FancyArrowPatch(p0, p1, connectionstyle=f"arc3,rad={rad}",
                 arrowstyle="-|>" if arrow else "-", mutation_scale=9,
                 color=color, lw=lw, linestyle=ls, zorder=2,
                 shrinkA=sh, shrinkB=sh))


def part_box(ax, x, y, w, h, color, edge=RED):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=1.4", facecolor=color, alpha=0.18,
                 edgecolor=edge, linewidth=1.8,
                 linestyle=":", zorder=1))


fig, ax = plt.subplots(figsize=(9.2, 4.8))
ax.set_xlim(0, 200)
ax.set_ylim(0, 106)
ax.axis("off")

# dividers
ax.plot([100, 100], [8, 103], color="#cccccc", lw=0.8, ls=(0, (4, 4)))
ax.plot([2, 198], [58, 58], color="#cccccc", lw=0.8, ls=(0, (4, 4)))

ax.text(50, 104, "(a) Before failure", ha="center", fontsize=FS_T)
ax.text(150, 104, "(b) After failure and repair", ha="center",
        fontsize=FS_T)
ax.text(1, 80, "Resource\ngraph", ha="left", va="center", fontsize=FS)
ax.text(1, 30, "Task graph\npartitions", ha="left", va="center",
        fontsize=FS)

# ---------------- resource graph (left) ----------------
res = {"A": (34, 82), "B": (54, 86), "C": (74, 82), "D": (54, 64),
       "E": (86, 64)}
for u, v, rad in [("A", "B", 0), ("B", "C", 0), ("A", "C", -0.3),
                  ("B", "D", 0), ("C", "D", 0), ("D", "E", 0)]:
    link(ax, res[u], res[v], rad=rad)
for k in ("A", "C", "D", "E"):
    node(ax, *res[k], k, r=7)
node(ax, *res["B"], "B", r=7, fc=C_AFF, ec=RED)
ax.annotate("B fails", xy=(59, 90), xytext=(64, 97),
            color=RED, fontsize=FS, ha="left",
            arrowprops=dict(arrowstyle="-|>", color=RED, lw=1.1))

# ---------------- resource graph (right) ----------------
res2 = {k: (x + 100, y) for k, (x, y) in res.items()}
for u, v in [("A", "B"), ("B", "C"), ("B", "D")]:
    link(ax, res2[u], res2[v], lw=1.1, color=RED, ls=(0, (3, 3)))
link(ax, res2["D"], res2["E"])
link(ax, res2["A"], res2["C"], rad=-0.3, lw=1.6, color=BLUE, arrow=True)
link(ax, res2["C"], res2["D"], lw=1.6, color=BLUE, arrow=True)
for k in ("A", "C", "D", "E"):
    node(ax, *res2[k], k, r=7)
ax.add_patch(Circle(res2["B"], 7, facecolor=C_AFF, edgecolor=RED,
                    lw=1.1, zorder=3))
bx, by = res2["B"]
ax.plot([bx - 5.5, bx + 5.5], [by - 5.5, by + 5.5], color=RED, lw=1.6,
        zorder=4)
ax.plot([bx - 5.5, bx + 5.5], [by + 5.5, by - 5.5], color=RED, lw=1.6,
        zorder=4)
ax.text(144, 93, "B failed", color=RED, fontsize=FS, ha="right")
ax.text(198, 102, "routes rerouted\nover surviving links", color=BLUE,
        fontsize=FS - 1.5, ha="right", va="top")

# ---------------- task graph (left) ----------------
node(ax, 56, 46, "T1", fc=C_DONE, ec=GREY, check=True)
part_box(ax, 56, 46, 13, 11, C_DONE)
ax.text(56, 54.8, "Partition A", ha="center", fontsize=FS - 1.5,
        color="#333333")

node(ax, 38, 34, "T2", fc=C_AFF, ec=RED)
node(ax, 38, 22, "T4", fc=C_AFF, ec=RED)
part_box(ax, 38, 28, 15, 24, C_AFF)
ax.text(38, 16.8, "Partition B", ha="center", fontsize=FS - 1.5,
        color="#333333")

node(ax, 74, 34, "T3", fc=C_OK, ec=GREEN)
part_box(ax, 74, 34, 13, 11, C_OK)
ax.text(82.5, 34, "Partition C", ha="left", va="center",
        fontsize=FS - 1.5, color="#333333")
node(ax, 74, 22, "T5", fc=C_OK, ec=GREEN)
part_box(ax, 74, 22, 13, 11, C_OK)
ax.text(82.5, 22, "Partition D", ha="left", va="center",
        fontsize=FS - 1.5, color="#333333")
node(ax, 56, 10, "T6", fc=C_OK, ec=GREEN)
part_box(ax, 56, 10, 13, 11, C_OK)
ax.text(64.5, 10, "Partition E", ha="left", va="center",
        fontsize=FS - 1.5, color="#333333")

link(ax, (52, 42), (41.5, 37), arrow=True, sh=6)
link(ax, (60, 42), (70.5, 37), arrow=True, sh=6)
link(ax, (38, 28.5), (38, 27.5), arrow=True, sh=6)
link(ax, (40, 16), (52, 13.5), arrow=True, sh=6)
link(ax, (70, 17.5), (60, 13.5), arrow=True, sh=6)

ax.add_patch(FancyArrowPatch((58, 61), (32, 39),
             connectionstyle="arc3,rad=0.35", arrowstyle="-|>",
             mutation_scale=9, color=RED, lw=1.1,
             linestyle=(0, (4, 3))))

ax.add_patch(FancyArrowPatch((84, 28), (97, 28), arrowstyle="-|>",
             mutation_scale=14, color="black", lw=2.6))
ax.text(90.5, 47, "repartition\naffected tasks\nonly", ha="center",
        fontsize=FS - 1.5)

# ---------------- task graph (right) ----------------
node(ax, 156, 46, "T1", fc=C_DONE, ec=GREY, check=True)
part_box(ax, 156, 46, 13, 11, C_DONE)
ax.text(156, 54.8, "Partition A", ha="center", fontsize=FS - 1.5,
        color="#333333")

part_box(ax, 156, 34, 48, 11, C_OK, edge=BLUE)
node(ax, 140, 34, "T2", fc=C_NEW, ec=BLUE)
node(ax, 170, 34, "T3", fc=C_OK, ec=GREEN)
part_box(ax, 156, 22, 48, 11, C_OK, edge=BLUE)
node(ax, 140, 22, "T4", fc=C_NEW, ec=BLUE)
node(ax, 170, 22, "T5", fc=C_OK, ec=GREEN)
node(ax, 156, 8, "T6", fc=C_OK, ec=GREEN)
part_box(ax, 156, 8, 13, 11, C_OK)
ax.text(164.5, 8, "Partition E", ha="left", va="center",
        fontsize=FS - 1.5, color="#333333")

link(ax, (152, 42), (141.5, 37), color=BLUE, arrow=True, sh=6)
link(ax, (160, 42), (166.5, 37), arrow=True, sh=6)
link(ax, (140, 28.5), (140, 27.5), color=BLUE, arrow=True, sh=6)
link(ax, (144, 16), (154, 11.5), color=BLUE, arrow=True, sh=6)
link(ax, (166, 17.5), (159, 12), arrow=True, sh=6)

ax.text(196, 53, "T2: B $\\rightarrow$ C\nT4: B $\\rightarrow$ D",
        color=BLUE, fontsize=FS, ha="right", va="top")

# ---------------- legend ----------------
handles = [
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_DONE,
           markeredgecolor=GREY, markersize=8, label="Completed task"),
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_AFF,
           markeredgecolor=RED, markersize=8, label="Affected task"),
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_NEW,
           markeredgecolor=BLUE, markersize=8, label="Reassigned task"),
    Line2D([0], [0], marker="o", color="none", markerfacecolor=C_OK,
           markeredgecolor=GREEN, markersize=8, label="Unaffected task"),
    Line2D([0], [0], color=GREY, lw=1.3, label="Resource link"),
    Line2D([0], [0], color=GREY, lw=1.3, marker=">", markersize=6,
           label="Task dependency"),
    Line2D([0], [0], color=RED, lw=1.8, linestyle=":",
           label="Partition boundary"),
    Line2D([0], [0], color=BLUE, lw=1.8, linestyle=":",
           label="Updated partition"),
]
ax.legend(handles=handles, loc="lower center",
          bbox_to_anchor=(0.5, -0.075), ncol=4, fontsize=FS - 1.5,
          frameon=False, columnspacing=1.1, handletextpad=0.4)

fig.savefig("figures/fig0_overview.png", dpi=220,
            bbox_inches="tight", facecolor="white")
fig.savefig("paper/figures/fig0_overview.png", dpi=220,
            bbox_inches="tight", facecolor="white")
print("wrote fig0_overview.png")
