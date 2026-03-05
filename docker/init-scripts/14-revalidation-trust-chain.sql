-- =============================================================================
-- Revalidation History: Trust Chain + CRL columns
-- =============================================================================
-- Date: 2026-03-05
-- Version: v2.29.0
-- Purpose: Add 9 columns for 3-step revalidation results
--          (expiration + trust chain + CRL)
-- =============================================================================

-- Trust Chain re-validation columns
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS tc_processed INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS tc_newly_valid INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS tc_still_pending INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS tc_errors INTEGER DEFAULT 0;

-- CRL re-check columns
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS crl_checked INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS crl_revoked INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS crl_unavailable INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS crl_expired INTEGER DEFAULT 0;
ALTER TABLE revalidation_history ADD COLUMN IF NOT EXISTS crl_errors INTEGER DEFAULT 0;
