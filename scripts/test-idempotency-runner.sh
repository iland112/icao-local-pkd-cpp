#!/bin/bash
# Runner: Wait for current processing, reset, then run idempotency test
set -e

echo "=== $(date) Starting idempotency test runner ==="

# Step 1: Wait for any current processing to complete
echo "Waiting for current uploads to finish processing..."
while true; do
    count=$(curl -s http://localhost:8080/api/upload/history?limit=10 2>/dev/null | \
        python3 -c "import sys,json;d=json.load(sys.stdin);print(len([u for u in d.get('content',[]) if u.get('status')=='PROCESSING']))" 2>/dev/null || echo "0")
    echo "  $(date +%H:%M:%S) - Processing: $count"
    if [ "$count" = "0" ]; then break; fi
    sleep 30
done
echo "All processing complete."

# Step 2: Reset DB
echo "Resetting PostgreSQL..."
docker exec -i icao-local-pkd-postgres psql -U pkd -d localpkd <<'EOSQL'
SET session_replication_role = 'replica';
DELETE FROM validation_result;
DELETE FROM certificate;
DELETE FROM crl;
DELETE FROM master_list;
DELETE FROM uploaded_file;
DELETE FROM reconciliation_log;
DELETE FROM reconciliation_summary;
DELETE FROM sync_status;
DELETE FROM operation_audit_log;
SET session_replication_role = 'origin';
EOSQL
echo "DB reset done."

# Step 3: Reset LDAP
echo "Resetting LDAP..."
LDAP_ARGS="-x -H ldap://localhost:3891 -D cn=admin,dc=ldap,dc=smartcoreinc,dc=com -w ldap_test_password_123"
BASE="dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

for branch in "dc=data" "dc=nc-data"; do
    ldapsearch $LDAP_ARGS -b "${branch},${BASE}" -s one "(objectClass=*)" dn 2>/dev/null | \
        grep "^dn:" | sed "s/^dn: //" | while IFS= read -r dn; do
        ldapdelete -r $LDAP_ARGS "$dn" 2>/dev/null || true
    done
done
echo "LDAP reset done."

# Step 4: Verify clean state
cert_count=$(docker exec icao-local-pkd-postgres psql -U pkd -d localpkd -t -A -c "SELECT COUNT(*) FROM certificate;" 2>/dev/null)
echo "Certificates in DB: $cert_count"

# Step 5: Run the idempotency test
echo ""
echo "=== Starting idempotency test ==="
bash /home/kbjung/projects/c/icao-local-pkd/scripts/test-idempotency.sh

echo ""
echo "=== $(date) Test runner complete ==="
