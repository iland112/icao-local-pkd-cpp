-- =============================================================================
-- Pending DSC Registration Table (v2.31.0)
-- =============================================================================
-- DSC certificates discovered during PA verification are saved here
-- for admin review before being registered into the certificate table + LDAP.
-- =============================================================================

CREATE TABLE IF NOT EXISTS pending_dsc_registration (
    id UUID DEFAULT uuid_generate_v4() PRIMARY KEY,
    fingerprint_sha256 VARCHAR(64) NOT NULL,
    country_code VARCHAR(10) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(128),
    not_before TIMESTAMP,
    not_after TIMESTAMP,
    certificate_data BYTEA NOT NULL,
    signature_algorithm VARCHAR(50),
    public_key_algorithm VARCHAR(20),
    public_key_size INTEGER,
    is_self_signed BOOLEAN DEFAULT FALSE,
    validation_status VARCHAR(20) DEFAULT 'UNKNOWN',

    -- PA verification context
    pa_verification_id VARCHAR(36),
    verification_status VARCHAR(20),

    -- Approval workflow
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING'
        CHECK (status IN ('PENDING', 'APPROVED', 'REJECTED')),
    reviewed_by VARCHAR(255),
    reviewed_at TIMESTAMP,
    review_comment TEXT,

    -- Timestamps
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    -- Prevent duplicate pending entries for same DSC
    CONSTRAINT uq_pending_dsc_fingerprint UNIQUE (fingerprint_sha256)
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_pending_dsc_status ON pending_dsc_registration(status);
CREATE INDEX IF NOT EXISTS idx_pending_dsc_country ON pending_dsc_registration(country_code);
CREATE INDEX IF NOT EXISTS idx_pending_dsc_created ON pending_dsc_registration(created_at DESC);
