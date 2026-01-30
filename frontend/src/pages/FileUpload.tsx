import { useState, useRef, useCallback, useMemo, useEffect } from 'react';
import { useNavigate, useSearchParams } from 'react-router-dom';
import axios from 'axios';
import {
  Upload,
  CloudUpload,
  Clock,
  CheckCircle,
  XCircle,
  AlertTriangle,
  Play,
  FileText,
  Loader2,
  Upload as UploadIcon,
  FileSearch,
  Database,
} from 'lucide-react';
import { uploadApi, createProgressEventSource } from '@/services/api';
import type { ProcessingMode, UploadProgress, UploadedFile, ValidationStatistics, CertificateMetadata, IcaoComplianceStatus } from '@/types';
import { cn } from '@/utils/cn';
import { Stepper, type Step, type StepStatus } from '@/components/common/Stepper';
import { RealTimeStatisticsPanel } from '@/components/RealTimeStatisticsPanel';
import { CurrentCertificateCard } from '@/components/CurrentCertificateCard';

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
  const navigate = useNavigate();
  const [searchParams] = useSearchParams();
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [processingMode, setProcessingMode] = useState<ProcessingMode>('AUTO');
  const [isProcessing, setIsProcessing] = useState(false);
  const [uploadId, setUploadId] = useState<string | null>(null);

  // Stage statuses for 2-stage MANUAL mode (v1.5.0)
  // Stage 1: Upload & Parse
  // Stage 2: Validate & Save (DB + LDAP simultaneously)
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

  // Phase 4.4: Enhanced metadata tracking
  const [statistics, setStatistics] = useState<ValidationStatistics | null>(null);
  const [currentCertificate, setCurrentCertificate] = useState<CertificateMetadata | null>(null);
  const [currentCompliance, setCurrentCompliance] = useState<IcaoComplianceStatus | null>(null);

  // Restore upload state on page load (for MANUAL mode)
  useEffect(() => {
    const restoreUploadState = async () => {
      // CRITICAL: URL parameter takes precedence over localStorage
      const urlUploadId = searchParams.get('uploadId');
      if (urlUploadId) {
        // Clear localStorage to avoid conflict
        localStorage.removeItem('currentUploadId');
        return; // Let the URL parameter handling take over
      }

      const savedUploadId = localStorage.getItem('currentUploadId');
      if (!savedUploadId) return;

      try {
        const response = await uploadApi.getDetail(savedUploadId);
        if (!response.data?.success || !response.data.data) {
          localStorage.removeItem('currentUploadId');
          return;
        }

        const upload = response.data.data as UploadedFile;

        // Only restore MANUAL mode uploads that are not FAILED or fully COMPLETED
        if (upload.processingMode !== 'MANUAL') {
          localStorage.removeItem('currentUploadId');
          return;
        }

        // v1.5.0: MANUAL mode is 2-stage, DB save = LDAP save (simultaneous)
        // All stages completed if status is COMPLETED
        if (upload.status === 'FAILED' || upload.status === 'COMPLETED') {
          localStorage.removeItem('currentUploadId');
          return;
        }

        // Restore state
        setUploadId(savedUploadId);
        setProcessingMode('MANUAL');
        setSelectedFile(new File([], upload.fileName));  // Dummy file for display

        // Determine stage states based on DB data
        // Stage 1 (Upload): Always completed if we have the upload record
        setUploadStage({ status: 'COMPLETED', message: '파일 업로드 완료', percentage: 100 });

        // Parse stage: Only completed if totalEntries > 0 (parsing was actually done)
        if ((upload.totalEntries || 0) > 0) {
          // For Master List: use processedEntries (extracted certificates)
          // For LDIF: use totalEntries (LDIF entries)
          const entriesCount = upload.fileFormat === 'ML' ? upload.processedEntries : upload.totalEntries;
          setParseStage({
            status: 'COMPLETED',
            message: '파싱 완료',
            percentage: 100,
            details: `${entriesCount}건 처리`
          });
        } else {
          // Parsing not started yet - keep IDLE state
          setParseStage({
            status: 'IDLE',
            message: '파싱 대기 중',
            percentage: 0
          });
        }

        // Stage 2 (Validate & DB + LDAP): Check if certificates exist in DB
        // v1.5.0: DB and LDAP are saved simultaneously, so DB save = completion
        const hasCertificates = (upload.cscaCount || 0) + (upload.dscCount || 0) + (upload.dscNcCount || 0) + (upload.mlscCount || 0) > 0;
        if (hasCertificates) {
          // Build detailed certificate breakdown
          const certDetails = [];
          if (upload.mlscCount) certDetails.push(`MLSC: ${upload.mlscCount}`);  // Master List Signer Certificate (v2.1.1)
          if (upload.cscaCount) certDetails.push(`CSCA: ${upload.cscaCount}`);
          if (upload.dscCount) certDetails.push(`DSC: ${upload.dscCount}`);
          if (upload.dscNcCount) certDetails.push(`DSC_NC: ${upload.dscNcCount}`);
          if (upload.crlCount) certDetails.push(`CRL: ${upload.crlCount}`);
          if (upload.mlCount) certDetails.push(`ML: ${upload.mlCount}`);

          setDbSaveStage({
            status: 'COMPLETED',
            message: 'DB + LDAP 저장 완료',
            percentage: 100,
            details: certDetails.join(', ')
          });
        } else {
          setDbSaveStage({
            status: 'IDLE',
            message: 'DB + LDAP 저장 대기 중',
            percentage: 0
          });
        }

        setOverallStatus('PROCESSING');
        setOverallMessage(`업로드 재개: ${upload.fileName}`);

        console.log('Upload state restored:', savedUploadId, upload);
      } catch (error) {
        console.error('Failed to restore upload state:', error);
        localStorage.removeItem('currentUploadId');
      }
    };

    restoreUploadState();
  }, [searchParams]);

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
        label: '파일 업로드',
        description: uploadStage.message || '서버로 파일 전송',
        status: toStepStatus(uploadStage.status),
        progress: uploadStage.percentage,
        details: uploadStage.details,
        icon: <UploadIcon className="w-3.5 h-3.5" />,
      },
      {
        id: 'parse',
        label: '파일 파싱',
        description: parseStage.message || 'LDIF/Master List 구문 분석',
        status: toStepStatus(parseStage.status),
        progress: parseStage.percentage,
        details: parseStage.details,
        icon: <FileSearch className="w-3.5 h-3.5" />,
      },
      {
        id: 'database',
        label: '검증 및 저장 (DB + LDAP)',
        description: dbSaveStage.message || '인증서 검증 및 DB/LDAP 동시 저장',
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
      // Master List files contain MLSC + CSCA (including link certificates)
      return ['MLSC', 'CSCA', 'Link Cert'];
    } else if (name.endsWith('.ldif')) {
      // LDIF files can contain DSC, DSC_NC, CSCA, CRL, and embedded Master Lists
      return ['DSC', 'DSC_NC', 'CSCA', 'CRL', 'Master List'];
    }
    return [];
  }, []);

  const getFileTypeDescription = useCallback((file: File | null): string => {
    if (!file) return '';
    const name = file.name.toLowerCase();

    if (name.endsWith('.ml') || name.endsWith('.bin')) {
      return 'Master List 서명 인증서(MLSC)와 국가 인증 기관 인증서(CSCA)가 포함됩니다.';
    } else if (name.endsWith('.ldif')) {
      return 'LDIF 파일은 다양한 인증서 유형(DSC, CSCA, CRL 등)을 포함할 수 있습니다.';
    }
    return '';
  }, []);

  const handleFileSelect = (file: File) => {
    if (isValidFileType(file)) {
      setSelectedFile(file);
      resetStages();
    }
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
    setUploadId(null);
    // Phase 4.4: Reset enhanced metadata
    setStatistics(null);
    setCurrentCertificate(null);
    setCurrentCompliance(null);
  };

  const handleUpload = async () => {
    if (!selectedFile) return;

    setIsProcessing(true);
    setOverallStatus('PROCESSING');
    resetStages();

    try {
      // Start upload
      setUploadStage({ status: 'IN_PROGRESS', message: '파일 업로드 중...', percentage: 0 });

      const isLdif = selectedFile.name.toLowerCase().endsWith('.ldif');
      const uploadFn = isLdif ? uploadApi.uploadLdif : uploadApi.uploadMasterList;

      const response = await uploadFn(selectedFile, processingMode);

      if (response.data.success && response.data.data) {
        const uploadedFile = response.data.data;
        // Backend returns uploadId, but type defines id - handle both
        const fileId = (uploadedFile as { uploadId?: string }).uploadId || uploadedFile.id;
        setUploadId(fileId);
        setUploadStage({ status: 'COMPLETED', message: '파일 업로드 완료', percentage: 100 });

        // Save uploadId to localStorage for MANUAL mode state restoration
        if (processingMode === 'MANUAL') {
          localStorage.setItem('currentUploadId', fileId);
        }

        // Connect to SSE for progress updates (both AUTO and MANUAL modes)
        connectToProgressStream(fileId);
      } else {
        throw new Error(response.data.error || '업로드 실패');
      }
    } catch (error: unknown) {
      // Check for HTTP 409 Conflict (duplicate file)
      if (axios.isAxiosError(error) && error.response?.status === 409) {
        const errorData = error.response.data as {
          message?: string;
          existingUpload?: {
            uploadId: string;
            fileName: string;
            uploadTimestamp: string;
            status: string;
            processingMode: string;
            fileFormat: string;
          };
        };

        setUploadStage({ status: 'FAILED', message: '중복 파일', percentage: 0 });
        setOverallStatus('FAILED');
        setOverallMessage(errorData.message || '이미 업로드된 파일입니다.');

        const existing = errorData.existingUpload;
        if (existing) {
          setErrorMessages([
            `이 파일은 이미 업로드되었습니다 (SHA-256 해시 중복).`,
            `기존 업로드 ID: ${existing.uploadId}`,
            `파일명: ${existing.fileName}`,
            `업로드 시간: ${new Date(existing.uploadTimestamp).toLocaleString('ko-KR')}`,
            `상태: ${existing.status}`,
            `처리 모드: ${existing.processingMode}`,
            `파일 형식: ${existing.fileFormat}`,
          ]);
        } else {
          setErrorMessages(['이 파일은 이미 업로드되었습니다.']);
        }
      } else {
        // Other errors
        setUploadStage({ status: 'FAILED', message: '업로드 실패', percentage: 0 });
        setOverallStatus('FAILED');
        setOverallMessage('파일 업로드에 실패했습니다.');
        setErrorMessages([error instanceof Error ? error.message : '알 수 없는 오류']);
      }
      setIsProcessing(false);
    }
  };

  // v1.5.5: Polling backup mechanism - sync state from DB every 30s
  const syncStateFromDB = async (id: string) => {
    try {
      const response = await uploadApi.getDetail(id);
      if (!response.data?.success || !response.data.data) {
        console.warn('[Polling] Failed to fetch upload details');
        return;
      }

      const upload = response.data.data as UploadedFile;
      console.log('[Polling] Syncing state from DB:', upload.status, upload);

      // Update stage states based on DB data
      // Stage 1: Upload & Parse (always completed if we have the record)
      if (upload.status !== 'PENDING') {
        setUploadStage({ status: 'COMPLETED', message: '파일 업로드 완료', percentage: 100 });
        // For Master List: use processedEntries (extracted certificates)
        // For LDIF: use totalEntries (LDIF entries)
        const entriesCount = upload.fileFormat === 'ML' ? upload.processedEntries : upload.totalEntries;
        setParseStage({
          status: 'COMPLETED',
          message: '파싱 완료',
          percentage: 100,
          details: `${entriesCount}건 처리`
        });
      }

      // Stage 2: Validate & DB + LDAP (check certificate counts)
      const hasCertificates = (upload.cscaCount || 0) + (upload.dscCount || 0) + (upload.dscNcCount || 0) + (upload.mlscCount || 0) > 0;

      if (upload.status === 'COMPLETED' || hasCertificates) {
        // Build detailed certificate breakdown
        const parts: string[] = [];
        if (upload.mlscCount) parts.push(`MLSC ${upload.mlscCount}`);
        if (upload.cscaCount) parts.push(`CSCA ${upload.cscaCount}`);
        if (upload.dscCount) parts.push(`DSC ${upload.dscCount}`);
        if (upload.dscNcCount) parts.push(`DSC_NC ${upload.dscNcCount}`);
        if (upload.crlCount) parts.push(`CRL ${upload.crlCount}`);
        if (upload.mlCount) parts.push(`ML ${upload.mlCount}`);
        const details = parts.length > 0 ? `저장 완료: ${parts.join(', ')}` : `${upload.processedEntries || upload.totalEntries}건 저장 (DB+LDAP)`;

        setDbSaveStage({
          status: 'COMPLETED',
          message: 'DB 및 LDAP 저장 완료',
          percentage: 100,
          details
        });
      }

      // Handle completion states
      if (upload.status === 'COMPLETED') {
        setOverallStatus('FINALIZED');
        setOverallMessage('모든 처리가 완료되었습니다.');
        setIsProcessing(false);
        localStorage.removeItem('currentUploadId');
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
        setOverallMessage('처리 중 오류가 발생했습니다.');
        setIsProcessing(false);
        localStorage.removeItem('currentUploadId');
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
      console.error('[Polling] Error syncing state from DB:', error);
    }
  };

  // v1.5.5: Start polling backup mechanism (30s interval)
  const startPolling = (id: string) => {
    // Clear existing interval if any
    if (pollingIntervalRef.current) {
      clearInterval(pollingIntervalRef.current);
    }

    // Start polling every 30 seconds
    console.log('[Polling] Starting 30s interval backup for uploadId:', id);
    pollingIntervalRef.current = setInterval(() => {
      if (!sseConnected) {
        console.log('[Polling] SSE disconnected, using polling as primary source');
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
      console.log('[SSE] Connected to progress stream');
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
      console.warn('[SSE] Connection error, closing...');
      setSseConnected(false);
      eventSource.close();

      // Try to reconnect if not too many attempts
      if (reconnectAttempts < maxReconnectAttempts && isProcessing) {
        reconnectAttempts++;
        console.log(`[SSE] Reconnect attempt ${reconnectAttempts}/${maxReconnectAttempts}`);
        setTimeout(() => connectToProgressStream(id), 1000);
      } else {
        console.log('[SSE] Max reconnect attempts reached, relying on polling');
        sseRef.current = null;
        // Don't set isProcessing to false - let polling continue
      }
    };

    // v1.5.5: Start polling backup alongside SSE
    startPolling(id);
  };

  const handleProgressUpdate = (progress: UploadProgress) => {
    const { stage, stageName, message, percentage, processedCount, totalCount, errorMessage } = progress;

    // Phase 4.4: Extract enhanced metadata fields
    if (progress.statistics) {
      setStatistics(progress.statistics);
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
        // Combined VALIDATION (55-70) + DB_SAVING (72-85) mapped to 0-100
        // VALIDATION: 55-70 -> 0-50, DB_SAVING: 72-85 -> 50-100
        if (stageStr.startsWith('VALIDATION')) {
          return Math.min(50, Math.max(0, Math.round((overallPercent - 55) * 50 / 15)));
        } else {
          return Math.min(100, Math.max(50, 50 + Math.round((overallPercent - 72) * 50 / 13)));
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
      console.log(`[FileUpload] Using detailed message as details: "${details}"`);
    } else if (stage.endsWith('_COMPLETED') || stage === 'COMPLETED') {
      // Show final count on completion (use processedCount if available, fallback to totalCount)
      const count = processedCount || totalCount;
      if (count > 0) {
        details = `${count}건 처리`;
      }
    } else if (processedCount > 0 && totalCount > 0) {
      // Show progress during processing
      details = `${processedCount}/${totalCount}`;
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
        details: stage === 'VALIDATION_COMPLETED' && validationCount > 0 ? `검증: ${validationCount}건` : details,
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
        details: stage === 'DB_SAVING_COMPLETED' && saveCount > 0 ? `${saveCount}건 저장` : details,
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
          : (prev.details || `${totalCount}건 저장 (DB+LDAP)`);
        return { ...prev, status: 'COMPLETED', percentage: 100, details: completionDetails };
      });
      setOverallStatus('FINALIZED');
      setOverallMessage(message || '모든 처리가 완료되었습니다.');
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

  // Manual mode triggers
  const triggerParse = async () => {
    if (!uploadId) return;
    setParseStage({ status: 'IN_PROGRESS', message: '파싱 시작...', percentage: 0 });
    try {
      // Reconnect to SSE for progress updates
      connectToProgressStream(uploadId);
      await uploadApi.triggerParse(uploadId);
    } catch (error) {
      setParseStage({ status: 'FAILED', message: '파싱 실패', percentage: 0 });
      setErrorMessages(prev => [...prev, error instanceof Error ? error.message : '파싱 요청 실패']);
    }
  };

  const triggerValidate = async () => {
    if (!uploadId) return;
    setDbSaveStage({ status: 'IN_PROGRESS', message: '검증 시작...', percentage: 0 });
    try {
      // Reconnect to SSE for progress updates
      connectToProgressStream(uploadId);
      await uploadApi.triggerValidate(uploadId);
    } catch (error) {
      setDbSaveStage({ status: 'FAILED', message: '검증 실패', percentage: 0 });
      setErrorMessages(prev => [...prev, error instanceof Error ? error.message : '검증 요청 실패']);
    }
  };


  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-lg">
            <Upload className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              PKD 파일 업로드
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              ICAO PKD LDIF 또는 Master List 파일을 업로드합니다. 파일 형식이 자동으로 감지됩니다.
            </p>
          </div>
        </div>
      </div>

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
                  <h2 className="text-lg font-bold text-gray-900 dark:text-white">파일 업로드</h2>
                  <p className="text-xs text-gray-500 dark:text-gray-400">
                    LDIF, Master List 파일을 처리 서버에 업로드합니다.
                  </p>
                </div>
              </div>

              {/* Processing Mode Selector */}
              <div className="mb-4 pb-4 border-b border-gray-200 dark:border-gray-700">
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  처리 모드
                </label>
                <div className="flex gap-2">
                  {(['AUTO', 'MANUAL'] as ProcessingMode[]).map((mode) => (
                    <button
                      key={mode}
                      onClick={() => setProcessingMode(mode)}
                      className={cn(
                        'flex-1 py-2 px-4 rounded-lg text-sm font-medium transition-all',
                        processingMode === mode
                          ? 'bg-indigo-500 text-white'
                          : 'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 hover:bg-gray-200 dark:hover:bg-gray-600'
                      )}
                    >
                      {mode === 'AUTO' ? '자동 처리' : '수동 처리'}
                    </button>
                  ))}
                </div>
              </div>

              {/* File Drop Zone */}
              <div
                className={cn(
                  'relative border-2 border-dashed rounded-xl p-8 text-center cursor-pointer transition-all duration-300',
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
                <div className="flex flex-col items-center justify-center space-y-3">
                  <div className={cn(
                    'p-4 rounded-full transition-colors',
                    selectedFile ? 'bg-blue-100 dark:bg-blue-900/40' : 'bg-gray-100 dark:bg-gray-700'
                  )}>
                    <CloudUpload
                      className={cn(
                        'w-10 h-10 transition-colors',
                        selectedFile ? 'text-blue-500' : 'text-gray-400'
                      )}
                    />
                  </div>
                  {!selectedFile ? (
                    <>
                      <p className="text-gray-600 dark:text-gray-300 font-medium">
                        파일을 여기로 드래그하거나 클릭하여 선택하세요
                      </p>
                      <p className="text-xs text-gray-400 dark:text-gray-500">
                        LDIF, ML 파일 지원 (최대 100MB)
                      </p>
                    </>
                  ) : (
                    <div className="text-center space-y-1">
                      <p className="font-semibold text-gray-900 dark:text-white">{selectedFile.name}</p>
                      <p className="text-sm text-gray-500">{formatFileSize(selectedFile.size)}</p>
                      <span className={cn(
                        'inline-block px-2 py-0.5 rounded text-xs font-medium',
                        selectedFile.name.endsWith('.ldif')
                          ? 'bg-purple-100 text-purple-700 dark:bg-purple-900/40 dark:text-purple-300'
                          : 'bg-orange-100 text-orange-700 dark:bg-orange-900/40 dark:text-orange-300'
                      )}>
                        {selectedFile.name.endsWith('.ldif') ? 'LDIF' : 'Master List'}
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
                    지원하지 않는 파일 형식입니다. LDIF 또는 ML 파일을 선택해주세요.
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
                  업로드 이력
                </button>
                <button
                  onClick={handleUpload}
                  disabled={isProcessing || !selectedFile || (selectedFile && !isValidFileType(selectedFile))}
                  className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 transition-all duration-200 shadow-md hover:shadow-lg disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  {isProcessing ? (
                    <Loader2 className="w-4 h-4 animate-spin" />
                  ) : (
                    <Upload className="w-4 h-4" />
                  )}
                  업로드
                </button>
              </div>
            </div>
          </div>
        </div>

        {/* Right Column: Progress & Results */}
        <div className="col-span-1">
          <div className="rounded-2xl transition-all duration-300 bg-white dark:bg-gray-800 shadow-lg">
            <div className="flex flex-col p-6">
              {/* Header */}
              <div className="flex items-center gap-3 mb-5">
                <div className="p-2.5 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                  <FileText className="w-5 h-5 text-cyan-500" />
                </div>
                <div className="flex-1 min-w-0">
                  <h3 className="text-lg font-bold text-gray-900 dark:text-white">처리 진행 상황</h3>
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
                    {overallStatus === 'PROCESSING' && '처리 중'}
                    {overallStatus === 'FINALIZED' && '완료'}
                    {overallStatus === 'FAILED' && '실패'}
                  </span>
                )}
              </div>

              {/* Preline-style Stepper */}
              <div className="py-2">
                <Stepper
                  steps={steps}
                  orientation="vertical"
                  size="md"
                  showProgress={true}
                />
              </div>

              {/* Phase 4.4: Currently Processing Certificate */}
              {currentCertificate && (overallStatus === 'PROCESSING' || dbSaveStage.status === 'IN_PROGRESS') && (
                <div className="mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
                  <h4 className="font-bold text-sm mb-3 text-gray-700 dark:text-gray-300">처리 중인 인증서</h4>
                  <CurrentCertificateCard
                    certificate={currentCertificate}
                    compliance={currentCompliance || undefined}
                    compact={true}
                  />
                </div>
              )}

              {/* Phase 4.4: Real-time Statistics */}
              {statistics && (overallStatus === 'PROCESSING' || overallStatus === 'FINALIZED') && (
                <div className="mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
                  <RealTimeStatisticsPanel
                    statistics={statistics}
                    isProcessing={overallStatus === 'PROCESSING'}
                  />
                </div>
              )}

              {/* Manual Mode Controls */}
              {processingMode === 'MANUAL' && uploadId && overallStatus !== 'FINALIZED' && overallStatus !== 'FAILED' && (
                <div className="mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
                  <h4 className="font-bold text-sm mb-3 text-gray-700 dark:text-gray-300">수동 처리 제어</h4>
                  <div className="grid grid-cols-3 gap-2">
                    <button
                      onClick={triggerParse}
                      disabled={parseStage.status === 'COMPLETED' || parseStage.status === 'IN_PROGRESS'}
                      className={cn(
                        'py-2.5 px-3 text-xs font-medium rounded-lg flex items-center justify-center gap-1.5 transition-all',
                        parseStage.status === 'COMPLETED'
                          ? 'bg-teal-100 text-teal-700 dark:bg-teal-900/30 dark:text-teal-400'
                          : 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400 hover:bg-blue-200 dark:hover:bg-blue-900/50',
                        'disabled:opacity-50'
                      )}
                    >
                      {parseStage.status === 'IN_PROGRESS' ? (
                        <Loader2 className="w-3.5 h-3.5 animate-spin" />
                      ) : parseStage.status === 'COMPLETED' ? (
                        <CheckCircle className="w-3.5 h-3.5" />
                      ) : (
                        <Play className="w-3.5 h-3.5" />
                      )}
                      {parseStage.status === 'COMPLETED' ? '파싱 완료' : '1. 파싱'}
                    </button>

                    <button
                      onClick={triggerValidate}
                      disabled={parseStage.status !== 'COMPLETED' || dbSaveStage.status === 'COMPLETED' || dbSaveStage.status === 'IN_PROGRESS'}
                      className={cn(
                        'py-2.5 px-3 text-xs font-medium rounded-lg flex items-center justify-center gap-1.5 transition-all',
                        dbSaveStage.status === 'COMPLETED'
                          ? 'bg-teal-100 text-teal-700 dark:bg-teal-900/30 dark:text-teal-400'
                          : 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400 hover:bg-blue-200 dark:hover:bg-blue-900/50',
                        'disabled:opacity-50'
                      )}
                    >
                      {dbSaveStage.status === 'IN_PROGRESS' ? (
                        <Loader2 className="w-3.5 h-3.5 animate-spin" />
                      ) : dbSaveStage.status === 'COMPLETED' ? (
                        <CheckCircle className="w-3.5 h-3.5" />
                      ) : (
                        <Play className="w-3.5 h-3.5" />
                      )}
                      {dbSaveStage.status === 'COMPLETED' ? '저장 완료' : '2. 저장 (DB+LDAP)'}
                    </button>
                  </div>
                </div>
              )}

              {/* Final Status */}
              {(overallStatus === 'FINALIZED' || overallStatus === 'FAILED') && (
                <div
                  className={cn(
                    'mt-4 p-4 rounded-xl flex items-start gap-3',
                    overallStatus === 'FINALIZED'
                      ? 'bg-gradient-to-r from-teal-50 to-emerald-50 dark:from-teal-900/20 dark:to-emerald-900/20 border border-teal-200 dark:border-teal-800'
                      : 'bg-gradient-to-r from-red-50 to-orange-50 dark:from-red-900/20 dark:to-orange-900/20 border border-red-200 dark:border-red-800'
                  )}
                >
                  <div className={cn(
                    'p-2 rounded-lg shrink-0',
                    overallStatus === 'FINALIZED' ? 'bg-teal-100 dark:bg-teal-900/40' : 'bg-red-100 dark:bg-red-900/40'
                  )}>
                    {overallStatus === 'FINALIZED' ? (
                      <CheckCircle className="w-5 h-5 text-teal-600 dark:text-teal-400" />
                    ) : (
                      <AlertTriangle className="w-5 h-5 text-red-600 dark:text-red-400" />
                    )}
                  </div>
                  <div>
                    <h4 className={cn(
                      'font-semibold text-sm',
                      overallStatus === 'FINALIZED'
                        ? 'text-teal-700 dark:text-teal-400'
                        : 'text-red-700 dark:text-red-400'
                    )}>
                      {overallStatus === 'FINALIZED' ? '처리 완료' : '처리 실패'}
                    </h4>
                    <p className={cn(
                      'text-sm mt-0.5',
                      overallStatus === 'FINALIZED'
                        ? 'text-teal-600 dark:text-teal-500'
                        : 'text-red-600 dark:text-red-500'
                    )}>
                      {overallMessage}
                    </p>
                  </div>
                </div>
              )}

              {/* LDAP Connection Failure Warning (v2.0.0 - Data Consistency Protection) */}
              {overallStatus === 'FAILED' && overallMessage &&
               (overallMessage.includes('LDAP 연결') || overallMessage.includes('LDAP connection') ||
                overallMessage.includes('데이터 일관성')) && (
                <div className="mt-4 p-4 bg-red-50 dark:bg-red-900/20 border-2 border-red-300 dark:border-red-700 rounded-xl">
                  <div className="flex items-start gap-3">
                    <AlertTriangle className="w-6 h-6 text-red-500 mt-0.5 flex-shrink-0" />
                    <div className="flex-1">
                      <h4 className="font-bold text-base text-red-800 dark:text-red-300 mb-2">
                        ⚠️ LDAP 연결 실패 - 데이터 일관성 보장 불가
                      </h4>
                      <p className="text-sm text-red-700 dark:text-red-400 mb-2">
                        {overallMessage}
                      </p>
                      <div className="bg-red-100 dark:bg-red-900/30 rounded-lg p-3 mt-3">
                        <p className="text-sm font-semibold text-red-800 dark:text-red-300 mb-2">
                          💡 해결 방법:
                        </p>
                        <ul className="text-sm text-red-700 dark:text-red-400 space-y-1.5">
                          <li className="flex items-start gap-2">
                            <span className="text-red-500 mt-1">1.</span>
                            <span>LDAP 서버 상태를 확인하세요 (시스템 정보 &gt; LDAP 연결 테스트)</span>
                          </li>
                          <li className="flex items-start gap-2">
                            <span className="text-red-500 mt-1">2.</span>
                            <span>LDAP 서버가 정상이면 이 파일을 다시 업로드하세요</span>
                          </li>
                          <li className="flex items-start gap-2">
                            <span className="text-red-500 mt-1">3.</span>
                            <span>문제가 계속되면 관리자에게 문의하세요</span>
                          </li>
                        </ul>
                      </div>
                      <p className="text-xs text-red-600 dark:text-red-500 mt-3 font-medium">
                        ℹ️ 참고: 이 오류는 v2.0.0부터 데이터 일관성을 보장하기 위해 도입되었습니다.
                        LDAP 저장 실패 시 자동으로 업로드가 중단되어 PostgreSQL과 LDAP 간 데이터 불일치를 방지합니다.
                      </p>
                    </div>
                  </div>
                </div>
              )}

              {/* General Error Messages */}
              {errorMessages.length > 0 && (
                <div className="mt-4 p-4 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl">
                  <h4 className="font-bold text-sm text-red-700 dark:text-red-400 mb-2 flex items-center gap-2">
                    <XCircle className="w-4 h-4" />
                    오류 상세
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
    </div>
  );
}

export default FileUpload;
