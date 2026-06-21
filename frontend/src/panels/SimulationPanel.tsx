import { useState } from "react";
import {
  Area,
  AreaChart,
  Bar,
  BarChart,
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import { api } from "../api";
import {
  num,
  int,
  lat,
  compact,
  Card,
  ChartTooltip,
  ErrorState,
  Loading,
  Metric,
} from "../ui";
import type { SimulationResult } from "../types";

export function SimulationPanel() {
  const [events, setEvents] = useState("200000");
  const [mid, setMid] = useState("150");
  const [seed, setSeed] = useState("12345");
  const [cancel, setCancel] = useState("0.4");
  const [snap, setSnap] = useState("2000");
  const [result, setResult] = useState<SimulationResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  const run = async () => {
    setLoading(true);
    setError("");
    try {
      const r = await api.simulate({
        events: Number(events),
        mid: Number(mid),
        seed: Number(seed),
        cancel_rate: Number(cancel),
        snap_interval: Number(snap),
        levels: 10,
      });
      setResult(r);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div>
      <div className="panel-head">
        <div className="eyebrow">Research</div>
        <h1>Simulation Lab</h1>
        <p>
          Drive a synthetic Poisson order flow through the engine and study the
          resulting microstructure: mid-price path, spread and imbalance
          dynamics, per-order latency distribution, and the live ML signals the
          pipeline produces along the way.
        </p>
      </div>

      <Card title="Configuration" style={{ marginBottom: 16 }}>
        <div className="grid cols-4">
          <div>
            <label className="field">Events</label>
            <input
              className="inp"
              value={events}
              onChange={(e) => setEvents(e.target.value)}
              inputMode="numeric"
            />
          </div>
          <div>
            <label className="field">Initial Mid</label>
            <input
              className="inp"
              value={mid}
              onChange={(e) => setMid(e.target.value)}
              inputMode="decimal"
            />
          </div>
          <div>
            <label className="field">Seed</label>
            <input
              className="inp"
              value={seed}
              onChange={(e) => setSeed(e.target.value)}
              inputMode="numeric"
            />
          </div>
          <div>
            <label className="field">Cancel Rate</label>
            <input
              className="inp"
              value={cancel}
              onChange={(e) => setCancel(e.target.value)}
              inputMode="decimal"
            />
          </div>
        </div>
        <div className="row mt-16">
          <div style={{ width: 220 }}>
            <label className="field">Snapshot Interval (events)</label>
            <input
              className="inp"
              value={snap}
              onChange={(e) => setSnap(e.target.value)}
              inputMode="numeric"
            />
          </div>
          <div className="spacer" />
          <button
            className="btn"
            onClick={run}
            disabled={loading}
            style={{ alignSelf: "flex-end" }}
          >
            {loading ? "Simulating" : "Run Simulation"}
          </button>
        </div>
      </Card>

      {error && <ErrorState message={error} />}
      {loading && <Loading label="Running engine" />}

      {result && (
        <>
          <div className="grid cols-4" style={{ marginBottom: 16 }}>
            <Metric
              label="Throughput"
              value={`${compact(result.throughput)}/s`}
              tone="cyan"
              sub={`${num(result.wall_ms, 0)} ms wall`}
            />
            <Metric
              label="Trades"
              value={int(result.stats.total_trades)}
              sub={`${compact(result.stats.total_volume)} volume`}
            />
            <Metric
              label="Median Latency"
              value={lat(result.latency.p50_ns)}
              sub={`p99 ${lat(result.latency.p99_ns)}`}
            />
            <Metric
              label="Orders Resting"
              value={int(result.stats.orders_resting)}
              sub={`${int(result.stats.orders_cancelled)} cancelled`}
            />
          </div>

          <Card title="Mid-Price Path" style={{ marginBottom: 16 }}>
            <ResponsiveContainer width="100%" height={240}>
              <LineChart
                data={result.time_series}
                margin={{ top: 6, right: 12, bottom: 0, left: 8 }}
              >
                <CartesianGrid stroke="#19202f" vertical={false} />
                <XAxis
                  dataKey="event"
                  tickLine={false}
                  axisLine={false}
                  tickFormatter={(v: number) => compact(v)}
                  minTickGap={50}
                />
                <YAxis
                  domain={["auto", "auto"]}
                  tickLine={false}
                  axisLine={false}
                  width={56}
                  tickFormatter={(v: number) => num(v)}
                />
                <Tooltip
                  content={(p) => (
                    <ChartTooltip {...p} formatter={(v) => num(v)} />
                  )}
                />
                <Line
                  type="monotone"
                  dataKey="mid"
                  stroke="#35e0c9"
                  strokeWidth={1.6}
                  dot={false}
                  name="Mid"
                />
              </LineChart>
            </ResponsiveContainer>
          </Card>

          <div className="grid cols-2">
            <Card title="Order-Book Imbalance">
              <ResponsiveContainer width="100%" height={200}>
                <AreaChart
                  data={result.time_series}
                  margin={{ top: 6, right: 10, bottom: 0, left: 4 }}
                >
                  <defs>
                    <linearGradient id="imb" x1="0" y1="0" x2="0" y2="1">
                      <stop
                        offset="0%"
                        stopColor="#35e0c9"
                        stopOpacity={0.35}
                      />
                      <stop offset="100%" stopColor="#35e0c9" stopOpacity={0} />
                    </linearGradient>
                  </defs>
                  <CartesianGrid stroke="#19202f" vertical={false} />
                  <XAxis
                    dataKey="event"
                    tickLine={false}
                    axisLine={false}
                    tickFormatter={(v: number) => compact(v)}
                    minTickGap={50}
                  />
                  <YAxis
                    domain={[-1, 1]}
                    tickLine={false}
                    axisLine={false}
                    width={40}
                    tickFormatter={(v: number) => v.toFixed(1)}
                  />
                  <Tooltip
                    content={(p) => (
                      <ChartTooltip {...p} formatter={(v) => num(v, 3)} />
                    )}
                  />
                  <Area
                    type="monotone"
                    dataKey="imbalance"
                    stroke="#35e0c9"
                    strokeWidth={1.4}
                    fill="url(#imb)"
                    name="Imbalance"
                  />
                </AreaChart>
              </ResponsiveContainer>
            </Card>

            <Card title="Latency Distribution">
              <ResponsiveContainer width="100%" height={200}>
                <BarChart
                  data={result.latency_hist}
                  margin={{ top: 6, right: 10, bottom: 0, left: 4 }}
                >
                  <CartesianGrid stroke="#19202f" vertical={false} />
                  <XAxis
                    dataKey="ns"
                    tickLine={false}
                    axisLine={false}
                    tickFormatter={(v: number) => lat(v)}
                    minTickGap={40}
                  />
                  <YAxis
                    tickLine={false}
                    axisLine={false}
                    width={44}
                    tickFormatter={(v: number) => compact(v)}
                  />
                  <Tooltip
                    content={(p) => (
                      <ChartTooltip {...p} formatter={(v) => int(v)} />
                    )}
                  />
                  <Bar
                    dataKey="count"
                    fill="#5b8def"
                    name="Orders"
                    radius={[2, 2, 0, 0]}
                  />
                </BarChart>
              </ResponsiveContainer>
            </Card>
          </div>

          <Card title="ML Signals Over Time" style={{ marginTop: 16 }}>
            <ResponsiveContainer width="100%" height={220}>
              <LineChart
                data={result.ml_series}
                margin={{ top: 6, right: 12, bottom: 0, left: 8 }}
              >
                <CartesianGrid stroke="#19202f" vertical={false} />
                <XAxis
                  dataKey="event"
                  tickLine={false}
                  axisLine={false}
                  tickFormatter={(v: number) => compact(v)}
                  minTickGap={50}
                />
                <YAxis
                  yAxisId="p"
                  domain={[0, 1]}
                  tickLine={false}
                  axisLine={false}
                  width={40}
                  tickFormatter={(v: number) => v.toFixed(1)}
                />
                <YAxis
                  yAxisId="a"
                  orientation="right"
                  tickLine={false}
                  axisLine={false}
                  width={40}
                  tickFormatter={(v: number) => num(v, 1)}
                />
                <Tooltip
                  content={(p) => (
                    <ChartTooltip {...p} formatter={(v) => num(v, 3)} />
                  )}
                />
                <Line
                  yAxisId="p"
                  type="monotone"
                  dataKey="buy_probability"
                  stroke="#2bd49a"
                  strokeWidth={1.5}
                  dot={false}
                  name="P(buy)"
                />
                <Line
                  yAxisId="a"
                  type="monotone"
                  dataKey="anomaly_score"
                  stroke="#f4b14a"
                  strokeWidth={1.3}
                  dot={false}
                  name="Anomaly"
                />
              </LineChart>
            </ResponsiveContainer>
          </Card>

          <div className="grid cols-2 mt-16">
            <Card title="Latency Percentiles">
              <table className="data">
                <tbody>
                  {[
                    ["Samples", int(result.latency.samples)],
                    ["Mean", lat(result.latency.mean_ns)],
                    ["Min", lat(result.latency.min_ns)],
                    ["p50", lat(result.latency.p50_ns)],
                    ["p90", lat(result.latency.p90_ns)],
                    ["p99", lat(result.latency.p99_ns)],
                    ["p99.9", lat(result.latency.p999_ns)],
                    ["Max", lat(result.latency.max_ns)],
                  ].map(([k, v]) => (
                    <tr key={k}>
                      <td className="dim">{k}</td>
                      <td style={{ textAlign: "right" }}>{v}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </Card>
            <Card title="Engine Stats">
              <table className="data">
                <tbody>
                  {[
                    ["Orders processed", int(result.stats.orders_processed)],
                    ["Orders matched", int(result.stats.orders_matched)],
                    ["Orders resting", int(result.stats.orders_resting)],
                    ["Orders cancelled", int(result.stats.orders_cancelled)],
                    ["Orders rejected", int(result.stats.orders_rejected)],
                    ["Total trades", int(result.stats.total_trades)],
                    ["Total volume", int(result.stats.total_volume)],
                    ["Total notional", num(result.stats.total_notional, 0)],
                  ].map(([k, v]) => (
                    <tr key={k}>
                      <td className="dim">{k}</td>
                      <td style={{ textAlign: "right" }}>{v}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </Card>
          </div>
        </>
      )}

      {!result && !loading && (
        <div className="empty">
          <span className="cyan">Configure and run a simulation</span>
          <span className="faint">Charts and analytics will appear here.</span>
        </div>
      )}
    </div>
  );
}
