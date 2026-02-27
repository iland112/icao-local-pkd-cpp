-- =============================================================================
-- Oracle Database Core Schema
-- ICAO Local PKD - Core Tables (Oracle Version)
-- =============================================================================
-- Version: 2.24.0 (Rewritten to match PostgreSQL column names exactly)
-- Updated: 2026-02-27
-- Description: Column names MUST match PostgreSQL schema (01-core-schema.sql)
--              because C++ code expects PostgreSQL column names.
--              OracleQueryExecutor auto-lowercases Oracle UPPERCASE column names.
-- =============================================================================

SET SQLBLANKLINES ON

CONNECT pkd_user/pkd_password@ORCLPDB1;

-- Allow re-runs (skip "already exists" errors)
WHENEVER SQLERROR CONTINUE;

-- =============================================================================
-- Sequences
-- =============================================================================

CREATE SEQUENCE seq_cert_duplicates START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_icao_versions START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_link_certificate START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_lc_issuers START WITH 1 INCREMENT BY 1 NOCACHE;

-- =============================================================================
-- File Upload Tables
-- =============================================================================

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
    error_message VARCHAR2(4000),
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
    valid_period_count NUMBER(10) DEFAULT 0,
    revoked_count NUMBER(10) DEFAULT 0,
    icao_compliant_count NUMBER(10) DEFAULT 0,
    icao_non_compliant_count NUMBER(10) DEFAULT 0,
    icao_warning_count NUMBER(10) DEFAULT 0,
    csca_extracted_from_ml NUMBER(10) DEFAULT 0 NOT NULL,
    csca_duplicates NUMBER(10) DEFAULT 0 NOT NULL
);

CREATE INDEX idx_uploaded_file_status ON uploaded_file(status);
CREATE INDEX idx_uploaded_file_upload_timestamp ON uploaded_file(upload_timestamp);
CREATE INDEX idx_uploaded_file_file_hash ON uploaded_file(file_hash);

-- =============================================================================
-- Certificate Tables (CSCA, DSC, DSC_NC, MLSC)
-- =============================================================================

CREATE TABLE certificate (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    certificate_type VARCHAR2(20) NOT NULL,
    country_code VARCHAR2(3) NOT NULL,
    subject_dn VARCHAR2(4000) NOT NULL,
    issuer_dn VARCHAR2(4000) NOT NULL,
    serial_number VARCHAR2(100) NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    not_before TIMESTAMP,
    not_after TIMESTAMP,
    certificate_data BLOB NOT NULL,

    -- Validation status
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    validation_message VARCHAR2(4000),
    validated_at TIMESTAMP,

    -- LDAP DN for stored certificate
    ldap_dn VARCHAR2(512),
    ldap_dn_v2 VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- Duplicate tracking (v2.0.0+)
    duplicate_count NUMBER(10) DEFAULT 0 NOT NULL,
    first_upload_id VARCHAR2(36),
    last_seen_upload_id VARCHAR2(36),
    last_seen_at TIMESTAMP,

    -- Source tracking (v2.8.0)
    source_type VARCHAR2(50) DEFAULT 'FILE_UPLOAD',
    source_context CLOB,
    extracted_from VARCHAR2(100),
    registered_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- X.509 metadata fields (v2.2.x)
    version NUMBER(5) DEFAULT 2,
    signature_algorithm VARCHAR2(50),
    signature_hash_algorithm VARCHAR2(20),
    public_key_algorithm VARCHAR2(50),
    public_key_size NUMBER(10),
    public_key_curve VARCHAR2(50),
    key_usage VARCHAR2(500),
    extended_key_usage VARCHAR2(500),
    is_ca NUMBER(1) DEFAULT 0,
    path_len_constraint NUMBER(10),
    subject_key_identifier VARCHAR2(128),
    authority_key_identifier VARCHAR2(128),
    crl_distribution_points VARCHAR2(4000),
    ocsp_responder_url VARCHAR2(4000),
    is_self_signed NUMBER(1) DEFAULT 0
);

CREATE INDEX idx_certificate_upload_id ON certificate(upload_id);
CREATE INDEX idx_certificate_type ON certificate(certificate_type);
CREATE INDEX idx_certificate_country ON certificate(country_code);
CREATE INDEX idx_certificate_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_certificate_serial ON certificate(serial_number);
CREATE INDEX idx_certificate_stored_in_ldap ON certificate(stored_in_ldap);
CREATE INDEX idx_certificate_first_upload ON certificate(first_upload_id);
CREATE INDEX idx_certificate_ldap_dn_v2 ON certificate(ldap_dn_v2);
CREATE INDEX idx_certificate_source_type ON certificate(source_type);
CREATE INDEX idx_certificate_extracted_from ON certificate(extracted_from);
CREATE INDEX idx_certificate_signature_algorithm ON certificate(signature_algorithm);
CREATE INDEX idx_certificate_public_key_algorithm ON certificate(public_key_algorithm);
CREATE INDEX idx_certificate_public_key_size ON certificate(public_key_size);

-- Unique constraint: (certificate_type, fingerprint_sha256)
CREATE UNIQUE INDEX idx_certificate_unique ON certificate(certificate_type, fingerprint_sha256);

-- =============================================================================
-- CRL Tables
-- =============================================================================

CREATE TABLE crl (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    country_code VARCHAR2(3) NOT NULL,
    issuer_dn VARCHAR2(4000) NOT NULL,
    this_update TIMESTAMP NOT NULL,
    next_update TIMESTAMP,
    crl_number VARCHAR2(100),
    crl_binary BLOB NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,

    -- Validation
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    signature_valid NUMBER(1),

    -- LDAP storage
    ldap_dn VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT crl_fingerprint_unique UNIQUE (fingerprint_sha256)
);

CREATE INDEX idx_crl_upload_id ON crl(upload_id);
CREATE INDEX idx_crl_country ON crl(country_code);
CREATE INDEX idx_crl_issuer ON crl(issuer_dn);
CREATE INDEX idx_crl_fingerprint ON crl(fingerprint_sha256);

-- Revoked certificates (from CRL)
CREATE TABLE revoked_certificate (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    crl_id VARCHAR2(36) NOT NULL,
    serial_number VARCHAR2(100) NOT NULL,
    revocation_date TIMESTAMP NOT NULL,
    revocation_reason VARCHAR2(50),

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_revoked_cert_crl_id ON revoked_certificate(crl_id);
CREATE INDEX idx_revoked_cert_serial ON revoked_certificate(serial_number);

-- =============================================================================
-- Master List Tables
-- =============================================================================

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
    signer_dn VARCHAR2(4000),
    signer_certificate_id VARCHAR2(36),
    signature_valid NUMBER(1),

    -- Statistics
    csca_certificate_count NUMBER(10) DEFAULT 0,

    -- LDAP storage
    ldap_dn VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_ml_upload_id ON master_list(upload_id);
CREATE INDEX idx_ml_signer_country ON master_list(signer_country);
CREATE INDEX idx_ml_fingerprint ON master_list(fingerprint_sha256);

-- =============================================================================
-- Deviation List Tables
-- =============================================================================

CREATE TABLE deviation_list (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    issuer_country VARCHAR2(3) NOT NULL,
    version NUMBER(10) DEFAULT 0,
    hash_algorithm VARCHAR2(20),
    signing_time TIMESTAMP,
    dl_binary BLOB NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    signer_dn VARCHAR2(4000),
    signer_certificate_id VARCHAR2(36),
    signature_valid NUMBER(1),
    deviation_count NUMBER(10) DEFAULT 0,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT deviation_list_fp_unique UNIQUE (fingerprint_sha256)
);

CREATE INDEX idx_dl_upload_id ON deviation_list(upload_id);
CREATE INDEX idx_dl_issuer_country ON deviation_list(issuer_country);

-- Deviation entries (individual defect records from DL)
CREATE TABLE deviation_entry (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    deviation_list_id VARCHAR2(36) NOT NULL,
    certificate_issuer_dn VARCHAR2(4000),
    certificate_serial_number VARCHAR2(100),
    matched_certificate_id VARCHAR2(36),
    defect_description VARCHAR2(4000),
    defect_type_oid VARCHAR2(50) NOT NULL,
    defect_category VARCHAR2(20),
    defect_parameters BLOB,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_de_dl_id ON deviation_entry(deviation_list_id);
CREATE INDEX idx_de_category ON deviation_entry(defect_category);

-- =============================================================================
-- Validation Result Table
-- =============================================================================

CREATE TABLE validation_result (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    certificate_id VARCHAR2(128),
    upload_id VARCHAR2(36),
    certificate_type VARCHAR2(10) NOT NULL,
    country_code VARCHAR2(3),
    subject_dn VARCHAR2(4000) NOT NULL,
    issuer_dn VARCHAR2(4000) NOT NULL,
    serial_number VARCHAR2(255),

    -- Overall validation status
    validation_status VARCHAR2(20) NOT NULL,

    -- Trust Chain validation
    trust_chain_valid NUMBER(1) DEFAULT 0,
    trust_chain_message VARCHAR2(4000),

    -- CSCA lookup details
    csca_found NUMBER(1) DEFAULT 0,
    csca_subject_dn VARCHAR2(4000),
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

    -- ICAO 9303 compliance (per-certificate)
    icao_compliant NUMBER(1),
    icao_compliance_level VARCHAR2(20),
    icao_violations VARCHAR2(4000),
    icao_key_usage_compliant NUMBER(1),
    icao_algorithm_compliant NUMBER(1),
    icao_key_size_compliant NUMBER(1),
    icao_validity_period_compliant NUMBER(1),
    icao_extensions_compliant NUMBER(1),

    validation_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT uq_validation_cert_upload UNIQUE (certificate_id, upload_id)
);

CREATE INDEX idx_validation_cert ON validation_result(certificate_id);
CREATE INDEX idx_validation_upload ON validation_result(upload_id);
CREATE INDEX idx_validation_status ON validation_result(validation_status);
CREATE INDEX idx_validation_trust_chain ON validation_result(trust_chain_valid);
CREATE INDEX idx_validation_timestamp ON validation_result(validation_timestamp);
CREATE INDEX idx_validation_icao_compliant ON validation_result(icao_compliant);

-- =============================================================================
-- Certificate Duplicate Tracking
-- =============================================================================

CREATE TABLE certificate_duplicates (
    id NUMBER,
    certificate_id VARCHAR2(36) NOT NULL,
    upload_id VARCHAR2(36) NOT NULL,
    source_type VARCHAR2(20) NOT NULL,
    source_country VARCHAR2(3),
    source_entry_dn VARCHAR2(4000),
    source_file_name VARCHAR2(255),
    detected_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

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

-- Unique constraint matching PostgreSQL
ALTER TABLE certificate_duplicates ADD CONSTRAINT uq_cert_dup_source UNIQUE (certificate_id, upload_id, source_type);

-- =============================================================================
-- Duplicate Certificate Table (v2.2.1)
-- =============================================================================

CREATE TABLE duplicate_certificate (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36) NOT NULL,
    first_upload_id VARCHAR2(36),
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    certificate_type VARCHAR2(20) NOT NULL,
    subject_dn VARCHAR2(4000),
    issuer_dn VARCHAR2(4000),
    country_code VARCHAR2(2),
    serial_number VARCHAR2(255),
    duplicate_count NUMBER(10) DEFAULT 1,
    detection_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,

    CONSTRAINT dup_cert_upload_fp_unique UNIQUE (upload_id, fingerprint_sha256, certificate_type)
);

CREATE INDEX idx_dup_cert_upload_id ON duplicate_certificate(upload_id);
CREATE INDEX idx_dup_cert_fingerprint ON duplicate_certificate(fingerprint_sha256);
CREATE INDEX idx_dup_cert_first_upload ON duplicate_certificate(first_upload_id);
CREATE INDEX idx_dup_cert_country ON duplicate_certificate(country_code);
CREATE INDEX idx_dup_cert_type ON duplicate_certificate(certificate_type);

-- =============================================================================
-- Passive Authentication Tables
-- =============================================================================

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
    dsc_subject_dn VARCHAR2(4000),
    dsc_issuer_dn VARCHAR2(4000),
    dsc_serial_number VARCHAR2(100),
    dsc_fingerprint VARCHAR2(64),

    -- CSCA info (looked up from LDAP)
    csca_subject_dn VARCHAR2(4000),
    csca_fingerprint VARCHAR2(64),

    -- Verification result
    verification_status VARCHAR2(30) NOT NULL,
    verification_message VARCHAR2(4000),

    -- Individual check results
    trust_chain_valid NUMBER(1),
    trust_chain_message VARCHAR2(4000),
    sod_signature_valid NUMBER(1),
    sod_signature_message VARCHAR2(4000),
    dg_hashes_valid NUMBER(1),
    dg_hashes_message VARCHAR2(4000),
    crl_status VARCHAR2(20),
    crl_message VARCHAR2(4000),

    -- Timing
    request_timestamp TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_timestamp TIMESTAMP,
    processing_time_ms NUMBER(10),

    -- Request metadata
    client_ip VARCHAR2(45),
    user_agent VARCHAR2(4000),
    requested_by VARCHAR2(100),

    -- DSC conformance (ICAO PKD nc-data)
    dsc_non_conformant NUMBER(1) DEFAULT 0,
    pkd_conformance_code VARCHAR2(100),
    pkd_conformance_text VARCHAR2(500)
);

CREATE INDEX idx_pa_verification_status ON pa_verification(verification_status);
CREATE INDEX idx_pa_verification_country ON pa_verification(issuing_country);
CREATE INDEX idx_pa_verification_timestamp ON pa_verification(request_timestamp);
CREATE INDEX idx_pa_verification_dsc ON pa_verification(dsc_fingerprint);

-- Data Group hashes from PA verification
CREATE TABLE pa_data_group (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    verification_id VARCHAR2(36) NOT NULL,
    dg_number NUMBER(5) NOT NULL,
    expected_hash VARCHAR2(128) NOT NULL,
    actual_hash VARCHAR2(128),
    hash_algorithm VARCHAR2(20) NOT NULL,
    hash_valid NUMBER(1),
    dg_binary BLOB
);

CREATE INDEX idx_pa_dg_verification_id ON pa_data_group(verification_id);

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
    details CLOB
);

CREATE INDEX idx_audit_log_timestamp ON audit_log(event_timestamp);
CREATE INDEX idx_audit_log_event_type ON audit_log(event_type);
CREATE INDEX idx_audit_log_entity ON audit_log(entity_type, entity_id);

-- =============================================================================
-- ICAO PKD Auto Sync (Oracle Version)
-- =============================================================================

CREATE TABLE icao_pkd_versions (
    id VARCHAR2(36) DEFAULT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), '([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', '\1-\2-\3-\4-\5')) PRIMARY KEY,
    collection_type VARCHAR2(20) NOT NULL,
    file_name VARCHAR2(255) NOT NULL,
    file_version VARCHAR2(50) NOT NULL,
    download_url VARCHAR2(2000),
    file_size_mb NUMBER(10, 2),
    detected_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    status VARCHAR2(20) DEFAULT 'DETECTED' NOT NULL,
    notified_at TIMESTAMP,
    notification_sent NUMBER(1) DEFAULT 0,
    notification_sent_at TIMESTAMP,
    downloaded_at TIMESTAMP,
    import_upload_id VARCHAR2(36),
    imported_at TIMESTAMP,
    metadata CLOB,
    certificate_count NUMBER,
    error_message VARCHAR2(2000),
    CONSTRAINT uq_icao_file_name UNIQUE (file_name),
    CONSTRAINT uq_icao_collection_version UNIQUE (collection_type, file_version)
);

CREATE INDEX idx_icao_versions_collection ON icao_pkd_versions(collection_type);
CREATE INDEX idx_icao_versions_file_version ON icao_pkd_versions(file_version);
CREATE INDEX idx_icao_versions_status ON icao_pkd_versions(status);
CREATE INDEX idx_icao_versions_detected_at ON icao_pkd_versions(detected_at);

-- =============================================================================
-- Link Certificate (CSCA Key Transition) (Oracle Version)
-- =============================================================================

CREATE TABLE link_certificate (
    id VARCHAR2(36) DEFAULT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), '([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', '\1-\2-\3-\4-\5')) PRIMARY KEY,
    upload_id VARCHAR2(36),
    country_code VARCHAR2(3) NOT NULL,
    subject_dn CLOB NOT NULL,
    issuer_dn CLOB NOT NULL,
    serial_number VARCHAR2(255) NOT NULL,
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    not_before VARCHAR2(50),
    not_after VARCHAR2(50),
    certificate_data BLOB NOT NULL,
    public_key_algorithm VARCHAR2(50),
    public_key_size NUMBER(10),
    signature_algorithm VARCHAR2(50),
    key_usage CLOB,
    basic_constraints VARCHAR2(500),
    authority_key_identifier VARCHAR2(255),
    subject_key_identifier VARCHAR2(255),
    ldap_dn VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    CONSTRAINT uq_lc_fingerprint UNIQUE (fingerprint_sha256)
);

CREATE INDEX idx_lc_country ON link_certificate(country_code);
CREATE INDEX idx_lc_upload_id ON link_certificate(upload_id);

CREATE TABLE link_certificate_issuers (
    id NUMBER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
    link_cert_id VARCHAR2(36) NOT NULL,
    issuer_csca_id VARCHAR2(36),
    issuer_fingerprint VARCHAR2(64) NOT NULL,
    issuer_subject_dn CLOB NOT NULL,
    issuer_serial_number VARCHAR2(255),
    signature_valid NUMBER(1) DEFAULT 0,
    verified_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    CONSTRAINT uq_lc_issuer UNIQUE (link_cert_id, issuer_csca_id)
);

CREATE INDEX idx_lc_issuers_link_cert ON link_certificate_issuers(link_cert_id);
CREATE INDEX idx_lc_issuers_csca ON link_certificate_issuers(issuer_csca_id);

-- =============================================================================
-- Commit and Exit
-- =============================================================================

COMMIT;

EXIT;
