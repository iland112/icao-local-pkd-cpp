-- =============================================================================
-- Oracle XE 21c — XE 제한 내 최적화
-- =============================================================================
-- Version: 1.0.0
-- Created: 2026-02-28
-- Description: Oracle XE 하드 제한 내 최적 파라미터 설정
--   XE 21c 하드 제한: PROCESSES≤150, SESSIONS≤248, SGA≤2GB, PGA≤1GB
--   이전 00-ee-tuning.sql이 EE 파라미터(SGA 4GB, PGA 2GB, PROCESSES 1000)를
--   XE 이미지에 적용하여 ORA-56752 에러로 기동 실패하던 문제를 해결합니다.
-- =============================================================================
-- Note: SPFILE 변경이므로 DB 재시작 필요
--       Docker 컨테이너 초기 기동 시 startup 스크립트로 실행됨
--       두 번째 기동부터는 SPFILE에 이미 설정값이 저장되어 있음
-- =============================================================================

-- 메모리 할당 (XE 제한 내 최적)
ALTER SYSTEM SET SGA_TARGET=1536M SCOPE=SPFILE;           -- 1.5GB (XE max 2GB)
ALTER SYSTEM SET PGA_AGGREGATE_TARGET=512M SCOPE=SPFILE;  -- 512MB (XE max 1GB)

-- 동시 접속 (XE 하드 제한)
ALTER SYSTEM SET PROCESSES=150 SCOPE=SPFILE;              -- XE max 150

-- 커서/세션 관련
ALTER SYSTEM SET OPEN_CURSORS=300 SCOPE=SPFILE;           -- 기본값 충분

EXIT;
