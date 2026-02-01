/**
 * @file DuplicateCertificatesTree.tsx
 * @brief Duplicate Certificates Tree Viewer
 *
 * Displays duplicate certificates grouped by country using TreeViewer component.
 * Shows certificate details and duplicate occurrences in a hierarchical tree structure.
 *
 * @version v2.3.0 (Refactored with TreeViewer)
 * @date 2026-02-01
 */

import React, { useMemo } from 'react';
import type { UploadDuplicate } from '../types';
import { TreeViewer } from './TreeViewer';
import type { TreeNode } from './TreeViewer';

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
  // Format fingerprint: first 8 + ... + last 8
  const formatFingerprint = (fp: string) => {
    if (fp.length <= 16) return fp;
    return `${fp.substring(0, 8)}...${fp.substring(fp.length - 8)}`;
  };

  // Truncate long text for better readability
  const truncateText = (text: string, maxLength: number = 80): string => {
    if (text.length <= maxLength) return text;
    return text.substring(0, maxLength) + '...';
  };

  // Get certificate type icon (must be defined before useMemo)
  function getCertTypeIcon(type: string): string {
    switch (type) {
      case 'CSCA': return 'shield';
      case 'DSC': return 'key';
      case 'DSC_NC': return 'alert-circle';
      case 'MLSC': return 'file-text';
      case 'CRL': return 'file-text';
      default: return 'file-text';
    }
  }

  // Group duplicates by country and certificate
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

  // Convert country groups to TreeNode format
  const treeData = useMemo((): TreeNode[] => {
    return countryGroups.map(group => {
      const certificateNodes: TreeNode[] = group.certificates.map(cert => {
        const duplicateNodes: TreeNode[] = cert.duplicates.map((dup, dupIdx) => ({
          id: `${group.countryCode}-${cert.certificateId}-dup-${dupIdx}`,
          name: `Duplicate #${dupIdx + 1}`,
          value: `${formatFingerprint(dup.fingerprint)} • ${dup.sourceType} • ${dup.sourceCountry}`,
          icon: 'file-text',
        }));

        return {
          id: `${group.countryCode}-${cert.certificateId}`,
          name: `[${cert.certificateType}] ${formatFingerprint(cert.fingerprint)}`,
          value: truncateText(cert.subjectDn, 80),
          icon: getCertTypeIcon(cert.certificateType),
          children: duplicateNodes,
        };
      });

      return {
        id: `country-${group.countryCode}`,
        name: group.countryCode,
        value: `${group.certificates.length}개 인증서, ${group.totalDuplicates}회 중복`,
        icon: `flag-${group.countryCode.toLowerCase()}`,
        children: certificateNodes,
      };
    });
  }, [countryGroups]);

  if (countryGroups.length === 0) {
    return (
      <div className="text-center py-8 text-gray-500 dark:text-gray-400">
        중복된 인증서가 없습니다.
      </div>
    );
  }

  return (
    <div className="h-full flex flex-col">
      {/* Summary */}
      <div className="mb-3 px-2 py-2 bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded">
        <div className="text-sm text-yellow-800 dark:text-yellow-300">
          <strong>{countryGroups.length}개 국가</strong>에서{' '}
          <strong>{countryGroups.reduce((sum, g) => sum + g.certificates.length, 0)}개 인증서</strong>가{' '}
          <strong>{countryGroups.reduce((sum, g) => sum + g.totalDuplicates, 0)}회 중복</strong>됨
        </div>
      </div>

      {/* Tree view */}
      <TreeViewer
        data={treeData}
        height="500px"
      />
    </div>
  );
};
