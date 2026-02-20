import React, { useState, useEffect } from 'react';
import { X, CheckCircle, XCircle, Clock, RefreshCw, ChevronRight, Shield, FileText, Loader2 } from 'lucide-react';
import { cn } from '@/utils/cn';
import { TrustChainVisualization } from '@/components/TrustChainVisualization';
import type { ValidationResult } from '@/types/validation';
import type { Doc9303ChecklistResult } from '@/types';
import { TreeViewer } from '@/components/TreeViewer';
import type { TreeNode } from '@/components/TreeViewer';
import { certificateApi } from '@/services/pkdApi';
import { Doc9303ComplianceChecklist } from '@/components/Doc9303ComplianceChecklist';

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
  detailTab: 'general' | 'details' | 'doc9303';
  setDetailTab: (tab: 'general' | 'details' | 'doc9303') => void;
  validationResult: ValidationResult | null;
  validationLoading: boolean;
  exportCertificate: (dn: string, format: 'der' | 'pem') => void;
  formatDate: (dateStr: string) => string;
  formatVersion: (version: number | undefined) => string;

  isLinkCertificate: (cert: Certificate) => boolean;
  isMasterListSignerCertificate: (cert: Certificate) => boolean;
  getActualCertType: (cert: Certificate) => 'CSCA' | 'DSC' | 'DSC_NC' | 'MLSC' | 'UNKNOWN';
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
  formatDate,
  formatVersion,
  isLinkCertificate,
  isMasterListSignerCertificate,
  getActualCertType,
  getCertTypeBadge,
}) => {
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
        setDoc9303Error(err.response?.data?.error || 'Doc 9303 체크리스트를 로드할 수 없습니다.');
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
    <div className="fixed inset-0 z-50 flex items-center justify-center">
      {/* Backdrop */}
      <div
        className="absolute inset-0 bg-black/50 backdrop-blur-sm"
        onClick={() => setShowDetailDialog(false)}
      />

      {/* Dialog Content */}
      <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-4xl mx-4 max-h-[90vh] flex flex-col">
        {/* Header */}
        <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
              <Shield className="w-5 h-5 text-white" />
            </div>
            <div>
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white">
                인증서 상세 정보
              </h2>
              <p className="text-sm text-gray-500 dark:text-gray-400 truncate max-w-md">
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
                      <span className="text-xs text-gray-600 dark:text-gray-400">SHA1:</span>
                      <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                        {selectedCert.fingerprint.substring(0, 40) || 'N/A'}
                      </span>
                    </div>
                    <div className="grid grid-cols-[80px_1fr] gap-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">MD5:</span>
                      <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                        {selectedCert.fingerprint.substring(0, 32) || 'N/A'}
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
                      <span className="text-xs text-gray-500 dark:text-gray-400">검증 결과 로드 중...</span>
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
                              만료-유효 (서명 검증 성공)
                            </span>
                          ) : validationResult.trustChainValid ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300">
                              <CheckCircle className="w-3 h-3" />
                              신뢰 체인 유효
                            </span>
                          ) : validationResult.validationStatus === 'PENDING' ? (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-yellow-100 dark:bg-yellow-900/40 text-yellow-800 dark:text-yellow-300">
                              <Clock className="w-3 h-3" />
                              검증 대기 (만료됨)
                            </span>
                          ) : (
                            <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-semibold bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300">
                              <XCircle className="w-3 h-3" />
                              신뢰 체인 유효하지 않음
                            </span>
                          )}
                        </div>
                        <button
                          onClick={() => setDetailTab('details')}
                          className="text-xs text-blue-600 dark:text-blue-400 hover:underline flex items-center gap-1"
                        >
                          자세히 보기 <ChevronRight className="w-3 h-3" />
                        </button>
                      </div>

                      {/* Trust Chain Path (Compact) */}
                      {validationResult.trustChainPath && (
                        <div>
                          <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">신뢰 체인 경로:</span>
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
                      이 인증서에 대한 신뢰 체인 검증 결과가 없습니다.
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
                  <span className="text-sm text-gray-500 dark:text-gray-400">Doc 9303 체크리스트 로드 중...</span>
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
                  Doc 9303 적합성 체크리스트 데이터가 없습니다.
                </p>
              )}
            </div>
          )}

          {/* Details Tab */}
          {detailTab === 'details' && (
            <div className="space-y-4">
              {/* Trust Chain Validation */}
              <div>
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                  신뢰 체인 검증
                </h3>
                {validationLoading ? (
                  <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 flex items-center justify-center gap-2">
                    <RefreshCw className="w-4 h-4 animate-spin text-blue-500" />
                    <span className="text-sm text-gray-600 dark:text-gray-400">검증 결과 로드 중...</span>
                  </div>
                ) : validationResult ? (
                  <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600 space-y-3">
                    {/* Validation Status */}
                    <div className="flex items-center gap-2">
                      <span className="text-sm text-gray-600 dark:text-gray-400">상태:</span>
                      {validationResult.trustChainValid ? (
                        <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400">
                          <CheckCircle className="w-3 h-3 mr-1" />
                          유효
                        </span>
                      ) : (
                        <span className="inline-flex items-center px-2 py-1 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400">
                          <XCircle className="w-3 h-3 mr-1" />
                          유효하지 않음
                        </span>
                      )}
                    </div>

                    {/* Trust Chain Path Visualization */}
                    {validationResult.trustChainPath && (
                      <div>
                        <span className="text-sm text-gray-600 dark:text-gray-400 mb-2 block">신뢰 체인 경로:</span>
                        <TrustChainVisualization
                          trustChainPath={validationResult.trustChainPath}
                          trustChainValid={validationResult.trustChainValid}
                          compact={false}
                        />
                      </div>
                    )}

                    {/* Message */}
                    {validationResult.trustChainMessage && (
                      <div>
                        <span className="text-sm text-gray-600 dark:text-gray-400">메시지:</span>
                        <p className="text-sm text-gray-700 dark:text-gray-300 mt-1">{validationResult.trustChainMessage}</p>
                      </div>
                    )}
                  </div>
                ) : (
                  <div className="bg-gray-50 dark:bg-gray-700/50 p-4 rounded-lg border border-gray-200 dark:border-gray-600">
                    <p className="text-sm text-gray-600 dark:text-gray-400">
                      이 인증서에 대한 검증 결과가 없습니다.
                    </p>
                  </div>
                )}
              </div>

              {/* Link Certificate Information */}
              {isLinkCertificate(selectedCert) && (
                <div>
                  <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
                    링크 인증서 정보
                  </h3>
                  <div className="bg-cyan-50 dark:bg-cyan-900/20 border border-cyan-200 dark:border-cyan-700 rounded-lg p-4 space-y-3">
                    <div className="flex items-start gap-2">
                      <Shield className="w-5 h-5 text-cyan-600 dark:text-cyan-400 mt-0.5" />
                      <div className="flex-1">
                        <h4 className="text-sm font-semibold text-cyan-800 dark:text-cyan-300 mb-2">
                          목적
                        </h4>
                        <p className="text-xs text-cyan-700 dark:text-cyan-400 leading-relaxed">
                          링크 인증서는 서로 다른 CSCA 인증서 간의 암호화 신뢰 체인을 생성합니다. 일반적으로 다음과 같은 경우에 사용됩니다:
                        </p>
                        <ul className="mt-2 ml-4 space-y-1 text-xs text-cyan-700 dark:text-cyan-400">
                          <li className="list-disc">국가가 CSCA 인프라를 업데이트할 때</li>
                          <li className="list-disc">조직 정보가 변경될 때 (예: 조직명 변경)</li>
                          <li className="list-disc">인증서 정책이 업데이트될 때</li>
                          <li className="list-disc">새로운 암호화 알고리즘으로 마이그레이션할 때</li>
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
                    Master List 서명 인증서 정보
                  </h3>
                  <div className="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-700 rounded-lg p-4 space-y-3">
                    <div className="flex items-start gap-2">
                      <FileText className="w-5 h-5 text-purple-600 dark:text-purple-400 mt-0.5" />
                      <div className="flex-1">
                        <h4 className="text-sm font-semibold text-purple-800 dark:text-purple-300 mb-2">
                          목적
                        </h4>
                        <p className="text-xs text-purple-700 dark:text-purple-400 leading-relaxed">
                          Master List 서명 인증서(MLSC)는 Master List CMS 구조에 디지털 서명하는 데 사용됩니다. 이러한 인증서의 특징:
                        </p>
                        <ul className="mt-2 ml-4 space-y-1 text-xs text-purple-700 dark:text-purple-400">
                          <li className="list-disc">자체 서명 인증서</li>
                          <li className="list-disc">digitalSignature 키 사용 (0x80 비트)</li>
                          <li className="list-disc">Master List CMS에 서명 인증서로 포함됨</li>
                          <li className="list-disc">국가 PKI 기관에서 발급</li>
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
                          <span className="text-purple-600 dark:text-purple-400 font-medium">저장 위치:</span>
                          <span className="text-purple-800 dark:text-purple-300">
                            데이터베이스에는 CSCA 타입으로 저장되지만, LDAP에서는 <code className="bg-purple-100 dark:bg-purple-900/50 px-1 py-0.5 rounded">o=mlsc</code> 조직 단위에 저장
                          </span>
                        </div>
                        <div className="grid grid-cols-[120px_1fr] gap-2">
                          <span className="text-purple-600 dark:text-purple-400 font-medium">자체 서명:</span>
                          <span className="text-purple-800 dark:text-purple-300">
                            {selectedCert.isSelfSigned ? '예 (Subject DN = Issuer DN)' : '아니오'}
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
                    신뢰 체인 계층
                  </h3>
                  <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg border border-gray-200 dark:border-gray-600 overflow-hidden">
                    <TreeViewer
                      data={buildTrustChainTree(validationResult.trustChainPath)}
                      height="200px"
                    />
                  </div>
                  {validationResult.trustChainValid ? (
                    <div className="mt-2 flex items-center gap-2 text-sm text-green-600 dark:text-green-400">
                      <CheckCircle className="w-4 h-4" />
                      <span>신뢰 체인이 유효합니다</span>
                    </div>
                  ) : (
                    <div className="mt-2 flex items-center gap-2 text-sm text-red-600 dark:text-red-400">
                      <XCircle className="w-4 h-4" />
                      <span>신뢰 체인 검증 실패</span>
                    </div>
                  )}
                </div>
              )}

              {/* Certificate Fields Tree */}
              <div>
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">인증서 필드</h3>
                <TreeViewer
                  data={buildCertificateTree(selectedCert)}
                  height="400px"
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
            인증서 저장...
          </button>
          <button
            onClick={() => setShowDetailDialog(false)}
            className="px-4 py-2 rounded-lg text-sm font-medium text-white bg-blue-600 hover:bg-blue-700 transition-colors"
          >
            닫기
          </button>
        </div>
      </div>
    </div>
  );
};

export default CertificateDetailDialog;
