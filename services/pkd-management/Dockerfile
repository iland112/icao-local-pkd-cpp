# =============================================================================
# PKD Management Service Dockerfile
# Uses shared icao-vcpkg-base image for pre-built vcpkg dependencies
# Supports: linux/amd64, linux/arm64
# =============================================================================

# Stage 1: Application Build
FROM icao-vcpkg-base:latest AS builder

# ARG to force cache invalidation on every build
ARG CACHE_BUST=unknown
RUN echo "=== Cache Bust Token: $CACHE_BUST ==="

WORKDIR /app

# Clean any potential cached artifacts
RUN rm -rf build build_fresh bin lib CMakeCache.txt CMakeFiles

# Copy build configuration
COPY services/pkd-management/CMakeLists.txt ./

# Copy shared library (common-lib)
COPY services/common-lib/ ../common-lib/

# Copy shared code
COPY shared/ ./shared/

# Copy source code - AFTER CACHE_BUST
COPY services/pkd-management/src/ ./src/

# Copy tests directory
COPY services/pkd-management/tests/ ./tests/

# Force CMake to recompile by touching source files
RUN find ./src -type f \( -name "*.cpp" -o -name "*.h" \) -exec touch {} \;

# Build application
RUN mkdir -p build_fresh && \
    cmake -B build_fresh -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_VERBOSE_MAKEFILE=ON && \
    cmake --build build_fresh -j$(nproc) --verbose

# Create versioned binary for cache invalidation
ARG CACHE_BUST
RUN cp build_fresh/bin/pkd-management build_fresh/bin/pkd-management-${CACHE_BUST}

# Stage 2: Runtime (production image)
FROM debian:bookworm-slim AS runtime

ARG CACHE_BUST=unknown

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
    libaio1 \
    && rm -rf /var/lib/apt/lists/*

# Set timezone
RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone

# Create app user
RUN useradd -m -s /bin/bash app

# Create directories
RUN mkdir -p /app/logs /app/uploads /app/static /app/config /app/data/cert \
    && chown -R app:app /app

# Copy versioned binary
ARG CACHE_BUST
COPY --from=builder /app/build_fresh/bin/pkd-management-${CACHE_BUST} /app/pkd-management

# Copy test binary
COPY --from=builder /app/build_fresh/bin/test_ldap_dn /app/test_ldap_dn

# Copy shared library (common-lib)
COPY --from=builder /app/build_fresh/lib/libicao-common.so* /usr/local/lib/

# Copy Oracle Instant Client libraries (runtime dependency)
COPY --from=builder /opt/oracle/instantclient_21_13/ /opt/oracle/instantclient_21_13/
ENV ORACLE_HOME=/opt/oracle/instantclient_21_13
ENV LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH
ENV PATH=$ORACLE_HOME:$PATH

RUN ldconfig

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
