import { useEffect, useMemo, useState } from "react";
import {
  Area,
  AreaChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import { api, type OrderRequest } from "../api";
import {
  cls,
  num,
  int,
  pct,
  Card,
  ChartTooltip,
  ErrorState,
  Loading,
  Metric,
} from "../ui";
import type { BookView, Trade } from "../types";

function Ladder({ book }: { book: BookView }) {
  const maxQty = useMemo(() => {
    const all = [...book.bids, ...book.asks].map((l) => l.quantity);
    return all.length ? Math.max(...all) : 1;
  }, [book]);

  const asks = [...book.asks].slice(0, 10).reverse(); // best ask nearest the mid
  const bids = book.bids.slice(0, 10);

  return (
    <div className="ladder">
      {asks.map((l, i) => (
        <div className="ladder-row ask" key={`a${i}`}>
          <div
            className="depth-bar"
            style={{ width: `${(l.quantity / maxQty) * 100}%` }}
          />
          <span className="px">{num(l.price)}</span>
          <span className="qty">{int(l.quantity)}</span>
        </div>
      ))}
      <div className="ladder-mid">
        <span className="dim">mid {num(book.mid_price)}</span>
        <span className="spread">
          spread {num(book.spread)} (
          {book.relative_spread != null ? pct(book.relative_spread, 3) : "-"})
        </span>
      </div>
      {bids.map((l, i) => (
        <div className="ladder-row bid" key={`b${i}`}>
          <div
            className="depth-bar"
            style={{ width: `${(l.quantity / maxQty) * 100}%` }}
          />
          <span className="px">{num(l.price)}</span>
          <span className="qty">{int(l.quantity)}</span>
        </div>
      ))}
    </div>
  );
}

function DepthChart({ book }: { book: BookView }) {
  const data = useMemo(() => {
    const bids = [...book.bids].slice(0, 12);
    const asks = [...book.asks].slice(0, 12);
    let cum = 0;
    const bidPts = bids.map((l) => {
      cum += l.quantity;
      return { price: l.price, bid: cum, ask: undefined as number | undefined };
    });
    cum = 0;
    const askPts = asks.map((l) => {
      cum += l.quantity;
      return { price: l.price, ask: cum, bid: undefined as number | undefined };
    });
    return [...bidPts.reverse(), ...askPts];
  }, [book]);

  return (
    <ResponsiveContainer width="100%" height={200}>
      <AreaChart data={data} margin={{ top: 6, right: 8, bottom: 0, left: 0 }}>
        <defs>
          <linearGradient id="gb" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#2bd49a" stopOpacity={0.4} />
            <stop offset="100%" stopColor="#2bd49a" stopOpacity={0} />
          </linearGradient>
          <linearGradient id="ga" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#ff5d73" stopOpacity={0.4} />
            <stop offset="100%" stopColor="#ff5d73" stopOpacity={0} />
          </linearGradient>
        </defs>
        <CartesianGrid stroke="#19202f" vertical={false} />
        <XAxis
          dataKey="price"
          tickLine={false}
          axisLine={false}
          tickFormatter={(v: number) => num(v)}
          minTickGap={40}
        />
        <YAxis
          tickLine={false}
          axisLine={false}
          width={44}
          tickFormatter={(v: number) =>
            v >= 1000 ? `${(v / 1000).toFixed(0)}k` : String(v)
          }
        />
        <Tooltip
          content={(p) => <ChartTooltip {...p} formatter={(v) => int(v)} />}
        />
        <Area
          type="stepAfter"
          dataKey="bid"
          stroke="#2bd49a"
          strokeWidth={1.5}
          fill="url(#gb)"
          name="Cum Bid"
          connectNulls={false}
        />
        <Area
          type="stepBefore"
          dataKey="ask"
          stroke="#ff5d73"
          strokeWidth={1.5}
          fill="url(#ga)"
          name="Cum Ask"
          connectNulls={false}
        />
      </AreaChart>
    </ResponsiveContainer>
  );
}

function MLStrip({ book }: { book: BookView }) {
  const ml = book.ml;
  if (!ml) return null;
  const buy = ml.buy_probability;
  return (
    <Card
      title="ML Signals"
      extra={
        <span className={cls("pill", ml.model_ready && "cyan")}>
          {ml.model_ready ? "READY" : "WARMING"}
        </span>
      }
    >
      <div
        className="row"
        style={{ justifyContent: "space-between", marginBottom: 10 }}
      >
        <span className="dim mono" style={{ fontSize: 11 }}>
          P(next order = BUY)
        </span>
        <span className="mono">{pct(buy, 1)}</span>
      </div>
      <div className="gauge-track">
        <div
          className="gauge-fill"
          style={{
            width: `${buy * 100}%`,
            background: buy >= 0.5 ? "var(--bid)" : "var(--ask)",
          }}
        />
      </div>
      <div className="grid cols-2 mt-16">
        <Metric
          label="Mid Forecast"
          value={num(ml.mid_price_forecast, 4)}
          tone={ml.mid_price_forecast >= 0 ? "bid" : "ask"}
        />
        <Metric
          label="Anomaly Score"
          value={num(ml.anomaly_score, 3)}
          tone={ml.is_anomaly ? "ask" : ""}
          sub={
            ml.is_anomaly
              ? `${ml.anomaly_features} features flagged`
              : "nominal"
          }
        />
      </div>
    </Card>
  );
}

export function OrderBookPanel({
  onStatus,
}: {
  onStatus: (connected: boolean) => void;
}) {
  const [book, setBook] = useState<BookView | null>(null);
  const [trades, setTrades] = useState<Trade[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [toast, setToast] = useState<{ kind: string; msg: string } | null>(
    null,
  );

  // Order ticket state
  const [side, setSide] = useState<"buy" | "sell">("buy");
  const [type, setType] = useState<OrderRequest["type"]>("limit");
  const [price, setPrice] = useState("150.00");
  const [qty, setQty] = useState("500");
  const [cancelId, setCancelId] = useState("");
  const [seedEvents, setSeedEvents] = useState("8000");

  const refresh = async () => {
    try {
      const [b, t] = await Promise.all([api.book(10), api.trades(40)]);
      setBook(b);
      setTrades(t.trades);
      onStatus(true);
      setError("");
    } catch (e) {
      setError((e as Error).message);
      onStatus(false);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    refresh(); /* eslint-disable-next-line */
  }, []);

  const act = async (fn: () => Promise<unknown>, ok: string) => {
    setBusy(true);
    setToast(null);
    try {
      await fn();
      await refresh();
      setToast({ kind: "ok", msg: ok });
    } catch (e) {
      setToast({ kind: "err", msg: (e as Error).message });
    } finally {
      setBusy(false);
    }
  };

  const submit = () =>
    act(async () => {
      const res = await api.order({
        side,
        type,
        price: Number(price),
        quantity: Number(qty),
      });
      const e = res.execution;
      const detail = e.rejected
        ? `rejected: ${e.reject_reason}`
        : `${e.trades.length} trade(s), ${e.fully_filled ? "filled" : e.resting ? "resting" : "done"}`;
      setToast({ kind: e.rejected ? "err" : "ok", msg: `Order: ${detail}` });
      setBook(res.book);
      const t = await api.trades(40);
      setTrades(t.trades);
    }, "Order submitted");

  if (loading && !book) return <Loading label="Connecting to engine" />;
  if (error && !book) return <ErrorState message={error} />;
  if (!book) return null;

  return (
    <div>
      <div className="panel-head">
        <div className="eyebrow">Live Engine</div>
        <h1>Order Book - {book.symbol}</h1>
        <p>
          A live price-time-priority book served by the C++ matching engine.
          Submit limit, market, IOC or FOK orders and watch the ladder, depth
          and ML signals update against real fills.
        </p>
      </div>

      <div className="grid cols-4" style={{ marginBottom: 16 }}>
        <Metric label="Best Bid" value={num(book.best_bid)} tone="bid" />
        <Metric label="Best Ask" value={num(book.best_ask)} tone="ask" />
        <Metric label="Mid" value={num(book.mid_price)} tone="cyan" />
        <Metric
          label="Imbalance"
          value={num(book.imbalance, 3)}
          tone={book.imbalance >= 0 ? "bid" : "ask"}
          sub={`${int(book.order_count)} resting orders`}
        />
      </div>

      <div className="grid book-layout">
        <div
          className="grid"
          style={{ gridTemplateRows: "auto auto", gap: 16 }}
        >
          <Card
            title="Depth Ladder"
            extra={
              <button className="btn ghost" onClick={refresh}>
                Refresh
              </button>
            }
          >
            {book.bids.length === 0 && book.asks.length === 0 ? (
              <div className="empty">
                <span>The book is empty.</span>
                <span className="faint">
                  Seed it with synthetic liquidity to begin.
                </span>
              </div>
            ) : (
              <Ladder book={book} />
            )}
          </Card>
          <Card title="Depth Profile (cumulative)">
            <DepthChart book={book} />
          </Card>
        </div>

        <div
          className="grid"
          style={{ gridTemplateRows: "auto auto auto", gap: 16 }}
        >
          <Card title="Order Ticket">
            <div className="toggle-group" style={{ marginBottom: 12 }}>
              <button
                className={cls("toggle", side === "buy" && "on")}
                onClick={() => setSide("buy")}
                style={
                  side === "buy"
                    ? {
                        color: "var(--bid)",
                        borderColor: "var(--bid)",
                        background: "var(--bid-soft)",
                      }
                    : undefined
                }
              >
                BUY
              </button>
              <button
                className={cls("toggle", side === "sell" && "on")}
                onClick={() => setSide("sell")}
                style={
                  side === "sell"
                    ? {
                        color: "var(--ask)",
                        borderColor: "var(--ask)",
                        background: "var(--ask-soft)",
                      }
                    : undefined
                }
              >
                SELL
              </button>
            </div>
            <div className="grid cols-2">
              <div>
                <label className="field">Type</label>
                <select
                  value={type}
                  onChange={(e) =>
                    setType(e.target.value as OrderRequest["type"])
                  }
                >
                  <option value="limit">Limit</option>
                  <option value="market">Market</option>
                  <option value="ioc">IOC</option>
                  <option value="fok">FOK</option>
                </select>
              </div>
              <div>
                <label className="field">Quantity</label>
                <input
                  className="inp"
                  value={qty}
                  onChange={(e) => setQty(e.target.value)}
                  inputMode="numeric"
                />
              </div>
            </div>
            <div className="mt-12">
              <label className="field">
                Price {type === "market" ? "(ignored)" : ""}
              </label>
              <input
                className="inp"
                value={price}
                onChange={(e) => setPrice(e.target.value)}
                disabled={type === "market"}
                inputMode="decimal"
              />
            </div>
            <button
              className={cls("btn", side === "buy" ? "buy" : "sell")}
              style={{ width: "100%", marginTop: 14 }}
              onClick={submit}
              disabled={busy}
            >
              {busy
                ? "Working"
                : `Submit ${side.toUpperCase()} ${type.toUpperCase()}`}
            </button>
            {toast && (
              <div className={cls("toast", toast.kind, "mt-12")}>
                {toast.msg}
              </div>
            )}
          </Card>

          <Card title="Book Controls">
            <div className="row">
              <input
                className="inp"
                style={{ flex: 1 }}
                value={seedEvents}
                onChange={(e) => setSeedEvents(e.target.value)}
                inputMode="numeric"
              />
              <button
                className="btn"
                onClick={() =>
                  act(
                    () =>
                      api.seed({
                        events: Number(seedEvents),
                        mid: 150,
                        tick: 0.01,
                      }),
                    "Book seeded",
                  )
                }
                disabled={busy}
              >
                Seed
              </button>
              <button
                className="btn ghost"
                onClick={() => act(() => api.reset(), "Book reset")}
                disabled={busy}
              >
                Reset
              </button>
            </div>
            <div className="row mt-12">
              <input
                className="inp"
                style={{ flex: 1 }}
                placeholder="order id to cancel"
                value={cancelId}
                onChange={(e) => setCancelId(e.target.value)}
                inputMode="numeric"
              />
              <button
                className="btn ghost"
                onClick={() =>
                  act(async () => {
                    const r = await api.cancel(Number(cancelId));
                    if (!r.cancelled)
                      throw new Error("order not found / not active");
                  }, "Order cancelled")
                }
                disabled={busy || !cancelId}
              >
                Cancel
              </button>
            </div>
          </Card>

          <MLStrip book={book} />
        </div>
      </div>

      <Card title="Recent Trades" style={{ marginTop: 16 }}>
        {trades.length === 0 ? (
          <div className="dim mono" style={{ fontSize: 12 }}>
            No trades yet.
          </div>
        ) : (
          <div style={{ maxHeight: 240, overflowY: "auto" }}>
            <table className="data">
              <thead>
                <tr>
                  <th>Buy ID</th>
                  <th>Sell ID</th>
                  <th>Price</th>
                  <th>Qty</th>
                </tr>
              </thead>
              <tbody>
                {trades.map((t, i) => (
                  <tr key={i}>
                    <td className="bid">{t.buy_order_id}</td>
                    <td className="ask">{t.sell_order_id}</td>
                    <td>{num(t.price)}</td>
                    <td>{int(t.quantity)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </Card>
    </div>
  );
}
