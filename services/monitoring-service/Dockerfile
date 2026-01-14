# =============================================================================
# Stage 1: vcpkg base - System dependencies
# =============================================================================
FROM debian:bookworm-slim AS vcpkg-base

# Install build tools and system libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    libssl-dev \
    zlib1g-dev \
    libpq-dev \
    bison \
    flex \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/Microsoft/vcpkg.git ${VCPKG_ROOT} && \
    ${VCPKG_ROOT}/bootstrap-vcpkg.sh

# =============================================================================
# Stage 2: vcpkg dependencies - Build C++ packages
# =============================================================================
FROM vcpkg-base AS vcpkg-deps

WORKDIR /build

# Copy vcpkg manifest
COPY services/monitoring-service/vcpkg.json ./

# Install dependencies
RUN ${VCPKG_ROOT}/vcpkg install \
    --x-install-root=${VCPKG_ROOT}/installed

# =============================================================================
# Stage 3: Builder - Compile application
# =============================================================================
FROM vcpkg-deps AS builder

WORKDIR /build

# Copy source code
COPY services/monitoring-service/CMakeLists.txt ./
COPY services/monitoring-service/src/ ./src/

# Build application
RUN cmake -B build_fresh -S . \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build_fresh --parallel $(nproc)

# Verify binary
RUN strings build_fresh/monitoring-service | grep -i "Monitoring Service" || \
    (echo "ERROR: Version string not found in binary!" && exit 1)

# =============================================================================
# Stage 4: Runtime - Production image
# =============================================================================
FROM debian:bookworm-slim AS runtime

# Install runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    zlib1g \
    libpq5 \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create app user
RUN useradd -m -u 1000 appuser

# Create directories
RUN mkdir -p /app/logs && \
    chown -R appuser:appuser /app

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/build_fresh/monitoring-service /app/
RUN chmod +x /app/monitoring-service

USER appuser

# Expose port
EXPOSE 8084

# Run service
CMD ["./monitoring-service"]
