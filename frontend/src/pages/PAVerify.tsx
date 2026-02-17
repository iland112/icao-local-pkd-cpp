import React, { useState, useRef, useCallback, useEffect } from 'react';
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
  Zap,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import type {
  PAVerificationRequest,
  PAVerificationResponse,
  DataGroupNumber,
  MRZData,
} from '@/types';
import { cn } from '@/utils/cn';
import { QuickLookupPanel } from '@/components/pa/QuickLookupPanel';
import type { QuickLookupResult } from '@/components/pa/QuickLookupPanel';
import { VerificationStepsPanel } from '@/components/pa/VerificationStepsPanel';
import type { VerificationStep, StepStatus } from '@/components/pa/VerificationStepsPanel';
import { VerificationResultCard } from '@/components/pa/VerificationResultCard';
import type { DG1ParseResult, DG2ParseResult } from '@/components/pa/VerificationResultCard';

interface FileInfo {
  file: File;
  name: string;
  number?: DataGroupNumber;
}

// 초기 검증 단계 정의
const initialSteps: VerificationStep[] = [
  { id: 1, title: 'Step 1: SOD 파싱', description: 'CMS SignedData 구조 분석 및 Data Group 해시 추출', status: 'pending' },
  { id: 2, title: 'Step 2: DSC 추출', description: 'SOD에서 Document Signer Certificate 추출', status: 'pending' },
  { id: 3, title: 'Step 3: Trust Chain 검증', description: 'DSC 서명을 CSCA 공개키로 검증', status: 'pending' },
  { id: 4, title: 'Step 4: CSCA 조회', description: 'Local PKD에서 CSCA 검색 (Link Certificate 포함)', status: 'pending' },
  { id: 5, title: 'Step 5: SOD 서명 검증', description: 'LDSSecurityObject가 DSC로 서명되었는지 검증', status: 'pending' },
  { id: 6, title: 'Step 6: Data Group 해시 검증', description: 'SOD의 예상 해시값과 실제 계산된 해시값 비교', status: 'pending' },
  { id: 7, title: 'Step 7: CRL 검사', description: 'DSC 인증서 폐기 여부 확인 (RFC 5280)', status: 'pending' },
  { id: 8, title: 'Step 8: DSC 자동 등록', description: '신규 발견된 DSC를 Local PKD에 자동 등록', status: 'pending' },
];

type VerificationMode = 'full' | 'quick';

export function PAVerify() {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const mrzInputRef = useRef<HTMLInputElement>(null);

  // Mode state
  const [verificationMode, setVerificationMode] = useState<VerificationMode>('full');

  // Quick lookup states
  const [quickLookupDn, setQuickLookupDn] = useState('');
  const [quickLookupFingerprint, setQuickLookupFingerprint] = useState('');
  const [quickLookupResult, setQuickLookupResult] = useState<QuickLookupResult | null>(null);
  const [quickLookupLoading, setQuickLookupLoading] = useState(false);
  const [quickLookupError, setQuickLookupError] = useState<string | null>(null);

  // File states
  const [sodFile, setSodFile] = useState<File | null>(null);
  const [dgFiles, setDgFiles] = useState<FileInfo[]>([]);
  const [, setMrzFile] = useState<File | null>(null);
  const [mrzData, setMrzData] = useState<MRZData | null>(null);

  // Verification states
  const [verifying, setVerifying] = useState(false);
  const [result, setResult] = useState<PAVerificationResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [, setSuccessMessage] = useState<string | null>(null);

  // Step-based verification progress
  const [steps, setSteps] = useState<VerificationStep[]>(initialSteps);
  const [expandedSteps, setExpandedSteps] = useState<Set<number>>(new Set());

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
  const [, setParsingDg1] = useState(false);
  const [, setParsingDg2] = useState(false);

  // 결과로부터 Step 상태 업데이트
  const updateStepsFromResult = useCallback((response: PAVerificationResponse) => {
    // SOD 미리보기 정보 업데이트 (검증 결과에서 실제 값으로)
    const dgDetails = response.dataGroupValidation?.details || {};
    const dataGroupNumbers = Object.keys(dgDetails)
      .map((dgName: string) => {
        const match = dgName.match(/DG(\d+)/);
        return match ? parseInt(match[1], 10) : 0;
      })
      .filter((n: number) => n > 0)
      .sort((a: number, b: number) => a - b);

    setSodInfo({
      dscSubject: response.certificateChainValidation?.dscSubject || 'N/A',
      dscSerial: response.certificateChainValidation?.dscSerialNumber,
      hashAlgorithm: response.sodSignatureValidation?.hashAlgorithm || 'SHA-256',
      dataGroups: dataGroupNumbers.length > 0 ? dataGroupNumbers : [1, 2],
    });

    setSteps(prevSteps => {
      const newSteps = [...prevSteps];

      // Step 1: SOD 파싱 - 항상 성공 (응답이 왔으면)
      newSteps[0] = {
        ...newSteps[0],
        status: 'success',
        message: '✓ SOD 파싱 완료',
        details: {
          hashAlgorithm: response.sodSignatureValidation?.hashAlgorithm,
          signatureAlgorithm: response.sodSignatureValidation?.signatureAlgorithm,
        },
        expanded: true,
      };

      // Step 2: DSC 추출
      if (response.certificateChainValidation?.dscSubject) {
        newSteps[1] = {
          ...newSteps[1],
          status: 'success',
          message: '✓ DSC 인증서 추출 완료',
          details: {
            subject: response.certificateChainValidation.dscSubject,
            serial: response.certificateChainValidation.dscSerialNumber,
            issuer: response.certificateChainValidation.cscaSubject,
          },
          expanded: true,
        };
      }

      // Step 3: Trust Chain 검증
      if (response.certificateChainValidation?.valid) {
        const chainValidation = response.certificateChainValidation;
        // Determine status based on expiration
        const stepStatus = chainValidation.expirationStatus === 'EXPIRED' || chainValidation.expirationStatus === 'WARNING'
          ? 'warning' as StepStatus
          : 'success' as StepStatus;
        const statusMessage = chainValidation.expirationStatus === 'EXPIRED'
          ? '✓ Trust Chain 검증 성공 (인증서 만료됨)'
          : chainValidation.expirationStatus === 'WARNING'
            ? '✓ Trust Chain 검증 성공 (인증서 만료 임박)'
            : '✓ Trust Chain 검증 성공';

        newSteps[2] = {
          ...newSteps[2],
          status: stepStatus,
          message: statusMessage,
          details: {
            dscSubject: chainValidation.dscSubject,
            cscaSubject: chainValidation.cscaSubject,
            notBefore: chainValidation.notBefore,
            notAfter: chainValidation.notAfter,
            dscExpired: chainValidation.dscExpired,
            cscaExpired: chainValidation.cscaExpired,
            validAtSigningTime: chainValidation.validAtSigningTime,
            expirationStatus: chainValidation.expirationStatus,
            expirationMessage: chainValidation.expirationMessage,
            dscNonConformant: chainValidation.dscNonConformant,
            pkdConformanceCode: chainValidation.pkdConformanceCode,
            pkdConformanceText: chainValidation.pkdConformanceText,
          },
          expanded: true,
        };
      } else {
        const chainValidation = response.certificateChainValidation;
        newSteps[2] = {
          ...newSteps[2],
          status: 'error',
          message: '✗ Trust Chain 검증 실패',
          details: {
            error: chainValidation?.validationErrors,
            dscSubject: chainValidation?.dscSubject,
            cscaSubject: chainValidation?.cscaSubject,
            notBefore: chainValidation?.notBefore,
            notAfter: chainValidation?.notAfter,
            dscNonConformant: chainValidation?.dscNonConformant,
            pkdConformanceCode: chainValidation?.pkdConformanceCode,
            pkdConformanceText: chainValidation?.pkdConformanceText,
          },
          expanded: true,
        };
      }

      // Step 4: CSCA 조회
      if (response.certificateChainValidation?.cscaSubject) {
        newSteps[3] = {
          ...newSteps[3],
          status: 'success',
          message: '✓ CSCA 인증서 조회 성공',
          details: {
            dn: response.certificateChainValidation.cscaSubject,
          },
          expanded: true,
        };
      } else {
        newSteps[3] = {
          ...newSteps[3],
          status: 'error',
          message: '✗ CSCA 인증서를 찾을 수 없음',
          details: {
            error: response.certificateChainValidation?.validationErrors,
            dscSubject: response.certificateChainValidation?.dscSubject,
          },
          expanded: true,
        };
      }

      // Step 5: SOD 서명 검증
      if (response.sodSignatureValidation?.valid) {
        newSteps[4] = {
          ...newSteps[4],
          status: 'success',
          message: '✓ SOD 서명 검증 성공',
          details: {
            signatureAlgorithm: response.sodSignatureValidation.signatureAlgorithm,
            hashAlgorithm: response.sodSignatureValidation.hashAlgorithm,
          },
          expanded: true,
        };
      } else {
        newSteps[4] = {
          ...newSteps[4],
          status: 'error',
          message: '✗ SOD 서명 검증 실패',
          details: {
            error: response.sodSignatureValidation?.validationErrors,
            signatureAlgorithm: response.sodSignatureValidation?.signatureAlgorithm,
            hashAlgorithm: response.sodSignatureValidation?.hashAlgorithm,
          },
          expanded: true,
        };
      }

      // Step 6: DG 해시 검증
      if (response.dataGroupValidation) {
        const { totalGroups, validGroups, invalidGroups, details } = response.dataGroupValidation;
        if (invalidGroups === 0) {
          newSteps[5] = {
            ...newSteps[5],
            status: 'success',
            message: `✓ 모든 Data Group 해시 검증 성공 (${validGroups}/${totalGroups})`,
            details: { dgDetails: details },
            expanded: true,
          };
        } else {
          newSteps[5] = {
            ...newSteps[5],
            status: 'error',
            message: `✗ Data Group 해시 검증 실패 (${invalidGroups}/${totalGroups} 실패)`,
            details: { dgDetails: details },
            expanded: true,
          };
        }
      }

      // Step 7: CRL 검사
      if (response.certificateChainValidation?.crlChecked) {
        if (!response.certificateChainValidation.revoked) {
          newSteps[6] = {
            ...newSteps[6],
            status: 'success',
            message: '✓ CRL 확인 완료 - 인증서 유효',
            details: {
              description: response.certificateChainValidation.crlStatusDescription,
              detailedDescription: response.certificateChainValidation.crlStatusDetailedDescription,
            },
            expanded: true,
          };
        } else {
          newSteps[6] = {
            ...newSteps[6],
            status: 'error',
            message: '✗ 인증서가 폐기됨',
            details: {
              description: response.certificateChainValidation.crlStatusDescription,
              detailedDescription: response.certificateChainValidation.crlStatusDetailedDescription,
            },
            expanded: true,
          };
        }
      } else {
        // CRL을 찾을 수 없는 경우 warning
        newSteps[6] = {
          ...newSteps[6],
          status: 'warning',
          message: response.certificateChainValidation?.crlMessage || '⚠ CRL을 찾을 수 없음',
          details: {
            description: response.certificateChainValidation?.crlStatusDescription,
            detailedDescription: response.certificateChainValidation?.crlStatusDetailedDescription,
          },
          expanded: true,
        };
      }

      // Step 8: DSC 자동 등록
      if (response.dscAutoRegistration) {
        const dscReg = response.dscAutoRegistration;
        if (dscReg.registered && dscReg.newlyRegistered) {
          newSteps[7] = {
            ...newSteps[7],
            status: 'success',
            message: '✓ DSC 자동 등록 완료',
            details: {
              certificateId: dscReg.certificateId,
              fingerprint: dscReg.fingerprint,
              countryCode: dscReg.countryCode,
              newlyRegistered: true,
            },
            expanded: true,
          };
        } else if (dscReg.registered && !dscReg.newlyRegistered) {
          newSteps[7] = {
            ...newSteps[7],
            status: 'success',
            message: '✓ DSC 이미 등록됨',
            details: {
              certificateId: dscReg.certificateId,
              fingerprint: dscReg.fingerprint,
              countryCode: dscReg.countryCode,
              newlyRegistered: false,
            },
            expanded: true,
          };
        } else {
          newSteps[7] = {
            ...newSteps[7],
            status: 'warning',
            message: '⚠ DSC 등록 실패',
            expanded: true,
          };
        }
      } else {
        newSteps[7] = {
          ...newSteps[7],
          status: 'warning',
          message: '⚠ DSC 자동 등록 정보 없음',
          expanded: true,
        };
      }

      return newSteps;
    });

    // 모든 단계 펼치기
    setExpandedSteps(new Set([1, 2, 3, 4, 5, 6, 7, 8]));
  }, []);

  // 자동 DG 파싱 함수
  const autoParseDataGroups = useCallback(async () => {
    const dg1File = dgFiles.find(f => f.number === 'DG1');
    const dg2File = dgFiles.find(f => f.number === 'DG2');

    let dg1Result: DG1ParseResult | null = null;
    let dg2Result: DG2ParseResult | null = null;
    let parsedCount = 0;

    // DG1 파싱
    if (dg1File) {
      setParsingDg1(true);
      try {
        const base64 = await fileToBase64(dg1File.file);
        const response = await paApi.parseDG1(base64);
        const data = response.data;
        dg1Result = data as DG1ParseResult;
        setDg1ParseResult(data as DG1ParseResult);
        if ((data as DG1ParseResult).success) parsedCount++;
      } catch {
        dg1Result = { success: false, error: 'DG1 파싱 실패' };
        setDg1ParseResult(dg1Result);
      } finally {
        setParsingDg1(false);
      }
    }

    // DG2 파싱
    if (dg2File) {
      setParsingDg2(true);
      try {
        const base64 = await fileToBase64(dg2File.file);
        const response = await paApi.parseDG2(base64);
        const data = response.data;
        dg2Result = data as DG2ParseResult;
        setDg2ParseResult(data as DG2ParseResult);
        if ((data as DG2ParseResult).success) parsedCount++;
      } catch {
        dg2Result = { success: false, error: 'DG2 파싱 실패' };
        setDg2ParseResult(dg2Result);
      } finally {
        setParsingDg2(false);
      }
    }

    // DG 파싱 결과는 상단 결과 박스에서 표시 (Step 8은 DSC 자동 등록으로 변경됨)
    // parsedCount, dg1Result, dg2Result는 state에 저장되어 결과 박스에서 사용
    void parsedCount;
    void dg1Result;
    void dg2Result;
  }, [dgFiles]);

  // 결과가 변경되면 Step 상태 업데이트 및 자동 DG 파싱
  useEffect(() => {
    if (result) {
      updateStepsFromResult(result);
      // 자동으로 DG1/DG2 파싱 실행
      autoParseDataGroups();
    }
  }, [result, updateStepsFromResult, autoParseDataGroups]);

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
    setSteps(initialSteps);
    setDg1ParseResult(null);
    setDg2ParseResult(null);
  }, []);

  const previewSodFile = async (_file: File) => {
    try {
      setSodInfo({
        dscSubject: 'Parsing...',
        hashAlgorithm: 'SHA-256',
        dataGroups: [1, 2],
      });
    } catch {
      if (import.meta.env.DEV) console.error('Failed to preview SOD');
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
    setSteps(initialSteps);
    setExpandedSteps(new Set());
    setDg1ParseResult(null);
    setDg2ParseResult(null);
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
    setDg1ParseResult(null);
    setDg2ParseResult(null);

    // 모든 Step을 running 상태로 변경
    setSteps(prevSteps => prevSteps.map(step => ({ ...step, status: 'running' as StepStatus })));

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
          setSuccessMessage('✓ Passive Authentication 검증 성공');
        }
      } else {
        throw new Error(response.data.error || '검증 실패');
      }
    } catch (error) {
      setErrorMessage(error instanceof Error ? error.message : '검증 중 오류가 발생했습니다.');
      // 에러 시 모든 Step을 pending으로 리셋
      setSteps(initialSteps);
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

  const toggleStep = (stepId: number) => {
    setExpandedSteps(prev => {
      const newSet = new Set(prev);
      if (newSet.has(stepId)) {
        newSet.delete(stepId);
      } else {
        newSet.add(stepId);
      }
      return newSet;
    });
  };

  // Quick lookup handler
  const handleQuickLookup = async () => {
    if (!quickLookupDn && !quickLookupFingerprint) {
      setQuickLookupError('Subject DN 또는 Fingerprint를 입력해주세요.');
      return;
    }

    setQuickLookupLoading(true);
    setQuickLookupError(null);
    setQuickLookupResult(null);

    try {
      const params: { subjectDn?: string; fingerprint?: string } = {};
      if (quickLookupDn) params.subjectDn = quickLookupDn;
      else if (quickLookupFingerprint) params.fingerprint = quickLookupFingerprint;

      const response = await paApi.paLookup(params);
      const data = response.data as QuickLookupResult;
      setQuickLookupResult(data);

      if (!data.success) {
        setQuickLookupError(data.error || '조회 실패');
      } else if (!data.validation) {
        setQuickLookupError('해당 인증서의 검증 결과가 없습니다. 파일 업로드를 통해 먼저 Trust Chain 검증을 수행해주세요.');
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : '조회 중 오류가 발생했습니다.';
      setQuickLookupError(msg);
    } finally {
      setQuickLookupLoading(false);
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

      {/* Mode Toggle Tabs */}
      <div className="flex gap-2 mb-6">
        <button
          onClick={() => setVerificationMode('full')}
          className={cn(
            'flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium transition-all',
            verificationMode === 'full'
              ? 'bg-teal-500 text-white shadow-md'
              : 'bg-white dark:bg-gray-800 text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700 border border-gray-200 dark:border-gray-700'
          )}
        >
          <ShieldCheck className="w-4 h-4" />
          전체 검증
        </button>
        <button
          onClick={() => setVerificationMode('quick')}
          className={cn(
            'flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium transition-all',
            verificationMode === 'quick'
              ? 'bg-teal-500 text-white shadow-md'
              : 'bg-white dark:bg-gray-800 text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700 border border-gray-200 dark:border-gray-700'
          )}
        >
          <Zap className="w-4 h-4" />
          간편 검증 (Trust Chain 조회)
        </button>
      </div>

      {/* Quick Lookup Mode */}
      {verificationMode === 'quick' && (
        <QuickLookupPanel
          quickLookupDn={quickLookupDn}
          setQuickLookupDn={setQuickLookupDn}
          quickLookupFingerprint={quickLookupFingerprint}
          setQuickLookupFingerprint={setQuickLookupFingerprint}
          quickLookupResult={quickLookupResult}
          quickLookupLoading={quickLookupLoading}
          quickLookupError={quickLookupError}
          setQuickLookupError={setQuickLookupError}
          handleQuickLookup={handleQuickLookup}
        />
      )}

      {/* Full Verification Mode */}
      {verificationMode === 'full' && (
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5">
        {/* Left Column: File Upload */}
        <div className="lg:col-span-1 space-y-4">
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
            <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5">
              <h3 className="font-bold text-gray-900 dark:text-white flex items-center gap-2 mb-3">
                <Eye className="w-5 h-5 text-blue-500" />
                데이터 미리보기
              </h3>
              <div className="text-sm space-y-2 text-gray-600 dark:text-gray-400">
                <div>
                  <span className="font-semibold">DSC Subject:</span>
                  <div className="mt-1 text-xs font-mono bg-gray-100 dark:bg-gray-700 px-2 py-1 rounded break-all">
                    {sodInfo.dscSubject}
                  </div>
                </div>
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
        <div className="lg:col-span-2 space-y-4">
          {/* Alert Messages */}
          {errorMessage && (
            <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg flex items-center gap-2 text-red-700 dark:text-red-400">
              <XCircle className="w-5 h-5" />
              <span className="text-sm">{errorMessage}</span>
              <button onClick={() => setErrorMessage(null)} className="ml-auto">
                <X className="w-4 h-4" />
              </button>
            </div>
          )}

          {/* Overall Result Summary (top position) */}
          {result && (
            <VerificationResultCard
              result={result}
              dg1ParseResult={dg1ParseResult}
              dg2ParseResult={dg2ParseResult}
            />
          )}

          {/* ICAO 9303 Verification Steps */}
          <VerificationStepsPanel
            steps={steps}
            expandedSteps={expandedSteps}
            toggleStep={toggleStep}
          />

        </div>
      </div>
      )}
    </div>
  );
}

export default PAVerify;
