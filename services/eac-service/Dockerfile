# =============================================================================
# EAC Service Dockerfile
# Uses shared icao-vcpkg-base image for pre-built vcpkg dependencies
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Application Build
ARG BASE_IMAGE=icao-vcpkg-base:latest
FROM ${BASE_IMAGE} AS builder

WORKDIR /app

# Ensure Oracle directory exists (may be empty for postgres-only ARM64 builds)
RUN mkdir -p /opt/oracle/instantclient_21_13

COPY services/eac-service/CMakeLists.txt ./
COPY services/eac-service/src/ ./src/
# Copy shared libraries (required by CMakeLists.txt add_subdirectory)
COPY shared/ ./shared/

ARG ENABLE_ORACLE=ON
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_ORACLE=${ENABLE_ORACLE} \
    && cmake --build build -j$(nproc)

# Stage 2: Runtime
FROM debian:bookworm-slim AS runtime

ARG CACHE_BUST=unknown

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libpq5 \
    libuuid1 \
    libjsoncpp25 \
    libc-ares2 \
    libbrotli1 \
    zlib1g \
    ca-certificates \
    tzdata \
    curl \
    libaio1 \
    && rm -rf /var/lib/apt/lists/*

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone
RUN useradd -m -s /bin/bash app
RUN mkdir -p /app/logs \
    && chown -R app:app /app

# Copy Oracle Instant Client from builder (may be empty for postgres-only builds)
RUN mkdir -p /opt/oracle/instantclient_21_13
COPY --from=builder /opt/oracle/instantclient_21_13/ /opt/oracle/instantclient_21_13/

# Set Oracle environment variables for runtime
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

COPY --from=builder /app/build/bin/eac-service /app/

WORKDIR /app

ENV SERVICE_NAME=eac-service
ENV SERVER_PORT=8086
ENV DB_HOST=postgres
ENV DB_PORT=5432
ENV DB_NAME=localpkd
ENV DB_USER=pkd

EXPOSE 8086

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8086/api/eac/health || exit 1

USER app
CMD ["./eac-service"]
