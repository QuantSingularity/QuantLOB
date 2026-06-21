import type {
  BookView,
  Health,
  OrderResponse,
  SimulationResult,
  Trade,
} from "./types";

const BASE = "/api";

async function getJson<T>(path: string): Promise<T> {
  const res = await fetch(`${BASE}${path}`);
  if (!res.ok) {
    const b = await res.json().catch(() => ({}));
    throw new Error(
      (b as { error?: string }).error || `request failed (${res.status})`,
    );
  }
  return res.json() as Promise<T>;
}

async function postJson<T>(path: string, payload: unknown): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok) {
    const b = await res.json().catch(() => ({}));
    throw new Error(
      (b as { error?: string }).error || `request failed (${res.status})`,
    );
  }
  return res.json() as Promise<T>;
}

export interface OrderRequest {
  side: "buy" | "sell";
  type: "limit" | "market" | "ioc" | "fok";
  price: number;
  quantity: number;
}

export interface SeedRequest {
  events?: number;
  mid?: number;
  tick?: number;
  seed?: number;
  cancel_rate?: number;
}

export interface SimulateRequest {
  symbol?: string;
  events?: number;
  mid?: number;
  tick?: number;
  seed?: number;
  cancel_rate?: number;
  snap_interval?: number;
  levels?: number;
}

export const api = {
  health: () => getJson<Health>("/health"),
  book: (levels?: number) =>
    getJson<BookView>(`/book${levels ? `?levels=${levels}` : ""}`),
  trades: (limit = 50) =>
    getJson<{ trades: Trade[] }>(`/trades?limit=${limit}`),
  order: (req: OrderRequest) => postJson<OrderResponse>("/order", req),
  cancel: (order_id: number) =>
    postJson<{ cancelled: boolean; book: BookView }>("/cancel", { order_id }),
  seed: (req: SeedRequest) =>
    postJson<{ seeded: number; book: BookView }>("/seed", req),
  reset: () => postJson<{ reset: boolean; book: BookView }>("/reset", {}),
  simulate: (req: SimulateRequest) =>
    postJson<SimulationResult>("/simulate", req),
};
