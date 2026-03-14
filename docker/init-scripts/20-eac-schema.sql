-- =============================================================================
-- EAC Service Schema (PostgreSQL)
-- BSI TR-03110 CVC Certificate Management
-- =============================================================================

-- CVC Certificates
CREATE TABLE IF NOT EXISTS cvc_certificate (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID,

    cvc_type VARCHAR(20) NOT NULL,           -- CVCA, DV_DOMESTIC, DV_FOREIGN, IS
    country_code VARCHAR(10) NOT NULL,

    car VARCHAR(255),                         -- Certification Authority Reference
    chr VARCHAR(255),                         -- Certificate Holder Reference

    chat_oid VARCHAR(100),
    chat_role VARCHAR(20),
    chat_permissions TEXT,                    -- JSON array string

    public_key_oid VARCHAR(100),
    public_key_algorithm VARCHAR(50),

    effective_date VARCHAR(30),
    expiration_date VARCHAR(30),

    fingerprint_sha256 VARCHAR(128) NOT NULL UNIQUE,

    signature_valid BOOLEAN DEFAULT FALSE,
    validation_status VARCHAR(20) DEFAULT 'PENDING',  -- VALID, INVALID, PENDING, EXPIRED
    validation_message TEXT,

    issuer_cvc_id UUID,
    source_type VARCHAR(30) DEFAULT 'FILE_UPLOAD',

    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- EAC Trust Chain Records
CREATE TABLE IF NOT EXISTS eac_trust_chain (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    is_certificate_id UUID REFERENCES cvc_certificate(id),
    dv_certificate_id UUID REFERENCES cvc_certificate(id),
    cvca_certificate_id UUID REFERENCES cvc_certificate(id),
    chain_valid BOOLEAN DEFAULT FALSE,
    chain_path TEXT,
    chain_depth INTEGER DEFAULT 0,
    validation_message TEXT,
    validated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_cvc_cert_country ON cvc_certificate(country_code);
CREATE INDEX IF NOT EXISTS idx_cvc_cert_type ON cvc_certificate(cvc_type);
CREATE INDEX IF NOT EXISTS idx_cvc_cert_status ON cvc_certificate(validation_status);
CREATE INDEX IF NOT EXISTS idx_cvc_cert_car ON cvc_certificate(car);
CREATE INDEX IF NOT EXISTS idx_cvc_cert_chr ON cvc_certificate(chr);
CREATE INDEX IF NOT EXISTS idx_cvc_cert_fingerprint ON cvc_certificate(fingerprint_sha256);
CREATE INDEX IF NOT EXISTS idx_cvc_cert_country_type ON cvc_certificate(country_code, cvc_type);
CREATE INDEX IF NOT EXISTS idx_eac_chain_is ON eac_trust_chain(is_certificate_id);
CREATE INDEX IF NOT EXISTS idx_eac_chain_dv ON eac_trust_chain(dv_certificate_id);
CREATE INDEX IF NOT EXISTS idx_eac_chain_cvca ON eac_trust_chain(cvca_certificate_id);
