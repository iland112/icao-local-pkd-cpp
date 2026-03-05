-- =============================================================================
-- Revalidation History: Trust Chain + CRL columns (Oracle)
-- =============================================================================
-- Date: 2026-03-05
-- Version: v2.29.0
-- Purpose: Add 9 columns for 3-step revalidation results
-- =============================================================================

SET SERVEROUTPUT ON;

BEGIN
    -- Trust Chain re-validation columns
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD tc_processed NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD tc_newly_valid NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD tc_still_pending NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD tc_errors NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;

    -- CRL re-check columns
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD crl_checked NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD crl_revoked NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD crl_unavailable NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD crl_expired NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;
    BEGIN EXECUTE IMMEDIATE 'ALTER TABLE revalidation_history ADD crl_errors NUMBER(10) DEFAULT 0'; EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1430 THEN RAISE; END IF; END;

    DBMS_OUTPUT.PUT_LINE('Revalidation history trust chain/CRL columns added successfully');
END;
/
