-- =============================================================================
-- ICAO Local PKD - Database Schema
-- =============================================================================
-- Version: 1.0
-- Created: 2025-12-29
-- =============================================================================

-- Enable UUID extension
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- =============================================================================
-- File Upload Tables
-- =============================================================================

-- Uploaded File (Aggregate Root)
CREATE TABLE uploaded_file (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    file_name VARCHAR(255) NOT NULL,
    original_file_name VARCHAR(255),
    file_path VARCHAR(500),
    file_hash VARCHAR(64) NOT NULL,  -- SHA-256
    file_size BIGINT NOT NULL,
    file_format VARCHAR(20) NOT NULL,  -- LDIF, ML (Master List)
    collection_number VARCHAR(50),
    status VARCHAR(30) NOT NULL DEFAULT 'PENDING',  -- PENDING, PROCESSING, COMPLETED, FAILED
    upload_timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    completed_timestamp TIMESTAMP WITH TIME ZONE,
    error_message TEXT,
    uploaded_by VARCHAR(100),

    -- Processing statistics
    total_entries INTEGER DEFAULT 0,
    processed_entries INTEGER DEFAULT 0,
    csca_count INTEGER DEFAULT 0,
    dsc_count INTEGER DEFAULT 0,
    dsc_nc_count INTEGER DEFAULT 0,
    crl_count INTEGER DEFAULT 0,
    ml_count INTEGER DEFAULT 0,

    CONSTRAINT chk_file_format CHECK (file_format IN ('LDIF', 'ML')),
    CONSTRAINT chk_status CHECK (status IN ('PENDING', 'PROCESSING', 'COMPLETED', 'FAILED'))
);

-- Index for common queries
CREATE INDEX idx_uploaded_file_status ON uploaded_file(status);
CREATE INDEX idx_uploaded_file_upload_timestamp ON uploaded_file(upload_timestamp DESC);
CREATE INDEX idx_uploaded_file_file_hash ON uploaded_file(file_hash);

-- =============================================================================
-- Certificate Tables
-- =============================================================================

-- Certificate (parsed from LDIF/ML)
CREATE TABLE certificate (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    certificate_type VARCHAR(20) NOT NULL,  -- CSCA, DSC, DSC_NC
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(100) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,
    not_before TIMESTAMP WITH TIME ZONE,
    not_after TIMESTAMP WITH TIME ZONE,
    certificate_binary BYTEA NOT NULL,

    -- Validation status
    validation_status VARCHAR(20) DEFAULT 'PENDING',  -- VALID, INVALID, PENDING, EXPIRED, REVOKED
    validation_message TEXT,
    validated_at TIMESTAMP WITH TIME ZONE,

    -- LDAP DN for stored certificate
    ldap_dn TEXT,
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    stored_at TIMESTAMP WITH TIME ZONE,

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    CONSTRAINT chk_certificate_type CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC')),
    CONSTRAINT chk_validation_status CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED', 'REVOKED'))
);

-- Indexes for certificate lookups
CREATE INDEX idx_certificate_upload_id ON certificate(upload_id);
CREATE INDEX idx_certificate_type ON certificate(certificate_type);
CREATE INDEX idx_certificate_country ON certificate(country_code);
CREATE INDEX idx_certificate_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_certificate_subject_dn ON certificate(subject_dn);
CREATE INDEX idx_certificate_issuer_dn ON certificate(issuer_dn);
CREATE INDEX idx_certificate_serial ON certificate(serial_number);
CREATE UNIQUE INDEX idx_certificate_unique ON certificate(certificate_type, fingerprint_sha256);

-- =============================================================================
-- CRL Tables
-- =============================================================================

-- Certificate Revocation List
CREATE TABLE crl (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    country_code VARCHAR(3) NOT NULL,
    issuer_dn TEXT NOT NULL,
    this_update TIMESTAMP WITH TIME ZONE NOT NULL,
    next_update TIMESTAMP WITH TIME ZONE,
    crl_number VARCHAR(100),
    crl_binary BYTEA NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,

    -- Validation
    validation_status VARCHAR(20) DEFAULT 'PENDING',
    signature_valid BOOLEAN,

    -- LDAP storage
    ldap_dn TEXT,
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    stored_at TIMESTAMP WITH TIME ZONE,

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    CONSTRAINT chk_crl_validation CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED'))
);

CREATE INDEX idx_crl_upload_id ON crl(upload_id);
CREATE INDEX idx_crl_country ON crl(country_code);
CREATE INDEX idx_crl_issuer ON crl(issuer_dn);
CREATE INDEX idx_crl_fingerprint ON crl(fingerprint_sha256);

-- Revoked certificates (from CRL)
CREATE TABLE revoked_certificate (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    crl_id UUID REFERENCES crl(id) ON DELETE CASCADE,
    serial_number VARCHAR(100) NOT NULL,
    revocation_date TIMESTAMP WITH TIME ZONE NOT NULL,
    revocation_reason VARCHAR(50),

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

CREATE INDEX idx_revoked_cert_crl_id ON revoked_certificate(crl_id);
CREATE INDEX idx_revoked_cert_serial ON revoked_certificate(serial_number);

-- =============================================================================
-- Master List Tables
-- =============================================================================

-- Master List (CMS SignedData containing CSCA certificates)
CREATE TABLE master_list (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    signer_country VARCHAR(3) NOT NULL,
    version INTEGER,
    issue_date TIMESTAMP WITH TIME ZONE,
    next_update TIMESTAMP WITH TIME ZONE,
    ml_binary BYTEA NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,

    -- Signer certificate info
    signer_dn TEXT,
    signer_certificate_id UUID REFERENCES certificate(id),
    signature_valid BOOLEAN,

    -- Statistics
    csca_certificate_count INTEGER DEFAULT 0,

    -- LDAP storage
    ldap_dn TEXT,
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    stored_at TIMESTAMP WITH TIME ZONE,

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

CREATE INDEX idx_ml_upload_id ON master_list(upload_id);
CREATE INDEX idx_ml_signer_country ON master_list(signer_country);
CREATE INDEX idx_ml_fingerprint ON master_list(fingerprint_sha256);

-- =============================================================================
-- Passive Authentication Tables
-- =============================================================================

-- PA Verification Request
CREATE TABLE pa_verification (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),

    -- Request info
    issuing_country VARCHAR(3) NOT NULL,
    document_number VARCHAR(50),
    date_of_birth DATE,
    date_of_expiry DATE,

    -- SOD info
    sod_binary BYTEA NOT NULL,
    sod_hash VARCHAR(64) NOT NULL,

    -- DSC info (extracted from SOD)
    dsc_subject_dn TEXT,
    dsc_issuer_dn TEXT,
    dsc_serial_number VARCHAR(100),
    dsc_fingerprint VARCHAR(64),

    -- CSCA info (looked up from LDAP)
    csca_subject_dn TEXT,
    csca_fingerprint VARCHAR(64),

    -- Verification result
    verification_status VARCHAR(30) NOT NULL,  -- VALID, INVALID, ERROR
    verification_message TEXT,

    -- Individual check results
    trust_chain_valid BOOLEAN,
    trust_chain_message TEXT,
    sod_signature_valid BOOLEAN,
    sod_signature_message TEXT,
    dg_hashes_valid BOOLEAN,
    dg_hashes_message TEXT,
    crl_status VARCHAR(20),  -- VALID, REVOKED, CRL_UNAVAILABLE, CRL_EXPIRED
    crl_message TEXT,

    -- Timing
    request_timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    completed_timestamp TIMESTAMP WITH TIME ZONE,
    processing_time_ms INTEGER,

    -- Request metadata
    client_ip VARCHAR(45),
    user_agent TEXT,

    CONSTRAINT chk_verification_status CHECK (verification_status IN ('VALID', 'INVALID', 'ERROR', 'PENDING'))
);

CREATE INDEX idx_pa_verification_status ON pa_verification(verification_status);
CREATE INDEX idx_pa_verification_country ON pa_verification(issuing_country);
CREATE INDEX idx_pa_verification_timestamp ON pa_verification(request_timestamp DESC);
CREATE INDEX idx_pa_verification_dsc ON pa_verification(dsc_fingerprint);

-- Data Group hashes from PA verification
CREATE TABLE pa_data_group (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    verification_id UUID REFERENCES pa_verification(id) ON DELETE CASCADE,
    dg_number INTEGER NOT NULL,  -- 1, 2, 3, ...
    expected_hash VARCHAR(128) NOT NULL,  -- From SOD
    actual_hash VARCHAR(128),  -- Calculated from DG
    hash_algorithm VARCHAR(20) NOT NULL,  -- SHA-256, SHA-384, SHA-512
    hash_valid BOOLEAN,
    dg_binary BYTEA,  -- Optional: store DG content

    CONSTRAINT chk_dg_number CHECK (dg_number BETWEEN 1 AND 16)
);

CREATE INDEX idx_pa_dg_verification_id ON pa_data_group(verification_id);

-- =============================================================================
-- Audit Log
-- =============================================================================

CREATE TABLE audit_log (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    event_type VARCHAR(50) NOT NULL,
    event_timestamp TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    entity_type VARCHAR(50),
    entity_id UUID,
    user_id VARCHAR(100),
    client_ip VARCHAR(45),
    details JSONB,

    CONSTRAINT chk_event_type CHECK (event_type IN (
        'FILE_UPLOADED', 'FILE_PROCESSED', 'FILE_FAILED',
        'CERTIFICATE_STORED', 'CERTIFICATE_VALIDATED',
        'CRL_STORED', 'CRL_VALIDATED',
        'PA_VERIFICATION_REQUESTED', 'PA_VERIFICATION_COMPLETED',
        'LDAP_ENTRY_CREATED', 'LDAP_ENTRY_UPDATED', 'LDAP_ENTRY_DELETED'
    ))
);

CREATE INDEX idx_audit_log_timestamp ON audit_log(event_timestamp DESC);
CREATE INDEX idx_audit_log_event_type ON audit_log(event_type);
CREATE INDEX idx_audit_log_entity ON audit_log(entity_type, entity_id);

-- =============================================================================
-- Views for Statistics
-- =============================================================================

-- Upload statistics view
CREATE VIEW v_upload_statistics AS
SELECT
    file_format,
    status,
    COUNT(*) as count,
    SUM(file_size) as total_size,
    SUM(csca_count) as total_csca,
    SUM(dsc_count) as total_dsc,
    SUM(dsc_nc_count) as total_dsc_nc,
    SUM(crl_count) as total_crl,
    SUM(ml_count) as total_ml
FROM uploaded_file
GROUP BY file_format, status;

-- Certificate statistics by country
CREATE VIEW v_certificate_by_country AS
SELECT
    country_code,
    certificate_type,
    COUNT(*) as count,
    SUM(CASE WHEN validation_status = 'VALID' THEN 1 ELSE 0 END) as valid_count,
    SUM(CASE WHEN validation_status = 'INVALID' THEN 1 ELSE 0 END) as invalid_count,
    SUM(CASE WHEN validation_status = 'EXPIRED' THEN 1 ELSE 0 END) as expired_count,
    SUM(CASE WHEN validation_status = 'REVOKED' THEN 1 ELSE 0 END) as revoked_count
FROM certificate
GROUP BY country_code, certificate_type;

-- PA verification statistics
CREATE VIEW v_pa_statistics AS
SELECT
    issuing_country,
    verification_status,
    COUNT(*) as count,
    AVG(processing_time_ms) as avg_processing_time_ms,
    DATE_TRUNC('day', request_timestamp) as date
FROM pa_verification
GROUP BY issuing_country, verification_status, DATE_TRUNC('day', request_timestamp);

-- =============================================================================
-- Initial Data
-- =============================================================================

-- No initial data needed; schema only
