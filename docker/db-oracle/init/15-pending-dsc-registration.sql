-- =============================================================================
-- Pending DSC Registration Table (v2.31.0) — Oracle
-- =============================================================================

SET SQLBLANKLINES ON
WHENEVER SQLERROR CONTINUE

-- Create table
DECLARE
    v_count NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_count FROM user_tables WHERE table_name = 'PENDING_DSC_REGISTRATION';
    IF v_count = 0 THEN
        EXECUTE IMMEDIATE '
        CREATE TABLE pending_dsc_registration (
            id VARCHAR2(36) DEFAULT SYS_GUID() PRIMARY KEY,
            fingerprint_sha256 VARCHAR2(64) NOT NULL,
            country_code VARCHAR2(10) NOT NULL,
            subject_dn VARCHAR2(4000) NOT NULL,
            issuer_dn VARCHAR2(4000) NOT NULL,
            serial_number VARCHAR2(128),
            not_before TIMESTAMP,
            not_after TIMESTAMP,
            certificate_data BLOB NOT NULL,
            signature_algorithm VARCHAR2(50),
            public_key_algorithm VARCHAR2(20),
            public_key_size NUMBER(10),
            is_self_signed NUMBER(1) DEFAULT 0,
            validation_status VARCHAR2(20) DEFAULT ''UNKNOWN'',
            pa_verification_id VARCHAR2(36),
            verification_status VARCHAR2(20),
            status VARCHAR2(20) DEFAULT ''PENDING'' NOT NULL
                CHECK (status IN (''PENDING'', ''APPROVED'', ''REJECTED'')),
            reviewed_by VARCHAR2(255),
            reviewed_at TIMESTAMP,
            review_comment VARCHAR2(4000),
            created_at TIMESTAMP DEFAULT SYSTIMESTAMP NOT NULL,
            CONSTRAINT uq_pending_dsc_fingerprint UNIQUE (fingerprint_sha256)
        )';
        DBMS_OUTPUT.PUT_LINE('Created table: pending_dsc_registration');
    ELSE
        DBMS_OUTPUT.PUT_LINE('Table pending_dsc_registration already exists');
    END IF;
END;
/

-- Indexes
BEGIN
    EXECUTE IMMEDIATE 'CREATE INDEX idx_pending_dsc_status ON pending_dsc_registration(status)';
EXCEPTION WHEN OTHERS THEN
    IF SQLCODE = -955 THEN NULL; ELSE RAISE; END IF;
END;
/

BEGIN
    EXECUTE IMMEDIATE 'CREATE INDEX idx_pending_dsc_country ON pending_dsc_registration(country_code)';
EXCEPTION WHEN OTHERS THEN
    IF SQLCODE = -955 THEN NULL; ELSE RAISE; END IF;
END;
/

BEGIN
    EXECUTE IMMEDIATE 'CREATE INDEX idx_pending_dsc_created ON pending_dsc_registration(created_at DESC)';
EXCEPTION WHEN OTHERS THEN
    IF SQLCODE = -955 THEN NULL; ELSE RAISE; END IF;
END;
/

COMMIT;
