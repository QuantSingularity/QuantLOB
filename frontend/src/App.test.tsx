import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import App from "./App";
import { api } from "./api";

vi.mock("./api", () => ({ api: { health: vi.fn() } }));

// The panels have their own tests, so the shell is tested against stand-ins.
vi.mock("./panels/OrderBookPanel", () => ({
  OrderBookPanel: ({
    onStatus,
  }: {
    onStatus: (connected: boolean) => void;
  }) => (
    <div>
      <p>order book panel</p>
      <button onClick={() => onStatus(false)}>drop connection</button>
      <button onClick={() => onStatus(true)}>restore connection</button>
    </div>
  ),
}));
vi.mock("./panels/SimulationPanel", () => ({
  SimulationPanel: () => <p>simulation panel</p>,
}));

const mockedHealth = vi.mocked(api.health);

const HEALTH = {
  status: "ok",
  version: "1.4.0",
  symbol: "AAPL",
  order_types: ["limit", "market"],
};

// Waits for the health request to settle and React to apply the result.
async function flush() {
  await act(async () => {
    await Promise.resolve();
  });
}

describe("App", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    mockedHealth.mockResolvedValue(HEALTH);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  describe("engine status", () => {
    it("shows the engine as live with its version and symbol", async () => {
      render(<App />);

      expect(await screen.findByText("ENGINE LIVE")).toBeInTheDocument();
      expect(screen.getByText("v1.4.0")).toBeInTheDocument();
      expect(screen.getByText("AAPL")).toBeInTheDocument();
    });

    it("asks the engine for its health once on load", async () => {
      render(<App />);
      await flush();

      expect(mockedHealth).toHaveBeenCalledTimes(1);
    });

    it("shows the engine as offline when the health check fails", async () => {
      mockedHealth.mockRejectedValue(new Error("connection refused"));

      render(<App />);

      expect(await screen.findByText("OFFLINE")).toBeInTheDocument();
      expect(screen.queryByText(/^v\d/)).not.toBeInTheDocument();
    });

    it("follows the connection status reported by the order book panel", async () => {
      render(<App />);
      expect(await screen.findByText("ENGINE LIVE")).toBeInTheDocument();

      fireEvent.click(screen.getByText("drop connection"));
      expect(screen.getByText("OFFLINE")).toBeInTheDocument();

      fireEvent.click(screen.getByText("restore connection"));
      expect(screen.getByText("ENGINE LIVE")).toBeInTheDocument();
    });
  });

  describe("navigation", () => {
    it("opens on the order book", async () => {
      render(<App />);
      await flush();

      expect(screen.getByText("order book panel")).toBeInTheDocument();
      expect(screen.queryByText("simulation panel")).not.toBeInTheDocument();
      expect(screen.getByRole("button", { name: /Order Book/ })).toHaveClass(
        "active",
      );
    });

    it("switches to the simulation lab and back", async () => {
      render(<App />);
      await flush();

      fireEvent.click(screen.getByRole("button", { name: /Simulation Lab/ }));
      expect(screen.getByText("simulation panel")).toBeInTheDocument();
      expect(screen.queryByText("order book panel")).not.toBeInTheDocument();
      expect(
        screen.getByRole("button", { name: /Simulation Lab/ }),
      ).toHaveClass("active");
      expect(
        screen.getByRole("button", { name: /Order Book/ }),
      ).not.toHaveClass("active");

      fireEvent.click(screen.getByRole("button", { name: /Order Book/ }));
      expect(screen.getByText("order book panel")).toBeInTheDocument();
    });
  });

  describe("clock", () => {
    it("shows the current UTC time and ticks every second", async () => {
      vi.useFakeTimers();
      vi.setSystemTime(new Date("2026-01-15T12:34:56Z"));

      render(<App />);
      await flush();
      expect(screen.getByText("12:34:56 UTC")).toBeInTheDocument();

      await act(async () => {
        await vi.advanceTimersByTimeAsync(1000);
      });
      expect(screen.getByText("12:34:57 UTC")).toBeInTheDocument();
    });

    it("stops ticking once the app is unmounted", async () => {
      vi.useFakeTimers();
      const { unmount } = render(<App />);
      await flush();

      unmount();

      expect(vi.getTimerCount()).toBe(0);
    });
  });
});
