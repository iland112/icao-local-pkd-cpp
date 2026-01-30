#!/bin/bash
# PostgreSQL Helper Functions
# Source this file: source scripts/db-helpers.sh

DB_CONTAINER="icao-local-pkd-postgres"
DB_NAME="localpkd"
DB_USER="pkd"

# Execute SQL query
db_query() {
    local query=$1
    docker exec ${DB_CONTAINER} psql -U ${DB_USER} -d ${DB_NAME} -c "$query"
}

# Count certificates by type
db_count_certs() {
    echo "=========================================="
    echo "Database Certificate Count"
    echo "=========================================="
    db_query "
        SELECT
            certificate_type,
            COUNT(*) as count,
            COUNT(CASE WHEN stored_in_ldap THEN 1 END) as stored_in_ldap
        FROM certificate
        GROUP BY certificate_type
        ORDER BY certificate_type;
    "
}

# Count CRLs
db_count_crls() {
    echo "=========================================="
    echo "Database CRL Count"
    echo "=========================================="
    db_query "
        SELECT
            COUNT(*) as total,
            COUNT(CASE WHEN stored_in_ldap THEN 1 END) as stored_in_ldap,
            COUNT(CASE WHEN NOT stored_in_ldap THEN 1 END) as not_stored
        FROM crl;
    "
}

# Reset CRL stored_in_ldap flags
db_reset_crl_flags() {
    echo "⚠️  Resetting all CRL stored_in_ldap flags to FALSE..."
    db_query "UPDATE crl SET stored_in_ldap = FALSE; SELECT COUNT(*) as reset_count FROM crl WHERE stored_in_ldap = FALSE;"
}

# Show recent reconciliation summary
db_reconciliation_summary() {
    local limit=${1:-5}
    echo "=========================================="
    echo "Recent Reconciliation Summary (last ${limit})"
    echo "=========================================="
    db_query "
        SELECT
            id,
            started_at,
            triggered_by,
            status,
            total_processed,
            success_count,
            failed_count,
            csca_added,
            dsc_added,
            crl_added,
            duration_ms
        FROM reconciliation_summary
        ORDER BY started_at DESC
        LIMIT ${limit};
    "
}

# Show reconciliation logs for a specific reconciliation
db_reconciliation_logs() {
    local reconciliation_id=$1
    if [[ -z "$reconciliation_id" ]]; then
        echo "Usage: db_reconciliation_logs <reconciliation_id>"
        return 1
    fi

    echo "=========================================="
    echo "Reconciliation Logs (ID: ${reconciliation_id})"
    echo "=========================================="
    db_query "
        SELECT
            id,
            cert_type,
            cert_fingerprint,
            country_code,
            operation,
            status,
            error_message,
            duration_ms
        FROM reconciliation_log
        WHERE reconciliation_id = ${reconciliation_id}
        ORDER BY id;
    "
}

# Show latest reconciliation logs
db_latest_reconciliation_logs() {
    echo "=========================================="
    echo "Latest Reconciliation Logs"
    echo "=========================================="
    db_query "
        SELECT
            rl.id,
            rl.cert_type,
            rl.cert_fingerprint,
            rl.country_code,
            rl.operation,
            rl.status,
            rl.duration_ms
        FROM reconciliation_log rl
        WHERE rl.reconciliation_id = (
            SELECT MAX(id) FROM reconciliation_summary
        )
        ORDER BY rl.id
        LIMIT 10;
    "
}

# Show sync status history
db_sync_status() {
    local limit=${1:-5}
    echo "=========================================="
    echo "Sync Status History (last ${limit})"
    echo "=========================================="
    db_query "
        SELECT
            id,
            checked_at,
            db_csca_count,
            db_dsc_count,
            db_crl_count,
            ldap_csca_count,
            ldap_dsc_count,
            ldap_crl_count,
            csca_discrepancy,
            dsc_discrepancy,
            crl_discrepancy,
            total_discrepancy,
            status
        FROM sync_status
        ORDER BY checked_at DESC
        LIMIT ${limit};
    "
}

# Show database connection info
db_info() {
    echo "=========================================="
    echo "Database Connection Info"
    echo "=========================================="
    echo "Container: ${DB_CONTAINER}"
    echo "Database:  ${DB_NAME}"
    echo "User:      ${DB_USER}"
    echo "=========================================="
}

# Export functions
export -f db_query
export -f db_count_certs
export -f db_count_crls
export -f db_reset_crl_flags
export -f db_reconciliation_summary
export -f db_reconciliation_logs
export -f db_latest_reconciliation_logs
export -f db_sync_status
export -f db_info

echo "Database Helper functions loaded:"
echo "  db_info                          - Show connection info"
echo "  db_count_certs                   - Count certificates by type"
echo "  db_count_crls                    - Count CRLs"
echo "  db_reset_crl_flags               - Reset CRL stored_in_ldap flags"
echo "  db_reconciliation_summary [N]    - Show recent reconciliations"
echo "  db_reconciliation_logs <ID>      - Show logs for specific reconciliation"
echo "  db_latest_reconciliation_logs    - Show latest reconciliation logs"
echo "  db_sync_status [N]               - Show sync status history"
echo "  db_query '<SQL>'                 - Execute custom query"
