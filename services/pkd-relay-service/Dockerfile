# =============================================================================
# PKD Relay Service Dockerfile
# Uses shared icao-vcpkg-base image for pre-built vcpkg dependencies
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Application Build
ARG BASE_IMAGE=icao-vcpkg-base:latest
FROM ${BASE_IMAGE} AS builder

WORKDIR /app

# Ensure Oracle directory exists (may be empty for postgres-only ARM64 builds)
RUN mkdir -p /opt/oracle/instantclient_21_13

COPY services/pkd-relay-service/CMakeLists.txt ./
COPY services/pkd-relay-service/src/ ./src/
# Copy shared libraries (required by CMakeLists.txt add_subdirectory)
COPY shared/ ./shared/
# Copy common-lib (required by upload module)
COPY services/common-lib/ ./common-lib/

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

# Copy Oracle Instant Client from builder (may be empty for postgres-only builds)
RUN mkdir -p /opt/oracle/instantclient_21_13
COPY --from=builder /opt/oracle/instantclient_21_13/ /opt/oracle/instantclient_21_13/

# Set Oracle environment variables for runtime
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

COPY --from=builder /app/build/bin/pkd-relay-service /app/
# Copy shared libraries built from common-lib and shared/
COPY --from=builder /app/build/lib/libicao-common.so* /app/lib/
ENV LD_LIBRARY_PATH=$ORACLE_HOME:/app/lib:$LD_LIBRARY_PATH

WORKDIR /app

ENV SERVICE_NAME=pkd-relay
ENV SERVER_PORT=8083
ENV DB_HOST=postgres
ENV DB_PORT=5432
ENV DB_NAME=localpkd
ENV DB_USER=pkd
# DB_PASSWORD must be provided at runtime via docker-compose .env or -e flag (not baked into image)
ENV LDAPTLS_REQCERT=never
RUN mkdir -p /etc/ldap && printf "TLS_REQCERT never\nTLS_CACERT /app/icao-tls/ca.pem\nTLS_CERT /app/icao-tls/client.pem\nTLS_KEY /app/icao-tls/client-key.pem\n" > /etc/ldap/ldap.conf && \
    mkdir -p /etc/openldap && cp /etc/ldap/ldap.conf /etc/openldap/ldap.conf
ENV LDAP_HOST=haproxy
ENV LDAP_PORT=389
ENV LDAP_WRITE_HOST=openldap1
ENV LDAP_WRITE_PORT=389
ENV LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
# LDAP_BIND_PASSWORD must be provided at runtime via docker-compose .env or -e flag (not baked into image)

EXPOSE 8083

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8083/api/sync/health || exit 1

USER app
CMD ["./pkd-relay-service"]
