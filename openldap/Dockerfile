# Custom OpenLDAP image based on osixia/openldap
# Includes ICAO PKD custom schema and fixes missing replication config files
FROM osixia/openldap:1.5.0

# Create required empty config files that osixia startup script expects
RUN mkdir -p /container/service/slapd/assets/config/replication && \
    mkdir -p /container/service/slapd/assets/config/admin && \
    echo "# Empty replication disable config" > /container/service/slapd/assets/config/replication/replication-disable.ldif && \
    echo "# Empty replication enable config" > /container/service/slapd/assets/config/replication/replication-enable.ldif && \
    echo "# Empty root password change config" > /container/service/slapd/assets/config/admin/root-password-change.ldif

# Copy ICAO PKD custom schema
COPY schemas/icao-pkd.ldif /container/service/slapd/assets/config/bootstrap/ldif/custom/
