#!/bin/bash
# docker-ldap-init.sh - OpenLDAP ICAO PKD DIT 구조 및 MMR 초기화 스크립트

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "OpenLDAP ICAO PKD DIT 구조 및 MMR 초기화..."

# 컨테이너가 실행 중인지 확인
if ! docker ps | grep -q icao-local-pkd-openldap1; then
    echo "[ERROR] openldap1 컨테이너가 실행 중이지 않습니다."
    echo "        먼저 ./scripts/docker-start.sh --skip-app 을 실행하세요."
    exit 1
fi

if ! docker ps | grep -q icao-local-pkd-openldap2; then
    echo "[ERROR] openldap2 컨테이너가 실행 중이지 않습니다."
    echo "        먼저 ./scripts/docker-start.sh --skip-app 을 실행하세요."
    exit 1
fi

echo ""
echo "OpenLDAP 시작 대기 중..."
sleep 5

# ===== MMR (Multi-Master Replication) 설정 =====
echo ""
echo "[MMR] Multi-Master Replication 설정 중..."

# OpenLDAP1 MMR 설정
echo "   - OpenLDAP1 MMR 설정..."
docker exec icao-local-pkd-openldap1 bash -c 'cat > /tmp/mmr-setup.ldif << EOF
# Load syncprov module
dn: cn=module{0},cn=config
changetype: modify
add: olcModuleLoad
olcModuleLoad: syncprov

# Add syncprov overlay to mdb database
dn: olcOverlay=syncprov,olcDatabase={1}mdb,cn=config
changetype: add
objectClass: olcOverlayConfig
objectClass: olcSyncProvConfig
olcOverlay: syncprov
olcSpCheckpoint: 100 10
olcSpSessionLog: 100

# Configure server ID
dn: cn=config
changetype: modify
replace: olcServerID
olcServerID: 1 ldap://openldap1
olcServerID: 2 ldap://openldap2

# Add syncrepl to mdb database
dn: olcDatabase={1}mdb,cn=config
changetype: modify
add: olcSyncRepl
olcSyncRepl: rid=001 provider=ldap://openldap2 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=admin searchbase="dc=ldap,dc=smartcoreinc,dc=com" type=refreshAndPersist retry="5 5 300 5" timeout=1
-
add: olcMirrorMode
olcMirrorMode: TRUE
EOF
ldapmodify -x -H ldap://localhost -D "cn=admin,cn=config" -w config -f /tmp/mmr-setup.ldif 2>/dev/null' && echo "   [OK] OpenLDAP1 MMR 설정 완료" || echo "   (OpenLDAP1 MMR 이미 설정됨)"

# OpenLDAP2 MMR 설정
echo "   - OpenLDAP2 MMR 설정..."
docker exec icao-local-pkd-openldap2 bash -c 'cat > /tmp/mmr-setup.ldif << EOF
# Load syncprov module
dn: cn=module{0},cn=config
changetype: modify
add: olcModuleLoad
olcModuleLoad: syncprov

# Add syncprov overlay to mdb database
dn: olcOverlay=syncprov,olcDatabase={1}mdb,cn=config
changetype: add
objectClass: olcOverlayConfig
objectClass: olcSyncProvConfig
olcOverlay: syncprov
olcSpCheckpoint: 100 10
olcSpSessionLog: 100

# Configure server ID
dn: cn=config
changetype: modify
replace: olcServerID
olcServerID: 1 ldap://openldap1
olcServerID: 2 ldap://openldap2

# Add syncrepl to mdb database
dn: olcDatabase={1}mdb,cn=config
changetype: modify
add: olcSyncRepl
olcSyncRepl: rid=002 provider=ldap://openldap1 binddn="cn=admin,dc=ldap,dc=smartcoreinc,dc=com" bindmethod=simple credentials=admin searchbase="dc=ldap,dc=smartcoreinc,dc=com" type=refreshAndPersist retry="5 5 300 5" timeout=1
-
add: olcMirrorMode
olcMirrorMode: TRUE
EOF
ldapmodify -x -H ldap://localhost -D "cn=admin,cn=config" -w config -f /tmp/mmr-setup.ldif 2>/dev/null' && echo "   [OK] OpenLDAP2 MMR 설정 완료" || echo "   (OpenLDAP2 MMR 이미 설정됨)"

echo "[OK] MMR 설정 완료!"

# ===== PKD DIT 구조 생성 =====
echo ""
echo "[DIT] ICAO PKD DIT 구조 생성 중..."

# PKD DIT LDIF 파일 생성 및 적용 (heredoc을 컨테이너 내부에서 처리)
docker exec icao-local-pkd-openldap1 bash -c 'cat > /tmp/pkd-dit.ldif << EOF
dn: dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: dcObject
objectClass: organization
dc: pkd
o: ICAO PKD

dn: dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: dcObject
objectClass: organization
dc: download
o: PKD Download

dn: dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: dcObject
objectClass: organization
dc: data
o: PKD Data

dn: dc=nc-data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
objectClass: dcObject
objectClass: organization
dc: nc-data
o: PKD Non-Compliant Data
EOF
ldapadd -x -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin -H ldap://localhost -f /tmp/pkd-dit.ldif 2>/dev/null || echo "   (DIT already exists)"'

# 복제 대기
echo ""
echo "복제 동기화 대기 중..."
sleep 3

echo ""
echo "[OK] ICAO PKD DIT 구조 초기화 완료!"

# ===== MMR 복제 테스트 =====
echo ""
echo "[TEST] MMR 복제 테스트 중..."

# OpenLDAP1에서 DIT 확인
LDAP1_COUNT=$(docker exec icao-local-pkd-openldap1 ldapsearch -x \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
    -w admin \
    -H ldap://localhost \
    -b "dc=ldap,dc=smartcoreinc,dc=com" \
    -s sub "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo 0)

# OpenLDAP2에서 DIT 확인
LDAP2_COUNT=$(docker exec icao-local-pkd-openldap2 ldapsearch -x \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
    -w admin \
    -H ldap://localhost \
    -b "dc=ldap,dc=smartcoreinc,dc=com" \
    -s sub "(objectClass=*)" dn 2>/dev/null | grep -c "^dn:" || echo 0)

echo "   - OpenLDAP1 엔트리 수: $LDAP1_COUNT"
echo "   - OpenLDAP2 엔트리 수: $LDAP2_COUNT"

if [ "$LDAP1_COUNT" == "$LDAP2_COUNT" ]; then
    echo "[OK] MMR 복제 정상 작동!"
else
    echo "[WARN] 복제 동기화 대기 중... (수 초 후 동기화됩니다)"
fi

echo ""
echo "[DIT] 현재 DIT 구조:"
docker exec icao-local-pkd-openldap1 ldapsearch -x \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
    -w admin \
    -H ldap://localhost \
    -b "dc=ldap,dc=smartcoreinc,dc=com" \
    -s sub "(objectClass=*)" dn | grep "^dn:"

echo ""
echo "[INFO] 접속 정보:"
echo "   - HAProxy (LB):   ldap://localhost:389"
echo "   - OpenLDAP 1:     ldap://localhost:3891"
echo "   - OpenLDAP 2:     ldap://localhost:3892"
echo "   - HAProxy Stats:  http://localhost:8404/stats"
echo "   - Admin DN:       cn=admin,dc=ldap,dc=smartcoreinc,dc=com"
echo "   - Admin Password: admin"
