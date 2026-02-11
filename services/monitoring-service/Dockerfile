# =============================================================================
# Monitoring Service Dockerfile
# Uses shared icao-vcpkg-base image for pre-built vcpkg dependencies
# =============================================================================

# Stage 1: Application Build
FROM icao-vcpkg-base:latest AS builder

WORKDIR /app

# Copy source code
COPY services/monitoring-service/CMakeLists.txt ./
COPY services/monitoring-service/src/ ./src/

# Build application
RUN cmake -B build_fresh -S . \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build_fresh --parallel $(nproc)

# Stage 2: Runtime
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
COPY --from=builder /app/build_fresh/monitoring-service /app/
RUN chmod +x /app/monitoring-service

USER appuser

# Expose port
EXPOSE 8084

# Run service
CMD ["./monitoring-service"]
