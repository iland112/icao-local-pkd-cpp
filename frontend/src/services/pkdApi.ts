/**
 * PKD Management Service API
 *
 * Handles communication with PKD Management Service (port 8081)
 * Responsibilities:
 * - System health monitoring
 * - Certificate search & export
 * - Upload history & statistics (read-only views)
 * - LDAP direct queries
 *
 * @version 2.0.0
 * @since Phase 6 - Frontend API Refactoring
 */

import axios, { type AxiosError, type AxiosResponse } from 'axios';
import type {
  ApiResponse,
  HealthStatus,
  UploadedFile,
  UploadStatisticsOverview,
  UploadChangesResponse,
  PageRequest,
  PageResponse,
} from '@/types';

// =============================================================================
// Axios Instance
// =============================================================================

/**
 * PKD API client
 * Base URL: /api (PKD Management Service)
 */
const pkdApi = axios.create({
  baseURL: '/api',
  timeout: 30000, // 30 seconds
  headers: {
    'Content-Type': 'application/json',
  },
});

// Response interceptor for error handling
pkdApi.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    console.error('[PKD API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// =============================================================================
// Health Check APIs
// =============================================================================

/**
 * System Health Monitoring
 */
export const healthApi = {
  /**
   * Get overall application health
   * @returns Health status including database and LDAP
   */
  check: () => pkdApi.get<HealthStatus>('/health'),

  /**
   * Check PostgreSQL database connection
   * @returns Database status and version
   */
  checkDatabase: () => pkdApi.get<{ status: string; version?: string }>('/health/database'),

  /**
   * Check LDAP connection
   * @returns LDAP status and response time
   */
  checkLdap: () => pkdApi.get<{ status: string; responseTime?: number }>('/health/ldap'),
};

// =============================================================================
// Certificate Search & Export APIs
// =============================================================================

/**
 * Certificate search parameters
 */
export interface CertificateSearchParams {
  country?: string;        // ISO 3166-1 alpha-2 code (e.g., "KR", "US")
  certType?: 'CSCA' | 'DSC' | 'DSC_NC' | 'CRL' | 'ALL';
  validationStatus?: 'VALID' | 'INVALID' | 'PENDING' | 'EXPIRED';
  text?: string;           // Search in subject DN or serial number
  limit?: number;
  offset?: number;
}

export interface CertificateSearchResult {
  success: boolean;
  total: number;
  certificates: Array<{
    dn: string;
    certType: string;
    countryCode: string;
    subject: string;
    issuer: string;
    serialNumber: string;
    notBefore: string;
    notAfter: string;
    validationStatus: string;
    storedInLdap: boolean;
  }>;
}

export interface CertificateDetail {
  dn: string;
  certType: string;
  countryCode: string;
  subject: string;
  issuer: string;
  serialNumber: string;
  notBefore: string;
  notAfter: string;
  validationStatus: string;
  validationMessage: string | null;
  storedInLdap: boolean;
  certificatePem: string;
}

/**
 * Certificate Search & Export
 */
export const certificateApi = {
  /**
   * Search certificates in LDAP
   * @param params - Search filters
   * @returns Matching certificates with metadata
   */
  search: (params: CertificateSearchParams) =>
    pkdApi.get<CertificateSearchResult>('/certificates/search', { params }),

  /**
   * Get list of available countries
   * @returns Array of country codes that have certificates
   */
  getCountries: () => pkdApi.get<string[]>('/certificates/countries'),

  /**
   * Get certificate details by DN
   * @param dn - LDAP Distinguished Name
   * @returns Full certificate details including PEM
   */
  getDetail: (dn: string) =>
    pkdApi.get<ApiResponse<CertificateDetail>>('/certificates/detail', { params: { dn } }),

  /**
   * Export single certificate file
   * @param dn - LDAP Distinguished Name
   * @param format - DER (binary) or PEM (base64 text)
   * @returns Certificate file as blob
   */
  exportFile: (dn: string, format: 'DER' | 'PEM' = 'DER') =>
    pkdApi.get('/certificates/export/file', {
      params: { dn, format },
      responseType: 'blob',
    }),

  /**
   * Export all certificates for a country
   * @param country - ISO 3166-1 alpha-2 code
   * @param format - DER or PEM
   * @returns ZIP archive containing all certificates
   */
  exportCountry: (country: string, format: 'DER' | 'PEM' = 'DER') =>
    pkdApi.get('/certificates/export/country', {
      params: { country, format },
      responseType: 'blob',
    }),
};

// =============================================================================
// Upload History & Statistics (Read-Only)
// =============================================================================

/**
 * Upload history and statistics (read-only views)
 * Write operations (upload, trigger) are in relayApi.uploadApi
 */
export const uploadHistoryApi = {
  /**
   * Get upload history with pagination
   * @param params - Pagination parameters
   * @returns Paginated list of uploaded files
   */
  getHistory: (params: PageRequest) =>
    pkdApi.get<PageResponse<UploadedFile>>('/upload/history', { params }),

  /**
   * Get upload details by ID
   * @param uploadId - Upload record UUID
   * @returns Full upload record with statistics
   */
  getDetail: (uploadId: string) =>
    pkdApi.get<ApiResponse<UploadedFile>>(`/upload/detail/${uploadId}`),

  /**
   * Get upload statistics overview
   * @returns Overall upload statistics (total files, certificates, etc.)
   */
  getStatistics: () => pkdApi.get<UploadStatisticsOverview>('/upload/statistics'),

  /**
   * Get country-level statistics
   * @param limit - Number of countries to return (sorted by certificate count)
   * @returns Top countries by certificate count
   */
  getCountryStatistics: (limit: number = 20) =>
    pkdApi.get<Array<{
      country: string;
      csca: number;
      dsc: number;
      dscNc: number;
      total: number;
    }>>('/upload/countries', { params: { limit } }),

  /**
   * Get recent upload changes
   * @param limit - Number of recent uploads to return
   * @returns Recent uploads with change summary
   */
  getChanges: (limit: number = 10) =>
    pkdApi.get<UploadChangesResponse>('/upload/changes', { params: { limit } }),
};

// =============================================================================
// LDAP Direct Query APIs
// =============================================================================

/**
 * Direct LDAP queries (low-level access)
 */
export const ldapApi = {
  /**
   * Get LDAP health status
   */
  getHealth: () => pkdApi.get('/health/ldap'),

  /**
   * Get LDAP statistics
   * @returns Entry counts by objectClass
   */
  getStatistics: () => pkdApi.get('/ldap/statistics'),

  /**
   * Search certificates in LDAP directory
   * @param params - LDAP search filters
   * @returns LDAP search results
   */
  searchCertificates: (params: {
    country?: string;
    type?: string;
    fingerprint?: string;
  }) => pkdApi.get('/ldap/certificates', { params }),

  /**
   * Get certificate by SHA-256 fingerprint
   * @param fingerprint - Certificate SHA-256 hash
   * @returns Certificate entry
   */
  getCertificateByFingerprint: (fingerprint: string) =>
    pkdApi.get(`/ldap/certificates/${fingerprint}`),

  /**
   * Search CRLs in LDAP directory
   * @param params - CRL search filters
   * @returns CRL entries
   */
  searchCrls: (params: { country?: string }) =>
    pkdApi.get('/ldap/crls', { params }),

  /**
   * Check certificate revocation status
   * @param params - Certificate serial number and issuer DN
   * @returns Revocation status
   */
  checkRevocation: (params: { serialNumber: string; issuerDn: string }) =>
    pkdApi.get('/ldap/revocation/check', { params }),
};

// =============================================================================
// Default Export
// =============================================================================

export default pkdApi;
