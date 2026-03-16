-- ============================================================================
-- CSR Management Schema (PostgreSQL)
-- v2.35.0: ICAO PKD CSR generation and management
-- ============================================================================

-- CSR Request table
CREATE TABLE IF NOT EXISTS csr_request (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),

    -- Subject DN fields
    subject_dn VARCHAR(1024) NOT NULL,
    country_code VARCHAR(2),
    organization VARCHAR(255),
    common_name VARCHAR(255),

    -- Key & Algorithm (ICAO requirement: RSA-2048 + SHA256withRSA)
    key_algorithm VARCHAR(20) NOT NULL DEFAULT 'RSA-2048',
    signature_algorithm VARCHAR(30) NOT NULL DEFAULT 'SHA256withRSA',

    -- CSR data
    csr_pem TEXT NOT NULL,
    csr_der BYTEA NOT NULL,
    public_key_fingerprint VARCHAR(64) NOT NULL,

    -- Private key (AES-256-GCM encrypted via PII_ENCRYPTION_KEY)
    private_key_encrypted TEXT NOT NULL,

    -- Status workflow
    status VARCHAR(20) NOT NULL DEFAULT 'CREATED',
    memo TEXT,

    -- Audit
    created_by VARCHAR(100),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_csr_request_status ON csr_request(status);
CREATE INDEX IF NOT EXISTS idx_csr_request_created ON csr_request(created_at);
CREATE INDEX IF NOT EXISTS idx_csr_request_fingerprint ON csr_request(public_key_fingerprint);

-- Status constraint: CREATED, SUBMITTED, ISSUED, REVOKED
ALTER TABLE csr_request DROP CONSTRAINT IF EXISTS chk_csr_status;
ALTER TABLE csr_request ADD CONSTRAINT chk_csr_status
    CHECK (status IN ('CREATED', 'SUBMITTED', 'ISSUED', 'REVOKED'));
