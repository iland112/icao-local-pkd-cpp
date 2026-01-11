# =============================================================================
# PKD Management Service Dockerfile
# Multi-stage build for optimized production image with aggressive caching
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: vcpkg Base (rarely changes - heavily cached)
FROM debian:bookworm AS vcpkg-base

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

# Stage 2: vcpkg Dependencies (changes only when vcpkg.json changes)
FROM vcpkg-base AS vcpkg-deps

WORKDIR /app

# Copy ONLY dependency manifests (for better layer caching)
COPY services/pkd-management/vcpkg.json ./

# Install vcpkg dependencies (auto-detect architecture)
# This layer will be cached unless vcpkg.json changes
RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then TRIPLET="x64-linux"; else TRIPLET="arm64-linux"; fi && \
    vcpkg install --triplet $TRIPLET

# Stage 3: Application Build (changes frequently)
FROM vcpkg-deps AS builder

# ARG to force cache invalidation on every build
# This will be set to github.sha in GitHub Actions workflow
ARG CACHE_BUST=unknown
RUN echo "=== Cache Bust Token: $CACHE_BUST ===" && \
    echo "This ensures fresh build on every run"

WORKDIR /app

# CRITICAL: Clean any potential cached artifacts from vcpkg-deps stage
RUN echo "=== CLEANING ALL POTENTIAL CACHED ARTIFACTS ===" && \
    rm -rf build build_fresh bin lib CMakeCache.txt CMakeFiles && \
    find . -name "*.o" -delete && \
    find . -name "*.a" -delete && \
    find . -name "CMakeCache.txt" -delete && \
    echo "=== CLEANUP COMPLETE ==="

# Copy build configuration (rarely changes)
COPY services/pkd-management/CMakeLists.txt ./

# Copy shared code (changes occasionally)
COPY shared/ ./shared/

# Copy source code (changes frequently) - AFTER CACHE_BUST
COPY services/pkd-management/src/ ./src/

# DEBUG: Verify source code was copied correctly
RUN echo "=== VERIFYING COPIED SOURCE CODE ===" && \
    grep "spdlog::info.*ICAO" ./src/main.cpp | head -5 && \
    echo "=== END VERIFICATION ==="

# CRITICAL: Touch all source files to force CMake to recompile
RUN echo "=== FORCING SOURCE FILES TIMESTAMP UPDATE ===" && \
    find ./src -type f -name "*.cpp" -exec touch {} \; && \
    find ./src -type f -name "*.h" -exec touch {} \; && \
    echo "=== TIMESTAMP UPDATE COMPLETE ==="

# Build application in completely new directory with verbose output
RUN mkdir -p build_fresh && \
    cmake -B build_fresh -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_VERBOSE_MAKEFILE=ON && \
    cmake --build build_fresh -j$(nproc) --verbose

# CRITICAL: Copy binary with CACHE_BUST in filename to force cache invalidation
ARG CACHE_BUST
RUN cp build_fresh/bin/pkd-management build_fresh/bin/pkd-management-${CACHE_BUST} && \
    echo "=== Created versioned binary: pkd-management-${CACHE_BUST} ===" && \
    strings build_fresh/bin/pkd-management-${CACHE_BUST} | grep -i "ICAO.*PKD" | head -10

# Stage 4: Runtime (production image)
FROM debian:bookworm-slim AS runtime

# ARG to force cache invalidation (must be re-declared in each stage)
ARG CACHE_BUST=unknown
RUN echo "=== Runtime Cache Bust Token: $CACHE_BUST ===" && \
    echo "This ensures fresh binary copy from builder"

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

# CRITICAL: Copy versioned binary - filename changes force cache invalidation
ARG CACHE_BUST
COPY --from=builder /app/build_fresh/bin/pkd-management-${CACHE_BUST} /app/pkd-management

# Verify binary version immediately after COPY
RUN apt-get update && apt-get install -y binutils && \
    echo "=== Verifying copied binary version ===" && \
    strings /app/pkd-management | grep "ICAO Local PKD v" && \
    apt-get remove -y binutils && apt-get autoremove -y && rm -rf /var/lib/apt/lists/*

# Copy Swagger UI static files
COPY shared/static/swagger-ui/ /app/static/swagger-ui/

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
