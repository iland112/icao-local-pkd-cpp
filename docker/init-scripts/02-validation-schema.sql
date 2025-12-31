-- =============================================================================
-- ICAO Local PKD - Validation Result Schema
-- =============================================================================
-- Version: 1.1
-- Created: 2025-12-31
-- Description: Detailed validation results for trust chain verification
-- =============================================================================

-- =============================================================================
-- Validation Result Table (per certificate)
-- =============================================================================

CREATE TABLE IF NOT EXISTS validation_result (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    certificate_id UUID REFERENCES certificate(id) ON DELETE CASCADE,
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,

    -- Certificate info (denormalized for quick access)
    certificate_type VARCHAR(20) NOT NULL,  -- CSCA, DSC, DSC_NC
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT,
    issuer_dn TEXT,
    serial_number VARCHAR(100),

    -- Overall validation result
    validation_status VARCHAR(20) NOT NULL DEFAULT 'PENDING',  -- VALID, INVALID, PENDING, ERROR

    -- Trust Chain validation
    trust_chain_valid BOOLEAN,
    trust_chain_message TEXT,
    csca_found BOOLEAN DEFAULT FALSE,
    csca_subject_dn TEXT,
    csca_fingerprint VARCHAR(64),
    signature_verified BOOLEAN,
    signature_algorithm VARCHAR(50),

    -- Certificate validity
    validity_check_passed BOOLEAN,
    is_expired BOOLEAN DEFAULT FALSE,
    is_not_yet_valid BOOLEAN DEFAULT FALSE,
    not_before TIMESTAMP WITH TIME ZONE,
    not_after TIMESTAMP WITH TIME ZONE,

    -- Basic Constraints (for CSCA)
    is_ca BOOLEAN,
    is_self_signed BOOLEAN,
    path_length_constraint INTEGER,

    -- Key Usage
    key_usage_valid BOOLEAN,
    key_usage_flags TEXT,  -- e.g., "keyCertSign,cRLSign" or "digitalSignature"

    -- CRL Check (for DSC)
    crl_check_status VARCHAR(30),  -- VALID, REVOKED, CRL_UNAVAILABLE, CRL_EXPIRED, NOT_CHECKED
    crl_check_message TEXT,
    revocation_date TIMESTAMP WITH TIME ZONE,
    revocation_reason VARCHAR(50),

    -- Error details
    error_code VARCHAR(50),
    error_message TEXT,
    error_details TEXT,

    -- Timestamps
    validated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    validation_duration_ms INTEGER,

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Indexes for validation_result
CREATE INDEX IF NOT EXISTS idx_validation_result_cert_id ON validation_result(certificate_id);
CREATE INDEX IF NOT EXISTS idx_validation_result_upload_id ON validation_result(upload_id);
CREATE INDEX IF NOT EXISTS idx_validation_result_status ON validation_result(validation_status);
CREATE INDEX IF NOT EXISTS idx_validation_result_cert_type ON validation_result(certificate_type);
CREATE INDEX IF NOT EXISTS idx_validation_result_country ON validation_result(country_code);
CREATE INDEX IF NOT EXISTS idx_validation_result_trust_chain ON validation_result(trust_chain_valid);

-- =============================================================================
-- Add validation statistics columns to uploaded_file
-- =============================================================================

-- Add validation statistics columns if not exist
DO $$
BEGIN
    -- Validation counts
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'validation_valid_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN validation_valid_count INTEGER DEFAULT 0;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'validation_invalid_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN validation_invalid_count INTEGER DEFAULT 0;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'validation_pending_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN validation_pending_count INTEGER DEFAULT 0;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'validation_error_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN validation_error_count INTEGER DEFAULT 0;
    END IF;

    -- Trust chain specific counts
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'trust_chain_valid_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN trust_chain_valid_count INTEGER DEFAULT 0;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'trust_chain_invalid_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN trust_chain_invalid_count INTEGER DEFAULT 0;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'csca_not_found_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN csca_not_found_count INTEGER DEFAULT 0;
    END IF;

    -- Expired/Revoked counts
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'expired_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN expired_count INTEGER DEFAULT 0;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'uploaded_file' AND column_name = 'revoked_count') THEN
        ALTER TABLE uploaded_file ADD COLUMN revoked_count INTEGER DEFAULT 0;
    END IF;
END $$;

-- =============================================================================
-- Validation Statistics View
-- =============================================================================

CREATE OR REPLACE VIEW v_validation_statistics AS
SELECT
    certificate_type,
    country_code,
    COUNT(*) as total_count,
    SUM(CASE WHEN validation_status = 'VALID' THEN 1 ELSE 0 END) as valid_count,
    SUM(CASE WHEN validation_status = 'INVALID' THEN 1 ELSE 0 END) as invalid_count,
    SUM(CASE WHEN validation_status = 'PENDING' THEN 1 ELSE 0 END) as pending_count,
    SUM(CASE WHEN validation_status = 'ERROR' THEN 1 ELSE 0 END) as error_count,
    SUM(CASE WHEN trust_chain_valid = TRUE THEN 1 ELSE 0 END) as trust_chain_valid_count,
    SUM(CASE WHEN trust_chain_valid = FALSE THEN 1 ELSE 0 END) as trust_chain_invalid_count,
    SUM(CASE WHEN csca_found = FALSE THEN 1 ELSE 0 END) as csca_not_found_count,
    SUM(CASE WHEN is_expired = TRUE THEN 1 ELSE 0 END) as expired_count,
    SUM(CASE WHEN crl_check_status = 'REVOKED' THEN 1 ELSE 0 END) as revoked_count
FROM validation_result
GROUP BY certificate_type, country_code;

-- View for upload-specific validation statistics
CREATE OR REPLACE VIEW v_upload_validation_statistics AS
SELECT
    upload_id,
    COUNT(*) as total_validated,
    SUM(CASE WHEN validation_status = 'VALID' THEN 1 ELSE 0 END) as valid_count,
    SUM(CASE WHEN validation_status = 'INVALID' THEN 1 ELSE 0 END) as invalid_count,
    SUM(CASE WHEN validation_status = 'PENDING' THEN 1 ELSE 0 END) as pending_count,
    SUM(CASE WHEN validation_status = 'ERROR' THEN 1 ELSE 0 END) as error_count,
    SUM(CASE WHEN trust_chain_valid = TRUE THEN 1 ELSE 0 END) as trust_chain_valid_count,
    SUM(CASE WHEN trust_chain_valid = FALSE THEN 1 ELSE 0 END) as trust_chain_invalid_count,
    SUM(CASE WHEN csca_found = FALSE AND certificate_type IN ('DSC', 'DSC_NC') THEN 1 ELSE 0 END) as csca_not_found_count,
    SUM(CASE WHEN is_expired = TRUE THEN 1 ELSE 0 END) as expired_count,
    SUM(CASE WHEN crl_check_status = 'REVOKED' THEN 1 ELSE 0 END) as revoked_count,
    AVG(validation_duration_ms) as avg_validation_time_ms
FROM validation_result
GROUP BY upload_id;

-- =============================================================================
-- Comments
-- =============================================================================

COMMENT ON TABLE validation_result IS 'Stores detailed validation results for each certificate';
COMMENT ON COLUMN validation_result.trust_chain_valid IS 'TRUE if DSC was signed by valid CSCA, or CSCA is self-signed';
COMMENT ON COLUMN validation_result.csca_found IS 'TRUE if issuing CSCA was found in database';
COMMENT ON COLUMN validation_result.signature_verified IS 'TRUE if certificate signature was verified successfully';
COMMENT ON COLUMN validation_result.crl_check_status IS 'Result of CRL revocation check';
