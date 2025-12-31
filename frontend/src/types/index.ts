// Common types
export interface ApiResponse<T> {
  success: boolean;
  data?: T;
  error?: string;
  message?: string;
}

// Health check types
export interface HealthStatus {
  status: 'UP' | 'DOWN';
  database?: {
    status: 'UP' | 'DOWN';
    version?: string;
  };
  ldap?: {
    status: 'UP' | 'DOWN';
    responseTime?: number;
  };
}

// Upload types
export type FileFormat = 'LDIF' | 'MASTER_LIST';
export type UploadStatus = 'PENDING' | 'UPLOADING' | 'PARSING' | 'VALIDATING' | 'SAVING_DB' | 'SAVING_LDAP' | 'COMPLETED' | 'FAILED';
export type ProcessingMode = 'AUTO' | 'MANUAL';

export interface UploadedFile {
  id: string;
  fileName: string;
  fileFormat: FileFormat;
  fileSize: number;
  fileHash: string;
  status: UploadStatus;
  processingMode: ProcessingMode;
  uploadedAt: string;
  completedAt?: string;
  errorMessage?: string;
  statistics?: UploadStatistics;
}

export interface UploadStatistics {
  totalCertificates: number;
  cscaCount: number;
  dscCount: number;
  crlCount: number;
  validCount: number;
  invalidCount: number;
  skippedCount: number;
}

export interface UploadProgress {
  uploadId: string;
  stage: string;
  stageName: string;
  message: string;
  percentage: number;
  processedCount: number;
  totalCount: number;
  errorMessage?: string;
  details?: string;
  updatedAt?: string;
  // For backward compatibility with frontend stage handling
  status?: 'IDLE' | 'IN_PROGRESS' | 'COMPLETED' | 'FAILED';
}

// Certificate types
export type CertificateType = 'CSCA' | 'DSC' | 'DSC_NC' | 'DS' | 'ML_SIGNER';
export type CertificateStatus = 'PENDING' | 'VALID' | 'INVALID' | 'EXPIRED' | 'REVOKED';

export interface Certificate {
  id: string;
  type: CertificateType;
  status: CertificateStatus;
  subjectDn: string;
  issuerDn: string;
  serialNumber: string;
  countryCode: string;
  notBefore: string;
  notAfter: string;
  fingerprint: string;
  createdAt: string;
}

// PA types
export type PAStatus = 'VALID' | 'INVALID' | 'ERROR';
export type DataGroupNumber = 'DG1' | 'DG2' | 'DG3' | 'DG4' | 'DG5' | 'DG6' | 'DG7' | 'DG8' | 'DG9' | 'DG10' | 'DG11' | 'DG12' | 'DG13' | 'DG14' | 'DG15' | 'DG16';

export interface PAVerificationRequest {
  sod: string; // base64 encoded
  dataGroups: DataGroupInput[];
  mrzData?: MRZData;
}

export interface DataGroupInput {
  number: DataGroupNumber;
  data: string; // base64 encoded
}

export interface MRZData {
  line1: string;
  line2: string;
  fullName?: string;
  documentNumber?: string;
  nationality?: string;
  dateOfBirth?: string;
  sex?: string;
  expirationDate?: string;
}

export interface PAVerificationResult {
  id: string;
  status: PAStatus;
  overallValid: boolean;
  sodParsing: StepResult;
  dscExtraction: StepResult;
  cscaLookup: StepResult;
  trustChainValidation: StepResult;
  sodSignatureValidation: StepResult;
  dataGroupHashValidation: StepResult;
  crlCheck: StepResult;
  verifiedAt: string;
  processingTimeMs: number;
}

export interface StepResult {
  step: string;
  status: 'SUCCESS' | 'FAILED' | 'SKIPPED';
  message: string;
  details?: Record<string, unknown>;
}

export interface PAHistory {
  id: string;
  status: PAStatus;
  countryCode?: string;
  documentNumber?: string;
  verifiedAt: string;
  processingTimeMs: number;
  requestedBy?: string;
}

// Alias for PAHistory used in dashboard/history pages
export interface PAHistoryItem {
  id: string;
  status: PAStatus;
  issuingCountry?: string;
  documentNumber?: string;
  verifiedAt: string;
  processingTimeMs: number;
  requestedBy?: string;
}

// Upload history item
export interface UploadHistoryItem {
  id: string;
  fileName: string;
  fileType: 'LDIF' | 'ML';
  fileSize: number;
  status: UploadStatus;
  uploadedAt: string;
  completedAt?: string;
  errorMessage?: string;
  processedCertificates?: {
    total: number;
    csca: number;
    dsc: number;
    crl: number;
    countries: string[];
  };
}

// Pagination
export interface PageRequest {
  page: number;
  size: number;
  sort?: string;
  direction?: 'ASC' | 'DESC';
}

export interface PageResponse<T> {
  content: T[];
  page: number;
  size: number;
  totalElements: number;
  totalPages: number;
  first: boolean;
  last: boolean;
}

// Statistics
export interface UploadStatisticsOverview {
  totalUploads: number;
  successfulUploads: number;
  failedUploads: number;
  totalCertificates: number;
  cscaCount: number;
  dscCount: number;
  crlCount: number;
  countriesCount: number;
}

export interface PAStatisticsOverview {
  totalVerifications: number;
  validCount: number;
  invalidCount: number;
  errorCount: number;
  averageProcessingTimeMs: number;
  countriesVerified: number;
}
