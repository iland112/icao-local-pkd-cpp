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
 * @version 2.12.0
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
};
