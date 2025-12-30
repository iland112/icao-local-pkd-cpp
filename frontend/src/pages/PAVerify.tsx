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
} from 'lucide-react';
import { paApi } from '@/services/api';
import type { PAVerificationRequest, PAVerificationResult, DataGroupNumber, MRZData } from '@/types';
import { cn } from '@/utils/cn';

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
  const [result, setResult] = useState<PAVerificationResult | null>(null);
  const [currentStep, setCurrentStep] = useState(0);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  // SOD preview data
  const [sodInfo, setSodInfo] = useState<{
    dscSubject?: string;
    dscSerial?: string;
    hashAlgorithm?: string;
    dataGroups?: number[];
  } | null>(null);

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
      // In a real implementation, we would parse the SOD to extract info
      // For now, just show file info
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
    setCurrentStep(0);
    setErrorMessage(null);
    setSuccessMessage(null);
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  const performVerification = async () => {
    if (!sodFile) return;

    setVerifying(true);
    setErrorMessage(null);
    setSuccessMessage(null);
    setResult(null);
    setCurrentStep(1);

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

      // Simulate step progression
      for (let step = 1; step <= 7; step++) {
        setCurrentStep(step);
        await delay(300);
      }

      const response = await paApi.verify(request);

      if (response.data.success && response.data.data) {
        setResult(response.data.data);
        setSuccessMessage('Passive Authentication 검증이 완료되었습니다.');
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

  const delay = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

  const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
  };

  const steps = result
    ? [
        { name: 'SOD 파싱', result: result.sodParsing },
        { name: 'DSC 추출', result: result.dscExtraction },
        { name: 'CSCA 조회', result: result.cscaLookup },
        { name: 'Trust Chain', result: result.trustChainValidation },
        { name: 'SOD 서명', result: result.sodSignatureValidation },
        { name: 'DG 해시', result: result.dataGroupHashValidation },
        { name: 'CRL 확인', result: result.crlCheck },
      ]
    : [];

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

        {/* Right Column: Verification Steps & Results */}
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

          {successMessage && (
            <div className="mb-4 p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-lg flex items-center gap-2 text-green-700 dark:text-green-400">
              <CheckCircle className="w-5 h-5" />
              <span className="text-sm">{successMessage}</span>
            </div>
          )}

          {/* Verification Steps */}
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg">
            <div className="p-5">
              <div className="flex items-center gap-3 mb-5">
                <div className="p-2.5 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                  <ShieldCheck className="w-5 h-5 text-cyan-500" />
                </div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">검증 단계 (ICAO 9303)</h2>
              </div>

              <div className="space-y-4">
                {[
                  { step: 1, name: 'SOD 파싱', desc: 'CMS SignedData 구조 분석 및 Data Group 해시 추출' },
                  { step: 2, name: 'DSC 추출', desc: 'SOD에서 Document Signer Certificate 추출' },
                  { step: 3, name: 'CSCA 조회', desc: 'LDAP에서 Country Signing CA 인증서 조회' },
                  { step: 4, name: 'Trust Chain', desc: 'DSC → CSCA Trust Chain 검증' },
                  { step: 5, name: 'SOD 서명', desc: 'SOD CMS 서명 검증' },
                  { step: 6, name: 'DG 해시', desc: 'Data Group 해시 무결성 검증' },
                  { step: 7, name: 'CRL 확인', desc: 'DSC 인증서 폐기 상태 확인' },
                ].map((s, idx) => {
                  const stepResult = result ? steps[idx]?.result : null;
                  const isActive = verifying && currentStep === s.step;

                  return (
                    <div
                      key={s.step}
                      className={cn(
                        'flex items-start gap-3 pb-4 border-b border-gray-100 dark:border-gray-700 last:border-0',
                        currentStep < s.step && !result && 'opacity-50'
                      )}
                    >
                      <div className="flex-shrink-0">
                        <div
                          className={cn(
                            'w-10 h-10 rounded-full flex items-center justify-center',
                            isActive && 'bg-blue-500 text-white',
                            stepResult?.status === 'SUCCESS' && 'bg-green-500 text-white',
                            stepResult?.status === 'FAILED' && 'bg-red-500 text-white',
                            !isActive && !stepResult && 'bg-gray-200 dark:bg-gray-700 text-gray-500'
                          )}
                        >
                          {isActive ? (
                            <Loader2 className="w-5 h-5 animate-spin" />
                          ) : stepResult?.status === 'SUCCESS' ? (
                            <CheckCircle className="w-5 h-5" />
                          ) : stepResult?.status === 'FAILED' ? (
                            <XCircle className="w-5 h-5" />
                          ) : (
                            <span className="font-bold text-sm">{s.step}</span>
                          )}
                        </div>
                      </div>
                      <div className="flex-grow min-w-0">
                        <h3 className="font-bold text-gray-900 dark:text-white">Step {s.step}: {s.name}</h3>
                        <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">{s.desc}</p>
                        {stepResult && (
                          <div className={cn(
                            'mt-2 p-2 rounded-lg text-sm',
                            stepResult.status === 'SUCCESS' && 'bg-green-50 dark:bg-green-900/20 text-green-700 dark:text-green-400',
                            stepResult.status === 'FAILED' && 'bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-400'
                          )}>
                            {stepResult.message}
                          </div>
                        )}
                      </div>
                    </div>
                  );
                })}
              </div>

              {/* Final Result */}
              {result && (
                <div className={cn(
                  'mt-6 p-4 rounded-xl',
                  result.overallValid
                    ? 'bg-gradient-to-r from-green-500 to-emerald-500 text-white'
                    : 'bg-gradient-to-r from-red-500 to-rose-500 text-white'
                )}>
                  <div className="flex items-center gap-3">
                    {result.overallValid ? (
                      <CheckCircle className="w-8 h-8" />
                    ) : (
                      <XCircle className="w-8 h-8" />
                    )}
                    <div>
                      <h3 className="text-lg font-bold">
                        {result.overallValid ? 'Passive Authentication 성공' : 'Passive Authentication 실패'}
                      </h3>
                      <p className="text-sm opacity-90">
                        처리 시간: {result.processingTimeMs}ms
                      </p>
                    </div>
                  </div>
                </div>
              )}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default PAVerify;
