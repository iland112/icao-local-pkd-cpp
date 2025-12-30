import { useState, useRef, useCallback, useMemo } from 'react';
import { useNavigate } from 'react-router-dom';
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
  Server,
} from 'lucide-react';
import { uploadApi, createProgressEventSource } from '@/services/api';
import type { ProcessingMode, UploadProgress } from '@/types';
import { cn } from '@/utils/cn';
import { Stepper, type Step, type StepStatus } from '@/components/common/Stepper';

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
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [processingMode, setProcessingMode] = useState<ProcessingMode>('AUTO');
  const [isProcessing, setIsProcessing] = useState(false);
  const [uploadId, setUploadId] = useState<string | null>(null);

  // Stage statuses
  const [uploadStage, setUploadStage] = useState<StageStatus>(initialStage);
  const [parseStage, setParseStage] = useState<StageStatus>(initialStage);
  const [validateStage, setValidateStage] = useState<StageStatus>(initialStage);
  const [dbSaveStage, setDbSaveStage] = useState<StageStatus>(initialStage);
  const [ldapStage, setLdapStage] = useState<StageStatus>(initialStage);

  const [overallStatus, setOverallStatus] = useState<'IDLE' | 'PROCESSING' | 'FINALIZED' | 'FAILED'>('IDLE');
  const [overallMessage, setOverallMessage] = useState('');
  const [errorMessages, setErrorMessages] = useState<string[]>([]);

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
    // Combine validate and dbSave into one step
    const dbStage = dbSaveStage.status !== 'IDLE' ? dbSaveStage : validateStage;

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
        label: '검증 및 DB 저장',
        description: dbStage.message || '인증서 검증 및 데이터베이스 저장',
        status: toStepStatus(dbStage.status),
        progress: dbStage.percentage,
        details: dbStage.details,
        icon: <Database className="w-3.5 h-3.5" />,
      },
      {
        id: 'ldap',
        label: 'LDAP 저장',
        description: ldapStage.message || 'OpenLDAP 디렉토리에 저장',
        status: toStepStatus(ldapStage.status),
        progress: ldapStage.percentage,
        details: ldapStage.details,
        icon: <Server className="w-3.5 h-3.5" />,
      },
    ];
  }, [uploadStage, parseStage, validateStage, dbSaveStage, ldapStage]);

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
    setValidateStage(initialStage);
    setDbSaveStage(initialStage);
    setLdapStage(initialStage);
    setOverallStatus('IDLE');
    setOverallMessage('');
    setErrorMessages([]);
    setUploadId(null);
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

        // Connect to SSE for progress updates if AUTO mode
        if (processingMode === 'AUTO') {
          connectToProgressStream(fileId);
        }
      } else {
        throw new Error(response.data.error || '업로드 실패');
      }
    } catch (error) {
      setUploadStage({ status: 'FAILED', message: '업로드 실패', percentage: 0 });
      setOverallStatus('FAILED');
      setOverallMessage('파일 업로드에 실패했습니다.');
      setErrorMessages([error instanceof Error ? error.message : '알 수 없는 오류']);
      setIsProcessing(false);
    }
  };

  const connectToProgressStream = (id: string) => {
    const eventSource = createProgressEventSource(id);

    // Handle 'connected' event
    eventSource.addEventListener('connected', (event) => {
      console.log('SSE connected:', (event as MessageEvent).data);
    });

    // Handle 'progress' events from backend
    eventSource.addEventListener('progress', (event) => {
      try {
        const progress: UploadProgress = JSON.parse((event as MessageEvent).data);
        handleProgressUpdate(progress);
      } catch (error) {
        console.error('Failed to parse progress event:', error);
      }
    });

    // Fallback for unnamed events
    eventSource.onmessage = (event) => {
      try {
        const progress: UploadProgress = JSON.parse(event.data);
        handleProgressUpdate(progress);
      } catch (error) {
        console.error('Failed to parse message event:', error);
      }
    };

    eventSource.onerror = () => {
      eventSource.close();
      setIsProcessing(false);
    };
  };

  const handleProgressUpdate = (progress: UploadProgress) => {
    const { stage, stageName, message, percentage, processedCount, totalCount, errorMessage } = progress;

    // Determine status from stage
    const getStatus = (stageStr: string): StageStatus['status'] => {
      if (stageStr.endsWith('_STARTED') || stageStr.endsWith('_IN_PROGRESS')) return 'IN_PROGRESS';
      if (stageStr.endsWith('_COMPLETED') || stageStr === 'COMPLETED') return 'COMPLETED';
      if (stageStr === 'FAILED') return 'FAILED';
      return 'IDLE';
    };

    const stageStatus: StageStatus = {
      status: getStatus(stage),
      message: stageName || message,
      percentage,
      details: processedCount > 0 ? `${processedCount}/${totalCount}` : undefined,
    };

    // Map backend stage names to frontend stages
    if (stage.startsWith('UPLOAD')) {
      setUploadStage(stageStatus);
    } else if (stage.startsWith('PARSING')) {
      setParseStage(stageStatus);
    } else if (stage.startsWith('VALIDATION')) {
      setValidateStage(stageStatus);
    } else if (stage.startsWith('DB_SAVING')) {
      setDbSaveStage(stageStatus);
    } else if (stage.startsWith('LDAP_SAVING')) {
      setLdapStage(stageStatus);
    } else if (stage === 'COMPLETED') {
      // Mark all remaining stages as completed
      setDbSaveStage(prev => prev.status === 'IDLE' ? { ...prev, status: 'COMPLETED' } : prev);
      setLdapStage(prev => prev.status === 'IDLE' ? { ...prev, status: 'COMPLETED' } : prev);
      setOverallStatus('FINALIZED');
      setOverallMessage(message || '모든 처리가 완료되었습니다.');
      setIsProcessing(false);
    } else if (stage === 'FAILED') {
      setOverallStatus('FAILED');
      setOverallMessage(errorMessage || message);
      setIsProcessing(false);
    }
  };

  // Manual mode triggers
  const triggerParse = async () => {
    if (!uploadId) return;
    setParseStage({ status: 'IN_PROGRESS', message: '파싱 중...', percentage: 0 });
    try {
      await uploadApi.triggerParse(uploadId);
    } catch (error) {
      setParseStage({ status: 'FAILED', message: '파싱 실패', percentage: 0 });
    }
  };

  const triggerValidate = async () => {
    if (!uploadId) return;
    setValidateStage({ status: 'IN_PROGRESS', message: '검증 중...', percentage: 0 });
    try {
      await uploadApi.triggerValidate(uploadId);
    } catch (error) {
      setValidateStage({ status: 'FAILED', message: '검증 실패', percentage: 0 });
    }
  };

  const triggerLdapUpload = async () => {
    if (!uploadId) return;
    setLdapStage({ status: 'IN_PROGRESS', message: 'LDAP 저장 중...', percentage: 0 });
    try {
      await uploadApi.triggerLdapUpload(uploadId);
    } catch (error) {
      setLdapStage({ status: 'FAILED', message: 'LDAP 저장 실패', percentage: 0 });
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
                      disabled={parseStage.status !== 'COMPLETED' || dbSaveStage.status === 'COMPLETED' || validateStage.status === 'IN_PROGRESS'}
                      className={cn(
                        'py-2.5 px-3 text-xs font-medium rounded-lg flex items-center justify-center gap-1.5 transition-all',
                        dbSaveStage.status === 'COMPLETED'
                          ? 'bg-teal-100 text-teal-700 dark:bg-teal-900/30 dark:text-teal-400'
                          : 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400 hover:bg-blue-200 dark:hover:bg-blue-900/50',
                        'disabled:opacity-50'
                      )}
                    >
                      {validateStage.status === 'IN_PROGRESS' || dbSaveStage.status === 'IN_PROGRESS' ? (
                        <Loader2 className="w-3.5 h-3.5 animate-spin" />
                      ) : dbSaveStage.status === 'COMPLETED' ? (
                        <CheckCircle className="w-3.5 h-3.5" />
                      ) : (
                        <Play className="w-3.5 h-3.5" />
                      )}
                      {dbSaveStage.status === 'COMPLETED' ? '검증+DB 완료' : '2. 검증+DB'}
                    </button>

                    <button
                      onClick={triggerLdapUpload}
                      disabled={dbSaveStage.status !== 'COMPLETED' || ldapStage.status === 'COMPLETED' || ldapStage.status === 'IN_PROGRESS'}
                      className={cn(
                        'py-2.5 px-3 text-xs font-medium rounded-lg flex items-center justify-center gap-1.5 transition-all',
                        ldapStage.status === 'COMPLETED'
                          ? 'bg-teal-100 text-teal-700 dark:bg-teal-900/30 dark:text-teal-400'
                          : 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400 hover:bg-blue-200 dark:hover:bg-blue-900/50',
                        'disabled:opacity-50'
                      )}
                    >
                      {ldapStage.status === 'IN_PROGRESS' ? (
                        <Loader2 className="w-3.5 h-3.5 animate-spin" />
                      ) : ldapStage.status === 'COMPLETED' ? (
                        <CheckCircle className="w-3.5 h-3.5" />
                      ) : (
                        <Play className="w-3.5 h-3.5" />
                      )}
                      {ldapStage.status === 'COMPLETED' ? 'LDAP 완료' : '3. LDAP'}
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

              {/* Error Messages */}
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
