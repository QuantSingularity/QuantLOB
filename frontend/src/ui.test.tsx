import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import {
  Card,
  ChartTooltip,
  ErrorState,
  Loading,
  Metric,
  cls,
  compact,
  int,
  lat,
  num,
  pct,
} from "./ui";

describe("cls", () => {
  it("joins the class names that are present", () => {
    expect(cls("a", "b")).toBe("a b");
  });

  it("drops false, undefined and empty entries", () => {
    expect(cls("a", false, undefined, "", "b")).toBe("a b");
  });

  it("returns an empty string when nothing is left", () => {
    expect(cls(false, undefined)).toBe("");
  });
});

describe("num", () => {
  it("groups thousands and pads to two decimals", () => {
    expect(num(1234567.8)).toBe("1,234,567.80");
  });

  it("honours a custom number of decimals", () => {
    expect(num(3.14159, 3)).toBe("3.142");
    expect(num(42, 0)).toBe("42");
  });

  it("shows a dash for a missing value", () => {
    expect(num(null)).toBe("-");
    expect(num(undefined)).toBe("-");
  });

  it("keeps zero as a number rather than treating it as missing", () => {
    expect(num(0)).toBe("0.00");
  });
});

describe("int", () => {
  it("rounds and groups thousands", () => {
    expect(int(1234.6)).toBe("1,235");
  });

  it("shows a dash for a missing value", () => {
    expect(int(null)).toBe("-");
    expect(int(undefined)).toBe("-");
  });
});

describe("compact", () => {
  it.each([
    [999, "999"],
    [1500, "1.5K"],
    [2_500_000, "2.5M"],
    [3_000_000_000, "3B"],
  ])("abbreviates %d as %s", (value, expected) => {
    expect(compact(value)).toBe(expected);
  });
});

describe("pct", () => {
  it("converts a fraction to a percentage with two decimals", () => {
    expect(pct(0.1234)).toBe("12.34%");
  });

  it("honours a custom number of decimals", () => {
    expect(pct(0.000133, 3)).toBe("0.013%");
    expect(pct(0.5, 0)).toBe("50%");
  });
});

describe("lat", () => {
  it("shows a missing latency as a dash", () => {
    expect(lat(undefined)).toBe("-");
  });

  it.each([
    [0, "0 ns"],
    [999.4, "999 ns"],
    [1000, "1.00 us"],
    [1500, "1.50 us"],
    [999_999, "1000.00 us"],
    [1_000_000, "1.00 ms"],
    [2_500_000, "2.50 ms"],
  ])("formats %d ns as %s", (ns, expected) => {
    expect(lat(ns)).toBe(expected);
  });
});

describe("Card", () => {
  it("renders its children", () => {
    render(
      <Card>
        <p>inside</p>
      </Card>,
    );

    expect(screen.getByText("inside")).toBeInTheDocument();
  });

  it("shows a title and extra content only when given", () => {
    const { container, rerender } = render(<Card>body</Card>);
    expect(container.querySelector(".card-title")).toBeNull();

    rerender(
      <Card title="Positions" extra={<em>live</em>}>
        body
      </Card>,
    );
    expect(screen.getByText("Positions")).toBeInTheDocument();
    expect(screen.getByText("live")).toBeInTheDocument();
  });

  it("applies inline styles to the card", () => {
    const { container } = render(<Card style={{ minHeight: 120 }}>body</Card>);

    expect(container.firstElementChild).toHaveStyle({ minHeight: "120px" });
  });
});

describe("Metric", () => {
  it("renders the label and the value", () => {
    render(<Metric label="Best Bid" value="149.99" />);

    expect(screen.getByText("Best Bid")).toHaveClass("label");
    expect(screen.getByText("149.99")).toHaveClass("value");
  });

  it("shows the sub line only when given", () => {
    const { container, rerender } = render(<Metric label="Mid" value="150" />);
    expect(container.querySelector(".sub")).toBeNull();

    rerender(<Metric label="Mid" value="150" sub="4 resting orders" />);
    expect(screen.getByText("4 resting orders")).toHaveClass("sub");
  });

  it.each(["bid", "ask", "cyan"] as const)(
    "colours the value for the %s tone",
    (tone) => {
      render(<Metric label="Mid" value="150" tone={tone} />);

      expect(screen.getByText("150")).toHaveClass("value", tone);
    },
  );

  it("adds no tone class by default", () => {
    render(<Metric label="Mid" value="150" />);

    expect(screen.getByText("150").className).toBe("value");
  });
});

describe("Loading and ErrorState", () => {
  it("Loading defaults to a plain loading message", () => {
    render(<Loading />);

    expect(screen.getByText("Loading...")).toHaveClass("loading");
  });

  it("Loading accepts a custom label", () => {
    render(<Loading label="Connecting to engine" />);

    expect(screen.getByText("Connecting to engine...")).toBeInTheDocument();
  });

  it("ErrorState shows the message in an error toast", () => {
    render(<ErrorState message="request failed (500)" />);

    expect(screen.getByText("request failed (500)")).toHaveClass(
      "toast",
      "err",
    );
  });
});

describe("ChartTooltip", () => {
  const payload = [
    { name: "Cum Bid", value: 1234.5, color: "#2bd49a" },
    { name: "Cum Ask", value: 99, color: "#ff5d73" },
  ];

  it("renders nothing when the tooltip is inactive", () => {
    const { container } = render(
      <ChartTooltip active={false} payload={payload} />,
    );

    expect(container).toBeEmptyDOMElement();
  });

  it("renders nothing when there is no payload", () => {
    const { container, rerender } = render(<ChartTooltip active />);
    expect(container).toBeEmptyDOMElement();

    rerender(<ChartTooltip active payload={[]} />);
    expect(container).toBeEmptyDOMElement();
  });

  it("shows the label and one formatted row per series", () => {
    render(<ChartTooltip active label="150.01" payload={payload} />);

    expect(screen.getByText("150.01")).toHaveClass("t-date");
    expect(screen.getByText("Cum Bid: 1234.50")).toBeInTheDocument();
    expect(screen.getByText("Cum Ask: 99.00")).toBeInTheDocument();
  });

  it("uses the supplied formatter for numeric values", () => {
    render(
      <ChartTooltip
        active
        payload={payload}
        formatter={(v) => `${Math.round(v)} lots`}
      />,
    );

    expect(screen.getByText("Cum Bid: 1235 lots")).toBeInTheDocument();
  });

  it("shows non-numeric values as text", () => {
    render(<ChartTooltip active payload={[{ name: "Side", value: "buy" }]} />);

    expect(screen.getByText("Side: buy")).toBeInTheDocument();
  });
});
