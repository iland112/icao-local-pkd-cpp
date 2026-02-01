/**
 * Sprint 3 Task 3.6: Validation API Client
 *
 * API client for fetching validation results with trust chain information
 */

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
  const params = new URLSearchParams();
  if (options.limit) params.append('limit', options.limit.toString());
  if (options.offset) params.append('offset', options.offset.toString());
  if (options.status) params.append('status', options.status);
  if (options.certType) params.append('certType', options.certType);

  const queryString = params.toString();
  const url = `/api/upload/${uploadId}/validations${queryString ? `?${queryString}` : ''}`;

  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to fetch validations: ${response.statusText}`);
  }

  return response.json();
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
  const response = await fetch(`/api/certificates/validation?fingerprint=${encodeURIComponent(fingerprint)}`);
  if (!response.ok) {
    throw new Error(`Failed to fetch validation: ${response.statusText}`);
  }

  return response.json();
};

/**
 * Validation API exports
 */
export const validationApi = {
  getUploadValidations,
  getCertificateValidation,
};

export default validationApi;
