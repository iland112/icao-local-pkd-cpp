-- =============================================================================
-- Oracle Database Initialization Script
-- Create PKD_USER with necessary privileges
-- =============================================================================
-- This script runs automatically when Oracle container starts for the first time
-- Location: /opt/oracle/scripts/startup/ (mapped from docker/db-oracle/init/)
-- =============================================================================

-- Connect as SYSDBA
WHENEVER SQLERROR EXIT SQL.SQLCODE

-- Create tablespace for PKD data
CREATE TABLESPACE pkd_data
  DATAFILE '/opt/oracle/oradata/XE/pkd_data01.dbf'
  SIZE 100M
  AUTOEXTEND ON
  NEXT 10M
  MAXSIZE UNLIMITED;

-- Create PKD user
CREATE USER pkd_user IDENTIFIED BY pkd_password
  DEFAULT TABLESPACE pkd_data
  TEMPORARY TABLESPACE temp
  QUOTA UNLIMITED ON pkd_data;

-- Grant necessary privileges
GRANT CONNECT TO pkd_user;
GRANT RESOURCE TO pkd_user;
GRANT CREATE SESSION TO pkd_user;
GRANT CREATE TABLE TO pkd_user;
GRANT CREATE VIEW TO pkd_user;
GRANT CREATE SEQUENCE TO pkd_user;
GRANT CREATE PROCEDURE TO pkd_user;
GRANT CREATE TRIGGER TO pkd_user;

-- Grant additional privileges for application
GRANT SELECT ANY TABLE TO pkd_user;
GRANT INSERT ANY TABLE TO pkd_user;
GRANT UPDATE ANY TABLE TO pkd_user;
GRANT DELETE ANY TABLE TO pkd_user;

-- Grant privileges for CLOB/BLOB operations (needed for certificate data)
GRANT UNLIMITED TABLESPACE TO pkd_user;

-- Enable DBMS_OUTPUT for debugging
GRANT EXECUTE ON DBMS_OUTPUT TO pkd_user;

-- Commit
COMMIT;

-- Display success message
BEGIN
  DBMS_OUTPUT.PUT_LINE('=============================================================================');
  DBMS_OUTPUT.PUT_LINE('PKD_USER created successfully');
  DBMS_OUTPUT.PUT_LINE('Tablespace: pkd_data');
  DBMS_OUTPUT.PUT_LINE('Privileges: CONNECT, RESOURCE, and table operations granted');
  DBMS_OUTPUT.PUT_LINE('=============================================================================');
END;
/

EXIT;
