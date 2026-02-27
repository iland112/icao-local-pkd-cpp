-- =============================================================================
-- Oracle Database Schema Migration Script
-- ICAO Local PKD - Database Schema (Oracle Version)
-- =============================================================================
-- This file will be populated in Phase 4.3: Schema Migration
-- PostgreSQL DDL → Oracle DDL conversion
-- =============================================================================
-- Tables to be migrated (22 total):
-- 1. uploaded_file
-- 2. certificate
-- 3. crl
-- 4. master_list
-- 5. validation_result
-- 6. sync_status
-- 7. reconciliation_summary
-- 8. reconciliation_log
-- 9. duplicate_certificate_tracking
-- 10. auth_audit_log
-- 11. operation_audit_log
-- 12. user_account
-- 13. role_definition
-- 14. user_role
-- 15. permission_definition
-- 16. role_permission
-- 17. user_permission_override
-- 18. pa_verification
-- 19. data_group_hash
-- 20. icao_version_check
-- 21. icao_version_history
-- 22. system_metrics
-- =============================================================================

-- SQL*Plus settings
SET SQLBLANKLINES ON

-- Connect as PKD_USER to XEPDB1 (Pluggable Database — Oracle EE 21c)
-- Note: pkd_user is created in XEPDB1 by 01-create-user.sql
CONNECT pkd_user/pkd_password@XEPDB1;

-- Placeholder for schema creation
-- TO BE IMPLEMENTED IN PHASE 4.3

BEGIN
  DBMS_OUTPUT.PUT_LINE('=============================================================================');
  DBMS_OUTPUT.PUT_LINE('Schema migration script ready');
  DBMS_OUTPUT.PUT_LINE('Phase 4.3 will populate this file with complete DDL');
  DBMS_OUTPUT.PUT_LINE('=============================================================================');
END;
/

EXIT;
