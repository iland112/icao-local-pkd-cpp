# =============================================================================
# PA Service Dockerfile
# Multi-stage build for optimized production image
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Build
FROM --platform=$TARGETPLATFORM ubuntu:22.04 AS builder

# Build arguments for multi-arch support
ARG TARGETPLATFORM
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive

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
    linux-libc-dev \
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
COPY services/pa-service/CMakeLists.txt services/pa-service/vcpkg.json ./
COPY services/pa-service/src/ ./src/

# Copy shared code
COPY shared/ ./shared/

# Set vcpkg triplet based on architecture (amd64->x64, arm64->arm64)
ENV VCPKG_TRIPLET=${TARGETARCH}-linux
RUN if [ "$TARGETARCH" = "amd64" ]; then export VCPKG_TRIPLET="x64-linux"; else export VCPKG_TRIPLET="arm64-linux"; fi && \
    vcpkg install --triplet $VCPKG_TRIPLET

# Build
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# Stage 2: Runtime
FROM --platform=$TARGETPLATFORM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Seoul

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
    ldap-utils \
    && rm -rf /var/lib/apt/lists/*

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone

RUN useradd -m -s /bin/bash app

RUN mkdir -p /app/logs /app/static /app/config \
    && chown -R app:app /app

COPY --from=builder /app/build/bin/pa-service /app/

WORKDIR /app

# Environment variables
ENV SERVICE_NAME=pa-service
ENV SERVER_PORT=8082
ENV DB_HOST=postgres
ENV DB_PORT=5432
ENV DB_NAME=localpkd
ENV DB_USER=pkd
ENV DB_PASSWORD=pkd123
ENV LDAP_HOST=haproxy
ENV LDAP_PORT=389
ENV LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
ENV LDAP_BIND_PASSWORD=admin

EXPOSE 8082

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8082/api/health || exit 1

USER app

CMD ["./pa-service"]
