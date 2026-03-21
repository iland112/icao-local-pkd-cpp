/**
 * PA (Passive Authentication) Service API
 *
 * Handles communication with PA Service (port 8082)
 * Responsibilities:
 * - PA verification (ICAO 9303 Part 10 & 11)
 * - DG1 (MRZ) / DG2 (Face) parsing
 * - PA verification history & statistics
 * - Lightweight PA lookup by subject DN or fingerprint
 *
 * @version 2.13.0
 */

import axios, { type AxiosError, type AxiosResponse } from 'axios';
import type {
  ApiResponse,
  PAVerificationRequest,
  PAVerificationResponse,
  PAStatisticsRawResponse,
  PAHistoryListResponse,
  PageRequest,
} from '@/types';

// --- Axios Instance ---

const paClient = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

// Request interceptor: Inject JWT token if available (for user identification on public endpoints)
paClient.interceptors.request.use(
  (config) => {
    const token = localStorage.getItem('access_token');
    if (token) {
      config.headers.Authorization = `Bearer ${token}`;
    }
    return config;
  },
);

paClient.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (import.meta.env.DEV) console.error('[PA API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// --- PA Service APIs ---

export const paApi = {
  verify: (request: PAVerificationRequest) =>
    paClient.post<ApiResponse<PAVerificationResponse>>('/pa/verify', request),

  getHistory: (params: PageRequest) =>
    paClient.get<PAHistoryListResponse>('/pa/history', { params }),

  getDetail: (id: string) =>
    paClient.get<PAVerificationResponse>(`/pa/${id}`),

  getStatistics: () =>
    paClient.get<PAStatisticsRawResponse>('/pa/statistics'),

  parseDG1: (data: string) =>
    paClient.post('/pa/parse-dg1', { data }),

  parseDG2: (data: string) =>
    paClient.post('/pa/parse-dg2', { data }),

  /** Lightweight PA lookup by subject DN or fingerprint (no SOD/DG required) */
  paLookup: (params: { subjectDn?: string; fingerprint?: string }) =>
    paClient.post('/certificates/pa-lookup', params),

  /** Get data groups (DG1, DG2) for a PA verification record */
  getDataGroups: (verificationId: string) =>
    paClient.get(`/pa/${verificationId}/datagroups`),

  /** Fetch trust materials (CSCA/CRL/Link Certificates) for client-side PA */
  getTrustMaterials: (params: {
    countryCode: string;
    dscIssuerDn?: string;
    requestedBy?: string;
  }) => paClient.post('/pa/trust-materials', params),

  /** Report client-side PA verification result with encrypted MRZ */
  reportTrustMaterialResult: (params: {
    requestId: string;
    verificationStatus: string;
    verificationMessage?: string;
    trustChainValid?: boolean;
    sodSignatureValid?: boolean;
    dgHashValid?: boolean;
    crlCheckPassed?: boolean;
    processingTimeMs?: number;
    encryptedMrz?: string;
  }) => paClient.post('/pa/trust-materials/result', params),

  /** Get client PA (trust material) request history */
  getTrustMaterialHistory: (params?: { page?: number; size?: number; country?: string }) =>
    paClient.get('/pa/trust-materials/history', { params }),

  /** Get combined statistics (server PA + client PA) */
  getCombinedStatistics: () =>
    paClient.get('/pa/combined-statistics'),
};
