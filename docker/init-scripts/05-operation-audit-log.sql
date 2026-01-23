-- ============================================================================
-- Phase 4.4 Enhanced Audit Logging Schema
-- Operation Audit Log Table for Sensitive Business Operations
-- ============================================================================

-- Operation audit trail for sensitive business operations
-- Complements auth_audit_log (authentication events) with operation tracking
CREATE TABLE IF NOT EXISTS operation_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),

    -- User identification
    user_id UUID REFERENCES users(id) ON DELETE SET NULL,
    username VARCHAR(255),

    -- Operation details
    operation_type VARCHAR(50) NOT NULL,  -- FILE_UPLOAD, CERT_EXPORT, UPLOAD_DELETE, PA_VERIFY
    operation_subtype VARCHAR(50),        -- LDIF, MASTER_LIST, SINGLE_CERT, COUNTRY_ZIP, etc.
    resource_id VARCHAR(255),             -- UUID of uploaded file, certificate DN, verification ID
    resource_type VARCHAR(50),            -- UPLOADED_FILE, CERTIFICATE, PA_VERIFICATION

    -- Request context
    ip_address VARCHAR(45),               -- IPv4 or IPv6
    user_agent TEXT,
    request_method VARCHAR(10),           -- GET, POST, PUT, DELETE
    request_path TEXT,                    -- /api/upload/ldif, /api/certificates/export/file

    -- Operation result
    success BOOLEAN DEFAULT true,
    status_code INTEGER,                  -- HTTP status code (200, 400, 500, etc.)
    error_message TEXT,

    -- Metadata (JSON for flexibility)
    metadata JSONB,                       -- Additional operation-specific data

    -- Timing
    duration_ms INTEGER,                  -- Operation duration in milliseconds
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for audit queries
CREATE INDEX IF NOT EXISTS idx_op_audit_user_id ON operation_audit_log(user_id);
CREATE INDEX IF NOT EXISTS idx_op_audit_created_at ON operation_audit_log(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_op_audit_operation_type ON operation_audit_log(operation_type);
CREATE INDEX IF NOT EXISTS idx_op_audit_success ON operation_audit_log(success);
CREATE INDEX IF NOT EXISTS idx_op_audit_username ON operation_audit_log(username);
CREATE INDEX IF NOT EXISTS idx_op_audit_resource_id ON operation_audit_log(resource_id);
CREATE INDEX IF NOT EXISTS idx_op_audit_ip_address ON operation_audit_log(ip_address);
CREATE INDEX IF NOT EXISTS idx_op_audit_metadata ON operation_audit_log USING GIN(metadata);

-- ============================================================================
-- Operation Type Reference
-- ============================================================================
--
-- FILE_UPLOAD:
--   - Subtypes: LDIF, MASTER_LIST
--   - Metadata: { fileName, fileSize, processingMode, totalEntries, validationStats }
--
-- CERT_EXPORT:
--   - Subtypes: SINGLE_CERT, COUNTRY_ZIP
--   - Metadata: { country, certType, format (DER/PEM), certificateCount }
--
-- UPLOAD_DELETE:
--   - Subtypes: FAILED_UPLOAD
--   - Metadata: { uploadId, fileName, deletedRecords }
--
-- PA_VERIFY:
--   - Subtypes: SOD, DG1, DG2
--   - Metadata: { issuingCountry, documentNumber, verificationSteps }
--
-- SYNC_TRIGGER:
--   - Subtypes: MANUAL_SYNC, AUTO_RECONCILE
--   - Metadata: { discrepancyCount, reconciledCount }
--
-- ============================================================================

-- ============================================================================
-- Sample Queries (for testing and monitoring)
-- ============================================================================

-- Most frequent operations
-- SELECT operation_type, COUNT(*) as count FROM operation_audit_log GROUP BY operation_type ORDER BY count DESC;

-- Failed operations in last 24 hours
-- SELECT * FROM operation_audit_log WHERE success = false AND created_at > NOW() - INTERVAL '24 hours' ORDER BY created_at DESC;

-- User activity summary
-- SELECT username, operation_type, COUNT(*) as count FROM operation_audit_log GROUP BY username, operation_type ORDER BY count DESC;

-- Slowest operations
-- SELECT operation_type, operation_subtype, AVG(duration_ms) as avg_ms, MAX(duration_ms) as max_ms
-- FROM operation_audit_log WHERE duration_ms IS NOT NULL GROUP BY operation_type, operation_subtype ORDER BY avg_ms DESC;

-- Export operations by country
-- SELECT metadata->>'country' as country, COUNT(*) as export_count
-- FROM operation_audit_log WHERE operation_type = 'CERT_EXPORT' AND metadata->>'country' IS NOT NULL
-- GROUP BY metadata->>'country' ORDER BY export_count DESC;

-- Recent PA verifications
-- SELECT username, metadata->>'issuingCountry' as country, success, created_at
-- FROM operation_audit_log WHERE operation_type = 'PA_VERIFY' ORDER BY created_at DESC LIMIT 10;
