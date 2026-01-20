# ICAO Auto Sync - Cron Job Setup Guide

**Version**: 1.0.0
**Date**: 2026-01-20
**Purpose**: Configure daily automatic version checks

---

## Overview

The ICAO Auto Sync feature includes a cron job script (`scripts/icao-version-check.sh`) that automatically checks for new ICAO PKD versions daily.

**Schedule**: Every day at 8:00 AM
**Cron Expression**: `0 8 * * *`

---

## Prerequisites

### 1. Required Tools

```bash
# Check if tools are installed
which curl jq

# Install if missing (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install -y curl jq

# Install if missing (RHEL/CentOS)
sudo yum install -y curl jq
```

### 2. Docker Services Running

Ensure all ICAO Local PKD services are running:

```bash
docker compose -f docker/docker-compose.yaml ps

# Expected: All services in "running" or "healthy" state
# - postgres
# - openldap1, openldap2
# - haproxy
# - pkd-management (with ICAO module)
# - pa-service
# - sync-service
# - api-gateway
# - frontend
```

### 3. Script Permissions

```bash
chmod +x /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh
```

---

## Cron Job Installation

### Method 1: User Crontab (Recommended)

```bash
# Open crontab editor
crontab -e

# Add the following line:
# Daily ICAO version check at 8:00 AM
0 8 * * * /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh >> /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/cron.log 2>&1

# Save and exit (Ctrl+X, then Y, then Enter)
```

### Method 2: System Crontab

```bash
# Create cron file
sudo tee /etc/cron.d/icao-pkd-sync << 'EOF'
# ICAO PKD Auto Sync - Daily Version Check
# Runs every day at 8:00 AM
SHELL=/bin/bash
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
API_GATEWAY_URL=http://localhost:8080

0 8 * * * kbjung /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh >> /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/cron.log 2>&1
EOF

# Set permissions
sudo chmod 644 /etc/cron.d/icao-pkd-sync

# Restart cron service
sudo systemctl restart cron  # Ubuntu/Debian
# or
sudo systemctl restart crond  # RHEL/CentOS
```

### Method 3: Systemd Timer (Alternative)

#### Create Service Unit

```bash
sudo tee /etc/systemd/system/icao-pkd-sync.service << 'EOF'
[Unit]
Description=ICAO PKD Auto Sync - Version Check
After=network.target docker.service

[Service]
Type=oneshot
User=kbjung
Group=kbjung
Environment="API_GATEWAY_URL=http://localhost:8080"
ExecStart=/home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh
StandardOutput=append:/home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/systemd.log
StandardError=append:/home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/systemd.log
EOF
```

#### Create Timer Unit

```bash
sudo tee /etc/systemd/system/icao-pkd-sync.timer << 'EOF'
[Unit]
Description=ICAO PKD Auto Sync - Daily Timer
Requires=icao-pkd-sync.service

[Timer]
OnCalendar=daily
OnCalendar=*-*-* 08:00:00
Persistent=true

[Install]
WantedBy=timers.target
EOF
```

#### Enable and Start Timer

```bash
sudo systemctl daemon-reload
sudo systemctl enable icao-pkd-sync.timer
sudo systemctl start icao-pkd-sync.timer

# Check status
sudo systemctl status icao-pkd-sync.timer
sudo systemctl list-timers icao-pkd-sync.timer
```

---

## Configuration

### Environment Variables

The script supports the following environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `API_GATEWAY_URL` | `http://localhost:8080` | API Gateway base URL |

**Example**:
```bash
# Set in crontab
0 8 * * * API_GATEWAY_URL=http://192.168.100.11:8080 /path/to/icao-version-check.sh
```

### Log Retention

Default retention: **30 days**

To change, modify the script:
```bash
vim /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh

# Line 21:
MAX_LOG_RETENTION_DAYS=30  # Change to desired value
```

---

## Verification

### 1. Manual Test Run

```bash
cd /home/kbjung/projects/c/icao-local-pkd
./scripts/icao-version-check.sh
```

**Expected Output**:
```
[INFO] =========================================
[INFO] ICAO PKD Auto Sync - Daily Version Check
[INFO] =========================================
[INFO] Timestamp: 2026-01-20 08:00:00
[INFO] API Gateway: http://localhost:8080
[INFO] Log File: /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/icao-version-check-20260120_080000.log

[INFO] Checking prerequisites...
[SUCCESS] Prerequisites check passed

[INFO] Triggering ICAO version check via API Gateway...
[INFO] API Response Code: 200
[INFO] API Response Body: {"success":true,"new_version_count":0,...}
[SUCCESS] Version check triggered successfully

[INFO] Waiting for async processing to complete (5 seconds)...

[INFO] Fetching latest detected versions...
[SUCCESS] Found 2 version(s)
[INFO]   - DSC_CRL: icaopkd-001-dsccrl-009668.ldif (v9668) - Status: DETECTED
[INFO]   - MASTERLIST: icaopkd-002-ml-000334.ldif (v334) - Status: DETECTED

[SUCCESS] Version check completed successfully
[INFO] =========================================
[INFO] Script execution completed
[INFO] =========================================
```

### 2. Cron Job Verification

```bash
# List crontab entries
crontab -l | grep icao

# Or check system crontab
cat /etc/cron.d/icao-pkd-sync

# Check cron logs (Ubuntu/Debian)
grep icao /var/log/syslog

# Check cron logs (RHEL/CentOS)
grep icao /var/log/cron
```

### 3. Check Log Files

```bash
# List log files
ls -lh /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/

# View latest log
tail -f /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/icao-version-check-*.log
```

### 4. Systemd Timer Verification (if using systemd)

```bash
# Check timer status
systemctl status icao-pkd-sync.timer

# Check last execution
systemctl status icao-pkd-sync.service

# View logs
journalctl -u icao-pkd-sync.service -f
```

---

## Monitoring

### Log File Location

All logs are stored in:
```
/home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/
```

**Log Files**:
- `icao-version-check-YYYYMMDD_HHMMSS.log` - Execution logs
- `cron.log` - Cron job output (if using crontab)
- `systemd.log` - Systemd output (if using systemd timer)

### Log Rotation

Automatic cleanup is performed by the script:
- Retention period: 30 days (configurable)
- Cleanup runs at the end of each execution

**Manual cleanup**:
```bash
find /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/ \
  -name "icao-version-check-*.log" \
  -type f \
  -mtime +30 \
  -delete
```

### Monitoring Dashboard

Access the ICAO Status page in the web UI:
```
http://localhost:3000/icao
```

Features:
- Latest detected versions
- Version detection history
- Manual check-updates button
- Status lifecycle tracking

---

## Troubleshooting

### Issue 1: Script Not Executing

**Symptoms**: No log files created, cron job doesn't run

**Solutions**:
```bash
# 1. Check script permissions
ls -l /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh
# Should show: -rwxr-xr-x

# 2. Check cron service
sudo systemctl status cron  # Ubuntu/Debian
sudo systemctl status crond  # RHEL/CentOS

# 3. Check crontab syntax
crontab -l | grep icao

# 4. Test script manually
/home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh
```

### Issue 2: API Gateway Unreachable

**Symptoms**: "API Gateway at http://localhost:8080 is not reachable"

**Solutions**:
```bash
# 1. Check if API Gateway is running
docker compose -f docker/docker-compose.yaml ps api-gateway

# 2. Test API Gateway health
curl http://localhost:8080/health

# 3. Check PKD Management service
curl http://localhost:8080/api/health

# 4. Verify ICAO endpoints
curl http://localhost:8080/api/icao/latest
```

### Issue 3: HTTP 405 Method Not Allowed

**Symptoms**: "Failed to trigger version check. HTTP 405"

**Solution**: The endpoint only supports GET requests (not POST):
```bash
# Correct
curl http://localhost:8080/api/icao/check-updates

# Incorrect
curl -X POST http://localhost:8080/api/icao/check-updates
```

### Issue 4: Permission Denied

**Symptoms**: "Permission denied" when running script

**Solutions**:
```bash
# 1. Make script executable
chmod +x /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh

# 2. Check log directory permissions
mkdir -p /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync
chmod 755 /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync

# 3. Run as correct user
whoami  # Should be 'kbjung' or appropriate user
```

### Issue 5: jq Not Found

**Symptoms**: "jq is not installed. JSON parsing will be limited."

**Solution**: Install jq (optional but recommended):
```bash
# Ubuntu/Debian
sudo apt-get install jq

# RHEL/CentOS
sudo yum install jq

# macOS
brew install jq
```

---

## Security Considerations

### 1. Script Permissions

Ensure only authorized users can execute the script:
```bash
chmod 750 /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh
chown kbjung:kbjung /home/kbjung/projects/c/icao-local-pkd/scripts/icao-version-check.sh
```

### 2. Log File Permissions

Protect log files from unauthorized access:
```bash
chmod 640 /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/*.log
```

### 3. API Gateway Access

If API Gateway is exposed externally, consider:
- Using HTTPS instead of HTTP
- Implementing API authentication
- Restricting access by IP address

### 4. Cron Job User

Run cron job with minimal privileges:
```bash
# Avoid running as root
# Use dedicated service account if possible
```

---

## Best Practices

### 1. Test Before Deployment

Always test the script manually before adding to crontab:
```bash
./scripts/icao-version-check.sh
echo $?  # Should be 0 (success)
```

### 2. Monitor Initially

After deployment, monitor logs for the first week:
```bash
tail -f /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/cron.log
```

### 3. Set Up Alerts

Consider setting up alerts for failures:
```bash
# Example: Email notification on failure
0 8 * * * /path/to/icao-version-check.sh || echo "ICAO check failed" | mail -s "Alert" admin@example.com
```

### 4. Document Changes

Keep a log of configuration changes:
```bash
# Add comments to crontab
# Modified: 2026-01-20 - Changed schedule from 6 AM to 8 AM
0 8 * * * /path/to/icao-version-check.sh
```

### 5. Regular Review

Review logs monthly:
- Check for errors
- Verify version detections
- Monitor execution time
- Review log file sizes

---

## Alternative Schedules

### Twice Daily (8 AM and 8 PM)

```bash
0 8,20 * * * /path/to/icao-version-check.sh
```

### Every 6 Hours

```bash
0 */6 * * * /path/to/icao-version-check.sh
```

### Weekdays Only

```bash
0 8 * * 1-5 /path/to/icao-version-check.sh
```

### First Day of Month

```bash
0 8 1 * * /path/to/icao-version-check.sh
```

---

## Uninstallation

### Remove User Crontab Entry

```bash
crontab -e
# Delete the ICAO line, save and exit
```

### Remove System Crontab

```bash
sudo rm /etc/cron.d/icao-pkd-sync
```

### Remove Systemd Timer

```bash
sudo systemctl stop icao-pkd-sync.timer
sudo systemctl disable icao-pkd-sync.timer
sudo rm /etc/systemd/system/icao-pkd-sync.service
sudo rm /etc/systemd/system/icao-pkd-sync.timer
sudo systemctl daemon-reload
```

### Remove Logs (Optional)

```bash
rm -rf /home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/
```

---

## FAQ

**Q: Why does the script take 5 seconds to complete?**
A: The script waits for async processing to complete before fetching results.

**Q: Can I change the schedule?**
A: Yes, edit the crontab or systemd timer configuration.

**Q: What happens if the script fails?**
A: Errors are logged to the log file. Check logs for details. The next scheduled run will retry.

**Q: Does the script send email notifications?**
A: Not by default. The ICAO module logs notifications to console. Configure SMTP for email delivery.

**Q: Can I run the script more frequently?**
A: Yes, but be respectful of ICAO server load. Recommended: No more than once every 6 hours.

**Q: What if a new version is detected?**
A: The status will be "DETECTED". Check the ICAO Status page (`/icao`) for download instructions.

---

## Support

For issues or questions:
1. Check logs: `/home/kbjung/projects/c/icao-local-pkd/logs/icao-sync/`
2. Review troubleshooting section
3. Test script manually
4. Check API Gateway and services status

---

**Document Version**: 1.0.0
**Last Updated**: 2026-01-20
**Author**: SmartCore Inc.
