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
  Lock,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { CertificatePreviewResult, CertificatePreviewItem, DeviationPreviewItem, CertificateUploadResponse } from '@/types';
import { cn } from '@/utils/cn';
import { TreeViewer, type TreeNode } from '@/components/TreeViewer';

type PageState = 'IDLE' | 'FILE_SELECTED' | 'PREVIEWING' | 'PREVIEW_READY' | 'PREVIEW_ERROR' | 'CONFIRMING' | 'COMPLETED' | 'FAILED';
type PreviewTab = 'certificates' | 'dl-structure' | 'crl';

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

// ============================================================================
// Tree builders
// ============================================================================

function buildPreviewCertificateTree(cert: CertificatePreviewItem): TreeNode[] {
  const children: TreeNode[] = [];
  children.push({ id: 'serial', name: 'Serial Number', value: cert.serialNumber, icon: 'hash', copyable: true });
  if (cert.signatureAlgorithm) {
    children.push({ id: 'signature', name: 'Signature Algorithm', value: cert.signatureAlgorithm, icon: 'shield' });
  }
  children.push({ id: 'issuer', name: 'Issuer', value: cert.issuerDn, icon: 'user', copyable: true });
  children.push({
    id: 'validity', name: 'Validity', icon: 'calendar',
    children: [
      { id: 'valid-from', name: 'Not Before', value: cert.notBefore },
      { id: 'valid-to', name: 'Not After', value: cert.notAfter + (cert.isExpired ? ' (Expired)' : '') },
    ],
  });
  children.push({ id: 'subject', name: 'Subject', value: cert.subjectDn, icon: 'user', copyable: true });
  if (cert.publicKeyAlgorithm) {
    const pkChildren: TreeNode[] = [{ id: 'pk-algo', name: 'Algorithm', value: cert.publicKeyAlgorithm }];
    if (cert.keySize > 0) pkChildren.push({ id: 'pk-size', name: 'Key Size', value: `${cert.keySize} bits` });
    children.push({ id: 'public-key', name: 'Public Key', children: pkChildren, icon: 'key' });
  }
  const propChildren: TreeNode[] = [
    { id: 'cert-type', name: 'Certificate Type', value: cert.certificateType },
    { id: 'country', name: 'Country', value: cert.countryCode },
    { id: 'self-signed', name: 'Self-Signed', value: cert.isSelfSigned ? 'Yes' : 'No' },
  ];
  if (cert.isLinkCertificate) propChildren.push({ id: 'link-cert', name: 'Link Certificate', value: 'Yes' });
  children.push({ id: 'properties', name: 'Properties', children: propChildren, icon: 'settings' });
  if (cert.fingerprintSha256) {
    children.push({ id: 'fingerprint', name: 'Fingerprint (SHA-256)', value: cert.fingerprintSha256, icon: 'hash', copyable: true });
  }
  return [{ id: 'certificate', name: 'Certificate', children, icon: 'file-text' }];
}

function buildDlStructureTree(preview: CertificatePreviewResult): TreeNode[] {
  const eContentType = preview.dlEContentType || '2.23.136.1.1.7';
  const eContentTypeName = eContentType === '2.23.136.1.1.7'
    ? 'id-icao-mrtd-security-deviationList' : eContentType;

  const deviationNodes: TreeNode[] = (preview.deviations || []).map((dev, i) => ({
    id: `sd-${i}`,
    name: `SignerDeviation [${i}]`,
    icon: 'alert-circle',
    children: [
      {
        id: `sd-${i}-id`, name: 'signerIdentifier', icon: 'user',
        children: [
          { id: `sd-${i}-issuer`, name: 'issuer', value: dev.certificateIssuerDn, copyable: true },
          { id: `sd-${i}-serial`, name: 'serialNumber', value: dev.certificateSerialNumber, icon: 'hash', copyable: true },
        ],
      },
      {
        id: `sd-${i}-defects`, name: 'defects',
        children: [{
          id: `sd-${i}-d0`, name: 'Defect',
          children: [
            ...(dev.defectDescription ? [{ id: `sd-${i}-d0-desc`, name: 'description', value: dev.defectDescription }] : []),
            { id: `sd-${i}-d0-oid`, name: 'defectType', value: `${dev.defectTypeOid} (${dev.defectCategory})` },
          ],
        }],
      },
    ],
  }));

  const certNodes: TreeNode[] = preview.certificates.map((cert, i) => ({
    id: `cms-cert-${i}`,
    name: `Certificate [${i}]`,
    value: `${cert.certificateType} — ${cert.subjectDn}`,
    icon: cert.certificateType === 'CSCA' ? 'shield' : 'file-text',
  }));

  return [{
    id: 'cms', name: 'CMS ContentInfo', icon: 'lock',
    children: [
      { id: 'cms-type', name: 'contentType', value: 'signedData (1.2.840.113549.1.7.2)' },
      {
        id: 'signed-data', name: 'SignedData', icon: 'shield',
        children: [
          { id: 'digest-algos', name: 'digestAlgorithms', value: preview.dlCmsDigestAlgorithm || '-' },
          {
            id: 'encap', name: 'encapContentInfo', icon: 'file-text',
            children: [
              { id: 'ectype', name: 'eContentType', value: `${eContentType} (${eContentTypeName})` },
              {
                id: 'econtent', name: 'eContent: DeviationList',
                children: [
                  { id: 'dl-version', name: 'version', value: String(preview.dlVersion ?? 0) },
                  { id: 'dl-hash', name: 'hashAlgorithm', value: preview.dlHashAlgorithm || '-' },
                  {
                    id: 'dl-deviations', name: `deviations (${(preview.deviations || []).length})`,
                    children: deviationNodes.length > 0 ? deviationNodes : [{ id: 'dl-no-dev', name: '(empty)', value: '' }],
                  },
                ],
              },
            ],
          },
          { id: 'cms-certs', name: `certificates (${preview.certificates.length})`, icon: 'key', children: certNodes },
          {
            id: 'signer-infos', name: 'signerInfos', icon: 'settings',
            children: [{
              id: 'si-0', name: 'SignerInfo [0]',
              children: [
                ...(preview.dlSignerDn ? [{ id: 'si-signer', name: 'signer', value: preview.dlSignerDn, icon: 'user' as const, copyable: true }] : []),
                { id: 'si-digest', name: 'digestAlgorithm', value: preview.dlCmsDigestAlgorithm || '-' },
                ...(preview.dlSigningTime ? [{ id: 'si-time', name: 'signingTime', value: preview.dlSigningTime, icon: 'calendar' as const }] : []),
                { id: 'si-sig-algo', name: 'signatureAlgorithm', value: preview.dlCmsSignatureAlgorithm || '-' },
                { id: 'si-sig-valid', name: 'signature', value: preview.dlSignatureValid ? 'Verified' : 'Not Verified' },
              ],
            }],
          },
        ],
      },
    ],
  }];
}

// ============================================================================
// CertificateCard component
// ============================================================================

function CertificateCard({ cert, index, deviations = [] }: { cert: CertificatePreviewItem; index: number; deviations?: DeviationPreviewItem[] }) {
  const [expanded, setExpanded] = useState(index === 0);
  const [activeTab, setActiveTab] = useState<'general' | 'details'>('general');

  const tabClass = (active: boolean) => cn(
    'px-5 py-2 text-xs font-medium border-b-2 transition-colors',
    active
      ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
      : 'border-transparent text-gray-500 dark:text-gray-400 hover:text-gray-800 dark:hover:text-gray-200'
  );

  return (
    <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
      <button
        onClick={() => setExpanded(!expanded)}
        className="w-full flex items-center justify-between px-4 py-2.5 hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
      >
        <div className="flex items-center gap-2.5 min-w-0">
          <div className="p-1 rounded bg-gradient-to-br from-blue-500 to-indigo-600 flex-shrink-0">
            <Shield className="w-3.5 h-3.5 text-white" />
          </div>
          <div className="flex items-center gap-1.5 flex-wrap min-w-0">
            <span className={cn(
              'inline-flex items-center px-1.5 py-0.5 text-[10px] font-bold rounded',
              cert.certificateType === 'CSCA' && 'bg-purple-100 dark:bg-purple-900/40 text-purple-700 dark:text-purple-300',
              cert.certificateType === 'DSC' && 'bg-blue-100 dark:bg-blue-900/40 text-blue-700 dark:text-blue-300',
              cert.certificateType === 'DSC_NC' && 'bg-orange-100 dark:bg-orange-900/40 text-orange-700 dark:text-orange-300',
              cert.certificateType === 'MLSC' && 'bg-teal-100 dark:bg-teal-900/40 text-teal-700 dark:text-teal-300',
              !['CSCA', 'DSC', 'DSC_NC', 'MLSC'].includes(cert.certificateType) && 'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300',
            )}>
              {cert.certificateType}
            </span>
            <span className="text-xs font-semibold text-gray-800 dark:text-gray-200">{cert.countryCode}</span>
            <span className="text-xs text-gray-500 dark:text-gray-400 truncate max-w-[280px]">{cert.subjectDn}</span>
            {cert.isExpired && (
              <span className="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded text-[10px] font-semibold bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400">
                <XCircle className="w-3 h-3" /> Expired
              </span>
            )}
            {deviations.length > 0 && (
              <span className="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded text-[10px] font-semibold bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-400">
                <AlertTriangle className="w-3 h-3" /> Deviation
              </span>
            )}
          </div>
        </div>
        <svg className={cn('w-4 h-4 text-gray-400 transition-transform flex-shrink-0 ml-2', expanded && 'rotate-180')} fill="none" viewBox="0 0 24 24" strokeWidth="2" stroke="currentColor">
          <path strokeLinecap="round" strokeLinejoin="round" d="M19.5 8.25l-7.5 7.5-7.5-7.5" />
        </svg>
      </button>

      {expanded && (
        <div className="border-t border-gray-200 dark:border-gray-700">
          <div className="border-b border-gray-100 dark:border-gray-700/50 bg-gray-50/50 dark:bg-gray-700/30 flex">
            <button onClick={() => setActiveTab('general')} className={tabClass(activeTab === 'general')}>General</button>
            <button onClick={() => setActiveTab('details')} className={tabClass(activeTab === 'details')}>Details</button>
          </div>

          {activeTab === 'general' && (
            <div className="p-4 space-y-3">
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">Subject</h4>
                  <div className="space-y-1">
                    <InfoRow label="DN" value={cert.subjectDn} mono />
                    <InfoRow label="Country" value={cert.countryCode} />
                    <InfoRow label="Serial" value={cert.serialNumber} mono />
                  </div>
                </div>
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">Issuer</h4>
                  <div className="space-y-1">
                    <InfoRow label="DN" value={cert.issuerDn} mono />
                    {cert.isSelfSigned && <InfoRow label="Type" value="Self-Signed" />}
                  </div>
                </div>
              </div>

              <div className="grid grid-cols-2 gap-4">
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">Validity</h4>
                  <div className="space-y-1">
                    <InfoRow label="From" value={cert.notBefore} />
                    <InfoRow label="To" value={cert.notAfter} className={cert.isExpired ? 'text-red-600 dark:text-red-400 font-semibold' : ''} />
                  </div>
                </div>
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">Algorithm</h4>
                  <div className="space-y-1">
                    <InfoRow label="Public Key" value={`${cert.publicKeyAlgorithm}${cert.keySize > 0 ? ` ${cert.keySize}` : ''}`} />
                    <InfoRow label="Signature" value={cert.signatureAlgorithm} />
                  </div>
                </div>
              </div>

              <div>
                <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">Fingerprint</h4>
                <InfoRow label="SHA-256" value={cert.fingerprintSha256 || '-'} mono />
              </div>

              {deviations.length > 0 && (
                <div>
                  <h4 className="text-xs font-semibold text-amber-600 dark:text-amber-400 uppercase tracking-wider mb-1.5 flex items-center gap-1">
                    <AlertTriangle className="w-3.5 h-3.5" /> Deviation ({deviations.length})
                  </h4>
                  {deviations.map((dev, di) => (
                    <div key={di} className="p-2 rounded bg-amber-50 dark:bg-amber-900/10 border border-amber-200 dark:border-amber-800 mt-1">
                      <p className="text-xs text-amber-900 dark:text-amber-200">{dev.defectDescription}</p>
                      <div className="flex items-center gap-2 mt-1 text-[10px] text-amber-700 dark:text-amber-400">
                        <span className="font-mono">{dev.defectTypeOid}</span>
                        <span className="px-1 py-0.5 rounded bg-amber-200/50 dark:bg-amber-800/40 font-semibold">{dev.defectCategory}</span>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          {activeTab === 'details' && (
            <div className="p-4">
              <TreeViewer data={buildPreviewCertificateTree(cert)} height="320px" />
            </div>
          )}
        </div>
      )}
    </div>
  );
}

function InfoRow({ label, value, mono, className }: { label: string; value: string; mono?: boolean; className?: string }) {
  return (
    <div className="flex gap-2 text-xs">
      <span className="text-gray-400 dark:text-gray-500 w-16 flex-shrink-0">{label}</span>
      <span className={cn('text-gray-900 dark:text-white break-all', mono && 'font-mono', className)}>{value}</span>
    </div>
  );
}

// ============================================================================
// Main page
// ============================================================================

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
  const [previewTab, setPreviewTab] = useState<PreviewTab>('certificates');

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
    setPreviewTab('certificates');
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
      const result = response.data;
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
    setPreviewTab('certificates');
    if (fileInputRef.current) fileInputRef.current.value = '';
  }, []);

  const isDl = previewResult?.fileFormat === 'DL';
  const hasCrl = !!previewResult?.crlInfo;

  const previewTabClass = (active: boolean) => cn(
    'px-4 py-2 text-xs font-medium rounded-t-lg transition-colors border-b-2',
    active
      ? 'border-indigo-600 text-indigo-600 dark:text-indigo-400 bg-white dark:bg-gray-800'
      : 'border-transparent text-gray-500 dark:text-gray-400 hover:text-gray-800 dark:hover:text-gray-200'
  );

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-5">
      {/* Header */}
      <div className="flex items-center gap-3">
        <div className="p-2.5 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-md">
          <Shield className="w-6 h-6 text-white" />
        </div>
        <div>
          <h1 className="text-xl font-bold text-gray-900 dark:text-white">인증서 업로드</h1>
          <p className="text-xs text-gray-500 dark:text-gray-400">
            개별 인증서 파일을 미리보기 후 DB + LDAP에 저장합니다 (PEM, DER, P7B, DL, CRL)
          </p>
        </div>
      </div>

      <div className="max-w-4xl space-y-4">
        {/* Step 1: File Selection — compact */}
        <div className="rounded-xl bg-white dark:bg-gray-800 shadow p-4">
          <div className="flex items-center gap-2.5 mb-3">
            <div className="w-6 h-6 rounded-full bg-indigo-100 dark:bg-indigo-900/30 flex items-center justify-center text-xs font-bold text-indigo-600 dark:text-indigo-400">1</div>
            <h2 className="text-sm font-bold text-gray-900 dark:text-white">파일 선택</h2>
          </div>

          <div
            className={cn(
              'relative border-2 border-dashed rounded-lg text-center cursor-pointer transition-all duration-200',
              selectedFile ? 'p-3 border-blue-300 bg-blue-50/50 dark:bg-blue-900/10 dark:border-blue-700' : 'p-5',
              isDragging && 'border-indigo-500 bg-indigo-50 dark:bg-indigo-900/20 scale-[1.01]',
              !selectedFile && !isDragging && 'border-gray-300 dark:border-gray-600 hover:border-indigo-400 hover:bg-gray-50 dark:hover:bg-gray-700/50',
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
              onChange={(e) => { const file = e.target.files?.[0]; if (file) handleFileSelect(file); }}
            />

            {selectedFile ? (
              <div className="flex items-center justify-center gap-3">
                <FileText className="w-7 h-7 text-blue-500 flex-shrink-0" />
                <div className="flex items-center gap-2">
                  <span className="px-1.5 py-0.5 text-[10px] font-bold rounded bg-indigo-100 text-indigo-700 dark:bg-indigo-900/50 dark:text-indigo-400">
                    {getFileTypeBadge(selectedFile.name)}
                  </span>
                  <span className="text-sm font-semibold text-gray-900 dark:text-white">{selectedFile.name}</span>
                  <span className="text-xs text-gray-400">({formatFileSize(selectedFile.size)})</span>
                </div>
                <span className="text-[10px] text-gray-400 ml-2">Click to change</span>
              </div>
            ) : (
              <div className="flex flex-col items-center gap-1.5">
                <CloudUpload className="w-8 h-8 text-gray-400" />
                <p className="text-sm text-gray-600 dark:text-gray-300">파일을 드래그하거나 클릭하여 선택</p>
                <p className="text-[10px] text-gray-400">.pem .crt .der .cer .p7b .dl .dvl .crl</p>
              </div>
            )}
          </div>

          {/* Actions */}
          <div className="mt-3 flex items-center justify-end gap-2">
            {pageState === 'FILE_SELECTED' && (
              <button
                onClick={handlePreview}
                className="flex items-center gap-1.5 px-4 py-2 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-sm font-medium transition-colors"
              >
                <Eye className="w-4 h-4" />
                미리보기
              </button>
            )}
            {pageState === 'PREVIEWING' && (
              <div className="flex items-center gap-2 text-indigo-600 dark:text-indigo-400">
                <Loader2 className="w-4 h-4 animate-spin" />
                <span className="text-sm">파싱 중...</span>
              </div>
            )}
          </div>
        </div>

        {/* Step 2: Preview Results — tabbed layout */}
        {(pageState === 'PREVIEW_READY' || pageState === 'CONFIRMING' || pageState === 'COMPLETED' || pageState === 'FAILED') && previewResult && (
          <div className="rounded-xl bg-white dark:bg-gray-800 shadow overflow-hidden">
            {/* Header bar */}
            <div className="px-4 pt-4 pb-0">
              <div className="flex items-center gap-2.5 mb-3">
                <div className="w-6 h-6 rounded-full bg-blue-100 dark:bg-blue-900/30 flex items-center justify-center text-xs font-bold text-blue-600 dark:text-blue-400">2</div>
                <h2 className="text-sm font-bold text-gray-900 dark:text-white">파싱 결과</h2>

                {/* Summary badges */}
                <div className="flex items-center gap-2 ml-auto">
                  {previewResult.isDuplicate && (
                    <span className="inline-flex items-center gap-1 px-2 py-0.5 text-[10px] font-semibold rounded-full bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400">
                      <AlertTriangle className="w-3 h-3" /> Duplicate
                    </span>
                  )}
                  {isDl && previewResult.dlSignatureValid !== undefined && (
                    <span className={cn(
                      'inline-flex items-center gap-1 px-2 py-0.5 text-[10px] font-semibold rounded-full',
                      previewResult.dlSignatureValid
                        ? 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400'
                        : 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400'
                    )}>
                      <Lock className="w-3 h-3" /> {previewResult.dlSignatureValid ? 'Sig Valid' : 'Sig Invalid'}
                    </span>
                  )}
                  <span className="px-2 py-0.5 text-[10px] font-bold rounded-full bg-indigo-100 text-indigo-700 dark:bg-indigo-900/30 dark:text-indigo-400">
                    {previewResult.fileFormat}
                  </span>
                </div>
              </div>

              {/* Tabs */}
              <div className="flex border-b border-gray-200 dark:border-gray-700 -mx-4 px-4">
                <button onClick={() => setPreviewTab('certificates')} className={previewTabClass(previewTab === 'certificates')}>
                  <span className="flex items-center gap-1.5">
                    <Key className="w-3.5 h-3.5" />
                    Certificates ({previewResult.certificates.length})
                  </span>
                </button>
                {isDl && (
                  <button onClick={() => setPreviewTab('dl-structure')} className={previewTabClass(previewTab === 'dl-structure')}>
                    <span className="flex items-center gap-1.5">
                      <FileText className="w-3.5 h-3.5" />
                      DL Structure
                    </span>
                  </button>
                )}
                {hasCrl && (
                  <button onClick={() => setPreviewTab('crl')} className={previewTabClass(previewTab === 'crl')}>
                    <span className="flex items-center gap-1.5">
                      <Shield className="w-3.5 h-3.5" />
                      CRL
                    </span>
                  </button>
                )}
              </div>
            </div>

            {/* Tab content */}
            <div className="p-4">
              {/* Certificates tab */}
              {previewTab === 'certificates' && (
                <div className="space-y-2">
                  {previewResult.certificates.map((cert, i) => {
                    const certDeviations = (previewResult.deviations || []).filter(
                      dev => dev.certificateIssuerDn === cert.issuerDn || dev.certificateIssuerDn === cert.subjectDn
                    );
                    return <CertificateCard key={i} cert={cert} index={i} deviations={certDeviations} />;
                  })}
                  {previewResult.certificates.length === 0 && (
                    <p className="text-sm text-gray-500 text-center py-6">인증서가 없습니다</p>
                  )}
                </div>
              )}

              {/* DL Structure tab */}
              {previewTab === 'dl-structure' && isDl && (
                <TreeViewer data={buildDlStructureTree(previewResult)} height="500px" />
              )}

              {/* CRL tab */}
              {previewTab === 'crl' && previewResult.crlInfo && (
                <div className="space-y-2">
                  <InfoRow label="Issuer" value={previewResult.crlInfo.issuerDn} mono />
                  <InfoRow label="Country" value={previewResult.crlInfo.countryCode} />
                  <InfoRow label="This Update" value={previewResult.crlInfo.thisUpdate} />
                  <InfoRow label="Next Update" value={previewResult.crlInfo.nextUpdate || '-'} />
                  <InfoRow label="CRL Number" value={previewResult.crlInfo.crlNumber || '-'} mono />
                  <InfoRow label="Revoked" value={String(previewResult.crlInfo.revokedCount)} />
                </div>
              )}
            </div>

            {/* Action bar */}
            <div className="px-4 pb-4">
              {pageState === 'PREVIEW_READY' && (
                <div className="flex items-center justify-end gap-2 pt-3 border-t border-gray-100 dark:border-gray-700">
                  <button
                    onClick={handleReset}
                    className="flex items-center gap-1.5 px-3.5 py-2 border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-sm transition-colors"
                  >
                    <RotateCcw className="w-3.5 h-3.5" /> 취소
                  </button>
                  <button
                    onClick={handleConfirm}
                    className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm font-medium transition-colors"
                  >
                    <Database className="w-3.5 h-3.5" /> DB + LDAP 저장
                  </button>
                </div>
              )}
              {pageState === 'CONFIRMING' && (
                <div className="flex items-center justify-center gap-2 pt-3 border-t border-gray-100 dark:border-gray-700 text-blue-600 dark:text-blue-400">
                  <Loader2 className="w-4 h-4 animate-spin" />
                  <span className="text-sm">저장 중...</span>
                </div>
              )}
            </div>
          </div>
        )}

        {/* Step 3: Result */}
        {pageState === 'COMPLETED' && uploadResult && (
          <div className="rounded-xl bg-white dark:bg-gray-800 shadow p-4">
            <div className="flex items-center gap-2.5 mb-3">
              <div className="w-6 h-6 rounded-full bg-green-100 dark:bg-green-900/30 flex items-center justify-center text-xs font-bold text-green-600 dark:text-green-400">3</div>
              <h2 className="text-sm font-bold text-gray-900 dark:text-white">저장 완료</h2>
            </div>

            <div className="flex items-start gap-3 p-3 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
              <CheckCircle className="w-5 h-5 text-green-500 flex-shrink-0 mt-0.5" />
              <div>
                <p className="text-sm font-semibold text-green-800 dark:text-green-300">처리 완료</p>
                <div className="mt-0.5 flex flex-wrap gap-2 text-xs text-green-700 dark:text-green-400">
                  {uploadResult.cscaCount ? <span>CSCA {uploadResult.cscaCount}</span> : null}
                  {uploadResult.dscCount ? <span>DSC {uploadResult.dscCount}</span> : null}
                  {uploadResult.dscNcCount ? <span>DSC_NC {uploadResult.dscNcCount}</span> : null}
                  {uploadResult.mlscCount ? <span>MLSC {uploadResult.mlscCount}</span> : null}
                  {uploadResult.crlCount ? <span>CRL {uploadResult.crlCount}</span> : null}
                </div>
                {uploadId && <p className="mt-0.5 text-[10px] text-gray-400">ID: {uploadId}</p>}
              </div>
            </div>

            <div className="flex items-center gap-2 mt-3">
              <button onClick={handleReset} className="flex items-center gap-1.5 px-3.5 py-1.5 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-xs font-medium transition-colors">
                <CloudUpload className="w-3.5 h-3.5" /> 새 파일
              </button>
              {uploadId && (
                <button onClick={() => navigate(`/upload/${uploadId}`)} className="flex items-center gap-1.5 px-3.5 py-1.5 border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-xs transition-colors">
                  <ExternalLink className="w-3.5 h-3.5" /> 상세보기
                </button>
              )}
            </div>
          </div>
        )}

        {/* Error */}
        {(pageState === 'PREVIEW_ERROR' || pageState === 'FAILED') && errorMessage && (
          <div className="rounded-xl bg-white dark:bg-gray-800 shadow p-4">
            <div className="flex items-start gap-2.5 p-3 rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
              <XCircle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
              <div>
                <p className="text-sm font-semibold text-red-800 dark:text-red-300">
                  {pageState === 'PREVIEW_ERROR' ? '파싱 실패' : '저장 실패'}
                </p>
                <p className="mt-0.5 text-xs text-red-700 dark:text-red-400">{errorMessage}</p>
              </div>
            </div>
            <div className="mt-3 flex gap-2">
              <button onClick={handleReset} className="flex items-center gap-1.5 px-3.5 py-1.5 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-xs font-medium transition-colors">
                <RotateCcw className="w-3.5 h-3.5" /> 다시 시도
              </button>
              {pageState === 'FAILED' && previewResult && (
                <button onClick={handleConfirm} className="flex items-center gap-1.5 px-3.5 py-1.5 border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-xs transition-colors">
                  <Database className="w-3.5 h-3.5" /> 저장 재시도
                </button>
              )}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
