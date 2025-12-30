# =============================================================================
# ICAO Local PKD - Multi-stage Docker Build
# =============================================================================

# -----------------------------------------------------------------------------
# Stage 1: Build Environment
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

# Install build dependencies
RUN apt-get update && apt-get install -y \
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
COPY CMakeLists.txt vcpkg.json ./
COPY src/ ./src/
COPY include/ ./include/
COPY tests/ ./tests/

# Install vcpkg dependencies
RUN vcpkg install --triplet x64-linux

# Build the application
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    && cmake --build build -j$(nproc)

# -----------------------------------------------------------------------------
# Stage 2: Runtime Environment
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

# Install runtime dependencies only
RUN apt-get update && apt-get install -y \
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
    && rm -rf /var/lib/apt/lists/*

# Set timezone
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# Create app user
RUN useradd -m -s /bin/bash app

# Create directories
RUN mkdir -p /app/logs /app/uploads /app/static /app/config \
    && chown -R app:app /app

# Copy built binary from builder
COPY --from=builder /app/build/bin/icao-local-pkd /app/

# Copy static files if any
# COPY --from=builder /app/static/ /app/static/

WORKDIR /app
USER app

# Expose port
EXPOSE 8081

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8081/api/health || exit 1

# Run the application
CMD ["./icao-local-pkd"]
