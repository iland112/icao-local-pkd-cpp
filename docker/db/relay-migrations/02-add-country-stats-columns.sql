-- =============================================================================
-- Add db_country_stats and ldap_country_stats to sync_status
-- =============================================================================
-- Version: 2.5.0 (Phase 5.3)
-- Created: 2026-02-05
-- Description: Split country_stats into separate DB and LDAP columns
-- =============================================================================

-- Drop old single column if it exists
ALTER TABLE sync_status
DROP COLUMN IF EXISTS country_stats;

-- Add separate DB and LDAP country statistics columns
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS db_country_stats JSONB;

ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS ldap_country_stats JSONB;

SELECT 'Migration 02: Added db_country_stats and ldap_country_stats columns' AS status;
