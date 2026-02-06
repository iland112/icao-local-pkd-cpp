-- =============================================================================
-- Add status, error_message, and check_duration_ms to sync_status
-- =============================================================================
-- Version: 2.5.0 (Phase 5.3)
-- Created: 2026-02-05
-- Description: Add sync status, error tracking, and performance metrics
-- =============================================================================

-- Add status column
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS status VARCHAR(50) DEFAULT 'UNKNOWN';

-- Add error_message column
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS error_message TEXT;

-- Add check_duration_ms column
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS check_duration_ms INTEGER DEFAULT 0;

-- Update existing rows
UPDATE sync_status
SET status = 'UNKNOWN', check_duration_ms = 0
WHERE status IS NULL OR check_duration_ms IS NULL;

SELECT 'Migration 03: Added status, error_message, and check_duration_ms columns' AS status;
