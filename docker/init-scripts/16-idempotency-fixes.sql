-- =============================================================================
-- Migration 16: Idempotency fixes
-- 1. Adds UNIQUE constraint to revoked_certificate(crl_id, serial_number)
--    to support ON CONFLICT DO NOTHING on retry/resume.
-- 2. Adds partial UNIQUE index on uploaded_file(file_hash) for non-empty hashes
--    to prevent race-condition duplicate uploads.
-- =============================================================================

-- 1. Add UNIQUE constraint to revoked_certificate if not already present (safe to re-run)
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'uq_revoked_cert_crl_serial'
    ) THEN
        -- Deduplicate first (keep earliest created_at) before adding constraint
        DELETE FROM revoked_certificate a
        USING revoked_certificate b
        WHERE a.crl_id = b.crl_id
          AND a.serial_number = b.serial_number
          AND a.created_at > b.created_at;

        ALTER TABLE revoked_certificate
            ADD CONSTRAINT uq_revoked_cert_crl_serial UNIQUE (crl_id, serial_number);
    END IF;
END $$;

-- 2. Add partial unique index on uploaded_file.file_hash if not already present (safe to re-run)
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_indexes
        WHERE indexname = 'uq_uploaded_file_nonempty_hash'
    ) THEN
        CREATE UNIQUE INDEX uq_uploaded_file_nonempty_hash
            ON uploaded_file(file_hash) WHERE file_hash != '';
    END IF;
END $$;
