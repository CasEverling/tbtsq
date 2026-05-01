"""
plot_latency.py
───────────────
Reads the CSVs produced by tests/benchmarking.cpp and generates two sets
of figures using Seaborn:

  Figure 1 – Cross-queue comparison per workload
  ───────────────────────────────────────────────
  For every workload × role (enqueue / dequeue) pair:
    • One figure with overlapping ECDF curves, one line per queue.
    • Tail region (p90 → max) is zoomed in a separate inset.
    • Saved as:  plots/compare_<workload>_<role>.png

  Figure 2 – Single queue across all workloads
  ─────────────────────────────────────────────
  For every queue × role pair:
    • One figure with overlapping ECDF curves, one line per workload.
    • Same zoomed inset on the tail.
    • Saved as:  plots/workloads_<queue>_<role>.png

  Figure 3 – Summary heatmap
  ──────────────────────────
  p99 cycles for every (queue × workload) pair, enqueue and dequeue
  side-by-side.
    • Saved as:  plots/heatmap_p99.png

Usage
─────
  python plot_latency.py [--results DIR] [--plots DIR] [--dpi N]

  Defaults: --results results/  --plots plots/  --dpi 150
"""

import argparse
import os
import warnings
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from mpl_toolkits.axes_grid1.inset_locator import inset_axes
import seaborn as sns

# ── Aesthetics ────────────────────────────────────────────────────────────────

sns.set_theme(style="darkgrid", context="paper", font_scale=1.15)

# Distinct palette – up to 8 entries (5 queues + 5 workloads, never both at once)
PALETTE = sns.color_palette("tab10")

QUEUE_ORDER = [
    "coarse_mutex",
    "two_locks",
    "ms_queue",
    "spsc_ring",
    "vuykov",
    "hazard_pointer",
]

WORKLOAD_ORDER = [
    "lightly_loaded",
    "balanced",
    "write_heavy",
    "read_heavy",
    "contended",
]

ROLES = ["enqueue", "dequeue"]

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_raw(results_dir: Path) -> dict[tuple[str, str], pd.DataFrame]:
    """
    Returns  { (queue_name, workload_name) : DataFrame[role, thread_id, seq, cycles] }
    Skips missing files silently.
    """
    data = {}
    for csv in results_dir.glob("*.csv"):
        if csv.stem == "summary":
            continue
        # filename format: <queue>_<workload>.csv
        # workload names contain underscores too, so split from the right on
        # the known workload suffixes
        stem = csv.stem
        for wl in WORKLOAD_ORDER:
            if stem.endswith("_" + wl):
                queue = stem[: -(len(wl) + 1)]
                try:
                    df = pd.read_csv(csv)
                    data[(queue, wl)] = df
                except Exception as e:
                    warnings.warn(f"Could not read {csv}: {e}")
                break
    return data


def ecdf(series: pd.Series) -> tuple[np.ndarray, np.ndarray]:
    """Return (sorted_values, probabilities) for an empirical CDF."""
    x = np.sort(series.dropna().values)
    y = np.arange(1, len(x) + 1) / len(x)
    return x, y


def percentile_label(ax, x, y, p, color, fontsize=7):
    """Annotate a single percentile tick on an ECDF axis."""
    idx = np.searchsorted(y, p / 100.0)
    idx = min(idx, len(x) - 1)
    ax.axvline(x[idx], color=color, linestyle=":", linewidth=0.8, alpha=0.5)


def add_tail_inset(parent_ax, lines_data: list[tuple[np.ndarray, np.ndarray, str, str]],
                   tail_lo: float = 0.90):
    """
    Add a zoomed inset showing the tail (percentile > tail_lo).
    lines_data: list of (x, y, color, label)
    """
    axins = inset_axes(parent_ax, width="38%", height="42%", loc="lower right",
                       bbox_to_anchor=parent_ax.bbox,
                       bbox_transform=parent_ax.transAxes)
    axins.set_facecolor("#1e1e2e" if sns.axes_style()["axes.facecolor"] == "white"
                        else parent_ax.get_facecolor())

    x_mins, x_maxs = [], []
    for x, y, color, label in lines_data:
        mask = y >= tail_lo
        if mask.sum() < 2:
            continue
        axins.plot(x[mask], y[mask] * 100, color=color, linewidth=1.2)
        x_mins.append(x[mask][0])
        x_maxs.append(x[mask][-1])

    if x_mins:
        pad = (max(x_maxs) - min(x_mins)) * 0.05 or 1
        axins.set_xlim(min(x_mins) - pad, max(x_maxs) + pad)
    axins.set_ylim(tail_lo * 100, 100.2)
    axins.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.1f%%"))
    axins.tick_params(labelsize=6)
    axins.set_title("tail", fontsize=7, pad=2)
    axins.grid(True, linewidth=0.4, alpha=0.5)
    return axins


def save(fig, path: Path, dpi: int):
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    print(f"  saved → {path}")


# ── Figure 1: cross-queue comparison per workload ─────────────────────────────

def plot_compare_queues(raw: dict, plots_dir: Path, dpi: int):
    """
    For each (workload, role): ECDF curves, one line per queue.
    """
    for wl in WORKLOAD_ORDER:
        for role in ROLES:
            present = [(q, raw[(q, wl)]) for q in QUEUE_ORDER if (q, wl) in raw]
            if not present:
                continue

            fig, ax = plt.subplots(figsize=(8, 5))
            lines_data = []

            for i, (queue, df) in enumerate(present):
                role_df = df[df["role"] == role]
                if role_df.empty:
                    continue
                x, y = ecdf(role_df["cycles"])
                color = PALETTE[i % len(PALETTE)]
                ax.plot(x, y * 100, color=color, linewidth=1.6,
                        label=queue.replace("_", " "))
                lines_data.append((x, y, color, queue))

            if not lines_data:
                plt.close(fig)
                continue

            ax.set_xlabel("Latency (cycles)", fontsize=11)
            ax.set_ylabel("Percentile (%)", fontsize=11)
            ax.set_title(
                f"Queue comparison — {wl.replace('_', ' ')} · {role}",
                fontsize=13, fontweight="bold"
            )
            ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.0f%%"))
            ax.legend(fontsize=9, framealpha=0.7)

            # Mark key percentiles
            for p in (50, 90, 99, 99.9):
                ax.axhline(p, color="grey", linewidth=0.5, linestyle="--", alpha=0.4)
                ax.text(ax.get_xlim()[0], p + 0.3, f"p{p}", fontsize=6,
                        color="grey", va="bottom")

            add_tail_inset(ax, lines_data)

            fname = plots_dir / f"compare_{wl}_{role}.png"
            save(fig, fname, dpi)


# ── Figure 2: single queue across workloads ───────────────────────────────────

def plot_workloads_per_queue(raw: dict, plots_dir: Path, dpi: int):
    """
    For each (queue, role): ECDF curves, one line per workload.
    """
    for queue in QUEUE_ORDER:
        for role in ROLES:
            present = [(wl, raw[(queue, wl)]) for wl in WORKLOAD_ORDER
                       if (queue, wl) in raw]
            if not present:
                continue

            fig, ax = plt.subplots(figsize=(8, 5))
            lines_data = []

            for i, (wl, df) in enumerate(present):
                role_df = df[df["role"] == role]
                if role_df.empty:
                    continue
                x, y = ecdf(role_df["cycles"])
                color = PALETTE[i % len(PALETTE)]
                ax.plot(x, y * 100, color=color, linewidth=1.6,
                        label=wl.replace("_", " "))
                lines_data.append((x, y, color, wl))

            if not lines_data:
                plt.close(fig)
                continue

            ax.set_xlabel("Latency (cycles)", fontsize=11)
            ax.set_ylabel("Percentile (%)", fontsize=11)
            ax.set_title(
                f"{queue.replace('_', ' ')} — workload comparison · {role}",
                fontsize=13, fontweight="bold"
            )
            ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.0f%%"))
            ax.legend(fontsize=9, framealpha=0.7)

            for p in (50, 90, 99, 99.9):
                ax.axhline(p, color="grey", linewidth=0.5, linestyle="--", alpha=0.4)
                ax.text(ax.get_xlim()[0], p + 0.3, f"p{p}", fontsize=6,
                        color="grey", va="bottom")

            add_tail_inset(ax, lines_data)

            fname = plots_dir / f"workloads_{queue}_{role}.png"
            save(fig, fname, dpi)


# ── Figure 3: p99 heatmap ─────────────────────────────────────────────────────

def plot_heatmap(summary_path: Path, plots_dir: Path, dpi: int):
    if not summary_path.exists():
        warnings.warn(f"summary.csv not found at {summary_path}, skipping heatmap.")
        return

    df = pd.read_csv(summary_path)
    df.columns = df.columns.str.strip()

    fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=True)

    for ax, role in zip(axes, ROLES):
        sub = df[df["role"] == role].copy()

        # Keep only queues and workloads we know about
        sub = sub[sub["queue"].isin(QUEUE_ORDER) & sub["workload"].isin(WORKLOAD_ORDER)]

        pivot = sub.pivot_table(index="queue", columns="workload",
                                values="p99", aggfunc="mean")

        # Reorder axes
        pivot = pivot.reindex(
            index=[q for q in QUEUE_ORDER if q in pivot.index],
            columns=[w for w in WORKLOAD_ORDER if w in pivot.columns]
        )

        sns.heatmap(
            pivot,
            ax=ax,
            annot=True,
            fmt=".0f",
            cmap="YlOrRd",
            linewidths=0.4,
            cbar_kws={"label": "p99 latency (cycles)"},
        )
        ax.set_title(f"p99 latency — {role}", fontsize=12, fontweight="bold")
        ax.set_xlabel("Workload", fontsize=10)
        ax.set_ylabel("Queue" if role == "enqueue" else "", fontsize=10)
        ax.tick_params(axis="x", rotation=30, labelsize=9)
        ax.tick_params(axis="y", rotation=0,  labelsize=9)

    fig.suptitle("p99 tail latency heatmap (cycles)", fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()
    save(fig, plots_dir / "heatmap_p99.png", dpi)


# ── Figure 4: tail bar chart (p50/p99/p99.9/p99.99) per queue per workload ────

def plot_tail_bars(summary_path: Path, plots_dir: Path, dpi: int):
    """
    Grouped bar chart showing p50, p99, p99.9, p99.99 for each queue,
    one subplot per workload, for enqueue and dequeue side by side.
    """
    if not summary_path.exists():
        return

    df = pd.read_csv(summary_path)
    df.columns = df.columns.str.strip()

    pct_cols = {"p50": "p50", "p99": "p99", "p99.9": "p999", "p99.99": "p9999"}

    for wl in WORKLOAD_ORDER:
        sub = df[df["workload"] == wl]
        if sub.empty:
            continue

        fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=False)

        for ax, role in zip(axes, ROLES):
            role_df = sub[sub["role"] == role].copy()
            role_df = role_df[role_df["queue"].isin(QUEUE_ORDER)]
            role_df["queue"] = pd.Categorical(
                role_df["queue"], categories=QUEUE_ORDER, ordered=True
            )
            role_df = role_df.sort_values("queue")

            x = np.arange(len(role_df))
            width = 0.18
            offsets = np.linspace(-1.5 * width, 1.5 * width, len(pct_cols))

            for j, (label, col) in enumerate(pct_cols.items()):
                if col not in role_df.columns:
                    continue
                bars = ax.bar(
                    x + offsets[j],
                    role_df[col].values,
                    width,
                    label=label,
                    color=PALETTE[j],
                    alpha=0.85,
                    edgecolor="white",
                    linewidth=0.4,
                )

            ax.set_xticks(x)
            ax.set_xticklabels(
                [q.replace("_", "\n") for q in role_df["queue"].tolist()],
                fontsize=8
            )
            ax.set_ylabel("Latency (cycles)", fontsize=10)
            ax.set_title(f"{role} — {wl.replace('_', ' ')}", fontsize=11,
                         fontweight="bold")
            ax.legend(fontsize=8, framealpha=0.7)
            ax.yaxis.grid(True, linewidth=0.5, alpha=0.6)
            ax.set_axisbelow(True)

        fig.suptitle(
            f"Tail-latency percentiles — {wl.replace('_', ' ')}",
            fontsize=13, fontweight="bold"
        )
        fig.tight_layout()
        save(fig, plots_dir / f"tail_bars_{wl}.png", dpi)


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", default="results",
                        help="Directory containing benchmark CSV files (default: results/)")
    parser.add_argument("--plots",   default="plots",
                        help="Output directory for PNG figures (default: plots/)")
    parser.add_argument("--dpi",     type=int, default=150,
                        help="Figure DPI (default: 150)")
    args = parser.parse_args()

    results_dir = Path(args.results)
    plots_dir   = Path(args.plots)
    summary_csv = results_dir / "summary.csv"

    if not results_dir.exists():
        print(f"[error] results directory not found: {results_dir}")
        print("  Run the C++ benchmark first: ./bench_all")
        return

    print(f"[plot] loading raw CSVs from {results_dir}/")
    raw = load_raw(results_dir)
    print(f"  found {len(raw)} (queue, workload) pair(s)")

    print("\n[plot] Figure 1 — cross-queue comparison per workload")
    plot_compare_queues(raw, plots_dir, args.dpi)

    print("\n[plot] Figure 2 — single queue across workloads")
    plot_workloads_per_queue(raw, plots_dir, args.dpi)

    print("\n[plot] Figure 3 — p99 heatmap")
    plot_heatmap(summary_csv, plots_dir, args.dpi)

    print("\n[plot] Figure 4 — tail bar charts")
    plot_tail_bars(summary_csv, plots_dir, args.dpi)

    print(f"\n[done] all plots written to {plots_dir}/")


if __name__ == "__main__":
    main()
