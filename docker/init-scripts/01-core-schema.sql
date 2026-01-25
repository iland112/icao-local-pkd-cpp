-- =============================================================================
-- ICAO Local PKD - Core Database Schema
-- =============================================================================
-- Version: 2.0.0
-- Created: 2026-01-25
-- Description: Core tables for certificate storage, validation, and duplicates
-- =============================================================================

-- =============================================================================
-- Enable UUID extension
-- =============================================================================
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- =============================================================================
-- File Upload Tables
-- =============================================================================

-- Uploaded files tracking (LDIF, Master List, etc.)
CREATE TABLE IF NOT EXISTS uploaded_file (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    file_name VARCHAR(255) NOT NULL,
    original_file_name VARCHAR(255),
    file_path VARCHAR(500),
    file_size BIGINT NOT NULL,
    file_hash VARCHAR(64) NOT NULL,
    file_format VARCHAR(20) NOT NULL,
    collection_number VARCHAR(50),
    status VARCHAR(30) NOT NULL DEFAULT 'PENDING',
    processing_mode VARCHAR(10) NOT NULL DEFAULT 'AUTO',
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
    validation_valid_count INTEGER DEFAULT 0,
    validation_invalid_count INTEGER DEFAULT 0,
    validation_pending_count INTEGER DEFAULT 0,
    validation_error_count INTEGER DEFAULT 0,
    trust_chain_valid_count INTEGER DEFAULT 0,
    trust_chain_invalid_count INTEGER DEFAULT 0,
    csca_not_found_count INTEGER DEFAULT 0,
    expired_count INTEGER DEFAULT 0,
    revoked_count INTEGER DEFAULT 0,
    csca_extracted_from_ml INTEGER NOT NULL DEFAULT 0,
    csca_duplicates INTEGER NOT NULL DEFAULT 0,

    CONSTRAINT chk_file_format CHECK (file_format IN ('LDIF', 'ML')),
    CONSTRAINT chk_status CHECK (status IN ('PENDING', 'PROCESSING', 'COMPLETED', 'FAILED')),
    CONSTRAINT chk_processing_mode CHECK (processing_mode IN ('AUTO', 'MANUAL'))
);

CREATE INDEX idx_uploaded_file_status ON uploaded_file(status);
CREATE INDEX idx_uploaded_file_upload_timestamp ON uploaded_file(upload_timestamp DESC);
CREATE INDEX idx_uploaded_file_file_hash ON uploaded_file(file_hash);

-- =============================================================================
-- Certificate Tables
-- =============================================================================

-- Certificates (CSCA, DSC, DSC_NC)
CREATE TABLE IF NOT EXISTS certificate (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    certificate_type VARCHAR(20) NOT NULL,
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(100) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,
    not_before TIMESTAMP WITH TIME ZONE,
    not_after TIMESTAMP WITH TIME ZONE,
    certificate_data BYTEA NOT NULL,

    -- Validation status
    validation_status VARCHAR(20) DEFAULT 'PENDING',
    validation_message TEXT,
    validated_at TIMESTAMP WITH TIME ZONE,

    -- LDAP DN for stored certificate
    ldap_dn TEXT,
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    stored_at TIMESTAMP WITH TIME ZONE,

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    -- Duplicate tracking (v2.0.0+)
    duplicate_count INTEGER NOT NULL DEFAULT 0,
    first_upload_id UUID REFERENCES uploaded_file(id),
    last_seen_upload_id UUID REFERENCES uploaded_file(id),
    last_seen_at TIMESTAMP,

    CONSTRAINT chk_certificate_type CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC')),
    CONSTRAINT chk_validation_status CHECK (validation_status IN ('VALID', 'INVALID', 'PENDING', 'EXPIRED', 'REVOKED', 'UNKNOWN'))
);

CREATE INDEX idx_certificate_upload_id ON certificate(upload_id);
CREATE INDEX idx_certificate_type ON certificate(certificate_type);
CREATE INDEX idx_certificate_country ON certificate(country_code);
CREATE INDEX idx_certificate_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX idx_certificate_subject_dn ON certificate(subject_dn);
CREATE INDEX idx_certificate_issuer_dn ON certificate(issuer_dn);
CREATE INDEX idx_certificate_serial ON certificate(serial_number);
CREATE INDEX idx_certificate_stored_in_ldap ON certificate(stored_in_ldap);
CREATE INDEX idx_certificate_first_upload ON certificate(first_upload_id);
CREATE UNIQUE INDEX idx_certificate_unique ON certificate(certificate_type, fingerprint_sha256);

-- =============================================================================
-- CRL Tables
-- =============================================================================

-- Certificate Revocation Lists
CREATE TABLE IF NOT EXISTS crl (
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
CREATE TABLE IF NOT EXISTS revoked_certificate (
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

-- Master Lists (CMS SignedData containing CSCA certificates)
CREATE TABLE IF NOT EXISTS master_list (
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
-- Validation Result Table
-- =============================================================================

-- Detailed validation results for trust chain verification
CREATE TABLE IF NOT EXISTS validation_result (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    certificate_id UUID REFERENCES certificate(id) ON DELETE CASCADE,
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    certificate_type VARCHAR(10) NOT NULL,
    country_code VARCHAR(3),
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(255),

    -- Overall validation status
    validation_status VARCHAR(20) NOT NULL,

    -- Trust Chain validation
    trust_chain_valid BOOLEAN DEFAULT FALSE,
    trust_chain_message TEXT,

    -- CSCA lookup details
    csca_found BOOLEAN DEFAULT FALSE,
    csca_subject_dn TEXT,
    csca_serial_number VARCHAR(255),
    csca_country VARCHAR(3),

    -- Signature validation
    signature_valid BOOLEAN DEFAULT FALSE,
    signature_algorithm VARCHAR(50),

    -- Validity period
    validity_period_valid BOOLEAN DEFAULT FALSE,
    not_before VARCHAR(50),
    not_after VARCHAR(50),

    -- Revocation status
    revocation_status VARCHAR(20) DEFAULT 'UNKNOWN',
    crl_checked BOOLEAN DEFAULT FALSE,
    ocsp_checked BOOLEAN DEFAULT FALSE,

    validation_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    UNIQUE(certificate_id, upload_id)
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
CREATE TABLE IF NOT EXISTS certificate_duplicates (
    id SERIAL PRIMARY KEY,
    certificate_id UUID NOT NULL REFERENCES certificate(id) ON DELETE CASCADE,
    upload_id UUID NOT NULL REFERENCES uploaded_file(id) ON DELETE CASCADE,
    source_type VARCHAR(20) NOT NULL,
    source_country VARCHAR(3),
    source_entry_dn TEXT,
    source_file_name VARCHAR(255),
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(certificate_id, upload_id)
);

CREATE INDEX idx_cert_dup_cert_id ON certificate_duplicates(certificate_id);
CREATE INDEX idx_cert_dup_upload_id ON certificate_duplicates(upload_id);
CREATE INDEX idx_cert_dup_source_type ON certificate_duplicates(source_type);
CREATE INDEX idx_cert_dup_detected_at ON certificate_duplicates(detected_at);

-- =============================================================================
-- Passive Authentication Tables
-- =============================================================================

-- PA Verification Request
CREATE TABLE IF NOT EXISTS pa_verification (
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
    verification_status VARCHAR(30) NOT NULL,
    verification_message TEXT,

    -- Individual check results
    trust_chain_valid BOOLEAN,
    trust_chain_message TEXT,
    sod_signature_valid BOOLEAN,
    sod_signature_message TEXT,
    dg_hashes_valid BOOLEAN,
    dg_hashes_message TEXT,
    crl_status VARCHAR(20),
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
CREATE TABLE IF NOT EXISTS pa_data_group (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    verification_id UUID REFERENCES pa_verification(id) ON DELETE CASCADE,
    dg_number INTEGER NOT NULL,
    expected_hash VARCHAR(128) NOT NULL,
    actual_hash VARCHAR(128),
    hash_algorithm VARCHAR(20) NOT NULL,
    hash_valid BOOLEAN,
    dg_binary BYTEA,

    CONSTRAINT chk_dg_number CHECK (dg_number BETWEEN 1 AND 16)
);

CREATE INDEX idx_pa_dg_verification_id ON pa_data_group(verification_id);

-- =============================================================================
-- Audit Log
-- =============================================================================

CREATE TABLE IF NOT EXISTS audit_log (
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
