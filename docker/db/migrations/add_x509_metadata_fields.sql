-- =============================================================================
-- X.509 Certificate Metadata Fields Addition
-- =============================================================================
-- Date: 2026-01-30
-- Description: Add 15 critical X.509 metadata fields to certificate table
-- Impact: Schema change only (existing data will have NULL values)
-- Follow-up: Run update_x509_metadata.sql to populate existing certificates
-- =============================================================================

-- Drop existing constraints that might conflict
-- (None expected, but safe to check)

-- =============================================================================
-- 1. Basic Certificate Fields (4 fields)
-- =============================================================================

-- Version (v1=0, v2=1, v3=2)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS version INTEGER DEFAULT 2;

COMMENT ON COLUMN certificate.version IS 'X.509 certificate version (0=v1, 1=v2, 2=v3)';

-- Signature Algorithm Name
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS signature_algorithm VARCHAR(50);

COMMENT ON COLUMN certificate.signature_algorithm IS 'Signature algorithm (e.g., sha256WithRSAEncryption, ecdsa-with-SHA256)';

-- Signature Hash Algorithm (extracted from signature_algorithm)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS signature_hash_algorithm VARCHAR(20);

COMMENT ON COLUMN certificate.signature_hash_algorithm IS 'Hash algorithm used in signature (e.g., SHA-256, SHA-384, SHA-512)';

-- =============================================================================
-- 2. Public Key Information (3 fields)
-- =============================================================================

-- Public Key Algorithm
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS public_key_algorithm VARCHAR(30);

COMMENT ON COLUMN certificate.public_key_algorithm IS 'Public key algorithm (RSA, ECDSA, DSA, Ed25519, etc.)';

-- Public Key Size (in bits)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS public_key_size INTEGER;

COMMENT ON COLUMN certificate.public_key_size IS 'Public key size in bits (2048, 4096 for RSA; 256, 384 for ECDSA)';

-- Public Key Curve (for ECDSA only)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS public_key_curve VARCHAR(50);

COMMENT ON COLUMN certificate.public_key_curve IS 'Elliptic curve name for ECDSA (e.g., prime256v1, secp384r1, secp521r1)';

-- =============================================================================
-- 3. X.509 v3 Extensions - Key Usage (2 fields)
-- =============================================================================

-- Key Usage (array of strings)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS key_usage TEXT[];

COMMENT ON COLUMN certificate.key_usage IS 'Key usage flags (e.g., {digitalSignature, keyCertSign, cRLSign})';

-- Extended Key Usage (array of strings)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS extended_key_usage TEXT[];

COMMENT ON COLUMN certificate.extended_key_usage IS 'Extended key usage OIDs/names (e.g., {serverAuth, clientAuth})';

-- =============================================================================
-- 4. X.509 v3 Extensions - Basic Constraints (2 fields)
-- =============================================================================

-- Is CA certificate
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS is_ca BOOLEAN DEFAULT FALSE;

COMMENT ON COLUMN certificate.is_ca IS 'TRUE if this is a CA certificate (from Basic Constraints extension)';

-- Path Length Constraint
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS path_len_constraint INTEGER;

COMMENT ON COLUMN certificate.path_len_constraint IS 'Max certification path depth (NULL = unlimited, 0 = end-entity only)';

-- =============================================================================
-- 5. X.509 v3 Extensions - Identifiers (2 fields)
-- =============================================================================

-- Subject Key Identifier (SKI)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS subject_key_identifier VARCHAR(40);

COMMENT ON COLUMN certificate.subject_key_identifier IS 'Subject Key Identifier - SHA-1 hash of public key (hex string)';

-- Authority Key Identifier (AKI)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS authority_key_identifier VARCHAR(40);

COMMENT ON COLUMN certificate.authority_key_identifier IS 'Authority Key Identifier - matches issuer SKI (hex string)';

-- =============================================================================
-- 6. X.509 v3 Extensions - CRL & Revocation (2 fields)
-- =============================================================================

-- CRL Distribution Points (array of URLs)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS crl_distribution_points TEXT[];

COMMENT ON COLUMN certificate.crl_distribution_points IS 'Array of CRL download URLs';

-- OCSP Responder URL
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS ocsp_responder_url TEXT;

COMMENT ON COLUMN certificate.ocsp_responder_url IS 'OCSP responder URL from Authority Information Access extension';

-- =============================================================================
-- 7. Derived/Computed Fields (1 field)
-- =============================================================================

-- Is Self-Signed (computed from Subject DN == Issuer DN)
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS is_self_signed BOOLEAN DEFAULT FALSE;

COMMENT ON COLUMN certificate.is_self_signed IS 'TRUE if Subject DN equals Issuer DN (self-signed certificate)';

-- =============================================================================
-- Create Indexes for Performance
-- =============================================================================

-- Index on signature algorithm for security analysis
CREATE INDEX IF NOT EXISTS idx_certificate_signature_algorithm
ON certificate(signature_algorithm);

-- Index on public key algorithm for filtering
CREATE INDEX IF NOT EXISTS idx_certificate_public_key_algorithm
ON certificate(public_key_algorithm);

-- Index on public key size for security queries
CREATE INDEX IF NOT EXISTS idx_certificate_public_key_size
ON certificate(public_key_size);

-- Index on is_ca for CA certificate queries
CREATE INDEX IF NOT EXISTS idx_certificate_is_ca
ON certificate(is_ca)
WHERE is_ca = TRUE;

-- Index on is_self_signed for root CA queries
CREATE INDEX IF NOT EXISTS idx_certificate_is_self_signed
ON certificate(is_self_signed)
WHERE is_self_signed = TRUE;

-- Index on subject_key_identifier for trust chain building
CREATE INDEX IF NOT EXISTS idx_certificate_subject_key_identifier
ON certificate(subject_key_identifier)
WHERE subject_key_identifier IS NOT NULL;

-- Index on authority_key_identifier for trust chain building
CREATE INDEX IF NOT EXISTS idx_certificate_authority_key_identifier
ON certificate(authority_key_identifier)
WHERE authority_key_identifier IS NOT NULL;

-- =============================================================================
-- Add Constraints
-- =============================================================================

-- Version must be 0, 1, or 2
ALTER TABLE certificate
ADD CONSTRAINT chk_certificate_version
CHECK (version IN (0, 1, 2));

-- Public key size must be positive
ALTER TABLE certificate
ADD CONSTRAINT chk_public_key_size_positive
CHECK (public_key_size IS NULL OR public_key_size > 0);

-- Path length must be non-negative
ALTER TABLE certificate
ADD CONSTRAINT chk_path_len_nonnegative
CHECK (path_len_constraint IS NULL OR path_len_constraint >= 0);

-- =============================================================================
-- Summary
-- =============================================================================
-- Total fields added: 15
--   - Basic fields: 4 (version, signature_algorithm, signature_hash_algorithm)
--   - Public key: 3 (public_key_algorithm, public_key_size, public_key_curve)
--   - Key usage: 2 (key_usage, extended_key_usage)
--   - Basic constraints: 2 (is_ca, path_len_constraint)
--   - Identifiers: 2 (subject_key_identifier, authority_key_identifier)
--   - CRL/OCSP: 2 (crl_distribution_points, ocsp_responder_url)
--   - Computed: 1 (is_self_signed)
--
-- Indexes created: 7
-- Constraints added: 3
-- =============================================================================

-- Verify schema
SELECT column_name, data_type, character_maximum_length, is_nullable
FROM information_schema.columns
WHERE table_schema = 'public'
  AND table_name = 'certificate'
  AND column_name IN (
    'version', 'signature_algorithm', 'signature_hash_algorithm',
    'public_key_algorithm', 'public_key_size', 'public_key_curve',
    'key_usage', 'extended_key_usage',
    'is_ca', 'path_len_constraint',
    'subject_key_identifier', 'authority_key_identifier',
    'crl_distribution_points', 'ocsp_responder_url',
    'is_self_signed'
  )
ORDER BY ordinal_position;
