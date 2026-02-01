-- =============================================================================
-- Certificate Data Reset Script
-- =============================================================================
-- Purpose: Reset all certificate and upload data for fresh testing
-- Scope: Deletes all data but preserves schema and system tables
-- Date: 2026-01-30
-- =============================================================================

-- Disable triggers temporarily to avoid constraint issues
SET session_replication_role = 'replica';

-- =============================================================================
-- 1. Delete Certificate-Related Data
-- =============================================================================

-- Delete validation results (references certificate)
DELETE FROM validation_result;

-- Delete certificate tracking (references certificate and uploaded_file)
DELETE FROM certificate_tracking;

-- Delete all certificates
DELETE FROM certificate;

-- Delete CRLs
DELETE FROM crl;

-- Delete Master Lists
DELETE FROM master_list;

-- Delete upload issues
DELETE FROM upload_issue;

-- Delete uploaded files
DELETE FROM uploaded_file;

-- =============================================================================
-- 2. Delete Sync and Reconciliation History
-- =============================================================================

-- Delete reconciliation logs
DELETE FROM reconciliation_log;

-- Delete reconciliation summaries
DELETE FROM reconciliation_summary;

-- Delete sync status history
DELETE FROM sync_status;

-- =============================================================================
-- 3. Delete Audit Logs (Optional - Comment out if you want to preserve)
-- =============================================================================

-- Delete operation audit logs
DELETE FROM operation_audit_log;

-- =============================================================================
-- 4. Reset Auto-increment Sequences (if any)
-- =============================================================================

-- Note: We use UUIDs for primary keys, so no sequences to reset
-- If there are any integer sequences in the future, reset them here

-- =============================================================================
-- 5. Re-enable Triggers
-- =============================================================================

SET session_replication_role = 'origin';

-- =============================================================================
-- 6. Verify Data Deletion
-- =============================================================================

-- Show counts of remaining data
SELECT
    'certificate' AS table_name,
    COUNT(*) AS remaining_count
FROM certificate
UNION ALL
SELECT 'crl', COUNT(*) FROM crl
UNION ALL
SELECT 'master_list', COUNT(*) FROM master_list
UNION ALL
SELECT 'uploaded_file', COUNT(*) FROM uploaded_file
UNION ALL
SELECT 'certificate_tracking', COUNT(*) FROM certificate_tracking
UNION ALL
SELECT 'validation_result', COUNT(*) FROM validation_result
UNION ALL
SELECT 'upload_issue', COUNT(*) FROM upload_issue
UNION ALL
SELECT 'reconciliation_log', COUNT(*) FROM reconciliation_log
UNION ALL
SELECT 'reconciliation_summary', COUNT(*) FROM reconciliation_summary
UNION ALL
SELECT 'sync_status', COUNT(*) FROM sync_status
UNION ALL
SELECT 'operation_audit_log', COUNT(*) FROM operation_audit_log;

-- =============================================================================
-- Success Message
-- =============================================================================

DO $$
BEGIN
    RAISE NOTICE '✅ Database reset complete!';
    RAISE NOTICE 'All certificate and upload data has been deleted.';
    RAISE NOTICE 'Schema and X.509 metadata fields are preserved.';
    RAISE NOTICE 'Ready for fresh data upload.';
END $$;
