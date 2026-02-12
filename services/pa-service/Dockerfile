# =============================================================================
# PA Service Dockerfile
# Uses shared icao-vcpkg-base image for pre-built vcpkg dependencies
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Application Build
ARG BASE_IMAGE=icao-vcpkg-base:latest
FROM ${BASE_IMAGE} AS builder

WORKDIR /app

# Ensure Oracle directory exists (may be empty for postgres-only ARM64 builds)
RUN mkdir -p /opt/oracle/instantclient_21_13

# Install image processing libraries for JPEG2000 → JPEG conversion (DG2 face images)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libopenjp2-7-dev \
    libjpeg62-turbo-dev \
    && rm -rf /var/lib/apt/lists/*

COPY services/pa-service/CMakeLists.txt ./

# Copy shared library (common-lib)
COPY services/common-lib/ ../common-lib/

COPY shared/ ./shared/
COPY services/pa-service/src/ ./src/
COPY services/pa-service/tests/ ./tests/

# Build main application
ARG ENABLE_ORACLE=ON
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    -DENABLE_ORACLE=${ENABLE_ORACLE} \
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
    libopenjp2-7 \
    libjpeg62-turbo \
    && rm -rf /var/lib/apt/lists/*

# Copy Oracle Instant Client from builder (may be empty for postgres-only builds)
RUN mkdir -p /opt/oracle/instantclient_21_13
COPY --from=builder /opt/oracle/instantclient_21_13/ /opt/oracle/instantclient_21_13/
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone
RUN useradd -m -s /bin/bash app
RUN mkdir -p /app/logs /app/uploads /app/static /app/config /app/data/cert \
    && chown -R app:app /app

COPY --from=builder /app/build/bin/pa-service /app/

# Copy shared library (common-lib)
COPY --from=builder /app/build/lib/libicao-common.so* /usr/local/lib/
RUN ldconfig

COPY shared/static/swagger-ui/ /app/static/swagger-ui/

WORKDIR /app

ENV SERVICE_NAME=pa-service
ENV SERVER_PORT=8082
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
