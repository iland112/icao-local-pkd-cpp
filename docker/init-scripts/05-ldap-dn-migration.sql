-- LDAP DN Migration Schema
-- Sprint 1: Week 5 - LDAP Storage Fix
-- Version: 1.0.0
-- Created: 2026-01-23

-- Purpose: Add ldap_dn_v2 columns to support fingerprint-based DN migration

-- =====================================================================
-- 1. Add ldap_dn_v2 columns to existing tables
-- =====================================================================

-- Certificate table
ALTER TABLE certificate ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);
COMMENT ON COLUMN certificate.ldap_dn_v2 IS 'New fingerprint-based LDAP DN (cn={fingerprint},o={type},c={country},...)';

-- Master List table
ALTER TABLE master_list ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);
COMMENT ON COLUMN master_list.ldap_dn_v2 IS 'New fingerprint-based LDAP DN (already using fingerprint)';

-- CRL table
ALTER TABLE crl ADD COLUMN IF NOT EXISTS ldap_dn_v2 VARCHAR(512);
COMMENT ON COLUMN crl.ldap_dn_v2 IS 'New fingerprint-based LDAP DN (already using fingerprint)';

-- =====================================================================
-- 2. Migration tracking table
-- =====================================================================

CREATE TABLE IF NOT EXISTS ldap_migration_status (
    id SERIAL PRIMARY KEY,
    table_name VARCHAR(50) NOT NULL,
    total_records INTEGER NOT NULL DEFAULT 0,
    migrated_records INTEGER NOT NULL DEFAULT 0,
    failed_records INTEGER NOT NULL DEFAULT 0,
    migration_start TIMESTAMP,
    migration_end TIMESTAMP,
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    error_log TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

COMMENT ON TABLE ldap_migration_status IS 'Tracks LDAP DN migration progress';
COMMENT ON COLUMN ldap_migration_status.status IS 'PENDING, IN_PROGRESS, COMPLETED, FAILED, ROLLED_BACK';

-- Index for faster status lookups
CREATE INDEX IF NOT EXISTS idx_ldap_migration_status_table
    ON ldap_migration_status(table_name);
CREATE INDEX IF NOT EXISTS idx_ldap_migration_status_status
    ON ldap_migration_status(status);

-- =====================================================================
-- 3. Migration error log table
-- =====================================================================

CREATE TABLE IF NOT EXISTS ldap_migration_error_log (
    id SERIAL PRIMARY KEY,
    migration_status_id INTEGER REFERENCES ldap_migration_status(id),
    record_id UUID,
    fingerprint VARCHAR(64),
    old_dn VARCHAR(512),
    new_dn VARCHAR(512),
    error_type VARCHAR(50),
    error_message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

COMMENT ON TABLE ldap_migration_error_log IS 'Detailed error log for migration failures';

CREATE INDEX IF NOT EXISTS idx_ldap_migration_error_log_migration_id
    ON ldap_migration_error_log(migration_status_id);
CREATE INDEX IF NOT EXISTS idx_ldap_migration_error_log_record_id
    ON ldap_migration_error_log(record_id);

-- =====================================================================
-- 4. Helper functions
-- =====================================================================

-- Function: Get migration statistics
CREATE OR REPLACE FUNCTION get_ldap_migration_stats(p_table_name VARCHAR)
RETURNS TABLE (
    total_records BIGINT,
    migrated_records BIGINT,
    remaining_records BIGINT,
    migration_percentage NUMERIC
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        COUNT(*)::BIGINT AS total_records,
        COUNT(
            CASE
                WHEN p_table_name = 'certificate' THEN c.ldap_dn_v2
                WHEN p_table_name = 'master_list' THEN NULL  -- Will be added later
                WHEN p_table_name = 'crl' THEN NULL  -- Will be added later
            END
        )::BIGINT AS migrated_records,
        (COUNT(*) - COUNT(
            CASE
                WHEN p_table_name = 'certificate' THEN c.ldap_dn_v2
                WHEN p_table_name = 'master_list' THEN NULL
                WHEN p_table_name = 'crl' THEN NULL
            END
        ))::BIGINT AS remaining_records,
        CASE
            WHEN COUNT(*) = 0 THEN 0
            ELSE ROUND(
                (COUNT(
                    CASE
                        WHEN p_table_name = 'certificate' THEN c.ldap_dn_v2
                        WHEN p_table_name = 'master_list' THEN NULL
                        WHEN p_table_name = 'crl' THEN NULL
                    END
                )::NUMERIC / COUNT(*)::NUMERIC) * 100,
                2
            )
        END AS migration_percentage
    FROM
        CASE
            WHEN p_table_name = 'certificate' THEN certificate c
            WHEN p_table_name = 'master_list' THEN master_list
            WHEN p_table_name = 'crl' THEN crl
        END
    WHERE
        CASE
            WHEN p_table_name = 'certificate' THEN c.ldap_stored = true
            WHEN p_table_name = 'master_list' THEN true  -- All master lists
            WHEN p_table_name = 'crl' THEN true  -- All CRLs
        END;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION get_ldap_migration_stats IS 'Returns migration progress statistics for a table';

-- =====================================================================
-- 5. Initial data
-- =====================================================================

-- Initialize migration status for certificate table
INSERT INTO ldap_migration_status (table_name, total_records, status)
SELECT
    'certificate',
    COUNT(*),
    'PENDING'
FROM certificate
WHERE ldap_stored = true
ON CONFLICT DO NOTHING;

-- =====================================================================
-- 6. Indexes for performance
-- =====================================================================

-- Index on ldap_dn_v2 for faster lookups
CREATE INDEX IF NOT EXISTS idx_certificate_ldap_dn_v2
    ON certificate(ldap_dn_v2) WHERE ldap_dn_v2 IS NOT NULL;

-- Index on fingerprint for migration queries
CREATE INDEX IF NOT EXISTS idx_certificate_fingerprint_sha256
    ON certificate(fingerprint_sha256);

-- Partial index for unmigrated records
CREATE INDEX IF NOT EXISTS idx_certificate_unmigrated
    ON certificate(id, fingerprint_sha256, certificate_type, country_code)
    WHERE ldap_stored = true AND ldap_dn_v2 IS NULL;

-- =====================================================================
-- 7. Verification queries (for testing)
-- =====================================================================

-- These are commented out but can be run manually for verification

/*
-- Check migration completeness
SELECT
    COUNT(*) AS total,
    COUNT(ldap_dn_v2) AS migrated,
    COUNT(*) - COUNT(ldap_dn_v2) AS remaining,
    ROUND((COUNT(ldap_dn_v2)::NUMERIC / COUNT(*)::NUMERIC) * 100, 2) AS percentage
FROM certificate
WHERE ldap_stored = true;

-- Check for DN duplicates (should return 0 rows)
SELECT ldap_dn_v2, COUNT(*)
FROM certificate
WHERE ldap_dn_v2 IS NOT NULL
GROUP BY ldap_dn_v2
HAVING COUNT(*) > 1;

-- Verify DN format (should all start with 'cn=')
SELECT ldap_dn_v2
FROM certificate
WHERE ldap_dn_v2 IS NOT NULL
  AND ldap_dn_v2 NOT LIKE 'cn=%'
LIMIT 10;

-- Check serial number collisions that will be resolved
SELECT serial_number, COUNT(*) AS count,
       STRING_AGG(DISTINCT issuer_dn, ' | ') AS issuers
FROM certificate
WHERE ldap_stored = true
GROUP BY serial_number
HAVING COUNT(*) > 1
ORDER BY count DESC
LIMIT 10;

-- Get migration statistics using helper function
SELECT * FROM get_ldap_migration_stats('certificate');
*/

-- =====================================================================
-- Migration script complete
-- =====================================================================
