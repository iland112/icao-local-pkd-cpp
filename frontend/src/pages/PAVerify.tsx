import { useTranslation } from 'react-i18next';
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
import type { TFunction } from 'i18next';

interface FileInfo {
  file: File;
  name: string;
  number?: DataGroupNumber;
}

const getInitialSteps = (t: TFunction): VerificationStep[] => [
  { id: 1, title: t('pa:verify.step1Title'), description: t('pa:verify.step1Desc'), status: 'pending' },
  { id: 2, title: t('pa:verify.step2Title'), description: t('pa:verify.step2Desc'), status: 'pending' },
  { id: 3, title: t('pa:verify.step3Title'), description: t('pa:verify.step3Desc'), status: 'pending' },
  { id: 4, title: t('pa:verify.step4Title'), description: t('pa:verify.step4Desc'), status: 'pending' },
  { id: 5, title: t('pa:verify.step5Title'), description: t('pa:verify.step5Desc'), status: 'pending' },
  { id: 6, title: t('pa:verify.step6Title'), description: t('pa:verify.step6Desc'), status: 'pending' },
  { id: 7, title: t('pa:verify.step7Title'), description: t('pa:verify.step7Desc'), status: 'pending' },
];

type VerificationMode = 'full' | 'quick';

export function PAVerify() {
  const { t } = useTranslation(['pa', 'common']);
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
  const initialSteps = getInitialSteps(t);
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
        message: t('pa:verify.sodParseComplete'),
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
          message: t('pa:verify.dscExtractComplete'),
          details: {
            subject: response.certificateChainValidation.dscSubject,
            serial: response.certificateChainValidation.dscSerialNumber,
            issuer: response.certificateChainValidation.cscaSubject,
          },
          expanded: true,
        };
      }

      // Step 3: CSCA 조회
      if (response.certificateChainValidation?.cscaSubject) {
        newSteps[2] = {
          ...newSteps[2],
          status: 'success',
          message: t('pa:verify.cscaLookupSuccess'),
          details: {
            dn: response.certificateChainValidation.cscaSubject,
          },
          expanded: true,
        };
      } else {
        newSteps[2] = {
          ...newSteps[2],
          status: 'error',
          message: t('pa:verify.cscaLookupFailed'),
          details: {
            error: response.certificateChainValidation?.validationErrors,
            errorCode: response.certificateChainValidation?.errorCode,
            dscSubject: response.certificateChainValidation?.dscSubject,
            dscIssuer: response.certificateChainValidation?.dscIssuer,
          },
          expanded: true,
        };
      }

      // Step 4: Trust Chain 검증
      if (response.certificateChainValidation?.valid) {
        const chainValidation = response.certificateChainValidation;
        // Determine status based on expiration
        const stepStatus = chainValidation.expirationStatus === 'EXPIRED' || chainValidation.expirationStatus === 'WARNING'
          ? 'warning' as StepStatus
          : 'success' as StepStatus;
        const statusMessage = chainValidation.expirationStatus === 'EXPIRED'
          ? t('pa:verify.trustChainSuccessExpired')
          : chainValidation.expirationStatus === 'WARNING'
            ? t('pa:verify.trustChainSuccessExpiring')
            : t('pa:verify.trustChainSuccess');

        newSteps[3] = {
          ...newSteps[3],
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
        newSteps[3] = {
          ...newSteps[3],
          status: 'error',
          message: t('pa:verify.trustChainFailed'),
          details: {
            error: chainValidation?.validationErrors,
            errorCode: chainValidation?.errorCode,
            dscSubject: chainValidation?.dscSubject,
            dscIssuer: chainValidation?.dscIssuer,
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

      // Step 5: SOD 서명 검증
      if (response.sodSignatureValidation?.valid) {
        newSteps[4] = {
          ...newSteps[4],
          status: 'success',
          message: t('pa:verify.sodSignatureSuccess'),
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
          message: t('pa:verify.sodSignatureFailed'),
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
            message: t('pa:verify.dgHashAllSuccess', { valid: validGroups, total: totalGroups }),
            details: { dgDetails: details },
            expanded: true,
          };
        } else {
          newSteps[5] = {
            ...newSteps[5],
            status: 'error',
            message: t('pa:verify.dgHashSomeFailed', { invalid: invalidGroups, total: totalGroups }),
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
            message: t('pa:verify.crlCheckValid'),
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
            message: t('pa:verify.crlCheckRevoked'),
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
          message: response.certificateChainValidation?.crlMessage || t('pa:verify.crlNotFound'),
          details: {
            description: response.certificateChainValidation?.crlStatusDescription,
            detailedDescription: response.certificateChainValidation?.crlStatusDetailedDescription,
          },
          expanded: true,
        };
      }

      // DSC 등록은 백엔드에서 pending_dsc_registration 테이블에 자동 저장됨
      // 관리자 승인은 /admin/pending-dsc 페이지에서 별도 처리

      return newSteps;
    });

    setExpandedSteps(new Set([1, 2, 3, 4, 5, 6, 7, 8]));
  }, [t]);

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
        dg1Result = { success: false, error: t('pa:verify.dg1ParseFailed') };
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
        dg2Result = { success: false, error: t('pa:verify.dg2ParseFailed') };
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
      setErrorMessage(t('pa:verify.mrzParseFailed'));
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

      // Get current user for requestedBy
      let requestedBy = 'anonymous';
      try {
        const userStr = localStorage.getItem('user');
        if (userStr) {
          const user = JSON.parse(userStr);
          requestedBy = user.username || user.full_name || 'anonymous';
        }
        // Fallback: decode username from JWT token
        if (requestedBy === 'anonymous') {
          const token = localStorage.getItem('access_token');
          if (token) {
            try {
              const payload = JSON.parse(atob(token.split('.')[1]));
              requestedBy = payload.username || payload.sub || 'anonymous';
            } catch { /* malformed token — use anonymous */ }
          }
        }
      } catch { /* ignore */ }

      const request: PAVerificationRequest = {
        sod: sodBase64,
        dataGroups,
        mrzData: mrzData || undefined,
        requestedBy,
      };

      const response = await paApi.verify(request);

      if (response.data.success && response.data.data) {
        setResult(response.data.data);
        if (response.data.data.status === 'VALID') {
          setSuccessMessage(t('pa:verify.paSuccess'));
        }
      } else {
        throw new Error(response.data.error || t('pa:result.failed'));
      }
    } catch (error) {
      setErrorMessage(error instanceof Error ? error.message : t('pa:verify.verificationError'));
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
      setQuickLookupError(t('pa:verify.enterSubjectDnOrFingerprint'));
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
        setQuickLookupError(data.error || t('pa:verify.lookupFailed'));
      } else if (!data.validation) {
        setQuickLookupError(t('pa:verify.noValidationResultHint'));
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : t('pa:verify.lookupError');
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
              {t('pa:verify.pageTitle')}
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('pa:verify.pageDescription')}
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
          {t('pa:verify.fullVerification')}
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
          {t('pa:verify.quickLookup')}
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
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">{t('pa:verify.dataFileUpload')}</h2>
              </div>

              {/* File Upload */}
              <div className="mb-3">
                <label className="block text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 flex items-center gap-1">
                  <FileText className="w-4 h-4 text-blue-500" />
                  {t('pa:verify.selectDataFiles')}
                </label>
                <input
                  ref={fileInputRef}
                  type="file"
                  multiple
                  accept=".bin"
                  onChange={handleDirectoryUpload}
                  className="w-full text-sm text-gray-500 file:mr-4 file:py-2 file:px-4 file:rounded-lg file:border-0 file:text-sm file:font-medium file:bg-blue-50 file:text-blue-700 hover:file:bg-blue-100 dark:file:bg-blue-900/30 dark:file:text-blue-400"
                />
                <p className="mt-1 text-xs text-gray-500">{t('pa:verify.selectDataFilesHint')}</p>
              </div>

              {/* MRZ File Upload */}
              <div className="mb-3">
                <label className="block text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 flex items-center gap-1">
                  <FileText className="w-4 h-4 text-purple-500" />
                  {t('pa:verify.mrzFileLabel')}
                  <span className="text-xs text-gray-400 ml-2">{t('common:label.optional')}</span>
                </label>
                <input
                  ref={mrzInputRef}
                  type="file"
                  accept=".txt"
                  onChange={handleMrzFileUpload}
                  className="w-full text-sm text-gray-500 file:mr-4 file:py-2 file:px-4 file:rounded-lg file:border-0 file:text-sm file:font-medium file:bg-purple-50 file:text-purple-700 hover:file:bg-purple-100 dark:file:bg-purple-900/30 dark:file:text-purple-400"
                />
                <p className="mt-1 text-xs text-gray-500">{t('pa:verify.mrzFileHint')}</p>
              </div>

              {/* MRZ Preview */}
              {mrzData && (
                <div className="mb-3 p-3 bg-gray-50 dark:bg-gray-700 rounded-lg">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">{t('pa:verify.mrzParseResult')}</span>
                    <button onClick={clearMrzFile} className="text-gray-400 hover:text-gray-600">
                      <X className="w-4 h-4" />
                    </button>
                  </div>
                  <div className="grid grid-cols-1 sm:grid-cols-2 gap-2 text-xs">
                    <div><span className="text-gray-500">{t('common:label.fullName')}:</span> {mrzData.fullName}</div>
                    <div><span className="text-gray-500">{t('common:label.passportNumber')}:</span> {mrzData.documentNumber}</div>
                    <div><span className="text-gray-500">{t('common:label.nationality')}:</span> {mrzData.nationality}</div>
                    <div><span className="text-gray-500">{t('common:label.dateOfBirth')}:</span> {mrzData.dateOfBirth}</div>
                    <div><span className="text-gray-500">{t('common:label.gender')}:</span> {mrzData.sex === 'M' ? t('common:label.male') : mrzData.sex === 'F' ? t('common:label.female') : '-'}</div>
                    <div><span className="text-gray-500">{t('common:label.expiryDate')}:</span> {mrzData.expirationDate}</div>
                  </div>
                </div>
              )}

              {/* Uploaded Files Summary */}
              {(sodFile || dgFiles.length > 0) && (
                <div className="mb-3 p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-lg">
                  <div className="flex items-center gap-2 mb-2">
                    <CheckCircle className="w-4 h-4 text-green-500" />
                    <span className="text-sm font-semibold text-green-700 dark:text-green-400">
                      {t('pa:verify.uploadCount', { num: (sodFile ? 1 : 0) + dgFiles.length })}
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
                {verifying ? t('pa:verify.verifying') : t('pa:verify.startVerification')}
              </button>

              <button
                onClick={clearAllData}
                disabled={verifying}
                className="w-full mt-2 inline-flex items-center justify-center gap-2 px-4 py-2 rounded-xl text-sm font-medium transition-all duration-200 border text-gray-600 dark:text-gray-300 border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50"
              >
                <X className="w-4 h-4" />
                {t('common:button.reset')}
              </button>
            </div>
          </div>

          {/* Data Preview Card */}
          {sodInfo && (
            <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5">
              <h3 className="font-bold text-gray-900 dark:text-white flex items-center gap-2 mb-3">
                <Eye className="w-5 h-5 text-blue-500" />
                {t('pa:verify.dataPreview')}
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
