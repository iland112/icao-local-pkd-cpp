-- =============================================================================
-- ICAO Local PKD - LDAP DN Migration
-- =============================================================================
-- Version: 2.0.0
-- Created: 2026-01-25
-- Description: Add ldap_dn_v2 columns for fingerprint-based DN migration
-- Purpose: Support transition from subject DN-based to fingerprint-based LDAP DNs
-- =============================================================================

-- =============================================================================
-- Add ldap_dn_v2 columns
-- =============================================================================

-- Certificate table
ALTER TABLE certificate ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);

-- CRL table
ALTER TABLE crl ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);

-- Master List table
ALTER TABLE master_list ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);

-- Link Certificate table
ALTER TABLE link_certificate ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);

-- =============================================================================
-- Indexes for ldap_dn_v2
-- =============================================================================

CREATE INDEX IF NOT EXISTS idx_certificate_ldap_dn_v2 ON certificate(ldap_dn_v2);
CREATE INDEX IF NOT EXISTS idx_crl_ldap_dn_v2 ON crl(ldap_dn_v2);
CREATE INDEX IF NOT EXISTS idx_ml_ldap_dn_v2 ON master_list(ldap_dn_v2);
CREATE INDEX IF NOT EXISTS idx_lc_ldap_dn_v2 ON link_certificate(ldap_dn_v2);

-- =============================================================================
-- Migration status tracking
-- =============================================================================

CREATE TABLE IF NOT EXISTS ldap_dn_migration_status (
    id SERIAL PRIMARY KEY,
    table_name VARCHAR(50) NOT NULL,
    total_records INTEGER NOT NULL DEFAULT 0,
    migrated_records INTEGER NOT NULL DEFAULT 0,
    failed_records INTEGER NOT NULL DEFAULT 0,
    migration_started_at TIMESTAMP,
    migration_completed_at TIMESTAMP,
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    error_message TEXT,
    UNIQUE(table_name)
);

CREATE INDEX idx_ldap_migration_status ON ldap_dn_migration_status(status);
