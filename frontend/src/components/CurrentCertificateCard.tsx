import { FileText, Key, Calendar, Hash, MapPin } from 'lucide-react';
import type { CertificateMetadata, IcaoComplianceStatus } from '@/types';
import { IcaoComplianceBadge } from './IcaoComplianceBadge';
import { cn } from '@/utils/cn';

interface CurrentCertificateCardProps {
  certificate: CertificateMetadata;
  compliance?: IcaoComplianceStatus;
  compact?: boolean;
}

export function CurrentCertificateCard({
  certificate,
  compliance,
  compact = false
}: CurrentCertificateCardProps) {
  const getCertTypeColor = (type: string) => {
    switch (type) {
      case 'CSCA':
        return 'bg-blue-50 dark:bg-blue-900/20 text-blue-700 dark:text-blue-300 border-blue-200 dark:border-blue-800';
      case 'DSC':
        return 'bg-green-50 dark:bg-green-900/20 text-green-700 dark:text-green-300 border-green-200 dark:border-green-800';
      case 'DSC_NC':
        return 'bg-amber-50 dark:bg-amber-900/20 text-amber-700 dark:text-amber-300 border-amber-200 dark:border-amber-800';
      case 'MLSC':
        return 'bg-purple-50 dark:bg-purple-900/20 text-purple-700 dark:text-purple-300 border-purple-200 dark:border-purple-800';
      default:
        return 'bg-gray-50 dark:bg-gray-900/20 text-gray-700 dark:text-gray-300 border-gray-200 dark:border-gray-800';
    }
  };

  if (compact) {
    return (
      <div className="flex items-center gap-3 p-3 bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700">
        <FileText className="w-5 h-5 text-gray-400 flex-shrink-0" />
        <div className="flex-1 min-w-0">
          <div className="flex items-center gap-2 mb-1">
            <span className={cn(
              'text-xs font-medium px-2 py-0.5 rounded border',
              getCertTypeColor(certificate.certificateType)
            )}>
              {certificate.certificateType}
            </span>
            <span className="text-xs text-gray-500 dark:text-gray-400">
              {certificate.countryCode}
            </span>
          </div>
          <div className="text-sm text-gray-900 dark:text-gray-100 truncate">
            {certificate.subjectDn}
          </div>
        </div>
        {compliance && (
          <div className="flex-shrink-0">
            <IcaoComplianceBadge compliance={compliance} size="sm" />
          </div>
        )}
      </div>
    );
  }

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg border border-gray-200 dark:border-gray-700 p-4 space-y-3">
      {/* Header */}
      <div className="flex items-start justify-between gap-3">
        <div className="flex items-center gap-2">
          <FileText className="w-5 h-5 text-gray-400" />
          <span className={cn(
            'text-sm font-medium px-2 py-1 rounded border',
            getCertTypeColor(certificate.certificateType)
          )}>
            {certificate.certificateType}
          </span>
          {certificate.isSelfSigned && (
            <span className="text-xs px-2 py-1 rounded bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-400">
              Self-signed
            </span>
          )}
          {certificate.isLinkCertificate && (
            <span className="text-xs px-2 py-1 rounded bg-cyan-100 dark:bg-cyan-900/20 text-cyan-700 dark:text-cyan-300">
              Link Cert
            </span>
          )}
        </div>
        {compliance && (
          <IcaoComplianceBadge compliance={compliance} size="sm" />
        )}
      </div>

      {/* Subject DN */}
      <div className="space-y-1">
        <div className="text-xs text-gray-500 dark:text-gray-400">Subject DN</div>
        <div className="text-sm text-gray-900 dark:text-gray-100 font-mono break-all">
          {certificate.subjectDn}
        </div>
      </div>

      {/* Grid Info */}
      <div className="grid grid-cols-2 gap-3 text-sm">
        {/* Country */}
        <div className="flex items-center gap-2">
          <MapPin className="w-4 h-4 text-gray-400" />
          <div>
            <div className="text-xs text-gray-500 dark:text-gray-400">Country</div>
            <div className="font-medium text-gray-900 dark:text-gray-100">
              {certificate.countryCode}
            </div>
          </div>
        </div>

        {/* Serial Number */}
        <div className="flex items-center gap-2">
          <Hash className="w-4 h-4 text-gray-400" />
          <div className="min-w-0">
            <div className="text-xs text-gray-500 dark:text-gray-400">Serial</div>
            <div className="font-mono text-xs text-gray-900 dark:text-gray-100 truncate">
              {certificate.serialNumber}
            </div>
          </div>
        </div>

        {/* Algorithm */}
        <div className="flex items-center gap-2">
          <Key className="w-4 h-4 text-gray-400" />
          <div>
            <div className="text-xs text-gray-500 dark:text-gray-400">Algorithm</div>
            <div className="font-medium text-gray-900 dark:text-gray-100">
              {certificate.signatureAlgorithm}
            </div>
          </div>
        </div>

        {/* Key Size */}
        <div className="flex items-center gap-2">
          <Key className="w-4 h-4 text-gray-400" />
          <div>
            <div className="text-xs text-gray-500 dark:text-gray-400">Key Size</div>
            <div className="font-medium text-gray-900 dark:text-gray-100">
              {certificate.publicKeyAlgorithm} {certificate.keySize} bits
            </div>
          </div>
        </div>

        {/* Validity */}
        <div className="col-span-2 flex items-start gap-2">
          <Calendar className="w-4 h-4 text-gray-400 mt-0.5" />
          <div className="flex-1">
            <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">Validity Period</div>
            <div className="flex items-center gap-2 text-xs">
              <span className="text-gray-600 dark:text-gray-400">
                {new Date(certificate.notBefore).toLocaleDateString()}
              </span>
              <span className="text-gray-400">→</span>
              <span className={cn(
                certificate.isExpired
                  ? 'text-red-600 dark:text-red-400 font-medium'
                  : 'text-green-600 dark:text-green-400'
              )}>
                {new Date(certificate.notAfter).toLocaleDateString()}
                {certificate.isExpired && ' (만료됨)'}
              </span>
            </div>
          </div>
        </div>
      </div>

      {/* Key Usage */}
      {certificate.keyUsage.length > 0 && (
        <div className="pt-2 border-t border-gray-200 dark:border-gray-700">
          <div className="text-xs text-gray-500 dark:text-gray-400 mb-2">Key Usage</div>
          <div className="flex flex-wrap gap-1">
            {certificate.keyUsage.map((usage, idx) => (
              <span
                key={idx}
                className="text-xs px-2 py-0.5 rounded bg-blue-50 dark:bg-blue-900/20 text-blue-700 dark:text-blue-300"
              >
                {usage}
              </span>
            ))}
          </div>
        </div>
      )}

      {/* Fingerprint */}
      <div className="pt-2 border-t border-gray-200 dark:border-gray-700">
        <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">SHA-256 Fingerprint</div>
        <div className="font-mono text-xs text-gray-600 dark:text-gray-400 break-all">
          {certificate.fingerprintSha256}
        </div>
      </div>
    </div>
  );
}
