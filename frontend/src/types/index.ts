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
export type FileFormat = 'LDIF' | 'ML' | 'MASTER_LIST';  // Backend uses 'ML', some places use 'MASTER_LIST'
export type UploadStatus = 'PENDING' | 'UPLOADING' | 'PARSING' | 'VALIDATING' | 'SAVING_DB' | 'SAVING_LDAP' | 'COMPLETED' | 'FAILED';
export type ProcessingMode = 'AUTO' | 'MANUAL';

export interface UploadedFile {
  id: string;
  fileName: string;
  fileFormat: FileFormat;
  fileSize: number;
  fileHash?: string;
  status: UploadStatus;
  processingMode: ProcessingMode;
  uploadedAt?: string;
  createdAt?: string;  // Backend uses createdAt
  completedAt?: string;
  updatedAt?: string;  // Backend uses updatedAt
  errorMessage?: string;
  statistics?: UploadStatistics;
  // Certificate counts
  cscaCount?: number;
  dscCount?: number;
  dscNcCount?: number;
  crlCount?: number;
  mlCount?: number;
  mlscCount?: number;  // Master List Signer Certificate count (v2.1.1)
  certificateCount?: number;
  totalEntries?: number;
  processedEntries?: number;
  // Collection 002 CSCA extraction statistics (v2.0.0)
  cscaExtractedFromMl?: number;  // Total CSCAs extracted from Master Lists
  cscaDuplicates?: number;       // Duplicate CSCAs detected
  // Validation statistics
  validation?: {
    validCount: number;
    invalidCount: number;
    pendingCount: number;
    errorCount: number;
    trustChainValidCount: number;
    trustChainInvalidCount: number;
    cscaNotFoundCount: number;
    expiredCount: number;
    revokedCount: number;
  };
  // LDAP upload status
  ldapUploadedCount?: number;
  ldapPendingCount?: number;
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

// Upload Issues (v2.1.2.2) - Duplicates detected during upload
export interface UploadDuplicate {
  id: number;
  certificateType: CertificateType;
  country: string;
  subjectDn: string;
  fingerprint: string;
  sourceType: string;
  sourceCountry?: string;
  detectedAt: string;
}

export interface UploadIssues {
  success: boolean;
  totalDuplicates: number;
  duplicates: UploadDuplicate[];
  byType: {
    CSCA: number;
    DSC: number;
    DSC_NC: number;
    MLSC: number;
    CRL: number;
  };
  error?: string;
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
export type CertificateType = 'CSCA' | 'DSC' | 'DSC_NC' | 'DS' | 'ML_SIGNER' | 'MLSC';  // MLSC added in v2.1.1
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

// Java와 동일한 PA 검증 응답 형식
export interface PAVerificationResponse {
  status: PAStatus;
  verificationId: string;
  verificationTimestamp: string;
  issuingCountry: string;
  documentNumber: string;
  certificateChainValidation: CertificateChainValidationDto;
  sodSignatureValidation: SodSignatureValidationDto;
  dataGroupValidation: DataGroupValidationDto;
  processingDurationMs: number;
  errors: PAError[];
}

export interface CertificateChainValidationDto {
  valid: boolean;
  dscSubject: string;
  dscSerialNumber: string;
  cscaSubject: string;
  cscaSerialNumber: string;
  notBefore: string;
  notAfter: string;
  // Certificate expiration status (ICAO 9303)
  dscExpired?: boolean;
  cscaExpired?: boolean;
  validAtSigningTime?: boolean;
  expirationStatus?: 'VALID' | 'WARNING' | 'EXPIRED';
  expirationMessage?: string;
  crlChecked: boolean;
  revoked: boolean;
  crlStatus: string;
  crlStatusDescription: string;
  crlStatusDetailedDescription: string;
  crlStatusSeverity: string;
  crlMessage: string;
  validationErrors?: string;
}

export interface SodSignatureValidationDto {
  valid: boolean;
  signatureAlgorithm: string;
  hashAlgorithm: string;
  validationErrors?: string;
}

export interface DataGroupValidationDto {
  totalGroups: number;
  validGroups: number;
  invalidGroups: number;
  details: Record<string, DataGroupDetailDto>;
}

export interface DataGroupDetailDto {
  valid: boolean;
  expectedHash: string;
  actualHash: string;
}

export interface PAError {
  code: string;
  message: string;
  step?: string;
}

// Legacy types for backward compatibility (deprecated)
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
// API returns: verificationId, verificationTimestamp, processingDurationMs
export interface PAHistoryItem {
  verificationId: string;
  status: PAStatus;
  issuingCountry?: string;
  documentNumber?: string;
  verificationTimestamp: string;
  processingDurationMs: number;
  requestedBy?: string;
  // Validation results from API
  sodSignatureValidation?: { valid: boolean };
  certificateChainValidation?: Partial<CertificateChainValidationDto>;
  dataGroupValidation?: { valid: boolean };
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

// Validation Statistics
export interface ValidationStats {
  validCount: number;
  invalidCount: number;
  pendingCount: number;
  errorCount: number;
  trustChainValidCount: number;
  trustChainInvalidCount: number;
  cscaNotFoundCount: number;
  expiredCount: number;
  revokedCount: number;
}

// Statistics
export interface UploadStatisticsOverview {
  totalUploads: number;
  successfulUploads: number;
  failedUploads: number;
  totalCertificates: number;
  cscaCount: number;
  mlscCount?: number;  // Master List Signer Certificate count (v2.1.1)
  dscCount: number;
  dscNcCount: number;
  crlCount: number;
  mlCount: number;  // Master List count
  countriesCount: number;
  // Master List extraction statistics (v2.1.1)
  cscaExtractedFromMl?: number;  // Total certificates extracted from Master Lists (MLSC + CSCA + LC)
  cscaDuplicates?: number;       // Duplicate certificates detected
  // CSCA breakdown (v2.0.9)
  cscaBreakdown?: {
    total: number;
    selfSigned: number;
    linkCertificates: number;
  };
  validation: ValidationStats;
}

// Upload changes tracking types
export interface UploadChange {
  uploadId: string;
  fileName: string;
  collectionNumber: string;
  uploadTime: string;
  counts: {
    csca: number;
    mlsc?: number;
    dsc: number;
    dscNc: number;
    crl: number;
    ml: number;
  };
  changes: {
    csca: number;
    dsc: number;
    dscNc: number;
    crl: number;
    ml: number;
  };
  totalChange: number;
  previousUpload?: {
    fileName: string;
    uploadTime: string;
  } | null;
}

export interface UploadChangesResponse {
  success: boolean;
  count: number;
  changes: UploadChange[];
}

export interface PAStatisticsOverview {
  totalVerifications: number;
  validCount: number;
  invalidCount: number;
  errorCount: number;
  averageProcessingTimeMs: number;
  countriesVerified: number;
}

// Sync types
export type SyncStatusType = 'SYNCED' | 'DISCREPANCY' | 'ERROR' | 'PENDING' | 'NO_DATA';

export interface SyncStats {
  csca: number;
  mlsc: number;  // Master List Signer Certificates (Sprint 3)
  dsc: number;
  dscNc: number;
  crl: number;
  total?: number;
  storedInLdap?: number;
}

export interface SyncDiscrepancy {
  csca: number;
  mlsc: number;  // Master List Signer Certificates (Sprint 3)
  dsc: number;
  dscNc: number;
  crl: number;
  total: number;
}

export interface SyncStatusResponse {
  id?: number;
  checkedAt?: string;
  dbStats?: SyncStats;
  ldapStats?: SyncStats;
  discrepancy?: SyncDiscrepancy;
  status: SyncStatusType;
  errorMessage?: string;
  checkDurationMs?: number;
  message?: string;
}

export interface SyncHistoryItem {
  id: number;
  checkedAt: string;
  dbTotal: number;
  ldapTotal: number;
  totalDiscrepancy: number;
  status: SyncStatusType;
  checkDurationMs: number;
}

export interface SyncDiscrepancyItem {
  id: string;
  detectedAt: string;
  itemType: 'CERTIFICATE' | 'CRL';
  certificateType?: string;
  countryCode?: string;
  fingerprint?: string;
  issueType: 'MISSING_IN_LDAP' | 'MISSING_IN_DB' | 'MISMATCH';
  dbExists: boolean;
  ldapExists: boolean;
}

export interface SyncCheckResponse {
  success: boolean;
  syncStatusId: number;
  status: SyncStatusType;
  dbStats: SyncStats;
  ldapStats: SyncStats;
  discrepancy: SyncDiscrepancy;
  checkDurationMs: number;
}
