-- =============================================================================
-- ICAO Local PKD - Core Database Schema (Oracle Version)
-- =============================================================================
-- Version: 2.0.0 (Oracle Migration)
-- Created: 2026-02-05
-- Description: Core tables for certificate storage, validation, and duplicates
-- Converted from PostgreSQL to Oracle DDL
-- =============================================================================

-- Connect as PKD_USER
CONNECT pkd_user/pkd_password@XE;

-- =============================================================================
-- File Upload Tables
-- =============================================================================

-- Sequences for auto-increment columns
CREATE SEQUENCE seq_uploaded_file START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_certificate START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_crl START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_revoked_cert START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_master_list START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_validation_result START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_cert_duplicates START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_pa_verification START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_pa_data_group START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_audit_log START WITH 1 INCREMENT BY 1 NOCACHE;

-- Uploaded files tracking (LDIF, Master List, etc.)
CREATE TABLE uploaded_file (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
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
    mlsc_count NUMBER(10) DEFAULT 0,  -- Master List Signer Certificate count
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

    CONSTRAINT chk_file_format CHECK (file_format IN ('LDIF', 'ML')),
    CONSTRAINT chk_status CHECK (status IN ('PENDING', 'PROCESSING', 'COMPLETED', 'FAILED')),
    CONSTRAINT chk_processing_mode CHECK (processing_mode IN ('AUTO', 'MANUAL'))
);

CREATE INDEX idx_uploaded_file_status ON uploaded_file(status);
CREATE INDEX idx_uploaded_file_timestamp ON uploaded_file(upload_timestamp DESC);
CREATE INDEX idx_uploaded_file_file_hash ON uploaded_file(file_hash);

-- =============================================================================
-- Certificate Tables
-- =============================================================================

-- Certificates (CSCA, DSC, DSC_NC, MLSC)
CREATE TABLE certificate (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
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

    CONSTRAINT fk_cert_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT fk_cert_first_upload FOREIGN KEY (first_upload_id) REFERENCES uploaded_file(id),
    CONSTRAINT fk_cert_last_upload FOREIGN KEY (last_seen_upload_id) REFERENCES uploaded_file(id),
    CONSTRAINT chk_cert_type CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC')),
    CONSTRAINT chk_validation_status CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED', 'REVOKED', 'UNKNOWN'))
);

CREATE INDEX idx_cert_upload_id ON certificate(upload_id);
CREATE INDEX idx_cert_type ON certificate(certificate_type);
CREATE INDEX idx_cert_country ON certificate(country_code);
CREATE INDEX idx_cert_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_cert_serial ON certificate(serial_number);
CREATE INDEX idx_cert_stored_ldap ON certificate(stored_in_ldap);
CREATE INDEX idx_cert_first_upload ON certificate(first_upload_id);
CREATE INDEX idx_cert_ldap_dn_v2 ON certificate(ldap_dn_v2);
CREATE UNIQUE INDEX idx_cert_unique ON certificate(certificate_type, fingerprint_sha256);

-- Indexes on CLOB columns (using functional index on substring)
CREATE INDEX idx_cert_subject_dn ON certificate(SUBSTR(subject_dn, 1, 500));
CREATE INDEX idx_cert_issuer_dn ON certificate(SUBSTR(issuer_dn, 1, 500));

-- =============================================================================
-- CRL Tables
-- =============================================================================

-- Certificate Revocation Lists
CREATE TABLE crl (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    country_code VARCHAR2(3) NOT NULL,
    issuer_dn CLOB NOT NULL,
    this_update TIMESTAMP NOT NULL,
    next_update TIMESTAMP,
    crl_number VARCHAR2(100),
    crl_binary BLOB NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL UNIQUE,  -- v2.2.2 FIX

    -- Validation
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    signature_valid NUMBER(1),

    -- LDAP storage
    ldap_dn CLOB,
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_crl_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT chk_crl_validation CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED'))
);

CREATE INDEX idx_crl_upload_id ON crl(upload_id);
CREATE INDEX idx_crl_country ON crl(country_code);
CREATE INDEX idx_crl_issuer ON crl(SUBSTR(issuer_dn, 1, 500));
CREATE INDEX idx_crl_fingerprint ON crl(fingerprint_sha256);

-- Revoked certificates (from CRL)
CREATE TABLE revoked_certificate (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    crl_id VARCHAR2(36),
    serial_number VARCHAR2(100) NOT NULL,
    revocation_date TIMESTAMP NOT NULL,
    revocation_reason VARCHAR2(50),

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_revoked_crl FOREIGN KEY (crl_id) REFERENCES crl(id) ON DELETE CASCADE
);

CREATE INDEX idx_revoked_crl_id ON revoked_certificate(crl_id);
CREATE INDEX idx_revoked_serial ON revoked_certificate(serial_number);

-- =============================================================================
-- Master List Tables
-- =============================================================================

-- Master Lists (CMS SignedData containing CSCA certificates)
CREATE TABLE master_list (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    signer_country VARCHAR2(3) NOT NULL,
    version NUMBER(10),
    issue_date TIMESTAMP,
    next_update TIMESTAMP,
    ml_binary BLOB NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,

    -- Signer certificate info
    signer_dn CLOB,
    signer_certificate_id VARCHAR2(36),
    signature_valid NUMBER(1),

    -- Statistics
    csca_certificate_count NUMBER(10) DEFAULT 0,

    -- LDAP storage
    ldap_dn CLOB,
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_ml_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT fk_ml_signer_cert FOREIGN KEY (signer_certificate_id) REFERENCES certificate(id)
);

CREATE INDEX idx_ml_upload_id ON master_list(upload_id);
CREATE INDEX idx_ml_signer_country ON master_list(signer_country);
CREATE INDEX idx_ml_fingerprint ON master_list(fingerprint_sha256);

-- =============================================================================
-- Validation Result Table
-- =============================================================================

-- Detailed validation results for trust chain verification
CREATE TABLE validation_result (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    certificate_id VARCHAR2(36),
    upload_id VARCHAR2(36),
    certificate_type VARCHAR2(10) NOT NULL,
    country_code VARCHAR2(3),
    subject_dn CLOB NOT NULL,
    issuer_dn CLOB NOT NULL,
    serial_number VARCHAR2(255),

    -- Overall validation status
    validation_status VARCHAR2(20) NOT NULL,

    -- Trust Chain validation
    trust_chain_valid NUMBER(1) DEFAULT 0,
    trust_chain_message CLOB,

    -- CSCA lookup details
    csca_found NUMBER(1) DEFAULT 0,
    csca_subject_dn CLOB,
    csca_serial_number VARCHAR2(255),
    csca_country VARCHAR2(3),

    -- Signature validation
    signature_valid NUMBER(1) DEFAULT 0,
    signature_algorithm VARCHAR2(50),

    -- Validity period
    validity_period_valid NUMBER(1) DEFAULT 0,
    not_before VARCHAR2(50),
    not_after VARCHAR2(50),

    -- Revocation status
    revocation_status VARCHAR2(20) DEFAULT 'UNKNOWN',
    crl_checked NUMBER(1) DEFAULT 0,
    ocsp_checked NUMBER(1) DEFAULT 0,

    validation_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_validation_cert FOREIGN KEY (certificate_id) REFERENCES certificate(id) ON DELETE CASCADE,
    CONSTRAINT fk_validation_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT uk_validation_cert_upload UNIQUE(certificate_id, upload_id)
);

CREATE INDEX idx_validation_cert ON validation_result(certificate_id);
CREATE INDEX idx_validation_upload ON validation_result(upload_id);
CREATE INDEX idx_validation_status ON validation_result(validation_status);
CREATE INDEX idx_validation_trust_chain ON validation_result(trust_chain_valid);
CREATE INDEX idx_validation_timestamp ON validation_result(validation_timestamp);

-- =============================================================================
-- Certificate Duplicate Tracking
-- =============================================================================

-- Track certificate duplicates across multiple sources
CREATE TABLE certificate_duplicates (
    id NUMBER(10) PRIMARY KEY,
    certificate_id VARCHAR2(36) NOT NULL,
    upload_id VARCHAR2(36) NOT NULL,
    source_type VARCHAR2(20) NOT NULL,
    source_country VARCHAR2(3),
    source_entry_dn CLOB,
    source_file_name VARCHAR2(255),
    detected_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT fk_dup_cert FOREIGN KEY (certificate_id) REFERENCES certificate(id) ON DELETE CASCADE,
    CONSTRAINT fk_dup_upload FOREIGN KEY (upload_id) REFERENCES uploaded_file(id) ON DELETE CASCADE,
    CONSTRAINT uk_dup_cert_upload UNIQUE(certificate_id, upload_id, source_type)
);

-- Trigger for auto-increment ID
CREATE OR REPLACE TRIGGER trg_cert_dup_id
BEFORE INSERT ON certificate_duplicates
FOR EACH ROW
WHEN (NEW.id IS NULL)
BEGIN
    SELECT seq_cert_duplicates.NEXTVAL INTO :NEW.id FROM DUAL;
END;
/

CREATE INDEX idx_cert_dup_cert_id ON certificate_duplicates(certificate_id);
CREATE INDEX idx_cert_dup_upload_id ON certificate_duplicates(upload_id);
CREATE INDEX idx_cert_dup_source_type ON certificate_duplicates(source_type);
CREATE INDEX idx_cert_dup_detected_at ON certificate_duplicates(detected_at);

-- =============================================================================
-- Passive Authentication Tables
-- =============================================================================

-- PA Verification Request
CREATE TABLE pa_verification (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,

    -- Request info
    issuing_country VARCHAR2(3) NOT NULL,
    document_number VARCHAR2(50),
    date_of_birth DATE,
    date_of_expiry DATE,

    -- SOD info
    sod_binary BLOB NOT NULL,
    sod_hash VARCHAR2(64) NOT NULL,

    -- DSC info (extracted from SOD)
    dsc_subject_dn CLOB,
    dsc_issuer_dn CLOB,
    dsc_serial_number VARCHAR2(100),
    dsc_fingerprint VARCHAR2(64),

    -- CSCA info (looked up from LDAP)
    csca_subject_dn CLOB,
    csca_fingerprint VARCHAR2(64),

    -- Verification result
    verification_status VARCHAR2(30) NOT NULL,
    verification_message CLOB,

    -- Individual check results
    trust_chain_valid NUMBER(1),
    trust_chain_message CLOB,
    sod_signature_valid NUMBER(1),
    sod_signature_message CLOB,
    dg_hashes_valid NUMBER(1),
    dg_hashes_message CLOB,
    crl_status VARCHAR2(20),
    crl_message CLOB,

    -- Timing
    request_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_timestamp TIMESTAMP,
    processing_time_ms NUMBER(10),

    -- Request metadata
    client_ip VARCHAR2(45),
    user_agent CLOB,

    CONSTRAINT chk_pa_status CHECK (verification_status IN ('VALID', 'INVALID', 'ERROR', 'PENDING'))
);

CREATE INDEX idx_pa_status ON pa_verification(verification_status);
CREATE INDEX idx_pa_country ON pa_verification(issuing_country);
CREATE INDEX idx_pa_timestamp ON pa_verification(request_timestamp DESC);
CREATE INDEX idx_pa_dsc ON pa_verification(dsc_fingerprint);

-- Data Group hashes from PA verification
CREATE TABLE pa_data_group (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
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
-- Audit Log
-- =============================================================================

CREATE TABLE audit_log (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    event_type VARCHAR2(50) NOT NULL,
    event_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,
    entity_type VARCHAR2(50),
    entity_id VARCHAR2(36),
    user_id VARCHAR2(100),
    client_ip VARCHAR2(45),
    details CLOB,  -- JSON stored as CLOB (Oracle 12c+ supports JSON validation)

    CONSTRAINT chk_event_type CHECK (event_type IN (
        'FILE_UPLOADED', 'FILE_PROCESSED', 'FILE_FAILED',
        'CERTIFICATE_STORED', 'CERTIFICATE_VALIDATED',
        'CRL_STORED', 'CRL_VALIDATED',
        'PA_VERIFICATION_REQUESTED', 'PA_VERIFICATION_COMPLETED',
        'LDAP_ENTRY_CREATED', 'LDAP_ENTRY_UPDATED', 'LDAP_ENTRY_DELETED'
    ))
);

CREATE INDEX idx_audit_timestamp ON audit_log(event_timestamp DESC);
CREATE INDEX idx_audit_event_type ON audit_log(event_type);
CREATE INDEX idx_audit_entity_type ON audit_log(entity_type);
CREATE INDEX idx_audit_entity_id ON audit_log(entity_id);

-- =============================================================================
-- Triggers for updated_at timestamps
-- =============================================================================

-- Note: Oracle doesn't have automatic updated_at columns like PostgreSQL
-- If needed, create triggers for each table that needs auto-update timestamps

-- =============================================================================
-- Commit changes
-- =============================================================================

COMMIT;

-- Display completion message
BEGIN
    DBMS_OUTPUT.PUT_LINE('=============================================================================');
    DBMS_OUTPUT.PUT_LINE('Core schema created successfully');
    DBMS_OUTPUT.PUT_LINE('Tables: 12 (uploaded_file, certificate, crl, revoked_certificate,');
    DBMS_OUTPUT.PUT_LINE('        master_list, validation_result, certificate_duplicates,');
    DBMS_OUTPUT.PUT_LINE('        pa_verification, pa_data_group, audit_log)');
    DBMS_OUTPUT.PUT_LINE('Sequences: 10');
    DBMS_OUTPUT.PUT_LINE('Indexes: 50+');
    DBMS_OUTPUT.PUT_LINE('=============================================================================');
END;
/

EXIT;
