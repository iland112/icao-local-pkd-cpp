-- =============================================================================
-- ICAO Local PKD - Oracle Database Schema
-- =============================================================================
-- Version: 2.0.0 (Oracle Edition)
-- Created: 2026-02-04
-- Description: Complete database schema for Oracle XE 21c
-- Strategy: Oracle-specific implementation (PostgreSQL counterpart in docker/init-scripts/)
-- =============================================================================

-- Connect to pluggable database
ALTER SESSION SET CONTAINER = XEPDB1;

-- Grant privileges to PKD user
GRANT CREATE TABLE TO pkd;
GRANT CREATE VIEW TO pkd;
GRANT CREATE SEQUENCE TO pkd;
GRANT CREATE PROCEDURE TO pkd;
GRANT CREATE TRIGGER TO pkd;
GRANT UNLIMITED TABLESPACE TO pkd;

ALTER USER pkd DEFAULT TABLESPACE USERS;
ALTER USER pkd TEMPORARY TABLESPACE TEMP;

-- Connect as PKD user for schema creation
-- Note: This script should be run as PKD user after initial setup

-- =============================================================================
-- Helper Function: UUID Generation (Oracle equivalent of uuid_generate_v4())
-- =============================================================================
CREATE OR REPLACE FUNCTION uuid_generate_v4 RETURN VARCHAR2 IS
BEGIN
    RETURN LOWER(REGEXP_REPLACE(
        RAWTOHEX(SYS_GUID()),
        '([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})',
        '\1-\2-\3-\4-\5'
    ));
END;
/

-- =============================================================================
-- File Upload Tables
-- =============================================================================

-- Uploaded files tracking (LDIF, Master List, Certificate files, DVL, etc.)
CREATE TABLE uploaded_file (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    file_name VARCHAR2(255) NOT NULL,
    original_file_name VARCHAR2(255),
    file_path VARCHAR2(500),
    file_size NUMBER(19) NOT NULL,
    file_hash VARCHAR2(64) NOT NULL,
    file_format VARCHAR2(20) NOT NULL,
    collection_number VARCHAR2(50),
    status VARCHAR2(30) DEFAULT 'PENDING' NOT NULL,
    processing_mode VARCHAR2(10) DEFAULT 'AUTO' NOT NULL,
    upload_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_timestamp TIMESTAMP,
    error_message CLOB,
    uploaded_by VARCHAR2(100),

    -- Processing statistics
    total_entries NUMBER(10) DEFAULT 0,
    processed_entries NUMBER(10) DEFAULT 0,
    csca_count NUMBER(10) DEFAULT 0,
    dsc_count NUMBER(10) DEFAULT 0,
    dsc_nc_count NUMBER(10) DEFAULT 0,
    crl_count NUMBER(10) DEFAULT 0,
    ml_count NUMBER(10) DEFAULT 0,
    mlsc_count NUMBER(10) DEFAULT 0,
    validation_valid_count NUMBER(10) DEFAULT 0,
    validation_invalid_count NUMBER(10) DEFAULT 0,
    validation_pending_count NUMBER(10) DEFAULT 0,
    validation_error_count NUMBER(10) DEFAULT 0,
    trust_chain_valid_count NUMBER(10) DEFAULT 0,
    trust_chain_invalid_count NUMBER(10) DEFAULT 0,
    csca_not_found_count NUMBER(10) DEFAULT 0,
    expired_count NUMBER(10) DEFAULT 0,
    revoked_count NUMBER(10) DEFAULT 0,
    csca_extracted_from_ml NUMBER(10) DEFAULT 0 NOT NULL,
    csca_duplicates NUMBER(10) DEFAULT 0 NOT NULL,

    CONSTRAINT chk_file_format CHECK (file_format IN ('LDIF', 'ML', 'PEM', 'DER', 'CER', 'BIN', 'DVL', 'MASTER_LIST')),
    CONSTRAINT chk_status CHECK (status IN ('PENDING', 'PROCESSING', 'COMPLETED', 'FAILED')),
    CONSTRAINT chk_processing_mode CHECK (processing_mode IN ('AUTO', 'MANUAL'))
);

CREATE INDEX idx_uploaded_file_status ON uploaded_file(status);
CREATE INDEX idx_uploaded_file_upload_ts ON uploaded_file(upload_timestamp DESC);
CREATE INDEX idx_uploaded_file_file_hash ON uploaded_file(file_hash);

-- =============================================================================
-- Certificate Tables
-- =============================================================================

-- Certificates (CSCA, DSC, DSC_NC, MLSC, LINK_CERT, DVL_SIGNER)
CREATE TABLE certificate (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    upload_id VARCHAR2(36),
    certificate_type VARCHAR2(20) NOT NULL,
    country_code VARCHAR2(3) NOT NULL,
    subject_dn CLOB NOT NULL,
    issuer_dn CLOB NOT NULL,
    serial_number VARCHAR2(100) NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    not_before TIMESTAMP,
    not_after TIMESTAMP,
    certificate_data BLOB NOT NULL,

    -- Validation status
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    validation_message CLOB,
    validated_at TIMESTAMP,

    -- LDAP DN for stored certificate
    ldap_dn CLOB,
    ldap_dn_v2 VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- Duplicate tracking
    duplicate_count NUMBER(10) DEFAULT 0 NOT NULL,
    first_upload_id VARCHAR2(36),
    last_seen_upload_id VARCHAR2(36),
    last_seen_at TIMESTAMP,

    -- X.509 Metadata fields (v2.1.4)
    version NUMBER(3),
    signature_algorithm VARCHAR2(50),
    signature_hash_algorithm VARCHAR2(20),
    public_key_algorithm VARCHAR2(30),
    public_key_size NUMBER(10),
    public_key_curve VARCHAR2(50),
    key_usage VARCHAR2(200),
    extended_key_usage VARCHAR2(500),
    is_ca NUMBER(1),
    path_len_constraint NUMBER(10),
    subject_key_identifier VARCHAR2(100),
    authority_key_identifier VARCHAR2(100),
    crl_distribution_points CLOB,
    ocsp_responder_url VARCHAR2(500),
    is_self_signed NUMBER(1),

    -- Certificate source tracking (v2.4.0)
    source_type VARCHAR2(50) DEFAULT 'FILE_UPLOAD',
    source_context CLOB,
    extracted_from VARCHAR2(100),
    registered_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- PKD Conformance (for DSC_NC)
    pkd_conformance_code VARCHAR2(50),
    pkd_conformance_text CLOB,
    pkd_version VARCHAR2(20),

    CONSTRAINT fk_cert_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT fk_cert_first_upload FOREIGN KEY (first_upload_id) REFERENCES uploaded_file(id),
    CONSTRAINT fk_cert_last_upload FOREIGN KEY (last_seen_upload_id) REFERENCES uploaded_file(id),
    CONSTRAINT chk_cert_type CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC', 'LINK_CERT', 'DVL_SIGNER')),
    CONSTRAINT chk_cert_source_type CHECK (source_type IN ('FILE_UPLOAD', 'PA_EXTRACTED', 'LDIF_PARSED', 'ML_PARSED', 'DVL_PARSED', 'API_REGISTERED', 'SYSTEM_GENERATED'))
);

CREATE UNIQUE INDEX idx_cert_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_cert_type ON certificate(certificate_type);
CREATE INDEX idx_cert_country ON certificate(country_code);
CREATE INDEX idx_cert_upload ON certificate(upload_id);
CREATE INDEX idx_cert_stored_ldap ON certificate(stored_in_ldap);
CREATE INDEX idx_cert_first_upload ON certificate(first_upload_id);
CREATE INDEX idx_cert_source_type ON certificate(source_type);

-- =============================================================================
-- CRL Tables
-- =============================================================================

CREATE TABLE crl (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    upload_id VARCHAR2(36),
    country_code VARCHAR2(3) NOT NULL,
    issuer_dn CLOB NOT NULL,
    this_update TIMESTAMP NOT NULL,
    next_update TIMESTAMP,
    crl_binary BLOB NOT NULL,
    crl_number VARCHAR2(100),
    fingerprint_sha256 VARCHAR2(64) NOT NULL,

    -- LDAP storage
    ldap_dn CLOB,
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_crl_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE
);

CREATE UNIQUE INDEX idx_crl_fingerprint ON crl(fingerprint_sha256);
CREATE INDEX idx_crl_country ON crl(country_code);
CREATE INDEX idx_crl_upload ON crl(upload_id);
CREATE INDEX idx_crl_stored_ldap ON crl(stored_in_ldap);

-- =============================================================================
-- Validation Tables
-- =============================================================================

CREATE TABLE validation_result (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    upload_id VARCHAR2(36),
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    certificate_type VARCHAR2(20) NOT NULL,

    -- Trust chain validation
    trust_chain_valid NUMBER(1) DEFAULT 0,
    trust_chain_path CLOB,
    csca_subject_dn CLOB,

    -- Certificate checks
    signature_verified NUMBER(1) DEFAULT 0,
    is_expired NUMBER(1) DEFAULT 0,
    crl_checked NUMBER(1) DEFAULT 0,
    crl_revoked NUMBER(1) DEFAULT 0,

    -- Validation status
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    error_message CLOB,
    validated_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_validation_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE
);

CREATE INDEX idx_validation_fingerprint ON validation_result(fingerprint_sha256);
CREATE INDEX idx_validation_upload ON validation_result(upload_id);
CREATE INDEX idx_validation_status ON validation_result(validation_status);

-- =============================================================================
-- Duplicate Certificate Tracking (v2.1.2.2)
-- =============================================================================

CREATE TABLE duplicate_certificate (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    upload_id VARCHAR2(36) NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    first_upload_id VARCHAR2(36) NOT NULL,
    certificate_type VARCHAR2(20) NOT NULL,
    country_code VARCHAR2(3) NOT NULL,
    subject_dn CLOB NOT NULL,
    detected_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_dup_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT fk_dup_first_upload FOREIGN KEY (first_upload_id) REFERENCES uploaded_file(id)
);

CREATE INDEX idx_dup_upload ON duplicate_certificate(upload_id);
CREATE INDEX idx_dup_fingerprint ON duplicate_certificate(fingerprint_sha256);
CREATE INDEX idx_dup_first_upload ON duplicate_certificate(first_upload_id);

-- =============================================================================
-- PKD Relay Service Tables (Sync & Reconciliation)
-- =============================================================================

-- Sync status tracking
CREATE TABLE sync_status (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- Database counts
    db_csca_count NUMBER(10) DEFAULT 0,
    db_mlsc_count NUMBER(10) DEFAULT 0,
    db_dsc_count NUMBER(10) DEFAULT 0,
    db_dsc_nc_count NUMBER(10) DEFAULT 0,
    db_crl_count NUMBER(10) DEFAULT 0,

    -- LDAP counts
    ldap_csca_count NUMBER(10) DEFAULT 0,
    ldap_mlsc_count NUMBER(10) DEFAULT 0,
    ldap_dsc_count NUMBER(10) DEFAULT 0,
    ldap_dsc_nc_count NUMBER(10) DEFAULT 0,
    ldap_crl_count NUMBER(10) DEFAULT 0,

    -- Discrepancies
    csca_discrepancy NUMBER(10) DEFAULT 0,
    mlsc_discrepancy NUMBER(10) DEFAULT 0,
    dsc_discrepancy NUMBER(10) DEFAULT 0,
    dsc_nc_discrepancy NUMBER(10) DEFAULT 0,
    crl_discrepancy NUMBER(10) DEFAULT 0,
    total_discrepancy NUMBER(10) DEFAULT 0,

    -- Country-level statistics (JSON format)
    country_stats CLOB
);

CREATE INDEX idx_sync_status_checked_at ON sync_status(checked_at DESC);

-- Sequences for reconciliation tables
CREATE SEQUENCE seq_recon_summary START WITH 1 INCREMENT BY 1;
CREATE SEQUENCE seq_recon_log START WITH 1 INCREMENT BY 1;
CREATE SEQUENCE seq_sync_status START WITH 1 INCREMENT BY 1;

-- Reconciliation summary (aligned with pkd-relay-service code expectations)
CREATE TABLE reconciliation_summary (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    sync_status_id VARCHAR2(36),
    triggered_by VARCHAR2(50),
    status VARCHAR2(30) DEFAULT 'IN_PROGRESS',
    started_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_at TIMESTAMP,
    duration_ms NUMBER(19),

    dry_run NUMBER(1) DEFAULT 0 NOT NULL,

    total_processed NUMBER(10) DEFAULT 0,
    success_count NUMBER(10) DEFAULT 0,
    failed_count NUMBER(10) DEFAULT 0,

    csca_added NUMBER(10) DEFAULT 0,
    dsc_added NUMBER(10) DEFAULT 0,
    dsc_nc_added NUMBER(10) DEFAULT 0,
    crl_added NUMBER(10) DEFAULT 0,
    total_added NUMBER(10) DEFAULT 0,

    csca_deleted NUMBER(10) DEFAULT 0,
    dsc_deleted NUMBER(10) DEFAULT 0,
    dsc_nc_deleted NUMBER(10) DEFAULT 0,
    crl_deleted NUMBER(10) DEFAULT 0,

    error_message CLOB,
    metadata CLOB
);

CREATE INDEX idx_recon_summary_started ON reconciliation_summary(started_at DESC);

-- Reconciliation logs (aligned with pkd-relay-service code expectations)
CREATE TABLE reconciliation_log (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    reconciliation_id VARCHAR2(36) NOT NULL,
    operation VARCHAR2(20) NOT NULL,
    certificate_type VARCHAR2(20),
    cert_type VARCHAR2(20),
    country_code VARCHAR2(3),
    subject_dn CLOB,
    subject VARCHAR2(4000),
    issuer VARCHAR2(4000),
    fingerprint_sha256 VARCHAR2(64),
    cert_fingerprint VARCHAR2(64),
    ldap_dn VARCHAR2(4000),
    status VARCHAR2(30),
    error_message CLOB,
    started_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_at TIMESTAMP,
    duration_ms NUMBER(10),

    CONSTRAINT fk_recon_log_summary FOREIGN KEY (reconciliation_id) REFERENCES reconciliation_summary(id) ON DELETE CASCADE,
    CONSTRAINT chk_recon_operation CHECK (operation IN ('ADD', 'UPDATE', 'DELETE', 'SKIP'))
);

CREATE INDEX idx_recon_log_recon_id ON reconciliation_log(reconciliation_id);
CREATE INDEX idx_recon_log_fingerprint ON reconciliation_log(cert_fingerprint);

-- =============================================================================
-- Authentication & Audit Tables
-- =============================================================================

-- Users table
CREATE TABLE users (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    username VARCHAR2(50) UNIQUE NOT NULL,
    password_hash VARCHAR2(255) NOT NULL,
    email VARCHAR2(100),
    is_admin NUMBER(1) DEFAULT 0,
    is_active NUMBER(1) DEFAULT 1,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    last_login_at TIMESTAMP
);

CREATE INDEX idx_users_username ON users(username);

-- Authentication audit log
CREATE TABLE auth_audit_log (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    username VARCHAR2(50) NOT NULL,
    action VARCHAR2(50) NOT NULL,
    success NUMBER(1) DEFAULT 1,
    ip_address VARCHAR2(50),
    user_agent VARCHAR2(500),
    error_message CLOB,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT chk_auth_action CHECK (action IN ('LOGIN', 'LOGOUT', 'REGISTER', 'PASSWORD_CHANGE', 'TOKEN_REFRESH'))
);

CREATE INDEX idx_auth_audit_username ON auth_audit_log(username);
CREATE INDEX idx_auth_audit_created ON auth_audit_log(created_at DESC);

-- Operation audit log
CREATE TABLE operation_audit_log (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    user_id VARCHAR2(36),
    username VARCHAR2(50) DEFAULT 'anonymous' NOT NULL,
    operation_type VARCHAR2(50) NOT NULL,
    operation_subtype VARCHAR2(50),
    resource_id VARCHAR2(100),
    resource_type VARCHAR2(50),
    request_method VARCHAR2(10),
    request_path VARCHAR2(500),
    ip_address VARCHAR2(50),
    user_agent VARCHAR2(500),
    success NUMBER(1) DEFAULT 1,
    status_code NUMBER(5),
    duration_ms NUMBER(10),
    metadata CLOB,
    error_message CLOB,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT chk_op_type CHECK (operation_type IN ('FILE_UPLOAD', 'CERTIFICATE_SEARCH', 'PA_VERIFY', 'SYNC_CHECK', 'RECONCILIATION', 'USER_MANAGEMENT', 'SYSTEM_CONFIG'))
);

CREATE INDEX idx_op_audit_username ON operation_audit_log(username);
CREATE INDEX idx_op_audit_type ON operation_audit_log(operation_type);
CREATE INDEX idx_op_audit_created ON operation_audit_log(created_at DESC);

-- =============================================================================
-- ICAO PKD Version Monitoring
-- =============================================================================

CREATE TABLE icao_version_history (
    id VARCHAR2(36) DEFAULT uuid_generate_v4() PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    local_version VARCHAR2(20),
    icao_version VARCHAR2(20),
    version_match NUMBER(1) DEFAULT 0,
    icao_response CLOB,
    error_message CLOB
);

CREATE INDEX idx_icao_version_checked ON icao_version_history(checked_at DESC);

-- =============================================================================
-- 14. Revalidation History
-- =============================================================================
CREATE SEQUENCE seq_revalidation_history START WITH 1 INCREMENT BY 1;

CREATE TABLE revalidation_history (
    id NUMBER DEFAULT seq_revalidation_history.NEXTVAL PRIMARY KEY,
    executed_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    total_processed NUMBER DEFAULT 0 NOT NULL,
    newly_expired NUMBER DEFAULT 0 NOT NULL,
    newly_valid NUMBER DEFAULT 0 NOT NULL,
    unchanged NUMBER DEFAULT 0 NOT NULL,
    errors NUMBER DEFAULT 0 NOT NULL,
    duration_ms NUMBER DEFAULT 0 NOT NULL
);

CREATE INDEX idx_reval_history_executed ON revalidation_history(executed_at DESC);

-- =============================================================================
-- PA (Passive Authentication) Tables
-- =============================================================================

CREATE TABLE pa_verification (
    id VARCHAR2(36) DEFAULT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), '([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', '\1-\2-\3-\4-\5')) PRIMARY KEY,
    issuing_country VARCHAR2(3) NOT NULL,
    document_number VARCHAR2(50),
    date_of_birth DATE,
    date_of_expiry DATE,
    sod_binary BLOB,
    sod_hash VARCHAR2(64),
    dsc_subject_dn CLOB,
    dsc_issuer_dn CLOB,
    dsc_serial_number VARCHAR2(100),
    dsc_fingerprint VARCHAR2(64),
    csca_subject_dn CLOB,
    csca_fingerprint VARCHAR2(64),
    verification_status VARCHAR2(30) NOT NULL,
    verification_message CLOB,
    trust_chain_valid NUMBER(1),
    trust_chain_message CLOB,
    sod_signature_valid NUMBER(1),
    sod_signature_message CLOB,
    dg_hashes_valid NUMBER(1),
    dg_hashes_message CLOB,
    crl_status VARCHAR2(20),
    crl_message CLOB,
    request_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_timestamp TIMESTAMP,
    processing_time_ms NUMBER(10),
    client_ip VARCHAR2(45),
    user_agent CLOB,
    CONSTRAINT chk_pa_status CHECK (verification_status IN ('VALID', 'INVALID', 'ERROR', 'PENDING'))
);

CREATE INDEX idx_pa_status ON pa_verification(verification_status);
CREATE INDEX idx_pa_country ON pa_verification(issuing_country);
CREATE INDEX idx_pa_timestamp ON pa_verification(request_timestamp DESC);
CREATE INDEX idx_pa_dsc ON pa_verification(dsc_fingerprint);

CREATE TABLE pa_data_group (
    id VARCHAR2(36) DEFAULT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), '([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', '\1-\2-\3-\4-\5')) PRIMARY KEY,
    verification_id VARCHAR2(36),
    dg_number NUMBER(2) NOT NULL,
    expected_hash VARCHAR2(128) NOT NULL,
    actual_hash VARCHAR2(128),
    hash_algorithm VARCHAR2(20) NOT NULL,
    hash_valid NUMBER(1),
    dg_binary BLOB,
    CONSTRAINT fk_pa_dg_verification FOREIGN KEY (verification_id) REFERENCES pa_verification(id) ON DELETE CASCADE,
    CONSTRAINT chk_dg_number CHECK (dg_number BETWEEN 1 AND 16)
);

CREATE INDEX idx_pa_dg_verification ON pa_data_group(verification_id);

-- =============================================================================
-- Success Message
-- =============================================================================
SELECT 'Oracle PKD schema initialized successfully' AS status FROM dual;
