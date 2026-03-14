-- =============================================================================
-- EAC Service Schema (Oracle)
-- BSI TR-03110 CVC Certificate Management
-- =============================================================================

CONNECT pkd_user/pkd_password@//localhost:1521/XEPDB1

SET SQLBLANKLINES ON
WHENEVER SQLERROR CONTINUE

-- CVC Certificates
BEGIN
    EXECUTE IMMEDIATE '
        CREATE TABLE cvc_certificate (
            id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
            upload_id VARCHAR2(36),

            cvc_type VARCHAR2(20) NOT NULL,
            country_code VARCHAR2(10) NOT NULL,

            car VARCHAR2(255),
            chr VARCHAR2(255),

            chat_oid VARCHAR2(100),
            chat_role VARCHAR2(20),
            chat_permissions VARCHAR2(4000),

            public_key_oid VARCHAR2(100),
            public_key_algorithm VARCHAR2(50),

            effective_date VARCHAR2(30),
            expiration_date VARCHAR2(30),

            fingerprint_sha256 VARCHAR2(128) NOT NULL,

            signature_valid NUMBER(1) DEFAULT 0,
            validation_status VARCHAR2(20) DEFAULT ''PENDING'',
            validation_message VARCHAR2(4000),

            issuer_cvc_id VARCHAR2(36),
            source_type VARCHAR2(30) DEFAULT ''FILE_UPLOAD'',

            created_at TIMESTAMP DEFAULT SYSTIMESTAMP,
            updated_at TIMESTAMP DEFAULT SYSTIMESTAMP,

            CONSTRAINT uq_cvc_fingerprint UNIQUE (fingerprint_sha256)
        )';
EXCEPTION WHEN OTHERS THEN
    IF SQLCODE != -955 THEN RAISE; END IF;
END;
/

-- EAC Trust Chain Records
BEGIN
    EXECUTE IMMEDIATE '
        CREATE TABLE eac_trust_chain (
            id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
            is_certificate_id VARCHAR2(36) REFERENCES cvc_certificate(id),
            dv_certificate_id VARCHAR2(36) REFERENCES cvc_certificate(id),
            cvca_certificate_id VARCHAR2(36) REFERENCES cvc_certificate(id),
            chain_valid NUMBER(1) DEFAULT 0,
            chain_path VARCHAR2(4000),
            chain_depth NUMBER(10) DEFAULT 0,
            validation_message VARCHAR2(4000),
            validated_at TIMESTAMP DEFAULT SYSTIMESTAMP
        )';
EXCEPTION WHEN OTHERS THEN
    IF SQLCODE != -955 THEN RAISE; END IF;
END;
/

-- Indexes
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_cvc_cert_country ON cvc_certificate(country_code)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_cvc_cert_type ON cvc_certificate(cvc_type)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_cvc_cert_status ON cvc_certificate(validation_status)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_cvc_cert_car ON cvc_certificate(car)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_cvc_cert_chr ON cvc_certificate(chr)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_cvc_cert_country_type ON cvc_certificate(country_code, cvc_type)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_eac_chain_is ON eac_trust_chain(is_certificate_id)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_eac_chain_dv ON eac_trust_chain(dv_certificate_id)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'CREATE INDEX idx_eac_chain_cvca ON eac_trust_chain(cvca_certificate_id)'; EXCEPTION WHEN OTHERS THEN IF SQLCODE NOT IN (-955, -1408) THEN RAISE; END IF; END;
/

COMMIT;
EXIT;
