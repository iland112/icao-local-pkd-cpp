import axios, { type AxiosError, type AxiosResponse } from 'axios';
import type {
  ApiResponse,
  HealthStatus,
  UploadedFile,
  UploadStatisticsOverview,
  PAVerificationRequest,
  PAVerificationResponse,
  PAHistoryItem,
  PAStatisticsOverview,
  PageRequest,
  PageResponse,
} from '@/types';

const api = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

// Response interceptor for error handling
api.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    console.error('API Error:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// Health Check APIs
export const healthApi = {
  check: () => api.get<HealthStatus>('/health'),
  checkDatabase: () => api.get<{ status: string; version?: string }>('/health/database'),
  checkLdap: () => api.get<{ status: string; responseTime?: number }>('/health/ldap'),
};

// Upload APIs
export const uploadApi = {
  uploadLdif: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);
    // Let axios set Content-Type automatically with boundary for multipart
    return api.post<ApiResponse<UploadedFile>>('/upload/ldif', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 300000, // 5 minutes for large files
    });
  },

  uploadMasterList: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);
    // Let axios set Content-Type automatically with boundary for multipart
    return api.post<ApiResponse<UploadedFile>>('/upload/masterlist', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 300000, // 5 minutes for large files
    });
  },

  getHistory: (params: PageRequest) =>
    api.get<PageResponse<UploadedFile>>('/upload/history', { params }),

  getDetail: (uploadId: string) =>
    api.get<UploadedFile>(`/upload/${uploadId}`),

  getStatistics: () =>
    api.get<UploadStatisticsOverview>('/upload/statistics'),

  getCountryStatistics: (limit: number = 20) =>
    api.get<{ country: string; csca: number; dsc: number; dscNc: number; total: number }[]>('/upload/countries', { params: { limit } }),

  // Manual processing triggers
  triggerParse: (uploadId: string) =>
    api.post(`/upload/${uploadId}/parse`),

  triggerValidate: (uploadId: string) =>
    api.post(`/upload/${uploadId}/validate`),

  triggerLdapUpload: (uploadId: string) =>
    api.post(`/upload/${uploadId}/ldap`),
};

// PA APIs
export const paApi = {
  verify: (request: PAVerificationRequest) =>
    api.post<ApiResponse<PAVerificationResponse>>('/pa/verify', request),

  getHistory: (params: PageRequest) =>
    api.get<PageResponse<PAHistoryItem>>('/pa/history', { params }),

  getDetail: (id: string) =>
    api.get<PAVerificationResponse>(`/pa/${id}`),

  getStatistics: () =>
    api.get<PAStatisticsOverview>('/pa/statistics'),

  parseDG1: (data: string) =>
    api.post('/pa/parse-dg1', { data }),

  parseDG2: (data: string) =>
    api.post('/pa/parse-dg2', { data }),
};

// LDAP APIs
export const ldapApi = {
  getHealth: () =>
    api.get('/ldap/health'),

  getStatistics: () =>
    api.get('/ldap/statistics'),

  searchCertificates: (params: { country?: string; type?: string; fingerprint?: string }) =>
    api.get('/ldap/certificates', { params }),

  getCertificateByFingerprint: (fingerprint: string) =>
    api.get(`/ldap/certificates/${fingerprint}`),

  searchCrls: (params: { country?: string }) =>
    api.get('/ldap/crls', { params }),

  checkRevocation: (params: { serialNumber: string; issuerDn: string }) =>
    api.get('/ldap/revocation/check', { params }),
};

// SSE connection for progress updates
export const createProgressEventSource = (uploadId: string): EventSource => {
  return new EventSource(`/api/progress/stream/${uploadId}`);
};

// Progress status polling (alternative to SSE)
export const getProgressStatus = (uploadId: string) =>
  api.get(`/progress/status/${uploadId}`);

export default api;
