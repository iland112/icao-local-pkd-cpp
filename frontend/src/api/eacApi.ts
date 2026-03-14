/**
 * EAC Service API module
 * BSI TR-03110 CVC Certificate Management (:8086)
 */
import axios from 'axios';

const eacApi = axios.create({
  baseURL: '/api/eac',
  timeout: 30000,
  headers: { 'Content-Type': 'application/json' },
});

// JWT interceptor
eacApi.interceptors.request.use((config) => {
  const token = localStorage.getItem('access_token');
  if (token && config.headers) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

// --- Types ---

export interface CvcCertificate {
  id: string;
  cvc_type: 'CVCA' | 'DV_DOMESTIC' | 'DV_FOREIGN' | 'IS';
  country_code: string;
  car: string;   // Certification Authority Reference
  chr: string;   // Certificate Holder Reference
  chat_oid: string;
  chat_role: 'IS' | 'AT' | 'ST';
  chat_permissions: string;  // JSON string of decoded permissions
  public_key_oid: string;
  public_key_algorithm: string;
  effective_date: string;    // YYMMDD
  expiration_date: string;   // YYMMDD
  fingerprint_sha256: string;
  signature_valid: boolean;
  validation_status: 'PENDING' | 'VALID' | 'INVALID' | 'EXPIRED';
  validation_message: string;
  source_type: string;
  created_at: string;
  updated_at: string;
}

export interface CvcListResponse {
  success: boolean;
  data: CvcCertificate[];
  total: number;
  page: number;
  pageSize: number;
  totalPages: number;
}

export interface CvcStatistics {
  total: number;
  byType: Record<string, number>;
  byCountry: Array<{ country_code: string; cnt: number }>;
  validCount: number;
  expiredCount: number;
}

export interface ChainResult {
  certificateId: string;
  chainValid: boolean;
  chainPath: string;
  chainDepth: number;
  message: string;
  certificates?: CvcCertificate[];
}

// --- API functions ---

export const getEacHealth = () => eacApi.get('/health');

export const getEacStatistics = () =>
  eacApi.get<{ success: boolean; statistics: CvcStatistics }>('/statistics');

export const getEacCountries = () =>
  eacApi.get<{ success: boolean; countries: Array<{ country_code: string }> }>('/countries');

export const searchEacCertificates = (params: {
  country?: string;
  type?: string;
  status?: string;
  page?: number;
  pageSize?: number;
}) => eacApi.get<CvcListResponse>('/certificates', { params });

export const getEacCertificate = (id: string) =>
  eacApi.get<{ success: boolean; certificate: CvcCertificate }>(`/certificates/${id}`);

export const getEacChain = (id: string) =>
  eacApi.get<{ success: boolean; chain: ChainResult }>(`/certificates/${id}/chain`);

export const uploadCvc = (file: File) => {
  const formData = new FormData();
  formData.append('file', file);
  return eacApi.post<{ success: boolean; certificate: Partial<CvcCertificate>; error?: string }>(
    '/upload',
    formData,
    { headers: { 'Content-Type': 'multipart/form-data' } }
  );
};

export const previewCvc = (file: File) => {
  const formData = new FormData();
  formData.append('file', file);
  return eacApi.post<{ success: boolean; certificate: Record<string, unknown>; error?: string }>(
    '/upload/preview',
    formData,
    { headers: { 'Content-Type': 'multipart/form-data' } }
  );
};
