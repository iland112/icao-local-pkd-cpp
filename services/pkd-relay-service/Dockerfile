# =============================================================================
# PKD Relay Service Dockerfile
# Uses shared icao-vcpkg-base image for pre-built vcpkg dependencies
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Application Build
FROM icao-vcpkg-base:latest AS builder

WORKDIR /app

COPY services/pkd-relay-service/CMakeLists.txt ./
COPY services/pkd-relay-service/src/ ./src/
# Copy shared libraries (required by CMakeLists.txt add_subdirectory)
COPY shared/ ./shared/

RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# Stage 2: Runtime
FROM debian:bookworm-slim AS runtime

ARG CACHE_BUST=unknown

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

# Copy Oracle Instant Client from builder
COPY --from=builder /opt/oracle /opt/oracle

# Set Oracle environment variables for runtime
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
