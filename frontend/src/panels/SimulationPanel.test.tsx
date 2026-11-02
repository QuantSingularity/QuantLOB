import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { api } from "../api";
import { sampleSimulation } from "../test/fixtures";
import { SimulationPanel } from "./SimulationPanel";

vi.mock("../api", () => ({ api: { simulate: vi.fn() } }));

// jsdom cannot lay out charts, so every chart component renders nothing.
vi.mock("recharts", () => {
  const Empty = () => null;
  return {
    Area: Empty,
    AreaChart: Empty,
    Bar: Empty,
    BarChart: Empty,
    CartesianGrid: Empty,
    Line: Empty,
    LineChart: Empty,
    ResponsiveContainer: Empty,
    Tooltip: Empty,
    XAxis: Empty,
    YAxis: Empty,
  };
});

const mockedSimulate = vi.mocked(api.simulate);

const run = () => screen.getByRole("button", { name: "Run Simulation" });

// A metric tile is a label with its value in the next element.
const metricValue = (label: string) =>
  (screen.getByText(label).nextSibling as HTMLElement).textContent;

describe("SimulationPanel", () => {
  beforeEach(() => {
    vi.resetAllMocks();
  });

  it("renders the configuration form with its defaults", () => {
    render(<SimulationPanel />);

    expect(
      screen.getByRole("heading", { name: "Simulation Lab" }),
    ).toBeInTheDocument();
    for (const value of ["200000", "150", "12345", "0.4", "2000"]) {
      expect(screen.getByDisplayValue(value)).toBeInTheDocument();
    }
    expect(screen.queryByText("Throughput")).not.toBeInTheDocument();
  });

  it("runs a simulation with the configured numbers", async () => {
    mockedSimulate.mockResolvedValue(sampleSimulation);
    render(<SimulationPanel />);

    await userEvent.click(run());

    expect(mockedSimulate).toHaveBeenCalledWith({
      events: 200000,
      mid: 150,
      seed: 12345,
      cancel_rate: 0.4,
      snap_interval: 2000,
      levels: 10,
    });
  });

  it("sends the edited settings", async () => {
    mockedSimulate.mockResolvedValue(sampleSimulation);
    render(<SimulationPanel />);

    const events = screen.getByDisplayValue("200000");
    await userEvent.clear(events);
    await userEvent.type(events, "5000");
    await userEvent.click(run());

    expect(mockedSimulate).toHaveBeenCalledWith(
      expect.objectContaining({ events: 5000 }),
    );
  });

  it("shows a running state and disables the button while simulating", async () => {
    let finish: (value: typeof sampleSimulation) => void = () => {};
    mockedSimulate.mockReturnValue(
      new Promise((resolve) => {
        finish = resolve;
      }),
    );
    render(<SimulationPanel />);

    await userEvent.click(run());

    expect(screen.getByText("Running engine...")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Simulating" })).toBeDisabled();

    finish(sampleSimulation);
    expect(await screen.findByText("Throughput")).toBeInTheDocument();
    expect(screen.queryByText("Running engine...")).not.toBeInTheDocument();
  });

  it("shows the headline results", async () => {
    mockedSimulate.mockResolvedValue(sampleSimulation);
    render(<SimulationPanel />);

    await userEvent.click(run());
    await screen.findByText("Throughput");

    expect(metricValue("Throughput")).toBe("1.5M/s");
    expect(screen.getByText("1,234 ms wall")).toBeInTheDocument();
    expect(metricValue("Trades")).toBe("85,000");
    expect(screen.getByText("2.5M volume")).toBeInTheDocument();
    expect(metricValue("Median Latency")).toBe("700 ns");
    expect(screen.getByText("p99 12.00 us")).toBeInTheDocument();
    expect(metricValue("Orders Resting")).toBe("4,200");
    expect(screen.getByText("40,000 cancelled")).toBeInTheDocument();
  });

  it("shows the latency percentiles and engine stats", async () => {
    mockedSimulate.mockResolvedValue(sampleSimulation);
    render(<SimulationPanel />);

    await userEvent.click(run());
    const latency = (await screen.findByText("Latency Percentiles")).closest(
      ".card",
    ) as HTMLElement;
    const stats = screen
      .getByText("Engine Stats")
      .closest(".card") as HTMLElement;

    expect(within(latency).getByText("p99.9").nextSibling).toHaveTextContent(
      "90.00 us",
    );
    expect(within(latency).getByText("Max").nextSibling).toHaveTextContent(
      "1.50 ms",
    );
    expect(
      within(stats).getByText("Orders rejected").nextSibling,
    ).toHaveTextContent("12");
    expect(
      within(stats).getByText("Total notional").nextSibling,
    ).toHaveTextContent("375,000,000");
  });

  it("shows the error when the simulation fails", async () => {
    mockedSimulate.mockRejectedValue(new Error("simulation timed out"));
    render(<SimulationPanel />);

    await userEvent.click(run());

    expect(await screen.findByText("simulation timed out")).toHaveClass(
      "toast",
      "err",
    );
    expect(screen.queryByText("Throughput")).not.toBeInTheDocument();
    expect(run()).toBeEnabled();
  });

  it("clears an earlier error when run again", async () => {
    mockedSimulate
      .mockRejectedValueOnce(new Error("simulation timed out"))
      .mockResolvedValueOnce(sampleSimulation);
    render(<SimulationPanel />);

    await userEvent.click(run());
    await screen.findByText("simulation timed out");
    await userEvent.click(run());

    expect(await screen.findByText("Throughput")).toBeInTheDocument();
    expect(screen.queryByText("simulation timed out")).not.toBeInTheDocument();
  });
});
