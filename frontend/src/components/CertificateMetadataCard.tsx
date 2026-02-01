/**
 * @file CertificateMetadataCard.tsx
 * @brief X.509 Certificate Metadata Display Component
 *
 * Displays detailed X.509 certificate metadata including:
 * - Algorithm Information (version, signature, public key)
 * - Key Usage Extensions
 * - CA Information (Basic Constraints)
 * - Identifiers (SKI, AKI)
 * - Distribution Points (CRL, OCSP)
 *
 * @date 2026-02-01
 * @version v2.3.0
 */

import React from 'react';
import { Shield, Key, Lock, Link, AlertCircle, CheckCircle, Copy } from 'lucide-react';

interface CertificateMetadataCardProps {
  certificate: {
    // X.509 Metadata
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
  };
}

const CertificateMetadataCard: React.FC<CertificateMetadataCardProps> = ({ certificate }) => {
  const [copiedField, setCopiedField] = React.useState<string | null>(null);

  // Copy to clipboard helper
  const copyToClipboard = (text: string, fieldName: string) => {
    navigator.clipboard.writeText(text);
    setCopiedField(fieldName);
    setTimeout(() => setCopiedField(null), 2000);
  };

  // Format version number
  const formatVersion = (version: number) => {
    const versionMap: { [key: number]: string } = {
      0: 'v1',
      1: 'v2',
      2: 'v3'
    };
    return versionMap[version] || `v${version + 1}`;
  };

  // If no metadata available, show placeholder
  if (!certificate.version && !certificate.signatureAlgorithm && !certificate.publicKeyAlgorithm) {
    return (
      <div className="bg-gray-50 dark:bg-gray-800/50 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
        <div className="flex items-center gap-2 text-gray-500 dark:text-gray-400">
          <AlertCircle className="w-5 h-5" />
          <p className="text-sm">X.509 메타데이터를 사용할 수 없습니다.</p>
        </div>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* Algorithm Information */}
      <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
        <div className="flex items-center gap-2 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
          <Shield className="w-4 h-4 text-blue-500" />
          <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">알고리즘 정보</h4>
        </div>
        <div className="space-y-2">
          {certificate.version !== undefined && (
            <div className="grid grid-cols-[140px_1fr] gap-2">
              <span className="text-sm text-gray-600 dark:text-gray-400">Version:</span>
              <span className="text-sm text-gray-900 dark:text-white font-medium">
                {formatVersion(certificate.version)}
              </span>
            </div>
          )}
          {certificate.signatureAlgorithm && (
            <div className="grid grid-cols-[140px_1fr] gap-2">
              <span className="text-sm text-gray-600 dark:text-gray-400">Signature Algorithm:</span>
              <span className="text-sm text-gray-900 dark:text-white font-mono">
                {certificate.signatureAlgorithm}
              </span>
            </div>
          )}
          {certificate.signatureHashAlgorithm && (
            <div className="grid grid-cols-[140px_1fr] gap-2">
              <span className="text-sm text-gray-600 dark:text-gray-400">Hash Algorithm:</span>
              <span className="text-sm text-gray-900 dark:text-white font-mono">
                {certificate.signatureHashAlgorithm}
              </span>
            </div>
          )}
        </div>
      </div>

      {/* Public Key Information */}
      {(certificate.publicKeyAlgorithm || certificate.publicKeySize || certificate.publicKeyCurve) && (
        <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
            <Key className="w-4 h-4 text-green-500" />
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">공개키 정보</h4>
          </div>
          <div className="space-y-2">
            {certificate.publicKeyAlgorithm && (
              <div className="grid grid-cols-[140px_1fr] gap-2">
                <span className="text-sm text-gray-600 dark:text-gray-400">Algorithm:</span>
                <span className="text-sm text-gray-900 dark:text-white font-mono">
                  {certificate.publicKeyAlgorithm}
                </span>
              </div>
            )}
            {certificate.publicKeySize && (
              <div className="grid grid-cols-[140px_1fr] gap-2">
                <span className="text-sm text-gray-600 dark:text-gray-400">Key Size:</span>
                <span className="text-sm text-gray-900 dark:text-white font-mono">
                  {certificate.publicKeySize} bits
                </span>
              </div>
            )}
            {certificate.publicKeyCurve && (
              <div className="grid grid-cols-[140px_1fr] gap-2">
                <span className="text-sm text-gray-600 dark:text-gray-400">Curve:</span>
                <span className="text-sm text-gray-900 dark:text-white font-mono">
                  {certificate.publicKeyCurve}
                </span>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Key Usage */}
      {(certificate.keyUsage && certificate.keyUsage.length > 0) ||
       (certificate.extendedKeyUsage && certificate.extendedKeyUsage.length > 0) ? (
        <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
            <Lock className="w-4 h-4 text-purple-500" />
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">Key Usage</h4>
          </div>
          <div className="space-y-3">
            {certificate.keyUsage && certificate.keyUsage.length > 0 && (
              <div>
                <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">Key Usage:</span>
                <div className="flex flex-wrap gap-1.5">
                  {certificate.keyUsage.map((usage, index) => (
                    <span
                      key={index}
                      className="inline-flex items-center px-2 py-1 text-xs font-medium rounded bg-purple-100 dark:bg-purple-900/30 text-purple-800 dark:text-purple-300 border border-purple-200 dark:border-purple-700"
                    >
                      {usage}
                    </span>
                  ))}
                </div>
              </div>
            )}
            {certificate.extendedKeyUsage && certificate.extendedKeyUsage.length > 0 && (
              <div>
                <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">Extended Key Usage:</span>
                <div className="flex flex-wrap gap-1.5">
                  {certificate.extendedKeyUsage.map((usage, index) => (
                    <span
                      key={index}
                      className="inline-flex items-center px-2 py-1 text-xs font-medium rounded bg-purple-50 dark:bg-purple-900/20 text-purple-700 dark:text-purple-400 border border-purple-200 dark:border-purple-800"
                    >
                      {usage}
                    </span>
                  ))}
                </div>
              </div>
            )}
          </div>
        </div>
      ) : null}

      {/* CA Information */}
      {(certificate.isCA !== undefined || certificate.pathLenConstraint !== undefined) && (
        <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
            <Shield className="w-4 h-4 text-cyan-500" />
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">CA 정보</h4>
          </div>
          <div className="space-y-2">
            {certificate.isCA !== undefined && (
              <div className="grid grid-cols-[140px_1fr] gap-2">
                <span className="text-sm text-gray-600 dark:text-gray-400">Is CA:</span>
                <span className="flex items-center gap-1">
                  {certificate.isCA ? (
                    <>
                      <CheckCircle className="w-4 h-4 text-green-500" />
                      <span className="text-sm text-green-700 dark:text-green-400 font-medium">TRUE</span>
                    </>
                  ) : (
                    <>
                      <AlertCircle className="w-4 h-4 text-gray-400" />
                      <span className="text-sm text-gray-600 dark:text-gray-400">FALSE</span>
                    </>
                  )}
                </span>
              </div>
            )}
            {certificate.pathLenConstraint !== undefined && (
              <div className="grid grid-cols-[140px_1fr] gap-2">
                <span className="text-sm text-gray-600 dark:text-gray-400">Path Length:</span>
                <span className="text-sm text-gray-900 dark:text-white">
                  {certificate.pathLenConstraint}
                </span>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Identifiers */}
      {(certificate.subjectKeyIdentifier || certificate.authorityKeyIdentifier) && (
        <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
            <Key className="w-4 h-4 text-amber-500" />
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">식별자</h4>
          </div>
          <div className="space-y-2">
            {certificate.subjectKeyIdentifier && (
              <div className="grid grid-cols-[140px_1fr_auto] gap-2 items-center">
                <span className="text-sm text-gray-600 dark:text-gray-400">Subject Key ID:</span>
                <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                  {certificate.subjectKeyIdentifier}
                </span>
                <button
                  onClick={() => copyToClipboard(certificate.subjectKeyIdentifier!, 'ski')}
                  className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded transition-colors"
                  title="Copy to clipboard"
                >
                  {copiedField === 'ski' ? (
                    <CheckCircle className="w-4 h-4 text-green-500" />
                  ) : (
                    <Copy className="w-4 h-4 text-gray-400" />
                  )}
                </button>
              </div>
            )}
            {certificate.authorityKeyIdentifier && (
              <div className="grid grid-cols-[140px_1fr_auto] gap-2 items-center">
                <span className="text-sm text-gray-600 dark:text-gray-400">Authority Key ID:</span>
                <span className="text-xs text-gray-900 dark:text-white font-mono break-all">
                  {certificate.authorityKeyIdentifier}
                </span>
                <button
                  onClick={() => copyToClipboard(certificate.authorityKeyIdentifier!, 'aki')}
                  className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded transition-colors"
                  title="Copy to clipboard"
                >
                  {copiedField === 'aki' ? (
                    <CheckCircle className="w-4 h-4 text-green-500" />
                  ) : (
                    <Copy className="w-4 h-4 text-gray-400" />
                  )}
                </button>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Distribution Points */}
      {((certificate.crlDistributionPoints && certificate.crlDistributionPoints.length > 0) ||
        certificate.ocspResponderUrl) && (
        <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-3 pb-2 border-b border-gray-200 dark:border-gray-700">
            <Link className="w-4 h-4 text-indigo-500" />
            <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">배포 지점</h4>
          </div>
          <div className="space-y-3">
            {certificate.crlDistributionPoints && certificate.crlDistributionPoints.length > 0 && (
              <div>
                <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">CRL Distribution Points:</span>
                <div className="space-y-1">
                  {certificate.crlDistributionPoints.map((url, index) => (
                    <a
                      key={index}
                      href={url}
                      target="_blank"
                      rel="noopener noreferrer"
                      className="block text-xs text-blue-600 dark:text-blue-400 hover:underline break-all"
                    >
                      {url}
                    </a>
                  ))}
                </div>
              </div>
            )}
            {certificate.ocspResponderUrl && (
              <div>
                <span className="text-xs text-gray-500 dark:text-gray-400 block mb-1">OCSP Responder:</span>
                <a
                  href={certificate.ocspResponderUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="block text-xs text-blue-600 dark:text-blue-400 hover:underline break-all"
                >
                  {certificate.ocspResponderUrl}
                </a>
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
};

export default CertificateMetadataCard;
