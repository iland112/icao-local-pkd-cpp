# =============================================================================
# ICAO PKD Simulation LDAP Server
# =============================================================================
# Simulates the ICAO PKD Download LDAP (pkddownloadsg.icao.int)
# DIT: dc=download,dc=pkd,dc=icao,dc=int
# Purpose: Development/testing of automatic LDAP V3 sync
# =============================================================================
FROM osixia/openldap:1.5.0

# Create required empty config files that osixia startup script expects
RUN mkdir -p /container/service/slapd/assets/config/replication && \
    mkdir -p /container/service/slapd/assets/config/admin && \
    echo "# Empty" > /container/service/slapd/assets/config/replication/replication-disable.ldif && \
    echo "# Empty" > /container/service/slapd/assets/config/replication/replication-enable.ldif && \
    echo "# Empty" > /container/service/slapd/assets/config/admin/root-password-change.ldif

# Copy ICAO PKD schema (same as local, different DIT)
COPY schemas/icao-pkd.ldif /container/service/slapd/assets/config/bootstrap/schema/

# Copy population script
COPY scripts/populate-from-local.sh /populate-from-local.sh
RUN chmod +x /populate-from-local.sh

# Fix permissions
RUN chmod 644 /container/service/slapd/assets/config/bootstrap/schema/icao-pkd.ldif
