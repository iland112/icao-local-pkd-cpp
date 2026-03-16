import axios from 'axios';

const csrApi = axios.create({
  baseURL: '/api/csr',
  timeout: 30000,
  headers: { 'Content-Type': 'application/json' },
});

// JWT interceptor
csrApi.interceptors.request.use((config) => {
  const token = localStorage.getItem('access_token');
  if (token && config.headers) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

csrApi.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401) {
      const currentPath = window.location.pathname;
      if (currentPath !== '/login') {
        localStorage.removeItem('access_token');
        localStorage.removeItem('user');
        window.dispatchEvent(new CustomEvent('auth:expired'));
      }
    }
    return Promise.reject(error);
  }
);

export interface CsrRequest {
  id: string;
  subjectDn: string;
  countryCode: string;
  organization: string;
  commonName: string;
  keyAlgorithm: string;
  signatureAlgorithm: string;
  publicKeyFingerprint: string;
  status: 'CREATED' | 'SUBMITTED' | 'ISSUED' | 'REVOKED';
  memo: string;
  createdBy: string;
  createdAt: string;
  updatedAt: string;
  csrPem?: string;
  csrDerHex?: string;
  // Issued certificate fields (snake_case from backend JSON)
  certificate_serial?: string;
  certificate_subject_dn?: string;
  certificate_issuer_dn?: string;
  certificate_not_before?: string;
  certificate_not_after?: string;
  certificate_fingerprint?: string;
  issued_at?: string;
  registered_by?: string;
}

export interface CsrGenerateRequest {
  countryCode: string;
  organization: string;
  commonName: string;
  memo?: string;
}

export interface CsrGenerateResponse {
  success: boolean;
  data?: {
    id: string;
    subjectDn: string;
    csrPem: string;
    publicKeyFingerprint: string;
    keyAlgorithm: string;
    signatureAlgorithm: string;
  };
  error?: string;
}

export interface CsrListResponse {
  success: boolean;
  total: number;
  page: number;
  pageSize: number;
  data: CsrRequest[];
}

export const csrApiService = {
  generate: (data: CsrGenerateRequest) =>
    csrApi.post<CsrGenerateResponse>('/generate', data),

  list: (page = 1, pageSize = 20, status?: string) =>
    csrApi.get<CsrListResponse>('', {
      params: { page, pageSize, ...(status && { status }) },
    }),

  getById: (id: string) =>
    csrApi.get<{ success: boolean; data: CsrRequest }>(`/${id}`),

  exportPem: (id: string) =>
    csrApi.get(`/${id}/export/pem`, { responseType: 'blob' }),

  exportDer: (id: string) =>
    csrApi.get(`/${id}/export/der`, { responseType: 'blob' }),

  registerCertificate: (id: string, certificatePem: string) =>
    csrApi.post<{ success: boolean; data?: { id: string; subjectDn: string; fingerprint: string }; error?: string }>(
      `/${id}/certificate`, { certificatePem }),

  deleteById: (id: string) =>
    csrApi.delete<{ success: boolean }>(`/${id}`),
};
