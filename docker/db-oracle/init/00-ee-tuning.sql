-- =============================================================================
-- Oracle EE 21c — Performance Tuning for Load Testing
-- =============================================================================
-- Version: 1.0.0
-- Created: 2026-02-27
-- Description: Oracle EE 파라미터 설정 (XE 하드 제한 해제)
--   XE 21c: PROCESSES=150, SESSIONS=248, SGA=2GB (변경 불가)
--   EE 21c: 모두 변경 가능 — 부하 테스트용 값 설정
-- =============================================================================
-- Note: SPFILE 변경이므로 DB 재시작 필요
--       Docker 컨테이너 초기 기동 시 startup 스크립트로 실행됨
--       두 번째 기동부터는 SPFILE에 이미 설정값이 저장되어 있음
-- =============================================================================

-- 동시 접속 제한 해제
ALTER SYSTEM SET PROCESSES=1000 SCOPE=SPFILE;
ALTER SYSTEM SET SESSIONS=1500 SCOPE=SPFILE;

-- 메모리 할당 확대 (XE 2GB 제한 → EE 무제한)
ALTER SYSTEM SET SGA_TARGET=4G SCOPE=SPFILE;
ALTER SYSTEM SET PGA_AGGREGATE_TARGET=2G SCOPE=SPFILE;

-- 커서/세션 관련
ALTER SYSTEM SET OPEN_CURSORS=1000 SCOPE=SPFILE;

EXIT;
