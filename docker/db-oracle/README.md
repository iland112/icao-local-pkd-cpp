# Oracle Database XE 21c Setup

## Overview

Oracle Database Express Edition (XE) 21c is configured as an optional database backend for ICAO Local PKD. This allows testing and production deployment with Oracle while maintaining compatibility with PostgreSQL.

## Docker Configuration

### Service Name
`oracle`

### Ports
- **1521**: Oracle database listener (mapped to 11521 externally)
- **5500**: Oracle Enterprise Manager Express (mapped to 15500 externally)

### Environment Variables

| Variable | Value | Description |
|----------|-------|-------------|
| `ORACLE_PASSWORD` | From `.env` | SYS/SYSTEM password |
| `ORACLE_CHARACTERSET` | AL32UTF8 | UTF-8 encoding for international support |
| `TZ` | Asia/Seoul | Timezone |

### Data Persistence

- **Volume**: `.docker-data/oracle` → `/opt/oracle/oradata`
- First-time initialization takes 3-5 minutes
- Data persists across container restarts

## Initialization Scripts

Scripts in `docker/db-oracle/init/` run automatically on first startup:

### 01-create-user.sql
- Creates `pkd_data` tablespace (100MB, auto-extend)
- Creates `pkd_user` with password `pkd_password`
- Grants necessary privileges:
  - `CONNECT`, `RESOURCE`
  - Table operations (SELECT, INSERT, UPDATE, DELETE)
  - CLOB/BLOB operations (for certificate data)
  - Unlimited tablespace quota

### 02-schema.sql
- Placeholder for Phase 4.3 schema migration
- Will contain complete DDL for 22 tables

## Starting Oracle Container

### Option 1: Start with oracle profile
```bash
cd docker
docker compose --profile oracle up -d oracle
```

### Option 2: Start entire system with Oracle
```bash
cd docker
docker compose --profile oracle up -d
```

## Health Check

Oracle container health check runs every 30 seconds:
```sql
SELECT 1 FROM DUAL;
```

Health check allows 10 retries (5 minutes) for initial startup.

## Connection Information

### Internal (from other containers)
- **Host**: `oracle`
- **Port**: `1521`
- **Service Name**: `XE`
- **User**: `pkd_user`
- **Password**: `pkd_password`

### External (from host machine)
- **Host**: `localhost`
- **Port**: `11521`
- **Service Name**: `XE`
- **User**: `pkd_user`
- **Password**: `pkd_password`

### Connection Strings

**SQL*Plus**:
```bash
sqlplus pkd_user/pkd_password@//localhost:11521/XE
```

**JDBC**:
```
jdbc:oracle:thin:@//localhost:11521/XE
```

**OCI**:
```
oracle:11521/XE
```

## Oracle Enterprise Manager Express

Access web interface at: http://localhost:15500/em

## Shared Memory

Oracle requires shared memory for performance:
```yaml
shm_size: 1g
```

## Migration Status

- **Phase 4.1**: ✅ Docker environment complete
- **Phase 4.2**: ⏳ OracleQueryExecutor implementation pending
- **Phase 4.3**: ⏳ Schema migration pending
- **Phase 4.4**: ⏳ Environment-based DB selection pending

## Troubleshooting

### Container won't start
- Check Docker has at least 2GB RAM allocated
- Verify `.env` file has `ORACLE_PASSWORD` set
- Check logs: `docker logs icao-local-pkd-oracle`

### Initialization hangs
- First startup takes 3-5 minutes
- Wait for health check to pass: `docker ps` shows "healthy"

### Cannot connect
- Verify container is healthy: `docker ps | grep oracle`
- Check listener status: `docker exec icao-local-pkd-oracle lsnrctl status`
- Test connection: `docker exec icao-local-pkd-oracle sqlplus pkd_user/pkd_password@XE`

## Next Steps (Phase 4.2+)

1. Implement `OracleQueryExecutor` class (~500 lines)
2. Convert PostgreSQL DDL to Oracle DDL (22 tables)
3. Add `DB_TYPE` environment variable support
4. Test all 5 repositories with Oracle backend
5. Performance benchmarking vs PostgreSQL
