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
  AlertTriangle,
  Clock,
  Award,
  Globe,
  ListChecks,
  ChevronDown,
  ChevronRight,
  ExternalLink,
  Hash,
  FileKey,
  Search,
  Zap,
} from 'lucide-react';
import { paApi } from '@/services/api';
import type {
  PAVerificationRequest,
  PAVerificationResponse,
  DataGroupNumber,
  MRZData,
} from '@/types';
import { cn } from '@/utils/cn';
import { Link } from 'react-router-dom';

// Verification Step 상태 타입
type StepStatus = 'pending' | 'running' | 'success' | 'warning' | 'error';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type StepDetails = Record<string, any>;

interface VerificationStep {
  id: number;
  title: string;
  description: string;
  status: StepStatus;
  message?: string;
  details?: StepDetails;
  expanded?: boolean;
}

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

// DG 해시 상세 타입
interface DGHashDetail {
  valid: boolean;
  expectedHash: string;
  actualHash: string;
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

// Quick lookup result type (matches findByFingerprint/findBySubjectDn response)
interface QuickLookupResult {
  success: boolean;
  validation?: {
    certificateType?: string;
    countryCode?: string;
    subjectDn?: string;
    issuerDn?: string;
    serialNumber?: string;
    validationStatus?: string;
    trustChainValid?: boolean;
    trustChainMessage?: string;
    trustChainPath?: string;
    cscaFound?: boolean;
    cscaSubjectDn?: string;
    signatureVerified?: boolean;
    signatureAlgorithm?: string;
    validityCheckPassed?: boolean;
    notBefore?: string;
    notAfter?: string;
    crlCheckStatus?: string;
    crlChecked?: boolean;
    fingerprint?: string;
    validatedAt?: string;
    pkdConformanceCode?: string;
    pkdConformanceText?: string;
    pkdVersion?: string;
  } | null;
  error?: string;
}

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

  const getStepStatusIcon = (status: StepStatus) => {
    switch (status) {
      case 'success':
        return <CheckCircle className="w-5 h-5 text-green-500" />;
      case 'error':
        return <XCircle className="w-5 h-5 text-red-500" />;
      case 'warning':
        return <AlertTriangle className="w-5 h-5 text-yellow-500" />;
      case 'running':
        return <Loader2 className="w-5 h-5 text-blue-500 animate-spin" />;
      default:
        return <div className="w-5 h-5 rounded-full border-2 border-gray-300 dark:border-gray-600" />;
    }
  };

  const getStepBgColor = (status: StepStatus) => {
    switch (status) {
      case 'success':
        return 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800';
      case 'error':
        return 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800';
      case 'warning':
        return 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800';
      case 'running':
        return 'bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800';
      default:
        return 'bg-gray-50 dark:bg-gray-700/50 border-gray-200 dark:border-gray-700';
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
        <div className="max-w-3xl space-y-5">
          {/* Input Card */}
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5">
            <div className="flex items-center gap-3 mb-4">
              <div className="p-2.5 rounded-xl bg-teal-50 dark:bg-teal-900/30">
                <Search className="w-5 h-5 text-teal-500" />
              </div>
              <div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">Trust Chain 조회</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">
                  파일 업로드 시 수행된 Trust Chain 검증 결과를 DSC Subject DN 또는 Fingerprint로 조회합니다.
                </p>
              </div>
            </div>

            <div className="space-y-3">
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                  DSC Subject DN
                </label>
                <input
                  type="text"
                  value={quickLookupDn}
                  onChange={(e) => { setQuickLookupDn(e.target.value); setQuickLookupFingerprint(''); }}
                  placeholder="/C=KR/O=Government of Korea/CN=Document Signer..."
                  className="w-full px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-sm text-gray-900 dark:text-gray-100 focus:ring-2 focus:ring-teal-500 focus:border-transparent"
                />
              </div>
              <div className="text-center text-xs text-gray-400">또는</div>
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                  SHA-256 Fingerprint
                </label>
                <input
                  type="text"
                  value={quickLookupFingerprint}
                  onChange={(e) => { setQuickLookupFingerprint(e.target.value); setQuickLookupDn(''); }}
                  placeholder="a1b2c3d4e5f6..."
                  className="w-full px-3 py-2 rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-sm font-mono text-gray-900 dark:text-gray-100 focus:ring-2 focus:ring-teal-500 focus:border-transparent"
                />
              </div>
              <button
                onClick={handleQuickLookup}
                disabled={quickLookupLoading || (!quickLookupDn && !quickLookupFingerprint)}
                className={cn(
                  'w-full flex items-center justify-center gap-2 px-4 py-2.5 rounded-lg text-sm font-medium transition-all',
                  quickLookupLoading || (!quickLookupDn && !quickLookupFingerprint)
                    ? 'bg-gray-300 dark:bg-gray-600 text-gray-500 cursor-not-allowed'
                    : 'bg-teal-500 hover:bg-teal-600 text-white shadow-md'
                )}
              >
                {quickLookupLoading ? (
                  <><Loader2 className="w-4 h-4 animate-spin" /> 조회 중...</>
                ) : (
                  <><Search className="w-4 h-4" /> Trust Chain 결과 조회</>
                )}
              </button>
            </div>
          </div>

          {/* Error Message */}
          {quickLookupError && (
            <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg flex items-center gap-2 text-red-700 dark:text-red-400">
              <XCircle className="w-5 h-5 shrink-0" />
              <span className="text-sm">{quickLookupError}</span>
              <button onClick={() => setQuickLookupError(null)} className="ml-auto">
                <X className="w-4 h-4" />
              </button>
            </div>
          )}

          {/* Result Card */}
          {quickLookupResult?.validation && (
            <div className="space-y-4">
              {/* Overall Status */}
              <div className={cn(
                'rounded-2xl p-4',
                quickLookupResult.validation.validationStatus === 'VALID' || quickLookupResult.validation.validationStatus === 'EXPIRED_VALID'
                  ? 'bg-gradient-to-r from-green-500 to-emerald-500 text-white'
                  : quickLookupResult.validation.validationStatus === 'INVALID'
                  ? 'bg-gradient-to-r from-red-500 to-rose-500 text-white'
                  : 'bg-gradient-to-r from-yellow-500 to-orange-500 text-white'
              )}>
                <div className="flex items-center gap-3">
                  {quickLookupResult.validation.validationStatus === 'VALID' || quickLookupResult.validation.validationStatus === 'EXPIRED_VALID' ? (
                    <Award className="w-10 h-10" />
                  ) : quickLookupResult.validation.validationStatus === 'INVALID' ? (
                    <XCircle className="w-10 h-10" />
                  ) : (
                    <AlertTriangle className="w-10 h-10" />
                  )}
                  <div>
                    <h2 className="text-lg font-bold">
                      Trust Chain: {quickLookupResult.validation.validationStatus}
                    </h2>
                    <div className="flex items-center gap-4 mt-0.5 text-sm opacity-90">
                      {quickLookupResult.validation.countryCode && (
                        <span className="flex items-center gap-1">
                          <Globe className="w-3.5 h-3.5" />
                          {quickLookupResult.validation.countryCode}
                        </span>
                      )}
                      {quickLookupResult.validation.certificateType && (
                        <span className="flex items-center gap-1">
                          <FileKey className="w-3.5 h-3.5" />
                          {quickLookupResult.validation.certificateType}
                        </span>
                      )}
                    </div>
                  </div>
                </div>
              </div>

              {/* Detail Card */}
              <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-5 space-y-3">
                <h3 className="text-sm font-bold text-gray-900 dark:text-white flex items-center gap-2">
                  <ListChecks className="w-4 h-4 text-teal-500" />
                  검증 상세 결과
                </h3>

                {/* Trust Chain */}
                <div className={cn(
                  'p-3 rounded-lg border text-xs',
                  quickLookupResult.validation.trustChainValid
                    ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                    : 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
                )}>
                  <div className="flex items-center gap-2 mb-1.5">
                    {quickLookupResult.validation.trustChainValid
                      ? <CheckCircle className="w-4 h-4 text-green-500" />
                      : <XCircle className="w-4 h-4 text-red-500" />}
                    <span className="font-semibold">Trust Chain 검증</span>
                  </div>
                  {quickLookupResult.validation.trustChainPath && (
                    <div className="ml-6 text-gray-600 dark:text-gray-400">
                      경로: {quickLookupResult.validation.trustChainPath}
                    </div>
                  )}
                  {quickLookupResult.validation.trustChainMessage && (
                    <div className="ml-6 text-gray-600 dark:text-gray-400">
                      {quickLookupResult.validation.trustChainMessage}
                    </div>
                  )}
                </div>

                {/* CSCA Info */}
                <div className={cn(
                  'p-3 rounded-lg border text-xs',
                  quickLookupResult.validation.cscaFound
                    ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                    : 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800'
                )}>
                  <div className="flex items-center gap-2 mb-1.5">
                    {quickLookupResult.validation.cscaFound
                      ? <CheckCircle className="w-4 h-4 text-green-500" />
                      : <AlertTriangle className="w-4 h-4 text-yellow-500" />}
                    <span className="font-semibold">CSCA 조회</span>
                  </div>
                  {quickLookupResult.validation.cscaSubjectDn && (
                    <div className="ml-6">
                      <span className="text-gray-500">CSCA DN: </span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {quickLookupResult.validation.cscaSubjectDn}
                      </code>
                    </div>
                  )}
                </div>

                {/* Signature Verification */}
                <div className={cn(
                  'p-3 rounded-lg border text-xs',
                  quickLookupResult.validation.signatureVerified
                    ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                    : 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
                )}>
                  <div className="flex items-center gap-2">
                    {quickLookupResult.validation.signatureVerified
                      ? <CheckCircle className="w-4 h-4 text-green-500" />
                      : <XCircle className="w-4 h-4 text-red-500" />}
                    <span className="font-semibold">서명 검증</span>
                    {quickLookupResult.validation.signatureAlgorithm && (
                      <code className="ml-2 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                        {quickLookupResult.validation.signatureAlgorithm}
                      </code>
                    )}
                  </div>
                </div>

                {/* Validity Period */}
                <div className={cn(
                  'p-3 rounded-lg border text-xs',
                  quickLookupResult.validation.validityCheckPassed
                    ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                    : 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800'
                )}>
                  <div className="flex items-center gap-2 mb-1.5">
                    {quickLookupResult.validation.validityCheckPassed
                      ? <CheckCircle className="w-4 h-4 text-green-500" />
                      : <AlertTriangle className="w-4 h-4 text-yellow-500" />}
                    <span className="font-semibold">유효기간</span>
                  </div>
                  {(quickLookupResult.validation.notBefore || quickLookupResult.validation.notAfter) && (
                    <div className="ml-6 text-gray-600 dark:text-gray-400">
                      {quickLookupResult.validation.notBefore} ~ {quickLookupResult.validation.notAfter}
                    </div>
                  )}
                </div>

                {/* CRL Check */}
                <div className={cn(
                  'p-3 rounded-lg border text-xs',
                  quickLookupResult.validation.crlCheckStatus === 'REVOKED'
                    ? 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
                    : 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                )}>
                  <div className="flex items-center gap-2">
                    {quickLookupResult.validation.crlCheckStatus === 'REVOKED'
                      ? <XCircle className="w-4 h-4 text-red-500" />
                      : <CheckCircle className="w-4 h-4 text-green-500" />}
                    <span className="font-semibold">CRL 검사</span>
                    <span className="text-gray-500 ml-1">({quickLookupResult.validation.crlCheckStatus || 'NOT_CHECKED'})</span>
                  </div>
                </div>

                {/* Non-Conformant Reason (DSC_NC only) */}
                {quickLookupResult.validation.certificateType === 'DSC_NC' && (
                  <div className="p-3 rounded-lg bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800 text-xs">
                    <div className="flex items-center gap-2 mb-1.5">
                      <AlertTriangle className="w-4 h-4 text-orange-500" />
                      <span className="font-semibold text-orange-700 dark:text-orange-300">Non-Conformant (비준수)</span>
                    </div>
                    {quickLookupResult.validation.pkdConformanceCode && (
                      <div className="ml-6 mb-1">
                        <span className="text-gray-500">코드: </span>
                        <code className="font-mono bg-orange-100 dark:bg-orange-800/40 px-1.5 py-0.5 rounded text-orange-700 dark:text-orange-300">
                          {quickLookupResult.validation.pkdConformanceCode}
                        </code>
                      </div>
                    )}
                    {quickLookupResult.validation.pkdConformanceText && (
                      <div className="ml-6 mb-1 text-gray-600 dark:text-gray-400">
                        <span className="text-gray-500">사유: </span>
                        {quickLookupResult.validation.pkdConformanceText}
                      </div>
                    )}
                    {quickLookupResult.validation.pkdVersion && (
                      <div className="ml-6 text-gray-500">
                        PKD Version: {quickLookupResult.validation.pkdVersion}
                      </div>
                    )}
                    {!quickLookupResult.validation.pkdConformanceCode && !quickLookupResult.validation.pkdConformanceText && (
                      <div className="ml-6 text-gray-500">
                        ICAO PKD에서 Non-Conformant로 분류된 인증서입니다.
                      </div>
                    )}
                  </div>
                )}

                {/* Certificate Info */}
                <div className="p-3 rounded-lg bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-700 text-xs space-y-1.5">
                  <div className="font-semibold text-gray-700 dark:text-gray-300 mb-1">인증서 정보</div>
                  {quickLookupResult.validation.subjectDn && (
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">Subject:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {quickLookupResult.validation.subjectDn}
                      </code>
                    </div>
                  )}
                  {quickLookupResult.validation.issuerDn && (
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">Issuer:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {quickLookupResult.validation.issuerDn}
                      </code>
                    </div>
                  )}
                  {quickLookupResult.validation.fingerprint && (
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">Fingerprint:</span>
                      <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                        {quickLookupResult.validation.fingerprint}
                      </code>
                    </div>
                  )}
                  {quickLookupResult.validation.validatedAt && (
                    <div className="flex items-start gap-2">
                      <span className="text-gray-500 shrink-0 w-16">검증일시:</span>
                      <span className="text-gray-600 dark:text-gray-400">{quickLookupResult.validation.validatedAt}</span>
                    </div>
                  )}
                </div>

                {/* SOD/DG Note */}
                <div className="p-3 rounded-lg bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 text-xs text-blue-700 dark:text-blue-300">
                  <div className="flex items-start gap-2">
                    <Zap className="w-4 h-4 shrink-0 mt-0.5" />
                    <div>
                      <span className="font-semibold">간편 검증 모드: </span>
                      SOD 서명 검증, Data Group 해시 검증은 전체 검증 모드에서만 수행됩니다.
                      위 결과는 파일 업로드 시 수행된 Trust Chain 검증 결과입니다.
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}
        </div>
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
                <p className="mt-1 text-xs text-gray-500">mrz.txt 파일 (TD3 포맷: 2줄 × 44자)</p>
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
            <div className={cn(
              'rounded-2xl p-4',
              result.status === 'VALID'
                ? 'bg-gradient-to-r from-green-500 to-emerald-500 text-white'
                : result.status === 'INVALID'
                ? 'bg-gradient-to-r from-red-500 to-rose-500 text-white'
                : 'bg-gradient-to-r from-yellow-500 to-orange-500 text-white'
            )}>
              <div className="flex items-center gap-3">
                {result.status === 'VALID' ? (
                  <Award className="w-10 h-10" />
                ) : result.status === 'INVALID' ? (
                  <XCircle className="w-10 h-10" />
                ) : (
                  <AlertTriangle className="w-10 h-10" />
                )}
                <div className="flex-grow">
                  <h2 className="text-lg font-bold">
                    {result.status === 'VALID'
                      ? '검증 성공'
                      : result.status === 'INVALID'
                      ? '검증 실패'
                      : '검증 오류'}
                  </h2>
                  <div className="flex items-center gap-4 mt-0.5 text-sm opacity-90">
                    <span className="flex items-center gap-1">
                      <Clock className="w-3.5 h-3.5" />
                      {result.processingDurationMs}ms
                    </span>
                    {result.issuingCountry && (
                      <span className="flex items-center gap-1">
                        <Globe className="w-3.5 h-3.5" />
                        {result.issuingCountry}
                      </span>
                    )}
                    {result.documentNumber && (
                      <span className="flex items-center gap-1">
                        <IdCard className="w-3.5 h-3.5" />
                        {result.documentNumber}
                      </span>
                    )}
                  </div>
                </div>
                <Link
                  to={`/pa/history?id=${result.verificationId}`}
                  className="flex items-center gap-1 text-xs opacity-80 hover:opacity-100 underline shrink-0"
                >
                  <ExternalLink className="w-3.5 h-3.5" />
                  상세
                </Link>
              </div>

              {/* Failure reasons */}
              {result.status === 'INVALID' && (
                <div className="mt-3 pt-3 border-t border-white/20 space-y-1.5">
                  <div className="text-xs font-semibold opacity-90">실패 원인:</div>
                  {!result.certificateChainValidation?.valid && (
                    <div className="flex items-center gap-2 text-sm opacity-90">
                      <XCircle className="w-3.5 h-3.5 shrink-0" />
                      <span>Trust Chain 검증 실패{result.certificateChainValidation?.validationErrors ? ` — ${result.certificateChainValidation.validationErrors}` : ''}</span>
                    </div>
                  )}
                  {!result.sodSignatureValidation?.valid && (
                    <div className="flex items-center gap-2 text-sm opacity-90">
                      <XCircle className="w-3.5 h-3.5 shrink-0" />
                      <span>SOD 서명 검증 실패{result.sodSignatureValidation?.validationErrors ? ` — ${result.sodSignatureValidation.validationErrors}` : ''}</span>
                    </div>
                  )}
                  {result.dataGroupValidation && result.dataGroupValidation.invalidGroups > 0 && (
                    <div className="flex items-center gap-2 text-sm opacity-90">
                      <XCircle className="w-3.5 h-3.5 shrink-0" />
                      <span>Data Group 해시 불일치 ({result.dataGroupValidation.invalidGroups}/{result.dataGroupValidation.totalGroups})</span>
                    </div>
                  )}
                  {result.certificateChainValidation?.revoked && (
                    <div className="flex items-center gap-2 text-sm opacity-90">
                      <XCircle className="w-3.5 h-3.5 shrink-0" />
                      <span>인증서 폐기됨 (CRL)</span>
                    </div>
                  )}
                </div>
              )}

              {/* Non-Conformant DSC warning (shown regardless of VALID/INVALID) */}
              {result.certificateChainValidation?.dscNonConformant && (
                <div className="mt-3 pt-3 border-t border-white/20">
                  <div className="flex items-center gap-2 text-sm">
                    <AlertTriangle className="w-4 h-4 text-amber-300 shrink-0" />
                    <span className="font-semibold text-amber-200">Non-Conformant DSC</span>
                  </div>
                  <p className="mt-1 text-xs opacity-80">
                    {result.certificateChainValidation.pkdConformanceCode && (
                      <span className="font-mono">{result.certificateChainValidation.pkdConformanceCode}: </span>
                    )}
                    {result.certificateChainValidation.pkdConformanceText || 'ICAO PKD 비준수 인증서'}
                  </p>
                </div>
              )}

              {/* DG Parsing Results (shown when verification succeeds) */}
              {result.status === 'VALID' && (dg1ParseResult || dg2ParseResult) && (
                <div className="mt-3 pt-3 border-t border-white/20">
                  <div className="flex gap-3">
                    {/* DG2 Face Image */}
                    {dg2ParseResult?.success && dg2ParseResult.faceImages?.[0] && (
                      <div className="shrink-0">
                        <img
                          src={dg2ParseResult.faceImages[0].imageDataUrl}
                          alt="Passport Face"
                          className="w-20 h-26 object-cover rounded-lg border-2 border-white/40 shadow-md"
                        />
                      </div>
                    )}
                    {/* DG1 MRZ Data */}
                    {dg1ParseResult?.success && (
                      <div className="flex-grow grid grid-cols-3 gap-x-4 gap-y-1 text-xs">
                        <div>
                          <span className="opacity-70">성명</span>
                          <div className="font-semibold">{dg1ParseResult.fullName}</div>
                        </div>
                        <div>
                          <span className="opacity-70">여권번호</span>
                          <div className="font-mono font-semibold">{dg1ParseResult.documentNumber}</div>
                        </div>
                        <div>
                          <span className="opacity-70">국적</span>
                          <div className="font-semibold">{dg1ParseResult.nationality}</div>
                        </div>
                        <div>
                          <span className="opacity-70">생년월일</span>
                          <div className="font-mono font-semibold">{dg1ParseResult.dateOfBirth}</div>
                        </div>
                        <div>
                          <span className="opacity-70">만료일</span>
                          <div className="font-mono font-semibold">{dg1ParseResult.dateOfExpiry}</div>
                        </div>
                        <div>
                          <span className="opacity-70">성별</span>
                          <div className="font-semibold">
                            {dg1ParseResult.sex === 'M' ? '남성' : dg1ParseResult.sex === 'F' ? '여성' : dg1ParseResult.sex}
                          </div>
                        </div>
                      </div>
                    )}
                  </div>
                </div>
              )}

              {/* Verification ID & Timestamp */}
              <div className="mt-3 pt-2 border-t border-white/20 text-xs flex flex-wrap gap-4 opacity-75">
                <div>
                  <span>검증 ID: </span>
                  <span className="font-mono">{result.verificationId}</span>
                </div>
                <div>
                  <span>검증 시각: </span>
                  <span>{new Date(result.verificationTimestamp).toLocaleString('ko-KR')}</span>
                </div>
              </div>
            </div>
          )}

          {/* ICAO 9303 Verification Steps */}
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg overflow-hidden">
            <div className="px-5 py-4 bg-gradient-to-r from-indigo-500 to-purple-500">
              <div className="flex items-center gap-3">
                <ListChecks className="w-6 h-6 text-white" />
                <h2 className="text-lg font-bold text-white">검증 단계 (ICAO 9303)</h2>
              </div>
            </div>
            <div className="p-4 space-y-2">
              {steps.map((step: VerificationStep) => (
                <div
                  key={step.id}
                  className={cn(
                    'rounded-xl border transition-all duration-200',
                    getStepBgColor(step.status)
                  )}
                >
                  {/* Step Header */}
                  <div
                    className="flex items-center gap-3 p-3 cursor-pointer"
                    onClick={() => toggleStep(step.id)}
                  >
                    {getStepStatusIcon(step.status)}
                    <div className="flex-grow">
                      <h3 className="font-semibold text-gray-900 dark:text-white text-sm">{step.title}</h3>
                      <p className="text-xs text-gray-500 dark:text-gray-400">{step.description}</p>
                    </div>
                    {(step.message || step.id === 8) && (
                      expandedSteps.has(step.id) ? (
                        <ChevronDown className="w-4 h-4 text-gray-400" />
                      ) : (
                        <ChevronRight className="w-4 h-4 text-gray-400" />
                      )
                    )}
                  </div>

                  {/* Step Details (Expanded) */}
                  {expandedSteps.has(step.id) && (step.message || step.id === 8) && (
                    <div className="px-4 pb-3 pt-0">
                      {step.message ? (
                        <div className={cn(
                          'text-sm mb-2',
                          step.status === 'success' ? 'text-green-700 dark:text-green-400' :
                          step.status === 'error' ? 'text-red-700 dark:text-red-400' :
                          step.status === 'warning' ? 'text-yellow-700 dark:text-yellow-400' :
                          'text-gray-600 dark:text-gray-400'
                        )}>
                          {step.message}
                        </div>
                      ) : null}



                      {/* Step 1: SOD 파싱 상세 정보 */}
                      {step.id === 1 && step.details && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-2">
                          <div className="grid grid-cols-2 gap-2">
                            <div>
                              <span className="text-gray-500">해시 알고리즘:</span>
                              <code className="ml-1 font-mono bg-blue-100 dark:bg-blue-900/30 px-1.5 py-0.5 rounded text-blue-700 dark:text-blue-300">
                                {String(step.details.hashAlgorithm || '')}
                              </code>
                            </div>
                            <div>
                              <span className="text-gray-500">서명 알고리즘:</span>
                              <code className="ml-1 font-mono bg-purple-100 dark:bg-purple-900/30 px-1.5 py-0.5 rounded text-purple-700 dark:text-purple-300">
                                {String(step.details.signatureAlgorithm || '')}
                              </code>
                            </div>
                          </div>
                        </div>
                      )}

                      {/* Step 2: DSC 추출 상세 정보 */}
                      {step.id === 2 && step.details && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-16">주체(Subject):</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.subject || ''}
                            </code>
                          </div>
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-16">일련번호:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                              {step.details.serial || ''}
                            </code>
                          </div>
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0 w-16">발급자:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.issuer || ''}
                            </code>
                          </div>
                        </div>
                      )}

                      {/* Step 3: Trust Chain 검증 상세 정보 (성공) */}
                      {step.id === 3 && step.details && step.status !== 'error' && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                          <div className="font-semibold text-gray-700 dark:text-gray-300 mb-2">Trust Chain 경로:</div>
                          <div className="flex flex-col items-center gap-1">
                            {/* CSCA (Root) */}
                            <div className="w-full p-2 bg-green-100 dark:bg-green-900/30 rounded border border-green-300 dark:border-green-700">
                              <div className="flex items-center gap-2">
                                <Award className="w-4 h-4 text-green-600 dark:text-green-400" />
                                <span className="font-semibold text-green-700 dark:text-green-300">CSCA (Root)</span>
                              </div>
                              <code className="block mt-1 text-xs font-mono break-all text-gray-600 dark:text-gray-400">
                                {step.details.cscaSubject}
                              </code>
                            </div>
                            {/* Arrow */}
                            <div className="text-gray-400 dark:text-gray-500">↓ 서명</div>
                            {/* DSC (Leaf) */}
                            <div className="w-full p-2 bg-blue-100 dark:bg-blue-900/30 rounded border border-blue-300 dark:border-blue-700">
                              <div className="flex items-center gap-2">
                                <FileKey className="w-4 h-4 text-blue-600 dark:text-blue-400" />
                                <span className="font-semibold text-blue-700 dark:text-blue-300">DSC (Document Signer)</span>
                                {step.details.dscExpired && (
                                  <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-orange-200 dark:bg-orange-900/50 text-orange-700 dark:text-orange-300">
                                    만료됨
                                  </span>
                                )}
                              </div>
                              <code className="block mt-1 text-xs font-mono break-all text-gray-600 dark:text-gray-400">
                                {step.details.dscSubject}
                              </code>
                              {step.details.notBefore && step.details.notAfter && (
                                <div className="mt-1 text-xs text-gray-500">
                                  유효기간: {step.details.notBefore} ~ {step.details.notAfter}
                                </div>
                              )}
                            </div>

                            {/* Certificate Expiration Warning/Info */}
                            {step.details.expirationStatus && step.details.expirationStatus !== 'VALID' && (
                              <div className={cn(
                                'w-full mt-2 p-2 rounded text-xs',
                                step.details.expirationStatus === 'EXPIRED'
                                  ? 'bg-orange-100 dark:bg-orange-900/30 border border-orange-300 dark:border-orange-700'
                                  : 'bg-yellow-100 dark:bg-yellow-900/30 border border-yellow-300 dark:border-yellow-700'
                              )}>
                                <div className="flex items-center gap-2">
                                  <AlertTriangle className={cn(
                                    'w-4 h-4',
                                    step.details.expirationStatus === 'EXPIRED'
                                      ? 'text-orange-600 dark:text-orange-400'
                                      : 'text-yellow-600 dark:text-yellow-400'
                                  )} />
                                  <span className={cn(
                                    'font-semibold',
                                    step.details.expirationStatus === 'EXPIRED'
                                      ? 'text-orange-700 dark:text-orange-300'
                                      : 'text-yellow-700 dark:text-yellow-300'
                                  )}>
                                    {step.details.expirationStatus === 'EXPIRED' ? '인증서 만료' : '인증서 만료 임박'}
                                  </span>
                                  {step.details.validAtSigningTime && (
                                    <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-green-200 dark:bg-green-900/50 text-green-700 dark:text-green-300">
                                      ✓ 서명 당시 유효
                                    </span>
                                  )}
                                </div>
                                {step.details.expirationMessage && (
                                  <p className="mt-1 text-gray-600 dark:text-gray-400">
                                    {step.details.expirationMessage}
                                  </p>
                                )}
                              </div>
                            )}
                            {/* DSC Non-Conformant Warning (ICAO PKD nc-data) */}
                            {step.details.dscNonConformant && (
                              <div className="w-full mt-2 p-2 rounded text-xs bg-amber-100 dark:bg-amber-900/30 border border-amber-300 dark:border-amber-700">
                                <div className="flex items-center gap-2">
                                  <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400" />
                                  <span className="font-semibold text-amber-700 dark:text-amber-300">Non-Conformant DSC (ICAO PKD)</span>
                                </div>
                                {step.details.pkdConformanceCode && (
                                  <p className="mt-1 text-gray-600 dark:text-gray-400">
                                    <span className="font-mono font-medium">{step.details.pkdConformanceCode}</span>
                                    {step.details.pkdConformanceText && (
                                      <span> — {step.details.pkdConformanceText}</span>
                                    )}
                                  </p>
                                )}
                              </div>
                            )}
                          </div>
                        </div>
                      )}
                      {/* Step 3: Trust Chain 검증 상세 정보 (실패) */}
                      {step.id === 3 && step.status === 'error' && step.details && (
                        <div className="mt-2 space-y-2">
                          {/* 검증 대상 인증서 정보 */}
                          {(step.details.dscSubject || step.details.cscaSubject) && (
                            <div className="p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                              <div className="font-semibold text-gray-700 dark:text-gray-300 mb-1">검증 대상 인증서:</div>
                              {step.details.dscSubject && (
                                <div className="flex items-start gap-2">
                                  <span className="text-gray-500 shrink-0 w-12">DSC:</span>
                                  <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                                    {step.details.dscSubject}
                                  </code>
                                </div>
                              )}
                              {step.details.cscaSubject && (
                                <div className="flex items-start gap-2">
                                  <span className="text-gray-500 shrink-0 w-12">CSCA:</span>
                                  <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                                    {step.details.cscaSubject}
                                  </code>
                                </div>
                              )}
                              {step.details.notBefore && step.details.notAfter && (
                                <div className="flex items-start gap-2">
                                  <span className="text-gray-500 shrink-0 w-12">유효기간:</span>
                                  <span className="text-gray-600 dark:text-gray-400">
                                    {step.details.notBefore} ~ {step.details.notAfter}
                                  </span>
                                </div>
                              )}
                            </div>
                          )}
                          {/* 실패 원인 */}
                          <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs">
                            <div className="flex items-start gap-2 text-red-600 dark:text-red-400">
                              <XCircle className="w-4 h-4 shrink-0 mt-0.5" />
                              <div>
                                <span className="font-semibold">실패 원인: </span>
                                {step.details.error
                                  ? String(step.details.error)
                                  : 'DSC 인증서가 CSCA에 의해 서명되지 않았거나 서명 검증에 실패했습니다.'}
                              </div>
                            </div>
                          </div>
                        </div>
                      )}

                      {/* Step 4: CSCA 조회 상세 정보 */}
                      {step.id === 4 && step.details && step.status === 'success' && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                          <div className="flex items-start gap-2">
                            <span className="text-gray-500 shrink-0">CSCA DN:</span>
                            <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                              {step.details.dn || ''}
                            </code>
                          </div>
                        </div>
                      )}
                      {step.id === 4 && step.status === 'error' && step.details && (
                        <div className="mt-2 p-3 bg-red-50 dark:bg-red-900/20 rounded-lg text-xs space-y-1.5">
                          {step.details.dscSubject && (
                            <div className="flex items-start gap-2 text-gray-600 dark:text-gray-400">
                              <span className="text-gray-500 shrink-0">DSC 발급자:</span>
                              <code className="font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded break-all">
                                {step.details.dscSubject}
                              </code>
                            </div>
                          )}
                          {step.details.error && (
                            <div className="text-red-600 dark:text-red-400">
                              <span className="font-semibold">원인:</span> {String(step.details.error)}
                            </div>
                          )}
                        </div>
                      )}

                      {/* Step 5: SOD 서명 검증 상세 정보 (성공) */}
                      {step.id === 5 && step.status === 'success' && step.details && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                          <div className="grid grid-cols-2 gap-2">
                            {step.details.signatureAlgorithm && (
                              <div>
                                <span className="text-gray-500">서명 알고리즘:</span>
                                <code className="ml-1 font-mono bg-purple-100 dark:bg-purple-900/30 px-1.5 py-0.5 rounded text-purple-700 dark:text-purple-300">
                                  {String(step.details.signatureAlgorithm)}
                                </code>
                              </div>
                            )}
                            {step.details.hashAlgorithm && (
                              <div>
                                <span className="text-gray-500">해시 알고리즘:</span>
                                <code className="ml-1 font-mono bg-blue-100 dark:bg-blue-900/30 px-1.5 py-0.5 rounded text-blue-700 dark:text-blue-300">
                                  {String(step.details.hashAlgorithm)}
                                </code>
                              </div>
                            )}
                          </div>
                        </div>
                      )}
                      {/* Step 5: SOD 서명 검증 상세 정보 (실패) */}
                      {step.id === 5 && step.status === 'error' && (
                        <div className="mt-2 space-y-2">
                          {step.details && (step.details.signatureAlgorithm || step.details.hashAlgorithm) && (
                            <div className="p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs">
                              <div className="grid grid-cols-2 gap-2">
                                {step.details.signatureAlgorithm && (
                                  <div>
                                    <span className="text-gray-500">서명 알고리즘:</span>
                                    <code className="ml-1 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                                      {String(step.details.signatureAlgorithm)}
                                    </code>
                                  </div>
                                )}
                                {step.details.hashAlgorithm && (
                                  <div>
                                    <span className="text-gray-500">해시 알고리즘:</span>
                                    <code className="ml-1 font-mono bg-gray-200 dark:bg-gray-600 px-1.5 py-0.5 rounded">
                                      {String(step.details.hashAlgorithm)}
                                    </code>
                                  </div>
                                )}
                              </div>
                            </div>
                          )}
                          <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg text-xs">
                            <div className="flex items-start gap-2 text-red-600 dark:text-red-400">
                              <XCircle className="w-4 h-4 shrink-0 mt-0.5" />
                              <div>
                                <span className="font-semibold">실패 원인: </span>
                                {step.details?.error
                                  ? String(step.details.error)
                                  : 'SOD의 서명이 DSC 공개키로 검증되지 않았습니다.'}
                              </div>
                            </div>
                          </div>
                        </div>
                      )}

                      {/* Step 6: DG 해시 검증 상세 정보 */}
                      {step.id === 6 && step.details?.dgDetails ? (() => {
                        const dgDetails = step.details.dgDetails as Record<string, DGHashDetail>;
                        return (
                          <div className="space-y-2 mt-2">
                            {Object.entries(dgDetails).map(([dgName, detail]) => (
                              <div key={dgName} className={cn(
                                'p-3 rounded-lg text-xs',
                                detail.valid
                                  ? 'bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800'
                                  : 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                              )}>
                                <div className="flex items-center justify-between mb-2">
                                  <div className="flex items-center gap-2">
                                    <span className="font-semibold text-sm">{dgName}</span>
                                    {dgName === 'DG1' ? <span className="text-gray-500">기계판독영역 (MRZ)</span> : null}
                                    {dgName === 'DG2' ? <span className="text-gray-500">얼굴 생체이미지</span> : null}
                                    {dgName === 'DG3' ? <span className="text-gray-500">지문 생체정보</span> : null}
                                    {dgName === 'DG14' ? <span className="text-gray-500">보안 옵션</span> : null}
                                  </div>
                                  <div className={cn(
                                    'px-2 py-0.5 rounded-full text-xs font-medium',
                                    detail.valid ? 'bg-green-200 text-green-700 dark:bg-green-800 dark:text-green-200' : 'bg-red-200 text-red-700 dark:bg-red-800 dark:text-red-200'
                                  )}>
                                    {detail.valid ? (
                                      <span className="flex items-center gap-1"><CheckCircle className="w-3 h-3" /> 일치</span>
                                    ) : (
                                      <span className="flex items-center gap-1"><XCircle className="w-3 h-3" /> 불일치</span>
                                    )}
                                  </div>
                                </div>
                                <div className="space-y-2 mt-2">
                                  <div>
                                    <div className="flex items-center gap-1 text-gray-500 mb-1">
                                      <FileText className="w-3 h-3" />
                                      <span>예상 해시 (SOD):</span>
                                    </div>
                                    <code className="block font-mono bg-gray-200 dark:bg-gray-600 px-2 py-1 rounded break-all text-xs">{detail.expectedHash}</code>
                                  </div>
                                  <div>
                                    <div className="flex items-center gap-1 text-gray-500 mb-1">
                                      <Hash className="w-3 h-3" />
                                      <span>계산된 해시:</span>
                                    </div>
                                    <code className="block font-mono bg-gray-200 dark:bg-gray-600 px-2 py-1 rounded break-all text-xs">{detail.actualHash}</code>
                                  </div>
                                </div>
                                {detail.valid ? (
                                  <div className="mt-2 flex items-center gap-1 text-green-600 dark:text-green-400 font-medium">
                                    <CheckCircle className="w-3 h-3" />
                                    <span>해시 일치 - 데이터 무결성 검증 완료</span>
                                  </div>
                                ) : null}
                              </div>
                            ))}
                          </div>
                        );
                      })() : null}

                      {/* Step 7: CRL 검사 상세 정보 */}
                      {step.id === 7 && step.details && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1">
                          {step.details.description && (
                            <div className="text-gray-600 dark:text-gray-400">
                              {step.details.description}
                            </div>
                          )}
                          {step.details.detailedDescription && (
                            <div className="text-gray-500 dark:text-gray-500 text-xs">
                              {step.details.detailedDescription}
                            </div>
                          )}
                        </div>
                      )}

                      {/* Step 8: DSC 자동 등록 */}
                      {step.id === 8 && step.details && (
                        <div className="mt-2 p-3 bg-gray-50 dark:bg-gray-700/50 rounded-lg text-xs space-y-1.5">
                          {step.details.fingerprint && (
                            <div className="flex items-center gap-2">
                              <span className="text-gray-500 dark:text-gray-400 shrink-0">Fingerprint:</span>
                              <code className="font-mono text-gray-700 dark:text-gray-300 break-all">{step.details.fingerprint}</code>
                            </div>
                          )}
                          {step.details.certificateId && (
                            <div className="flex items-center gap-2">
                              <span className="text-gray-500 dark:text-gray-400 shrink-0">Certificate ID:</span>
                              <code className="font-mono text-gray-700 dark:text-gray-300">{step.details.certificateId}</code>
                            </div>
                          )}
                          {step.details.countryCode && (
                            <div className="flex items-center gap-2">
                              <span className="text-gray-500 dark:text-gray-400 shrink-0">Country:</span>
                              <span className="text-gray-700 dark:text-gray-300">{step.details.countryCode}</span>
                            </div>
                          )}
                          {step.details.newlyRegistered === false && (
                            <div className="text-gray-500 dark:text-gray-400 italic">
                              이미 Local PKD에 등록된 DSC 인증서입니다.
                            </div>
                          )}
                        </div>
                      )}
                    </div>
                  )}
                </div>
              ))}
            </div>
          </div>

        </div>
      </div>
      )}
    </div>
  );
}

export default PAVerify;
