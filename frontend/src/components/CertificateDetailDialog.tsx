import { useTranslation } from 'react-i18next';
import React, { useState, useEffect } from 'react';
import { X, CheckCircle, XCircle, Clock, RefreshCw, ChevronRight, Shield, FileText, Loader2, Brain, Award, FileKey, Link2 } from 'lucide-react';
import { cn } from '@/utils/cn';
import {
  formatDate,
  formatVersion,
  getActualCertType,
  isLinkCertificate,
  isMasterListSignerCertificate,
} from '@/utils/certificateDisplayUtils';
import { TrustChainVisualization } from '@/components/TrustChainVisualization';
import type { ValidationResult } from '@/types/validation';
import type { Doc9303ChecklistResult } from '@/types';
import { TreeViewer } from '@/components/TreeViewer';
import type { TreeNode } from '@/components/TreeViewer';
import { certificateApi } from '@/services/pkdApi';
import { Doc9303ComplianceChecklist } from '@/components/Doc9303ComplianceChecklist';
import ForensicAnalysisPanel from '@/components/ai/ForensicAnalysisPanel';

interface DnComponents {
  commonName?: string;
  organization?: string;
  organizationalUnit?: string;
  locality?: string;
  stateOrProvince?: string;
  country?: string;
  email?: string;
  serialNumber?: string;
}

export interface Certificate {
  dn: string;
  cn: string;
  sn: string;
  country: string;
  type: string;
  subjectDn: string;
  issuerDn: string;
  fingerprint: string;
  validFrom: string;
  validTo: string;
  validity: 'VALID' | 'EXPIRED' | 'NOT_YET_VALID' | 'UNKNOWN';
  isSelfSigned: boolean;
  pkdConformanceCode?: string;
  pkdConformanceText?: string;
  pkdVersion?: string;
  subjectDnComponents?: DnComponents;
  issuerDnComponents?: DnComponents;
  version?: number;
  signatureAlgorithm?: string;
  signatureHashAlgorithm?: string;
  publicKeyAlgorithm?: string;
  publicKeySize?: number;
  publicKeyCurve?: string;
  keyUsage?: string[];
  extendedKeyUsage?: string[];
  isCA?: boolean;
  pathLenConstraint?: number;
  subjectKeyIdentifier?: string;
  authorityKeyIdentifier?: string;
  crlDistributionPoints?: string[];
  ocspResponderUrl?: string;
  isCertSelfSigned?: boolean;
}

interface CertificateDetailDialogProps {
  selectedCert: Certificate;
  showDetailDialog: boolean;
  setShowDetailDialog: (show: boolean) => void;
  detailTab: 'general' | 'details' | 'doc9303' | 'forensic';
  setDetailTab: (tab: 'general' | 'details' | 'doc9303' | 'forensic') => void;
  validationResult: ValidationResult | null;
  validationLoading: boolean;
  exportCertificate: (dn: string, format: 'der' | 'pem') => void;
  getCertTypeBadge: (certType: string, cert?: Certificate) => React.ReactElement;
}

const CertificateDetailDialog: React.FC<CertificateDetailDialogProps> = ({
  selectedCert,
  showDetailDialog,
  setShowDetailDialog,
  detailTab,
  setDetailTab,
  validationResult,
  validationLoading,
  exportCertificate,
  getCertTypeBadge,
}) => {
  const { t } = useTranslation(['certificate', 'common']);
  // Doc 9303 checklist lazy loading state
  const [doc9303Checklist, setDoc9303Checklist] = useState<Doc9303ChecklistResult | null>(null);
  const [doc9303Loading, setDoc9303Loading] = useState(false);
  const [doc9303Error, setDoc9303Error] = useState<string | null>(null);

  // Load Doc 9303 checklist when tab is selected
  useEffect(() => {
    if (detailTab !== 'doc9303' || !selectedCert.fingerprint || doc9303Checklist) return;
    setDoc9303Loading(true);
    setDoc9303Error(null);
    certificateApi.getDoc9303Checklist(selectedCert.fingerprint)
      .then(res => {
        setDoc9303Checklist(res.data);
      })
      .catch(err => {
        setDoc9303Error(err.response?.data?.error || t('certificate:detail.doc9303LoadFailed'));
      })
      .finally(() => setDoc9303Loading(false));
  }, [detailTab, selectedCert.fingerprint, doc9303Checklist]);

  // Reset checklist when certificate changes
  useEffect(() => {
    setDoc9303Checklist(null);
    setDoc9303Error(null);
  }, [selectedCert.fingerprint]);

  // Build trust chain tree from validation result
  const buildTrustChainTree = (trustChainPath: string): TreeNode[] => {
    const parts = trustChainPath.split('\u2192').map(s => s.trim());

    if (parts.length === 0) return [];

    const nodes: TreeNode[] = [];
    const reversedParts = [...parts].reverse();

    reversedParts.forEach((part, index) => {
      const isRoot = index === 0;
      const isLeaf = index === reversedParts.length - 1;

      let displayName = part;
      let icon = 'shield-check';

      if (part.startsWith('DSC')) {
        icon = 'hard-drive';
        displayName = part.replace('DSC', 'Document Signer Certificate');
      } else if (part.includes('CSCA') || part.includes('CN=')) {
        icon = isRoot ? 'shield-check' : 'shield';
        const cnMatch = part.match(/CN=([^,]+)/);
        if (cnMatch) {
          displayName = cnMatch[1];
        }
      }

      nodes.push({
        id: `chain-${index}`,
        name: isRoot ? '\uD83D\uDD12 Root CA' : isLeaf ? '\uD83D\uDCC4 Certificate' : '\uD83D\uDD17 Intermediate CA',
        value: displayName,
        icon: icon as any,
      });
    });

    return nodes;
  };

  // Build certificate tree data for react-arborist
  const buildCertificateTree = (cert: Certificate): TreeNode[] => {
    const children: TreeNode[] = [];

    children.push({
      id: 'version',
      name: 'Version',
      value: formatVersion(cert.version),
      icon: 'file-text',
    });

    children.push({
      id: 'serial',
      name: 'Serial Number',
      value: cert.sn,
      icon: 'hash',
    });

    if (cert.signatureAlgorithm || cert.signatureHashAlgorithm) {
      const sigChildren: TreeNode[] = [];
      if (cert.signatureAlgorithm) {
        sigChildren.push({ id: 'sig-algo', name: 'Algorithm', value: cert.signatureAlgorithm });
      }
      if (cert.signatureHashAlgorithm) {
        sigChildren.push({ id: 'hash-algo', name: 'Hash', value: cert.signatureHashAlgorithm });
      }
      children.push({
        id: 'signature',
        name: 'Signature',
        children: sigChildren,
        icon: 'shield',
      });
    }

    children.push({
      id: 'issuer',
      name: 'Issuer',
      value: cert.issuerDn,
      icon: 'user',
    });

    children.push({
      id: 'validity',
      name: 'Validity',
      children: [
        { id: 'valid-from', name: 'Not Before', value: formatDate(cert.validFrom) },
        { id: 'valid-to', name: 'Not After', value: formatDate(cert.validTo) },
      ],
      icon: 'calendar',
    });

    children.push({
      id: 'subject',
      name: 'Subject',
      value: cert.subjectDn,
      icon: 'user',
    });

    if (cert.publicKeyAlgorithm || cert.publicKeySize) {
      const pkChildren: TreeNode[] = [];
      if (cert.publicKeyAlgorithm) {
        pkChildren.push({ id: 'pk-algo', name: 'Algorithm', value: cert.publicKeyAlgorithm });
      }
      if (cert.publicKeySize) {
        pkChildren.push({ id: 'pk-size', name: 'Key Size', value: `${cert.publicKeySize} bits` });
      }
      if (cert.publicKeyCurve) {
        pkChildren.push({ id: 'pk-curve', name: 'Curve', value: cert.publicKeyCurve });
      }
      children.push({
        id: 'public-key',
        name: 'Public Key',
        children: pkChildren,
        icon: 'key',
      });
    }

    const extChildren: TreeNode[] = [];

    if (cert.keyUsage && cert.keyUsage.length > 0) {
      extChildren.push({
        id: 'key-usage',
        name: 'Key Usage',
        value: cert.keyUsage.join(', '),
        icon: 'lock',
      });
    }

    if (cert.extendedKeyUsage && cert.extendedKeyUsage.length > 0) {
      extChildren.push({
        id: 'ext-key-usage',
        name: 'Extended Key Usage',
        value: cert.extendedKeyUsage.join(', '),
        icon: 'lock',
      });
    }

    if (cert.isCA !== undefined || cert.pathLenConstraint !== undefined) {
      const bcChildren: TreeNode[] = [];
      if (cert.isCA !== undefined) {
        bcChildren.push({ id: 'is-ca', name: 'CA', value: cert.isCA ? 'TRUE' : 'FALSE' });
      }
      if (cert.pathLenConstraint !== undefined) {
        bcChildren.push({ id: 'path-len', name: 'Path Length', value: String(cert.pathLenConstraint) });
      }
      extChildren.push({
        id: 'basic-constraints',
        name: 'Basic Constraints',
        children: bcChildren,
        icon: 'shield-check',
      });
    }

    if (cert.subjectKeyIdentifier) {
      extChildren.push({
        id: 'ski',
        name: 'Subject Key Identifier',
        value: cert.subjectKeyIdentifier,
        copyable: true,
        icon: 'hash',
      });
    }

    if (cert.authorityKeyIdentifier) {
      extChildren.push({
        id: 'aki',
        name: 'Authority Key Identifier',
        value: cert.authorityKeyIdentifier,
        copyable: true,
        icon: 'hash',
      });
    }

    if (cert.crlDistributionPoints && cert.crlDistributionPoints.length > 0) {
      const crlChildren: TreeNode[] = cert.crlDistributionPoints.map((url, index) => ({
        id: `crl-${index}`,
        name: `URL ${index + 1}`,
        value: url,
        linkUrl: url,
        icon: 'link-2',
      }));
      extChildren.push({
        id: 'crl-dist',
        name: 'CRL Distribution Points',
        children: crlChildren,
        icon: 'file-text',
      });
    }

    if (cert.ocspResponderUrl) {
      extChildren.push({
        id: 'ocsp',
        name: 'OCSP Responder',
        value: cert.ocspResponderUrl,
        linkUrl: cert.ocspResponderUrl,
        icon: 'link-2',
      });
    }

    if (extChildren.length > 0) {
      children.push({
        id: 'extensions',
        name: 'Extensions',
        children: extChildren,
        icon: 'settings',
      });
    }

    return [{
      id: 'certificate',
      name: 'Certificate',
      children,
      icon: 'file-text',
    }];
  };

  if (!showDetailDialog) return null;

  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center">
      {/* Backdrop */}
      <div
        className="absolute inset-0 bg-black/50 backdrop-blur-sm"
        onClick={() => setShowDetailDialog(false)}
      />

      {/* Dialog Content */}
      <div className="relative bg-white dark:bg-gray-800 rounded-xl shadow-xl w-full max-w-4xl mx-4 max-h-[90vh] flex flex-col">
        {/* Header */}
        <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700">
          <div className="flex items-center gap-2">
            <div className="p-1.5 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
              <Shield className="w-4 h-4 text-white" />
            </div>
            <div>
              <h2 className="text-base font-semibold text-gray-900 dark:text-white">
                {t('certificate:detail.certDetail')}
              </h2>
              <p className="text-sm text-gray-500 dark:text-gray-400 truncate max-w-md" title={`${selectedCert.country} - ${selectedCert.subjectDnComponents?.organization || selectedCert.cn}`}>
                {selectedCert.country} - {selectedCert.subjectDnComponents?.organization || selectedCert.cn}
              </p>
              {/* Certificate Type Badges */}
              <div className="flex items-center gap-2 mt-2">
                {getCertTypeBadge(getActualCertType(selectedCert), selectedCert)}
                {isLinkCertificate(selectedCert) && (
                  <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-cyan-100 dark:bg-cyan-900/40 text-cyan-800 dark:text-cyan-300 border border-cyan-200 dark:border-cyan-700">
                    Link Certificate
                  </span>
                )}
                {isMasterListSignerCertificate(selectedCert) && (
                  <span className="inline-flex items-center px-2 py-1 text-xs font-semibold rounded bg-purple-100 dark:bg-purple-900/40 text-purple-800 dark:text-purple-300 border border-purple-200 dark:border-purple-700">
                    Master List Signer
                  </span>
                )}
                {selectedCert.isSelfSigned && (
                  <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-400">
                    <CheckCircle className="w-3 h-3 mr-1" />
                    Self-signed
                  </span>
                )}
              </div>
            </div>
          </div>
          <button
            onClick={() => setShowDetailDialog(false)}
            className="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            aria-label={t('common:button.close')}
          >
            <X className="w-5 h-5 text-gray-500" />
          </button>
        </div>

        {/* Tabs */}
        <div className="border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-700/50">
          <div className="flex">
            <button
              onClick={() => setDetailTab('general')}
              className={cn(
                'px-6 py-3 text-sm font-medium border-b-2 transition-colors',
                detailTab === 'general'
                  ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                  : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
              )}
            >
              General
            </button>
            <button
              onClick={() => setDetailTab('details')}
              className={cn(
                'px-6 py-3 text-sm font-medium border-b-2 transition-colors',
                detailTab === 'details'
                  ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                  : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
              )}
            >
              Details
            </button>
            <button
              onClick={() => setDetailTab('doc9303')}
              className={cn(
                'px-6 py-3 text-sm font-medium border-b-2 transition-colors',
                detailTab === 'doc9303'
                  ? 'border-blue-600 text-blue-600 dark:text-blue-400 bg-white dark:bg-gray-800'
                  : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
              )}
            >
              Doc 9303
            </button>
            <button
              onClick={() => setDetailTab('forensic')}
              className={cn(
                'px-6 py-3 text-sm font-medium border-b-2 transition-colors flex items-center gap-1.5',
                detailTab === 'forensic'
                  ? 'border-purple-600 text-purple-600 dark:text-purple-400 bg-white dark:bg-gray-800'
                  : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 hover:bg-gray-100 dark:hover:bg-gray-700'
              )}
            >
              <Brain className="w-3.5 h-3.5" />
              {t('certificate:detail.forensicTab')}
            </button>
          </div>
        </div>

        {/* Content */}
        <div className="flex-1 overflow-y-auto p-6">
          {/* General Tab */}
          {detailTab === 'general' && (
            <div className="space-y-4">
              {/* Issued To/By in 2-column layout */}
              <div className="grid grid-cols-2 gap-4">
                {/* Issued To */}
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Issued To</h3>
                  <div className="space-y-2">
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">CN:</span>
                      <span className="text-xs text-gray-900 dark:text-white break-all">
                        {selectedCert.subjectDnComponents?.commonName || selectedCert.cn}
                      </span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Organization:</span>
                      <span className="text-xs text-gray-900 dark:text-white">
                        {selectedCert.subjectDnComponents?.organization || '-'}
                      </span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Org. Unit:</span>
                      <span className="text-xs text-gray-900 dark:text-white">
                        {selectedCert.subjectDnComponents?.organizationalUnit || '-'}
                      </span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Serial:</span>
                      <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                        {selectedCert.subjectDnComponents?.serialNumber || selectedCert.sn}
                      </span>
                    </div>
                  </div>
                </div>

                {/* Issued By */}
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Issued By</h3>
                  <div className="space-y-2">
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">CN:</span>
                      <span className="text-xs text-gray-900 dark:text-white break-all">
                        {selectedCert.issuerDnComponents?.commonName || '-'}
                      </span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Organization:</span>
                      <span className="text-xs text-gray-900 dark:text-white">
                        {selectedCert.issuerDnComponents?.organization || '-'}
                      </span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Org. Unit:</span>
                      <span className="text-xs text-gray-900 dark:text-white">
                        {selectedCert.issuerDnComponents?.organizationalUnit || '-'}
                      </span>
                    </div>
                  </div>
                </div>
              </div>

              {/* Validity and Fingerprints in 2-column layout */}
              <div className="grid grid-cols-2 gap-4">
                {/* Validity */}
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Validity</h3>
                  <div className="space-y-2">
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Issued on:</span>
                      <span className="text-xs text-gray-900 dark:text-white">{formatDate(selectedCert.validFrom)}</span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">Expires on:</span>
                      <span className="text-xs text-gray-900 dark:text-white">{formatDate(selectedCert.validTo)}</span>
                    </div>
                  </div>
                </div>

                {/* Fingerprints */}
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">Fingerprints</h3>
                  <div className="space-y-2">
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">SHA-256:</span>
                      <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                        {selectedCert.fingerprint || 'N/A'}
                      </span>
                    </div>
                  </div>
                </div>
              </div>

              {/* PKD Conformance Section (DSC_NC only) */}
              {getActualCertType(selectedCert) === 'DSC_NC' && (selectedCert.pkdConformanceCode || selectedCert.pkdConformanceText || selectedCert.pkdVersion) && (
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">
                    PKD Conformance Information
                  </h3>
                  <div className="space-y-2">
                    {selectedCert.pkdConformanceCode && (
                      <div className="grid grid-cols-[120px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Conformance Code:</span>
                        <span className="text-xs text-gray-900 dark:text-white font-mono">
                          {selectedCert.pkdConformanceCode}
                        </span>
                      </div>
                    )}
                    {selectedCert.pkdVersion && (
                      <div className="grid grid-cols-[120px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">PKD Version:</span>
                        <span className="text-xs text-gray-900 dark:text-white">
                          {selectedCert.pkdVersion}
                        </span>
                      </div>
                    )}
                    {selectedCert.pkdConformanceText && (
                      <div className="grid grid-cols-[120px_1fr] gap-2">
                        <span className="text-xs text-gray-600 dark:text-gray-400">Conformance Text:</span>
                        <div className="text-xs text-gray-900 dark:text-white">
                          <div className="bg-orange-50 dark:bg-orange-900/10 border border-orange-200 dark:border-orange-700 rounded p-2">
                            <pre className="whitespace-pre-wrap break-words text-xs font-mono">
                              {selectedCert.pkdConformanceText}
                            </pre>
                          </div>
                        </div>
                      </div>
                    )}
                  </div>
                </div>
              )}

              {/* Trust Chain Summary Card (DSC / DSC_NC only) */}
              {(getActualCertType(selectedCert) === 'DSC' || getActualCertType(selectedCert) === 'DSC_NC') && (
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2 pb-1.5 border-b border-gray-200 dark:border-gray-700">
                    Trust Chain Validation
                  </h3>
                  {validationLoading ? (
                    <div className="flex items-center gap-2 p-2.5 bg-gray-50 dark:bg-gray-800/50 rounded-lg border border-gray-200 dark:border-gray-700">
                      <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                      <span className="text-xs text-gray-500 dark:text-gray-400">{ t('certificate:detail.loading') }</span>
                    </div>
                  ) : validationResult ? (
                    <div className={`rounded-lg border p-3 space-y-2 ${
                      validationResult.trustChainValid
                        ? 'bg-green-50 dark:bg-green-900/10 border-green-200 dark:border-green-800'
                        : validationResult.validationStatus === 'PENDING'
                        ? 'bg-yellow-50 dark:bg-yellow-900/10 border-yellow-200 dark:border-yellow-800'
                        : 'bg-red-50 dark:bg-red-900/10 border-red-200 dark:border-red-800'
                    }`}>
                      {/* Status Badge Row */}
                      <div className="flex items-center justify-between">
                        <div className="flex items-center gap-2">
                          {validationResult.validationStatus === 'EXPIRED_VALID' ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-amber-100 dark:bg-amber-900/40 text-amber-800 dark:text-amber-300">
                              <CheckCircle className="w-3 h-3" />
                              {t('certificate:detail.expiredValid')}
                            </span>
                          ) : validationResult.trustChainValid ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300">
                              <CheckCircle className="w-3 h-3" />
                              {t('certificate:detail.trustChainValid')}
                            </span>
                          ) : validationResult.validationStatus === 'PENDING' ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-yellow-100 dark:bg-yellow-900/40 text-yellow-800 dark:text-yellow-300">
                              <Clock className="w-3 h-3" />
                              {t('certificate:detail.verificationPending')}
                            </span>
                          ) : (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300">
                              <XCircle className="w-3 h-3" />
                              {t('certificate:detail.trustChainInvalid')}
                            </span>
                          )}
                        </div>
                        <button
                          onClick={() => setDetailTab('details')}
                          className="text-xs text-blue-600 dark:text-blue-400 hover:underline flex items-center gap-1"
                        >
                          {t('icao:banner.viewDetails')} <ChevronRight className="w-3 h-3" />
                        </button>
                      </div>

                      {/* Trust Chain Path (Compact) */}
                      {validationResult.trustChainPath && (
                        <div>
                          <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">{t('certificate:detail.trustChainPath')}</span>
                          <TrustChainVisualization
                            trustChainPath={validationResult.trustChainPath}
                            trustChainValid={validationResult.trustChainValid}
                            compact={true}
                          />
                        </div>
                      )}

                      {/* Validation Message */}
                      {validationResult.trustChainMessage && (
                        <p className={`text-xs ${
                          validationResult.trustChainValid
                            ? 'text-green-700 dark:text-green-400'
                            : validationResult.validationStatus === 'PENDING'
                            ? 'text-yellow-700 dark:text-yellow-400'
                            : 'text-red-700 dark:text-red-400'
                        }`}>
                          {validationResult.trustChainMessage}
                        </p>
                      )}
                    </div>
                  ) : (
                    <p className="text-xs text-gray-500 dark:text-gray-400 p-2.5 bg-gray-50 dark:bg-gray-800/50 rounded-lg border border-gray-200 dark:border-gray-700">
                      {t('certificate:detail.noTrustChainResult')}
                    </p>
                  )}
                </div>
              )}
            </div>
          )}

          {/* Doc 9303 Tab */}
          {detailTab === 'doc9303' && (
            <div className="space-y-4">
              {doc9303Loading && (
                <div className="flex items-center justify-center gap-2 py-8 text-blue-500">
                  <Loader2 className="w-5 h-5 animate-spin" />
                  <span className="text-sm text-gray-500 dark:text-gray-400">{t('certificate:detail.doc9303Loading')}</span>
                </div>
              )}
              {doc9303Error && (
                <div className="p-4 rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
                  <p className="text-sm text-red-600 dark:text-red-400">{doc9303Error}</p>
                </div>
              )}
              {doc9303Checklist && (
                <Doc9303ComplianceChecklist checklist={doc9303Checklist} />
              )}
              {!doc9303Loading && !doc9303Error && !doc9303Checklist && (
                <p className="text-sm text-gray-500 dark:text-gray-400 text-center py-8">
                  {t('certificate:detail.doc9303NoData')}
                </p>
              )}
            </div>
          )}

          {/* Forensic Tab */}
          {detailTab === 'forensic' && (
            <ForensicAnalysisPanel fingerprint={selectedCert.fingerprint} />
          )}

          {/* Details Tab */}
          {detailTab === 'details' && (
            <div className="space-y-4">
              {/* Trust Chain Validation */}
              <div>
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                  {t('certificate:detail.trustChainVerification')}
                </h3>
                {validationLoading ? (
                  <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 flex items-center justify-center gap-2">
                    <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                    <span className="text-sm text-gray-600 dark:text-gray-400">{ t('certificate:detail.loading') }</span>
                  </div>
                ) : validationResult ? (
                  <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 space-y-3">
                    {/* Validation Status */}
                    <div className="flex items-center gap-2">
                      <span className="text-sm text-gray-600 dark:text-gray-400">{ t('certificate:detail.status_label') }</span>
                      {validationResult.trustChainValid ? (
                        <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
                          <CheckCircle className="w-3 h-3 mr-1" />
                          {t('upload:statistics.validCount')}
                        </span>
                      ) : (
                        <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
                          <XCircle className="w-3 h-3 mr-1" />
                          {t('certificate:detail.invalid')}
                        </span>
                      )}
                    </div>

                    {/* Trust Chain Path Visualization (PA-style) */}
                    <div>
                      <div className="text-xs font-semibold text-gray-600 dark:text-gray-400 mb-2">{ t('certificate:detail.trustChainPath') }</div>
                      <div className="flex flex-col items-center gap-1">
                        {/* CSCA (Root) */}
                        {validationResult.cscaFound && validationResult.cscaSubjectDn ? (
                          <div className="w-full p-2 bg-green-100 dark:bg-green-900/30 rounded border border-green-300 dark:border-green-700">
                            <div className="flex items-center gap-2">
                              <Award className="w-3.5 h-3.5 text-green-600 dark:text-green-400" />
                              <span className="text-xs font-semibold text-green-700 dark:text-green-300">CSCA (Root)</span>
                            </div>
                            <code className="block mt-1 text-[11px] font-mono break-all text-gray-600 dark:text-gray-400">
                              {validationResult.cscaSubjectDn}
                            </code>
                          </div>
                        ) : (
                          <div className="w-full p-2 bg-red-50 dark:bg-red-900/20 rounded border border-red-300 dark:border-red-700">
                            <div className="flex items-center gap-2">
                              <XCircle className="w-3.5 h-3.5 text-red-500" />
                              <span className="text-xs font-semibold text-red-700 dark:text-red-300">{ t('pa:steps.cscaNotFound') }</span>
                            </div>
                            {validationResult.issuerDn && (
                              <code className="block mt-1 text-[11px] font-mono break-all text-gray-500 dark:text-gray-400">
                                {t('certificate:detail.issuerLabel')} {validationResult.issuerDn}
                              </code>
                            )}
                          </div>
                        )}

                        {/* Link Certificate indicator (if chain has Link) */}
                        {validationResult.trustChainMessage && /Link/.test(validationResult.trustChainMessage) && (
                          <>
                            <div className="text-[10px] text-gray-400 dark:text-gray-500">{ t('certificate:detail.signatureArrow') }</div>
                            <div className="w-full p-2 bg-purple-50 dark:bg-purple-900/20 rounded border border-purple-300 dark:border-purple-700">
                              <div className="flex items-center gap-2">
                                <Link2 className="w-3.5 h-3.5 text-purple-600 dark:text-purple-400" />
                                <span className="text-xs font-semibold text-purple-700 dark:text-purple-300">Link Certificate</span>
                                {(() => {
                                  const linkCount = (validationResult.trustChainMessage.match(/Link/g) || []).length;
                                  return linkCount > 1 ? (
                                    <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-purple-200 dark:bg-purple-900/50 text-purple-700 dark:text-purple-300">
                                      ×{linkCount}
                                    </span>
                                  ) : null;
                                })()}
                              </div>
                              <span className="block mt-1 text-[11px] text-gray-500 dark:text-gray-400">
                                {t('certificate:detail.linkCertChain')}
                              </span>
                            </div>
                          </>
                        )}

                        {/* Arrow */}
                        <div className="text-[10px] text-gray-400 dark:text-gray-500">{ t('certificate:detail.signatureArrow') }</div>

                        {/* DSC (Leaf) */}
                        <div className="w-full p-2 bg-blue-100 dark:bg-blue-900/30 rounded border border-blue-300 dark:border-blue-700">
                          <div className="flex items-center gap-2">
                            <FileKey className="w-3.5 h-3.5 text-blue-600 dark:text-blue-400" />
                            <span className="text-xs font-semibold text-blue-700 dark:text-blue-300">DSC (Document Signer)</span>
                            {validationResult.isExpired && (
                              <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-orange-200 dark:bg-orange-900/50 text-orange-700 dark:text-orange-300">
                                {t('certificate:detail.expired')}
                              </span>
                            )}
                            {validationResult.signatureVerified && (
                              <span className="px-1.5 py-0.5 rounded text-[10px] font-medium bg-green-200 dark:bg-green-900/50 text-green-700 dark:text-green-300">
                                {t('certificate:detail.signatureVerified')}
                              </span>
                            )}
                          </div>
                          <code className="block mt-1 text-[11px] font-mono break-all text-gray-600 dark:text-gray-400">
                            {validationResult.subjectDn}
                          </code>
                          {validationResult.notBefore && validationResult.notAfter && (
                            <div className="mt-1 text-[11px] text-gray-500 dark:text-gray-400">
                              {t('certificate:detail.validityPeriod', { from: validationResult.notBefore?.split('T')[0], to: validationResult.notAfter?.split('T')[0] })}
                            </div>
                          )}
                        </div>
                      </div>
                    </div>

                    {/* Additional checks summary */}
                    <div className="flex flex-wrap gap-x-4 gap-y-1 text-[11px] pt-2 border-t border-gray-200 dark:border-gray-600">
                      <span className={validationResult.cscaFound ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'}>
                        {validationResult.cscaFound ? '✓' : '✗'} CSCA {validationResult.cscaFound ? t('common:button.confirm') : t('certificate:detail.cscaNotRegistered')}
                      </span>
                      <span className={validationResult.signatureVerified ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'}>
                        {validationResult.signatureVerified ? '✓' : '✗'} {t('certificate:detail.signatureVerify')}
                      </span>
                      <span className={validationResult.validityCheckPassed ? 'text-green-600 dark:text-green-400' : validationResult.isExpired ? 'text-amber-600 dark:text-amber-400' : 'text-red-600 dark:text-red-400'}>
                        {validationResult.validityCheckPassed ? '✓' : validationResult.isExpired ? '⚠' : '✗'} {validationResult.validityCheckPassed ? t('certificate:detail.withinValidityPeriod') : validationResult.isExpired ? t('common:status.expired') : t('common:label.validityPeriod')}
                      </span>
                      <span className={validationResult.crlCheckStatus && validationResult.crlCheckStatus !== 'NOT_CHECKED' ? (validationResult.crlCheckStatus === 'REVOKED' ? 'text-red-600 dark:text-red-400' : 'text-green-600 dark:text-green-400') : 'text-gray-400'}>
                        {validationResult.crlCheckStatus && validationResult.crlCheckStatus !== 'NOT_CHECKED' ? (validationResult.crlCheckStatus === 'REVOKED' ? t('certificate:detail.crlRevoked') : t('certificate:detail.crlNotRevoked')) : t('certificate:detail.crlNotChecked')}
                      </span>
                    </div>
                  </div>
                ) : (
                  <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600">
                    <p className="text-sm text-gray-600 dark:text-gray-400">
                      {t('certificate:detail.noVerificationResult')}
                    </p>
                  </div>
                )}
              </div>

              {/* Link Certificate Information */}
              {isLinkCertificate(selectedCert) && (
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                    {t('certificate:detail.linkCertInfo')}
                  </h3>
                  <div className="bg-cyan-50 dark:bg-cyan-900/20 border border-cyan-200 dark:border-cyan-700 rounded-lg p-4 space-y-3">
                    <div className="flex items-start gap-2">
                      <Shield className="w-5 h-5 text-cyan-600 dark:text-cyan-400 mt-0.5" />
                      <div className="flex-1">
                        <h4 className="text-sm font-semibold text-cyan-800 dark:text-cyan-300 mb-2">
                          {t('certificate:detail.purpose')}
                        </h4>
                        <p className="text-xs text-cyan-700 dark:text-cyan-400 leading-relaxed">
                          {t('certificate:detail.linkCertDesc')}
                        </p>
                        <ul className="mt-2 ml-4 space-y-1 text-xs text-cyan-700 dark:text-cyan-400">
                          <li className="list-disc">{t('certificate:detail.linkCertUseCase1')}</li>
                          <li className="list-disc">{t('certificate:detail.linkCertUseCase2')}</li>
                          <li className="list-disc">{t('certificate:detail.linkCertUseCase3')}</li>
                          <li className="list-disc">{t('certificate:detail.linkCertUseCase4')}</li>
                        </ul>
                      </div>
                    </div>
                    <div className="border-t border-cyan-200 dark:border-cyan-700 pt-3">
                      <div className="grid grid-cols-[120px_1fr] gap-2 text-xs">
                        <span className="text-cyan-600 dark:text-cyan-400 font-medium">LDAP DN:</span>
                        <span className="text-cyan-800 dark:text-cyan-300 font-mono break-all">{selectedCert.dn}</span>
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* Master List Signer Certificate Information */}
              {isMasterListSignerCertificate(selectedCert) && (
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                    {t('certificate:detail.mlscInfo')}
                  </h3>
                  <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-700 rounded-lg p-4 space-y-3">
                    <div className="flex items-start gap-2">
                      <FileText className="w-5 h-5 text-purple-600 dark:text-purple-400 mt-0.5" />
                      <div className="flex-1">
                        <h4 className="text-sm font-semibold text-purple-800 dark:text-purple-300 mb-2">
                          {t('certificate:detail.purpose')}
                        </h4>
                        <p className="text-xs text-purple-700 dark:text-purple-400 leading-relaxed">
                          {t('certificate:detail.mlscDesc')}
                        </p>
                        <ul className="mt-2 ml-4 space-y-1 text-xs text-purple-700 dark:text-purple-400">
                          <li className="list-disc">{t('certificate:detail.mlscFeature1')}</li>
                          <li className="list-disc">{t('certificate:detail.mlscFeature2')}</li>
                          <li className="list-disc">{t('certificate:detail.mlscFeature3')}</li>
                          <li className="list-disc">{t('certificate:detail.mlscFeature4')}</li>
                        </ul>
                      </div>
                    </div>
                    <div className="border-t border-purple-200 dark:border-purple-700 pt-3">
                      <div className="space-y-2 text-xs">
                        <div className="grid grid-cols-[120px_1fr] gap-2">
                          <span className="text-purple-600 dark:text-purple-400 font-medium">LDAP DN:</span>
                          <span className="text-purple-800 dark:text-purple-300 font-mono break-all">{selectedCert.dn}</span>
                        </div>
                        <div className="grid grid-cols-[120px_1fr] gap-2">
                          <span className="text-purple-600 dark:text-purple-400 font-medium">{t('certificate:detail.storageLocation')}</span>
                          <span className="text-purple-800 dark:text-purple-300">
                            {t('certificate:detail.storageLocationDesc')}
                          </span>
                        </div>
                        <div className="grid grid-cols-[120px_1fr] gap-2">
                          <span className="text-purple-600 dark:text-purple-400 font-medium">{t('certificate:detail.selfSigned')}</span>
                          <span className="text-purple-800 dark:text-purple-300">
                            {selectedCert.isSelfSigned ? t('certificate:detail.selfSignedYes') : t('common:label.no')}
                          </span>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* Trust Chain Hierarchy (only show for non-self-signed certificates with trust chain) */}
              {validationResult && validationResult.trustChainPath && validationResult.trustChainPath.trim() !== '' && !selectedCert.isSelfSigned && (
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                    {t('certificate:detail.trustChainHierarchy')}
                  </h3>
                  <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg border border-gray-200 dark:border-gray-600 overflow-hidden">
                    <TreeViewer
                      data={buildTrustChainTree(validationResult.trustChainPath)}
                      height="200px"
                      compact
                    />
                  </div>
                  {validationResult.trustChainValid ? (
                    <div className="mt-2 flex items-center gap-2 text-sm text-green-600 dark:text-green-400">
                      <CheckCircle className="w-4 h-4" />
                      <span>{t('certificate:detail.trustChainValidMsg')}</span>
                    </div>
                  ) : (
                    <div className="mt-2 flex items-center gap-2 text-sm text-red-600 dark:text-red-400">
                      <XCircle className="w-4 h-4" />
                      <span>{t('certificate:detail.trustChainFailedMsg')}</span>
                    </div>
                  )}
                </div>
              )}

              {/* Certificate Fields Tree */}
              <div>
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">{t('certificate:detail.certFields')}</h3>
                <TreeViewer
                  data={buildCertificateTree(selectedCert)}
                  height="400px"
                  compact
                />
              </div>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="flex justify-between items-center gap-3 px-5 py-3 border-t border-gray-200 dark:border-gray-700">
          <button
            onClick={() => exportCertificate(selectedCert.dn, 'pem')}
            className="px-4 py-2 rounded-lg text-sm font-medium text-gray-700 dark:text-gray-300 bg-white dark:bg-gray-700 border border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-600 transition-colors"
          >
            {t('certificate:detail.savingCert')}
          </button>
          <button
            onClick={() => setShowDetailDialog(false)}
            className="px-4 py-2 rounded-lg text-sm font-medium text-white bg-blue-600 hover:bg-blue-700 transition-colors"
          >
            {t('icao:banner.dismiss')}
          </button>
        </div>
      </div>
    </div>
  );
};

export default CertificateDetailDialog;
