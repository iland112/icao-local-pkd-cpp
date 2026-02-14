/**
 * Sprint 3 Task 3.6: Validation Result Types
 *
 * Type definitions for trust chain validation results
 */

export interface ValidationResult {
  id: string;
  certificateId: string;
  uploadId?: string;
  certificateType: 'CSCA' | 'DSC' | 'DSC_NC';
  countryCode: string;
  subjectDn: string;
  issuerDn: string;
  serialNumber: string;
  validationStatus: 'VALID' | 'EXPIRED_VALID' | 'INVALID' | 'PENDING' | 'ERROR';

  // Trust Chain Fields (Sprint 3)
  trustChainValid: boolean;
  trustChainMessage: string;
  trustChainPath: string; // Example: "DSC → CN=CSCA Latvia,serialNumber=003 → CN=CSCA Latvia,serialNumber=002 → CN=CSCA Latvia,serialNumber=001"

  // CSCA Info
  cscaFound: boolean;
  cscaSubjectDn: string;
  cscaFingerprint: string;

  // Signature Verification
  signatureVerified: boolean;
  signatureAlgorithm: string;

  // Validity Period
  validityCheckPassed: boolean;
  isExpired: boolean;
  isNotYetValid: boolean;
  notBefore: string;
  notAfter: string;

  // CA Flags
  isCa: boolean;
  isSelfSigned: boolean;

  // Key Usage
  keyUsageValid: boolean;
  keyUsageFlags: string;

  // CRL Check
  crlCheckStatus: string;
  crlCheckMessage: string;

  // Error Info
  errorCode: string;
  errorMessage: string;

  // Timestamps
  validatedAt: string;
  validationDurationMs: number;

  // Fingerprint (from certificate join)
  fingerprint?: string;
}

export interface ValidationListResponse {
  success: boolean;
  count: number;
  total: number;
  limit: number;
  offset: number;
  validations: ValidationResult[];
}

export interface ValidationDetailResponse {
  success: boolean;
  validation?: ValidationResult;
  error?: string;
}
