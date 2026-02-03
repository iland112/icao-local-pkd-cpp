// =============================================================================
// 권장 Public Endpoints 설정
// =============================================================================
// File: services/pkd-management/src/middleware/auth_middleware.cpp
// Version: v2.3.2
// Last Updated: 2026-02-02
//
// 이 설정은 다음 원칙에 따라 구성되었습니다:
// 1. Read-only 조회 기능은 Public (민감 정보 제외)
// 2. 데이터 변경 작업은 인증 필수 (POST/PUT/DELETE)
// 3. 관리자 기능은 인증 필수
// 4. Demo/Verification 기능은 Public (PA Service)
// =============================================================================

std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    // ========================================================================
    // System & Authentication
    // ========================================================================
    "^/api/health.*",              // Health check endpoints
    "^/api/auth/login$",           // Login endpoint
    "^/api/auth/register$",        // Registration endpoint (future)

    // ========================================================================
    // Dashboard & Statistics (Read-only public information)
    // ========================================================================
    "^/api/upload/countries$",     // Dashboard country statistics (homepage)

    // ========================================================================
    // Certificate Search (Public directory service)
    // ========================================================================
    "^/api/certificates/countries$", // Country list for certificate search
    "^/api/certificates/search.*",   // Certificate search with filters
    // Note: Export endpoints (/api/certificates/export/*) require authentication

    // ========================================================================
    // ICAO PKD Version Monitoring (Read-only public information)
    // ========================================================================
    "^/api/icao/status$",          // ICAO version status comparison
    "^/api/icao/latest$",          // Latest ICAO version information
    "^/api/icao/history.*",        // Version check history
    // Note: /api/icao/check-updates (POST) requires authentication

    // ========================================================================
    // Sync Dashboard (Read-only monitoring)
    // ========================================================================
    "^/api/sync/status$",          // DB-LDAP sync status
    "^/api/sync/stats$",           // Sync statistics
    "^/api/reconcile/history.*",   // Reconciliation history
    // Note: /api/sync/check and /api/sync/reconcile (POST) require authentication

    // ========================================================================
    // PA (Passive Authentication) Service (Demo/Verification functionality)
    // ========================================================================
    // Core verification endpoints
    "^/api/pa/verify$",            // PA verification (main function)
    "^/api/pa/parse-sod$",         // Parse SOD (Security Object Document)
    "^/api/pa/parse-dg1$",         // Parse DG1 (MRZ data)
    "^/api/pa/parse-dg2$",         // Parse DG2 (Face image)
    "^/api/pa/parse-mrz-text$",    // Parse MRZ text

    // PA history and statistics (Read-only)
    "^/api/pa/history.*",          // PA verification history
    "^/api/pa/statistics$",        // PA statistics
    "^/api/pa/[a-f0-9\\-]+$",      // PA verification detail by ID (UUID)
    "^/api/pa/[a-f0-9\\-]+/datagroups$", // DataGroups detail

    // ========================================================================
    // Static Files & Documentation
    // ========================================================================
    "^/static/.*",                 // Static files (CSS, JS, images)
    "^/api-docs.*",                // API documentation
    "^/swagger-ui/.*"              // Swagger UI

    // ========================================================================
    // IMPORTANT: Endpoints that REQUIRE authentication (NOT in this list)
    // ========================================================================
    // File Operations:
    //   - /api/upload/ldif (POST)
    //   - /api/upload/masterlist (POST)
    //   - /api/upload/history (GET - detailed with user filter)
    //   - /api/upload/{id} (GET/DELETE)
    //   - /api/upload/statistics (GET - detailed statistics)
    //
    // Certificate Operations:
    //   - /api/certificates/export/country (GET)
    //   - /api/certificates/export/file (GET)
    //
    // Sync Operations:
    //   - /api/sync/check (POST)
    //   - /api/sync/reconcile (POST)
    //
    // ICAO Operations:
    //   - /api/icao/check-updates (POST)
    //
    // Admin Operations:
    //   - /api/auth/users (GET/POST/PUT/DELETE)
    //   - /api/audit/operations (GET) - REMOVED from public
    //   - /api/audit/operations/stats (GET) - REMOVED from public
    //
    // User Operations:
    //   - /api/auth/profile (GET/PUT)
    //   - /api/auth/password (PUT)
    // ========================================================================
};

// =============================================================================
// Security Notes
// =============================================================================
//
// 1. Rate Limiting Recommended:
//    - PA Verify: 10 requests/minute per IP
//    - Certificate Search: 30 requests/minute per IP
//    - General API: 60 requests/minute per IP
//    Implement in nginx configuration
//
// 2. Public Endpoints Risk Assessment:
//    - Certificate Search: Medium risk (large data exposure)
//    - PA Verify: Medium risk (computation intensive)
//    - Upload History: High risk - MUST require authentication
//    - Audit Logs: High risk - MUST require authentication
//
// 3. Monitoring Required:
//    - Track public endpoint usage
//    - Monitor for abuse patterns
//    - Alert on unusual traffic
//
// 4. Future Enhancements:
//    - Implement API key system for public endpoints
//    - Add CAPTCHA for PA Verify
//    - Implement result pagination limits
//
// =============================================================================
// Change Log
// =============================================================================
// v2.3.2 (2026-02-02):
//   - Added /api/certificates/countries and /api/certificates/search
//   - Added ICAO monitoring endpoints
//   - Added Sync monitoring endpoints
//   - Added PA Service endpoints
//   - REMOVED /api/audit/.* from public (security enhancement)
//
// v2.3.1 (2026-02-02):
//   - Added /api/upload/countries for dashboard
//   - Added TEMPORARY /api/audit/.* (to be removed)
//
// =============================================================================
