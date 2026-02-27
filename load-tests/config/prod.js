/**
 * k6 Load Test - Production Environment Configuration
 *
 * Target: 10.0.0.220 (RHEL 9, Podman, Oracle XE 21c)
 * Domain: pkd.smartcoreinc.com
 */

export const ENV = {
  name: 'production',
  baseUrl: 'https://pkd.smartcoreinc.com',
  host: '10.0.0.220',

  // Auth credentials (admin user)
  username: 'admin',
  password: 'admin123',

  // System limits (production defaults)
  limits: {
    nginxWorkerConnections: 1024,
    nginxLimitConnPerIp: 20,
    nginxApiRatePerIp: 100,       // req/s
    oracleSessionPoolMax: 10,
    ldapPoolMax: 10,
    drogonThreads: 4,
    maxConcurrentUploads: 3,
  },

  // Tuned limits (for stress/peak tests)
  tunedLimits: {
    nginxWorkerConnections: 16384,
    nginxLimitConnPerIp: 5000,
    nginxApiRatePerIp: 10000,
    oracleSessionPoolMax: 50,
    ldapPoolMax: 50,
    drogonThreads: 16,
  },

  // Production data counts
  data: {
    totalCertificates: 31212,
    csca: 845,
    dsc: 29838,
    dscNc: 502,
    mlsc: 27,
    crl: 69,
    countries: 95,
  },
};
