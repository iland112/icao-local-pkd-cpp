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
export type FileFormat = 'LDIF' | 'ML' | 'MASTER_LIST' | 'PEM' | 'DER' | 'CER' | 'P7B' | 'DL' | 'CRL';  // Backend uses 'ML', some places use 'MASTER_LIST'
export type UploadStatus = 'PENDING' | 'UPLOADING' | 'PARSING' | 'PROCESSING' | 'VALIDATING' | 'SAVING_DB' | 'SAVING_LDAP' | 'COMPLETED' | 'FAILED';
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
    expiredValidCount: number;
    invalidCount: number;
    pendingCount: number;
    errorCount: number;
    trustChainValidCount: number;
    trustChainInvalidCount: number;
    cscaNotFoundCount: number;
    expiredCount: number;
    revokedCount: number;
    validPeriodCount?: number;
    icaoCompliantCount?: number;
    icaoNonCompliantCount?: number;
    icaoWarningCount?: number;
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
  sourceEntryDn?: string;
  sourceFileName?: string;
  detectedAt: string;

  // Certificate and first upload info for tree view
  certificateId: string;
  firstUploadId: string;
  firstUploadFileName?: string;
  firstUploadTimestamp?: string;
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

// Phase 4.4: X.509 Certificate Metadata (from backend)
export interface CertificateMetadata {
  // Identity
  subjectDn: string;
  issuerDn: string;
  serialNumber: string;
  countryCode: string;

  // Certificate type
  certificateType: string;  // CSCA, DSC, DSC_NC, MLSC
  isSelfSigned: boolean;
  isLinkCertificate: boolean;

  // Cryptographic details
  signatureAlgorithm: string;     // e.g., "SHA256withRSA"
  publicKeyAlgorithm: string;     // e.g., "RSA", "ECDSA"
  keySize: number;                // e.g., 2048, 4096

  // X.509 Extensions
  isCa: boolean;
  pathLengthConstraint?: number;
  keyUsage: string[];             // e.g., ["digitalSignature", "keyCertSign"]
  extendedKeyUsage: string[];     // e.g., ["1.3.6.1.5.5.7.3.2"]

  // Validity period
  notBefore: string;
  notAfter: string;
  isExpired: boolean;

  // Fingerprints
  fingerprintSha256: string;
  fingerprintSha1: string;
}

// Phase 4.4: ICAO 9303 Compliance Status
export interface IcaoComplianceStatus {
  isCompliant: boolean;
  complianceLevel: string;         // CONFORMANT, NON_CONFORMANT, WARNING
  violations: string[];
  pkdConformanceCode?: string;     // e.g., "ERR:CSCA.CDP.14"
  pkdConformanceText?: string;
  pkdVersion?: string;

  // Specific compliance checks
  keyUsageCompliant: boolean;
  algorithmCompliant: boolean;
  keySizeCompliant: boolean;
  validityPeriodCompliant: boolean;
  dnFormatCompliant: boolean;
  extensionsCompliant: boolean;
}

// Phase 4.4: Real-time Validation Statistics
export interface ValidationStatistics {
  // Overall counts
  totalCertificates: number;
  processedCount: number;
  validCount: number;
  invalidCount: number;
  pendingCount: number;

  // Trust chain results
  trustChainValidCount: number;
  trustChainInvalidCount: number;
  cscaNotFoundCount: number;

  // Expiration status
  expiredCount: number;
  notYetValidCount: number;
  validPeriodCount: number;

  // CRL status
  revokedCount: number;
  notRevokedCount: number;
  crlNotCheckedCount: number;

  // ICAO compliance (Phase 4.4)
  icaoCompliantCount: number;
  icaoNonCompliantCount: number;
  icaoWarningCount: number;
  complianceViolations: Record<string, number>;  // violation type -> count

  // Distribution maps
  signatureAlgorithms: Record<string, number>;  // "SHA256withRSA" -> count
  keySizes: Record<string, number>;             // "2048" -> count
  certificateTypes: Record<string, number>;     // "DSC" -> count, "CSCA" -> count

  // Duplicate tracking
  duplicateCount?: number;

  // Validation reason tracking ("INVALID: reason" → count)
  validationReasons?: Record<string, number>;
  expiredValidCount?: number;

  // Per-certificate validation logs
  totalValidationLogCount?: number;
  recentValidationLogs?: ValidationLogEntry[];

  // Processing error tracking
  totalErrorCount?: number;
  parseErrorCount?: number;
  dbSaveErrorCount?: number;
  ldapSaveErrorCount?: number;
  recentErrors?: ProcessingError[];
}

export interface ValidationLogEntry {
  timestamp: string;
  certificateType: string;
  countryCode: string;
  subjectDn: string;
  issuerDn: string;
  validationStatus: string;
  trustChainMessage: string;
  trustChainPath: string;
  errorCode: string;
  fingerprintSha256: string;
}

export interface ProcessingError {
  timestamp: string;
  errorType: string;
  entryDn: string;
  certificateDn: string;
  countryCode: string;
  certificateType: string;
  message: string;
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

  // Phase 4.4: Enhanced metadata
  currentCertificate?: CertificateMetadata;    // Currently processing certificate
  currentCompliance?: IcaoComplianceStatus;    // Current cert compliance status
  statistics?: ValidationStatistics;           // Aggregated statistics
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
  requestedBy?: string;
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
  dscAutoRegistration?: {
    registered: boolean;
    newlyRegistered: boolean;
    certificateId: string;
    fingerprint: string;
    countryCode: string;
  };
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
  crlThisUpdate?: string;
  crlNextUpdate?: string;
  validationErrors?: string;
  errorCode?: string;  // CSCA_NOT_FOUND, CSCA_DN_MISMATCH, CSCA_SELF_SIGNATURE_FAILED
  dscIssuer?: string;
  // DSC conformance status (ICAO PKD nc-data)
  dscNonConformant?: boolean;
  pkdConformanceCode?: string;
  pkdConformanceText?: string;
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
// API returns flat fields from pa_verification table (snake_case → camelCase via toCamelCase)
export interface PAHistoryItem {
  verificationId: string;
  status: PAStatus;
  issuingCountry?: string;
  documentNumber?: string;
  verificationTimestamp: string;
  completedTimestamp?: string;
  requestedBy?: string;
  // Flat validation result fields from backend
  sodSignatureValid?: boolean;
  sodSignatureMessage?: string;
  trustChainValid?: boolean;
  trustChainMessage?: string;
  dgHashesValid?: boolean;
  dgHashesMessage?: string;
  crlStatus?: string;
  crlMessage?: string;
  // Certificate info
  dscSubjectDn?: string;
  dscSerialNumber?: string;
  dscIssuerDn?: string;
  cscaSubjectDn?: string;
  verificationMessage?: string;
  // DSC conformance (ICAO PKD nc-data)
  dscNonConformant?: boolean;
  pkdConformanceCode?: string;
  pkdConformanceText?: string;
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
  expiredValidCount: number;
  invalidCount: number;
  pendingCount: number;
  errorCount: number;
  trustChainValidCount: number;
  trustChainInvalidCount: number;
  cscaNotFoundCount: number;
  expiredCount: number;
  validPeriodCount?: number;
  revokedCount: number;
  icaoCompliantCount?: number;
  icaoNonCompliantCount?: number;
  icaoWarningCount?: number;
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
  // Source tracking (v2.8.0)
  bySource?: Record<string, number>;
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
  dbCounts?: SyncStats;
  ldapCounts?: SyncStats;
  discrepancies?: SyncDiscrepancy;
  syncRequired?: boolean;
  countryStats?: Record<string, { csca?: number; mlsc?: number; dsc?: number; dsc_nc?: number; crl?: number }>;
  status?: SyncStatusType;
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
  message?: string;
  data: SyncStatusResponse;
}

// =============================================================================
// LDIF Structure Visualization Types (v2.2.2)
// =============================================================================

/**
 * LDIF Attribute with binary detection
 */
export interface LdifAttribute {
  name: string;           // Attribute name (e.g., "cn", "userCertificate;binary")
  value: string;          // Attribute value (or "[Binary Data: XXX bytes]" for binary)
  isBinary: boolean;      // True if this is a binary attribute
  binarySize?: number;    // Size in bytes (for binary attributes)
}

/**
 * LDIF Entry structure for visualization
 */
export interface LdifEntry {
  dn: string;                      // Distinguished Name
  objectClass: string;             // Primary objectClass (e.g., "pkdCertificate")
  attributes: LdifAttribute[];     // All attributes with values
  lineNumber: number;              // Line number in LDIF file
}

/**
 * Complete LDIF structure with statistics
 */
export interface LdifStructureData {
  entries: LdifEntry[];                         // Parsed entries (limited by maxEntries)
  totalEntries: number;                         // Total entries in file
  displayedEntries: number;                     // Number of entries displayed
  totalAttributes: number;                      // Total attributes across all entries
  objectClassCounts: Record<string, number>;    // Count of entries by objectClass
  truncated: boolean;                           // True if totalEntries > displayedEntries
}

// =============================================================================
// Certificate Preview Types (preview-before-save workflow)
// =============================================================================

// Doc 9303 Compliance Checklist
export interface Doc9303CheckItem {
  id: string;
  category: string;
  label: string;
  status: 'PASS' | 'FAIL' | 'WARNING' | 'NA';
  message: string;
  requirement: string;
}

export interface Doc9303ChecklistResult {
  certificateType: string;
  totalChecks: number;
  passCount: number;
  failCount: number;
  warningCount: number;
  naCount: number;
  overallStatus: 'CONFORMANT' | 'NON_CONFORMANT' | 'WARNING';
  items: Doc9303CheckItem[];
}

export interface CertificatePreviewItem {
  subjectDn: string;
  issuerDn: string;
  serialNumber: string;
  countryCode: string;
  certificateType: string;
  isSelfSigned: boolean;
  isLinkCertificate: boolean;
  notBefore: string;
  notAfter: string;
  isExpired: boolean;
  signatureAlgorithm: string;
  publicKeyAlgorithm: string;
  keySize: number;
  fingerprintSha256: string;
  doc9303Checklist?: Doc9303ChecklistResult;
}

export interface DeviationPreviewItem {
  certificateIssuerDn: string;
  certificateSerialNumber: string;
  defectDescription: string;
  defectTypeOid: string;
  defectCategory: string;
}

export interface CrlPreviewItem {
  issuerDn: string;
  countryCode: string;
  thisUpdate: string;
  nextUpdate: string;
  crlNumber: string;
  revokedCount: number;
}

export interface CertificatePreviewResult {
  success: boolean;
  fileFormat: string;
  isDuplicate: boolean;
  duplicateUploadId?: string;
  message?: string;
  errorMessage?: string;
  certificates: CertificatePreviewItem[];
  deviations?: DeviationPreviewItem[];
  crlInfo?: CrlPreviewItem;
  dlIssuerCountry?: string;
  dlVersion?: number;
  dlHashAlgorithm?: string;
  dlSignatureValid?: boolean;
  // CMS-level metadata (for ASN.1 structure tree)
  dlSigningTime?: string;
  dlEContentType?: string;
  dlCmsDigestAlgorithm?: string;
  dlCmsSignatureAlgorithm?: string;
  dlSignerDn?: string;
}

// =============================================================================
// Backend Response Types (for type-safe API calls)
// =============================================================================

/** Backend response for GET /api/certificates/countries */
export interface CertificateCountriesResponse {
  success: boolean;
  countries: string[];
}

/** Backend response for GET /api/pa/statistics (raw format) */
export interface PAStatisticsRawResponse {
  totalVerifications: number;
  byStatus: Record<string, number>;
  byCountry: Array<{ country: string; count: number }>;
  successRate: number;
  failedLogins?: number;
  last24hEvents?: number;
  topUsers?: Array<{ username: string; count: number }>;
}

/** Backend response for GET /api/pa/history */
export interface PAHistoryListResponse {
  success: boolean;
  total: number;
  page: number;
  size: number;
  data: PAHistoryItem[];
}

/** Backend response for POST /api/upload/certificate */
export interface CertificateUploadResponse {
  success: boolean;
  message: string;
  uploadId: string;
  fileFormat: string;
  status: string;
  certificateCount: number;
  cscaCount: number;
  dscCount: number;
  dscNcCount: number;
  mlscCount: number;
  crlCount: number;
  ldapStoredCount: number;
  duplicateCount: number;
  errorMessage?: string;
}
