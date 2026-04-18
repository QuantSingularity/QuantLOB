#!/usr/bin/env python3
"""
Offline training script for OrderFlowPredictor.

Reads a trade log CSV and a LOB time-series CSV, builds feature vectors
aligned by timestamp, and trains a logistic regression model via SGD.

Usage:
    python3 train_order_flow.py \
        --trades out/trades.csv \
        --snapshot out/lob_timeseries.csv \
        --weights out/order_flow_weights.json
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd

FEATURE_DIM = 40
DEFAULT_LR = 0.005
DEFAULT_REG = 0.01


def sigmoid(x: np.ndarray) -> np.ndarray:
    pos = x >= 0
    out = np.empty_like(x, dtype=float)
    out[pos] = 1.0 / (1.0 + np.exp(-x[pos]))
    ex = np.exp(x[~pos])
    out[~pos] = ex / (1.0 + ex)
    return out


def load_trade_labels(trade_path: str) -> pd.DataFrame:
    """Load trade log and infer aggressor direction."""
    df = pd.read_csv(trade_path)
    required = {"timestamp_ns", "buy_order_id", "sell_order_id"}
    if not required.issubset(df.columns):
        sys.exit(f"Missing columns in {trade_path}. Required: {required}")
    # Higher order ID was submitted later; treat as aggressor.
    df["label"] = (df["buy_order_id"] > df["sell_order_id"]).astype(float)
    return df[["timestamp_ns", "label"]]


def load_snapshot_features(snap_path: str) -> pd.DataFrame:
    """Load time-series CSV and compute a single representative feature per timestamp."""
    df = pd.read_csv(snap_path)
    bids = (
        df[df["side"] == "BID"]
        .groupby("timestamp_ns")
        .first()
        .rename(columns={"price": "bid_p1", "quantity": "bid_q1"})
    )
    asks = (
        df[df["side"] == "ASK"]
        .groupby("timestamp_ns")
        .first()
        .rename(columns={"price": "ask_p1", "quantity": "ask_q1"})
    )
    wide = bids.join(asks, how="outer").fillna(0.0)
    wide["mid"] = (wide["bid_p1"] + wide["ask_p1"]) / 2.0
    wide["spread"] = wide["ask_p1"] - wide["bid_p1"]
    wide["imbalance"] = (wide["bid_q1"] - wide["ask_q1"]) / (
        wide["bid_q1"] + wide["ask_q1"] + 1e-8
    )
    return wide.reset_index()


def build_dataset(
    trades: pd.DataFrame, snaps: pd.DataFrame
) -> tuple[np.ndarray, np.ndarray]:
    """Merge trade labels with nearest snapshot features."""
    snaps_sorted = snaps.sort_values("timestamp_ns").reset_index(drop=True)
    snap_ts = snaps_sorted["timestamp_ns"].values

    rows, labels = [], []
    for _, tr in trades.iterrows():
        ts = int(tr["timestamp_ns"])
        label = float(tr["label"])
        idx = int(np.searchsorted(snap_ts, ts, side="right")) - 1
        if idx < 0:
            continue
        snap = snaps_sorted.iloc[idx]
        # Build a simplified 40-dim feature vector from available fields
        mid = float(snap.get("mid", 0.0))
        sprd = float(snap.get("spread", 0.0))
        imb = float(snap.get("imbalance", 0.0))
        feat = np.zeros(FEATURE_DIM)
        feat[0] = sprd
        feat[1] = sprd / mid if mid > 0 else 0.0
        feat[4] = imb
        feat[5] = imb  # L2 imbalance (same here; only L1 data available)
        rows.append(feat)
        labels.append(label)

    return np.array(rows), np.array(labels)


def sgd_logistic(
    X: np.ndarray, y: np.ndarray, lr: float, reg: float, epochs: int
) -> tuple[np.ndarray, float]:
    n, d = X.shape
    w = np.zeros(d)
    b = 0.0
    batch = 256

    for epoch in range(epochs):
        idx = np.random.permutation(n)
        losses = []
        for start in range(0, n, batch):
            end = min(start + batch, n)
            Xb = X[idx[start:end]]
            yb = y[idx[start:end]]
            pred = sigmoid(Xb @ w + b)
            err = pred - yb
            loss = -float(
                np.mean(yb * np.log(pred + 1e-8) + (1 - yb) * np.log(1 - pred + 1e-8))
            )
            losses.append(loss)
            grad_w = (Xb.T @ err) / len(yb) + reg * w
            grad_b = np.mean(err)
            w -= lr * grad_w
            b -= lr * grad_b
        avg_loss = sum(losses) / len(losses)
        print(f"  Epoch {epoch+1:3d}/{epochs}  Log-loss={avg_loss:.4f}", flush=True)

    return w, b


def accuracy(X, y, w, b):
    pred = (sigmoid(X @ w + b) >= 0.5).astype(float)
    return float(np.mean(pred == y))


def main():
    ap = argparse.ArgumentParser(description="Train OrderFlowPredictor offline")
    ap.add_argument("--trades", required=True)
    ap.add_argument("--snapshot", required=True)
    ap.add_argument("--weights", required=True)
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--lr", type=float, default=DEFAULT_LR)
    ap.add_argument("--reg", type=float, default=DEFAULT_REG)
    ap.add_argument("--val-split", type=float, default=0.1)
    args = ap.parse_args()

    print(f"Loading trades from {args.trades} ...")
    trades = load_trade_labels(args.trades)
    print(f"  {len(trades)} trades loaded")

    print(f"Loading snapshots from {args.snapshot} ...")
    snaps = load_snapshot_features(args.snapshot)
    print(f"  {len(snaps)} snapshot timestamps")

    print("Building dataset ...")
    X, y = build_dataset(trades, snaps)
    print(f"  Dataset: {X.shape}, BUY fraction: {y.mean():.3f}")

    split = int(len(X) * (1 - args.val_split))
    X_tr, y_tr = X[:split], y[:split]
    X_va, y_va = X[split:], y[split:]

    mu = X_tr.mean(axis=0)
    std = X_tr.std(axis=0) + 1e-8
    X_tr = (X_tr - mu) / std
    X_va = (X_va - mu) / std

    print(f"Training: {len(X_tr)} samples  Validation: {len(X_va)} samples")
    w, b = sgd_logistic(X_tr, y_tr, args.lr, args.reg, args.epochs)

    val_acc = accuracy(X_va, y_va, w, b) if len(X_va) > 0 else float("nan")

    print(f"\nValidation accuracy: {val_acc*100:.2f}%")
    headers = ["Metric", "Value"]
    rows_data = [
        ["Val accuracy", f"{val_acc*100:.2f}%"],
        ["N train", str(len(X_tr))],
        ["N val", str(len(X_va))],
    ]
    col_w = [
        max(len(h), max(len(r[i]) for r in rows_data)) for i, h in enumerate(headers)
    ]

    def rs(r):
        return "| " + " | ".join(v.ljust(col_w[i]) for i, v in enumerate(r)) + " |"

    sep = "|-" + "-|-".join("-" * w2 for w2 in col_w) + "-|"
    print(rs(headers))
    print(sep)
    for r in rows_data:
        print(rs(r))

    out = {
        "model": "OrderFlowPredictor",
        "version": "1.0",
        "feature_dim": FEATURE_DIM,
        "learning_rate": args.lr,
        "regularisation": args.reg,
        "bias": float(b),
        "n_updates": int(len(X_tr) * args.epochs),
        "weights": [float(x) for x in w],
    }
    Path(args.weights).parent.mkdir(parents=True, exist_ok=True)
    with open(args.weights, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\nWeights saved to {args.weights}")


if __name__ == "__main__":
    main()
