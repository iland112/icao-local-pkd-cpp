# Custom OpenLDAP image based on osixia/openldap
# Includes ICAO PKD custom schema and MMR replication setup
FROM osixia/openldap:1.5.0

# Note: ldap-utils is already included in osixia/openldap image

# Create required empty config files that osixia startup script expects
RUN mkdir -p /container/service/slapd/assets/config/replication && \
    mkdir -p /container/service/slapd/assets/config/admin && \
    echo "# Empty replication disable config" > /container/service/slapd/assets/config/replication/replication-disable.ldif && \
    echo "# Empty replication enable config" > /container/service/slapd/assets/config/replication/replication-enable.ldif && \
    echo "# Empty root password change config" > /container/service/slapd/assets/config/admin/root-password-change.ldif

# Copy ICAO PKD custom schema (goes to schema directory)
COPY schemas/icao-pkd.ldif /container/service/slapd/assets/config/bootstrap/schema/

# Note: PKD DIT structure is initialized by ldap-init container, not bootstrap LDIF
# This avoids conflicts with osixia/openldap's base DN creation

# Copy MMR setup script
COPY scripts/setup-mmr.sh /setup-mmr.sh
RUN chmod +x /setup-mmr.sh

# Fix permissions for all copied files
RUN chmod 644 /container/service/slapd/assets/config/bootstrap/schema/icao-pkd.ldif
