import { useTranslation } from 'react-i18next';
import { useState, useRef, useCallback, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import axios from 'axios';
import {
  CloudUpload,
  FileText,
  CheckCircle,
  CheckCircle2,
  XCircle,
  AlertTriangle,
  Loader2,
  Shield,
  Key,
  Eye,
  Database,
  RotateCcw,
  Lock,
  Globe,
  History,
} from 'lucide-react';
import { uploadApi } from '@/services/api';
import type { CertificatePreviewResult, CertificatePreviewItem, DeviationPreviewItem, CertificateUploadResponse } from '@/types';
import { cn } from '@/utils/cn';
import { TreeViewer, type TreeNode } from '@/components/TreeViewer';
import { Doc9303ComplianceChecklist } from '@/components/Doc9303ComplianceChecklist';
import { Dialog } from '@/components/common/Dialog';

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
  const { t } = useTranslation(['upload', 'common', 'certificate']);
  const [expanded, setExpanded] = useState(index === 0);
  const [activeTab, setActiveTab] = useState<'general' | 'details' | 'doc9303'>('general');

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
            <span className="text-xs text-gray-500 dark:text-gray-400 truncate max-w-[280px]" title={cert.subjectDn}>{cert.subjectDn}</span>
            {cert.isExpired && (
              <span className="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded text-[10px] font-semibold bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400">
                <XCircle className="w-3 h-3" /> {t('upload:certUpload.card.expired')}
              </span>
            )}
            {deviations.length > 0 && (
              <span className="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded text-[10px] font-semibold bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-400">
                <AlertTriangle className="w-3 h-3" /> {t('upload:certUpload.card.deviation')}
              </span>
            )}
            {cert.doc9303Checklist && (
              <span className={cn(
                'inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded text-[10px] font-semibold',
                cert.doc9303Checklist.overallStatus === 'CONFORMANT' && 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400',
                cert.doc9303Checklist.overallStatus === 'WARNING' && 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-700 dark:text-yellow-400',
                cert.doc9303Checklist.overallStatus === 'NON_CONFORMANT' && 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400',
              )}>
                {cert.doc9303Checklist.overallStatus === 'CONFORMANT' ? <CheckCircle className="w-3 h-3" /> : <AlertTriangle className="w-3 h-3" />}
                {cert.doc9303Checklist.overallStatus === 'CONFORMANT' ? '9303' : `9303 (${cert.doc9303Checklist.failCount})`}
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
            <button onClick={() => setActiveTab('general')} className={tabClass(activeTab === 'general')}>{t('upload:certUpload.card.general')}</button>
            <button onClick={() => setActiveTab('details')} className={tabClass(activeTab === 'details')}>{t('upload:certUpload.card.details')}</button>
            {cert.doc9303Checklist && (
              <button onClick={() => setActiveTab('doc9303')} className={tabClass(activeTab === 'doc9303')}>{t('upload:certUpload.card.doc9303')}</button>
            )}
          </div>

          {activeTab === 'general' && (
            <div className="p-4 space-y-3">
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">{t('upload:certUpload.card.subject')}</h4>
                  <div className="space-y-1">
                    <InfoRow label={t('upload:certUpload.card.dn')} value={cert.subjectDn} mono />
                    <InfoRow label={t('upload:certUpload.card.country')} value={cert.countryCode} />
                    <InfoRow label={t('upload:certUpload.card.serial')} value={cert.serialNumber} mono />
                  </div>
                </div>
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">{t('upload:certUpload.card.issuer')}</h4>
                  <div className="space-y-1">
                    <InfoRow label={t('upload:certUpload.card.dn')} value={cert.issuerDn} mono />
                    {cert.isSelfSigned && <InfoRow label={t('upload:certUpload.card.type')} value={t('upload:certUpload.card.selfSigned')} />}
                  </div>
                </div>
              </div>

              <div className="grid grid-cols-2 gap-4">
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">{t('upload:certUpload.card.validity')}</h4>
                  <div className="space-y-1">
                    <InfoRow label={t('upload:certUpload.card.from')} value={cert.notBefore} />
                    <InfoRow label={t('upload:certUpload.card.to')} value={cert.notAfter} className={cert.isExpired ? 'text-red-600 dark:text-red-400 font-semibold' : ''} />
                  </div>
                </div>
                <div>
                  <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">{t('upload:certUpload.card.algorithm')}</h4>
                  <div className="space-y-1">
                    <InfoRow label={t('upload:certUpload.card.publicKey')} value={`${cert.publicKeyAlgorithm}${cert.keySize > 0 ? ` ${cert.keySize}` : ''}`} />
                    <InfoRow label={t('upload:certUpload.card.signature')} value={cert.signatureAlgorithm} />
                  </div>
                </div>
              </div>

              <div>
                <h4 className="text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider mb-1.5">{t('upload:certUpload.card.fingerprint')}</h4>
                <InfoRow label="SHA-256" value={cert.fingerprintSha256 || '-'} mono />
              </div>

              {deviations.length > 0 && (
                <div>
                  <h4 className="text-xs font-semibold text-amber-600 dark:text-amber-400 uppercase tracking-wider mb-1.5 flex items-center gap-1">
                    <AlertTriangle className="w-3.5 h-3.5" /> {t('upload:certUpload.card.deviationCount', { num: deviations.length })}
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

          {activeTab === 'doc9303' && cert.doc9303Checklist && (
            <div className="p-4">
              <Doc9303ComplianceChecklist checklist={cert.doc9303Checklist} />
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
  const { t } = useTranslation(['upload', 'common', 'certificate']);
  const navigate = useNavigate();
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [cscaCount, setCscaCount] = useState<number | null>(null);

  useEffect(() => {
    uploadApi.getStatistics()
      .then(res => setCscaCount(res.data.cscaCount ?? 0))
      .catch(() => {});
  }, []);

  const [pageState, setPageState] = useState<PageState>('IDLE');
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [previewResult, setPreviewResult] = useState<CertificatePreviewResult | null>(null);
  const [uploadResult, setUploadResult] = useState<CertificateUploadResponse | null>(null);
  const [uploadId, setUploadId] = useState<string>('');
  const [errorMessage, setErrorMessage] = useState<string>('');
  const [previewTab, setPreviewTab] = useState<PreviewTab>('certificates');
  const [showResultDialog, setShowResultDialog] = useState(false);

  const isValidFile = useCallback((file: File) => {
    const name = file.name.toLowerCase();
    return CERT_EXTENSIONS.some(ext => name.endsWith(ext));
  }, []);

  const handleFileSelect = useCallback((file: File) => {
    if (!isValidFile(file)) {
      setErrorMessage(`${t('upload:certUpload.unsupportedFormat')}. ${t('upload:certUpload.useLdifUploadPage')}`);
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
        setErrorMessage(data.errorMessage || t('upload:certUpload.previewError'));
        setPageState('PREVIEW_ERROR');
      }
    } catch (error: unknown) {
      if (axios.isAxiosError(error) && error.response?.data) {
        setErrorMessage(error.response.data.errorMessage || error.response.data.message || t('upload:certUpload.previewError'));
      } else {
        setErrorMessage(t('upload:certUpload.serverConnectionFailed'));
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
        setShowResultDialog(true);
      } else {
        throw new Error(result.errorMessage || result.message || t('upload:certUpload.uploadFailed'));
      }
    } catch (error: unknown) {
      if (axios.isAxiosError(error) && error.response?.status === 409) {
        setErrorMessage(t('upload:certUpload.duplicateFileDetected'));
      } else if (axios.isAxiosError(error) && error.response?.data) {
        setErrorMessage(error.response.data.message || error.response.data.error || t('upload:certUpload.uploadFailed'));
      } else if (error instanceof Error) {
        setErrorMessage(error.message);
      } else {
        setErrorMessage(t('upload:certUpload.serverConnectionFailed'));
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
          <h1 className="text-xl font-bold text-gray-900 dark:text-white">{t('upload:certUpload.title')}</h1>
          <p className="text-xs text-gray-500 dark:text-gray-400">
            {t('upload:certUpload.subtitle')}
          </p>
        </div>
      </div>

      {/* CSCA Certificate Status Banner */}
      {cscaCount !== null && (
        cscaCount === 0 ? (
          <div className="max-w-4xl mb-4 flex items-start gap-3 px-4 py-3 rounded-xl bg-gradient-to-r from-red-50 to-orange-50 dark:from-red-900/20 dark:to-orange-900/20 border border-red-200 dark:border-red-800">
            <AlertTriangle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
            <div>
              <p className="text-sm font-semibold text-red-800 dark:text-red-300">{t('upload:certUpload.cscaNotRegisteredTitle')}</p>
              <p className="text-xs text-red-600 dark:text-red-400 mt-0.5">
                {t('upload:certUpload.cscaTrustChainHint')}
              </p>
            </div>
          </div>
        ) : (
          <div className="max-w-4xl mb-4 flex items-center gap-2 px-4 py-2.5 rounded-xl bg-gradient-to-r from-green-50 to-emerald-50 dark:from-green-900/20 dark:to-emerald-900/20 border border-green-200 dark:border-green-800">
            <CheckCircle2 className="w-4 h-4 text-green-500 flex-shrink-0" />
            <p className="text-xs text-green-700 dark:text-green-400">
              {t('upload:certUpload.cscaRegisteredCount', { num: cscaCount.toLocaleString() })}
            </p>
          </div>
        )
      )}

      <div className="max-w-4xl space-y-4">
        {/* Step 1: File Selection — compact */}
        <div className="rounded-xl bg-white dark:bg-gray-800 shadow p-4">
          <div className="flex items-center gap-2.5 mb-3">
            <div className="w-6 h-6 rounded-full bg-indigo-100 dark:bg-indigo-900/30 flex items-center justify-center text-xs font-bold text-indigo-600 dark:text-indigo-400">1</div>
            <h2 className="text-sm font-bold text-gray-900 dark:text-white">{t('upload:certUpload.fileSelect')}</h2>
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
                <span className="text-[10px] text-gray-400 ml-2">{t('upload:certUpload.clickToChange')}</span>
              </div>
            ) : (
              <div className="flex flex-col items-center gap-1.5">
                <CloudUpload className="w-8 h-8 text-gray-400" />
                <p className="text-sm text-gray-600 dark:text-gray-300">{t('upload:certUpload.dropzoneText')}</p>
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
                {t('upload:certUpload.preview')}
              </button>
            )}
            {pageState === 'PREVIEWING' && (
              <div className="flex items-center gap-2 text-indigo-600 dark:text-indigo-400">
                <Loader2 className="w-4 h-4 animate-spin" />
                <span className="text-sm">{t('upload:certUpload.parsing')}</span>
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
                <h2 className="text-sm font-bold text-gray-900 dark:text-white">{t('upload:certUpload.parseResult')}</h2>

                {/* Summary badges */}
                <div className="flex items-center gap-2 ml-auto">
                  {previewResult.isDuplicate && (
                    <span className="inline-flex items-center gap-1 px-2 py-0.5 text-[10px] font-semibold rounded-full bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400">
                      <AlertTriangle className="w-3 h-3" /> {t('upload:statistics.duplicateCount')}
                    </span>
                  )}
                  {isDl && previewResult.dlSignatureValid !== undefined && (
                    <span className={cn(
                      'inline-flex items-center gap-1 px-2 py-0.5 text-[10px] font-semibold rounded-full',
                      previewResult.dlSignatureValid
                        ? 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400'
                        : 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400'
                    )}>
                      <Lock className="w-3 h-3" /> {previewResult.dlSignatureValid ? t('upload:certUpload.sigValid') : t('upload:certUpload.sigInvalid')}
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
                    {t('upload:certUpload.certificatesTab', { num: previewResult.certificates.length })}
                  </span>
                </button>
                {isDl && (
                  <button onClick={() => setPreviewTab('dl-structure')} className={previewTabClass(previewTab === 'dl-structure')}>
                    <span className="flex items-center gap-1.5">
                      <FileText className="w-3.5 h-3.5" />
                      {t('upload:certUpload.dlStructure')}
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
                    return <CertificateCard key={cert.fingerprintSha256} cert={cert} index={i} deviations={certDeviations} />;
                  })}
                  {previewResult.certificates.length === 0 && (
                    <p className="text-sm text-gray-500 text-center py-6">{t('upload:certUpload.noCertificates')}</p>
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
                  <InfoRow label={t('upload:certUpload.crl.issuer')} value={previewResult.crlInfo.issuerDn} mono />
                  <InfoRow label={t('upload:certUpload.crl.country')} value={previewResult.crlInfo.countryCode} />
                  <InfoRow label={t('upload:certUpload.crl.thisUpdate')} value={previewResult.crlInfo.thisUpdate} />
                  <InfoRow label={t('upload:certUpload.crl.nextUpdate')} value={previewResult.crlInfo.nextUpdate || '-'} />
                  <InfoRow label={t('upload:certUpload.crl.crlNumber')} value={previewResult.crlInfo.crlNumber || '-'} mono />
                  <InfoRow label={t('upload:certUpload.crl.revoked')} value={String(previewResult.crlInfo.revokedCount)} />
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
                    <RotateCcw className="w-3.5 h-3.5" /> {t('common:button.cancel')}
                  </button>
                  <button
                    onClick={handleConfirm}
                    disabled={previewResult?.isDuplicate}
                    className={cn(
                      'flex items-center gap-1.5 px-4 py-2 rounded-lg text-sm font-medium transition-colors',
                      previewResult?.isDuplicate
                        ? 'bg-gray-300 dark:bg-gray-600 text-gray-500 dark:text-gray-400 cursor-not-allowed'
                        : 'bg-blue-600 hover:bg-blue-700 text-white'
                    )}
                  >
                    <Database className="w-3.5 h-3.5" /> {t('upload:certUpload.saveToDbAndLdap')}
                  </button>
                </div>
              )}
              {pageState === 'CONFIRMING' && (
                <div className="flex items-center justify-center gap-2 pt-3 border-t border-gray-100 dark:border-gray-700 text-blue-600 dark:text-blue-400">
                  <Loader2 className="w-4 h-4 animate-spin" />
                  <span className="text-sm">{t('common:label.saving')}</span>
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
              <h2 className="text-sm font-bold text-gray-900 dark:text-white">{t('upload:certUpload.saveCompleteTitle')}</h2>
            </div>

            <div className="flex items-start gap-3 p-3 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
              <CheckCircle className="w-5 h-5 text-green-500 flex-shrink-0 mt-0.5" />
              <div>
                <p className="text-sm font-semibold text-green-800 dark:text-green-300">{ t('upload:statistics.totalProcessed') }</p>
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
                <CloudUpload className="w-3.5 h-3.5" /> {t('upload:certUpload.newFile')}
              </button>
              {uploadResult && (
                <button onClick={() => setShowResultDialog(true)} className="flex items-center gap-1.5 px-3.5 py-1.5 border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-xs transition-colors">
                  <Eye className="w-3.5 h-3.5" /> {t('certificate:search.viewDetail')}
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
                  {pageState === 'PREVIEW_ERROR' ? t('upload:certUpload.previewError') : t('upload:certUpload.saveError')}
                </p>
                <p className="mt-0.5 text-xs text-red-700 dark:text-red-400">{errorMessage}</p>
              </div>
            </div>
            <div className="mt-3 flex gap-2">
              <button onClick={handleReset} className="flex items-center gap-1.5 px-3.5 py-1.5 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-xs font-medium transition-colors">
                <RotateCcw className="w-3.5 h-3.5" /> {t('upload:certUpload.retryAction')}
              </button>
              {pageState === 'FAILED' && previewResult && (
                <button onClick={handleConfirm} className="flex items-center gap-1.5 px-3.5 py-1.5 border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700 rounded-lg text-xs transition-colors">
                  <Database className="w-3.5 h-3.5" /> {t('upload:certUpload.retrySave')}
                </button>
              )}
            </div>
          </div>
        )}
      </div>

      {/* Upload Result Dialog */}
      {uploadResult && (
        <Dialog
          isOpen={showResultDialog}
          onClose={() => setShowResultDialog(false)}
          title={t('upload:certUpload.uploadCompleteTitle')}
          size="2xl"
        >
          <div className="space-y-4">
            {/* Status Banner — contextual based on duplicate status */}
            {uploadResult.duplicateCount >= uploadResult.certificateCount && uploadResult.duplicateCount > 0 ? (
              /* All duplicates — no new certificates saved */
              <div className="flex items-center gap-3 p-3 rounded-lg bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800">
                <AlertTriangle className="w-5 h-5 text-orange-500 flex-shrink-0" />
                <div>
                  <span className="text-sm font-medium text-orange-800 dark:text-orange-300">
                    {t('upload:certUpload.allDuplicateMsg')}
                  </span>
                  <p className="text-xs text-orange-600 dark:text-orange-400 mt-0.5">
                    {t('upload:certUpload.allDuplicateDetail', { num: uploadResult.duplicateCount })}
                  </p>
                </div>
                <span className="ml-auto text-xs text-gray-500 dark:text-gray-400 flex-shrink-0">
                  {new Date().toLocaleString('ko-KR')}
                </span>
              </div>
            ) : (
              /* New certificates saved (with optional partial duplicates) */
              <>
                <div className="flex items-center gap-3 p-3 rounded-lg bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
                  <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
                  <span className="text-sm font-medium text-green-800 dark:text-green-300">
                    {t('upload:certUpload.saveCompleteMsg')}
                  </span>
                  <span className="ml-auto text-xs text-gray-500 dark:text-gray-400">
                    {new Date().toLocaleString('ko-KR')}
                  </span>
                </div>
                {uploadResult.duplicateCount > 0 && (
                  <div className="flex items-center gap-3 p-3 rounded-lg bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800">
                    <AlertTriangle className="w-4 h-4 text-orange-500 flex-shrink-0" />
                    <span className="text-sm text-orange-700 dark:text-orange-300">
                      {t('upload:certUpload.partialDuplicateMsg', { num: uploadResult.duplicateCount })}
                    </span>
                  </div>
                )}
              </>
            )}

            {/* File Info */}
            <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
              <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
                <FileText className="w-4 h-4 text-gray-500" />
                {t('upload:detail.fileInfo')}
              </h4>
              <div className="grid grid-cols-2 gap-x-6 gap-y-2 text-sm">
                <div>
                  <span className="text-gray-500 dark:text-gray-400">{ t('upload:history.fileName') }</span>
                  <p className="font-medium text-gray-900 dark:text-white truncate" title={selectedFile?.name}>
                    {selectedFile?.name || '-'}
                  </p>
                </div>
                <div>
                  <span className="text-gray-500 dark:text-gray-400">{ t('upload:history.fileType') }</span>
                  <p>
                    <span className={cn(
                      'inline-flex items-center px-2 py-0.5 rounded text-xs font-semibold',
                      uploadResult.fileFormat === 'DL' ? 'bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-400'
                        : uploadResult.fileFormat === 'CRL' ? 'bg-purple-100 dark:bg-purple-900/30 text-purple-700 dark:text-purple-400'
                        : 'bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400'
                    )}>
                      {uploadResult.fileFormat}
                    </span>
                  </p>
                </div>
                <div>
                  <span className="text-gray-500 dark:text-gray-400">{ t('upload:fileUpload.fileSize') }</span>
                  <p className="font-medium text-gray-900 dark:text-white">
                    {selectedFile ? formatFileSize(selectedFile.size) : '-'}
                  </p>
                </div>
                <div>
                  <span className="text-gray-500 dark:text-gray-400">{ t('upload:history.uploadId') }</span>
                  <p className="font-mono text-xs text-gray-600 dark:text-gray-300 truncate" title={uploadResult.uploadId}>
                    {uploadResult.uploadId.substring(0, 8)}...
                  </p>
                </div>
              </div>
            </div>

            {/* Certificate Count Grid */}
            <div className="border border-gray-200 dark:border-gray-700 rounded-lg p-4">
              <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
                <Shield className="w-4 h-4 text-indigo-500" />
                {t('upload:certUpload.certProcessResult')}
                <span className="ml-auto text-base font-bold text-gray-900 dark:text-white">
                  {t('upload:certUpload.countSuffix', { num: uploadResult.certificateCount })}
                </span>
              </h4>
              <div className="grid grid-cols-3 gap-2">
                {uploadResult.cscaCount > 0 && (
                  <div className="p-3 bg-blue-50 dark:bg-blue-900/20 rounded-lg text-center">
                    <div className="flex items-center justify-center gap-1.5 mb-1">
                      <Shield className="w-3.5 h-3.5 text-blue-500" />
                      <span className="text-xs text-blue-600 dark:text-blue-400 font-medium">CSCA</span>
                    </div>
                    <span className="text-lg font-bold text-blue-700 dark:text-blue-300">{uploadResult.cscaCount}</span>
                  </div>
                )}
                {uploadResult.dscCount > 0 && (
                  <div className="p-3 bg-green-50 dark:bg-green-900/20 rounded-lg text-center">
                    <div className="flex items-center justify-center gap-1.5 mb-1">
                      <Shield className="w-3.5 h-3.5 text-green-500" />
                      <span className="text-xs text-green-600 dark:text-green-400 font-medium">DSC</span>
                    </div>
                    <span className="text-lg font-bold text-green-700 dark:text-green-300">{uploadResult.dscCount}</span>
                  </div>
                )}
                {uploadResult.dscNcCount > 0 && (
                  <div className="p-3 bg-orange-50 dark:bg-orange-900/20 rounded-lg text-center">
                    <div className="flex items-center justify-center gap-1.5 mb-1">
                      <Shield className="w-3.5 h-3.5 text-orange-500" />
                      <span className="text-xs text-orange-600 dark:text-orange-400 font-medium">DSC_NC</span>
                    </div>
                    <span className="text-lg font-bold text-orange-700 dark:text-orange-300">{uploadResult.dscNcCount}</span>
                  </div>
                )}
                {uploadResult.mlscCount > 0 && (
                  <div className="p-3 bg-teal-50 dark:bg-teal-900/20 rounded-lg text-center">
                    <div className="flex items-center justify-center gap-1.5 mb-1">
                      <Key className="w-3.5 h-3.5 text-teal-500" />
                      <span className="text-xs text-teal-600 dark:text-teal-400 font-medium">MLSC</span>
                    </div>
                    <span className="text-lg font-bold text-teal-700 dark:text-teal-300">{uploadResult.mlscCount}</span>
                  </div>
                )}
                {uploadResult.crlCount > 0 && (
                  <div className="p-3 bg-purple-50 dark:bg-purple-900/20 rounded-lg text-center">
                    <div className="flex items-center justify-center gap-1.5 mb-1">
                      <Globe className="w-3.5 h-3.5 text-purple-500" />
                      <span className="text-xs text-purple-600 dark:text-purple-400 font-medium">CRL</span>
                    </div>
                    <span className="text-lg font-bold text-purple-700 dark:text-purple-300">{uploadResult.crlCount}</span>
                  </div>
                )}
              </div>
            </div>

            {/* LDAP Storage Status — hide when all duplicates (no new storage) */}
            {!(uploadResult.duplicateCount >= uploadResult.certificateCount && uploadResult.duplicateCount > 0) && (
              <div className="flex items-center gap-3 p-3 rounded-lg border border-gray-200 dark:border-gray-700">
                <Database className="w-4 h-4 text-gray-500 flex-shrink-0" />
                <div className="flex-1">
                  <div className="flex items-center justify-between">
                    <span className="text-sm text-gray-600 dark:text-gray-400">{ t('certificate:detail.storedInLdap') }</span>
                    <span className="text-sm font-semibold text-gray-900 dark:text-white">
                      {uploadResult.ldapStoredCount} / {uploadResult.certificateCount}
                    </span>
                  </div>
                  <div className="mt-1.5 w-full bg-gray-200 dark:bg-gray-600 rounded-full h-1.5">
                    <div
                      className={cn(
                        'h-1.5 rounded-full transition-all',
                        uploadResult.ldapStoredCount >= uploadResult.certificateCount
                          ? 'bg-green-500'
                          : 'bg-orange-500'
                      )}
                      style={{ width: `${uploadResult.certificateCount > 0 ? Math.round((uploadResult.ldapStoredCount / uploadResult.certificateCount) * 100) : 0}%` }}
                    />
                  </div>
                </div>
                {uploadResult.ldapStoredCount >= uploadResult.certificateCount ? (
                  <CheckCircle className="w-4 h-4 text-green-500 flex-shrink-0" />
                ) : (
                  <AlertTriangle className="w-4 h-4 text-orange-500 flex-shrink-0" />
                )}
              </div>
            )}

            {/* Action Buttons */}
            <div className="flex items-center justify-end gap-2 pt-2 border-t border-gray-200 dark:border-gray-700">
              <button
                onClick={() => { setShowResultDialog(false); navigate('/upload-history'); }}
                className="flex items-center gap-1.5 px-4 py-2 text-sm text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors"
              >
                <History className="w-4 h-4" />
                {t('upload:history.title')}
              </button>
              <button
                onClick={() => setShowResultDialog(false)}
                className="px-4 py-2 text-sm font-medium text-white bg-indigo-600 hover:bg-indigo-700 rounded-lg transition-colors"
              >
                {t('common:confirm.title')}
              </button>
            </div>
          </div>
        </Dialog>
      )}
    </div>
  );
}
