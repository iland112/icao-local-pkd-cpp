import { useState, useRef, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import axios from 'axios';
import {
  CloudUpload,
  FileText,
  CheckCircle,
  XCircle,
  AlertTriangle,
  Loader2,
  Shield,
  Key,
  Eye,
  Database,
  RotateCcw,
  ExternalLink,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { CertificatePreviewResult, CertificatePreviewItem } from '@/types';

interface CertificateUploadResponse {
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
import { cn } from '@/utils/cn';
import { TreeViewer, type TreeNode } from '@/components/TreeViewer';

type PageState = 'IDLE' | 'FILE_SELECTED' | 'PREVIEWING' | 'PREVIEW_READY' | 'PREVIEW_ERROR' | 'CONFIRMING' | 'COMPLETED' | 'FAILED';

const CERT_EXTENSIONS = ['.pem', '.crt', '.der', '.cer', '.p7b', '.p7c', '.dl', '.dvl', '.crl'];

function formatFileSize(bytes: number): string {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function getFileTypeBadge(name: string): string {
  const lower = name.toLowerCase();
  if (lower.endsWith('.pem') || lower.endsWith('.crt')) return 'PEM';
  if (lower.endsWith('.der') || lower.endsWith('.cer')) return 'DER';
  if (lower.endsWith('.p7b') || lower.endsWith('.p7c')) return 'P7B';
  if (lower.endsWith('.dl') || lower.endsWith('.dvl')) return 'DL';
  if (lower.endsWith('.crl')) return 'CRL';
  return 'CERT';
}

function buildPreviewCertificateTree(cert: CertificatePreviewItem): TreeNode[] {
  const children: TreeNode[] = [];

  // Serial Number
  children.push({
    id: 'serial',
    name: 'Serial Number',
    value: cert.serialNumber,
    icon: 'hash',
    copyable: true,
  });

  // Signature
  if (cert.signatureAlgorithm) {
    children.push({
      id: 'signature',
      name: 'Signature Algorithm',
      value: cert.signatureAlgorithm,
      icon: 'shield',
    });
  }

  // Issuer
  children.push({
    id: 'issuer',
    name: 'Issuer',
    value: cert.issuerDn,
    icon: 'user',
    copyable: true,
  });

  // Validity
  children.push({
    id: 'validity',
    name: 'Validity',
    children: [
      { id: 'valid-from', name: 'Not Before', value: cert.notBefore },
      { id: 'valid-to', name: 'Not After', value: cert.notAfter + (cert.isExpired ? ' (Expired)' : '') },
    ],
    icon: 'calendar',
  });

  // Subject
  children.push({
    id: 'subject',
    name: 'Subject',
    value: cert.subjectDn,
    icon: 'user',
    copyable: true,
  });

  // Public Key
  if (cert.publicKeyAlgorithm) {
    const pkChildren: TreeNode[] = [];
    pkChildren.push({ id: 'pk-algo', name: 'Algorithm', value: cert.publicKeyAlgorithm });
    if (cert.keySize > 0) {
      pkChildren.push({ id: 'pk-size', name: 'Key Size', value: `${cert.keySize} bits` });
    }
    children.push({
      id: 'public-key',
      name: 'Public Key',
      children: pkChildren,
      icon: 'key',
    });
  }

  // Properties
  const propChildren: TreeNode[] = [];
  propChildren.push({ id: 'cert-type', name: 'Certificate Type', value: cert.certificateType });
  propChildren.push({ id: 'country', name: 'Country', value: cert.countryCode });
  propChildren.push({ id: 'self-signed', name: 'Self-Signed', value: cert.isSelfSigned ? 'Yes' : 'No' });
  if (cert.isLinkCertificate) {
    propChildren.push({ id: 'link-cert', name: 'Link Certificate', value: 'Yes' });
  }
  children.push({
    id: 'properties',
    name: 'Properties',
    children: propChildren,
    icon: 'settings',
  });

  // Fingerprint
  if (cert.fingerprintSha256) {
    children.push({
      id: 'fingerprint',
      name: 'Fingerprint (SHA-256)',
      value: cert.fingerprintSha256,
      icon: 'hash',
      copyable: true,
    });
  }

  return [{
    id: 'certificate',
    name: 'Certificate',
    children,
    icon: 'file-text',
  }];
}

function CertificateCard({ cert, index }: { cert: CertificatePreviewItem; index: number }) {
  const [expanded, setExpanded] = useState(index === 0);
  const [activeTab, setActiveTab] = useState<'general' | 'details'>('general');

  return (
    <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
      {/* Card Header */}
      <button
        onClick={() => setExpanded(!expanded)}
        className="w-full flex items-center justify-between px-4 py-3 hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
      >
        <div className="flex items-center gap-3 min-w-0">
          <div className="p-1.5 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600 flex-shrink-0">
            <Shield className="w-4 h-4 text-white" />
          </div>
          <div className="flex items-center gap-2 flex-wrap min-w-0">
            <span className={cn(
              'inline-flex items-center px-2 py-0.5 text-xs font-semibold rounded border',
              cert.certificateType === 'CSCA' && 'bg-purple-100 dark:bg-purple-900/40 text-purple-800 dark:text-purple-300 border-purple-200 dark:border-purple-700',
              cert.certificateType === 'DSC' && 'bg-blue-100 dark:bg-blue-900/40 text-blue-800 dark:text-blue-300 border-blue-200 dark:border-blue-700',
              cert.certificateType === 'DSC_NC' && 'bg-orange-100 dark:bg-orange-900/40 text-orange-800 dark:text-orange-300 border-orange-200 dark:border-orange-700',
              cert.certificateType === 'MLSC' && 'bg-teal-100 dark:bg-teal-900/40 text-teal-800 dark:text-teal-300 border-teal-200 dark:border-teal-700',
              !['CSCA', 'DSC', 'DSC_NC', 'MLSC'].includes(cert.certificateType) && 'bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300 border-gray-200 dark:border-gray-600',
            )}>
              {cert.certificateType}
            </span>
            {cert.isSelfSigned && (
              <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400">
                <CheckCircle className="w-3 h-3 mr-1" />
                Self-signed
              </span>
            )}
            {cert.isLinkCertificate && (
              <span className="inline-flex items-center px-2 py-0.5 text-xs font-semibold rounded bg-cyan-100 dark:bg-cyan-900/40 text-cyan-800 dark:text-cyan-300 border border-cyan-200 dark:border-cyan-700">
                Link Certificate
              </span>
            )}
            {cert.isExpired && (
              <span className="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
                <XCircle className="w-3 h-3 mr-1" />
                Expired
              </span>
            )}
            <span className="text-sm font-medium text-gray-900 dark:text-white truncate">
              {cert.countryCode}
            </span>
          </div>
        </div>
        <svg className={cn('w-4 h-4 text-gray-400 transition-transform flex-shrink-0', expanded && 'rotate-180')} fill="none" viewBox="0 0 24 24" strokeWidth="2" stroke="currentColor">
          <path strokeLinecap="round" strokeLinejoin="round" d="M19.5 8.25l-7.5 7.5-7.5-7.5" />
        </svg>
      </button>

      {/* Card Detail Content */}
      {expanded && (
        <div className="border-t border-gray-200 dark:border-gray-700">
          {/* Tabs */}
          <div className="border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-700/50">
            <div className="flex">
              <button
                onClick={() => setActiveTab('general')}
                className={cn(
                  'px-6 py-2.5 text-xs font-medium border-b-2 transition-colors',
                  activeTab === 'general'
                    ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                    : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
                )}
              >
                General
              </button>
              <button
                onClick={() => setActiveTab('details')}
                className={cn(
                  'px-6 py-2.5 text-xs font-medium border-b-2 transition-colors',
                  activeTab === 'details'
                    ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                    : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
                )}
              >
                Details
              </button>
            </div>
          </div>

          {/* General Tab */}
          {activeTab === 'general' && (
            <div className="px-4 pb-4">
              <div className="space-y-4 pt-4">
                {/* Issued To / Issued By */}
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Issued To (Subject)</h3>
                    <div className="space-y-2">
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Subject DN:</span>
                        <span className="text-xs text-gray-900 dark:text-white break-all">{cert.subjectDn}</span>
                      </div>
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Country:</span>
                        <span className="text-xs text-gray-900 dark:text-white">{cert.countryCode}</span>
                      </div>
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Serial:</span>
                        <span className="text-xs text-gray-900 dark:text-white font-mono break-all">{cert.serialNumber}</span>
                      </div>
                    </div>
                  </div>

                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Issued By (Issuer)</h3>
                    <div className="space-y-2">
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Issuer DN:</span>
                        <span className="text-xs text-gray-900 dark:text-white break-all">{cert.issuerDn}</span>
                      </div>
                    </div>
                  </div>
                </div>

                {/* Validity / Technical Details */}
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Validity</h3>
                    <div className="space-y-2">
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Issued on:</span>
                        <span className="text-xs text-gray-900 dark:text-white">{cert.notBefore}</span>
                      </div>
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Expires on:</span>
                        <span className={cn('text-xs', cert.isExpired ? 'text-red-600 dark:text-red-400 font-semibold' : 'text-gray-900 dark:text-white')}>
                          {cert.notAfter} {cert.isExpired && '(Expired)'}
                        </span>
                      </div>
                    </div>
                  </div>

                  <div>
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Technical Details</h3>
                    <div className="space-y-2">
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Public Key:</span>
                        <span className="text-xs text-gray-900 dark:text-white">
                          {cert.publicKeyAlgorithm}{cert.keySize > 0 ? ` ${cert.keySize}` : ''}
                        </span>
                      </div>
                      <div className="grid grid-cols-[80px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Signature:</span>
                        <span className="text-xs text-gray-900 dark:text-white">{cert.signatureAlgorithm}</span>
                      </div>
                    </div>
                  </div>
                </div>

                {/* Fingerprint */}
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Fingerprint</h3>
                  <div className="grid grid-cols-[80px_1fr] gap-2">
                    <span className="text-xs text-gray-600 dark:text-gray-400">SHA-256:</span>
                    <span className="text-xs text-gray-900 dark:text-white font-mono break-all">{cert.fingerprintSha256 || '-'}</span>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* Details Tab - Certificate Fields Tree */}
          {activeTab === 'details' && (
            <div className="px-4 pb-4 pt-4">
              <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                인증서 필드
              </h3>
              <TreeViewer
                data={buildPreviewCertificateTree(cert)}
                height="350px"
              />
            </div>
          )}
        </div>
      )}
    </div>
  );
}

export default function CertificateUpload() {
  const navigate = useNavigate();
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [pageState, setPageState] = useState<PageState>('IDLE');
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [previewResult, setPreviewResult] = useState<CertificatePreviewResult | null>(null);
  const [uploadResult, setUploadResult] = useState<CertificateUploadResponse | null>(null);
  const [uploadId, setUploadId] = useState<string>('');
  const [errorMessage, setErrorMessage] = useState<string>('');

  const isValidFile = useCallback((file: File) => {
    const name = file.name.toLowerCase();
    return CERT_EXTENSIONS.some(ext => name.endsWith(ext));
  }, []);

  const handleFileSelect = useCallback((file: File) => {
    if (!isValidFile(file)) {
      setErrorMessage('지원하지 않는 파일 형식입니다. LDIF/Master List은 파일 업로드 페이지를 이용하세요.');
      return;
    }
    setSelectedFile(file);
    setPageState('FILE_SELECTED');
    setPreviewResult(null);
    setUploadResult(null);
    setUploadId('');
    setErrorMessage('');
  }, [isValidFile]);

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
    const file = e.dataTransfer.files[0];
    if (file) handleFileSelect(file);
  }, [handleFileSelect]);

  const handlePreview = useCallback(async () => {
    if (!selectedFile) return;

    setPageState('PREVIEWING');
    setErrorMessage('');

    try {
      const response = await uploadApi.previewCertificate(selectedFile);
      const data = response.data;

      if (data.success) {
        setPreviewResult(data);
        setPageState('PREVIEW_READY');
      } else {
        setErrorMessage(data.errorMessage || '파일 파싱에 실패했습니다.');
        setPageState('PREVIEW_ERROR');
      }
    } catch (error: unknown) {
      if (axios.isAxiosError(error) && error.response?.data) {
        setErrorMessage(error.response.data.errorMessage || error.response.data.message || '파일 파싱에 실패했습니다.');
      } else {
        setErrorMessage('서버 연결에 실패했습니다.');
      }
      setPageState('PREVIEW_ERROR');
    }
  }, [selectedFile]);

  const handleConfirm = useCallback(async () => {
    if (!selectedFile) return;

    setPageState('CONFIRMING');
    setErrorMessage('');

    try {
      const response = await uploadApi.uploadCertificate(selectedFile);
      const result = response.data as unknown as CertificateUploadResponse;

      if (result.success) {
        setUploadResult(result);
        setUploadId(result.uploadId);
        setPageState('COMPLETED');
      } else {
        throw new Error(result.errorMessage || result.message || '업로드에 실패했습니다.');
      }
    } catch (error: unknown) {
      if (axios.isAxiosError(error) && error.response?.status === 409) {
        setErrorMessage('이미 업로드된 파일입니다 (중복 파일 감지).');
      } else if (axios.isAxiosError(error) && error.response?.data) {
        setErrorMessage(error.response.data.message || error.response.data.error || '업로드에 실패했습니다.');
      } else if (error instanceof Error) {
        setErrorMessage(error.message);
      } else {
        setErrorMessage('서버 연결에 실패했습니다.');
      }
      setPageState('FAILED');
    }
  }, [selectedFile]);

  const handleReset = useCallback(() => {
    setPageState('IDLE');
    setSelectedFile(null);
    setPreviewResult(null);
    setUploadResult(null);
    setUploadId('');
    setErrorMessage('');
    if (fileInputRef.current) fileInputRef.current.value = '';
  }, []);

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center gap-4">
        <div className="p-3 rounded-2xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-lg">
          <Shield className="w-7 h-7 text-white" />
        </div>
        <div>
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
            인증서 업로드
          </h1>
          <p className="text-sm text-gray-500 dark:text-gray-400">
            개별 인증서 파일을 미리보기 후 DB + LDAP에 저장합니다 (PEM, DER, P7B, DL, CRL)
          </p>
        </div>
      </div>

      <div className="max-w-4xl space-y-6">
        {/* Step 1: File Selection */}
        <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-6">
          <div className="flex items-center gap-3 mb-4">
            <div className="w-8 h-8 rounded-full bg-indigo-100 dark:bg-indigo-900/30 flex items-center justify-center text-sm font-bold text-indigo-600 dark:text-indigo-400">1</div>
            <h2 className="text-lg font-bold text-gray-900 dark:text-white">파일 선택</h2>
          </div>

          {/* Drop Zone */}
          <div
            className={cn(
              'relative border-2 border-dashed rounded-xl p-8 text-center cursor-pointer transition-all duration-300',
              isDragging
                ? 'border-indigo-500 bg-indigo-50 dark:bg-indigo-900/20 scale-[1.02]'
                : selectedFile
                ? 'border-blue-400 bg-blue-50 dark:bg-blue-900/20'
                : 'border-gray-300 dark:border-gray-600 hover:border-indigo-400 hover:bg-gray-50 dark:hover:bg-gray-700/50'
            )}
            onDragOver={(e) => { e.preventDefault(); setIsDragging(true); }}
            onDragLeave={(e) => { e.preventDefault(); setIsDragging(false); }}
            onDrop={handleDrop}
            onClick={() => fileInputRef.current?.click()}
          >
            <input
              ref={fileInputRef}
              type="file"
              accept={CERT_EXTENSIONS.join(',')}
              className="hidden"
              onChange={(e) => {
                const file = e.target.files?.[0];
                if (file) handleFileSelect(file);
              }}
            />

            {selectedFile ? (
              <div className="space-y-2">
                <FileText className="w-10 h-10 text-blue-500 mx-auto" />
                <div className="flex items-center justify-center gap-2">
                  <span className="px-2 py-0.5 text-xs font-bold rounded bg-indigo-100 text-indigo-700 dark:bg-indigo-900/50 dark:text-indigo-400">
                    {getFileTypeBadge(selectedFile.name)}
                  </span>
                  <p className="text-sm font-semibold text-gray-900 dark:text-white">{selectedFile.name}</p>
                  <span className="text-xs text-gray-500">({formatFileSize(selectedFile.size)})</span>
                </div>
                <p className="text-xs text-gray-400">클릭하여 다른 파일 선택</p>
              </div>
            ) : (
              <div className="space-y-3">
                <CloudUpload className="w-12 h-12 text-gray-400 mx-auto" />
                <div>
                  <p className="text-sm font-medium text-gray-700 dark:text-gray-300">
                    파일을 드래그하거나 클릭하여 선택
                  </p>
                  <p className="text-xs text-gray-400 mt-1">
                    지원 형식: .pem .crt .der .cer .p7b .p7c .dl .dvl .crl (최대 10MB)
                  </p>
                </div>
              </div>
            )}
          </div>

          {/* Preview Button */}
          {pageState === 'FILE_SELECTED' && (
            <div className="mt-4 flex justify-end">
              <button
                onClick={handlePreview}
                className="flex items-center gap-2 px-5 py-2.5 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg font-medium transition-colors"
              >
                <Eye className="w-4 h-4" />
                미리보기
              </button>
            </div>
          )}

          {/* Previewing spinner */}
          {pageState === 'PREVIEWING' && (
            <div className="mt-4 flex items-center justify-center gap-2 text-indigo-600 dark:text-indigo-400">
              <Loader2 className="w-5 h-5 animate-spin" />
              <span className="text-sm font-medium">파일 파싱 중...</span>
            </div>
          )}
        </div>

        {/* Step 2: Preview Results */}
        {(pageState === 'PREVIEW_READY' || pageState === 'CONFIRMING' || pageState === 'COMPLETED' || pageState === 'FAILED') && previewResult && (
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-6">
            <div className="flex items-center gap-3 mb-4">
              <div className="w-8 h-8 rounded-full bg-blue-100 dark:bg-blue-900/30 flex items-center justify-center text-sm font-bold text-blue-600 dark:text-blue-400">2</div>
              <h2 className="text-lg font-bold text-gray-900 dark:text-white">파싱 결과 미리보기</h2>
              <span className="ml-auto px-2.5 py-1 text-xs font-semibold rounded-full bg-indigo-100 text-indigo-700 dark:bg-indigo-900/30 dark:text-indigo-400">
                {previewResult.fileFormat}
              </span>
            </div>

            {/* Duplicate Warning */}
            {previewResult.isDuplicate && (
              <div className="mb-4 flex items-center gap-2 p-3 rounded-lg bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800">
                <AlertTriangle className="w-5 h-5 text-amber-500 flex-shrink-0" />
                <p className="text-sm text-amber-700 dark:text-amber-400">
                  이 파일은 이미 업로드된 적이 있습니다. 저장 시 중복으로 처리됩니다.
                </p>
              </div>
            )}

            {/* Summary */}
            <div className="mb-4 flex items-center gap-4 text-sm text-gray-600 dark:text-gray-400">
              {previewResult.certificates.length > 0 && (
                <span className="flex items-center gap-1.5">
                  <Key className="w-4 h-4" />
                  인증서 {previewResult.certificates.length}건
                </span>
              )}
              {previewResult.deviations && previewResult.deviations.length > 0 && (
                <span className="flex items-center gap-1.5">
                  <AlertTriangle className="w-4 h-4" />
                  Deviation {previewResult.deviations.length}건
                </span>
              )}
              {previewResult.crlInfo && (
                <span className="flex items-center gap-1.5">
                  <Shield className="w-4 h-4" />
                  CRL (취소 인증서 {previewResult.crlInfo.revokedCount}건)
                </span>
              )}
            </div>

            {/* Certificate List */}
            {previewResult.certificates.length > 0 && (
              <div className="space-y-2 mb-4">
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">인증서 목록</h3>
                {previewResult.certificates.map((cert, i) => (
                  <CertificateCard key={i} cert={cert} index={i} />
                ))}
              </div>
            )}

            {/* CRL Info */}
            {previewResult.crlInfo && (
              <div className="mb-4 p-4 rounded-lg bg-gray-50 dark:bg-gray-900">
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2">CRL 정보</h3>
                <div className="grid grid-cols-2 gap-x-6 gap-y-1 text-sm">
                  <div>
                    <span className="text-gray-500 dark:text-gray-400">Issuer:</span>
                    <p className="text-gray-900 dark:text-white text-xs break-all">{previewResult.crlInfo.issuerDn}</p>
                  </div>
                  <div>
                    <span className="text-gray-500 dark:text-gray-400">Country:</span>
                    <p className="text-gray-900 dark:text-white text-xs">{previewResult.crlInfo.countryCode}</p>
                  </div>
                  <div>
                    <span className="text-gray-500 dark:text-gray-400">This Update:</span>
                    <p className="text-gray-900 dark:text-white text-xs">{previewResult.crlInfo.thisUpdate}</p>
                  </div>
                  <div>
                    <span className="text-gray-500 dark:text-gray-400">Next Update:</span>
                    <p className="text-gray-900 dark:text-white text-xs">{previewResult.crlInfo.nextUpdate || '-'}</p>
                  </div>
                  <div>
                    <span className="text-gray-500 dark:text-gray-400">CRL Number:</span>
                    <p className="text-gray-900 dark:text-white text-xs font-mono">{previewResult.crlInfo.crlNumber || '-'}</p>
                  </div>
                  <div>
                    <span className="text-gray-500 dark:text-gray-400">Revoked Certificates:</span>
                    <p className="text-gray-900 dark:text-white text-xs">{previewResult.crlInfo.revokedCount}</p>
                  </div>
                </div>
              </div>
            )}

            {/* DL Deviation Info */}
            {previewResult.deviations && previewResult.deviations.length > 0 && (
              <div className="mb-4 p-4 rounded-lg bg-amber-50 dark:bg-amber-900/10 border border-amber-200 dark:border-amber-800">
                <div className="flex items-center gap-2 mb-2">
                  <h3 className="text-sm font-semibold text-amber-800 dark:text-amber-300">Deviation List</h3>
                  <span className="text-xs text-amber-600 dark:text-amber-400">
                    (Country: {previewResult.dlIssuerCountry}, Version: {previewResult.dlVersion},
                    Hash: {previewResult.dlHashAlgorithm},
                    Signature: {previewResult.dlSignatureValid ? 'Valid' : 'Invalid'})
                  </span>
                </div>
                {previewResult.deviations.map((dev, i) => (
                  <div key={i} className="p-3 rounded bg-white dark:bg-gray-800 border border-amber-100 dark:border-amber-900 mt-2">
                    <div className="grid grid-cols-2 gap-x-4 gap-y-1 text-xs">
                      <div>
                        <span className="text-gray-500">Issuer DN:</span>
                        <p className="text-gray-900 dark:text-white break-all">{dev.certificateIssuerDn}</p>
                      </div>
                      <div>
                        <span className="text-gray-500">Serial:</span>
                        <p className="text-gray-900 dark:text-white font-mono">{dev.certificateSerialNumber}</p>
                      </div>
                      <div className="col-span-2">
                        <span className="text-gray-500">Description:</span>
                        <p className="text-gray-900 dark:text-white">{dev.defectDescription}</p>
                      </div>
                      <div>
                        <span className="text-gray-500">Defect OID:</span>
                        <p className="text-gray-900 dark:text-white font-mono">{dev.defectTypeOid}</p>
                      </div>
                      <div>
                        <span className="text-gray-500">Category:</span>
                        <p className="text-gray-900 dark:text-white">{dev.defectCategory}</p>
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            )}

            {/* Action Buttons */}
            {pageState === 'PREVIEW_READY' && (
              <div className="flex items-center justify-end gap-3 pt-4 border-t border-gray-200 dark:border-gray-700">
                <button
                  onClick={handleReset}
                  className="flex items-center gap-2 px-4 py-2.5 border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg font-medium transition-colors"
                >
                  <RotateCcw className="w-4 h-4" />
                  취소
                </button>
                <button
                  onClick={handleConfirm}
                  className="flex items-center gap-2 px-5 py-2.5 bg-blue-600 hover:bg-blue-700 text-white rounded-lg font-medium transition-colors"
                >
                  <Database className="w-4 h-4" />
                  DB + LDAP 저장
                </button>
              </div>
            )}

            {/* Confirming spinner */}
            {pageState === 'CONFIRMING' && (
              <div className="flex items-center justify-center gap-2 pt-4 border-t border-gray-200 dark:border-gray-700 text-blue-600 dark:text-blue-400">
                <Loader2 className="w-5 h-5 animate-spin" />
                <span className="text-sm font-medium">DB + LDAP 저장 중...</span>
              </div>
            )}
          </div>
        )}

        {/* Step 3: Result */}
        {pageState === 'COMPLETED' && uploadResult && (
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-6">
            <div className="flex items-center gap-3 mb-4">
              <div className="w-8 h-8 rounded-full bg-green-100 dark:bg-green-900/30 flex items-center justify-center text-sm font-bold text-green-600 dark:text-green-400">3</div>
              <h2 className="text-lg font-bold text-gray-900 dark:text-white">저장 완료</h2>
            </div>

            <div className="flex items-start gap-3 p-4 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
              <CheckCircle className="w-6 h-6 text-green-500 flex-shrink-0 mt-0.5" />
              <div>
                <p className="text-sm font-semibold text-green-800 dark:text-green-300">
                  인증서 파일 처리 완료
                </p>
                <div className="mt-1 flex flex-wrap gap-2 text-xs text-green-700 dark:text-green-400">
                  {uploadResult.cscaCount ? <span>CSCA {uploadResult.cscaCount}건</span> : null}
                  {uploadResult.dscCount ? <span>DSC {uploadResult.dscCount}건</span> : null}
                  {uploadResult.dscNcCount ? <span>DSC_NC {uploadResult.dscNcCount}건</span> : null}
                  {uploadResult.mlscCount ? <span>MLSC {uploadResult.mlscCount}건</span> : null}
                  {uploadResult.crlCount ? <span>CRL {uploadResult.crlCount}건</span> : null}
                </div>
                {uploadId && (
                  <p className="mt-1 text-xs text-gray-500 dark:text-gray-400">
                    Upload ID: {uploadId}
                  </p>
                )}
              </div>
            </div>

            <div className="flex items-center gap-3 mt-4">
              <button
                onClick={handleReset}
                className="flex items-center gap-2 px-4 py-2 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-sm font-medium transition-colors"
              >
                <CloudUpload className="w-4 h-4" />
                새 파일 업로드
              </button>
              {uploadId && (
                <button
                  onClick={() => navigate(`/upload/${uploadId}`)}
                  className="flex items-center gap-2 px-4 py-2 border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-sm font-medium transition-colors"
                >
                  <ExternalLink className="w-4 h-4" />
                  상세보기
                </button>
              )}
            </div>
          </div>
        )}

        {/* Error States */}
        {(pageState === 'PREVIEW_ERROR' || pageState === 'FAILED') && errorMessage && (
          <div className="rounded-2xl bg-white dark:bg-gray-800 shadow-lg p-6">
            <div className="flex items-start gap-3 p-4 rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
              <XCircle className="w-6 h-6 text-red-500 flex-shrink-0 mt-0.5" />
              <div>
                <p className="text-sm font-semibold text-red-800 dark:text-red-300">
                  {pageState === 'PREVIEW_ERROR' ? '파싱 실패' : '저장 실패'}
                </p>
                <p className="mt-1 text-xs text-red-700 dark:text-red-400">{errorMessage}</p>
              </div>
            </div>
            <div className="mt-4 flex gap-3">
              <button
                onClick={handleReset}
                className="flex items-center gap-2 px-4 py-2 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-sm font-medium transition-colors"
              >
                <RotateCcw className="w-4 h-4" />
                다시 시도
              </button>
              {pageState === 'FAILED' && previewResult && (
                <button
                  onClick={handleConfirm}
                  className="flex items-center gap-2 px-4 py-2 border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-sm font-medium transition-colors"
                >
                  <Database className="w-4 h-4" />
                  저장 재시도
                </button>
              )}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
