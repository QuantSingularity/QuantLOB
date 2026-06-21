import type { ReactNode } from "react";

export const cls = (...p: (string | false | undefined)[]): string =>
  p.filter(Boolean).join(" ");

export const num = (x: number | null | undefined, d = 2): string =>
  x == null
    ? "-"
    : x.toLocaleString("en-US", {
        minimumFractionDigits: d,
        maximumFractionDigits: d,
      });

export const int = (x: number | null | undefined): string =>
  x == null ? "-" : Math.round(x).toLocaleString("en-US");

export const compact = (x: number): string =>
  x.toLocaleString("en-US", { notation: "compact", maximumFractionDigits: 1 });

export const pct = (x: number, d = 2): string => `${(x * 100).toFixed(d)}%`;

// Latency: nanoseconds to a human unit.
export const lat = (ns: number | undefined): string => {
  if (ns == null) return "-";
  if (ns < 1000) return `${Math.round(ns)} ns`;
  if (ns < 1_000_000) return `${(ns / 1000).toFixed(2)} us`;
  return `${(ns / 1_000_000).toFixed(2)} ms`;
};

export function Card({
  title,
  extra,
  children,
  style,
}: {
  title?: string;
  extra?: ReactNode;
  children: ReactNode;
  style?: React.CSSProperties;
}) {
  return (
    <div className="card" style={style}>
      {title && (
        <div className="card-title">
          <span>{title}</span>
          {extra}
        </div>
      )}
      {children}
    </div>
  );
}

export function Metric({
  label,
  value,
  sub,
  tone,
}: {
  label: string;
  value: string;
  sub?: string;
  tone?: "bid" | "ask" | "cyan" | "";
}) {
  return (
    <div className="metric">
      <div className="label">{label}</div>
      <div className={cls("value", tone)}>{value}</div>
      {sub && <div className="sub">{sub}</div>}
    </div>
  );
}

export function Loading({ label = "Loading" }: { label?: string }) {
  return <div className="loading">{label}...</div>;
}
export function ErrorState({ message }: { message: string }) {
  return <div className="toast err">{message}</div>;
}

interface LooseTip {
  active?: boolean;
  label?: unknown;
  payload?: Array<{ name?: unknown; value?: unknown; color?: string }>;
  formatter?: (v: number) => string;
}
export function ChartTooltip(props: LooseTip) {
  const { active, label, payload, formatter } = props;
  if (!active || !payload || payload.length === 0) return null;
  const fmt = formatter || ((v: number) => v.toFixed(2));
  return (
    <div className="af-tooltip">
      <div className="t-date">{String(label ?? "")}</div>
      {payload.map((p, i) => (
        <div key={i} style={{ color: p.color }}>
          {String(p.name ?? "")}:{" "}
          {typeof p.value === "number" ? fmt(p.value) : String(p.value ?? "")}
        </div>
      ))}
    </div>
  );
}
