import { afterEach, describe, expect, it, vi } from "vitest";
import { api } from "./api";

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

// A Response body can only be read once, so build a fresh one for every call.
function mockFetch(body: unknown = {}, status = 200) {
  const fetchMock = vi
    .fn()
    .mockImplementation(() => Promise.resolve(jsonResponse(body, status)));
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

// The first argument is the URL, the second the request options.
function lastCall(fetchMock: ReturnType<typeof mockFetch>) {
  const calls = fetchMock.mock.calls;
  const [url, options] = calls[calls.length - 1] as [string, RequestInit?];
  return { url, options };
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("GET endpoints", () => {
  it("requests the health check under /api", async () => {
    const fetchMock = mockFetch({ status: "ok" });

    await expect(api.health()).resolves.toEqual({ status: "ok" });

    const { url, options } = lastCall(fetchMock);
    expect(url).toBe("/api/health");
    expect(options).toBeUndefined();
  });

  it("requests the book with an optional depth", async () => {
    const fetchMock = mockFetch();

    await api.book();
    expect(lastCall(fetchMock).url).toBe("/api/book");

    await api.book(10);
    expect(lastCall(fetchMock).url).toBe("/api/book?levels=10");
  });

  it("requests the last 50 trades unless told otherwise", async () => {
    const fetchMock = mockFetch();

    await api.trades();
    expect(lastCall(fetchMock).url).toBe("/api/trades?limit=50");

    await api.trades(40);
    expect(lastCall(fetchMock).url).toBe("/api/trades?limit=40");
  });
});

describe("POST endpoints", () => {
  it("posts an order as JSON", async () => {
    const fetchMock = mockFetch();

    await api.order({ side: "buy", type: "limit", price: 150, quantity: 500 });

    const { url, options } = lastCall(fetchMock);
    expect(url).toBe("/api/order");
    expect(options?.method).toBe("POST");
    expect(options?.headers).toEqual({ "Content-Type": "application/json" });
    expect(JSON.parse(options?.body as string)).toEqual({
      side: "buy",
      type: "limit",
      price: 150,
      quantity: 500,
    });
  });

  it("posts the order id to cancel", async () => {
    const fetchMock = mockFetch();

    await api.cancel(42);

    const { url, options } = lastCall(fetchMock);
    expect(url).toBe("/api/cancel");
    expect(JSON.parse(options?.body as string)).toEqual({ order_id: 42 });
  });

  it("posts the seed settings", async () => {
    const fetchMock = mockFetch();

    await api.seed({ events: 8000, mid: 150 });

    const { url, options } = lastCall(fetchMock);
    expect(url).toBe("/api/seed");
    expect(JSON.parse(options?.body as string)).toEqual({
      events: 8000,
      mid: 150,
    });
  });

  it("posts an empty object to reset the book", async () => {
    const fetchMock = mockFetch();

    await api.reset();

    const { url, options } = lastCall(fetchMock);
    expect(url).toBe("/api/reset");
    expect(options?.method).toBe("POST");
    expect(JSON.parse(options?.body as string)).toEqual({});
  });

  it("posts a simulation request", async () => {
    const fetchMock = mockFetch();

    await api.simulate({ events: 200000, seed: 12345, snap_interval: 2000 });

    const { url, options } = lastCall(fetchMock);
    expect(url).toBe("/api/simulate");
    expect(JSON.parse(options?.body as string)).toEqual({
      events: 200000,
      seed: 12345,
      snap_interval: 2000,
    });
  });

  it("returns the parsed response", async () => {
    mockFetch({ reset: true });

    await expect(api.reset()).resolves.toEqual({ reset: true });
  });
});

describe("errors", () => {
  it("throws the error message sent by the server on a GET", async () => {
    mockFetch({ error: "engine not ready" }, 503);

    await expect(api.book()).rejects.toThrow("engine not ready");
  });

  it("throws the error message sent by the server on a POST", async () => {
    mockFetch({ error: "invalid quantity" }, 400);

    await expect(
      api.order({ side: "buy", type: "limit", price: 150, quantity: -1 }),
    ).rejects.toThrow("invalid quantity");
  });

  it("falls back to the status code when the body has no message", async () => {
    mockFetch({}, 500);

    await expect(api.health()).rejects.toThrow("request failed (500)");
    await expect(api.reset()).rejects.toThrow("request failed (500)");
  });

  it("copes with an error response that is not JSON", async () => {
    vi.stubGlobal(
      "fetch",
      vi
        .fn()
        .mockImplementation(() =>
          Promise.resolve(new Response("Bad Gateway", { status: 502 })),
        ),
    );

    await expect(api.trades()).rejects.toThrow("request failed (502)");
    await expect(api.reset()).rejects.toThrow("request failed (502)");
  });
});
