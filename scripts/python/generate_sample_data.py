#!/usr/bin/env python3
"""
generate_sample_data.py — Generate a synthetic LOBSTER-format message CSV
for testing the C++ FeedHandler without real LOBSTER data.

LOBSTER message format (comma-separated, no header row):
  timestamp_sec, event_type, order_id, size, price_int, direction
  - timestamp_sec : float seconds since midnight
  - event_type    : 1=new limit, 2=partial cancel, 3=full cancel,
                    4=visible exec, 5=hidden exec, 7=halt
  - order_id      : integer
  - size          : shares
  - price_int     : price * 10000 (integer)
  - direction     : 1=buy, -1=sell
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path


def generate(
    n_events: int,
    seed: int,
    mid: float,
    tick: float,
    output: Path,
) -> None:
    rng = random.Random(seed)
    ts = 34200.0  # 09:30:00 in seconds since midnight
    oid = 1
    active: dict[int, dict] = {}  # order_id -> {price_int, size, side}

    rows: list[str] = []

    for _ in range(n_events):
        ts += rng.expovariate(1000.0)

        r = rng.random()

        if r < 0.60 or len(active) == 0:
            # New limit order
            side = 1 if rng.random() < 0.5 else -1
            levels = rng.randint(0, 4)
            offset = levels * tick + abs(rng.gauss(0, tick * 0.1))
            # side=1 (buy)  → price = mid - offset  (below mid)
            # side=-1 (sell) → price = mid + offset  (above mid)
            price = mid - side * offset
            price = max(price, tick)
            price_int = round(price * 10000)
            size = rng.randint(1, 200)
            rows.append(f"{ts:.9f},{1},{oid},{size},{price_int},{side}")
            active[oid] = {"price_int": price_int, "size": size, "side": side}
            oid += 1

        elif r < 0.85 and active:
            # Full cancel (event type 3)
            cid = rng.choice(list(active.keys()))
            info = active.pop(cid)
            rows.append(
                f"{ts:.9f},{3},{cid},{info['size']},{info['price_int']},{info['side']}"
            )

        elif active:
            # Visible execution (event type 4) — remove from active
            cid = rng.choice(list(active.keys()))
            info = active.pop(cid)
            rows.append(
                f"{ts:.9f},{4},{cid},{info['size']},{info['price_int']},{info['side']}"
            )
            # Nudge mid toward execution price (price discovery)
            exec_price = info["price_int"] / 10000.0
            mid = mid + (exec_price - mid) * 0.01 + rng.gauss(0, tick * 0.5)
            mid = max(mid, tick)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"Written {len(rows)} events to {output}")
    print(f"  Orders submitted : {sum(1 for r in rows if r.split(',')[1] == '1')}")
    print(f"  Cancels          : {sum(1 for r in rows if r.split(',')[1] == '3')}")
    print(f"  Executions       : {sum(1 for r in rows if r.split(',')[1] == '4')}")


def main() -> None:
    p = argparse.ArgumentParser(description="Generate sample LOBSTER CSV")
    p.add_argument("--events", type=int, default=10_000)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--mid", type=float, default=100.0)
    p.add_argument("--tick", type=float, default=0.01)
    p.add_argument("--out", type=Path, default=Path("code/data/sample/messages.csv"))
    args = p.parse_args()
    generate(args.events, args.seed, args.mid, args.tick, args.out)


if __name__ == "__main__":
    main()
