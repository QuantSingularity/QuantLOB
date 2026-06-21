# ============================================================
# Stage 1: builder
# ============================================================
FROM ubuntu:22.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_TYPE=Release

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        libstdc++-12-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy source tree (tests and benchmarks live under code/ and are disabled here)
COPY CMakeLists.txt .
COPY code/         code/

# Configure
RUN cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DQUANTLOB_BUILD_TESTS=OFF \
        -DQUANTLOB_BUILD_BENCHMARKS=OFF \
        -DQUANTLOB_BUILD_SERVER=OFF \
        -DQUANTLOB_BUILD_MAIN=ON \
    && cmake --build build --parallel $(nproc)

# ============================================================
# Stage 2: runtime (minimal)
# ============================================================
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -u 1000 quantlob
USER quantlob
WORKDIR /app

COPY --from=builder /src/build/quantlob /app/quantlob
COPY --chown=quantlob:quantlob code/data/ /app/data/

RUN mkdir -p /app/out

ENTRYPOINT ["/app/quantlob"]
CMD ["--help"]
