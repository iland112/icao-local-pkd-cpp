-- =============================================================================
-- Add db_stored_in_ldap_count and ldap_total_entries to sync_status
-- =============================================================================
-- Version: 2.5.0 (Phase 5.3)
-- Created: 2026-02-05
-- Description: Add tracking columns for stored_in_ldap flag and total LDAP entries
-- =============================================================================

-- Add db_stored_in_ldap_count column (after db_crl_count)
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS db_stored_in_ldap_count INTEGER DEFAULT 0;

-- Add ldap_total_entries column (after ldap_crl_count)
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS ldap_total_entries INTEGER DEFAULT 0;

-- Update existing rows to have default values
UPDATE sync_status
SET db_stored_in_ldap_count = 0, ldap_total_entries = 0
WHERE db_stored_in_ldap_count IS NULL OR ldap_total_entries IS NULL;

SELECT 'Migration 01: Added db_stored_in_ldap_count and ldap_total_entries columns' AS status;
