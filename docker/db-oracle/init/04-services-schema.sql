-- =============================================================================
-- ICAO Local PKD - Services Schema (Oracle Version)
-- =============================================================================
-- Version: 2.0.0 (Oracle Migration)
-- Created: 2026-02-05
-- Description: Schema for Sync, Monitoring, and Reconciliation services
-- Converted from PostgreSQL to Oracle DDL
-- =============================================================================

-- SQL*Plus settings
SET SQLBLANKLINES ON

-- Connect as PKD_USER to ORCLPDB1 (Pluggable Database — Oracle EE 21c)
CONNECT pkd_user/pkd_password@ORCLPDB1;

-- Allow re-runs: skip "already exists" errors (ORA-00955, ORA-01430, ORA-02261, ORA-01442)
WHENEVER SQLERROR CONTINUE;

-- =============================================================================
-- Sequences
-- =============================================================================

CREATE SEQUENCE seq_sync_status START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_system_metrics START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_service_health START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_recon_summary START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_recon_log START WITH 1 INCREMENT BY 1 NOCACHE;

-- =============================================================================
-- DB-LDAP Sync Service
-- =============================================================================

-- Sync status tracking (periodic sync check results)
CREATE TABLE sync_status (
    id NUMBER(10) PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- DB statistics
    db_csca_count NUMBER(10) DEFAULT 0 NOT NULL,
    db_mlsc_count NUMBER(10) DEFAULT 0 NOT NULL,  -- v2.1.2.6
    db_dsc_count NUMBER(10) DEFAULT 0 NOT NULL,
    db_dsc_nc_count NUMBER(10) DEFAULT 0 NOT NULL,
    db_crl_count NUMBER(10) DEFAULT 0 NOT NULL,
    db_stored_in_ldap_count NUMBER(10) DEFAULT 0 NOT NULL,

    -- LDAP statistics
    ldap_csca_count NUMBER(10) DEFAULT 0 NOT NULL,
    ldap_mlsc_count NUMBER(10) DEFAULT 0 NOT NULL,  -- v2.1.2.6
    ldap_dsc_count NUMBER(10) DEFAULT 0 NOT NULL,
    ldap_dsc_nc_count NUMBER(10) DEFAULT 0 NOT NULL,
    ldap_crl_count NUMBER(10) DEFAULT 0 NOT NULL,
    ldap_total_entries NUMBER(10) DEFAULT 0 NOT NULL,

    -- Discrepancy counts
    csca_discrepancy NUMBER(10) DEFAULT 0 NOT NULL,
    mlsc_discrepancy NUMBER(10) DEFAULT 0 NOT NULL,  -- v2.1.2.6
    dsc_discrepancy NUMBER(10) DEFAULT 0 NOT NULL,
    dsc_nc_discrepancy NUMBER(10) DEFAULT 0 NOT NULL,
    crl_discrepancy NUMBER(10) DEFAULT 0 NOT NULL,
    total_discrepancy NUMBER(10) DEFAULT 0 NOT NULL,

    -- Country-level statistics (JSON stored as CLOB)
    db_country_stats CLOB,
    ldap_country_stats CLOB,

    -- Overall status
    status VARCHAR2(20) DEFAULT 'UNKNOWN' NOT NULL,
    error_message CLOB,
    check_duration_ms NUMBER(10) DEFAULT 0 NOT NULL
);

-- Trigger for auto-increment ID
CREATE OR REPLACE TRIGGER trg_sync_status_id
BEFORE INSERT ON sync_status
FOR EACH ROW
WHEN (NEW.id IS NULL)
BEGIN
    SELECT seq_sync_status.NEXTVAL INTO :NEW.id FROM DUAL;
END;
/

CREATE INDEX idx_sync_status_checked_at ON sync_status(checked_at);
CREATE INDEX idx_sync_status_status ON sync_status(status);

-- =============================================================================
-- Monitoring Service
-- =============================================================================

-- System metrics tracking
CREATE TABLE system_metrics (
    id NUMBER(10) PRIMARY KEY,
    timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,
    metric_name VARCHAR2(100) NOT NULL,
    metric_value NUMBER NOT NULL,
    metric_unit VARCHAR2(20),
    service_name VARCHAR2(50),
    tags CLOB  -- JSON stored as CLOB
);

-- Trigger for auto-increment ID
CREATE OR REPLACE TRIGGER trg_system_metrics_id
BEFORE INSERT ON system_metrics
FOR EACH ROW
WHEN (NEW.id IS NULL)
BEGIN
    SELECT seq_system_metrics.NEXTVAL INTO :NEW.id FROM DUAL;
END;
/

CREATE INDEX idx_sys_metrics_timestamp ON system_metrics(timestamp);
CREATE INDEX idx_sys_metrics_name ON system_metrics(metric_name);
CREATE INDEX idx_sys_metrics_service ON system_metrics(service_name);

-- Service health status
CREATE TABLE service_health (
    id NUMBER(10) PRIMARY KEY,
    service_name VARCHAR2(50) NOT NULL UNIQUE,
    status VARCHAR2(20) DEFAULT 'UNKNOWN' NOT NULL,
    last_check_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    error_message CLOB,
    uptime_seconds NUMBER(19),
    metadata CLOB  -- JSON stored as CLOB
);

-- Trigger for auto-increment ID
CREATE OR REPLACE TRIGGER trg_service_health_id
BEFORE INSERT ON service_health
FOR EACH ROW
WHEN (NEW.id IS NULL)
BEGIN
    SELECT seq_service_health.NEXTVAL INTO :NEW.id FROM DUAL;
END;
/

-- Note: service_name already has UNIQUE constraint (implicit index)
CREATE INDEX idx_service_health_status ON service_health(status);

-- =============================================================================
-- Auto Reconciliation Service
-- =============================================================================

-- Reconciliation summary (high-level reconciliation results)
CREATE TABLE reconciliation_summary (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    sync_status_id NUMBER(10),
    triggered_by VARCHAR2(20) NOT NULL,
    status VARCHAR2(20) DEFAULT 'IN_PROGRESS' NOT NULL,
    dry_run NUMBER(1) DEFAULT 0 NOT NULL,  -- v2.1.2.6

    -- Execution details
    started_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_at TIMESTAMP,
    duration_ms NUMBER(10),

    -- Results
    total_processed NUMBER(10) DEFAULT 0 NOT NULL,
    success_count NUMBER(10) DEFAULT 0 NOT NULL,  -- v2.1.2.6 renamed from total_success
    failed_count NUMBER(10) DEFAULT 0 NOT NULL,   -- v2.1.2.6 renamed from total_failed
    csca_added NUMBER(10) DEFAULT 0 NOT NULL,
    csca_deleted NUMBER(10) DEFAULT 0 NOT NULL,   -- v2.1.2.6
    dsc_added NUMBER(10) DEFAULT 0 NOT NULL,
    dsc_deleted NUMBER(10) DEFAULT 0 NOT NULL,    -- v2.1.2.6
    dsc_nc_added NUMBER(10) DEFAULT 0 NOT NULL,
    dsc_nc_deleted NUMBER(10) DEFAULT 0 NOT NULL, -- v2.1.2.6
    crl_added NUMBER(10) DEFAULT 0 NOT NULL,
    crl_deleted NUMBER(10) DEFAULT 0 NOT NULL,    -- v2.1.2.6

    error_message CLOB,
    metadata CLOB,  -- JSON stored as CLOB

    CONSTRAINT fk_recon_sync_status FOREIGN KEY (sync_status_id) REFERENCES sync_status(id) ON DELETE SET NULL
);

CREATE INDEX idx_recon_triggered_by ON reconciliation_summary(triggered_by);
CREATE INDEX idx_recon_status ON reconciliation_summary(status);
CREATE INDEX idx_recon_started_at ON reconciliation_summary(started_at);
CREATE INDEX idx_recon_sync_status ON reconciliation_summary(sync_status_id);

-- Reconciliation detailed logs (per-operation logs)
CREATE TABLE reconciliation_log (
    id NUMBER(10) PRIMARY KEY,
    summary_id VARCHAR2(36) NOT NULL,
    operation VARCHAR2(20) NOT NULL,
    certificate_type VARCHAR2(10),
    country_code VARCHAR2(3),
    subject_dn CLOB,
    fingerprint_sha256 VARCHAR2(64),
    status VARCHAR2(20) NOT NULL,
    error_message CLOB,
    started_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_at TIMESTAMP,
    duration_ms NUMBER(10),

    CONSTRAINT fk_recon_log_summary FOREIGN KEY (summary_id) REFERENCES reconciliation_summary(id) ON DELETE CASCADE
);

-- Trigger for auto-increment ID
CREATE OR REPLACE TRIGGER trg_recon_log_id
BEFORE INSERT ON reconciliation_log
FOR EACH ROW
WHEN (NEW.id IS NULL)
BEGIN
    SELECT seq_recon_log.NEXTVAL INTO :NEW.id FROM DUAL;
END;
/

CREATE INDEX idx_recon_log_summary ON reconciliation_log(summary_id);
CREATE INDEX idx_recon_log_operation ON reconciliation_log(operation);
CREATE INDEX idx_recon_log_status ON reconciliation_log(status);
CREATE INDEX idx_recon_log_started_at ON reconciliation_log(started_at);

-- =============================================================================
-- Revalidation History (certificate expiration re-checks)
-- =============================================================================

CREATE SEQUENCE seq_revalidation_history START WITH 1 INCREMENT BY 1 NOCACHE;

CREATE TABLE revalidation_history (
    id NUMBER(10) PRIMARY KEY,
    executed_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    total_processed NUMBER(10) DEFAULT 0 NOT NULL,
    newly_expired NUMBER(10) DEFAULT 0 NOT NULL,
    newly_valid NUMBER(10) DEFAULT 0 NOT NULL,
    unchanged NUMBER(10) DEFAULT 0 NOT NULL,
    errors NUMBER(10) DEFAULT 0 NOT NULL,
    duration_ms NUMBER(10) DEFAULT 0 NOT NULL
);

CREATE OR REPLACE TRIGGER trg_revalidation_history_id
BEFORE INSERT ON revalidation_history
FOR EACH ROW
WHEN (NEW.id IS NULL)
BEGIN
    SELECT seq_revalidation_history.NEXTVAL INTO :NEW.id FROM DUAL;
END;
/

CREATE INDEX idx_reval_history_executed ON revalidation_history(executed_at DESC);

-- =============================================================================
-- Sync Configuration (daily sync settings)
-- =============================================================================

CREATE TABLE sync_config (
    id NUMBER(10) PRIMARY KEY,
    daily_sync_enabled NUMBER(1) DEFAULT 1 NOT NULL,
    daily_sync_hour NUMBER(5) DEFAULT 0 NOT NULL,
    daily_sync_minute NUMBER(5) DEFAULT 0 NOT NULL,
    auto_reconcile NUMBER(1) DEFAULT 0 NOT NULL,
    revalidate_certs_on_sync NUMBER(1) DEFAULT 1 NOT NULL,
    max_reconcile_batch_size NUMBER(10) DEFAULT 100 NOT NULL,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    updated_by VARCHAR2(100),
    CONSTRAINT chk_single_config_row CHECK (id = 1)
);

-- Insert default config row
INSERT INTO sync_config (id, daily_sync_enabled, daily_sync_hour, daily_sync_minute,
    auto_reconcile, revalidate_certs_on_sync, max_reconcile_batch_size)
VALUES (1, 1, 0, 0, 0, 1, 100);

-- =============================================================================
-- CRL Revocation Log (CRL check audit trail)
-- =============================================================================

CREATE TABLE crl_revocation_log (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    certificate_id VARCHAR2(128) NOT NULL,
    certificate_type VARCHAR2(20) NOT NULL,
    serial_number VARCHAR2(100),
    fingerprint_sha256 VARCHAR2(64),
    subject_dn VARCHAR2(4000),
    revocation_status VARCHAR2(20),
    revocation_reason VARCHAR2(50),
    revocation_date VARCHAR2(50),
    crl_id VARCHAR2(128),
    crl_issuer_dn VARCHAR2(4000),
    crl_this_update VARCHAR2(50),
    crl_next_update VARCHAR2(50),
    checked_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    check_duration_ms NUMBER(10)
);

CREATE INDEX idx_crl_revlog_cert_id ON crl_revocation_log(certificate_id);
CREATE INDEX idx_crl_revlog_fingerprint ON crl_revocation_log(fingerprint_sha256);
CREATE INDEX idx_crl_revlog_checked_at ON crl_revocation_log(checked_at DESC);

-- =============================================================================
-- Commit changes
-- =============================================================================

COMMIT;

-- Display completion message
BEGIN
    DBMS_OUTPUT.PUT_LINE('=============================================================================');
    DBMS_OUTPUT.PUT_LINE('Services schema created successfully');
    DBMS_OUTPUT.PUT_LINE('Tables: 8 (sync_status, system_metrics, service_health,');
    DBMS_OUTPUT.PUT_LINE('        reconciliation_summary, reconciliation_log,');
    DBMS_OUTPUT.PUT_LINE('        revalidation_history, sync_config, crl_revocation_log)');
    DBMS_OUTPUT.PUT_LINE('Sequences: 6');
    DBMS_OUTPUT.PUT_LINE('Triggers: 5 (auto-increment IDs)');
    DBMS_OUTPUT.PUT_LINE('=============================================================================');
END;
/

EXIT;
