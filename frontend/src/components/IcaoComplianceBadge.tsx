import { Shield, ShieldCheck, ShieldAlert, ShieldX } from 'lucide-react';
import type { IcaoComplianceStatus } from '@/types';
import { cn } from '@/utils/cn';

interface IcaoComplianceBadgeProps {
  compliance: IcaoComplianceStatus;
  size?: 'sm' | 'md' | 'lg';
  showDetails?: boolean;
}

export function IcaoComplianceBadge({
  compliance,
  size = 'md',
  showDetails = false
}: IcaoComplianceBadgeProps) {
  const getIcon = () => {
    switch (compliance.complianceLevel) {
      case 'CONFORMANT':
        return <ShieldCheck className={cn(
          size === 'sm' && 'w-3 h-3',
          size === 'md' && 'w-4 h-4',
          size === 'lg' && 'w-5 h-5'
        )} />;
      case 'WARNING':
        return <ShieldAlert className={cn(
          size === 'sm' && 'w-3 h-3',
          size === 'md' && 'w-4 h-4',
          size === 'lg' && 'w-5 h-5'
        )} />;
      case 'NON_CONFORMANT':
        return <ShieldX className={cn(
          size === 'sm' && 'w-3 h-3',
          size === 'md' && 'w-4 h-4',
          size === 'lg' && 'w-5 h-5'
        )} />;
      default:
        return <Shield className={cn(
          size === 'sm' && 'w-3 h-3',
          size === 'md' && 'w-4 h-4',
          size === 'lg' && 'w-5 h-5'
        )} />;
    }
  };

  const getColor = () => {
    switch (compliance.complianceLevel) {
      case 'CONFORMANT':
        return 'text-green-600 dark:text-green-400 bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800';
      case 'WARNING':
        return 'text-yellow-600 dark:text-yellow-400 bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800';
      case 'NON_CONFORMANT':
        return 'text-red-600 dark:text-red-400 bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800';
      default:
        return 'text-gray-600 dark:text-gray-400 bg-gray-50 dark:bg-gray-900/20 border-gray-200 dark:border-gray-800';
    }
  };

  const getLabel = () => {
    switch (compliance.complianceLevel) {
      case 'CONFORMANT':
        return 'ICAO 준수';
      case 'WARNING':
        return 'ICAO 경고';
      case 'NON_CONFORMANT':
        return 'ICAO 미준수';
      default:
        return 'ICAO 미확인';
    }
  };

  return (
    <div className="space-y-2">
      <div
        className={cn(
          'inline-flex items-center gap-2 rounded-full border px-3 py-1',
          getColor(),
          size === 'sm' && 'text-xs',
          size === 'md' && 'text-sm',
          size === 'lg' && 'text-base'
        )}
      >
        {getIcon()}
        <span className="font-medium">{getLabel()}</span>
      </div>

      {showDetails && compliance.violations.length > 0 && (
        <div className="mt-2 text-xs text-gray-600 dark:text-gray-400 space-y-1">
          <div className="font-medium">위반 항목:</div>
          <ul className="list-disc list-inside space-y-0.5">
            {compliance.violations.map((violation, idx) => (
              <li key={idx}>{violation}</li>
            ))}
          </ul>
          {compliance.pkdConformanceCode && (
            <div className="mt-2 font-mono text-xs text-red-600 dark:text-red-400">
              {compliance.pkdConformanceCode}
            </div>
          )}
        </div>
      )}

      {showDetails && (
        <div className="mt-3 grid grid-cols-2 gap-2 text-xs">
          <div className={cn(
            'flex items-center gap-1',
            compliance.keyUsageCompliant ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
          )}>
            {compliance.keyUsageCompliant ? '✓' : '✗'} Key Usage
          </div>
          <div className={cn(
            'flex items-center gap-1',
            compliance.algorithmCompliant ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
          )}>
            {compliance.algorithmCompliant ? '✓' : '✗'} Algorithm
          </div>
          <div className={cn(
            'flex items-center gap-1',
            compliance.keySizeCompliant ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
          )}>
            {compliance.keySizeCompliant ? '✓' : '✗'} Key Size
          </div>
          <div className={cn(
            'flex items-center gap-1',
            compliance.validityPeriodCompliant ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
          )}>
            {compliance.validityPeriodCompliant ? '✓' : '✗'} Validity
          </div>
          <div className={cn(
            'flex items-center gap-1',
            compliance.dnFormatCompliant ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
          )}>
            {compliance.dnFormatCompliant ? '✓' : '✗'} DN Format
          </div>
          <div className={cn(
            'flex items-center gap-1',
            compliance.extensionsCompliant ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'
          )}>
            {compliance.extensionsCompliant ? '✓' : '✗'} Extensions
          </div>
        </div>
      )}
    </div>
  );
}
