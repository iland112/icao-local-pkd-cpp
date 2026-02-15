/**
 * Validation API Client
 *
 * API client for fetching validation results with trust chain information.
 * Uses pkdApi axios instance for consistent error handling and interceptors.
 */

import pkdApi from '@/services/pkdApi';
import type { ValidationListResponse, ValidationDetailResponse } from '@/types/validation';

/**
 * Fetch validation results for a specific upload
 *
 * @param uploadId - Upload UUID
 * @param options - Query parameters (limit, offset, status, certType)
 * @returns Promise<ValidationListResponse>
 */
export const getUploadValidations = async (
  uploadId: string,
  options: {
    limit?: number;
    offset?: number;
    status?: 'VALID' | 'INVALID' | 'PENDING' | 'ERROR';
    certType?: 'CSCA' | 'DSC' | 'DSC_NC';
  } = {}
): Promise<ValidationListResponse> => {
  const params: Record<string, string | number> = {};
  if (options.limit) params.limit = options.limit;
  if (options.offset) params.offset = options.offset;
  if (options.status) params.status = options.status;
  if (options.certType) params.certType = options.certType;

  const response = await pkdApi.get<ValidationListResponse>(
    `/upload/${uploadId}/validations`,
    { params }
  );
  return response.data;
};

/**
 * Fetch validation result for a specific certificate by fingerprint
 *
 * @param fingerprint - SHA-256 fingerprint (hex string)
 * @returns Promise<ValidationDetailResponse>
 */
export const getCertificateValidation = async (
  fingerprint: string
): Promise<ValidationDetailResponse> => {
  const response = await pkdApi.get<ValidationDetailResponse>(
    '/certificates/validation',
    { params: { fingerprint } }
  );
  return response.data;
};

/**
 * Validation API exports
 */
export const validationApi = {
  getUploadValidations,
  getCertificateValidation,
};

export default validationApi;
