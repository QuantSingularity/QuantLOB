import type {
  BookView,
  MatchExecution,
  OrderResponse,
  SimulationResult,
  Trade,
} from "../types";

// Typed sample data shared by the tests.
export const sampleBook: BookView = {
  symbol: "AAPL",
  bids: [
    { price: 149.99, quantity: 500 },
    { price: 149.98, quantity: 300 },
  ],
  asks: [
    { price: 150.01, quantity: 400 },
    { price: 150.02, quantity: 700 },
  ],
  best_bid: 149.99,
  best_ask: 150.01,
  mid_price: 150.0,
  spread: 0.02,
  relative_spread: 0.000133,
  imbalance: 0.1,
  bid_depth: 800,
  ask_depth: 1100,
  order_count: 4,
  bid_levels: 2,
  ask_levels: 2,
  bid_vwap: 149.986,
  ask_vwap: 150.016,
};

export const sampleTrades: Trade[] = [
  {
    buy_order_id: 11,
    sell_order_id: 7,
    price: 150.0,
    quantity: 250,
    timestamp_ns: 1_700_000_000_000_000_000,
  },
];

export function orderResponse(
  execution: Partial<MatchExecution> = {},
  book: BookView = sampleBook,
): OrderResponse {
  return {
    execution: {
      trades: [],
      resting: false,
      fully_filled: false,
      rejected: false,
      reject_reason: "",
      ...execution,
    },
    book,
  };
}

export const sampleSimulation: SimulationResult = {
  symbol: "AAPL",
  wall_ms: 1234,
  events: 200000,
  throughput: 1_500_000,
  stats: {
    orders_processed: 200000,
    orders_matched: 90000,
    orders_resting: 4200,
    orders_cancelled: 40000,
    orders_rejected: 12,
    total_trades: 85000,
    total_volume: 2_500_000,
    total_notional: 375_000_000,
  },
  latency: {
    samples: 200000,
    mean_ns: 800,
    min_ns: 90,
    p50_ns: 700,
    p90_ns: 1500,
    p99_ns: 12_000,
    p999_ns: 90_000,
    max_ns: 1_500_000,
  },
  latency_hist: [
    { ns: 500, count: 120 },
    { ns: 1000, count: 80 },
  ],
  time_series: [
    {
      event: 2000,
      mid: 150.0,
      spread: 0.02,
      imbalance: 0.1,
      bid_depth: 800,
      ask_depth: 1100,
    },
    {
      event: 4000,
      mid: 150.05,
      spread: 0.03,
      imbalance: -0.2,
      bid_depth: 700,
      ask_depth: 1200,
    },
  ],
  ml_series: [
    {
      event: 2000,
      mid_forecast: 0.01,
      buy_probability: 0.55,
      anomaly_score: 0.2,
      is_anomaly: false,
    },
  ],
  recent_trades: sampleTrades,
};
