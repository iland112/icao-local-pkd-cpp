# =============================================================================
# Sync Service Dockerfile
# Multi-stage build for optimized production image
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Build
FROM debian:bookworm AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ninja-build \
    libssl-dev \
    libpq-dev \
    libldap2-dev \
    libjsoncpp-dev \
    libc-ares-dev \
    libbrotli-dev \
    zlib1g-dev \
    bison \
    flex \
    autoconf \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git \
    && cd vcpkg \
    && ./bootstrap-vcpkg.sh

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Copy project files
WORKDIR /app
COPY services/sync-service/CMakeLists.txt services/sync-service/vcpkg.json ./
COPY services/sync-service/src/ ./src/

# Install vcpkg dependencies (auto-detect architecture)
RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then TRIPLET="x64-linux"; else TRIPLET="arm64-linux"; fi && \
    vcpkg install --triplet $TRIPLET

# Build
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# Stage 2: Runtime
FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libpq5 \
    libldap-2.5-0 \
    libjsoncpp25 \
    libc-ares2 \
    libbrotli1 \
    zlib1g \
    ca-certificates \
    tzdata \
    curl \
    ldap-utils \
    && rm -rf /var/lib/apt/lists/*

# Set timezone
RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone

# Create app user
RUN useradd -m -s /bin/bash app

# Copy binary from builder
COPY --from=builder /app/build/bin/sync-service /app/

# Copy Swagger UI static files
COPY shared/static/swagger-ui/ /app/static/swagger-ui/

# Create directories and set ownership
RUN mkdir -p /app/logs /app/config /app/static \
    && chown -R app:app /app

WORKDIR /app

# Environment variables
ENV SERVICE_NAME=sync-service
ENV SERVER_PORT=8083
ENV DB_HOST=postgres
ENV DB_PORT=5432
ENV DB_NAME=pkd
ENV DB_USER=pkd
ENV DB_PASSWORD=pkd123
ENV LDAP_HOST=haproxy
ENV LDAP_PORT=389
ENV LDAP_WRITE_HOST=openldap1
ENV LDAP_WRITE_PORT=389
ENV LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
ENV LDAP_BIND_PASSWORD=admin
ENV LDAP_BASE_DN=dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
ENV SYNC_INTERVAL_MINUTES=5
ENV AUTO_RECONCILE=true
ENV MAX_RECONCILE_BATCH_SIZE=100

EXPOSE 8083

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=15s --retries=3 \
    CMD curl -f http://localhost:8083/api/sync/health || exit 1

USER app

CMD ["./sync-service"]
