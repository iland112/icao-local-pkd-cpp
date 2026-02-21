import type { UploadDuplicate } from '../types';

/**
 * Export duplicate certificates to CSV format
 */
export const exportDuplicatesToCsv = (duplicates: UploadDuplicate[], filename: string = 'duplicate-certificates.csv') => {
  if (duplicates.length === 0) {
    console.warn('No duplicates to export');
    return;
  }

  // CSV headers
  const headers = [
    'Duplicate ID',
    'Certificate ID',
    'Certificate Type',
    'Country',
    'Subject DN',
    'Fingerprint (SHA-256)',
    'Source Type',
    'Source Country',
    'Source File Name',
    'Source Entry DN',
    'Detected At',
    'First Upload ID',
    'First Upload File Name',
    'First Upload Timestamp'
  ];

  // Convert data to CSV rows
  const rows = duplicates.map(dup => [
    dup.id,
    dup.certificateId,
    dup.certificateType,
    dup.country,
    `"${escapeCsvValue(dup.subjectDn)}"`,
    dup.fingerprint,
    dup.sourceType,
    dup.sourceCountry || '',
    dup.sourceFileName ? `"${escapeCsvValue(dup.sourceFileName)}"` : '',
    dup.sourceEntryDn ? `"${escapeCsvValue(dup.sourceEntryDn)}"` : '',
    formatDateForCsv(dup.detectedAt),
    dup.firstUploadId,
    dup.firstUploadFileName ? `"${escapeCsvValue(dup.firstUploadFileName)}"` : '',
    dup.firstUploadTimestamp ? formatDateForCsv(dup.firstUploadTimestamp) : ''
  ]);

  // Combine headers and rows
  const csvContent = [
    headers.join(','),
    ...rows.map(row => row.join(','))
  ].join('\n');

  // Add BOM for proper UTF-8 encoding in Excel
  const BOM = '\uFEFF';
  const blob = new Blob([BOM + csvContent], { type: 'text/csv;charset=utf-8;' });

  // Trigger download
  const link = document.createElement('a');
  const url = URL.createObjectURL(blob);
  link.setAttribute('href', url);
  link.setAttribute('download', filename);
  link.style.visibility = 'hidden';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
};

/**
 * Escape special characters in CSV values
 */
const escapeCsvValue = (value: string): string => {
  // Replace double quotes with two double quotes
  return value.replace(/"/g, '""');
};

/**
 * Format date for CSV export
 */
const formatDateForCsv = (dateStr: string): string => {
  if (!dateStr) return '';

  const date = new Date(dateStr);

  // Format: YYYY-MM-DD HH:MM:SS
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  const seconds = String(date.getSeconds()).padStart(2, '0');

  return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
};

/**
 * Export duplicate statistics to CSV format
 */
/**
 * DSC_NC certificate item for CSV export
 */
interface DscNcCertItem {
  fingerprint: string;
  countryCode: string;
  subjectDn: string;
  issuerDn: string;
  serialNumber: string;
  notBefore: string;
  notAfter: string;
  validity: string;
  signatureAlgorithm?: string;
  publicKeyAlgorithm?: string;
  publicKeySize?: number;
  pkdConformanceCode?: string;
  pkdConformanceText?: string;
  pkdVersion?: string;
}

/**
 * Export DSC_NC non-conformant certificate report to CSV
 */
export const exportDscNcReportToCsv = (
  certificates: DscNcCertItem[],
  filename: string = 'dsc-nc-report.csv'
) => {
  if (certificates.length === 0) {
    console.warn('No DSC_NC certificates to export');
    return;
  }

  const headers = [
    'Country',
    'Subject DN',
    'Issuer DN',
    'Serial Number',
    'Not Before',
    'Not After',
    'Validity',
    'Signature Algorithm',
    'Public Key Algorithm',
    'Public Key Size',
    'Conformance Code',
    'Conformance Text',
    'PKD Version',
    'Fingerprint (SHA-256)'
  ];

  const rows = certificates.map(cert => [
    cert.countryCode,
    `"${escapeCsvValue(cert.subjectDn)}"`,
    `"${escapeCsvValue(cert.issuerDn)}"`,
    cert.serialNumber,
    formatDateForCsv(cert.notBefore),
    formatDateForCsv(cert.notAfter),
    cert.validity,
    cert.signatureAlgorithm || '',
    cert.publicKeyAlgorithm || '',
    cert.publicKeySize != null ? String(cert.publicKeySize) : '',
    cert.pkdConformanceCode || '',
    cert.pkdConformanceText ? `"${escapeCsvValue(cert.pkdConformanceText)}"` : '',
    cert.pkdVersion || '',
    cert.fingerprint
  ]);

  const csvContent = [
    headers.join(','),
    ...rows.map(row => row.join(','))
  ].join('\n');

  const BOM = '\uFEFF';
  const blob = new Blob([BOM + csvContent], { type: 'text/csv;charset=utf-8;' });

  const link = document.createElement('a');
  const url = URL.createObjectURL(blob);
  link.setAttribute('href', url);
  link.setAttribute('download', filename);
  link.style.visibility = 'hidden';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
};

export const exportDuplicateStatisticsToCsv = (
  byType: Record<string, number>,
  totalDuplicates: number,
  filename: string = 'duplicate-statistics.csv'
) => {
  // CSV headers
  const headers = ['Certificate Type', 'Duplicate Count', 'Percentage'];

  // Convert data to CSV rows
  const rows = Object.entries(byType)
    .filter(([_, count]) => count > 0)
    .map(([type, count]) => {
      const percentage = totalDuplicates > 0
        ? ((count / totalDuplicates) * 100).toFixed(2)
        : '0.00';
      return [type, count, `${percentage}%`];
    });

  // Add total row
  rows.push(['TOTAL', totalDuplicates, '100.00%']);

  // Combine headers and rows
  const csvContent = [
    headers.join(','),
    ...rows.map(row => row.join(','))
  ].join('\n');

  // Add BOM for proper UTF-8 encoding in Excel
  const BOM = '\uFEFF';
  const blob = new Blob([BOM + csvContent], { type: 'text/csv;charset=utf-8;' });

  // Trigger download
  const link = document.createElement('a');
  const url = URL.createObjectURL(blob);
  link.setAttribute('href', url);
  link.setAttribute('download', filename);
  link.style.visibility = 'hidden';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
};


export const exportAiAnalysisReportToCsv = (
  items: Array<{
    fingerprint: string;
    certificate_type: string | null;
    country_code: string | null;
    anomaly_score: number;
    anomaly_label: string;
    risk_score: number;
    risk_level: string;
    risk_factors: Record<string, number>;
    anomaly_explanations: string[];
    analyzed_at: string | null;
  }>,
  filename: string = 'ai-analysis-report.csv'
) => {
  if (items.length === 0) return;

  const headers = [
    'Country', 'Certificate Type', 'Anomaly Score', 'Anomaly Label',
    'Risk Score', 'Risk Level', 'Top Risk Factors', 'Anomaly Explanations',
    'Analyzed At', 'Fingerprint (SHA-256)'
  ];

  const rows = items.map(item => [
    item.country_code || '',
    item.certificate_type || '',
    item.anomaly_score.toFixed(3),
    item.anomaly_label,
    item.risk_score.toFixed(1),
    item.risk_level,
    `"${escapeCsvValue(
      Object.entries(item.risk_factors)
        .sort(([, a], [, b]) => b - a)
        .slice(0, 5)
        .map(([k, v]) => `${k}(${v})`)
        .join('; ')
    )}"`,
    `"${escapeCsvValue((item.anomaly_explanations || []).join('; '))}"`,
    item.analyzed_at ? formatDateForCsv(item.analyzed_at) : '',
    item.fingerprint,
  ]);

  const csvContent = [headers.join(','), ...rows.map(r => r.join(','))].join('\n');
  const BOM = '\uFEFF';
  const blob = new Blob([BOM + csvContent], { type: 'text/csv;charset=utf-8;' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = filename;
  link.click();
  URL.revokeObjectURL(link.href);
};

export const exportCrlReportToCsv = (
  crls: Array<{
    countryCode: string;
    issuerDn: string;
    thisUpdate: string;
    nextUpdate: string;
    crlNumber: string;
    status: string;
    revokedCount: number;
    signatureAlgorithm: string;
    fingerprint: string;
    storedInLdap: boolean;
    createdAt: string;
  }>,
  filename: string = 'crl-report.csv'
) => {
  const BOM = '\uFEFF';
  const headers = [
    'Country', 'Issuer DN', 'This Update', 'Next Update',
    'CRL Number', 'Status', 'Revoked Count', 'Signature Algorithm',
    'Fingerprint (SHA-256)', 'Stored in LDAP', 'Created At'
  ];

  const rows = crls.map(crl => [
    crl.countryCode,
    `"${(crl.issuerDn || '').replace(/"/g, '""')}"`,
    crl.thisUpdate,
    crl.nextUpdate || '',
    crl.crlNumber || '',
    crl.status,
    crl.revokedCount.toString(),
    crl.signatureAlgorithm || '',
    crl.fingerprint,
    crl.storedInLdap ? 'Yes' : 'No',
    crl.createdAt
  ]);

  const csvContent = BOM + [headers.join(','), ...rows.map(r => r.join(','))].join('\n');
  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = filename;
  link.click();
  URL.revokeObjectURL(link.href);
};
