-- =============================================================================
-- ICAO Local PKD - Sprint 3: Link Certificate Validation Schema Rollback
-- =============================================================================
-- Version: 1.0.0
-- Created: 2026-01-24
-- Sprint: Sprint 3 - DSC Validation Integration (Day 1-2)
-- Description: Rollback script for 08-link-cert-validation-schema.sql
-- =============================================================================

-- =============================================================================
-- Rollback: Remove trust_chain_path column
-- =============================================================================

-- Drop index first
DROP INDEX IF EXISTS idx_validation_result_trust_chain_path;

-- Drop column
ALTER TABLE validation_result
DROP COLUMN IF EXISTS trust_chain_path;

-- =============================================================================
-- Verification
-- =============================================================================

DO $$
DECLARE
    col_exists BOOLEAN;
    idx_exists BOOLEAN;
BEGIN
    -- Check column removed
    SELECT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_name = 'validation_result'
          AND column_name = 'trust_chain_path'
    ) INTO col_exists;

    IF col_exists THEN
        RAISE EXCEPTION '❌ Rollback failed: trust_chain_path column still exists';
    ELSE
        RAISE NOTICE '✅ Rollback successful: trust_chain_path column removed';
    END IF;

    -- Check index removed
    SELECT EXISTS (
        SELECT 1
        FROM pg_indexes
        WHERE tablename = 'validation_result'
          AND indexname = 'idx_validation_result_trust_chain_path'
    ) INTO idx_exists;

    IF idx_exists THEN
        RAISE WARNING '⚠️  Rollback warning: Index still exists';
    ELSE
        RAISE NOTICE '✅ Index removed: idx_validation_result_trust_chain_path';
    END IF;

    -- Display rollback summary
    RAISE NOTICE '================================================';
    RAISE NOTICE 'Sprint 3 Schema Rollback Complete';
    RAISE NOTICE '================================================';
    RAISE NOTICE 'Removed: trust_chain_path column';
    RAISE NOTICE 'Removed: idx_validation_result_trust_chain_path index';
    RAISE NOTICE '================================================';
END $$;

-- =============================================================================
