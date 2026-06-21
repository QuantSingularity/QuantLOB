import { useEffect, useState } from "react";
import { api } from "./api";
import { cls } from "./ui";
import { OrderBookPanel } from "./panels/OrderBookPanel";
import { SimulationPanel } from "./panels/SimulationPanel";
import type { Health } from "./types";

type Tab = "book" | "sim";

function Clock() {
  const [now, setNow] = useState(new Date());
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(t);
  }, []);
  return <span>{now.toUTCString().slice(17, 25)} UTC</span>;
}

export default function App() {
  const [tab, setTab] = useState<Tab>("book");
  const [health, setHealth] = useState<Health | null>(null);
  const [connected, setConnected] = useState(true);

  useEffect(() => {
    api
      .health()
      .then((h) => {
        setHealth(h);
        setConnected(true);
      })
      .catch(() => setConnected(false));
  }, []);

  return (
    <div className="shell">
      <div className="statusbar">
        <div className="brand">
          <div className="logo">
            <span className="q">Quant</span>
            <span className="rest">LOB</span>
          </div>
          <span className="tag">Order Book Terminal</span>
        </div>
        <div className="status-items">
          <span>
            <span className={cls("status-dot", !connected && "off")} />
            {connected ? "ENGINE LIVE" : "OFFLINE"}
          </span>
          {health && <span>v{health.version}</span>}
          {health && <span>{health.symbol}</span>}
          <Clock />
        </div>
      </div>

      <nav className="sidebar">
        <div className="nav-label">Trading</div>
        <button
          className={cls("nav-item", tab === "book" && "active")}
          onClick={() => setTab("book")}
        >
          <span className="glyph">#</span>Order Book
        </button>
        <div className="nav-label">Research</div>
        <button
          className={cls("nav-item", tab === "sim" && "active")}
          onClick={() => setTab("sim")}
        >
          <span className="glyph">~</span>Simulation Lab
        </button>
      </nav>

      <main className="workspace">
        {tab === "book" && <OrderBookPanel onStatus={setConnected} />}
        {tab === "sim" && <SimulationPanel />}
      </main>
    </div>
  );
}
