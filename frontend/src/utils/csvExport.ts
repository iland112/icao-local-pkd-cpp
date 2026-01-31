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
