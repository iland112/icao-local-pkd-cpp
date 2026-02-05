# =============================================================================
# PA Service Dockerfile
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

# Install Oracle Instant Client (Phase 4.4: for Oracle database support)
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

# Set Oracle environment variables (Phase 4.4)
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
COPY services/pa-service/vcpkg.json ./

# Copy host vcpkg binary cache to avoid re-downloading/building (optional - only exists in local builds)
# Note: .vcpkg-cache is in .gitignore and won't be available in CI/CD builds
# GitHub Actions uses GitHub cache instead (see build-arm64.yml cache-from/cache-to)
# Uncomment for local builds if .vcpkg-cache exists:
# COPY .vcpkg-cache/ /root/.cache/vcpkg/archives/

RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then TRIPLET="x64-linux"; else TRIPLET="arm64-linux"; fi && \
    vcpkg install --triplet $TRIPLET

# Stage 3: Application Build (changes frequently)
FROM vcpkg-deps AS builder

COPY services/pa-service/CMakeLists.txt ./

# Copy shared library (Phase 5: common-lib)
COPY services/common-lib/ ../common-lib/

COPY shared/ ./shared/
COPY services/pa-service/src/ ./src/
COPY services/pa-service/tests/ ./tests/

# Build main application
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    && cmake --build build -j$(nproc)

# Build and run tests (optional, controlled by build arg)
ARG RUN_TESTS=false
RUN if [ "$RUN_TESTS" = "true" ]; then \
        cmake -B build-test -S . \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_BUILD_TYPE=Debug \
        -DBUILD_TESTS=ON \
        && cmake --build build-test -j$(nproc) \
        && echo "=== Running Unit Tests ===" \
        && cd build-test && ctest --output-on-failure; \
    fi

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

# Copy Oracle Instant Client from builder (Phase 4.4: Oracle support)
COPY --from=vcpkg-base /opt/oracle/instantclient_21_13 /opt/oracle/instantclient_21_13
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone
RUN useradd -m -s /bin/bash app
RUN mkdir -p /app/logs /app/uploads /app/static /app/config /app/data/cert \
    && chown -R app:app /app

COPY --from=builder /app/build/bin/pa-service /app/

# Phase 5: Copy shared library (common-lib)
COPY --from=builder /app/build/lib/libicao-common.so* /usr/local/lib/
RUN ldconfig

COPY shared/static/swagger-ui/ /app/static/swagger-ui/

WORKDIR /app

ENV PA Service=PA Service
ENV 8082=8082
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

EXPOSE 8082

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8082/api/health || exit 1

USER app
CMD ["./pa-service"]
