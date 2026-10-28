"""Publication-style overview figure: failure + partial repartitioning."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch
from matplotlib.lines import Line2D

FS = 8.5
FS_T = 10
LW = 0.75

C_DONE = "#c9c9c9"      # completed task
C_AFF = "#e88f8f"       # affected task
C_NEW = "#9cc4e4"       # reassigned task
C_OK = "#a9d6a9"        # unaffected task
RED = "#b03030"
BLUE = "#1f5fa8"
GREY = "#444444"


def rnode(ax, x, y, label, r=6.0, fc="white", dead=False):
    """Resource node."""
    ec = RED if dead else "black"
    ax.add_patch(Circle((x, y), r, facecolor=fc, edgecolor=ec,
                        linewidth=LW, zorder=3))
    if dead:
        d = r * 0.62
        for s in (1, -1):
            ax.plot([x - d, x + d], [y - s * d, y + s * d], color=RED,
                    lw=LW, zorder=4)
    else:
        ax.text(x, y + 0.4, label, ha="center", va="center",
                fontsize=FS + 1, zorder=4)


def tnode(ax, x, y, label, fc, ec, w=13.5, h=7.5, check=False):
    """Task node: rounded rectangle, status via border colour."""
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=0.6,rounding_size=1.6",
                 facecolor="white", edgecolor=ec, linewidth=LW,
                 zorder=3))
    ax.text(x, y + 0.2, label, ha="center", va="center", fontsize=FS,
            zorder=4)
    if check:
        ax.text(x + w / 2 - 2.6, y - 0.2, "\u2713", ha="center",
                va="center", fontsize=FS - 1, zorder=4)


def elink(ax, p0, p1, color=GREY, ls="-", rad=0.0, arrow=False, sh=7.0):
    ax.add_patch(FancyArrowPatch(p0, p1, connectionstyle=f"arc3,rad={rad}",
                 arrowstyle="-|>" if arrow else "-", mutation_scale=10,
                 color=color, lw=LW, linestyle=ls, zorder=2,
                 shrinkA=sh, shrinkB=sh))


def partition(ax, x, y, w, h, tint, edge, label, lx=None, ly=None):
    """Dotted partition boundary with a small corner label."""
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=1.6,rounding_size=2.4",
                 facecolor="none", edgecolor=edge,
                 linewidth=LW, linestyle=(0, (1.6, 2.2)), zorder=1))
    ax.text(lx if lx is not None else x,
            ly if ly is not None else y + h / 2 + 3.4,
            label, ha="center", va="center", fontsize=FS - 1,
            color="#333333", zorder=4)


fig, ax = plt.subplots(figsize=(9.4, 4.4))
ax.set_xlim(0, 200)
ax.set_ylim(0, 100)
ax.axis("off")

# dividers
ax.plot([100, 100], [9, 97], color="#dddddd", lw=0.9)
ax.plot([2, 198], [55, 55], color="#dddddd", lw=0.9)

ax.text(50, 97, "(a) Before failure", ha="center", fontsize=FS_T)
ax.text(150, 97, "(b) After failure and repair", ha="center",
        fontsize=FS_T)
ax.text(3, 92, "resource\ngraph", ha="left", va="top", fontsize=FS - 1,
        color="#666666", style="italic")
ax.text(3, 50, "task DAG\npartitions", ha="left", va="top",
        fontsize=FS - 1, color="#666666", style="italic")

# ---------------- panel (a): resource graph ----------------
R = {"A": (32, 78), "B": (52, 84), "C": (72, 78), "D": (52, 62),
     "E": (84, 62)}
for u, v, rad in [("A", "B", 0), ("B", "C", 0), ("A", "C", -0.28),
                  ("B", "D", 0), ("C", "D", 0), ("D", "E", 0)]:
    elink(ax, R[u], R[v], rad=rad)
for k in ("A", "C", "D", "E"):
    rnode(ax, *R[k], k)
rnode(ax, *R["B"], "B", fc=C_AFF, dead=False)
ax.add_patch(Circle(R["B"], 6.0, facecolor="#f3d5d5", edgecolor=RED,
                    lw=LW, zorder=3))
ax.text(R["B"][0], R["B"][1] + 0.4, "B", ha="center", va="center",
        fontsize=FS + 1, zorder=4)
ax.annotate("B fails", xy=(57.5, 86.5), xytext=(64, 91), color=RED,
            fontsize=FS, ha="left",
            arrowprops=dict(arrowstyle="-", color=RED, lw=0.9))

# ---------------- panel (b): resource graph ----------------
R2 = {k: (x + 100, y) for k, (x, y) in R.items()}
for u, v in [("A", "B"), ("B", "C"), ("B", "D")]:
    elink(ax, R2[u], R2[v], color=RED, ls=(0, (3, 3)))
elink(ax, R2["D"], R2["E"])
elink(ax, R2["A"], R2["C"], rad=-0.28, color=BLUE, arrow=True)
elink(ax, R2["C"], R2["D"], color=BLUE, arrow=True)
for k in ("A", "C", "D", "E"):
    rnode(ax, *R2[k], k)
rnode(ax, *R2["B"], "", fc=C_AFF, dead=True)
ax.text(143, 92, "B failed", color=RED, fontsize=FS, ha="right")

# ---------------- panel (a): task DAG ----------------
def tdep(ax, p0, p1, color=GREY, rad=0.0):
    elink(ax, p0, p1, color=color, rad=rad, arrow=True, sh=8.0)

tnode(ax, 50, 44, "T1", C_DONE, "#777777", check=True)
partition(ax, 50, 44, 17, 11, C_DONE, RED, "$P_A$")

tnode(ax, 34, 31, "T2", C_AFF, RED)
tnode(ax, 34, 19, "T4", C_AFF, RED)
partition(ax, 34, 25, 19, 26, C_AFF, RED, "$P_B$")

tnode(ax, 70, 31, "T3", C_OK, "#4d8a4d")
partition(ax, 70, 31, 17, 11, C_OK, RED, "$P_C$")
tnode(ax, 70, 19, "T5", C_OK, "#4d8a4d")
partition(ax, 70, 19, 17, 11, C_OK, RED, "$P_D$", lx=60.5, ly=19)
tnode(ax, 52, 7, "T6", C_OK, "#4d8a4d")
partition(ax, 52, 7, 17, 10, C_OK, RED, "$P_E$", lx=63, ly=7)

tdep(ax, (44, 41.5), (39, 34.5), rad=-0.25)
tdep(ax, (56, 41.5), (65, 34.5), rad=0.25)
tdep(ax, (34, 27), (34, 23.2))
tdep(ax, (38, 15.5), (47, 10.5), rad=-0.2)
tdep(ax, (66, 15.5), (57, 10.5), rad=0.2)

# failure -> affected partition
elink(ax, (56, 58), (28, 38), color=RED, ls=(0, (4, 3)), rad=0.3,
      arrow=True, sh=6)
ax.text(100, 33.5, "partial repartitioning", ha="center", va="center",
        fontsize=FS - 1, color="#333333")
elink(ax, (86, 28), (114, 28), color="black", arrow=True, sh=0)

# ---------------- panel (b): task DAG ----------------
tnode(ax, 152, 44, "T1", C_DONE, "#777777", check=True)
partition(ax, 152, 44, 17, 11, C_DONE, RED, "$P_A$")

# updated partitions: same boxes, new members, blue dotted boundary
tnode(ax, 136, 31, "T2", C_NEW, BLUE)
tnode(ax, 170, 31, "T3", C_OK, "#4d8a4d")
partition(ax, 153, 31, 50, 11, C_OK, BLUE, "$P_C$  (updated)")
tnode(ax, 136, 19, "T4", C_NEW, BLUE)
tnode(ax, 170, 19, "T5", C_OK, "#4d8a4d")
partition(ax, 153, 19, 50, 11, C_OK, BLUE, "$P_D$  (updated)",
          lx=126, ly=15)
tnode(ax, 152, 7, "T6", C_OK, "#4d8a4d")
partition(ax, 152, 7, 17, 10, C_OK, RED, "$P_E$", lx=163, ly=7)

tdep(ax, (146, 41.5), (139, 34.5), color=BLUE, rad=-0.25)
tdep(ax, (158, 41.5), (166, 34.5), color=GREY, rad=0.25)
tdep(ax, (136, 27), (136, 23.2), color=BLUE)
tdep(ax, (140, 15.5), (149, 10.5), color=BLUE, rad=-0.2)
tdep(ax, (166, 15.5), (157, 10.5), color=GREY, rad=0.2)

# ---------------- legend ----------------
handles = [
    Line2D([0], [0], marker="s", color="none", markerfacecolor="white",
           markeredgecolor="#777777", markersize=8,
           label="completed task"),
    Line2D([0], [0], marker="s", color="none", markerfacecolor="white",
           markeredgecolor=RED, markersize=8, label="affected task"),
    Line2D([0], [0], marker="s", color="none", markerfacecolor="white",
           markeredgecolor=BLUE, markersize=8, label="reassigned task"),
    Line2D([0], [0], marker="s", color="none", markerfacecolor="white",
           markeredgecolor="#4d8a4d", markersize=8,
           label="unaffected task"),
    Line2D([0], [0], color=GREY, lw=LW, label="resource link"),
    Line2D([0], [0], color=GREY, lw=LW, marker=">", markersize=6,
           label="task dependency"),
    Line2D([0], [0], color=RED, lw=LW, linestyle=":",
           label="partition"),
    Line2D([0], [0], color=BLUE, lw=LW, linestyle=":",
           label="updated partition"),
]
ax.legend(handles=handles, loc="lower center",
          bbox_to_anchor=(0.5, -0.085), ncol=4, fontsize=FS - 1,
          frameon=False, columnspacing=1.2, handletextpad=0.45)

fig.savefig("figures/fig0_overview.png", dpi=220,
            bbox_inches="tight", facecolor="white")
fig.savefig("paper/figures/fig0_overview.png", dpi=220,
            bbox_inches="tight", facecolor="white")
print("wrote fig0_overview.png")
