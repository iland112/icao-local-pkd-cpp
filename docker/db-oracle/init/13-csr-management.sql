-- ============================================================================
-- CSR Management Schema (Oracle)
-- v2.35.0: ICAO PKD CSR generation and management
-- ============================================================================

CONNECT pkd_user/pkd_password@XEPDB1;
SET SQLBLANKLINES ON;
SET DEFINE OFF;

-- Allow re-runs (skip "already exists" errors)
WHENEVER SQLERROR CONTINUE;

-- CSR Request table
CREATE TABLE csr_request (
    id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,

    -- Subject DN fields
    subject_dn VARCHAR2(1024) NOT NULL,
    country_code VARCHAR2(2),
    organization VARCHAR2(255),
    common_name VARCHAR2(255),

    -- Key & Algorithm (ICAO requirement: RSA-2048 + SHA256withRSA)
    key_algorithm VARCHAR2(20) DEFAULT 'RSA-2048' NOT NULL,
    signature_algorithm VARCHAR2(30) DEFAULT 'SHA256withRSA' NOT NULL,

    -- CSR data (AES-256-GCM encrypted)
    csr_pem CLOB NOT NULL,
    csr_der BLOB NOT NULL,
    public_key_fingerprint VARCHAR2(64) NOT NULL,

    -- Private key (AES-256-GCM encrypted via PII_ENCRYPTION_KEY)
    private_key_encrypted VARCHAR2(4000) NOT NULL,

    -- Issued certificate (registered after ICAO issues cert from CSR)
    issued_certificate_pem CLOB,
    issued_certificate_der BLOB,
    certificate_serial VARCHAR2(128),
    certificate_subject_dn VARCHAR2(1024),
    certificate_issuer_dn VARCHAR2(1024),
    certificate_not_before TIMESTAMP,
    certificate_not_after TIMESTAMP,
    certificate_fingerprint VARCHAR2(64),
    issued_at TIMESTAMP,
    registered_by VARCHAR2(100),

    -- Status workflow
    status VARCHAR2(20) DEFAULT 'CREATED' NOT NULL,
    memo VARCHAR2(4000),

    -- Audit
    created_by VARCHAR2(100),
    created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
    updated_at TIMESTAMP DEFAULT SYSTIMESTAMP,

    -- Constraints
    CONSTRAINT chk_csr_status CHECK (status IN ('CREATED', 'SUBMITTED', 'ISSUED', 'REVOKED'))
);

CREATE INDEX idx_csr_request_status ON csr_request(status);
CREATE INDEX idx_csr_request_created ON csr_request(created_at);
CREATE INDEX idx_csr_request_fingerprint ON csr_request(public_key_fingerprint);

-- Auto-update trigger
CREATE OR REPLACE TRIGGER trg_csr_request_updated_at
BEFORE UPDATE ON csr_request
FOR EACH ROW
BEGIN
    :NEW.updated_at := SYSTIMESTAMP;
END;
/

COMMIT;

EXIT;
