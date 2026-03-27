/**
 * Format numbers in a message string with locale-aware separators.
 * e.g., "DSC 9200/22820 처리 완료 (신규: 9200)" → "DSC 9,200/22,820 처리 완료 (신규: 9,200)"
 *
 * Matches sequences of 4+ digits and applies toLocaleString().
 */
export function formatNumbersInMessage(message: string): string {
  return message.replace(/\d{4,}/g, (match) => Number(match).toLocaleString());
}
