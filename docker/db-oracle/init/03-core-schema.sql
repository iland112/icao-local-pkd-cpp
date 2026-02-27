-- =============================================================================
-- Oracle Database Core Schema
-- ICAO Local PKD - Core Tables (Oracle Version)
-- =============================================================================
-- Execution: sqlplus / as sysdba @03-core-schema.sql
-- Requires: 01-create-user.sql (PKD_USER must exist)
-- =============================================================================

-- Note: Oracle startup scripts run as SYS, so we need to connect as PKD_USER
-- to create tables in the correct schema.

SET SQLBLANKLINES ON

CONNECT pkd_user/pkd_password@XEPDB1;

-- Allow re-runs (skip "already exists" errors)
WHENEVER SQLERROR CONTINUE;

-- =============================================================================
-- Sequences (Oracle doesn't have SERIAL, use SEQUENCE + DEFAULT SYS_GUID())
-- =============================================================================

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
CREATE SEQUENCE seq_icao_versions START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_link_certificate START WITH 1 INCREMENT BY 1 NOCACHE;
CREATE SEQUENCE seq_lc_issuers START WITH 1 INCREMENT BY 1 NOCACHE;

-- =============================================================================
-- Uploaded File (file upload tracking)
-- =============================================================================

CREATE TABLE uploaded_file (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    file_name VARCHAR2(255) NOT NULL,
    file_type VARCHAR2(50) NOT NULL,
    file_size NUMBER(19),
    file_hash VARCHAR2(64),
    upload_mode VARCHAR2(20) DEFAULT 'AUTO',
    status VARCHAR2(20) DEFAULT 'PENDING',
    processing_stage VARCHAR2(50),
    total_entries NUMBER(10) DEFAULT 0,
    processed_entries NUMBER(10) DEFAULT 0,
    success_count NUMBER(10) DEFAULT 0,
    error_count NUMBER(10) DEFAULT 0,
    duplicate_count NUMBER(10) DEFAULT 0,
    csca_count NUMBER(10) DEFAULT 0,
    dsc_count NUMBER(10) DEFAULT 0,
    dsc_nc_count NUMBER(10) DEFAULT 0,
    crl_count NUMBER(10) DEFAULT 0,
    ml_count NUMBER(10) DEFAULT 0,
    mlsc_count NUMBER(10) DEFAULT 0,
    link_cert_count NUMBER(10) DEFAULT 0,
    valid_count NUMBER(10) DEFAULT 0,
    invalid_count NUMBER(10) DEFAULT 0,
    expired_count NUMBER(10) DEFAULT 0,
    valid_period_count NUMBER(10) DEFAULT 0,
    icao_compliant_count NUMBER(10) DEFAULT 0,
    icao_non_compliant_count NUMBER(10) DEFAULT 0,
    icao_warning_count NUMBER(10) DEFAULT 0,
    processing_errors CLOB,
    error_message VARCHAR2(2000),
    uploaded_by VARCHAR2(100),
    uploaded_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    completed_at TIMESTAMP
);

CREATE INDEX idx_uploaded_file_status ON uploaded_file(status);
CREATE INDEX idx_uploaded_file_timestamp ON uploaded_file(uploaded_at);
CREATE INDEX idx_uploaded_file_file_hash ON uploaded_file(file_hash);

-- =============================================================================
-- Certificate (CSCA, DSC, DSC_NC, MLSC)
-- =============================================================================

CREATE TABLE certificate (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    first_upload_id VARCHAR2(36),
    certificate_type VARCHAR2(20) NOT NULL,
    country_code VARCHAR2(3),
    subject_dn CLOB,
    issuer_dn CLOB,
    serial_number VARCHAR2(255),
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    not_before VARCHAR2(50),
    not_after VARCHAR2(50),
    certificate_data BLOB,
    public_key_algorithm VARCHAR2(50),
    public_key_size NUMBER(10),
    signature_algorithm VARCHAR2(50),
    key_usage CLOB,
    basic_constraints VARCHAR2(500),
    authority_key_identifier VARCHAR2(255),
    subject_key_identifier VARCHAR2(255),
    is_self_signed NUMBER(1) DEFAULT 0,
    validation_status VARCHAR2(20) DEFAULT 'PENDING',
    validation_message CLOB,
    source_type VARCHAR2(50),
    ldap_dn VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    CONSTRAINT uq_cert_fingerprint UNIQUE (fingerprint_sha256)
);

CREATE INDEX idx_cert_upload_id ON certificate(upload_id);
CREATE INDEX idx_cert_type ON certificate(certificate_type);
CREATE INDEX idx_cert_country ON certificate(country_code);
CREATE INDEX idx_cert_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_cert_serial ON certificate(serial_number);
CREATE INDEX idx_cert_stored_ldap ON certificate(stored_in_ldap);
CREATE INDEX idx_cert_first_upload ON certificate(first_upload_id);
CREATE INDEX idx_cert_ldap_dn_v2 ON certificate(ldap_dn);

-- =============================================================================
-- CRL (Certificate Revocation List)
-- =============================================================================

CREATE TABLE crl (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    country_code VARCHAR2(3),
    issuer_dn CLOB,
    this_update VARCHAR2(50),
    next_update VARCHAR2(50),
    crl_number VARCHAR2(100),
    crl_binary BLOB,
    fingerprint_sha256 VARCHAR2(64),
    signature_algorithm VARCHAR2(50),
    revoked_count NUMBER(10) DEFAULT 0,
    ldap_dn VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_crl_upload_id ON crl(upload_id);
CREATE INDEX idx_crl_country ON crl(country_code);

-- =============================================================================
-- Revoked Certificate (from CRL parsing)
-- =============================================================================

CREATE TABLE revoked_certificate (
    id NUMBER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
    crl_id VARCHAR2(36) NOT NULL,
    serial_number VARCHAR2(255) NOT NULL,
    revocation_date VARCHAR2(50),
    revocation_reason VARCHAR2(50)
);

CREATE INDEX idx_revoked_crl_id ON revoked_certificate(crl_id);
CREATE INDEX idx_revoked_serial ON revoked_certificate(serial_number);

-- =============================================================================
-- Master List
-- =============================================================================

CREATE TABLE master_list (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    signer_country VARCHAR2(3),
    signer_dn CLOB,
    signer_fingerprint VARCHAR2(64),
    total_certificates NUMBER(10) DEFAULT 0,
    csca_count NUMBER(10) DEFAULT 0,
    ml_signer_count NUMBER(10) DEFAULT 0,
    content_type VARCHAR2(100),
    signing_time VARCHAR2(50),
    cms_version NUMBER(5),
    digest_algorithm VARCHAR2(50),
    signature_algorithm VARCHAR2(50),
    raw_data BLOB,
    fingerprint_sha256 VARCHAR2(64),
    ldap_dn VARCHAR2(512),
    stored_in_ldap NUMBER(1) DEFAULT 0,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_ml_upload_id ON master_list(upload_id);
CREATE INDEX idx_ml_signer_country ON master_list(signer_country);
CREATE INDEX idx_ml_fingerprint ON master_list(fingerprint_sha256);

-- =============================================================================
-- Deviation List
-- =============================================================================

CREATE TABLE deviation_list (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    upload_id VARCHAR2(36),
    issuer_country VARCHAR2(3),
    issuer_dn CLOB,
    content_type VARCHAR2(100),
    hash_algorithm VARCHAR2(50),
    digest_algorithm VARCHAR2(50),
    signature_algorithm VARCHAR2(50),
    e_content_type VARCHAR2(100),
    total_entries NUMBER(10) DEFAULT 0,
    raw_data BLOB,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_dl_upload_id ON deviation_list(upload_id);
CREATE INDEX idx_dl_issuer_country ON deviation_list(issuer_country);

-- =============================================================================
-- Deviation Entry
-- =============================================================================

CREATE TABLE deviation_entry (
    id NUMBER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
    deviation_list_id VARCHAR2(36) NOT NULL,
    category VARCHAR2(50),
    description CLOB,
    severity VARCHAR2(20),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_de_dl_id ON deviation_entry(deviation_list_id);
CREATE INDEX idx_de_category ON deviation_entry(category);

-- =============================================================================
-- Validation Result
-- =============================================================================

CREATE TABLE validation_result (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    certificate_id VARCHAR2(128),
    upload_id VARCHAR2(36),
    validation_status VARCHAR2(20),
    trust_chain_valid NUMBER(1),
    trust_chain_message CLOB,
    csca_found NUMBER(1),
    csca_fingerprint VARCHAR2(64),
    csca_subject_dn CLOB,
    signature_valid NUMBER(1),
    signature_algorithm VARCHAR2(50),
    validity_period_valid NUMBER(1),
    not_before_date VARCHAR2(50),
    not_after_date VARCHAR2(50),
    revocation_status VARCHAR2(20),
    crl_checked NUMBER(1),
    crl_check_date VARCHAR2(50),
    has_unknown_critical_extensions NUMBER(1),
    unknown_extensions CLOB,
    key_usage_valid NUMBER(1),
    key_usage_warnings CLOB,
    icao_compliant NUMBER(1),
    icao_compliance_level VARCHAR2(20),
    icao_violations CLOB,
    icao_key_usage_compliant NUMBER(1),
    icao_algorithm_compliant NUMBER(1),
    icao_key_size_compliant NUMBER(1),
    icao_validity_period_compliant NUMBER(1),
    icao_extensions_compliant NUMBER(1),
    validated_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_validation_cert ON validation_result(certificate_id);
CREATE INDEX idx_validation_upload ON validation_result(upload_id);
CREATE INDEX idx_validation_status ON validation_result(validation_status);
CREATE INDEX idx_validation_trust_chain ON validation_result(trust_chain_valid);
CREATE INDEX idx_validation_timestamp ON validation_result(validated_at);
CREATE INDEX idx_validation_icao ON validation_result(icao_compliant);

-- =============================================================================
-- Certificate Duplicates
-- =============================================================================

CREATE TABLE certificate_duplicates (
    id NUMBER,
    certificate_id VARCHAR2(36),
    upload_id VARCHAR2(36),
    fingerprint_sha256 VARCHAR2(64) NOT NULL,
    certificate_type VARCHAR2(20),
    country_code VARCHAR2(3),
    subject_dn CLOB,
    source_type VARCHAR2(50),
    duplicate_type VARCHAR2(30),
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

-- =============================================================================
-- PA Verification
-- =============================================================================

CREATE TABLE pa_verification (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    verification_status VARCHAR2(20) NOT NULL,
    overall_result VARCHAR2(20),
    dsc_country_code VARCHAR2(3),
    dsc_subject_dn CLOB,
    dsc_issuer_dn CLOB,
    dsc_serial_number VARCHAR2(255),
    dsc_fingerprint VARCHAR2(64),
    csca_subject_dn CLOB,
    csca_fingerprint VARCHAR2(64),
    sod_hash_algorithm VARCHAR2(50),
    sod_signature_algorithm VARCHAR2(50),
    sod_binary BLOB,
    sod_hash VARCHAR2(128),
    trust_chain_valid NUMBER(1),
    trust_chain_message CLOB,
    sod_signature_valid NUMBER(1),
    sod_signature_message CLOB,
    dg_hash_valid NUMBER(1),
    dg_hash_message CLOB,
    crl_status VARCHAR2(20),
    crl_message CLOB,
    dsc_non_conformant NUMBER(1) DEFAULT 0,
    pkd_conformance_code VARCHAR2(50),
    pkd_conformance_text VARCHAR2(500),
    verification_message CLOB,
    requested_by VARCHAR2(100),
    client_ip VARCHAR2(45),
    user_agent VARCHAR2(500),
    processing_duration_ms NUMBER(10),
    verified_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_pa_status ON pa_verification(verification_status);
CREATE INDEX idx_pa_country ON pa_verification(dsc_country_code);
CREATE INDEX idx_pa_timestamp ON pa_verification(verified_at);
CREATE INDEX idx_pa_dsc ON pa_verification(dsc_fingerprint);

-- =============================================================================
-- PA Data Group
-- =============================================================================

CREATE TABLE pa_data_group (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
    verification_id VARCHAR2(36) NOT NULL,
    dg_number NUMBER(5) NOT NULL,
    dg_hash VARCHAR2(128),
    dg_data BLOB,
    hash_valid NUMBER(1),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_pa_dg_verification ON pa_data_group(verification_id);

-- =============================================================================
-- Audit Log
-- =============================================================================

CREATE TABLE audit_log (
    id NUMBER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
    event_type VARCHAR2(50) NOT NULL,
    entity_type VARCHAR2(50),
    entity_id VARCHAR2(100),
    description CLOB,
    user_id VARCHAR2(100),
    ip_address VARCHAR2(45),
    metadata CLOB,
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE INDEX idx_audit_timestamp ON audit_log(created_at);
CREATE INDEX idx_audit_event_type ON audit_log(event_type);
CREATE INDEX idx_audit_entity_type ON audit_log(entity_type);
CREATE INDEX idx_audit_entity_id ON audit_log(entity_id);

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
