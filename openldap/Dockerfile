# Custom OpenLDAP image based on osixia/openldap
# Includes ICAO PKD custom schema and fixes missing replication config files
FROM osixia/openldap:1.5.0

# Create required empty config files that osixia startup script expects
RUN mkdir -p /container/service/slapd/assets/config/replication && \
    mkdir -p /container/service/slapd/assets/config/admin && \
    echo "# Empty replication disable config" > /container/service/slapd/assets/config/replication/replication-disable.ldif && \
    echo "# Empty replication enable config" > /container/service/slapd/assets/config/replication/replication-enable.ldif && \
    echo "# Empty root password change config" > /container/service/slapd/assets/config/admin/root-password-change.ldif

# Copy ICAO PKD custom schema (goes to schema directory)
COPY schemas/icao-pkd.ldif /container/service/slapd/assets/config/bootstrap/schema/

# Copy PKD DIT structure initialization LDIF (goes to custom directory for ldapadd)
COPY bootstrap/ /container/service/slapd/assets/config/bootstrap/ldif/custom/

# Fix permissions for all copied files
RUN chmod 644 /container/service/slapd/assets/config/bootstrap/schema/icao-pkd.ldif && \
    chmod 644 /container/service/slapd/assets/config/bootstrap/ldif/custom/*.ldif || true
