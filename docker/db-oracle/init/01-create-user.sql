-- =============================================================================
-- Oracle Database Initialization Script
-- Create PKD_USER with necessary privileges
-- =============================================================================
-- This script runs on every Oracle container startup
-- Must be IDEMPOTENT (safe to re-run)
-- Location: /opt/oracle/scripts/startup/ (mapped from docker/db-oracle/init/)
-- =============================================================================

-- SQL*Plus settings
SET SQLBLANKLINES ON

-- Connect to ORCLPDB1 (pluggable database — Oracle EE 21c default)
ALTER SESSION SET CONTAINER = ORCLPDB1;

-- Create tablespace (skip if exists)
DECLARE
  v_count NUMBER;
BEGIN
  SELECT COUNT(*) INTO v_count FROM dba_tablespaces WHERE tablespace_name = 'PKD_DATA';
  IF v_count = 0 THEN
    EXECUTE IMMEDIATE 'CREATE TABLESPACE pkd_data DATAFILE ''/opt/oracle/oradata/ORCLCDB/ORCLPDB1/pkd_data01.dbf'' SIZE 100M AUTOEXTEND ON NEXT 10M MAXSIZE UNLIMITED';
    DBMS_OUTPUT.PUT_LINE('Tablespace PKD_DATA created');
  ELSE
    DBMS_OUTPUT.PUT_LINE('Tablespace PKD_DATA already exists - skipping');
  END IF;
END;
/

-- Create user (skip if exists)
DECLARE
  v_count NUMBER;
BEGIN
  SELECT COUNT(*) INTO v_count FROM dba_users WHERE username = 'PKD_USER';
  IF v_count = 0 THEN
    EXECUTE IMMEDIATE 'CREATE USER pkd_user IDENTIFIED BY pkd_password DEFAULT TABLESPACE pkd_data TEMPORARY TABLESPACE temp QUOTA UNLIMITED ON pkd_data';
    DBMS_OUTPUT.PUT_LINE('User PKD_USER created');
  ELSE
    DBMS_OUTPUT.PUT_LINE('User PKD_USER already exists - skipping');
  END IF;
END;
/

-- Grant privileges (idempotent - re-granting is safe)
GRANT CONNECT TO pkd_user;
GRANT RESOURCE TO pkd_user;
GRANT CREATE SESSION TO pkd_user;
GRANT CREATE TABLE TO pkd_user;
GRANT CREATE VIEW TO pkd_user;
GRANT CREATE SEQUENCE TO pkd_user;
GRANT CREATE PROCEDURE TO pkd_user;
GRANT CREATE TRIGGER TO pkd_user;
GRANT SELECT ANY TABLE TO pkd_user;
GRANT INSERT ANY TABLE TO pkd_user;
GRANT UPDATE ANY TABLE TO pkd_user;
GRANT DELETE ANY TABLE TO pkd_user;
GRANT UNLIMITED TABLESPACE TO pkd_user;
GRANT EXECUTE ON DBMS_OUTPUT TO pkd_user;

COMMIT;

BEGIN
  DBMS_OUTPUT.PUT_LINE('=============================================================================');
  DBMS_OUTPUT.PUT_LINE('PKD_USER initialization complete');
  DBMS_OUTPUT.PUT_LINE('=============================================================================');
END;
/

EXIT;
