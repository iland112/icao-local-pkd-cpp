/**
 * Centralized date/time formatting utilities.
 *
 * All UI date/time display MUST use these functions to ensure
 * consistent formatting across the entire application.
 *
 * Format: system locale + timezone (via Intl.DateTimeFormat)
 * - formatDateTime: "2026. 03. 07. 16:30:45"
 * - formatDate:     "2026. 03. 07."
 * - formatTime:     "16:30:45"
 */

const DATETIME_OPTIONS: Intl.DateTimeFormatOptions = {
  year: 'numeric',
  month: '2-digit',
  day: '2-digit',
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  hour12: false,
};

const DATE_OPTIONS: Intl.DateTimeFormatOptions = {
  year: 'numeric',
  month: '2-digit',
  day: '2-digit',
};

const TIME_OPTIONS: Intl.DateTimeFormatOptions = {
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  hour12: false,
};

function safeParse(value: string | Date | undefined | null): Date | null {
  if (!value) return null;
  if (value instanceof Date) return isNaN(value.getTime()) ? null : value;
  // PostgreSQL "2025-12-31 09:04:28.432487+09" → needs 'T' for reliable parsing
  const iso = value.replace(' ', 'T').replace(/\+(\d{2})$/, '+$1:00');
  const d = new Date(iso);
  return isNaN(d.getTime()) ? null : d;
}

/** Full datetime: "2026. 03. 07. 16:30:45" */
export function formatDateTime(value: string | Date | undefined | null, fallback = '-'): string {
  const d = safeParse(value);
  return d ? d.toLocaleString('ko-KR', DATETIME_OPTIONS) : fallback;
}

/** Date only: "2026. 03. 07." */
export function formatDate(value: string | Date | undefined | null, fallback = '-'): string {
  const d = safeParse(value);
  return d ? d.toLocaleDateString('ko-KR', DATE_OPTIONS) : fallback;
}

/** Time only: "16:30:45" */
export function formatTime(value: string | Date | undefined | null, fallback = '-'): string {
  const d = safeParse(value);
  return d ? d.toLocaleTimeString('ko-KR', TIME_OPTIONS) : fallback;
}
