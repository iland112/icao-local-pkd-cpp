import React, { useMemo } from 'react';
import { ChevronRight, ChevronDown, ChevronsDown, ChevronsUp } from 'lucide-react';
import type { UploadDuplicate } from '../types';
import { getFlagSvgPath } from '../utils/countryCode';

interface DuplicateTreeNode {
  certificateId: string;
  fingerprint: string;
  subjectDn: string;
  certificateType: string;
  country: string;
  duplicates: UploadDuplicate[];
}

interface CountryGroup {
  countryCode: string;
  certificates: DuplicateTreeNode[];
  totalDuplicates: number;
}

interface Props {
  duplicates: UploadDuplicate[];
}

export const DuplicateCertificatesTree: React.FC<Props> = ({ duplicates }) => {
  const [expandedCountries, setExpandedCountries] = React.useState<Set<string>>(new Set());
  const [expandedNodes, setExpandedNodes] = React.useState<Set<string>>(new Set());

  const countryGroups = useMemo((): CountryGroup[] => {
    const certMap = new Map<string, DuplicateTreeNode>();

    duplicates.forEach(dup => {
      const key = dup.certificateId;
      if (!certMap.has(key)) {
        certMap.set(key, {
          certificateId: dup.certificateId,
          fingerprint: dup.fingerprint,
          subjectDn: dup.subjectDn,
          certificateType: dup.certificateType,
          country: dup.country,
          duplicates: []
        });
      }
      certMap.get(key)!.duplicates.push(dup);
    });

    const countryMap = new Map<string, DuplicateTreeNode[]>();
    certMap.forEach(cert => {
      const country = cert.country || 'Unknown';
      if (!countryMap.has(country)) {
        countryMap.set(country, []);
      }
      countryMap.get(country)!.push(cert);
    });

    return Array.from(countryMap.entries())
      .map(([countryCode, certificates]) => ({
        countryCode,
        certificates: certificates.sort((a, b) => a.subjectDn.localeCompare(b.subjectDn)),
        totalDuplicates: certificates.reduce((sum, cert) => sum + cert.duplicates.length, 0)
      }))
      .sort((a, b) => b.totalDuplicates - a.totalDuplicates);
  }, [duplicates]);

  const toggleCountry = (countryCode: string) => {
    setExpandedCountries(prev => {
      const next = new Set(prev);
      if (next.has(countryCode)) {
        next.delete(countryCode);
      } else {
        next.add(countryCode);
      }
      return next;
    });
  };

  const toggleNode = (certificateId: string) => {
    setExpandedNodes(prev => {
      const next = new Set(prev);
      if (next.has(certificateId)) {
        next.delete(certificateId);
      } else {
        next.add(certificateId);
      }
      return next;
    });
  };

  const expandAll = () => {
    const allCountries = new Set(countryGroups.map(g => g.countryCode));
    const allCerts = new Set(countryGroups.flatMap(g => g.certificates.map(c => c.certificateId)));
    setExpandedCountries(allCountries);
    setExpandedNodes(allCerts);
  };

  const collapseAll = () => {
    setExpandedCountries(new Set());
    setExpandedNodes(new Set());
  };

  const isAllExpanded = expandedCountries.size === countryGroups.length &&
    expandedNodes.size === countryGroups.reduce((sum, g) => sum + g.certificates.length, 0);

  // Format fingerprint: first 8 + ... + last 8
  const formatFingerprint = (fp: string) => {
    if (fp.length <= 16) return fp;
    return `${fp.substring(0, 8)}...${fp.substring(fp.length - 8)}`;
  };

  const getCertTypeBadge = (type: string) => {
    const colors: Record<string, string> = {
      CSCA: 'text-blue-600 dark:text-blue-400',
      DSC: 'text-green-600 dark:text-green-400',
      DSC_NC: 'text-orange-600 dark:text-orange-400',
      MLSC: 'text-purple-600 dark:text-purple-400',
      CRL: 'text-red-600 dark:text-red-400'
    };
    return <span className={`font-mono text-xs ${colors[type] || 'text-gray-600'}`}>[{type}]</span>;
  };

  if (countryGroups.length === 0) {
    return (
      <div className="text-center py-8 text-gray-500 dark:text-gray-400">
        중복된 인증서가 없습니다.
      </div>
    );
  }

  return (
    <div className="h-full flex flex-col">
      {/* Control buttons */}
      <div className="flex justify-end gap-2 mb-3 px-2">
        <button
          onClick={isAllExpanded ? collapseAll : expandAll}
          className="px-2 py-1 text-xs font-medium text-gray-700 dark:text-gray-300 bg-white dark:bg-gray-800 border border-gray-300 dark:border-gray-600 rounded hover:bg-gray-50 dark:hover:bg-gray-750 transition-colors flex items-center gap-1.5"
        >
          {isAllExpanded ? (
            <>
              <ChevronsUp className="w-3 h-3" />
              모두 접기
            </>
          ) : (
            <>
              <ChevronsDown className="w-3 h-3" />
              모두 펼치기
            </>
          )}
        </button>
      </div>

      {/* Tree view - scrollable */}
      <div className="flex-1 overflow-y-auto px-2 font-mono text-sm">
        {countryGroups.map((group) => {
          const isCountryExpanded = expandedCountries.has(group.countryCode);

          return (
            <div key={group.countryCode} className="mb-2">
              {/* Country node */}
              <div
                className="flex items-center gap-2 py-1 px-2 hover:bg-gray-100 dark:hover:bg-gray-800 rounded cursor-pointer group"
                onClick={() => toggleCountry(group.countryCode)}
              >
                <div className="flex items-center gap-1.5">
                  {isCountryExpanded ? (
                    <ChevronDown className="w-4 h-4 text-gray-500" />
                  ) : (
                    <ChevronRight className="w-4 h-4 text-gray-500" />
                  )}
                  {getFlagSvgPath(group.countryCode) && (
                    <img
                      src={getFlagSvgPath(group.countryCode)}
                      alt={group.countryCode}
                      className="w-5 h-4 rounded border border-gray-300 dark:border-gray-600"
                    />
                  )}
                </div>
                <span className="font-bold text-gray-900 dark:text-gray-100">{group.countryCode}</span>
                <span className="text-xs text-gray-500 dark:text-gray-400">
                  ({group.certificates.length}개, {group.totalDuplicates}회 중복)
                </span>
              </div>

              {/* Certificates */}
              {isCountryExpanded && (
                <div className="ml-4 border-l-2 border-gray-300 dark:border-gray-600 pl-2 mt-1">
                  {group.certificates.map((cert, certIdx) => {
                    const isExpanded = expandedNodes.has(cert.certificateId);
                    const isLastCert = certIdx === group.certificates.length - 1;

                    return (
                      <div key={cert.certificateId} className={`relative ${!isLastCert ? 'mb-2' : ''}`}>
                        {/* Certificate node */}
                        <div
                          className="flex items-start gap-2 py-1 px-2 hover:bg-gray-100 dark:hover:bg-gray-800 rounded cursor-pointer group"
                          onClick={() => toggleNode(cert.certificateId)}
                        >
                          {/* Tree connector */}
                          <div className="flex items-center">
                            <span className="text-gray-400 dark:text-gray-600">
                              {isLastCert ? '└─' : '├─'}
                            </span>
                            {isExpanded ? (
                              <ChevronDown className="w-3 h-3 text-gray-500 ml-1" />
                            ) : (
                              <ChevronRight className="w-3 h-3 text-gray-500 ml-1" />
                            )}
                          </div>

                          <div className="flex-1 min-w-0">
                            <div className="flex items-center gap-2">
                              {getCertTypeBadge(cert.certificateType)}
                              <span className="text-xs text-gray-600 dark:text-gray-400 font-mono">
                                {formatFingerprint(cert.fingerprint)}
                              </span>
                            </div>
                            <div className="text-xs text-gray-700 dark:text-gray-300 truncate mt-0.5">
                              {cert.subjectDn}
                            </div>
                            <div className="text-xs text-orange-600 dark:text-orange-400 mt-0.5">
                              중복 {cert.duplicates.length}회
                            </div>
                          </div>
                        </div>

                        {/* Duplicates */}
                        {isExpanded && (
                          <div className={`ml-6 ${isLastCert ? 'pl-2' : 'border-l-2 border-gray-300 dark:border-gray-600 pl-2'} mt-1`}>
                            {cert.duplicates.map((dup, dupIdx) => {
                              const isLastDup = dupIdx === cert.duplicates.length - 1;

                              return (
                                <div
                                  key={dup.id}
                                  className={`flex items-start gap-2 py-1 px-2 hover:bg-gray-50 dark:hover:bg-gray-900 rounded ${!isLastDup ? 'mb-1' : ''}`}
                                >
                                  {/* Tree connector */}
                                  <span className="text-gray-400 dark:text-gray-600 text-xs">
                                    {isLastDup ? '└─' : '├─'}
                                  </span>

                                  <div className="flex-1 min-w-0">
                                    <div className="flex items-center gap-2">
                                      <span className="text-xs text-gray-500 dark:text-gray-400">
                                        #{dupIdx + 1}
                                      </span>
                                      <span className="text-xs text-gray-600 dark:text-gray-400 font-mono">
                                        {formatFingerprint(dup.fingerprint)}
                                      </span>
                                    </div>
                                    <div className="text-xs text-gray-600 dark:text-gray-400 mt-0.5">
                                      {dup.sourceType} • {dup.sourceCountry}
                                    </div>
                                  </div>
                                </div>
                              );
                            })}
                          </div>
                        )}
                      </div>
                    );
                  })}
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
};
