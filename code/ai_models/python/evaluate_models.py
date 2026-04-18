#!/usr/bin/env python3
"""
Load exported weights and evaluate both models on held-out data.

Usage:
    python3 evaluate_models.py \
        --data out/lob_timeseries.csv \
        --mid-weights out/mid_price_weights.json \
        --flow-weights out/order_flow_weights.json
"""

import argparse
import json
import math

import numpy as np
import pandas as pd


def sigmoid(x):
    pos = x >= 0
    out = np.empty_like(x, dtype=float)
    out[pos] = 1.0 / (1.0 + np.exp(-x[pos]))
    ex = np.exp(x[~pos])
    out[~pos] = ex / (1.0 + ex)
    return out


def print_table(headers, rows):
    col_w = [
        max(len(h), max(len(str(r[i])) for r in rows)) for i, h in enumerate(headers)
    ]

    def rs(r):
        return "| " + " | ".join(str(v).ljust(col_w[i]) for i, v in enumerate(r)) + " |"

    sep = "|-" + "-|-".join("-" * w for w in col_w) + "-|"
    print(rs(headers))
    print(sep)
    for r in rows:
        print(rs(r))


def load_weights(path):
    with open(path) as f:
        d = json.load(f)
    return np.array(d["weights"]), float(d["bias"]), d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--mid-weights", required=True)
    ap.add_argument("--flow-weights", required=True)
    ap.add_argument("--horizon", type=int, default=1)
    args = ap.parse_args()

    df = pd.read_csv(args.data)
    bids = (
        df[df["side"] == "BID"].groupby("timestamp_ns").first()[["price", "quantity"]]
    )
    asks = (
        df[df["side"] == "ASK"].groupby("timestamp_ns").first()[["price", "quantity"]]
    )
    bids.columns = ["bid_p", "bid_q"]
    asks.columns = ["ask_p", "ask_q"]
    wide = bids.join(asks, how="outer").fillna(0.0).reset_index()
    wide["mid"] = (wide["bid_p"] + wide["ask_p"]) / 2.0
    wide["imb"] = (wide["bid_q"] - wide["ask_q"]) / (
        wide["bid_q"] + wide["ask_q"] + 1e-8
    )

    mids = wide["mid"].values

    print(f"Evaluating on {len(wide)} snapshots\n")

    # MidPricePredictor
    w_mid, b_mid, meta_mid = load_weights(args.mid_weights)
    X_mid = np.zeros((len(wide), len(w_mid)))
    for i, row in wide.iterrows():
        X_mid[i, 0] = float(row["ask_p"]) - float(row["bid_p"])
        X_mid[i, 4] = float(row["imb"])

    mu = X_mid.mean(axis=0)
    std = X_mid.std(axis=0) + 1e-8
    X_n = (X_mid - mu) / std
    y_pred = X_n @ w_mid + b_mid
    y_true = np.zeros(len(mids))
    y_true[: -args.horizon] = mids[args.horizon :] - mids[: -args.horizon]

    valid = (mids > 0) & (np.arange(len(mids)) < len(mids) - args.horizon)
    mae = float(np.mean(np.abs(y_pred[valid] - y_true[valid])))
    rmse = math.sqrt(float(np.mean((y_pred[valid] - y_true[valid]) ** 2)))
    nz = y_true[valid] != 0
    dir_acc = (
        float(np.mean(np.sign(y_pred[valid][nz]) == np.sign(y_true[valid][nz])))
        if nz.any()
        else 0.0
    )

    print("MidPricePredictor results:")
    print_table(
        ["Metric", "Value"],
        [
            ["MAE", f"{mae:.6f}"],
            ["RMSE", f"{rmse:.6f}"],
            ["Dir accuracy", f"{dir_acc*100:.2f}%"],
            ["N updates (train)", str(meta_mid.get("n_updates", "?"))],
        ],
    )
    print()

    # OrderFlowPredictor
    w_fl, b_fl, meta_fl = load_weights(args.flow_weights)
    X_fl = np.zeros((len(wide), len(w_fl)))
    for i, row in wide.iterrows():
        sp = float(row["ask_p"]) - float(row["bid_p"])
        mid = float(row["mid"])
        X_fl[i, 0] = sp
        X_fl[i, 1] = sp / mid if mid > 0 else 0.0
        X_fl[i, 4] = float(row["imb"])

    mu2 = X_fl.mean(axis=0)
    std2 = X_fl.std(axis=0) + 1e-8
    X_fl_n = (X_fl - mu2) / std2

    # Label: BUY if mid rises next tick
    flow_labels = (y_true > 0).astype(float)
    probs = sigmoid(X_fl_n @ w_fl + b_fl)
    acc = float(np.mean((probs >= 0.5).astype(float)[valid] == flow_labels[valid]))

    print("OrderFlowPredictor results:")
    print_table(
        ["Metric", "Value"],
        [
            ["Accuracy", f"{acc*100:.2f}%"],
            ["N updates (train)", str(meta_fl.get("n_updates", "?"))],
        ],
    )


if __name__ == "__main__":
    main()
