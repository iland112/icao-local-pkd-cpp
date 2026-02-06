# =============================================================================
# PKD Relay Service Dockerfile
# Multi-stage build for optimized production image with aggressive caching
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: vcpkg Base (rarely changes - heavily cached)
FROM debian:bookworm AS vcpkg-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    wget \
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
    libaio1 \
    && rm -rf /var/lib/apt/lists/*

# Install Oracle Instant Client (Phase 5.2: for Oracle database support)
# Version: 21.13 (compatible with Oracle 11g, 12c, 18c, 19c, 21c)
RUN mkdir -p /opt/oracle && cd /opt/oracle && \
    # Download Oracle Instant Client Basic and SDK
    wget -q https://download.oracle.com/otn_software/linux/instantclient/2113000/instantclient-basic-linux.x64-21.13.0.0.0dbru.zip && \
    wget -q https://download.oracle.com/otn_software/linux/instantclient/2113000/instantclient-sdk-linux.x64-21.13.0.0.0dbru.zip && \
    # Extract files
    unzip -q instantclient-basic-linux.x64-21.13.0.0.0dbru.zip && \
    unzip -q instantclient-sdk-linux.x64-21.13.0.0.0dbru.zip && \
    # Cleanup zip files
    rm -f *.zip && \
    # Create symbolic links for compatibility
    cd instantclient_21_13 && \
    ln -sf libclntsh.so.21.1 libclntsh.so && \
    ln -sf libocci.so.21.1 libocci.so && \
    echo "Oracle Instant Client installed successfully"

# Set Oracle environment variables (Phase 5.2)
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git \
    && cd vcpkg \
    && ./bootstrap-vcpkg.sh

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Stage 2: vcpkg Dependencies (changes only when vcpkg.json changes)
FROM vcpkg-base AS vcpkg-deps

WORKDIR /app
COPY services/pkd-relay-service/vcpkg.json ./

RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then TRIPLET="x64-linux"; else TRIPLET="arm64-linux"; fi && \
    vcpkg install --triplet $TRIPLET

# Stage 3: Application Build (changes frequently)
FROM vcpkg-deps AS builder

COPY services/pkd-relay-service/CMakeLists.txt ./
COPY services/pkd-relay-service/src/ ./src/
# Copy shared libraries (required by CMakeLists.txt line 35: add_subdirectory)
COPY shared/ ./shared/

RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# Stage 4: Runtime
FROM debian:bookworm-slim AS runtime

# ARG to force cache invalidation (must be re-declared in each stage)
ARG CACHE_BUST=unknown
RUN echo "=== Runtime Cache Bust Token: $CACHE_BUST ===" && \
    echo "This ensures fresh binary copy from builder"

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

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
    libaio1 \
    && rm -rf /var/lib/apt/lists/*

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone
RUN useradd -m -s /bin/bash app
RUN mkdir -p /app/logs /app/uploads /app/static /app/config /app/data/cert \
    && chown -R app:app /app

# Copy Oracle Instant Client from builder stage (Phase 5.2)
COPY --from=builder /opt/oracle /opt/oracle

# Set Oracle environment variables for runtime (Phase 5.2)
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

COPY --from=builder /app/build/bin/pkd-relay-service /app/

WORKDIR /app

ENV SERVICE_NAME=pkd-relay
ENV SERVER_PORT=8083
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

EXPOSE 8083

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8083/api/sync/health || exit 1

USER app
CMD ["./pkd-relay-service"]
