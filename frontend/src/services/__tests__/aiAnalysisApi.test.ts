/**
 * Tests for aiAnalysisApi.ts
 *
 * aiAnalysisApi delegates all HTTP calls to pkdApi (the shared axios instance
 * exported from pkdApi.ts).  We mock that module so every test only verifies
 * that the correct URL and params are forwarded — no real network calls.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist a mock pkdApi object before any module is imported ----
const { mockPkdApi } = vi.hoisted(() => {
  const mock = {
    get: vi.fn(),
    post: vi.fn(),
  };
  return { mockPkdApi: mock };
});

vi.mock('@/services/pkdApi', () => ({
  default: mockPkdApi,
}));

import { aiAnalysisApi } from '../aiAnalysisApi';

beforeEach(() => {
  vi.clearAllMocks();
});

// ---------------------------------------------------------------------------
// getHealth
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getHealth', () => {
  it('should GET /ai/health', () => {
    aiAnalysisApi.getHealth();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/health');
  });
});

// ---------------------------------------------------------------------------
// triggerAnalysis
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.triggerAnalysis', () => {
  it('should POST /ai/analyze', () => {
    aiAnalysisApi.triggerAnalysis();
    expect(mockPkdApi.post).toHaveBeenCalledWith('/ai/analyze');
  });
});

// ---------------------------------------------------------------------------
// getAnalysisStatus
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getAnalysisStatus', () => {
  it('should GET /ai/analyze/status', () => {
    aiAnalysisApi.getAnalysisStatus();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/analyze/status');
  });
});

// ---------------------------------------------------------------------------
// getCertificateAnalysis
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getCertificateAnalysis', () => {
  it('should GET /ai/certificate/{fingerprint}', () => {
    aiAnalysisApi.getCertificateAnalysis('abc123def456');
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/certificate/abc123def456');
  });

  it('should embed the fingerprint directly in the URL path', () => {
    aiAnalysisApi.getCertificateAnalysis('deadbeef0102');
    const [url] = mockPkdApi.get.mock.calls[0];
    expect(url).toBe('/ai/certificate/deadbeef0102');
  });
});

// ---------------------------------------------------------------------------
// getAnomalies
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getAnomalies', () => {
  it('should GET /ai/anomalies with no params when called without arguments', () => {
    aiAnalysisApi.getAnomalies();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/anomalies', { params: undefined, signal: undefined });
  });

  it('should pass country, type, label, risk_level, page, size filters', () => {
    const params = { country: 'KR', type: 'DSC', label: 'ANOMALOUS', risk_level: 'HIGH', page: 1, size: 20 };
    aiAnalysisApi.getAnomalies(params);
    const [url, config] = mockPkdApi.get.mock.calls[0];
    expect(url).toBe('/ai/anomalies');
    expect(config.params).toEqual(params);
  });

  it('should forward an AbortSignal when provided', () => {
    const controller = new AbortController();
    aiAnalysisApi.getAnomalies(undefined, controller.signal);
    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.signal).toBe(controller.signal);
  });
});

// ---------------------------------------------------------------------------
// getStatistics
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getStatistics', () => {
  it('should GET /ai/statistics', () => {
    aiAnalysisApi.getStatistics();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/statistics');
  });
});

// ---------------------------------------------------------------------------
// getCountryMaturity
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getCountryMaturity', () => {
  it('should GET /ai/reports/country-maturity', () => {
    aiAnalysisApi.getCountryMaturity();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/country-maturity');
  });
});

// ---------------------------------------------------------------------------
// getAlgorithmTrends
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getAlgorithmTrends', () => {
  it('should GET /ai/reports/algorithm-trends', () => {
    aiAnalysisApi.getAlgorithmTrends();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/algorithm-trends');
  });
});

// ---------------------------------------------------------------------------
// getKeySizeDistribution
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getKeySizeDistribution', () => {
  it('should GET /ai/reports/key-size-distribution', () => {
    aiAnalysisApi.getKeySizeDistribution();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/key-size-distribution');
  });
});

// ---------------------------------------------------------------------------
// getRiskDistribution
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getRiskDistribution', () => {
  it('should GET /ai/reports/risk-distribution', () => {
    aiAnalysisApi.getRiskDistribution();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/risk-distribution');
  });
});

// ---------------------------------------------------------------------------
// getCountryReport
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getCountryReport', () => {
  it('should GET /ai/reports/country/{code}', () => {
    aiAnalysisApi.getCountryReport('KR');
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/country/KR');
  });

  it('should embed the country code directly in the URL path', () => {
    aiAnalysisApi.getCountryReport('DE');
    const [url] = mockPkdApi.get.mock.calls[0];
    expect(url).toBe('/ai/reports/country/DE');
  });
});

// ---------------------------------------------------------------------------
// getCertificateForensic
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getCertificateForensic', () => {
  it('should GET /ai/certificate/{fingerprint}/forensic', () => {
    aiAnalysisApi.getCertificateForensic('fp123');
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/certificate/fp123/forensic');
  });
});

// ---------------------------------------------------------------------------
// triggerIncrementalAnalysis
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.triggerIncrementalAnalysis', () => {
  it('should POST /ai/analyze/incremental with upload_id', () => {
    aiAnalysisApi.triggerIncrementalAnalysis('upload-uuid-001');
    expect(mockPkdApi.post).toHaveBeenCalledWith(
      '/ai/analyze/incremental',
      { upload_id: 'upload-uuid-001' }
    );
  });

  it('should POST with upload_id undefined when no argument given', () => {
    aiAnalysisApi.triggerIncrementalAnalysis();
    const [url, body] = mockPkdApi.post.mock.calls[0];
    expect(url).toBe('/ai/analyze/incremental');
    expect(body).toEqual({ upload_id: undefined });
  });
});

// ---------------------------------------------------------------------------
// getIssuerProfiles
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getIssuerProfiles', () => {
  it('should GET /ai/reports/issuer-profiles', () => {
    aiAnalysisApi.getIssuerProfiles();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/issuer-profiles');
  });
});

// ---------------------------------------------------------------------------
// getForensicSummary
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getForensicSummary', () => {
  it('should GET /ai/reports/forensic-summary', () => {
    aiAnalysisApi.getForensicSummary();
    expect(mockPkdApi.get).toHaveBeenCalledWith('/ai/reports/forensic-summary');
  });
});

// ---------------------------------------------------------------------------
// getExtensionAnomalies
// ---------------------------------------------------------------------------

describe('aiAnalysisApi.getExtensionAnomalies', () => {
  it('should GET /ai/reports/extension-anomalies with no params when called without arguments', () => {
    aiAnalysisApi.getExtensionAnomalies();
    expect(mockPkdApi.get).toHaveBeenCalledWith(
      '/ai/reports/extension-anomalies',
      { params: undefined }
    );
  });

  it('should pass type, country, and limit filters', () => {
    const params = { type: 'DSC', country: 'KR', limit: 50 };
    aiAnalysisApi.getExtensionAnomalies(params);
    const [url, config] = mockPkdApi.get.mock.calls[0];
    expect(url).toBe('/ai/reports/extension-anomalies');
    expect(config.params).toEqual(params);
  });
});
