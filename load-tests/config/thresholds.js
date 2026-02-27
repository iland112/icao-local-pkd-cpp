/**
 * k6 Load Test - Pass/Fail Thresholds
 *
 * Per-endpoint P95 latency targets and global error rate limits.
 */

export const THRESHOLDS = {
  // --- Global metrics ---
  http_req_duration: [
    'p(50)<500',    // 50th percentile < 500ms
    'p(90)<2000',   // 90th percentile < 2s
    'p(95)<5000',   // 95th percentile < 5s
    'p(99)<10000',  // 99th percentile < 10s
  ],
  http_req_failed: ['rate<0.05'],    // Overall error rate < 5%
  http_req_blocked: ['p(95)<100'],   // Connection acquisition < 100ms
  checks: ['rate>0.95'],              // 95%+ checks must pass

  // --- Per-endpoint latency (tagged via { endpoint: 'xxx' }) ---
  'http_req_duration{endpoint:health}':        ['p(95)<200'],
  'http_req_duration{endpoint:cert_search}':   ['p(95)<3000'],
  'http_req_duration{endpoint:country_list}':  ['p(95)<1000'],
  'http_req_duration{endpoint:upload_stats}':  ['p(95)<2000'],
  'http_req_duration{endpoint:upload_countries}': ['p(95)<2000'],
  'http_req_duration{endpoint:dsc_nc_report}': ['p(95)<10000'],
  'http_req_duration{endpoint:crl_report}':    ['p(95)<10000'],
  'http_req_duration{endpoint:doc9303}':       ['p(95)<5000'],
  'http_req_duration{endpoint:pa_history}':    ['p(95)<3000'],
  'http_req_duration{endpoint:pa_stats}':      ['p(95)<3000'],
  'http_req_duration{endpoint:ai_stats}':      ['p(95)<5000'],
  'http_req_duration{endpoint:ai_anomalies}':  ['p(95)<5000'],
  'http_req_duration{endpoint:ai_reports}':    ['p(95)<5000'],
  'http_req_duration{endpoint:icao_status}':   ['p(95)<2000'],
  'http_req_duration{endpoint:sync_status}':   ['p(95)<2000'],
  'http_req_duration{endpoint:pa_lookup}':     ['p(95)<3000'],
  'http_req_duration{endpoint:login}':         ['p(95)<2000'],
  'http_req_duration{endpoint:sync_check}':    ['p(95)<5000'],

  // --- Custom metrics ---
  'rate_limit_429': ['count<100'],       // Minimal rate limit hits
  'server_errors_5xx': ['count<50'],     // Minimal server errors
  'success_rate': ['rate>0.95'],          // 95%+ success
};

// Phase-specific threshold overrides
export const SMOKE_THRESHOLDS = {
  http_req_failed: ['rate<0.02'],   // 2% — nginx login rate limit (5r/m) 503 허용
  http_req_duration: ['p(95)<1000'],
  checks: ['rate>0.98'],            // 98% — login rate limit 실패 허용
};

export const BASELINE_THRESHOLDS = {
  http_req_failed: ['rate<0.05'],   // 5% — nginx login rate limit (5r/m) 503 허용 (튜닝 전)
  http_req_duration: ['p(95)<3000'], // 3s — CPU-heavy 엔드포인트 (dsc-nc, crl report) 허용
  checks: ['rate>0.95'],             // 95% — rate limit 실패 허용
};

export const STRESS_THRESHOLDS = {
  http_req_failed: ['rate<0.10'],
  http_req_duration: ['p(95)<10000'],
  checks: ['rate>0.90'],
};

export const PEAK_THRESHOLDS = {
  http_req_failed: ['rate<0.15'],
  http_req_duration: ['p(95)<15000'],
  checks: ['rate>0.85'],
};
