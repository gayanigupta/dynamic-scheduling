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
GREEN = "#4d8a4d"
GREY = "#444444"
DOM = "#555555"


def rnode(ax, x, y, label, r=5.5):
    ax.add_patch(Circle((x, y), r, facecolor="white", edgecolor="black",
                        linewidth=LW, zorder=3))
    ax.text(x, y + 0.3, label, ha="center", va="center",
            fontsize=FS - 0.5, zorder=4)


def domain(ax, x, y, w, h, label):
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                 boxstyle="round,pad=1.8,rounding_size=2.6",
                 facecolor="none", edgecolor=DOM, linewidth=LW,
                 linestyle=(0, (4, 2.5)), zorder=1))
    ax.text(x, y + h / 2 + 2.6, label, ha="center", va="center",
            fontsize=FS - 1, color="#333333", zorder=4)


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
            ly if ly is not None else y + h / 2 + 2.8,
            label, ha="center", va="center",
            fontsize=FS - 1, color="#333333", zorder=4)


def camera(ax, x, y):
    """Minimal camera icon: body + lens."""
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


fig, ax = plt.subplots(figsize=(9.4, 4.1))
ax.set_xlim(0, 200)
ax.set_ylim(0, 96)
ax.axis("off")

ax.plot([2, 198], [50, 50], color="#dddddd", lw=0.9)
ax.text(3, 92, "federated\ninfrastructure", ha="left", va="top",
        fontsize=FS - 1, color="#666666", style="italic")
ax.text(3, 44, "analytics\nworkflow", ha="left", va="top",
        fontsize=FS - 1, color="#666666", style="italic")

# ---------------- three federated domains ----------------
# Traffic authority: two fog nodes
domain(ax, 48, 68, 54, 26, "Traffic authority")
rnode(ax, 36, 72, "$f_1$")
rnode(ax, 60, 63, "$f_2$")
elink(ax, (36, 72), (60, 63))
camera(ax, 11, 78)
camera(ax, 11, 68)
elink(ax, (15.5, 77), (30.5, 73), arrow=True, sh=3)
elink(ax, (15.5, 69), (30.5, 71), arrow=True, sh=3)

# Transit agency
domain(ax, 110, 68, 52, 26, "Transit agency")
rnode(ax, 100, 66, "$f_3$")
rnode(ax, 120, 72, "$f_4$")
elink(ax, (100, 66), (120, 72))

# Public safety
domain(ax, 168, 68, 52, 26, "Public safety")
rnode(ax, 158, 66, "$f_5$")
rnode(ax, 178, 72, "$f_6$")
elink(ax, (158, 66), (178, 72))

# federation links across domains
elink(ax, (60, 63), (100, 66), rad=-0.18)
elink(ax, (120, 72), (158, 66), rad=0.18)
ax.text(100, 90, "resource federation\n(shared capacity across domains)",
        ha="center", fontsize=FS - 1, color="#555555")

# ---------------- analytics DAG ----------------
xs = [30, 70, 110, 150, 186]
labels = ["ingest", "detect", "track", "aggregate", "alert"]
for x, lab in zip(xs, labels):
    tnode(ax, x, 30, lab)
for i in range(4):
    elink(ax, (xs[i] + 8.75, 30), (xs[i + 1] - 8.75, 30), arrow=True,
          sh=5)

# partition boundaries mapped onto resources
partition(ax, 30, 30, 21, 11, "$P_1 \\rightarrow f_1$")
partition(ax, 90, 30, 62, 11, "$P_2 \\rightarrow f_2$", lx=90, ly=38.6)
partition(ax, 150, 30, 21, 11, "$P_3 \\rightarrow f_4$")
partition(ax, 186, 30, 21, 11, "$P_4 \\rightarrow f_5$")

# mapping hints
elink(ax, (30, 54.5), (30, 37), ls=(0, (3, 3)), sh=3)
elink(ax, (110, 54.5), (150, 37), ls=(0, (3, 3)), rad=-0.15, sh=3)

ax.text(100, 9, "each partition is pinned to a fog resource; failure of\n"
        "one resource affects only the tasks staged on it",
        ha="center", fontsize=FS - 1, color="#555555")

fig.savefig("figures/fig0_story.png", dpi=220,
            bbox_inches="tight", facecolor="white")
fig.savefig("paper/figures/fig0_story.png", dpi=220,
            bbox_inches="tight", facecolor="white")
print("wrote fig0_story.png")
