# ICAO Auto Sync - Runtime Testing Guide

**Date**: 2026-01-19
**Version**: v1.7.0-TIER1
**Branch**: feature/icao-auto-sync-tier1
**Status**: Ready for Testing

---

## Prerequisites

### 1. Infrastructure Services Running

```bash
# Start PostgreSQL and OpenLDAP stack
docker compose -f docker/docker-compose.yaml up -d postgres openldap1 openldap2 haproxy

# Wait for services to be ready (30 seconds)
sleep 30

# Verify database connectivity
docker compose -f docker/docker-compose.yaml exec postgres psql -U pkd -d pkd -c "SELECT 1;"

# Verify LDAP connectivity
docker compose -f docker/docker-compose.yaml exec openldap1 ldapsearch -x -H ldap://localhost:389 -b "dc=ldap,dc=smartcoreinc,dc=com" -LLL "(objectClass=*)" dn
```

### 2. Database Migration

```bash
# Check if icao_pkd_versions table exists
docker compose -f docker/docker-compose.yaml exec postgres psql -U pkd -d pkd -c "\d icao_pkd_versions"

# If not exists, run migration script
docker compose -f docker/docker-compose.yaml exec -T postgres psql -U pkd -d pkd < docker/init-scripts/004_create_icao_versions_table.sql
```

### 3. Environment Variables

Required environment variables in `docker/docker-compose.yaml`:

```yaml
services:
  pkd-management:
    image: icao-pkd-management:test-v1.7.0
    environment:
      # Existing variables
      - DB_HOST=postgres
      - POSTGRES_DB=pkd
      - POSTGRES_USER=pkd
      - POSTGRES_PASSWORD=pkd123
      - LDAP_HOST=haproxy
      - LDAP_PORT=389
      - LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
      - LDAP_BIND_PASSWORD=admin

      # ICAO Auto Sync Configuration (NEW)
      - ICAO_PORTAL_URL=https://pkddownloadsg.icao.int/
      - ICAO_NOTIFICATION_EMAIL=admin@yourcompany.com
      - ICAO_AUTO_NOTIFY=true
      - ICAO_HTTP_TIMEOUT=10
```

---

## Testing Scenarios

### Scenario 1: Health Check

**Purpose**: Verify service starts and all endpoints are registered

```bash
# Start PKD Management service
docker compose -f docker/docker-compose.yaml up -d pkd-management

# Wait for startup (10 seconds)
sleep 10

# Check logs for ICAO module initialization
docker compose -f docker/docker-compose.yaml logs pkd-management | grep -i "icao"

# Expected output:
# [info] ICAO Auto Sync module initialized
# [info] [IcaoHandler] Initialized
# [info] [IcaoHandler] Routes registered: /api/icao/check-updates, /api/icao/latest, /api/icao/history

# Test health endpoint
curl -s http://localhost:8080/api/health | jq

# Expected: "status": "healthy"
```

### Scenario 2: Check for ICAO Portal Updates

**Purpose**: Test the core version checking functionality

```bash
# Trigger manual version check
curl -X GET http://localhost:8080/api/icao/check-updates | jq

# Expected response:
{
  "success": true,
  "message": "New versions detected and saved" | "No new versions found",
  "new_version_count": 0-2,
  "new_versions": [
    {
      "id": 1,
      "collection_type": "DSC_CRL",
      "file_name": "icaopkd-001-dsccrl-005974.ldif",
      "file_version": 5974,
      "detected_at": "2026-01-19 08:00:00",
      "status": "DETECTED",
      "status_description": "New version detected, awaiting download",
      "notification_sent": false
    }
  ]
}

# Check database
docker compose -f docker/docker-compose.yaml exec postgres psql -U pkd -d pkd -c "SELECT * FROM icao_pkd_versions ORDER BY detected_at DESC LIMIT 5;"

# Check logs for ICAO portal access
docker compose -f docker/docker-compose.yaml logs pkd-management | grep "HttpClient\|HtmlParser\|IcaoSync"
```

### Scenario 3: Get Latest Versions

**Purpose**: Retrieve latest detected version for each collection type

```bash
# Get latest versions
curl -X GET http://localhost:8080/api/icao/latest | jq

# Expected response:
{
  "success": true,
  "count": 2,
  "versions": [
    {
      "collection_type": "DSC_CRL",
      "file_version": 5973,
      "status": "DETECTED",
      ...
    },
    {
      "collection_type": "MASTERLIST",
      "file_version": 216,
      "status": "DETECTED",
      ...
    }
  ]
}
```

### Scenario 4: Get Version History

**Purpose**: Retrieve detection history with pagination

```bash
# Get recent history (default 10 records)
curl -X GET http://localhost:8080/api/icao/history | jq

# Get specific number of records
curl -X GET "http://localhost:8080/api/icao/history?limit=5" | jq

# Expected response:
{
  "success": true,
  "limit": 5,
  "count": 5,
  "versions": [
    { /* most recent */ },
    { /* ... */ }
  ]
}
```

### Scenario 5: Email Notification Testing

**Purpose**: Verify email notification system (if mail command available)

```bash
# Check if mail command is available in container
docker compose -f docker/docker-compose.yaml exec pkd-management which mail

# If available, trigger version check (should send email if new versions found)
curl -X GET http://localhost:8080/api/icao/check-updates

# Check logs for email sending
docker compose -f docker/docker-compose.yaml logs pkd-management | grep "EmailSender"

# Expected output:
# [info] [EmailSender] Sending email to: admin@yourcompany.com
# [info] [EmailSender] Email sent successfully (or fallback to log if mail unavailable)
```

### Scenario 6: Error Handling

**Purpose**: Test graceful error handling

```bash
# Test with invalid ICAO portal URL (simulate network error)
docker compose -f docker/docker-compose.yaml exec pkd-management bash -c "export ICAO_PORTAL_URL=http://invalid.example.com && curl -X GET http://localhost:8080/api/icao/check-updates"

# Expected: Graceful error response, service continues running

# Check logs for error handling
docker compose -f docker/docker-compose.yaml logs pkd-management | grep "ERROR\|Exception"
```

### Scenario 7: Cron Job Simulation

**Purpose**: Test automated daily check workflow

```bash
# Create test cron job script
cat > /tmp/test-icao-cron.sh <<'EOF'
#!/bin/bash
# Simulate daily ICAO version check

echo "=== ICAO Version Check - $(date) ===" | tee -a /tmp/icao-cron.log

# Call API
response=$(curl -s -X GET http://localhost:8080/api/icao/check-updates)

# Parse response
success=$(echo "$response" | jq -r '.success')
new_count=$(echo "$response" | jq -r '.new_version_count')

if [ "$success" == "true" ] && [ "$new_count" -gt 0 ]; then
    echo "✓ New ICAO versions detected: $new_count" | tee -a /tmp/icao-cron.log
    echo "$response" | jq '.' >> /tmp/icao-cron.log
else
    echo "○ No new versions" | tee -a /tmp/icao-cron.log
fi

echo "" >> /tmp/icao-cron.log
EOF

chmod +x /tmp/test-icao-cron.sh

# Run test
/tmp/test-icao-cron.sh

# Check log
cat /tmp/icao-cron.log
```

---

## Integration Testing Checklist

### Database Integration
- [ ] `icao_pkd_versions` table created successfully
- [ ] `uploaded_file.icao_version_id` foreign key working
- [ ] Version records insert correctly
- [ ] Status updates (DETECTED → NOTIFIED) work
- [ ] `linkToUpload()` creates proper reference

### LDAP Integration
- [ ] Service connects to HAProxy (load balanced)
- [ ] Authentication with bind DN/password works
- [ ] No impact on other LDAP operations

### API Gateway Integration
- [ ] `/api/icao/*` routes accessible through Nginx (port 8080)
- [ ] CORS headers properly set
- [ ] Rate limiting works
- [ ] Error responses properly formatted

### Email Notification
- [ ] Email config loaded from environment
- [ ] Notification sent when new versions detected
- [ ] Graceful fallback if mail command unavailable

### Error Handling
- [ ] Invalid URL handled gracefully
- [ ] Network timeout handled
- [ ] HTML parsing errors logged but don't crash service
- [ ] Database errors logged but don't stop version check

---

## Performance Benchmarks

### Expected Performance

| Operation | Expected Time | Notes |
|-----------|--------------|-------|
| HTTP Fetch (ICAO portal) | 2-5 seconds | Depends on network |
| HTML Parsing | <100ms | Regex-based |
| Database Operations | <50ms per query | INSERT/SELECT |
| Email Sending | <1 second | System mail command |
| **Total Check Cycle** | **3-6 seconds** | Once per day |

### Performance Testing

```bash
# Measure check-updates endpoint performance
time curl -X GET http://localhost:8080/api/icao/check-updates

# Expected: real time ~3-6 seconds

# Measure latest endpoint (should be fast - DB only)
time curl -X GET http://localhost:8080/api/icao/latest

# Expected: real time <200ms

# Measure history endpoint
time curl -X GET "http://localhost:8080/api/icao/history?limit=100"

# Expected: real time <500ms
```

---

## Troubleshooting

### Issue 1: Container Fails to Start

**Symptoms**: Container exits immediately

**Diagnosis**:
```bash
docker logs pkd-management --tail 100
```

**Common Causes**:
- Database not reachable (DB_HOST incorrect)
- LDAP not reachable (LDAP_HOST incorrect)
- Missing environment variables

**Solution**: Verify all infrastructure services are running and environment variables are correct

---

### Issue 2: "No such table: icao_pkd_versions"

**Symptoms**: API returns database error

**Diagnosis**:
```bash
docker compose exec postgres psql -U pkd -d pkd -c "\dt icao_*"
```

**Solution**:
```bash
# Run migration script
docker compose exec -T postgres psql -U pkd -d pkd < docker/init-scripts/004_create_icao_versions_table.sql
```

---

### Issue 3: HTTP Fetch Fails

**Symptoms**: "Can't contact LDAP server" or timeout errors

**Diagnosis**:
```bash
# Test ICAO portal accessibility from container
docker compose exec pkd-management curl -I https://pkddownloadsg.icao.int/

# Check network connectivity
docker compose exec pkd-management ping -c 3 pkddownloadsg.icao.int
```

**Solution**:
- Check firewall rules
- Verify DNS resolution
- Check proxy settings if behind corporate proxy

---

### Issue 4: Email Notifications Not Sent

**Symptoms**: No email received, but logs show "Notification sent"

**Diagnosis**:
```bash
# Check if mail command exists
docker compose exec pkd-management which mail

# Check mail logs
docker compose exec pkd-management tail -f /var/log/mail.log
```

**Solution**:
- If `mail` command not available, notifications will only be logged
- Install `mailutils` package or configure SMTP server
- Update `email_sender.cpp` for production SMTP integration

---

### Issue 5: HTML Parsing Returns Empty Results

**Symptoms**: API returns `new_version_count: 0` but ICAO portal has versions

**Diagnosis**:
```bash
# Check HTML structure manually
curl -s https://pkddownloadsg.icao.int/ | grep -i "icaopkd.*dsccrl.*ldif"

# Check logs for parsing errors
docker compose logs pkd-management | grep "HtmlParser"
```

**Solution**:
- ICAO portal HTML structure may have changed
- Update regex patterns in `html_parser.cpp`
- Consider implementing more robust HTML parsing (libxml2)

---

## Success Criteria

### Compilation Phase ✅ (Completed)
- [x] All source files compile without errors
- [x] Docker image builds successfully (157MB)
- [x] All dependencies linked correctly

### Runtime Phase (To be completed)
- [ ] Service starts without crashes
- [ ] All API endpoints respond with valid JSON
- [ ] Database operations work correctly
- [ ] ICAO portal HTML fetching succeeds
- [ ] Version parsing extracts correct numbers
- [ ] Email notification sent (or logged if mail unavailable)
- [ ] No performance degradation to existing endpoints

### Integration Phase (To be completed)
- [ ] Works with full docker-compose stack
- [ ] API Gateway routes traffic correctly
- [ ] Cron job script executes successfully
- [ ] Frontend widget displays ICAO status

---

## Next Steps After Testing

1. **Frontend Integration**:
   - Create ICAO status widget component
   - Add to Dashboard page
   - Display latest version info
   - Show notification history

2. **Cron Job Setup**:
   - Create production cron script
   - Configure daily execution (0 8 * * *)
   - Set up log rotation

3. **Deployment**:
   - Update docker-compose.yaml with new image
   - Update environment variables
   - Deploy to staging
   - User acceptance testing
   - Deploy to production

4. **Monitoring**:
   - Add metrics (Prometheus/Grafana)
   - Set up alerts for failures
   - Dashboard for version tracking

---

## Documentation Updates Required

- [ ] Update `CLAUDE.md` - Add ICAO Auto Sync section
- [ ] Update `README.md` - Add new environment variables
- [ ] Create `docs/ICAO_AUTO_SYNC_USER_GUIDE.md`
- [ ] Update OpenAPI specs - Add `/api/icao/*` endpoints
- [ ] Add Swagger annotations to handlers

---

**Document Status**: Testing Guide
**Created**: 2026-01-19
**Ready for**: Runtime Testing → Integration Testing → Deployment
