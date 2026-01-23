-- ============================================================================
-- Sprint 2: Link Certificate (LC) Schema
-- ============================================================================
-- Created: 2026-01-24
-- Purpose: Support Link Certificate trust chain validation
--
-- Link Certificates bridge CSCA key changes during transition periods:
-- Trust Chain: CSCA (old) → LC → CSCA (new)
--
-- References:
-- - ICAO Doc 9303 Part 12: Public Key Infrastructure for MRTDs
-- - RFC 5280: X.509 PKI Certificate and CRL Profile
-- ============================================================================

-- ============================================================================
-- Table: link_certificate
-- ============================================================================
-- Stores Link Certificates used for CSCA key transitions

CREATE TABLE IF NOT EXISTS link_certificate (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID REFERENCES uploaded_file(id) ON DELETE CASCADE,

    -- Certificate Identity
    subject_dn VARCHAR(512) NOT NULL,
    issuer_dn VARCHAR(512) NOT NULL,
    serial_number VARCHAR(128) NOT NULL,
    fingerprint_sha256 VARCHAR(64) UNIQUE NOT NULL,

    -- Certificate Metadata
    not_before TIMESTAMP NOT NULL,
    not_after TIMESTAMP NOT NULL,
    country_code VARCHAR(3),

    -- Public Key Info
    public_key_algorithm VARCHAR(50),
    public_key_size INTEGER,

    -- Link Certificate Specifics
    old_csca_subject_dn VARCHAR(512),     -- CSCA being phased out
    old_csca_fingerprint VARCHAR(64),      -- References certificate.fingerprint_sha256
    new_csca_subject_dn VARCHAR(512),     -- New CSCA being introduced
    new_csca_fingerprint VARCHAR(64),      -- References certificate.fingerprint_sha256

    -- Validation Results
    trust_chain_valid BOOLEAN DEFAULT false,
    old_csca_signature_valid BOOLEAN DEFAULT false,
    new_csca_signature_valid BOOLEAN DEFAULT false,
    validity_period_valid BOOLEAN DEFAULT false,
    extensions_valid BOOLEAN DEFAULT false,
    revocation_status VARCHAR(20) DEFAULT 'UNKNOWN',  -- GOOD, REVOKED, UNKNOWN
    validation_message TEXT,
    validation_timestamp TIMESTAMP,

    -- Certificate Extensions
    basic_constraints_ca BOOLEAN,             -- CA:TRUE required for LC
    basic_constraints_pathlen INTEGER,        -- pathlen:0 typical for LC
    key_usage VARCHAR(256),                   -- Certificate Sign, CRL Sign
    extended_key_usage VARCHAR(256),

    -- Binary Data
    certificate_binary BYTEA NOT NULL,

    -- LDAP Synchronization (Sprint 1 fingerprint-based DN)
    ldap_dn_v2 VARCHAR(512),                  -- cn={fingerprint},o=lc,c={country},...
    stored_in_ldap BOOLEAN DEFAULT false,
    ldap_stored_at TIMESTAMP,

    -- Audit
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- Constraints
    CONSTRAINT uk_link_cert_fingerprint UNIQUE (fingerprint_sha256),
    CONSTRAINT chk_revocation_status CHECK (revocation_status IN ('GOOD', 'REVOKED', 'UNKNOWN'))
);

-- Indexes for performance
CREATE INDEX idx_link_cert_country ON link_certificate(country_code);
CREATE INDEX idx_link_cert_old_csca ON link_certificate(old_csca_fingerprint);
CREATE INDEX idx_link_cert_new_csca ON link_certificate(new_csca_fingerprint);
CREATE INDEX idx_link_cert_validity ON link_certificate(not_before, not_after);
CREATE INDEX idx_link_cert_issuer ON link_certificate(issuer_dn);
CREATE INDEX idx_link_cert_subject ON link_certificate(subject_dn);
CREATE INDEX idx_link_cert_serial ON link_certificate(serial_number);
CREATE INDEX idx_link_cert_ldap_dn ON link_certificate(ldap_dn_v2) WHERE ldap_dn_v2 IS NOT NULL;
CREATE INDEX idx_link_cert_stored ON link_certificate(stored_in_ldap);
CREATE INDEX idx_link_cert_trust_valid ON link_certificate(trust_chain_valid);
CREATE INDEX idx_link_cert_upload_id ON link_certificate(upload_id);

-- Comments
COMMENT ON TABLE link_certificate IS 'Link Certificates for CSCA key transitions (ICAO Doc 9303 Part 12)';
COMMENT ON COLUMN link_certificate.old_csca_subject_dn IS 'Subject DN of CSCA being phased out (issuer of LC)';
COMMENT ON COLUMN link_certificate.new_csca_subject_dn IS 'Subject DN of new CSCA (signed by LC)';
COMMENT ON COLUMN link_certificate.basic_constraints_pathlen IS 'pathlen:0 means LC can only sign end-entity certs';
COMMENT ON COLUMN link_certificate.ldap_dn_v2 IS 'Sprint 1 fingerprint-based DN: cn={sha256},o=lc,c={country},...';

-- ============================================================================
-- Table: crl_revocation_log
-- ============================================================================
-- Audit log for CRL revocation checks

CREATE TABLE IF NOT EXISTS crl_revocation_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),

    -- Certificate Reference (polymorphic - can reference CSCA/DSC/LC)
    certificate_id UUID,                      -- FK to certificate/link_certificate table
    certificate_type VARCHAR(10) NOT NULL,    -- CSCA, DSC, DSC_NC, LC
    serial_number VARCHAR(128) NOT NULL,
    fingerprint_sha256 VARCHAR(64) NOT NULL,
    subject_dn VARCHAR(512),

    -- Revocation Details
    revocation_status VARCHAR(20) NOT NULL,   -- REVOKED, GOOD, UNKNOWN
    revocation_reason VARCHAR(50),            -- keyCompromise, cACompromise, superseded, ...
    revocation_date TIMESTAMP,                -- Date certificate was revoked (from CRL)

    -- CRL Source
    crl_id UUID REFERENCES crl(id),
    crl_issuer_dn VARCHAR(512),
    crl_this_update TIMESTAMP,                -- CRL effective date
    crl_next_update TIMESTAMP,                -- CRL expiry date

    -- Audit
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    check_duration_ms INTEGER,                -- Time taken to check CRL

    -- Constraints
    CONSTRAINT chk_crl_revocation_status CHECK (revocation_status IN ('GOOD', 'REVOKED', 'UNKNOWN')),
    CONSTRAINT chk_cert_type CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'LC'))
);

-- Indexes
CREATE INDEX idx_revocation_cert_id ON crl_revocation_log(certificate_id);
CREATE INDEX idx_revocation_fingerprint ON crl_revocation_log(fingerprint_sha256);
CREATE INDEX idx_revocation_serial ON crl_revocation_log(serial_number);
CREATE INDEX idx_revocation_status ON crl_revocation_log(revocation_status);
CREATE INDEX idx_revocation_checked_at ON crl_revocation_log(checked_at DESC);
CREATE INDEX idx_revocation_cert_type ON crl_revocation_log(certificate_type);
CREATE INDEX idx_revocation_crl_id ON crl_revocation_log(crl_id);

-- Comments
COMMENT ON TABLE crl_revocation_log IS 'Audit log for CRL-based certificate revocation checks';
COMMENT ON COLUMN crl_revocation_log.certificate_type IS 'Type of certificate checked: CSCA, DSC, DSC_NC, or LC';
COMMENT ON COLUMN crl_revocation_log.revocation_reason IS 'RFC 5280 CRL reason codes: keyCompromise, cACompromise, etc.';

-- ============================================================================
-- Update Triggers for link_certificate
-- ============================================================================

CREATE OR REPLACE FUNCTION update_link_certificate_timestamp()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trigger_update_link_certificate_timestamp
    BEFORE UPDATE ON link_certificate
    FOR EACH ROW
    EXECUTE FUNCTION update_link_certificate_timestamp();

-- ============================================================================
-- Statistics View
-- ============================================================================

CREATE OR REPLACE VIEW v_link_certificate_stats AS
SELECT
    country_code,
    COUNT(*) AS total_lc,
    COUNT(*) FILTER (WHERE trust_chain_valid = true) AS valid_lc,
    COUNT(*) FILTER (WHERE trust_chain_valid = false) AS invalid_lc,
    COUNT(*) FILTER (WHERE revocation_status = 'GOOD') AS good_lc,
    COUNT(*) FILTER (WHERE revocation_status = 'REVOKED') AS revoked_lc,
    COUNT(*) FILTER (WHERE revocation_status = 'UNKNOWN') AS unknown_status_lc,
    COUNT(*) FILTER (WHERE not_after < NOW()) AS expired_lc,
    COUNT(*) FILTER (WHERE not_before <= NOW() AND not_after >= NOW()) AS active_lc,
    COUNT(*) FILTER (WHERE stored_in_ldap = true) AS stored_in_ldap_lc
FROM link_certificate
GROUP BY country_code
ORDER BY country_code;

COMMENT ON VIEW v_link_certificate_stats IS 'Link Certificate statistics by country';

-- ============================================================================
-- CRL Coverage View
-- ============================================================================

CREATE OR REPLACE VIEW v_crl_revocation_coverage AS
SELECT
    certificate_type,
    COUNT(DISTINCT certificate_id) AS total_checked,
    COUNT(*) FILTER (WHERE revocation_status = 'GOOD') AS good_count,
    COUNT(*) FILTER (WHERE revocation_status = 'REVOKED') AS revoked_count,
    COUNT(*) FILTER (WHERE revocation_status = 'UNKNOWN') AS unknown_count,
    MAX(checked_at) AS last_check,
    AVG(check_duration_ms) AS avg_check_duration_ms
FROM crl_revocation_log
GROUP BY certificate_type;

COMMENT ON VIEW v_crl_revocation_coverage IS 'CRL revocation check coverage statistics';

-- ============================================================================
-- Grant Permissions
-- ============================================================================

-- Grant to application user (assuming 'pkd' user)
GRANT SELECT, INSERT, UPDATE, DELETE ON link_certificate TO pkd;
GRANT SELECT, INSERT, UPDATE, DELETE ON crl_revocation_log TO pkd;
GRANT SELECT ON v_link_certificate_stats TO pkd;
GRANT SELECT ON v_crl_revocation_coverage TO pkd;

-- ============================================================================
-- Sample Queries (for reference)
-- ============================================================================

-- Find all active Link Certificates for a country
-- SELECT * FROM link_certificate
-- WHERE country_code = 'US'
--   AND not_before <= NOW()
--   AND not_after >= NOW()
--   AND trust_chain_valid = true
-- ORDER BY not_before DESC;

-- Find LC by old CSCA fingerprint
-- SELECT lc.*, old_csca.subject_dn AS old_csca_subject
-- FROM link_certificate lc
-- LEFT JOIN certificate old_csca ON lc.old_csca_fingerprint = old_csca.fingerprint_sha256
-- WHERE lc.old_csca_fingerprint = 'a1b2c3d4...';

-- Find LC by new CSCA fingerprint
-- SELECT lc.*, new_csca.subject_dn AS new_csca_subject
-- FROM link_certificate lc
-- LEFT JOIN certificate new_csca ON lc.new_csca_fingerprint = new_csca.fingerprint_sha256
-- WHERE lc.new_csca_fingerprint = 'e1f2g3h4...';

-- Check revocation history for a certificate
-- SELECT * FROM crl_revocation_log
-- WHERE fingerprint_sha256 = 'a1b2c3d4...'
-- ORDER BY checked_at DESC
-- LIMIT 10;

-- ============================================================================
-- End of Sprint 2 Link Certificate Schema
-- ============================================================================
