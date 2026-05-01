"""
plot_latency.py
───────────────
IEEE-formatted tail-latency plots from benchmarking CSVs.

Figures
  1  compare_<workload>_<role>.pdf   ECDF per queue, one workload
  2  workloads_<queue>_<role>.pdf    ECDF per workload, one queue
  3  heatmap_p99.pdf                 p99 heatmap
  4  tail_bars_<workload>.pdf        grouped percentile bar chart

Each figure is saved as PDF (vector, for LaTeX) and PNG at ≤150 DPI
(preview only — kept small to avoid matplotlib pixel-limit crashes).

Usage
  python plot_latency.py [--results DIR] [--plots DIR] [--dpi N] [--width W]
  Defaults: results/  plots/  600 dpi  7.16 in (double-column)
"""

import argparse
import warnings
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from matplotlib.lines import Line2D
import seaborn as sns

# ── IEEE style ────────────────────────────────────────────────────────────────

def apply_ieee_style():
    plt.rcParams.update({
        "font.family":           "serif",
        "font.serif":            ["Times New Roman", "DejaVu Serif", "serif"],
        "font.size":             8,
        "axes.titlesize":        9,
        "axes.titleweight":      "normal",
        "axes.labelsize":        8,
        "xtick.labelsize":       7,
        "ytick.labelsize":       7,
        "legend.fontsize":       7,
        "lines.linewidth":       1.0,
        "lines.markersize":      3,
        "axes.linewidth":        0.5,
        "axes.spines.top":       False,
        "axes.spines.right":     False,
        "axes.grid":             True,
        "grid.linewidth":        0.4,
        "grid.alpha":            0.4,
        "grid.color":            "#aaaaaa",
        "xtick.major.width":     0.5,
        "ytick.major.width":     0.5,
        "xtick.direction":       "in",
        "ytick.direction":       "in",
        "figure.facecolor":      "white",
        "axes.facecolor":        "white",
        "savefig.facecolor":     "white",
        "savefig.bbox":          "tight",
        "savefig.pad_inches":    0.02,
        "pdf.fonttype":          42,
        "ps.fonttype":           42,
    })

# Six styles that survive B&W printing: (color, linestyle, marker, markevery)
LINE_STYLES = [
    ("#000000", "-",  "o",  0.12),
    ("#000000", "--", "s",  0.12),
    ("#000000", ":",  "^",  0.12),
    ("#555555", "-",  "D",  0.12),
    ("#555555", "--", "v",  0.12),
    ("#555555", ":",  "x",  0.12),
]

QUEUE_ORDER = [
    "coarse_mutex", "two_locks", "ms_queue",
    "spsc_ring",    "vuykov",    "hazard_pointer",
]
WORKLOAD_ORDER = [
    "lightly_loaded", "balanced", "write_heavy", "read_heavy", "contended",
]
ROLES = ["enqueue", "dequeue"]

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_raw(results_dir: Path) -> dict:
    data = {}
    for csv in results_dir.glob("*.csv"):
        if csv.stem == "summary":
            continue
        stem = csv.stem
        for wl in WORKLOAD_ORDER:
            if stem.endswith("_" + wl):
                queue = stem[: -(len(wl) + 1)]
                try:
                    data[(queue, wl)] = pd.read_csv(csv)
                except Exception as e:
                    warnings.warn(f"Could not read {csv}: {e}")
                break
    return data


def ecdf(series: pd.Series, max_points: int = 10_000):
    x = np.sort(series.dropna().values)
    y = np.arange(1, len(x) + 1) / len(x)
    if len(x) > max_points:
        idx = np.linspace(0, len(x) - 1, max_points, dtype=int)
        x, y = x[idx], y[idx]
    return x, y


def new_fig(width: float, height: float):
    # NOTE: no tight_layout / constrained_layout — we call
    # fig.subplots_adjust() manually so inset axes don't cause warnings.
    fig, ax = plt.subplots(figsize=(width, height))
    return fig, ax


def style_ax(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_linewidth(0.5)
    ax.tick_params(which="both", direction="in", width=0.5, length=3)


def add_pct_lines(ax):
    for p in (50, 90, 99, 99.9):
        ax.axhline(p, color="#888888", linewidth=0.4, linestyle="--", zorder=0)


def add_tail_inset(parent_ax, lines_data, tail_lo=0.90):
    from mpl_toolkits.axes_grid1.inset_locator import inset_axes
    axins = inset_axes(parent_ax, width="34%", height="38%",
                       loc="lower right")
    axins.set_facecolor("white")
    x_mins, x_maxs = [], []
    for x, y, c, ls, mk, me in lines_data:
        mask = y >= tail_lo
        if mask.sum() < 2:
            continue
        n = mask.sum()
        axins.plot(x[mask], y[mask] * 100,
                   color=c, linestyle=ls,
                   marker=mk, markevery=max(1, int(n * me)),
                   linewidth=0.8, markersize=2)
        x_mins.append(x[mask][0])
        x_maxs.append(x[mask][-1])
    if x_mins:
        pad = (max(x_maxs) - min(x_mins)) * 0.05 or 1
        axins.set_xlim(min(x_mins) - pad, max(x_maxs) + pad)
    axins.set_ylim(tail_lo * 100, 100.15)
    axins.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.1f%%"))
    axins.tick_params(labelsize=5, width=0.4, length=2, direction="in")
    axins.set_title("Tail", fontsize=5, pad=1)
    axins.grid(True, linewidth=0.3, alpha=0.4)
    axins.spines["top"].set_visible(False)
    axins.spines["right"].set_visible(False)
    return axins


def legend_handles(entries):
    return [Line2D([0], [0], color=c, linestyle=ls, marker=mk,
                   markersize=3, linewidth=1.0, label=lbl)
            for c, ls, mk, _, lbl in entries]


def save(fig, path: Path, dpi: int):
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.subplots_adjust(left=0.12, right=0.97, top=0.93, bottom=0.13)
    # PDF — full quality vector for LaTeX
    fig.savefig(path.with_suffix(".pdf"), dpi=dpi)
    # PNG — hard cap at 150 DPI to avoid pixel-limit MemoryError
    fig.savefig(path.with_suffix(".png"), dpi=min(dpi, 150))
    plt.close(fig)
    print(f"  saved → {path.stem}.pdf + .png")


# ── Figure 1 ──────────────────────────────────────────────────────────────────

def plot_compare_queues(raw, plots_dir, dpi, width):
    for wl in WORKLOAD_ORDER:
        for role in ROLES:
            present = [(q, raw[(q, wl)]) for q in QUEUE_ORDER if (q, wl) in raw]
            if not present:
                continue
            fig, ax = new_fig(width, width * 0.65)
            lines_data, legend_ent = [], []
            for i, (queue, df) in enumerate(present):
                rdf = df[df["role"] == role]
                if rdf.empty:
                    continue
                x, y = ecdf(rdf["cycles"])
                c, ls, mk, me = LINE_STYLES[i % len(LINE_STYLES)]
                ax.plot(x, y * 100, color=c, linestyle=ls,
                        marker=mk, markevery=max(1, int(len(x) * me)),
                        linewidth=1.0, markersize=3)
                lines_data.append((x, y, c, ls, mk, me))
                legend_ent.append((c, ls, mk, me, queue.replace("_", " ")))
            if not lines_data:
                plt.close(fig)
                continue
            add_pct_lines(ax)
            ax.set_xlabel("Latency (cycles)")
            ax.set_ylabel("Cumulative probability (%)")
            ax.set_title(f"Queue comparison: {wl.replace('_',' ')} ({role})")
            ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.0f%%"))
            ax.legend(handles=legend_handles(legend_ent),
                      frameon=True, framealpha=0.9,
                      edgecolor="#cccccc", fancybox=False, handlelength=2.5)
            style_ax(ax)
            add_tail_inset(ax, lines_data)
            save(fig, plots_dir / f"compare_{wl}_{role}", dpi)


# ── Figure 2 ──────────────────────────────────────────────────────────────────

def plot_workloads_per_queue(raw, plots_dir, dpi, width):
    for queue in QUEUE_ORDER:
        for role in ROLES:
            present = [(wl, raw[(queue, wl)]) for wl in WORKLOAD_ORDER
                       if (queue, wl) in raw]
            if not present:
                continue
            fig, ax = new_fig(width, width * 0.65)
            lines_data, legend_ent = [], []
            for i, (wl, df) in enumerate(present):
                rdf = df[df["role"] == role]
                if rdf.empty:
                    continue
                x, y = ecdf(rdf["cycles"])
                c, ls, mk, me = LINE_STYLES[i % len(LINE_STYLES)]
                ax.plot(x, y * 100, color=c, linestyle=ls,
                        marker=mk, markevery=max(1, int(len(x) * me)),
                        linewidth=1.0, markersize=3)
                lines_data.append((x, y, c, ls, mk, me))
                legend_ent.append((c, ls, mk, me, wl.replace("_", " ")))
            if not lines_data:
                plt.close(fig)
                continue
            add_pct_lines(ax)
            ax.set_xlabel("Latency (cycles)")
            ax.set_ylabel("Cumulative probability (%)")
            ax.set_title(f"{queue.replace('_',' ')}: workload comparison ({role})")
            ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.0f%%"))
            ax.legend(handles=legend_handles(legend_ent),
                      frameon=True, framealpha=0.9,
                      edgecolor="#cccccc", fancybox=False, handlelength=2.5)
            style_ax(ax)
            add_tail_inset(ax, lines_data)
            save(fig, plots_dir / f"workloads_{queue}_{role}", dpi)


# ── Figure 3 ──────────────────────────────────────────────────────────────────

def plot_heatmap(summary_path, plots_dir, dpi, width):
    if not summary_path.exists():
        warnings.warn(f"summary.csv not found at {summary_path}, skipping.")
        return
    df = pd.read_csv(summary_path)
    df.columns = df.columns.str.strip()
    fig, axes = plt.subplots(1, 2, figsize=(width, width * 0.4), sharey=True)
    for ax, role in zip(axes, ROLES):
        sub = df[(df["role"] == role) &
                 df["queue"].isin(QUEUE_ORDER) &
                 df["workload"].isin(WORKLOAD_ORDER)].copy()
        pivot = sub.pivot_table(index="queue", columns="workload",
                                values="p99", aggfunc="mean")
        pivot = pivot.reindex(
            index=[q for q in QUEUE_ORDER if q in pivot.index],
            columns=[w for w in WORKLOAD_ORDER if w in pivot.columns])
        pivot.index   = [q.replace("_", " ") for q in pivot.index]
        pivot.columns = [w.replace("_", "\n") for w in pivot.columns]
        sns.heatmap(pivot, ax=ax, annot=True, fmt=".0f",
                    annot_kws={"size": 6, "family": "Times New Roman"},
                    cmap="Greys", linewidths=0.3, linecolor="#dddddd",
                    cbar_kws={"label": "p99 (cycles)", "shrink": 0.8})
        ax.set_title(f"p99 latency ({role})")
        ax.set_xlabel("Workload")
        ax.set_ylabel("Queue" if role == "enqueue" else "")
        ax.tick_params(axis="x", rotation=0,  labelsize=7)
        ax.tick_params(axis="y", rotation=0,  labelsize=7)
    fig.subplots_adjust(left=0.18, right=0.97, top=0.90, bottom=0.12, wspace=0.08)
    fig.savefig((plots_dir / "heatmap_p99").with_suffix(".pdf"), dpi=dpi)
    fig.savefig((plots_dir / "heatmap_p99").with_suffix(".png"), dpi=min(dpi, 150))
    plt.close(fig)
    print("  saved → heatmap_p99.pdf + .png")


# ── Figure 4 ──────────────────────────────────────────────────────────────────

HATCHES   = ["", "///", "...", "xxx"]
BAR_GREYS = ["#ffffff", "#aaaaaa", "#555555", "#111111"]

def plot_tail_bars(summary_path, plots_dir, dpi, width):
    if not summary_path.exists():
        return
    df = pd.read_csv(summary_path)
    df.columns = df.columns.str.strip()
    pct_cols = {"p50": "p50", "p99": "p99", "p99.9": "p999", "p99.99": "p9999"}
    for wl in WORKLOAD_ORDER:
        sub = df[df["workload"] == wl]
        if sub.empty:
            continue
        fig, axes = plt.subplots(1, 2, figsize=(width, width * 0.4))
        for ax, role in zip(axes, ROLES):
            rdf = sub[sub["role"] == role].copy()
            rdf = rdf[rdf["queue"].isin(QUEUE_ORDER)]
            rdf["queue"] = pd.Categorical(rdf["queue"],
                                          categories=QUEUE_ORDER, ordered=True)
            rdf = rdf.sort_values("queue")
            x        = np.arange(len(rdf))
            n        = len(pct_cols)
            bw       = 0.6 / n
            offsets  = np.linspace(-(n-1)/2*bw, (n-1)/2*bw, n)
            for j, (lbl, col) in enumerate(pct_cols.items()):
                if col not in rdf.columns:
                    continue
                ax.bar(x + offsets[j], rdf[col].values, bw,
                       label=lbl, color=BAR_GREYS[j],
                       edgecolor="black", linewidth=0.4, hatch=HATCHES[j])
            ax.set_xticks(x)
            ax.set_xticklabels([q.replace("_", "\n") for q in rdf["queue"].tolist()],
                               fontsize=6)
            ax.set_ylabel("Latency (cycles)")
            ax.set_title(f"{role} ({wl.replace('_', ' ')})")
            ax.legend(frameon=True, framealpha=0.9,
                      edgecolor="#cccccc", fancybox=False)
            ax.yaxis.grid(True, linewidth=0.4, alpha=0.5, color="#aaaaaa")
            ax.set_axisbelow(True)
            style_ax(ax)
        fig.subplots_adjust(left=0.10, right=0.97, top=0.90, bottom=0.18, wspace=0.3)
        fig.savefig((plots_dir / f"tail_bars_{wl}").with_suffix(".pdf"), dpi=dpi)
        fig.savefig((plots_dir / f"tail_bars_{wl}").with_suffix(".png"), dpi=min(dpi, 150))
        plt.close(fig)
        print(f"  saved → tail_bars_{wl}.pdf + .png")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", default="results")
    parser.add_argument("--plots",   default="plots")
    parser.add_argument("--dpi",     type=int,   default=600)
    parser.add_argument("--width",   type=float, default=7.16,
                        help="3.5 = single col, 7.16 = double col (inches)")
    args = parser.parse_args()

    apply_ieee_style()

    results_dir = Path(args.results)
    plots_dir   = Path(args.plots)
    summary_csv = results_dir / "summary.csv"

    if not results_dir.exists():
        print(f"[error] results/ not found — run bench_all first")
        return

    print(f"[plot] loading raw CSVs from {results_dir}/")
    raw = load_raw(results_dir)
    print(f"  found {len(raw)} (queue, workload) pair(s)")

    print("\n[plot] Figure 1 — cross-queue comparison per workload")
    plot_compare_queues(raw, plots_dir, args.dpi, args.width)

    print("\n[plot] Figure 2 — single queue across workloads")
    plot_workloads_per_queue(raw, plots_dir, args.dpi, args.width)

    print("\n[plot] Figure 3 — p99 heatmap")
    plot_heatmap(summary_csv, plots_dir, args.dpi, args.width)

    print("\n[plot] Figure 4 — tail bar charts")
    plot_tail_bars(summary_csv, plots_dir, args.dpi, args.width)

    print(f"\n[done] figures in {plots_dir}/")
    print(  "       .pdf → LaTeX  |  .png → preview")


if __name__ == "__main__":
    main()
