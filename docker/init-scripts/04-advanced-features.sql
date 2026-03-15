-- =============================================================================
-- ICAO Local PKD - Advanced Features Schema
-- =============================================================================
-- Version: 2.34.0
-- Created: 2026-01-25
-- Updated: 2026-03-15
-- Description: ICAO Auto Sync, Link Certificate, and LDAP DN migration
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
    detected_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    status VARCHAR(20) NOT NULL DEFAULT 'DETECTED',
    notified_at TIMESTAMP WITH TIME ZONE,
    notification_sent BOOLEAN DEFAULT FALSE,
    notification_sent_at TIMESTAMP WITH TIME ZONE,
    downloaded_at TIMESTAMP WITH TIME ZONE,
    import_upload_id UUID REFERENCES uploaded_file(id) ON DELETE SET NULL,
    imported_at TIMESTAMP WITH TIME ZONE,
    certificate_count INTEGER,
    error_message TEXT,
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
    ldap_dn_v2 VARCHAR(512),
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_lc_country ON link_certificate(country_code);
CREATE INDEX idx_lc_fingerprint ON link_certificate(fingerprint_sha256);
CREATE INDEX idx_lc_issuer_dn ON link_certificate(issuer_dn);
CREATE INDEX idx_lc_subject_dn ON link_certificate(subject_dn);
CREATE INDEX idx_lc_upload_id ON link_certificate(upload_id);
CREATE INDEX idx_lc_ldap_dn_v2 ON link_certificate(ldap_dn_v2);

-- Link Certificate issuers (CSCA old keys)
CREATE TABLE IF NOT EXISTS link_certificate_issuers (
    id SERIAL PRIMARY KEY,
    link_cert_id UUID NOT NULL REFERENCES link_certificate(id) ON DELETE CASCADE,
    issuer_csca_id UUID REFERENCES certificate(id) ON DELETE CASCADE,
    issuer_fingerprint VARCHAR(64) NOT NULL,
    issuer_subject_dn TEXT NOT NULL,
    issuer_serial_number VARCHAR(255),
    signature_valid BOOLEAN DEFAULT FALSE,
    verified_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(link_cert_id, issuer_csca_id)
);

CREATE INDEX idx_lc_issuers_link_cert ON link_certificate_issuers(link_cert_id);
CREATE INDEX idx_lc_issuers_csca ON link_certificate_issuers(issuer_csca_id);

-- =============================================================================
-- LDAP DN Migration Status
-- =============================================================================

CREATE TABLE IF NOT EXISTS ldap_dn_migration_status (
    id SERIAL PRIMARY KEY,
    table_name VARCHAR(50) NOT NULL,
    total_records INTEGER NOT NULL DEFAULT 0,
    migrated_records INTEGER NOT NULL DEFAULT 0,
    failed_records INTEGER NOT NULL DEFAULT 0,
    migration_started_at TIMESTAMP WITH TIME ZONE,
    migration_completed_at TIMESTAMP WITH TIME ZONE,
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    error_message TEXT,
    UNIQUE(table_name)
);

CREATE INDEX idx_ldap_migration_status ON ldap_dn_migration_status(status);
