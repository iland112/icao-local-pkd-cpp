import { useState, useRef, useCallback } from 'react';
import {
  IdCard,
  Upload,
  ShieldCheck,
  FileText,
  CheckCircle,
  XCircle,
  Play,
  X,
  Loader2,
  Eye,
  AlertTriangle,
  Clock,
  Award,
  Lock,
  Hash,
  FileKey,
  Globe,
  User,
  Image,
  ListChecks,
} from 'lucide-react';
import { paApi } from '@/services/api';
import type {
  PAVerificationRequest,
  PAVerificationResponse,
  DataGroupNumber,
  MRZData,
} from '@/types';
import { cn } from '@/utils/cn';

// DG1 MRZ 파싱 결과 타입
interface DG1ParseResult {
  success: boolean;
  documentType?: string;
  issuingCountry?: string;
  surname?: string;
  givenNames?: string;
  fullName?: string;
  documentNumber?: string;
  nationality?: string;
  dateOfBirth?: string;
  sex?: string;
  dateOfExpiry?: string;
  mrzLine1?: string;
  mrzLine2?: string;
  mrzFull?: string;
  error?: string;
}

// DG2 Face 파싱 결과 타입
interface DG2ParseResult {
  success: boolean;
  faceCount?: number;
  faceImages?: Array<{
    index: number;
    imageFormat: string;
    imageSize: number;
    width?: number;
    height?: number;
    imageDataUrl?: string;
  }>;
  hasFacContainer?: boolean;
  error?: string;
}

interface FileInfo {
  file: File;
  name: string;
  number?: DataGroupNumber;
}

export function PAVerify() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const mrzInputRef = useRef<HTMLInputElement>(null);

  // File states
  const [sodFile, setSodFile] = useState<File | null>(null);
  const [dgFiles, setDgFiles] = useState<FileInfo[]>([]);
  const [, setMrzFile] = useState<File | null>(null);
  const [mrzData, setMrzData] = useState<MRZData | null>(null);

  // Verification states
  const [verifying, setVerifying] = useState(false);
  const [result, setResult] = useState<PAVerificationResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  // SOD preview data
  const [sodInfo, setSodInfo] = useState<{
    dscSubject?: string;
    dscSerial?: string;
    hashAlgorithm?: string;
    dataGroups?: number[];
  } | null>(null);

  // DG Parsing states
  const [dg1ParseResult, setDg1ParseResult] = useState<DG1ParseResult | null>(null);
  const [dg2ParseResult, setDg2ParseResult] = useState<DG2ParseResult | null>(null);
  const [parsingDg1, setParsingDg1] = useState(false);
  const [parsingDg2, setParsingDg2] = useState(false);

  const handleDirectoryUpload = useCallback(async (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = e.target.files;
    if (!files) return;

    const newDgFiles: FileInfo[] = [];
    let newSodFile: File | null = null;

    for (const file of Array.from(files)) {
      const name = file.name.toLowerCase();

      if (name.includes('sod') || name === 'ef.sod' || name.endsWith('sod.bin')) {
        newSodFile = file;
      } else {
        // Try to extract DG number from filename
        const dgMatch = name.match(/dg(\d{1,2})/i) || name.match(/ef\.dg(\d{1,2})/i);
        if (dgMatch) {
          const dgNum = parseInt(dgMatch[1], 10);
          if (dgNum >= 1 && dgNum <= 16) {
            newDgFiles.push({
              file,
              name: file.name,
              number: `DG${dgNum}` as DataGroupNumber,
            });
          }
        }
      }
    }

    if (newSodFile) {
      setSodFile(newSodFile);
      // Read and preview SOD info
      await previewSodFile(newSodFile);
    }

    setDgFiles(newDgFiles);
    setResult(null);
    setErrorMessage(null);
    setSuccessMessage(null);
  }, []);

  const previewSodFile = async (_file: File) => {
    try {
      setSodInfo({
        dscSubject: 'Parsing...',
        hashAlgorithm: 'SHA-256',
        dataGroups: [1, 2],
      });
    } catch {
      console.error('Failed to preview SOD');
    }
  };

  const handleMrzFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    setMrzFile(file);

    try {
      const text = await file.text();
      const lines = text.trim().split('\n');

      if (lines.length >= 2) {
        const line1 = lines[0].trim();
        const line2 = lines[1].trim();

        // Parse TD3 MRZ format
        const parsed: MRZData = {
          line1,
          line2,
          fullName: parseMrzName(line1),
          documentNumber: line2.substring(0, 9).replace(/</g, ''),
          nationality: line2.substring(10, 13),
          dateOfBirth: formatMrzDate(line2.substring(13, 19)),
          sex: line2.substring(20, 21),
          expirationDate: formatMrzDate(line2.substring(21, 27)),
        };

        setMrzData(parsed);
      }
    } catch {
      setErrorMessage('MRZ 파일 파싱 실패');
    }
  };

  const parseMrzName = (line1: string): string => {
    const names = line1.substring(5).split('<<');
    const surname = names[0]?.replace(/</g, ' ').trim() || '';
    const givenName = names[1]?.replace(/</g, ' ').trim() || '';
    return `${givenName} ${surname}`.trim();
  };

  const formatMrzDate = (dateStr: string): string => {
    if (dateStr.length !== 6) return dateStr;
    const year = parseInt(dateStr.substring(0, 2), 10);
    const month = dateStr.substring(2, 4);
    const day = dateStr.substring(4, 6);
    const fullYear = year > 30 ? 1900 + year : 2000 + year;
    return `${fullYear}-${month}-${day}`;
  };

  const clearMrzFile = () => {
    setMrzFile(null);
    setMrzData(null);
    if (mrzInputRef.current) {
      mrzInputRef.current.value = '';
    }
  };

  const clearAllData = () => {
    setSodFile(null);
    setDgFiles([]);
    setSodInfo(null);
    clearMrzFile();
    setResult(null);
    setErrorMessage(null);
    setSuccessMessage(null);
    setDg1ParseResult(null);
    setDg2ParseResult(null);
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  // DG1 파싱 함수
  const parseDg1 = async () => {
    const dg1File = dgFiles.find(f => f.number === 'DG1');
    if (!dg1File) return;

    setParsingDg1(true);
    try {
      const base64 = await fileToBase64(dg1File.file);
      const response = await fetch('/api/pa/parse-dg1', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ dg1Base64: base64 }),
      });
      const data = await response.json();
      setDg1ParseResult(data);
    } catch (error) {
      setDg1ParseResult({ success: false, error: 'DG1 파싱 실패' });
    } finally {
      setParsingDg1(false);
    }
  };

  // DG2 파싱 함수
  const parseDg2 = async () => {
    const dg2File = dgFiles.find(f => f.number === 'DG2');
    if (!dg2File) return;

    setParsingDg2(true);
    try {
      const base64 = await fileToBase64(dg2File.file);
      const response = await fetch('/api/pa/parse-dg2', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ dg2Base64: base64 }),
      });
      const data = await response.json();
      setDg2ParseResult(data);
    } catch (error) {
      setDg2ParseResult({ success: false, error: 'DG2 파싱 실패' });
    } finally {
      setParsingDg2(false);
    }
  };

  const performVerification = async () => {
    if (!sodFile) return;

    setVerifying(true);
    setErrorMessage(null);
    setSuccessMessage(null);
    setResult(null);

    try {
      // Convert files to base64
      const sodBase64 = await fileToBase64(sodFile);
      const dataGroups = await Promise.all(
        dgFiles.map(async (dg) => ({
          number: dg.number!,
          data: await fileToBase64(dg.file),
        }))
      );

      const request: PAVerificationRequest = {
        sod: sodBase64,
        dataGroups,
        mrzData: mrzData || undefined,
      };

      const response = await paApi.verify(request);

      if (response.data.success && response.data.data) {
        setResult(response.data.data);
        if (response.data.data.status === 'VALID') {
          setSuccessMessage('Passive Authentication 검증이 완료되었습니다.');
        }
      } else {
        throw new Error(response.data.error || '검증 실패');
      }
    } catch (error) {
      setErrorMessage(error instanceof Error ? error.message : '검증 중 오류가 발생했습니다.');
    } finally {
      setVerifying(false);
    }
  };

  const fileToBase64 = (file: File): Promise<string> => {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => {
        const result = reader.result as string;
        resolve(result.split(',')[1] || result);
      };
      reader.onerror = reject;
      reader.readAsDataURL(file);
    });
  };

  const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
  };

  const getStatusIcon = (valid: boolean | undefined) => {
    if (valid === undefined) return null;
    return valid ? (
      <CheckCircle className="w-5 h-5 text-green-500" />
    ) : (
      <XCircle className="w-5 h-5 text-red-500" />
    );
  };

  const getCrlSeverityColor = (severity: string) => {
    switch (severity?.toUpperCase()) {
      case 'SUCCESS':
        return 'text-green-600 bg-green-50 dark:bg-green-900/20';
      case 'WARNING':
        return 'text-yellow-600 bg-yellow-50 dark:bg-yellow-900/20';
      case 'ERROR':
        return 'text-red-600 bg-red-50 dark:bg-red-900/20';
      default:
        return 'text-gray-600 bg-gray-50 dark:bg-gray-700';
    }
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-8">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-teal-500 to-cyan-600 shadow-lg">
            <IdCard className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              Passive Authentication 검증
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              전자여권 칩 데이터(SOD, Data Groups)를 업로드하여 ICAO 9303 표준에 따른 Passive Authentication을 수행합니다.
            </p>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5">
        {/* Left Column: File Upload */}
        <div className="lg:col-span-1">
          <div className="rounded-2xl transition-all duration-300 hover:shadow-xl bg-white dark:bg-gray-800 shadow-lg">
            <div className="p-5">
              <div className="flex items-center gap-3 mb-4">
                <div className="p-2.5 rounded-xl bg-teal-50 dark:bg-teal-900/30">
                  <Upload className="w-5 h-5 text-teal-500" />
                </div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">데이터 파일 업로드</h2>
              </div>

              {/* File Upload */}
              <div className="mb-3">
                <label className="block text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 flex items-center gap-1">
                  <FileText className="w-4 h-4 text-blue-500" />
                  데이터 파일 선택 (복수 선택 가능)
                </label>
                <input
                  ref={fileInputRef}
                  type="file"
                  multiple
                  accept=".bin"
                  onChange={handleDirectoryUpload}
                  className="w-full text-sm text-gray-500 file:mr-4 file:py-2 file:px-4 file:rounded-lg file:border-0 file:text-sm file:font-medium file:bg-blue-50 file:text-blue-700 hover:file:bg-blue-100 dark:file:bg-blue-900/30 dark:file:text-blue-400"
                />
                <p className="mt-1 text-xs text-gray-500">SOD, DG1, DG2 등 .bin 파일 선택</p>
              </div>

              {/* MRZ File Upload */}
              <div className="mb-3">
                <label className="block text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 flex items-center gap-1">
                  <FileText className="w-4 h-4 text-purple-500" />
                  MRZ 텍스트 파일 (선택사항)
                  <span className="text-xs text-gray-400 ml-2">Optional</span>
                </label>
                <input
                  ref={mrzInputRef}
                  type="file"
                  accept=".txt"
                  onChange={handleMrzFileUpload}
                  className="w-full text-sm text-gray-500 file:mr-4 file:py-2 file:px-4 file:rounded-lg file:border-0 file:text-sm file:font-medium file:bg-purple-50 file:text-purple-700 hover:file:bg-purple-100 dark:file:bg-purple-900/30 dark:file:text-purple-400"
                />
                <p className="mt-1 text-xs text-gray-500">mrz.txt 파일 (TD3 포맷: 2줄 x 44자)</p>
              </div>

              {/* MRZ Preview */}
              {mrzData && (
                <div className="mb-3 p-3 bg-gray-50 dark:bg-gray-700 rounded-lg">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">MRZ 파싱 결과</span>
                    <button onClick={clearMrzFile} className="text-gray-400 hover:text-gray-600">
                      <X className="w-4 h-4" />
                    </button>
                  </div>
                  <div className="grid grid-cols-2 gap-2 text-xs">
                    <div><span className="text-gray-500">성명:</span> {mrzData.fullName}</div>
                    <div><span className="text-gray-500">여권번호:</span> {mrzData.documentNumber}</div>
                    <div><span className="text-gray-500">국적:</span> {mrzData.nationality}</div>
                    <div><span className="text-gray-500">생년월일:</span> {mrzData.dateOfBirth}</div>
                    <div><span className="text-gray-500">성별:</span> {mrzData.sex === 'M' ? '남성' : mrzData.sex === 'F' ? '여성' : '-'}</div>
                    <div><span className="text-gray-500">만료일:</span> {mrzData.expirationDate}</div>
                  </div>
                </div>
              )}

              {/* Uploaded Files Summary */}
              {(sodFile || dgFiles.length > 0) && (
                <div className="mb-3 p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-lg">
                  <div className="flex items-center gap-2 mb-2">
                    <CheckCircle className="w-4 h-4 text-green-500" />
                    <span className="text-sm font-semibold text-green-700 dark:text-green-400">
                      업로드 완료 ({(sodFile ? 1 : 0) + dgFiles.length}개)
                    </span>
                  </div>
                  <div className="text-xs space-y-1 text-green-600 dark:text-green-400">
                    {sodFile && (
                      <div className="flex items-center gap-1">
                        <ShieldCheck className="w-3 h-3" />
                        SOD: {sodFile.name}
                      </div>
                    )}
                    {dgFiles.map((dg, idx) => (
                      <div key={idx} className="flex items-center gap-1">
                        <FileText className="w-3 h-3" />
                        {dg.number}: {dg.name}
                      </div>
                    ))}
                  </div>
                </div>
              )}

              {/* Action Buttons */}
              <button
                onClick={performVerification}
                disabled={!sodFile || verifying}
                className="w-full inline-flex items-center justify-center gap-2 px-4 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500 hover:from-teal-600 hover:to-cyan-600 transition-all duration-200 shadow-md hover:shadow-lg disabled:opacity-50 disabled:cursor-not-allowed"
              >
                {verifying ? (
                  <Loader2 className="w-4 h-4 animate-spin" />
                ) : (
                  <Play className="w-4 h-4" />
                )}
                {verifying ? '검증 중...' : '검증 시작'}
              </button>

              <button
                onClick={clearAllData}
                disabled={verifying}
                className="w-full mt-2 inline-flex items-center justify-center gap-2 px-4 py-2 rounded-xl text-sm font-medium transition-all duration-200 border text-gray-600 dark:text-gray-300 border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50"
              >
                <X className="w-4 h-4" />
                초기화
              </button>
            </div>
          </div>

          {/* Data Preview Card */}
          {sodInfo && (
            <div className="mt-4 rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5">
              <h3 className="font-bold text-gray-900 dark:text-white flex items-center gap-2 mb-3">
                <Eye className="w-5 h-5 text-blue-500" />
                데이터 미리보기
              </h3>
              <div className="text-sm space-y-2 text-gray-600 dark:text-gray-400">
                <div><span className="font-semibold">DSC Subject:</span> {sodInfo.dscSubject}</div>
                <div><span className="font-semibold">Hash Algorithm:</span> {sodInfo.hashAlgorithm}</div>
                <div>
                  <span className="font-semibold">Data Groups:</span>
                  <div className="flex flex-wrap gap-1 mt-1">
                    {sodInfo.dataGroups?.map((dg) => (
                      <span key={dg} className="px-2 py-0.5 rounded bg-gray-100 dark:bg-gray-700 text-xs">
                        DG{dg}
                      </span>
                    ))}
                  </div>
                </div>
              </div>
              <hr className="my-3 border-gray-200 dark:border-gray-700" />
              <div className="text-xs text-gray-400 space-y-1">
                {sodFile && <div>SOD: {formatFileSize(sodFile.size)}</div>}
                {dgFiles.map((dg, idx) => (
                  <div key={idx}>{dg.number}: {formatFileSize(dg.file.size)}</div>
                ))}
              </div>
            </div>
          )}
        </div>

        {/* Right Column: Verification Results */}
        <div className="lg:col-span-2">
          {/* Alert Messages */}
          {errorMessage && (
            <div className="mb-4 p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg flex items-center gap-2 text-red-700 dark:text-red-400">
              <XCircle className="w-5 h-5" />
              <span className="text-sm">{errorMessage}</span>
              <button onClick={() => setErrorMessage(null)} className="ml-auto">
                <X className="w-4 h-4" />
              </button>
            </div>
          )}

          {successMessage && result?.status === 'VALID' && (
            <div className="mb-4 p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-lg flex items-center gap-2 text-green-700 dark:text-green-400">
              <CheckCircle className="w-5 h-5" />
              <span className="text-sm">{successMessage}</span>
            </div>
          )}

          {/* Verification In Progress */}
          {verifying && (
            <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-8">
              <div className="flex flex-col items-center justify-center">
                <Loader2 className="w-12 h-12 text-teal-500 animate-spin mb-4" />
                <h3 className="text-lg font-bold text-gray-900 dark:text-white mb-2">검증 진행 중...</h3>
                <p className="text-sm text-gray-500 dark:text-gray-400">
                  ICAO 9303 Passive Authentication을 수행하고 있습니다.
                </p>
              </div>
            </div>
          )}

          {/* Verification Result */}
          {result && !verifying && (
            <div className="space-y-4">
              {/* Overall Status */}
              <div className={cn(
                'rounded-2xl p-5',
                result.status === 'VALID'
                  ? 'bg-gradient-to-r from-green-500 to-emerald-500 text-white'
                  : result.status === 'INVALID'
                  ? 'bg-gradient-to-r from-red-500 to-rose-500 text-white'
                  : 'bg-gradient-to-r from-yellow-500 to-orange-500 text-white'
              )}>
                <div className="flex items-center gap-4">
                  {result.status === 'VALID' ? (
                    <Award className="w-12 h-12" />
                  ) : result.status === 'INVALID' ? (
                    <XCircle className="w-12 h-12" />
                  ) : (
                    <AlertTriangle className="w-12 h-12" />
                  )}
                  <div className="flex-grow">
                    <h2 className="text-xl font-bold">
                      {result.status === 'VALID'
                        ? 'Passive Authentication 성공'
                        : result.status === 'INVALID'
                        ? 'Passive Authentication 실패'
                        : 'Passive Authentication 오류'}
                    </h2>
                    <div className="flex items-center gap-4 mt-1 text-sm opacity-90">
                      <span className="flex items-center gap-1">
                        <Clock className="w-4 h-4" />
                        {result.processingDurationMs}ms
                      </span>
                      {result.issuingCountry && (
                        <span className="flex items-center gap-1">
                          <Globe className="w-4 h-4" />
                          {result.issuingCountry}
                        </span>
                      )}
                      {result.documentNumber && (
                        <span className="flex items-center gap-1">
                          <IdCard className="w-4 h-4" />
                          {result.documentNumber}
                        </span>
                      )}
                    </div>
                  </div>
                </div>
              </div>

              {/* Errors List */}
              {result.errors && result.errors.length > 0 && (
                <div className="rounded-2xl bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 p-4">
                  <h3 className="font-bold text-red-700 dark:text-red-400 mb-2 flex items-center gap-2">
                    <AlertTriangle className="w-5 h-5" />
                    검증 오류
                  </h3>
                  <ul className="space-y-1 text-sm text-red-600 dark:text-red-400">
                    {result.errors.map((err, idx) => (
                      <li key={idx} className="flex items-start gap-2">
                        <span className="font-mono text-xs bg-red-100 dark:bg-red-900/50 px-1 rounded">{err.code}</span>
                        <span>{err.message}</span>
                      </li>
                    ))}
                  </ul>
                </div>
              )}

              {/* 3-Column Validation Results (Java Style) */}
              <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                {/* 1. Certificate Chain Validation */}
                <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
                  <div className={cn(
                    'px-4 py-3 flex items-center gap-2',
                    result.certificateChainValidation?.valid
                      ? 'bg-green-500 text-white'
                      : 'bg-red-500 text-white'
                  )}>
                    <Lock className="w-5 h-5" />
                    <span className="font-bold">인증서 체인</span>
                    {getStatusIcon(result.certificateChainValidation?.valid)}
                  </div>
                  <div className="p-4 space-y-3 text-sm">
                    {result.certificateChainValidation && (
                      <>
                        <div>
                          <label className="text-xs text-gray-500 dark:text-gray-400">DSC Subject</label>
                          <p className="font-mono text-xs break-all">{result.certificateChainValidation.dscSubject || '-'}</p>
                        </div>
                        <div>
                          <label className="text-xs text-gray-500 dark:text-gray-400">DSC Serial</label>
                          <p className="font-mono text-xs">{result.certificateChainValidation.dscSerialNumber || '-'}</p>
                        </div>
                        <div>
                          <label className="text-xs text-gray-500 dark:text-gray-400">CSCA Subject</label>
                          <p className="font-mono text-xs break-all">{result.certificateChainValidation.cscaSubject || '-'}</p>
                        </div>
                        <div>
                          <label className="text-xs text-gray-500 dark:text-gray-400">CSCA Serial</label>
                          <p className="font-mono text-xs">{result.certificateChainValidation.cscaSerialNumber || '-'}</p>
                        </div>
                        <div className="grid grid-cols-2 gap-2">
                          <div>
                            <label className="text-xs text-gray-500 dark:text-gray-400">유효기간 시작</label>
                            <p className="text-xs">{result.certificateChainValidation.notBefore || '-'}</p>
                          </div>
                          <div>
                            <label className="text-xs text-gray-500 dark:text-gray-400">유효기간 종료</label>
                            <p className="text-xs">{result.certificateChainValidation.notAfter || '-'}</p>
                          </div>
                        </div>
                        {/* CRL Status */}
                        {result.certificateChainValidation.crlChecked && (
                          <div className={cn(
                            'p-2 rounded-lg',
                            getCrlSeverityColor(result.certificateChainValidation.crlStatusSeverity)
                          )}>
                            <div className="flex items-center gap-2 mb-1">
                              <FileKey className="w-4 h-4" />
                              <span className="font-semibold text-xs">CRL 확인</span>
                            </div>
                            <p className="text-xs">{result.certificateChainValidation.crlStatusDescription}</p>
                            {result.certificateChainValidation.crlStatusDetailedDescription && (
                              <p className="text-xs mt-1 opacity-75">{result.certificateChainValidation.crlStatusDetailedDescription}</p>
                            )}
                          </div>
                        )}
                        {result.certificateChainValidation.validationErrors && (
                          <div className="p-2 rounded-lg bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400 text-xs">
                            {result.certificateChainValidation.validationErrors}
                          </div>
                        )}
                      </>
                    )}
                  </div>
                </div>

                {/* 2. SOD Signature Validation */}
                <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
                  <div className={cn(
                    'px-4 py-3 flex items-center gap-2',
                    result.sodSignatureValidation?.valid
                      ? 'bg-green-500 text-white'
                      : 'bg-red-500 text-white'
                  )}>
                    <ShieldCheck className="w-5 h-5" />
                    <span className="font-bold">SOD 서명</span>
                    {getStatusIcon(result.sodSignatureValidation?.valid)}
                  </div>
                  <div className="p-4 space-y-3 text-sm">
                    {result.sodSignatureValidation && (
                      <>
                        <div>
                          <label className="text-xs text-gray-500 dark:text-gray-400">서명 알고리즘</label>
                          <p className="font-mono text-sm">{result.sodSignatureValidation.signatureAlgorithm || '-'}</p>
                        </div>
                        <div>
                          <label className="text-xs text-gray-500 dark:text-gray-400">해시 알고리즘</label>
                          <p className="font-mono text-sm">{result.sodSignatureValidation.hashAlgorithm || '-'}</p>
                        </div>
                        {result.sodSignatureValidation.validationErrors && (
                          <div className="p-2 rounded-lg bg-red-50 dark:bg-red-900/20 text-red-600 dark:text-red-400 text-xs">
                            {result.sodSignatureValidation.validationErrors}
                          </div>
                        )}
                      </>
                    )}
                  </div>
                </div>

                {/* 3. Data Group Hash Validation */}
                <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
                  <div className={cn(
                    'px-4 py-3 flex items-center gap-2',
                    result.dataGroupValidation?.invalidGroups === 0
                      ? 'bg-green-500 text-white'
                      : 'bg-red-500 text-white'
                  )}>
                    <Hash className="w-5 h-5" />
                    <span className="font-bold">DG 해시</span>
                    {result.dataGroupValidation && getStatusIcon(result.dataGroupValidation.invalidGroups === 0)}
                  </div>
                  <div className="p-4 space-y-3 text-sm">
                    {result.dataGroupValidation && (
                      <>
                        <div className="grid grid-cols-3 gap-2 text-center">
                          <div className="p-2 rounded-lg bg-gray-100 dark:bg-gray-700">
                            <p className="text-lg font-bold text-gray-900 dark:text-white">{result.dataGroupValidation.totalGroups}</p>
                            <p className="text-xs text-gray-500">전체</p>
                          </div>
                          <div className="p-2 rounded-lg bg-green-100 dark:bg-green-900/30">
                            <p className="text-lg font-bold text-green-600">{result.dataGroupValidation.validGroups}</p>
                            <p className="text-xs text-gray-500">성공</p>
                          </div>
                          <div className="p-2 rounded-lg bg-red-100 dark:bg-red-900/30">
                            <p className="text-lg font-bold text-red-600">{result.dataGroupValidation.invalidGroups}</p>
                            <p className="text-xs text-gray-500">실패</p>
                          </div>
                        </div>

                        {/* DG Details */}
                        {result.dataGroupValidation.details && Object.keys(result.dataGroupValidation.details).length > 0 && (
                          <div className="space-y-2">
                            {Object.entries(result.dataGroupValidation.details).map(([dgName, detail]) => (
                              <div key={dgName} className={cn(
                                'p-2 rounded-lg text-xs',
                                detail.valid
                                  ? 'bg-green-50 dark:bg-green-900/20'
                                  : 'bg-red-50 dark:bg-red-900/20'
                              )}>
                                <div className="flex items-center gap-2 mb-1">
                                  {detail.valid ? (
                                    <CheckCircle className="w-3 h-3 text-green-500" />
                                  ) : (
                                    <XCircle className="w-3 h-3 text-red-500" />
                                  )}
                                  <span className="font-semibold">{dgName}</span>
                                </div>
                                <div className="font-mono text-xs break-all">
                                  <p className="text-gray-500">Expected: {detail.expectedHash?.substring(0, 32)}...</p>
                                  <p className="text-gray-500">Actual: {detail.actualHash?.substring(0, 32)}...</p>
                                </div>
                              </div>
                            ))}
                          </div>
                        )}
                      </>
                    )}
                  </div>
                </div>
              </div>

              {/* Verification ID and Timestamp */}
              <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-4">
                <div className="flex items-center justify-between text-sm text-gray-600 dark:text-gray-400">
                  <div>
                    <span className="text-gray-500">검증 ID:</span>
                    <span className="font-mono ml-2">{result.verificationId}</span>
                  </div>
                  <div>
                    <span className="text-gray-500">검증 시간:</span>
                    <span className="ml-2">{new Date(result.verificationTimestamp).toLocaleString('ko-KR')}</span>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* ICAO 9303 검증 단계 카드 - 항상 표시 */}
          {!verifying && !result && (
            <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
              <div className="px-5 py-4 bg-gradient-to-r from-indigo-500 to-purple-500">
                <div className="flex items-center gap-3">
                  <ListChecks className="w-6 h-6 text-white" />
                  <h2 className="text-lg font-bold text-white">검증 단계 (ICAO 9303)</h2>
                </div>
              </div>
              <div className="p-5 space-y-3">
                {/* Step 1 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    1
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 1: SOD 파싱</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">CMS SignedData 구조 분석 및 Data Group 해시 추출</p>
                  </div>
                </div>

                {/* Step 2 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    2
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 2: DSC 추출</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">SOD에서 Document Signer Certificate 추출</p>
                  </div>
                </div>

                {/* Step 3 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    3
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 3: CSCA 조회</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">LDAP에서 Country Signing CA 인증서 조회</p>
                  </div>
                </div>

                {/* Step 4 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    4
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 4: Trust Chain 검증</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">DSC가 CSCA에 의해 서명되었는지 검증</p>
                  </div>
                </div>

                {/* Step 5 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    5
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 5: SOD 서명 검증</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">LDSSecurityObject가 DSC로 서명되었는지 검증</p>
                  </div>
                </div>

                {/* Step 6 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    6
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 6: Data Group 해시 검증</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">SOD의 예상 해시값과 실제 계산된 해시값 비교</p>
                  </div>
                </div>

                {/* Step 7 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    7
                  </div>
                  <div>
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 7: CRL 검사</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">DSC 인증서 폐기 여부 확인 (RFC 5280)</p>
                  </div>
                </div>

                {/* Step 8 */}
                <div className="flex items-start gap-4 p-3 rounded-xl bg-gray-50 dark:bg-gray-700/50">
                  <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/50 flex items-center justify-center text-indigo-600 dark:text-indigo-400 font-bold text-sm">
                    8
                  </div>
                  <div className="flex-grow">
                    <h3 className="font-semibold text-gray-900 dark:text-white">Step 8: Data Group 파싱</h3>
                    <p className="text-sm text-gray-500 dark:text-gray-400">DG1 (MRZ) 및 DG2 (얼굴 이미지) 데이터 추출</p>

                    {/* DG Parsing Buttons */}
                    {dgFiles.length > 0 && (
                      <div className="flex gap-2 mt-2">
                        {dgFiles.some(f => f.number === 'DG1') && (
                          <button
                            onClick={parseDg1}
                            disabled={parsingDg1}
                            className="inline-flex items-center gap-1 px-3 py-1.5 text-xs font-medium rounded-lg bg-indigo-500 text-white hover:bg-indigo-600 disabled:opacity-50"
                          >
                            {parsingDg1 ? <Loader2 className="w-3 h-3 animate-spin" /> : <User className="w-3 h-3" />}
                            DG1 파싱
                          </button>
                        )}
                        {dgFiles.some(f => f.number === 'DG2') && (
                          <button
                            onClick={parseDg2}
                            disabled={parsingDg2}
                            className="inline-flex items-center gap-1 px-3 py-1.5 text-xs font-medium rounded-lg bg-purple-500 text-white hover:bg-purple-600 disabled:opacity-50"
                          >
                            {parsingDg2 ? <Loader2 className="w-3 h-3 animate-spin" /> : <Image className="w-3 h-3" />}
                            DG2 파싱
                          </button>
                        )}
                      </div>
                    )}

                    {/* DG1 Parse Result */}
                    {dg1ParseResult && (
                      <div className={cn(
                        'mt-3 p-3 rounded-lg text-sm',
                        dg1ParseResult.success ? 'bg-green-50 dark:bg-green-900/20' : 'bg-red-50 dark:bg-red-900/20'
                      )}>
                        <div className="flex items-center gap-2 mb-2 font-semibold">
                          {dg1ParseResult.success ? (
                            <CheckCircle className="w-4 h-4 text-green-500" />
                          ) : (
                            <XCircle className="w-4 h-4 text-red-500" />
                          )}
                          <span>DG1 MRZ 파싱 결과</span>
                        </div>
                        {dg1ParseResult.success ? (
                          <div className="space-y-1 text-xs">
                            {dg1ParseResult.mrzLine1 && (
                              <div className="font-mono bg-gray-900 text-green-400 p-2 rounded text-xs break-all">
                                {dg1ParseResult.mrzLine1}<br/>
                                {dg1ParseResult.mrzLine2}
                              </div>
                            )}
                            <div className="grid grid-cols-2 gap-2 mt-2">
                              <div><span className="text-gray-500">성명:</span> {dg1ParseResult.fullName}</div>
                              <div><span className="text-gray-500">여권번호:</span> {dg1ParseResult.documentNumber}</div>
                              <div><span className="text-gray-500">국적:</span> {dg1ParseResult.nationality}</div>
                              <div><span className="text-gray-500">생년월일:</span> {dg1ParseResult.dateOfBirth}</div>
                              <div><span className="text-gray-500">성별:</span> {dg1ParseResult.sex === 'M' ? '남성' : dg1ParseResult.sex === 'F' ? '여성' : dg1ParseResult.sex}</div>
                              <div><span className="text-gray-500">만료일:</span> {dg1ParseResult.dateOfExpiry}</div>
                            </div>
                          </div>
                        ) : (
                          <p className="text-red-600 text-xs">{dg1ParseResult.error}</p>
                        )}
                      </div>
                    )}

                    {/* DG2 Parse Result */}
                    {dg2ParseResult && (
                      <div className={cn(
                        'mt-3 p-3 rounded-lg text-sm',
                        dg2ParseResult.success ? 'bg-green-50 dark:bg-green-900/20' : 'bg-red-50 dark:bg-red-900/20'
                      )}>
                        <div className="flex items-center gap-2 mb-2 font-semibold">
                          {dg2ParseResult.success ? (
                            <CheckCircle className="w-4 h-4 text-green-500" />
                          ) : (
                            <XCircle className="w-4 h-4 text-red-500" />
                          )}
                          <span>DG2 얼굴 이미지 파싱 결과</span>
                        </div>
                        {dg2ParseResult.success ? (
                          <div className="space-y-2">
                            <div className="text-xs">
                              <span className="text-gray-500">얼굴 이미지 수:</span> {dg2ParseResult.faceCount}
                              {dg2ParseResult.hasFacContainer && (
                                <span className="ml-2 px-1.5 py-0.5 bg-indigo-100 text-indigo-600 rounded text-xs">ISO 19794-5 FAC</span>
                              )}
                            </div>
                            {dg2ParseResult.faceImages && dg2ParseResult.faceImages.length > 0 && (
                              <div className="flex flex-wrap gap-2">
                                {dg2ParseResult.faceImages.map((face, idx) => (
                                  <div key={idx} className="text-center">
                                    {face.imageDataUrl && (
                                      <img src={face.imageDataUrl} alt={`Face ${idx + 1}`} className="w-20 h-24 object-cover rounded shadow" />
                                    )}
                                    <p className="text-xs text-gray-500 mt-1">{face.imageFormat} {face.width}×{face.height}</p>
                                  </div>
                                ))}
                              </div>
                            )}
                          </div>
                        ) : (
                          <p className="text-red-600 text-xs">{dg2ParseResult.error}</p>
                        )}
                      </div>
                    )}
                  </div>
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

export default PAVerify;
