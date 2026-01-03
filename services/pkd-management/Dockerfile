# =============================================================================
# PKD Management Service Dockerfile
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
    uuid-dev \
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
COPY services/pkd-management/CMakeLists.txt services/pkd-management/vcpkg.json ./
COPY services/pkd-management/src/ ./src/

# Copy shared code
COPY shared/ ./shared/

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
    libuuid1 \
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

# Create directories
RUN mkdir -p /app/logs /app/uploads /app/static /app/config /app/data/cert \
    && chown -R app:app /app

# Copy binary from builder
COPY --from=builder /app/build/bin/pkd-management /app/

WORKDIR /app

# Environment variables
ENV SERVICE_NAME=pkd-management
ENV SERVER_PORT=8081
ENV DB_HOST=postgres
ENV DB_PORT=5432
ENV DB_NAME=localpkd
ENV DB_USER=pkd
ENV DB_PASSWORD=pkd123
ENV LDAP_HOST=haproxy
ENV LDAP_PORT=389
ENV LDAP_WRITE_HOST=openldap1
ENV LDAP_WRITE_PORT=389
ENV LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
ENV LDAP_BIND_PASSWORD=admin
# Trust anchor certificate path - mount externally or set via environment
ENV TRUST_ANCHOR_PATH=/app/data/cert/trust_anchor.pem

EXPOSE 8081

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8081/api/health || exit 1

USER app

CMD ["./pkd-management"]
