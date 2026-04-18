#!/usr/bin/env python3
"""
Offline training script for MidPricePredictor.

Reads a LOB time-series CSV (produced by --export-timeseries),
builds feature vectors, trains a ridge regression model using SGD,
and exports weights to a JSON file loadable by C++ MidPricePredictor.

Usage:
    python3 train_mid_price.py --data out/lob_timeseries.csv \
        --weights out/mid_price_weights.json --epochs 20 --lr 0.001
"""

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd

FEATURE_DIM = 40
DEFAULT_LR = 0.001
DEFAULT_REG = 0.01


def load_timeseries(path: str) -> pd.DataFrame:
    """Load and pivot LOB time-series CSV into a wide snapshot table."""
    df = pd.read_csv(path)
    required = {"timestamp_ns", "side", "price", "quantity"}
    if not required.issubset(df.columns):
        sys.exit(f"Missing columns in {path}. Required: {required}")

    bids = df[df["side"] == "BID"].copy()
    asks = df[df["side"] == "ASK"].copy()

    bids["rank"] = (
        bids.groupby("timestamp_ns")["price"]
        .rank(ascending=False, method="first")
        .astype(int)
    )
    asks["rank"] = (
        asks.groupby("timestamp_ns")["price"]
        .rank(ascending=True, method="first")
        .astype(int)
    )

    def pivot_side(side_df, prefix):
        out = {}
        for r in range(1, 7):
            lvl = side_df[side_df["rank"] == r][["timestamp_ns", "price", "quantity"]]
            lvl = lvl.rename(
                columns={"price": f"{prefix}_p{r}", "quantity": f"{prefix}_q{r}"}
            )
            out[r] = lvl.set_index("timestamp_ns")
        return pd.concat(out.values(), axis=1)

    bid_wide = pivot_side(bids, "bid")
    ask_wide = pivot_side(asks, "ask")
    wide = pd.concat([bid_wide, ask_wide], axis=1).sort_index().fillna(0.0)
    return wide


def compute_features(wide: pd.DataFrame) -> tuple[np.ndarray, np.ndarray]:
    """Build feature matrix X and mid-price delta target y."""
    rows = []
    ts_index = wide.index.tolist()

    mid_prices = []
    for ts in ts_index:
        row = wide.loc[ts]
        b1 = float(row.get("bid_p1", 0.0))
        a1 = float(row.get("ask_p1", 0.0))
        mid = (b1 + a1) / 2.0 if b1 > 0 and a1 > 0 else 0.0
        mid_prices.append(mid)

    for i, ts in enumerate(ts_index):
        if i < 10:
            rows.append(np.zeros(FEATURE_DIM))
            continue

        row = wide.loc[ts]
        wide.loc[ts_index[i - 1]]

        b1 = float(row.get("bid_p1", 0.0))
        a1 = float(row.get("ask_p1", 0.0))
        mid = (b1 + a1) / 2.0 if b1 > 0 and a1 > 0 else 0.0
        pm = mid_prices[i - 1] if i > 0 else mid

        spread = a1 - b1 if b1 > 0 and a1 > 0 else 0.0
        rel_sprd = spread / mid if mid > 0 else 0.0
        mid_chg = mid - pm
        log_ret = math.log(mid / pm) if pm > 0 and mid > 0 else 0.0

        bq = [float(row.get(f"bid_q{r}", 0)) for r in range(1, 7)]
        aq = [float(row.get(f"ask_q{r}", 0)) for r in range(1, 7)]
        bp = [float(row.get(f"bid_p{r}", 0)) for r in range(1, 7)]
        ap = [float(row.get(f"ask_p{r}", 0)) for r in range(1, 7)]

        def imb(n):
            b = sum(bq[:n])
            a = sum(aq[:n])
            return (b - a) / (b + a) if (b + a) > 0 else 0.0

        total_b = sum(bq)
        total_a = sum(aq)
        depth_ratio = total_b / total_a if total_a > 0 else 0.0

        def vwap(prices, qtys):
            w = sum(p * q for p, q in zip(prices, qtys))
            q = sum(qtys)
            return w / q if q > 0 else 0.0

        window_mids = mid_prices[max(0, i - 20) : i]
        momentum = window_mids[-1] - window_mids[0] if len(window_mids) >= 2 else 0.0
        if len(window_mids) >= 3:
            rets = [
                math.log(window_mids[j] / window_mids[j - 1])
                for j in range(1, len(window_mids))
                if window_mids[j - 1] > 0 and window_mids[j] > 0
            ]
            vol = float(np.std(rets)) if rets else 0.0
        else:
            vol = 0.0

        norm_bq = [q / total_b if total_b > 0 else 0.0 for q in bq]
        norm_aq = [q / total_a if total_a > 0 else 0.0 for q in aq]
        rel_bp = [(p - mid) / mid if mid > 0 else 0.0 for p in bp]
        rel_ap = [(p - mid) / mid if mid > 0 else 0.0 for p in ap]

        feat = [
            spread,
            rel_sprd,
            mid_chg,
            log_ret,
            imb(1),
            imb(2),
            imb(3),
            total_b,
            total_a,
            depth_ratio,
            vwap(bp, bq),
            vwap(ap, aq),
            0.0,  # trade_flow placeholder
            0.0,  # trade_intensity placeholder
            momentum,
            vol,
            *norm_bq,
            *norm_aq,
            *rel_bp,
            *rel_ap,
        ]
        rows.append(np.array(feat[:FEATURE_DIM], dtype=float))

    X = np.array(rows)
    mids = np.array(mid_prices)
    y = np.zeros(len(mids))
    y[:-1] = mids[1:] - mids[:-1]  # 1-tick-ahead delta

    valid = np.array([i >= 10 for i in range(len(rows))])
    return X[valid], y[valid]


def sgd_ridge(
    X: np.ndarray, y: np.ndarray, lr: float, reg: float, epochs: int
) -> tuple[np.ndarray, float]:
    """Mini-batch SGD ridge regression."""
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
            pred = Xb @ w + b
            err = pred - yb
            loss = float(np.mean(err**2))
            losses.append(loss)
            grad_w = (Xb.T @ err) / len(yb) + reg * w
            grad_b = np.mean(err)
            w -= lr * grad_w
            b -= lr * grad_b
        rmse = math.sqrt(sum(losses) / len(losses))
        print(f"  Epoch {epoch+1:3d}/{epochs}  RMSE={rmse:.6f}", flush=True)

    return w, b


def directional_accuracy(
    X: np.ndarray, y: np.ndarray, w: np.ndarray, b: float
) -> float:
    pred = X @ w + b
    signs = np.sign(pred) == np.sign(y)
    nonzero = y != 0
    return float(np.mean(signs[nonzero])) if nonzero.any() else 0.0


def main():
    ap = argparse.ArgumentParser(description="Train MidPricePredictor offline")
    ap.add_argument("--data", required=True, help="LOB time-series CSV")
    ap.add_argument("--weights", required=True, help="Output weight JSON path")
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--lr", type=float, default=DEFAULT_LR)
    ap.add_argument("--reg", type=float, default=DEFAULT_REG)
    ap.add_argument(
        "--val-split",
        type=float,
        default=0.1,
        help="Fraction of data held out for validation",
    )
    args = ap.parse_args()

    print(f"Loading {args.data} ...")
    wide = load_timeseries(args.data)
    print(f"  {len(wide)} snapshots loaded")

    print("Building features ...")
    X, y = compute_features(wide)
    print(f"  Feature matrix: {X.shape}")

    split = int(len(X) * (1 - args.val_split))
    X_tr, y_tr = X[:split], y[:split]
    X_va, y_va = X[split:], y[split:]

    print(f"Training: {len(X_tr)} samples  Validation: {len(X_va)} samples")
    print(f"LR={args.lr}  Reg={args.reg}  Epochs={args.epochs}")

    # Standardise features
    mu = X_tr.mean(axis=0)
    std = X_tr.std(axis=0) + 1e-8
    X_tr = (X_tr - mu) / std
    X_va = (X_va - mu) / std

    w, b = sgd_ridge(X_tr, y_tr, args.lr, args.reg, args.epochs)

    val_pred = X_va @ w + b
    val_rmse = math.sqrt(float(np.mean((val_pred - y_va) ** 2)))
    val_mae = float(np.mean(np.abs(val_pred - y_va)))
    dir_acc = directional_accuracy(X_va, y_va, w, b)

    print(f"\nValidation results:")

    headers = ["Metric", "Value"]
    rows = [
        ["Val RMSE", f"{val_rmse:.6f}"],
        ["Val MAE", f"{val_mae:.6f}"],
        ["Directional acc", f"{dir_acc*100:.2f}%"],
        ["N train", str(len(X_tr))],
        ["N val", str(len(X_va))],
    ]
    col_w = [max(len(h), max(len(r[i]) for r in rows)) for i, h in enumerate(headers)]

    def row_str(r):
        return "| " + " | ".join(v.ljust(col_w[i]) for i, v in enumerate(r)) + " |"

    sep = "|-" + "-|-".join("-" * w for w in col_w) + "-|"
    print(row_str(headers))
    print(sep)
    for r in rows:
        print(row_str(r))

    out = {
        "model": "MidPricePredictor",
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
