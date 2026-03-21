-- =============================================================================
-- ICAO PKD LDAP Sync Log Table - Oracle (v2.39.0)
-- =============================================================================

DECLARE
    v_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_exists FROM user_tables WHERE table_name = 'ICAO_LDAP_SYNC_LOG';
    IF v_exists = 0 THEN
        EXECUTE IMMEDIATE '
        CREATE TABLE icao_ldap_sync_log (
            id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
            sync_type VARCHAR2(50) NOT NULL,
            status VARCHAR2(50) NOT NULL,
            triggered_by VARCHAR2(100) NOT NULL,
            total_remote_count NUMBER(10) DEFAULT 0,
            new_certificates NUMBER(10) DEFAULT 0,
            updated_certificates NUMBER(10) DEFAULT 0,
            failed_count NUMBER(10) DEFAULT 0,
            duration_ms NUMBER(10) DEFAULT 0,
            error_message VARCHAR2(4000),
            started_at TIMESTAMP DEFAULT SYSTIMESTAMP,
            completed_at TIMESTAMP,
            created_at TIMESTAMP DEFAULT SYSTIMESTAMP
        )';

        EXECUTE IMMEDIATE 'CREATE INDEX idx_icao_sync_log_created ON icao_ldap_sync_log (created_at DESC)';
        EXECUTE IMMEDIATE 'CREATE INDEX idx_icao_sync_log_status ON icao_ldap_sync_log (status)';
    END IF;
END;
/
COMMIT;
