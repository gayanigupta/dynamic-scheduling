"""Motivating scenario figure: federated smart-city analytics (story fig)."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Polygon
from matplotlib.lines import Line2D

FS = 8.5
FS_T = 10
LW = 0.75

RED = "#b03030"
BLUE = "#1f5fa8"
GREY = "#444444"
DOM = "#555555"


def rnode(ax, x, y, label, r=5.5, dead=False):
    ec = RED if dead else "black"
    fc = "#f3d5d5" if dead else "white"
    ax.add_patch(Circle((x, y), r, facecolor=fc, edgecolor=ec,
                        linewidth=LW, zorder=3))
    if dead:
        d = r * 0.62
        for s in (1, -1):
            ax.plot([x - d, x + d], [y - s * d, y + s * d], color=RED,
                    lw=LW, zorder=4)
    else:
        ax.text(x, y + 0.3, label, ha="center", va="center",
                fontsize=FS - 0.5, zorder=4)


def domain(ax, x, y, w, h, label):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=1.8,rounding_size=2.6",
                 facecolor="none", edgecolor=DOM, linewidth=LW,
                 linestyle=(0, (4, 2.5)), zorder=1))
    ax.text(x, y + h / 2 + 1.8, label, ha="center", va="center",
            fontsize=FS - 1, color="#333333", zorder=5,
            bbox=dict(facecolor="white", edgecolor="none", pad=1.5))


def elink(ax, p0, p1, color=GREY, ls="-", rad=0.0, arrow=False, sh=6.0):
    ax.add_patch(FancyArrowPatch(p0, p1, connectionstyle=f"arc3,rad={rad}",
                 arrowstyle="-|>" if arrow else "-", mutation_scale=9,
                 color=color, lw=LW, linestyle=ls, zorder=2,
                 shrinkA=sh, shrinkB=sh))


def tnode(ax, x, y, label, w=17.5, h=7.5):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=0.6,rounding_size=1.6",
                 facecolor="white", edgecolor="black", linewidth=LW,
                 zorder=3))
    ax.text(x, y + 0.2, label, ha="center", va="center",
            fontsize=FS - 0.5, zorder=4)


def partition(ax, x, y, w, h, label, edge=RED, lx=None, ly=None):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=1.6,rounding_size=2.4",
                 facecolor="none", edgecolor=edge, linewidth=LW,
                 linestyle=(0, (1.6, 2.2)), zorder=1))
    ax.text(lx if lx is not None else x,
            ly if ly is not None else y + h / 2 + 1.8,
            label, ha="center", va="center",
            fontsize=FS - 1, color="#333333", zorder=5,
            bbox=dict(facecolor="white", edgecolor="none", pad=1.5))


def camera(ax, x, y):
    ax.add_patch(FancyBboxPatch((x - 2.6, y - 1.6), 4.6, 3.2,
                 boxstyle="round,pad=0.4,rounding_size=0.8",
                 facecolor="white", edgecolor="black", linewidth=LW,
                 zorder=3))
    ax.add_patch(Polygon([[x + 1.8, y + 0.7], [x + 4.2, y + 2.0],
                          [x + 4.2, y - 2.0], [x + 1.8, y - 0.7]],
                         closed=True, facecolor="white",
                         edgecolor="black", linewidth=LW, zorder=3))
    ax.text(x, y - 4.2, "camera", ha="center", va="center",
            fontsize=FS - 1.5, color="#555555", zorder=4)


fig, ax = plt.subplots(figsize=(9.4, 4.4))
ax.set_xlim(0, 200)
ax.set_ylim(0, 104)
ax.axis("off")

ax.plot([2, 198], [56, 56], color="#dddddd", lw=0.9)
ax.text(3, 98, "federated\ninfrastructure", ha="left", va="top",
        fontsize=FS - 1, color="#666666", style="italic")
ax.text(3, 50, "analytics\nworkflow", ha="left", va="top",
        fontsize=FS - 1, color="#666666", style="italic")

# ---------------- three federated domains ----------------
domain(ax, 48, 73, 54, 26, "Traffic authority")
rnode(ax, 36, 77, "$f_1$")
rnode(ax, 60, 68, "$f_2$")
elink(ax, (36, 77), (60, 68))
camera(ax, 11, 83)
camera(ax, 11, 73)
elink(ax, (15.5, 82), (30.5, 78), arrow=True, sh=3)
elink(ax, (15.5, 74), (30.5, 76), arrow=True, sh=3)

domain(ax, 112, 73, 52, 26, "Transit agency")
rnode(ax, 102, 71, "$f_3$", dead=True)
rnode(ax, 122, 77, "$f_4$")
elink(ax, (102, 71), (122, 77))

domain(ax, 168, 73, 52, 26, "Public safety")
rnode(ax, 158, 71, "$f_5$")
rnode(ax, 178, 77, "$f_6$")
elink(ax, (158, 71), (178, 77))

elink(ax, (60, 68), (102, 71), rad=-0.18)
elink(ax, (122, 77), (158, 71), rad=0.18)
ax.text(110, 96, "resource federation\n(shared capacity across domains)",
        ha="center", fontsize=FS - 1, color="#555555")
ax.text(102, 61.5, "member fails", color=RED, fontsize=FS - 1,
        ha="center", va="center",
        bbox=dict(facecolor="white", edgecolor="none", pad=1.0))

# ---------------- branching analytics DAG ----------------
# ingest -> {detect, count} -> track -> aggregate -> alert
tnode(ax, 24, 33, "ingest")
tnode(ax, 62, 41, "detect")
tnode(ax, 62, 25, "count")
tnode(ax, 100, 33, "track")
tnode(ax, 138, 33, "aggregate")
tnode(ax, 180, 33, "alert")

elink(ax, (33, 34.5), (52.5, 40), arrow=True, rad=-0.15, sh=5)
elink(ax, (33, 31.5), (52.5, 26), arrow=True, rad=0.15, sh=5)
elink(ax, (71.5, 40), (91, 34.5), arrow=True, rad=-0.15, sh=5)
elink(ax, (71.5, 26), (91, 31.5), arrow=True, rad=0.15, sh=5)
elink(ax, (109, 33), (129, 33), arrow=True, sh=5)
elink(ax, (147, 33), (171, 33), arrow=True, sh=5)

# partitions pinned to resources
partition(ax, 24, 33, 21, 11, "$P_1 \\rightarrow f_1$")
partition(ax, 62, 33, 21, 30, "$P_2 \\rightarrow f_2$", ly=50.5)
partition(ax, 100, 33, 21, 11, "$P_3 \\rightarrow f_3$ (failed)", edge=RED)
partition(ax, 158.5, 33, 62, 11, "$P_4 \\rightarrow f_4$", lx=158.5)


# mapping hints (dashed) from infra to partitions
elink(ax, (36, 71.2), (28, 45.5), ls=(0, (3, 3)), rad=0.15, sh=4)
elink(ax, (102, 65), (100, 38.5), ls=(0, (3, 3)), sh=4)

ax.text(100, 8, "each partition is pinned to a fog resource; failure of one\n"
        "member affects only the tasks staged on it",
        ha="center", fontsize=FS - 1, color="#555555")

fig.savefig("figures/fig0_story.png", dpi=220,
            bbox_inches="tight", facecolor="white")
fig.savefig("paper/figures/fig0_story.png", dpi=220,
            bbox_inches="tight", facecolor="white")
print("wrote fig0_story.png")
