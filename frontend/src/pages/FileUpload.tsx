import { useTranslation } from 'react-i18next';
import { useState, useRef, useCallback, useMemo, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import axios from 'axios';
import {
  Upload,
  CloudUpload,
  Clock,
  CheckCircle,
  CheckCircle2,
  XCircle,
  AlertTriangle,
  FileText,
  Loader2,
  Upload as UploadIcon,
  FileSearch,
  Database,
} from 'lucide-react';
import { uploadApi, createProgressEventSource } from '@/services/api';
import type { UploadProgress, UploadedFile, ValidationStatistics, CertificateMetadata, IcaoComplianceStatus, ProcessingError } from '@/types';
import { cn } from '@/utils/cn';
import { Stepper, type Step, type StepStatus } from '@/components/common/Stepper';
import { RealTimeStatisticsPanel } from '@/components/RealTimeStatisticsPanel';
import { ProcessingErrorsPanel } from '@/components/ProcessingErrorsPanel';
import { CurrentCertificateCard } from '@/components/CurrentCertificateCard';
import { EventLog, type EventLogEntry } from '@/components/EventLog';

interface StageStatus {
  status: 'IDLE' | 'IN_PROGRESS' | 'COMPLETED' | 'FAILED';
  message: string;
  percentage: number;
  details?: string;
}

const initialStage: StageStatus = {
  status: 'IDLE',
  message: '',
  percentage: 0,
};

export function FileUpload() {
  const { t } = useTranslation(['upload', 'common']);
  const navigate = useNavigate();
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [isProcessing, setIsProcessing] = useState(false);

  // Stage statuses for 3-stage processing progress
  const [uploadStage, setUploadStage] = useState<StageStatus>(initialStage);
  const [parseStage, setParseStage] = useState<StageStatus>(initialStage);
  const [dbSaveStage, setDbSaveStage] = useState<StageStatus>(initialStage);

  const [overallStatus, setOverallStatus] = useState<'IDLE' | 'PROCESSING' | 'FINALIZED' | 'FAILED'>('IDLE');
  const [overallMessage, setOverallMessage] = useState('');
  const [errorMessages, setErrorMessages] = useState<string[]>([]);

  // v1.5.5: SSE connection tracking and polling backup
  const [sseConnected, setSseConnected] = useState(false);
  const sseRef = useRef<EventSource | null>(null);
  const pollingIntervalRef = useRef<number | null>(null);
  const isProcessingRef = useRef(false);

  // Upload ID for violation detail lookups
  const [currentFileId, setCurrentFileId] = useState<string | null>(null);

  // Phase 4.4: Enhanced metadata tracking
  const [statistics, setStatistics] = useState<ValidationStatistics | null>(null);
  const [currentCertificate, setCurrentCertificate] = useState<CertificateMetadata | null>(null);
  const [currentCompliance, setCurrentCompliance] = useState<IcaoComplianceStatus | null>(null);

  // Processing error tracking
  const [processingErrors, setProcessingErrors] = useState<ProcessingError[]>([]);
  const [errorCounts, setErrorCounts] = useState({ total: 0, parse: 0, db: 0, ldap: 0 });

  // CSCA certificate count for warning banner
  const [cscaCount, setCscaCount] = useState<number | null>(null);

  // Re-upload confirmation dialog
  const [reuploadDialog, setReuploadDialog] = useState<{
    show: boolean;
    existingUpload?: { uploadId: string; fileName: string; status: string };
  }>({ show: false });
  const [duplicateWarningDialog, setDuplicateWarningDialog] = useState<{
    show: boolean;
    existingUpload?: { uploadId: string; fileName: string; status: string; fileFormat?: string };
  }>({ show: false });

  // Event log for SSE events (filtered: only meaningful events)
  const [eventLogEntries, setEventLogEntries] = useState<EventLogEntry[]>([]);
  const eventIdRef = useRef(0);
  const lastStageRef = useRef('');
  const lastErrorCountRef = useRef(0);
  const lastDuplicateCountRef = useRef(0);
  const lastValidationReasonsRef = useRef<Record<string, number>>({});
  const lastMilestoneRef = useRef(0);
  const lastValidationLogCountRef = useRef(0);

  // Keep isProcessingRef in sync for SSE reconnection closure
  useEffect(() => { isProcessingRef.current = isProcessing; }, [isProcessing]);

  // Fetch CSCA count on page load
  useEffect(() => {
    uploadApi.getStatistics()
      .then(res => setCscaCount(res.data.cscaCount ?? 0))
      .catch(() => {});
  }, []);

  // Convert stage status to step status for Stepper
  const toStepStatus = (status: StageStatus['status']): StepStatus => {
    switch (status) {
      case 'IN_PROGRESS':
        return 'active';
      case 'COMPLETED':
        return 'completed';
      case 'FAILED':
        return 'error';
      default:
        return 'idle';
    }
  };

  // Build steps array for Stepper component
  const steps: Step[] = useMemo(() => {
    return [
      {
        id: 'upload',
        label: t('fileUpload.title'),
        description: uploadStage.message || t('upload:fileUpload.sendingToServer'),
        status: toStepStatus(uploadStage.status),
        progress: uploadStage.percentage,
        details: uploadStage.details,
        icon: <UploadIcon className="w-3.5 h-3.5" />,
      },
      {
        id: 'parse',
        label: t('upload:fileUpload.fileParsing'),
        description: parseStage.message || t('upload:fileUpload.ldifMlParsing'),
        status: toStepStatus(parseStage.status),
        progress: parseStage.percentage,
        details: parseStage.details,
        icon: <FileSearch className="w-3.5 h-3.5" />,
      },
      {
        id: 'database',
        label: t('upload:fileUpload.validationAndSave'),
        description: dbSaveStage.message || t('upload:fileUpload.certValidationAndSave'),
        status: toStepStatus(dbSaveStage.status),
        progress: dbSaveStage.percentage,
        details: dbSaveStage.details,
        icon: <Database className="w-3.5 h-3.5" />,
      },
    ];
  }, [uploadStage, parseStage, dbSaveStage]);

  const isValidFileType = useCallback((file: File) => {
    const name = file.name.toLowerCase();
    return name.endsWith('.ldif') || name.endsWith('.ml') || name.endsWith('.bin');
  }, []);

  const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  // Phase 4.4: File-type-aware certificate type detection
  const getExpectedCertificateTypes = useCallback((file: File | null): string[] => {
    if (!file) return [];
    const name = file.name.toLowerCase();

    if (name.endsWith('.ml') || name.endsWith('.bin')) {
      return ['MLSC', 'CSCA', 'Link Cert'];
    } else if (name.endsWith('.ldif')) {
      return ['DSC', 'DSC_NC', 'CSCA', 'CRL', 'Master List'];
    }
    return [];
  }, []);

  const getFileTypeDescription = useCallback((file: File | null): string => {
    if (!file) return '';
    const name = file.name.toLowerCase();

    if (name.endsWith('.ml') || name.endsWith('.bin')) {
      return t('upload:fileUpload.mlDescription');
    } else if (name.endsWith('.ldif')) {
      return t('upload:fileUpload.ldifDescription');
    }
    return '';
  }, []);

  const MAX_FILE_SIZE_MB = 100;
  const handleFileSelect = (file: File) => {
    if (!isValidFileType(file)) return;
    if (file.size > MAX_FILE_SIZE_MB * 1024 * 1024) {
      setErrorMessages([t('upload:fileUpload.fileSizeTooLarge', { size: formatFileSize(file.size), max: MAX_FILE_SIZE_MB })]);
      return;
    }
    setSelectedFile(file);
    resetStages();
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
    const file = e.dataTransfer.files[0];
    if (file) handleFileSelect(file);
  };

  const handleFileInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) handleFileSelect(file);
  };

  const resetStages = () => {
    setUploadStage(initialStage);
    setParseStage(initialStage);
    setDbSaveStage(initialStage);
    setOverallStatus('IDLE');
    setOverallMessage('');
    setErrorMessages([]);
    // Phase 4.4: Reset enhanced metadata
    setStatistics(null);
    setCurrentCertificate(null);
    setCurrentCompliance(null);
    // Reset processing errors
    setProcessingErrors([]);
    setErrorCounts({ total: 0, parse: 0, db: 0, ldap: 0 });
    // Reset event log
    setEventLogEntries([]);
    eventIdRef.current = 0;
    lastStageRef.current = '';
    lastErrorCountRef.current = 0;
    lastDuplicateCountRef.current = 0;
    lastValidationReasonsRef.current = {};
    lastMilestoneRef.current = 0;
    lastValidationLogCountRef.current = 0;
  };

  const doUpload = async (force = false) => {
    if (!selectedFile) return;

    setIsProcessing(true);
    setOverallStatus('PROCESSING');
    resetStages();

    try {
      // Start upload
      setUploadStage({ status: 'IN_PROGRESS', message: t('upload:fileUpload.uploadingFile'), percentage: 0 });

      // LDIF / Master List upload (async with SSE)
      const isLdif = selectedFile.name.toLowerCase().endsWith('.ldif');
      const uploadFn = isLdif ? uploadApi.uploadLdif : uploadApi.uploadMasterList;

      const response = await uploadFn(selectedFile, force);

      if (response.data.success && response.data.data) {
        const uploadedFile = response.data.data;
        const fileId = (uploadedFile as { uploadId?: string }).uploadId || uploadedFile.id;
        setCurrentFileId(fileId);
        setUploadStage({ status: 'COMPLETED', message: t('upload:fileUpload.uploadComplete'), percentage: 100 });

        connectToProgressStream(fileId);
      } else {
        throw new Error(response.data.error || t('upload:fileUpload.uploadFailed'));
      }
    } catch (error: unknown) {
      // Check for HTTP 409 Conflict (duplicate file)
      if (axios.isAxiosError(error) && error.response?.status === 409) {
        const errorData = error.response.data as {
          message?: string;
          canReupload?: boolean;
          existingUpload?: {
            uploadId: string;
            fileName: string;
            uploadTimestamp: string;
            status: string;
            fileFormat: string;
          };
        };

        // Re-uploadable duplicate: show confirmation dialog
        if (errorData.canReupload && errorData.existingUpload) {
          setIsProcessing(false);
          setOverallStatus('IDLE');
          setReuploadDialog({
            show: true,
            existingUpload: {
              uploadId: errorData.existingUpload.uploadId,
              fileName: errorData.existingUpload.fileName || selectedFile.name,
              status: errorData.existingUpload.status || 'COMPLETED',
            },
          });
          return;
        }

        setIsProcessing(false);
        setOverallStatus('IDLE');
        setDuplicateWarningDialog({
          show: true,
          existingUpload: errorData.existingUpload ? {
            uploadId: errorData.existingUpload.uploadId,
            fileName: errorData.existingUpload.fileName || selectedFile.name,
            status: errorData.existingUpload.status || 'COMPLETED',
            fileFormat: errorData.existingUpload.fileFormat,
          } : undefined,
        });
        return;
      } else {
        // Other errors
        setUploadStage({ status: 'FAILED', message: t('upload:fileUpload.uploadFailed'), percentage: 0 });
        setOverallStatus('FAILED');
        setOverallMessage(t('upload:fileUpload.fileUploadFailed'));
        setErrorMessages([error instanceof Error ? error.message : t('upload:fileUpload.unknownError')]);
      }
      setIsProcessing(false);
    }
  };

  const handleUpload = () => doUpload(false);

  const handleForceReupload = () => {
    setReuploadDialog({ show: false });
    doUpload(true);
  };

  // v1.5.5: Polling backup mechanism - sync state from DB every 30s
  const syncStateFromDB = async (id: string) => {
    try {
      const response = await uploadApi.getDetail(id);
      if (!response.data?.success || !response.data.data) {
        if (import.meta.env.DEV) console.warn('[Polling] Failed to fetch upload details');
        return;
      }

      const upload = response.data.data as UploadedFile;
      if (import.meta.env.DEV) console.log('[Polling] Syncing state from DB:', upload.status, upload);

      // Update stage states based on DB data
      // Stage 1: Upload & Parse (always completed if we have the record)
      if (upload.status !== 'PENDING') {
        setUploadStage({ status: 'COMPLETED', message: t('upload:fileUpload.uploadComplete'), percentage: 100 });
        // For Master List: use processedEntries (extracted certificates)
        // For LDIF: use totalEntries (LDIF entries)
        const entriesCount = upload.fileFormat === 'ML' ? upload.processedEntries : upload.totalEntries;
        setParseStage({
          status: 'COMPLETED',
          message: t('upload:fileUpload.parsingComplete'),
          percentage: 100,
          details: t('upload:fileUpload.entriesProcessed', { num: entriesCount?.toLocaleString() ?? '0' })
        });
      }

      // Stage 2: Validate & DB + LDAP (check certificate counts)
      const hasCertificates = (upload.cscaCount || 0) + (upload.dscCount || 0) + (upload.dscNcCount || 0) + (upload.mlscCount || 0) > 0;
      const totalEntries = upload.totalEntries || 1;
      const processedEntries = upload.processedEntries || 0;

      if (upload.status === 'COMPLETED' || hasCertificates) {
        // Build detailed certificate breakdown
        const parts: string[] = [];
        if (upload.mlscCount) parts.push(`MLSC ${upload.mlscCount.toLocaleString()}`);
        if (upload.cscaCount) parts.push(`CSCA ${upload.cscaCount.toLocaleString()}`);
        if (upload.dscCount) parts.push(`DSC ${upload.dscCount.toLocaleString()}`);
        if (upload.dscNcCount) parts.push(`DSC_NC ${upload.dscNcCount.toLocaleString()}`);
        if (upload.crlCount) parts.push(`CRL ${upload.crlCount.toLocaleString()}`);
        if (upload.mlCount) parts.push(`ML ${upload.mlCount.toLocaleString()}`);

        if (upload.status === 'PROCESSING') {
          // In-progress: show live percentage from DB
          const pct = Math.min(Math.round(100 * processedEntries / totalEntries), 99);
          const details = parts.length > 0 ? `${parts.join(', ')} (${processedEntries.toLocaleString()}/${totalEntries.toLocaleString()})` : t('upload:fileUpload.entriesProcessing', { processed: processedEntries.toLocaleString(), total: totalEntries.toLocaleString() });
          setDbSaveStage({
            status: 'IN_PROGRESS',
            message: t('upload:fileUpload.processingPercent', { pct }),
            percentage: pct,
            details
          });
        } else {
          const details = parts.length > 0 ? `${t('upload:fileUpload.saveCompleteLabel')}: ${parts.join(', ')}` : t('upload:fileUpload.entriesSaved', { num: (upload.processedEntries || upload.totalEntries)?.toLocaleString() ?? '0' });
          setDbSaveStage({
            status: 'COMPLETED',
            message: t('upload:fileUpload.saveCompleteLabel'),
            percentage: 100,
            details
          });
        }
      }

      // Handle completion states
      if (upload.status === 'COMPLETED') {
        setOverallStatus('FINALIZED');
        setOverallMessage(t('upload:fileUpload.allProcessingComplete'));
        setIsProcessing(false);
        // Stop polling when completed
        if (pollingIntervalRef.current) {
          clearInterval(pollingIntervalRef.current);
          pollingIntervalRef.current = null;
        }
        // Close SSE connection
        if (sseRef.current) {
          sseRef.current.close();
          sseRef.current = null;
          setSseConnected(false);
        }
      } else if (upload.status === 'FAILED') {
        setOverallStatus('FAILED');
        setOverallMessage(t('upload:fileUpload.processingError'));
        setIsProcessing(false);
        // Stop polling on failure
        if (pollingIntervalRef.current) {
          clearInterval(pollingIntervalRef.current);
          pollingIntervalRef.current = null;
        }
        // Close SSE connection
        if (sseRef.current) {
          sseRef.current.close();
          sseRef.current = null;
          setSseConnected(false);
        }
      }
    } catch (error) {
      if (import.meta.env.DEV) console.error('[Polling] Error syncing state from DB:', error);
    }
  };

  // v1.5.5: Start polling backup mechanism (30s interval)
  const startPolling = (id: string) => {
    // Clear existing interval if any
    if (pollingIntervalRef.current) {
      clearInterval(pollingIntervalRef.current);
    }

    // Start polling every 30 seconds
    if (import.meta.env.DEV) console.log('[Polling] Starting 30s interval backup for uploadId:', id);
    pollingIntervalRef.current = window.setInterval(() => {
      if (!sseConnected) {
        if (import.meta.env.DEV) console.log('[Polling] SSE disconnected, using polling as primary source');
      }
      syncStateFromDB(id);
    }, 30000); // 30 seconds

    // Also sync immediately
    syncStateFromDB(id);
  };

  // v1.5.5: Stop polling when component unmounts or upload completes
  useEffect(() => {
    return () => {
      if (pollingIntervalRef.current) {
        clearInterval(pollingIntervalRef.current);
        pollingIntervalRef.current = null;
      }
      if (sseRef.current) {
        sseRef.current.close();
        sseRef.current = null;
      }
    };
  }, []);

  const connectToProgressStream = (id: string) => {
    const eventSource = createProgressEventSource(id);
    sseRef.current = eventSource;
    let reconnectAttempts = 0;
    const maxReconnectAttempts = 3;

    // Handle 'connected' event
    eventSource.addEventListener('connected', () => {
      reconnectAttempts = 0;
      setSseConnected(true);
      if (import.meta.env.DEV) console.log('[SSE] Connected to progress stream');
    });

    // Handle 'progress' events from backend
    eventSource.addEventListener('progress', (event) => {
      try {
        const data = (event as MessageEvent).data;
        const progress: UploadProgress = JSON.parse(data);
        handleProgressUpdate(progress);
      } catch {
        // Ignore parse errors
      }
    });

    // Fallback for unnamed events
    eventSource.onmessage = (event) => {
      try {
        const progress: UploadProgress = JSON.parse(event.data);
        handleProgressUpdate(progress);
      } catch {
        // Ignore parse errors
      }
    };

    eventSource.onerror = () => {
      if (import.meta.env.DEV) console.warn('[SSE] Connection error, closing...');
      setSseConnected(false);
      eventSource.close();

      // Try to reconnect if not too many attempts
      if (reconnectAttempts < maxReconnectAttempts && isProcessingRef.current) {
        reconnectAttempts++;
        if (import.meta.env.DEV) console.log(`[SSE] Reconnect attempt ${reconnectAttempts}/${maxReconnectAttempts}`);
        setTimeout(() => connectToProgressStream(id), 1000);
      } else {
        if (import.meta.env.DEV) console.log('[SSE] Max reconnect attempts reached, relying on polling');
        sseRef.current = null;
        // Don't set isProcessing to false - let polling continue
      }
    };

    // v1.5.5: Start polling backup alongside SSE
    startPolling(id);
  };

  // Format numbers in message string with locale-aware separators (e.g., "DSC 21440/29838" → "DSC 21,440/29,838")
  const formatMessageNumbers = (msg: string) => msg.replace(/\d+/g, m => Number(m).toLocaleString());

  // Translate validation reason to Korean
  const translateReason = (reason: string): string => {
    if (reason.includes('Trust chain signature verification failed')) return t('upload:dashboard.signatureVerificationFailed');
    if (reason.includes('Chain broken') || reason.includes('Failed to build trust chain')) return t('upload:dashboard.trustChainBroken');
    if (reason.includes('CSCA not found')) return t('report:trustChain.cscaNotFound');
    if (reason.includes('not yet valid')) return t('upload:dashboard.notYetValidPeriod');
    if (reason.includes('certificates expired')) return t('upload:dashboard.expiredValidSignature');
    return reason;
  };

  const handleProgressUpdate = (progress: UploadProgress) => {
    const { stage, stageName, percentage, processedCount, totalCount, errorMessage } = progress;
    const message = progress.message ? formatMessageNumbers(progress.message) : '';

    // Event log: only meaningful events (stage transitions, errors, milestones)
    {
      const now = new Date();
      const ts = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}.${String(now.getMilliseconds()).padStart(3, '0')}`;

      const addEntry = (eventName: string, detail: string, status: EventLogEntry['status']) => {
        setEventLogEntries(prev => {
          const next = [...prev, {
            id: ++eventIdRef.current, timestamp: ts, eventName, detail, status,
          }];
          return next.length > 500 ? next.slice(-500) : next;
        });
      };

      // 1) Errors always logged
      if (errorMessage) {
        addEntry(stage, errorMessage, 'fail');
      }
      // 2) Stage transitions (first occurrence of each stage)
      else if (stage !== lastStageRef.current) {
        const isComplete = stage.endsWith('_COMPLETED') || stage === 'COMPLETED';
        const isFail = stage === 'FAILED';
        const detail = message || stageName || (isComplete ? t('common:status.completed') : isFail ? t('common:status.failed') : t('upload:fileUpload.started'));
        addEntry(stage, detail, isFail ? 'fail' : isComplete ? 'success' : 'info');
      }
      // 3) Milestones every 10,000 entries (for long-running operations)
      else if (processedCount > 0 && totalCount > 0) {
        const milestone = Math.floor(processedCount / 10000) * 10000;
        if (milestone > 0 && milestone > lastMilestoneRef.current) {
          lastMilestoneRef.current = milestone;
          addEntry('MILESTONE', t('upload:fileUpload.milestoneProgress', { milestone: milestone.toLocaleString(), total: totalCount.toLocaleString(), pct: percentage }), 'info');
        }
      }

      lastStageRef.current = stage;

      // 4) Surface new processing errors from statistics as individual events
      if (progress.statistics?.recentErrors) {
        const newErrors = progress.statistics.recentErrors;
        const newCount = progress.statistics.totalErrorCount ?? 0;
        if (newCount > lastErrorCountRef.current) {
          // Log only newly appeared errors
          const addedCount = newCount - lastErrorCountRef.current;
          const recentNew = newErrors.slice(-addedCount);
          for (const err of recentNew) {
            const label = err.errorType === 'PARSE' ? t('upload:fileUpload.parseFailed')
              : err.errorType === 'DB_SAVE' ? t('upload:fileUpload.dbSaveFailed')
              : err.errorType === 'LDAP_SAVE' ? t('upload:fileUpload.ldapSaveFailed')
              : err.errorType;
            const detail = `[${err.countryCode}] ${err.certificateType} - ${err.message}`;
            addEntry(label, detail, err.errorType === 'LDAP_SAVE' ? 'warning' : 'fail');
          }
          lastErrorCountRef.current = newCount;
        }
      }

      // 5) Surface duplicate certificate count changes
      if (progress.statistics?.duplicateCount != null) {
        const dupCount = progress.statistics.duplicateCount;
        if (dupCount > lastDuplicateCountRef.current) {
          const newDups = dupCount - lastDuplicateCountRef.current;
          addEntry(t('upload:fileUpload.duplicateCert'), t('upload:fileUpload.duplicateSkipped', { newDups: newDups.toLocaleString(), total: dupCount.toLocaleString() }), 'warning');
          lastDuplicateCountRef.current = dupCount;
        }
      }

      // 6) Surface validation failure/pending reasons
      if (progress.statistics?.validationReasons) {
        const reasons = progress.statistics.validationReasons;
        const prev = lastValidationReasonsRef.current;
        for (const [key, count] of Object.entries(reasons)) {
          if (key === 'VALID' || key.startsWith('VALID')) continue; // skip successes
          const prevCount = prev[key] ?? 0;
          if (count > prevCount) {
            const isInvalid = key.startsWith('INVALID:');
            const isPending = key.startsWith('PENDING:');
            const reasonText = key.replace(/^(INVALID|EXPIRED_VALID|PENDING):\s*/, '');
            const label = isInvalid ? t('upload:fileUpload.validationFailed') : isPending ? t('upload:fileUpload.validationPending') : t('upload:fileUpload.expiredValidLabel');
            const translated = translateReason(reasonText);
            addEntry(label, `${translated} (${count.toLocaleString()}${t('common:unit.cases')})`, isInvalid ? 'fail' : 'warning');
          }
        }
        lastValidationReasonsRef.current = { ...reasons };
      }

      // 7) Per-certificate validation logs (real-time detail)
      if (progress.statistics?.recentValidationLogs) {
        const logs = progress.statistics.recentValidationLogs;
        const totalCount2 = progress.statistics.totalValidationLogCount ?? 0;
        if (totalCount2 > lastValidationLogCountRef.current) {
          // Use cumulative counter to determine how many new logs were added
          const addedCount = totalCount2 - lastValidationLogCountRef.current;
          const newLogs = logs.slice(-addedCount);
          for (const log of newLogs) {
            const statusIcon = log.validationStatus === 'VALID' ? '✓'
              : log.validationStatus === 'EXPIRED_VALID' ? '⚠'
              : log.validationStatus === 'INVALID' ? '✗'
              : log.validationStatus === 'PENDING' ? '?'
              : log.validationStatus === 'DUPLICATE' ? '↔' : '';
            const detail = `[${log.countryCode}] ${log.certificateType} ${statusIcon} ${log.validationStatus} — ${log.trustChainMessage || log.errorCode || ''}`;
            const status: EventLogEntry['status'] = log.validationStatus === 'VALID' ? 'success'
              : log.validationStatus === 'INVALID' ? 'fail'
              : log.validationStatus === 'DUPLICATE' ? 'info'
              : 'warning';
            addEntry(t('upload:fileUpload.validation'), detail, status);
          }
          lastValidationLogCountRef.current = totalCount2;
        }
      }
    }

    // Phase 4.4: Extract enhanced metadata fields
    if (progress.statistics) {
      setStatistics(progress.statistics);

      // Extract processing error data from statistics
      const stats = progress.statistics;
      if (stats.totalErrorCount && stats.totalErrorCount > 0) {
        setErrorCounts({
          total: stats.totalErrorCount,
          parse: stats.parseErrorCount ?? 0,
          db: stats.dbSaveErrorCount ?? 0,
          ldap: stats.ldapSaveErrorCount ?? 0,
        });
        if (stats.recentErrors && stats.recentErrors.length > 0) {
          setProcessingErrors(stats.recentErrors);
        }
      }
    }
    if (progress.currentCertificate) {
      setCurrentCertificate(progress.currentCertificate);
    }
    if (progress.currentCompliance) {
      setCurrentCompliance(progress.currentCompliance);
    }

    // Determine status from stage
    const getStatus = (stageStr: string): StageStatus['status'] => {
      if (stageStr.endsWith('_STARTED') || stageStr.endsWith('_IN_PROGRESS')) return 'IN_PROGRESS';
      if (stageStr.endsWith('_COMPLETED') || stageStr === 'COMPLETED') return 'COMPLETED';
      if (stageStr === 'FAILED') return 'FAILED';
      return 'IDLE';
    };

    // Convert backend overall percentage (0-100) to stage-specific percentage (0-100)
    // Backend percentage ranges per stage:
    // - PARSING: 10-50% (range: 40)
    // - VALIDATION + DB_SAVING combined: 55-85% (range: 30) -> shown as one "검증 및 DB 저장" step
    // - LDAP_SAVING: 87-100% (range: 13)
    const calculateStagePercentage = (stageStr: string, overallPercent: number): number => {
      if (stageStr.startsWith('PARSING')) {
        // Map 10-50 to 0-100
        return Math.min(100, Math.max(0, Math.round((overallPercent - 10) * 100 / 40)));
      } else if (stageStr.startsWith('VALIDATION') || stageStr.startsWith('DB_SAVING')) {
        // LDIF processing sends entire range as VALIDATION_IN_PROGRESS (60-99%)
        // DB+LDAP save happens inline, no separate DB_SAVING/LDAP_SAVING stages
        if (stageStr.startsWith('VALIDATION')) {
          // Map 60-99 -> 0-100
          return Math.min(100, Math.max(0, Math.round((overallPercent - 60) * 100 / 39)));
        } else {
          // DB_SAVING fallback (used by non-LDIF upload flows)
          return Math.min(100, Math.max(0, Math.round((overallPercent - 72) * 100 / 13)));
        }
      } else if (stageStr.startsWith('LDAP_SAVING')) {
        // Map 87-100 to 0-100
        return Math.min(100, Math.max(0, Math.round((overallPercent - 87) * 100 / 13)));
      }
      return overallPercent;
    };

    // v1.5.1: Use backend message directly for detailed breakdown
    // Backend sends detailed messages like "처리 중: CSCA 525, DSC 0, CRL 0"
    const stagePercent = calculateStagePercentage(stage, percentage);
    const status = getStatus(stage);

    // v1.5.10: Use backend message as details if it contains detailed information
    // Backend sends messages like "처리 중: CSCA 100/500, DSC 200/1000, DSC_NC 50/100, CRL 10/50, ML 5/10"
    let details: string | undefined;
    if (message && (message.includes('CSCA') || message.includes('DSC') || message.includes('CRL') || message.includes('ML') || message.includes('LDAP'))) {
      // Backend message contains detailed breakdown, use it directly
      details = message;
      if (import.meta.env.DEV) console.log(`[FileUpload] Using detailed message as details: "${details}"`);
    } else if (stage.endsWith('_COMPLETED') || stage === 'COMPLETED') {
      // Show final count on completion (use processedCount if available, fallback to totalCount)
      const count = processedCount || totalCount;
      if (count > 0) {
        details = t('upload:fileUpload.entriesProcessed', { num: count.toLocaleString() });
      }
    } else if (processedCount > 0 && totalCount > 0) {
      // Show progress during processing
      details = `${processedCount.toLocaleString()}/${totalCount.toLocaleString()}`;
    }

    const stageStatus: StageStatus = {
      status,
      message: stageName || message,
      percentage: status === 'COMPLETED' ? 100 : stagePercent,
      details,
    };

    // Map backend stage names to frontend stages
    // Note: VALIDATION and DB_SAVING are combined into one "검증 및 DB 저장" step (dbSaveStage)
    if (stage.startsWith('UPLOAD')) {
      setUploadStage(stageStatus);
    } else if (stage.startsWith('PARSING')) {
      // Mark upload as completed when parsing starts
      setUploadStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      // For PARSING_COMPLETED, add a small delay to ensure DB status is updated
      if (stage === 'PARSING_COMPLETED') {
        // Keep button disabled for 1 second after PARSING_COMPLETED to ensure DB update completes
        setParseStage({ ...stageStatus, status: 'IN_PROGRESS' });
        setTimeout(() => {
          setParseStage(stageStatus);  // Set to COMPLETED after delay
        }, 1000);
      } else {
        setParseStage(stageStatus);
      }
    } else if (stage.startsWith('VALIDATION')) {
      // VALIDATION is part of the combined "검증 및 DB 저장" step
      setUploadStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      // Keep existing parseStage details if available
      setParseStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      // Update dbSaveStage with validation progress (0-50%)
      const validationCount = processedCount || totalCount;
      const validationStatus: StageStatus = {
        status: stage === 'VALIDATION_COMPLETED' ? 'IN_PROGRESS' : stageStatus.status,  // Keep IN_PROGRESS until DB_SAVING_COMPLETED
        message: stageName || message,
        percentage: stagePercent,
        details: stage === 'VALIDATION_COMPLETED' && validationCount > 0 ? t('upload:fileUpload.validationCount', { num: validationCount.toLocaleString() }) : details,
      };
      setDbSaveStage(validationStatus);
    } else if (stage.startsWith('DB_SAVING')) {
      // DB_SAVING continues the combined "검증 및 DB 저장" step (50-100%)
      setUploadStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      setParseStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      const saveCount = processedCount || totalCount;
      const dbStatus: StageStatus = {
        status: stage === 'DB_SAVING_COMPLETED' ? 'COMPLETED' : 'IN_PROGRESS',
        message: stageName || message,
        percentage: stage === 'DB_SAVING_COMPLETED' ? 100 : stagePercent,
        details: stage === 'DB_SAVING_COMPLETED' && saveCount > 0 ? t('upload:fileUpload.entriesSavedShort', { num: saveCount.toLocaleString() }) : details,
      };
      setDbSaveStage(dbStatus);
    } else if (stage.startsWith('LDAP_SAVING')) {
      // v1.5.1: LDAP_SAVING is part of the combined "검증 및 DB 저장" step
      // This stage is sent after DB saving completes to show LDAP save summary
      setUploadStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      setParseStage(prev => prev.status !== 'COMPLETED' ? { ...prev, status: 'COMPLETED', percentage: 100 } : prev);
      const ldapStatus: StageStatus = {
        status: stage === 'LDAP_SAVING_COMPLETED' ? 'COMPLETED' : 'IN_PROGRESS',
        message: stageName || message,
        percentage: stage === 'LDAP_SAVING_COMPLETED' ? 100 : stagePercent,
        details: details || message,  // Use detailed LDAP message from backend
      };
      setDbSaveStage(ldapStatus);
    } else if (stage === 'COMPLETED') {
      // v1.5.10: DB save completed = LDAP also completed (simultaneous)
      // Mark all stages as completed with final message
      setUploadStage(prev => ({ ...prev, status: 'COMPLETED', percentage: 100 }));
      setParseStage(prev => ({ ...prev, status: 'COMPLETED', percentage: 100 }));
      // v1.5.10: Use backend completion message as details if it contains breakdown
      setDbSaveStage(prev => {
        const completionDetails = (message && (message.includes('CSCA') || message.includes('DSC') || message.includes('CRL') || message.includes('ML')))
          ? message
          : (prev.details || t('upload:fileUpload.entriesSaved', { num: totalCount.toLocaleString() }));
        return { ...prev, status: 'COMPLETED', percentage: 100, details: completionDetails };
      });
      setOverallStatus('FINALIZED');
      setOverallMessage(message || t('upload:fileUpload.allProcessingComplete'));
      setIsProcessing(false);
      // Clear localStorage when all stages completed
      localStorage.removeItem('currentUploadId');
    } else if (stage === 'FAILED') {
      setOverallStatus('FAILED');
      setOverallMessage(errorMessage || message);
      setIsProcessing(false);
      // Clear localStorage on failure
      localStorage.removeItem('currentUploadId');
    }
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Re-upload Confirmation Dialog */}
      {reuploadDialog.show && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50 backdrop-blur-sm">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-md w-full mx-4">
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2.5">
              <div className="p-1.5 rounded-lg bg-amber-100 dark:bg-amber-900/30">
                <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400" />
              </div>
              <h3 className="text-base font-bold text-gray-900 dark:text-white">
                {t('upload:fileUpload.reuploadConfirm')}
              </h3>
            </div>
            <div className="px-5 py-4 space-y-3">
              <p className="text-sm text-gray-600 dark:text-gray-300">{t('upload:fileUpload.previouslyUploaded')}</p>
              <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 space-y-1 text-sm text-gray-600 dark:text-gray-300">
                <p><span className="font-medium">{t('common:label.fileName')}:</span> {reuploadDialog.existingUpload?.fileName}</p>
                <p><span className="font-medium">{ t('certificate:detail.status_label') }</span> {reuploadDialog.existingUpload?.status}</p>
                <p><span className="font-medium">{t('common:label.uploadIdColon')}</span> <span className="font-mono text-xs">{reuploadDialog.existingUpload?.uploadId}</span></p>
              </div>
              <p className="text-sm text-amber-600 dark:text-amber-400 font-medium">
                {t('upload:fileUpload.reuploadWarning')}
                {' '}{t('upload:fileUpload.existingRecordKept')}
              </p>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => setReuploadDialog({ show: false })}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors border border-gray-200 dark:border-gray-600"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleForceReupload}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-white bg-amber-600 hover:bg-amber-700 transition-colors"
              >
                {t('upload:fileUpload.reupload')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Duplicate File Warning Dialog */}
      {duplicateWarningDialog.show && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50 backdrop-blur-sm">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-md w-full mx-4">
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2.5">
              <div className="p-1.5 rounded-lg bg-red-100 dark:bg-red-900/30">
                <XCircle className="w-4 h-4 text-red-600 dark:text-red-400" />
              </div>
              <h3 className="text-base font-bold text-gray-900 dark:text-white">
                {t('upload:fileUpload.duplicateFile')}
              </h3>
            </div>
            <div className="px-5 py-4 space-y-3">
              <p className="text-sm text-gray-600 dark:text-gray-300">{t('upload:fileUpload.duplicateFileAlreadyProcessed')}</p>
              {duplicateWarningDialog.existingUpload && (
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 space-y-1 text-sm text-gray-600 dark:text-gray-300">
                  <p><span className="font-medium">{t('common:label.fileName')}:</span> {duplicateWarningDialog.existingUpload.fileName}</p>
                  <p><span className="font-medium">{ t('certificate:detail.status_label') }</span> {duplicateWarningDialog.existingUpload.status}</p>
                  {duplicateWarningDialog.existingUpload.fileFormat && (
                    <p><span className="font-medium">{t('upload:fileUpload.fileFormat')}:</span> {duplicateWarningDialog.existingUpload.fileFormat}</p>
                  )}
                  <p><span className="font-medium">{t('common:label.uploadIdColon')}</span> <span className="font-mono text-xs">{duplicateWarningDialog.existingUpload.uploadId}</span></p>
                </div>
              )}
              <p className="text-sm text-red-600 dark:text-red-400">
                {t('upload:fileUpload.duplicateFileCannotReupload')}
              </p>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => navigate('/upload-history')}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors border border-gray-200 dark:border-gray-600"
              >
                {t('upload:fileUpload.viewUploadHistory')}
              </button>
              <button
                onClick={() => setDuplicateWarningDialog({ show: false })}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-white bg-blue-600 hover:bg-blue-700 transition-colors"
              >
                {t('common:confirm.title')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-lg">
            <Upload className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              {t('upload:fileUpload.pkdFileUpload')}
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('upload:fileUpload.pkdFileUploadDesc')}
            </p>
          </div>
        </div>
      </div>

      {/* CSCA Certificate Status Banner */}
      {cscaCount !== null && (
        cscaCount === 0 ? (
          <div className="mb-6 flex items-start gap-3 px-4 py-3 rounded-xl bg-gradient-to-r from-red-50 to-orange-50 dark:from-red-900/20 dark:to-orange-900/20 border border-red-200 dark:border-red-800">
            <AlertTriangle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
            <div>
              <p className="text-sm font-semibold text-red-800 dark:text-red-300">{t('pa:verify.cscaNotRegistered')}</p>
              <p className="text-xs text-red-600 dark:text-red-400 mt-0.5">
                {t('upload:fileUpload.cscaUploadRequired')}
              </p>
            </div>
          </div>
        ) : (
          <div className="mb-6 flex items-center gap-2 px-4 py-2.5 rounded-xl bg-gradient-to-r from-green-50 to-emerald-50 dark:from-green-900/20 dark:to-emerald-900/20 border border-green-200 dark:border-green-800">
            <CheckCircle2 className="w-4 h-4 text-green-500 flex-shrink-0" />
            <p className="text-xs text-green-700 dark:text-green-400">
              {t('upload:fileUpload.cscaRegisteredPrefix')} <span className="font-semibold">{cscaCount.toLocaleString()}</span> {t('upload:fileUpload.cscaRegisteredSuffix')}
            </p>
          </div>
        )
      )}

      {/* Row 1: Upload Result Card — hidden until upload completes */}
      {overallStatus === 'FINALIZED' && (
        <div className="mb-6 rounded-2xl bg-white dark:bg-gray-800 shadow-lg border border-teal-200 dark:border-teal-800 overflow-hidden">
          {/* Success Header */}
          <div className="bg-gradient-to-r from-teal-500 to-emerald-500 px-6 py-3">
            <div className="flex items-center gap-3">
              <div className="p-2 rounded-lg bg-white/20">
                <CheckCircle className="w-5 h-5 text-white" />
              </div>
              <div className="flex-1 min-w-0">
                <h3 className="text-base font-bold text-white">{t('upload:fileUpload.uploadProcessResult')}</h3>
                <p className="text-sm text-teal-100">{overallMessage}</p>
              </div>
              <button
                onClick={() => navigate('/upload-history')}
                className="px-4 py-1.5 rounded-lg text-sm font-medium text-teal-700 bg-white/90 hover:bg-white transition-colors shrink-0"
              >
                {t('upload:fileUpload.viewUploadHistory')}
              </button>
            </div>
          </div>

          {/* Statistics + Validation Reasons */}
          <div className="p-5 space-y-4">
            {statistics && (
              <RealTimeStatisticsPanel
                statistics={statistics}
                isProcessing={false}
                uploadId={currentFileId ?? undefined}
              />
            )}

            {/* Validation Reasons Summary */}
            {statistics?.validationReasons && Object.keys(statistics.validationReasons).length > 0 && (() => {
              const reasons = statistics.validationReasons!;
              const validCount = reasons['VALID'] ?? 0;
              const expiredValid = statistics.expiredValidCount ?? 0;
              const invalidReasons: [string, number][] = [];
              const pendingReasons: [string, number][] = [];
              let invalidTotal = 0;
              let pendingTotal = 0;
              for (const [key, count] of Object.entries(reasons)) {
                if (key.startsWith('INVALID:')) {
                  const reason = key.replace('INVALID: ', '');
                  invalidReasons.push([translateReason(reason), count]);
                  invalidTotal += count;
                } else if (key.startsWith('PENDING:')) {
                  const reason = key.replace('PENDING: ', '');
                  pendingReasons.push([translateReason(reason), count]);
                  pendingTotal += count;
                }
              }
              if (invalidTotal === 0 && pendingTotal === 0 && expiredValid === 0) return null;
              return (
                <div className="p-4 rounded-xl bg-gray-50 dark:bg-gray-800/50 border border-gray-200 dark:border-gray-700">
                  <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2">{t('upload:fileUpload.validationReasonDetail')}</h4>
                  <div className="space-y-1 text-sm">
                    <div className="flex justify-between text-green-600 dark:text-green-400">
                      <span>✓ {t('upload:fileUpload.validSuccess')} (VALID)</span>
                      <span className="font-medium">{validCount.toLocaleString()}{t('common:unit.cases')}</span>
                    </div>
                    {expiredValid > 0 && (
                      <div className="flex justify-between text-amber-600 dark:text-amber-400">
                        <span>✓ {t('upload:fileUpload.expiredValid')} (EXPIRED_VALID)</span>
                        <span className="font-medium">{expiredValid.toLocaleString()}{t('common:unit.cases')}</span>
                      </div>
                    )}
                    {invalidTotal > 0 && (
                      <>
                        <div className="flex justify-between text-red-600 dark:text-red-400">
                          <span>✗ {t('upload:fileUpload.validFailed')} (INVALID)</span>
                          <span className="font-medium">{invalidTotal.toLocaleString()}{t('common:unit.cases')}</span>
                        </div>
                        {invalidReasons.map(([reason, count]) => (
                          <div key={reason} className="flex justify-between text-red-500 dark:text-red-500 pl-4">
                            <span className="text-xs">· {reason}</span>
                            <span className="text-xs font-medium">{count.toLocaleString()}{t('common:unit.cases')}</span>
                          </div>
                        ))}
                      </>
                    )}
                    {pendingTotal > 0 && (
                      <>
                        <div className="flex justify-between text-gray-500 dark:text-gray-400">
                          <span>○ {t('upload:fileUpload.validPending')} (PENDING)</span>
                          <span className="font-medium">{pendingTotal.toLocaleString()}{t('common:unit.cases')}</span>
                        </div>
                        {pendingReasons.map(([reason, count]) => (
                          <div key={reason} className="flex justify-between text-gray-400 dark:text-gray-500 pl-4">
                            <span className="text-xs">· {reason}</span>
                            <span className="text-xs font-medium">{count.toLocaleString()}{t('common:unit.cases')}</span>
                          </div>
                        ))}
                      </>
                    )}
                  </div>
                </div>
              );
            })()}
          </div>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6 items-start">
        {/* Left Column: Upload Form */}
        <div className="col-span-1">
          <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
            <div className="flex flex-col p-6">
              <div className="flex items-center gap-3 mb-4">
                <div className="p-2.5 rounded-xl bg-indigo-50 dark:bg-indigo-900/30">
                  <CloudUpload className="w-6 h-6 text-indigo-500" />
                </div>
                <div>
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">{t('fileUpload.title')}</h2>
                  <p className="text-xs text-gray-500 dark:text-gray-400">
                    {t('upload:fileUpload.uploadLdifMl')}
                  </p>
                </div>
              </div>

              {/* File Drop Zone */}
              <div
                className={cn(
                  'relative border-2 border-dashed rounded-xl p-5 text-center cursor-pointer transition-all duration-300',
                  isDragging
                    ? 'border-indigo-500 bg-indigo-50 dark:bg-indigo-900/20 scale-[1.02]'
                    : selectedFile
                    ? 'border-blue-400 bg-blue-50 dark:bg-blue-900/20'
                    : 'border-gray-300 dark:border-gray-600 hover:border-indigo-400 hover:bg-gray-50 dark:hover:bg-gray-700/50'
                )}
                onDragOver={(e) => {
                  e.preventDefault();
                  setIsDragging(true);
                }}
                onDragLeave={(e) => {
                  e.preventDefault();
                  setIsDragging(false);
                }}
                onDrop={handleDrop}
                onClick={() => fileInputRef.current?.click()}
              >
                <input
                  ref={fileInputRef}
                  type="file"
                  accept=".ldif,.ml,.bin"
                  className="hidden"
                  onChange={handleFileInputChange}
                />
                <div className="flex flex-col items-center justify-center space-y-2">
                  <div className={cn(
                    'p-3 rounded-full transition-colors',
                    selectedFile ? 'bg-blue-100 dark:bg-blue-900/40' : 'bg-gray-100 dark:bg-gray-700'
                  )}>
                    <CloudUpload
                      className={cn(
                        'w-8 h-8 transition-colors',
                        selectedFile ? 'text-blue-500' : 'text-gray-400'
                      )}
                    />
                  </div>
                  {!selectedFile ? (
                    <>
                      <p className="text-gray-600 dark:text-gray-300 font-medium">
                        {t('upload:fileUpload.dragOrClick')}
                      </p>
                      <p className="text-xs text-gray-400 dark:text-gray-500">
                        {t('upload:fileUpload.supportedFormats', { formats: 'LDIF, Master List' })}
                      </p>
                    </>
                  ) : (
                    <div className="text-center space-y-1">
                      <p className="font-semibold text-gray-900 dark:text-white">{selectedFile.name}</p>
                      <p className="text-sm text-gray-500">{formatFileSize(selectedFile.size)}</p>
                      <span className={cn(
                        'inline-block px-2 py-0.5 rounded text-xs font-medium',
                        selectedFile.name.toLowerCase().endsWith('.ldif')
                          ? 'bg-purple-100 text-purple-700 dark:bg-purple-900/40 dark:text-purple-300'
                          : 'bg-orange-100 text-orange-700 dark:bg-orange-900/40 dark:text-orange-300'
                      )}>
                        {selectedFile.name.toLowerCase().endsWith('.ldif') ? 'LDIF' : 'Master List'}
                      </span>
                    </div>
                  )}
                </div>
              </div>

              {/* File Type Warning */}
              {selectedFile && !isValidFileType(selectedFile) && (
                <div className="mt-3 p-3 bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-lg flex items-center gap-2">
                  <AlertTriangle className="w-5 h-5 text-yellow-500 shrink-0" />
                  <span className="text-sm text-yellow-700 dark:text-yellow-400">
                    {t('upload:fileUpload.unsupportedFileType')}
                  </span>
                </div>
              )}

              {/* Phase 4.4: File Type Information */}
              {selectedFile && isValidFileType(selectedFile) && (
                <div className="mt-3 p-3 bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg">
                  <div className="flex items-start gap-2">
                    <FileText className="w-4 h-4 text-blue-600 dark:text-blue-400 mt-0.5 shrink-0" />
                    <div className="flex-1">
                      <p className="text-sm text-blue-700 dark:text-blue-300 mb-2">
                        {getFileTypeDescription(selectedFile)}
                      </p>
                      <div className="flex flex-wrap gap-1">
                        {getExpectedCertificateTypes(selectedFile).map((type) => (
                          <span
                            key={type}
                            className="text-xs px-2 py-0.5 rounded bg-blue-100 dark:bg-blue-900/40 text-blue-700 dark:text-blue-300 font-medium"
                          >
                            {type}
                          </span>
                        ))}
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* Action Buttons */}
              <div className="flex justify-end gap-3 mt-5 pt-4 border-t border-gray-100 dark:border-gray-700">
                <button
                  onClick={() => navigate('/upload-history')}
                  className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium transition-all duration-200 border text-gray-700 dark:text-gray-300 border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700"
                >
                  <Clock className="w-4 h-4" />
                  {t('upload:history.title')}
                </button>
                <button
                  onClick={handleUpload}
                  disabled={isProcessing || overallStatus === 'FINALIZED' || !selectedFile || (selectedFile && !isValidFileType(selectedFile))}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 transition-all duration-200 shadow-md hover:shadow-lg disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  {isProcessing ? (
                    <Loader2 className="w-4 h-4 animate-spin" />
                  ) : (
                    <Upload className="w-4 h-4" />
                  )}
                  {t('upload:fileUpload.upload')}
                </button>
              </div>

              {/* ── Processing Progress ── */}
              <div className="mt-5 pt-5 border-t border-gray-200 dark:border-gray-700">
                {/* Header */}
                <div className="flex items-center gap-3 mb-5">
                  <div className="p-2.5 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                    <FileText className="w-5 h-5 text-cyan-500" />
                  </div>
                  <div className="flex-1 min-w-0">
                    <h3 className="text-lg font-bold text-gray-900 dark:text-white">{t('upload:fileUpload.processingProgress')}</h3>
                    {selectedFile && (
                      <p className="font-mono text-xs text-gray-500 dark:text-gray-400 truncate">
                        {selectedFile.name}
                      </p>
                    )}
                  </div>
                  {/* Status Badge */}
                  {overallStatus !== 'IDLE' && (
                    <span className={cn(
                      'px-2.5 py-1 rounded-full text-xs font-semibold',
                      overallStatus === 'PROCESSING' && 'bg-blue-100 text-blue-700 dark:bg-blue-900/40 dark:text-blue-300',
                      overallStatus === 'FINALIZED' && 'bg-teal-100 text-teal-700 dark:bg-teal-900/40 dark:text-teal-300',
                      overallStatus === 'FAILED' && 'bg-red-100 text-red-700 dark:bg-red-900/40 dark:text-red-300',
                    )}>
                      {overallStatus === 'PROCESSING' && t('common:status.processing')}
                      {overallStatus === 'FINALIZED' && t('common:status.completed')}
                      {overallStatus === 'FAILED' && t('common:status.failed')}
                    </span>
                  )}
                </div>

                {/* Horizontal Stepper */}
                <div className="py-2">
                  <Stepper
                    steps={steps}
                    orientation="horizontal"
                    size="md"
                    showProgress={true}
                  />
                </div>

                {/* Currently Processing Certificate (inline in progress card) */}
                {currentCertificate && (overallStatus === 'PROCESSING' || dbSaveStage.status === 'IN_PROGRESS') && (
                  <div className="mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
                    <h4 className="font-bold text-sm mb-3 text-gray-700 dark:text-gray-300">{t('upload:fileUpload.currentlyProcessingCert')}</h4>
                    <CurrentCertificateCard
                      certificate={currentCertificate}
                      compliance={currentCompliance || undefined}
                      compact={true}
                    />
                  </div>
                )}

                {/* Final Status — FAILED only (FINALIZED shown in top summary card) */}
                {overallStatus === 'FAILED' && (
                  <div className="mt-4 p-4 rounded-xl flex items-start gap-3 bg-gradient-to-r from-red-50 to-orange-50 dark:from-red-900/20 dark:to-orange-900/20 border border-red-200 dark:border-red-800">
                    <div className="p-2 rounded-lg shrink-0 bg-red-100 dark:bg-red-900/40">
                      <AlertTriangle className="w-5 h-5 text-red-600 dark:text-red-400" />
                    </div>
                    <div>
                      <h4 className="font-semibold text-sm text-red-700 dark:text-red-400">{t('upload:detail.processFailed')}</h4>
                      <p className="text-sm mt-0.5 text-red-600 dark:text-red-500">{overallMessage}</p>
                    </div>
                  </div>
                )}


                {/* General Error Messages */}
                {errorMessages.length > 0 && (
                  <div className="mt-4 p-4 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl">
                    <h4 className="font-bold text-sm text-red-700 dark:text-red-400 mb-2 flex items-center gap-2">
                      <XCircle className="w-4 h-4" />
                      {t('upload:fileUpload.errorDetail')}
                    </h4>
                    <ul className="text-sm text-red-600 dark:text-red-300 space-y-1">
                      {errorMessages.map((msg, idx) => (
                        <li key={idx} className="flex items-start gap-2">
                          <span className="text-red-400 mt-1">•</span>
                          <span>{msg}</span>
                        </li>
                      ))}
                    </ul>
                  </div>
                )}
              </div>
            </div>
          </div>
        </div>

        {/* Right Column: Event Log */}
        <div className="col-span-1">
          {(eventLogEntries.length > 0 || overallStatus === 'PROCESSING' || overallStatus === 'FINALIZED') && (
            <div className="space-y-6">
              <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-4">
                <EventLog
                  events={eventLogEntries}
                  onClear={() => { setEventLogEntries([]); eventIdRef.current = 0; lastStageRef.current = ''; lastErrorCountRef.current = 0; lastDuplicateCountRef.current = 0; lastValidationReasonsRef.current = {}; lastMilestoneRef.current = 0; lastValidationLogCountRef.current = 0; }}
                />
              </div>

              {errorCounts.total > 0 && (
                <ProcessingErrorsPanel
                  errors={processingErrors}
                  totalErrorCount={errorCounts.total}
                  parseErrorCount={errorCounts.parse}
                  dbSaveErrorCount={errorCounts.db}
                  ldapSaveErrorCount={errorCounts.ldap}
                  isProcessing={overallStatus === 'PROCESSING'}
                />
              )}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

export default FileUpload;
