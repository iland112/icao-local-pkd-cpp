-- =============================================================================
-- ICAO Local PKD - Advanced Features Schema
-- =============================================================================
-- Version: 2.0.0
-- Created: 2026-01-25
-- Description: ICAO Auto Sync, Link Certificate, and Trust Chain enhancements
-- =============================================================================

-- =============================================================================
-- ICAO PKD Auto Sync
-- =============================================================================

-- ICAO PKD version tracking (detected from public portal)
CREATE TABLE IF NOT EXISTS icao_pkd_versions (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    collection_type VARCHAR(20) NOT NULL,
    file_name VARCHAR(255) NOT NULL UNIQUE,
    file_version VARCHAR(50) NOT NULL,
    download_url TEXT,
    file_size_mb NUMERIC(10, 2),
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status VARCHAR(20) NOT NULL DEFAULT 'DETECTED',
    notified_at TIMESTAMP,
    downloaded_at TIMESTAMP,
    import_upload_id UUID REFERENCES uploaded_file(id) ON DELETE SET NULL,
    imported_at TIMESTAMP,
    metadata JSONB,
    UNIQUE(collection_type, file_version)
);

CREATE INDEX idx_icao_versions_collection ON icao_pkd_versions(collection_type);
CREATE INDEX idx_icao_versions_file_version ON icao_pkd_versions(file_version);
CREATE INDEX idx_icao_versions_status ON icao_pkd_versions(status);
CREATE INDEX idx_icao_versions_detected_at ON icao_pkd_versions(detected_at);

-- =============================================================================
-- Link Certificate (CSCA Key Transition)
-- =============================================================================

-- Link Certificates for CSCA key transitions
CREATE TABLE IF NOT EXISTS link_certificate (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(255) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL UNIQUE,
    not_before VARCHAR(50),
    not_after VARCHAR(50),
    certificate_data BYTEA NOT NULL,
    public_key_algorithm VARCHAR(50),
    public_key_size INTEGER,
    signature_algorithm VARCHAR(50),
    key_usage TEXT[],
    basic_constraints TEXT,
    authority_key_identifier VARCHAR(255),
    subject_key_identifier VARCHAR(255),
    ldap_dn VARCHAR(512),
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_lc_country ON link_certificate(country_code);
CREATE INDEX idx_lc_fingerprint ON link_certificate(fingerprint_sha256);
CREATE INDEX idx_lc_issuer_dn ON link_certificate(issuer_dn);
CREATE INDEX idx_lc_subject_dn ON link_certificate(subject_dn);
CREATE INDEX idx_lc_upload_id ON link_certificate(upload_id);

-- Link Certificate issuers (CSCA old keys)
CREATE TABLE IF NOT EXISTS link_certificate_issuers (
    id SERIAL PRIMARY KEY,
    link_cert_id UUID NOT NULL REFERENCES link_certificate(id) ON DELETE CASCADE,
    issuer_csca_id UUID REFERENCES certificate(id) ON DELETE CASCADE,
    issuer_fingerprint VARCHAR(64) NOT NULL,
    issuer_subject_dn TEXT NOT NULL,
    issuer_serial_number VARCHAR(255),
    signature_valid BOOLEAN DEFAULT FALSE,
    verified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(link_cert_id, issuer_csca_id)
);

CREATE INDEX idx_lc_issuers_link_cert ON link_certificate_issuers(link_cert_id);
CREATE INDEX idx_lc_issuers_csca ON link_certificate_issuers(issuer_csca_id);

-- =============================================================================
-- Trust Chain Validation Enhancements
-- =============================================================================

-- Add trust_chain_path column to validation_result for Link Certificate support
ALTER TABLE validation_result
ADD COLUMN IF NOT EXISTS trust_chain_path JSONB DEFAULT '[]'::jsonb;

COMMENT ON COLUMN validation_result.trust_chain_path IS
'Trust chain path array: [{"type": "DSC|LC|CSCA", "subject": "...", "fingerprint": "..."}]';
