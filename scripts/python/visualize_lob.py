#!/usr/bin/env python3
"""
QuantLOB - Order Book Visualization
QuantSingularity Research Institute

Reads snapshot CSVs and latency samples exported from the C++ engine and
renders bid/ask depth charts, cumulative depth curves, and latency plots.

"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
import pandas as pd

# ---------------------------------------------------------------------------
# Loaders
# ---------------------------------------------------------------------------


def load_snapshot(path: Path) -> pd.DataFrame:
    """Load a snapshot CSV written by Exporter::export_snapshot."""
    df = pd.read_csv(path, header=0)
    # Normalise column names (strip whitespace)
    df.columns = [c.strip() for c in df.columns]
    return df


def load_latency(path: Path) -> np.ndarray:
    """
    Load latency samples from a file written by Exporter::export_latency.

    """
    df = pd.read_csv(path, header=0)
    col = df.columns[0]
    return df[col].to_numpy(dtype=np.float64)


def load_trades(path: Path) -> pd.DataFrame:
    """Load a trade log CSV written by Exporter::write_trade."""
    df = pd.read_csv(path, header=0)
    df.columns = [c.strip() for c in df.columns]
    return df


# ---------------------------------------------------------------------------
# Plotting helpers
# ---------------------------------------------------------------------------


def _style() -> None:
    """Apply a clean dark-ish style consistently."""
    plt.rcParams.update(
        {
            "figure.facecolor": "#1e1e2e",
            "axes.facecolor": "#1e1e2e",
            "axes.edgecolor": "#555577",
            "axes.labelcolor": "#cdd6f4",
            "xtick.color": "#cdd6f4",
            "ytick.color": "#cdd6f4",
            "text.color": "#cdd6f4",
            "grid.color": "#313244",
            "grid.linestyle": "--",
            "grid.linewidth": 0.6,
            "legend.facecolor": "#1e1e2e",
            "legend.edgecolor": "#555577",
        }
    )


def plot_order_book(df: pd.DataFrame, symbol: str, output: Path) -> None:
    _style()

    bids = df[df["side"] == "BID"].sort_values("price", ascending=False).copy()
    asks = df[df["side"] == "ASK"].sort_values("price", ascending=True).copy()

    bids["cum_qty"] = bids["quantity"].cumsum()
    asks["cum_qty"] = asks["quantity"].cumsum()

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle(
        f"Order Book Snapshot - {symbol}",
        fontsize=14,
        fontweight="bold",
        color="#cdd6f4",
    )

    # --- Left: price level bar chart ---
    ax1 = axes[0]
    ax1.barh(
        bids["price"].astype(str),
        bids["quantity"],
        color="#a6e3a1",
        alpha=0.85,
        label="Bid",
    )
    ax1.barh(
        asks["price"].astype(str),
        asks["quantity"],
        color="#f38ba8",
        alpha=0.85,
        label="Ask",
    )
    ax1.set_xlabel("Quantity")
    ax1.set_ylabel("Price")
    ax1.set_title("Price Level Depth", color="#cdd6f4")
    ax1.legend()
    ax1.grid(True, axis="x")

    # --- Right: cumulative depth (step) ---
    ax2 = axes[1]
    if not bids.empty:
        ax2.step(
            bids["price"],
            bids["cum_qty"],
            color="#a6e3a1",
            where="post",
            label="Cum. Bid",
            linewidth=1.8,
        )
        ax2.fill_between(
            bids["price"], bids["cum_qty"], step="post", alpha=0.15, color="#a6e3a1"
        )
    if not asks.empty:
        ax2.step(
            asks["price"],
            asks["cum_qty"],
            color="#f38ba8",
            where="post",
            label="Cum. Ask",
            linewidth=1.8,
        )
        ax2.fill_between(
            asks["price"], asks["cum_qty"], step="post", alpha=0.15, color="#f38ba8"
        )
    ax2.set_xlabel("Price")
    ax2.set_ylabel("Cumulative Quantity")
    ax2.set_title("Cumulative Depth", color="#cdd6f4")
    ax2.legend()
    ax2.grid(True)
    ax2.xaxis.set_major_formatter(mticker.FormatStrFormatter("%.4f"))

    plt.tight_layout()
    plt.savefig(output, dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
    print(f"Saved: {output}")
    plt.close()


def plot_latency(samples: np.ndarray, output: Path) -> None:
    _style()

    if len(samples) == 0:
        print("No latency samples to plot.")
        return

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle(
        "Matching Engine Latency Distribution",
        fontsize=14,
        fontweight="bold",
        color="#cdd6f4",
    )

    p50 = np.percentile(samples, 50)
    p99 = np.percentile(samples, 99)
    p999 = np.percentile(samples, 99.9)

    # --- Left: histogram ---
    ax1 = axes[0]
    ax1.hist(
        samples,
        bins=min(100, len(samples) // 10 + 1),
        color="#89b4fa",
        alpha=0.85,
        edgecolor="none",
    )
    ax1.axvline(
        p50,
        color="#fab387",
        linestyle="--",
        linewidth=1.4,
        label=f"p50  = {p50:.0f} ns",
    )
    ax1.axvline(
        p99,
        color="#f38ba8",
        linestyle="--",
        linewidth=1.4,
        label=f"p99  = {p99:.0f} ns",
    )
    ax1.axvline(
        p999,
        color="#cba6f7",
        linestyle="--",
        linewidth=1.4,
        label=f"p99.9= {p999:.0f} ns",
    )
    ax1.set_xlabel("Latency (ns)")
    ax1.set_ylabel("Count")
    ax1.set_title("Latency Histogram", color="#cdd6f4")
    ax1.legend(fontsize=9)
    ax1.grid(True, axis="x")

    # --- Right: percentile curve ---
    ax2 = axes[1]
    pctls = np.linspace(0, 99.99, min(10_000, len(samples)))
    values = np.percentile(samples, pctls)
    ax2.plot(pctls, values, color="#89b4fa", linewidth=1.4)
    ax2.set_xlabel("Percentile")
    ax2.set_ylabel("Latency (ns)")
    ax2.set_title("Percentile Curve", color="#cdd6f4")
    ax2.set_yscale("log")
    ax2.grid(True, which="both")

    summary = {
        "count": len(samples),
        "mean": float(np.mean(samples)),
        "min": float(np.min(samples)),
        "p50": float(p50),
        "p99": float(p99),
        "p99.9": float(p999),
        "max": float(np.max(samples)),
    }

    print("\nLatency Summary (ns)")
    print("-" * 32)
    for k, v in summary.items():
        if k == "count":
            print(f"  {k:<8}: {int(v):>12,}")
        else:
            print(f"  {k:<8}: {v:>12.1f}")

    plt.tight_layout()
    plt.savefig(output, dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
    print(f"\nSaved: {output}")
    plt.close()


def plot_trades(df: pd.DataFrame, output: Path) -> None:
    """Plot trade price history and volume."""
    _style()

    if df.empty:
        print("No trades to plot.")
        return

    df = df.copy()
    # Convert ns timestamp to a sequential index for simplicity
    df["trade_idx"] = range(len(df))

    fig, axes = plt.subplots(2, 1, figsize=(14, 8), sharex=True)
    fig.suptitle("Trade Log", fontsize=14, fontweight="bold", color="#cdd6f4")

    ax1 = axes[0]
    ax1.plot(df["trade_idx"], df["price"], color="#89dceb", linewidth=1.2)
    ax1.set_ylabel("Price")
    ax1.set_title("Trade Price", color="#cdd6f4")
    ax1.grid(True)

    ax2 = axes[1]
    ax2.bar(df["trade_idx"], df["quantity"], color="#a6e3a1", alpha=0.8, width=1.0)
    ax2.set_xlabel("Trade #")
    ax2.set_ylabel("Quantity")
    ax2.set_title("Trade Volume", color="#cdd6f4")
    ax2.grid(True, axis="y")

    plt.tight_layout()
    plt.savefig(output, dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
    print(f"Saved: {output}")
    plt.close()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description="QuantLOB Visualization - plot order book, latency, and trades"
    )
    sub = parser.add_subparsers(dest="command")

    p_book = sub.add_parser("book", help="Plot order book snapshot")
    p_book.add_argument("file", type=Path, help="Snapshot CSV path")
    p_book.add_argument("--symbol", default="UNKNOWN")
    p_book.add_argument("--out", type=Path, default=Path("orderbook.png"))

    p_lat = sub.add_parser("latency", help="Plot latency distribution")
    p_lat.add_argument(
        "file", type=Path, help="Latency samples CSV (one value per line)"
    )
    p_lat.add_argument("--out", type=Path, default=Path("latency.png"))

    p_trades = sub.add_parser("trades", help="Plot trade log")
    p_trades.add_argument("file", type=Path, help="Trades CSV path")
    p_trades.add_argument("--out", type=Path, default=Path("trades.png"))

    args = parser.parse_args()

    if args.command == "book":
        df = load_snapshot(args.file)
        plot_order_book(df, args.symbol, args.out)
    elif args.command == "latency":
        samples = load_latency(args.file)
        plot_latency(samples, args.out)
    elif args.command == "trades":
        df = load_trades(args.file)
        plot_trades(df, args.out)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
