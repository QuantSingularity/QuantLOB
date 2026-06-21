export interface Level {
  price: number;
  quantity: number;
}

export interface MLResult {
  mid_price_forecast: number;
  buy_probability: number;
  anomaly_score: number;
  anomaly_features: number;
  is_anomaly: boolean;
  model_ready: boolean;
}

export interface EngineStats {
  orders_processed: number;
  orders_matched: number;
  orders_resting: number;
  orders_cancelled: number;
  orders_rejected: number;
  total_trades: number;
  total_volume: number;
  total_notional: number;
}

export interface BookView {
  symbol: string;
  bids: Level[];
  asks: Level[];
  best_bid: number | null;
  best_ask: number | null;
  mid_price: number | null;
  spread: number | null;
  relative_spread: number | null;
  imbalance: number;
  bid_depth: number;
  ask_depth: number;
  order_count: number;
  bid_levels: number;
  ask_levels: number;
  bid_vwap: number | null;
  ask_vwap: number | null;
  stats?: EngineStats;
  ml?: MLResult;
}

export interface Trade {
  buy_order_id: number;
  sell_order_id: number;
  price: number;
  quantity: number;
  timestamp_ns: number;
}

export interface MatchExecution {
  trades: Trade[];
  resting: boolean;
  fully_filled: boolean;
  rejected: boolean;
  reject_reason: string;
}

export interface OrderResponse {
  execution: MatchExecution;
  book: BookView;
}

export interface Latency {
  samples: number;
  mean_ns?: number;
  stddev_ns?: number;
  min_ns?: number;
  max_ns?: number;
  p50_ns?: number;
  p90_ns?: number;
  p99_ns?: number;
  p999_ns?: number;
}

export interface TsPoint {
  event: number;
  mid: number | null;
  spread: number | null;
  imbalance: number;
  bid_depth: number;
  ask_depth: number;
}

export interface MlPoint {
  event: number;
  mid_forecast: number;
  buy_probability: number;
  anomaly_score: number;
  is_anomaly: boolean;
}

export interface HistBucket {
  ns: number;
  count: number;
}

export interface SimulationResult {
  symbol: string;
  wall_ms: number;
  events: number;
  throughput: number;
  stats: EngineStats;
  latency: Latency;
  latency_hist: HistBucket[];
  time_series: TsPoint[];
  ml_series: MlPoint[];
  recent_trades: Trade[];
  final_book?: BookView;
}

export interface Health {
  status: string;
  version: string;
  symbol: string;
  order_types: string[];
}
