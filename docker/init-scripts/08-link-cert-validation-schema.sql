-- =============================================================================
-- ICAO Local PKD - Sprint 3: Link Certificate Trust Chain Validation Schema
-- =============================================================================
-- Version: 1.0.0
-- Created: 2026-01-24
-- Sprint: Sprint 3 - DSC Validation Integration (Day 1-2)
-- Description: Add trust_chain_path column to validation_result table
--              for Link Certificate trust chain support
-- =============================================================================

-- =============================================================================
-- Migration: Add trust_chain_path column
-- =============================================================================

ALTER TABLE validation_result
ADD COLUMN IF NOT EXISTS trust_chain_path TEXT;

COMMENT ON COLUMN validation_result.trust_chain_path IS
    'Human-readable trust chain path for DSC validation. '
    'Example: "DSC → CN=CSCA_old → CN=Link → CN=CSCA_new" '
    'Shows the full certificate chain including Link Certificates for CSCA key transitions.';

-- =============================================================================
-- Index for faster queries (Full-text search on chain path)
-- =============================================================================

CREATE INDEX IF NOT EXISTS idx_validation_result_trust_chain_path
ON validation_result USING gin(to_tsvector('english', trust_chain_path));

COMMENT ON INDEX idx_validation_result_trust_chain_path IS
    'Full-text search index for trust chain path queries. '
    'Enables fast searching for specific CSCAs or Link Certificates in validation history.';

-- =============================================================================
-- Update existing records (set to empty string for NULL values)
-- =============================================================================

UPDATE validation_result
SET trust_chain_path = ''
WHERE trust_chain_path IS NULL;

-- =============================================================================
-- Verification
-- =============================================================================

DO $$
DECLARE
    col_exists BOOLEAN;
    idx_exists BOOLEAN;
    null_count INTEGER;
BEGIN
    -- Check column exists
    SELECT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_name = 'validation_result'
          AND column_name = 'trust_chain_path'
    ) INTO col_exists;

    IF col_exists THEN
        RAISE NOTICE '✅ Migration successful: trust_chain_path column added';
    ELSE
        RAISE EXCEPTION '❌ Migration failed: trust_chain_path column not found';
    END IF;

    -- Check index exists
    SELECT EXISTS (
        SELECT 1
        FROM pg_indexes
        WHERE tablename = 'validation_result'
          AND indexname = 'idx_validation_result_trust_chain_path'
    ) INTO idx_exists;

    IF idx_exists THEN
        RAISE NOTICE '✅ Index created: idx_validation_result_trust_chain_path';
    ELSE
        RAISE WARNING '⚠️  Index not found: idx_validation_result_trust_chain_path';
    END IF;

    -- Check for NULL values
    SELECT COUNT(*)
    INTO null_count
    FROM validation_result
    WHERE trust_chain_path IS NULL;

    IF null_count = 0 THEN
        RAISE NOTICE '✅ All existing records updated (no NULL values)';
    ELSE
        RAISE WARNING '⚠️  Found % records with NULL trust_chain_path', null_count;
    END IF;

    -- Display statistics
    RAISE NOTICE '================================================';
    RAISE NOTICE 'Sprint 3 Schema Migration Complete';
    RAISE NOTICE '================================================';
    RAISE NOTICE 'Table: validation_result';
    RAISE NOTICE 'New Column: trust_chain_path (TEXT)';
    RAISE NOTICE 'Index: idx_validation_result_trust_chain_path (GIN)';
    RAISE NOTICE '================================================';
END $$;

-- =============================================================================
-- Rollback Instructions (for reference, DO NOT execute)
-- =============================================================================

-- To rollback this migration, run:
--
-- ALTER TABLE validation_result
-- DROP COLUMN IF EXISTS trust_chain_path;
--
-- DROP INDEX IF EXISTS idx_validation_result_trust_chain_path;

-- =============================================================================
