import { describe, it, expect } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ProcessingErrorsPanel } from '../ProcessingErrorsPanel';
import type { ProcessingError } from '@/types';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, unknown>) => {
      const map: Record<string, string> = {
        'upload:processingErrors.title': '처리 오류',
        'upload:processingErrors.overflow': `...외 ${opts?.hidden ?? 0}건 더 있음`,
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

// formatTime returns the timestamp string as-is in tests
vi.mock('@/utils/dateFormat', () => ({
  formatTime: (t: string) => t,
  formatDate: (t: string) => t,
  formatDateTime: (t: string) => t,
}));

const makeError = (overrides: Partial<ProcessingError> = {}): ProcessingError => ({
  timestamp: '2026-01-01T00:00:00Z',
  errorType: 'CERT_PARSE_FAILED',
  entryDn: 'cn=test,o=dsc',
  certificateDn: 'cn=test',
  countryCode: 'KR',
  certificateType: 'DSC',
  message: 'Failed to parse certificate',
  ...overrides,
});

describe('ProcessingErrorsPanel', () => {
  it('should render nothing when totalErrorCount is 0', () => {
    const { container } = render(
      <ProcessingErrorsPanel
        errors={[]}
        totalErrorCount={0}
        parseErrorCount={0}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render the panel when totalErrorCount > 0', () => {
    render(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );
    expect(screen.getByText('처리 오류')).toBeInTheDocument();
    expect(screen.getAllByText('1').length).toBeGreaterThan(0);
  });

  it('should auto-expand on first error (totalErrorCount transitions 0→N)', () => {
    const { rerender } = render(
      <ProcessingErrorsPanel
        errors={[]}
        totalErrorCount={0}
        parseErrorCount={0}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    // Panel is not visible yet (totalErrorCount=0 → returns null)
    rerender(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    // After first error, error list should be auto-expanded and show error message
    expect(screen.getByText('Failed to parse certificate')).toBeInTheDocument();
  });

  it('should toggle expand/collapse when header button is clicked', () => {
    render(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    // Auto-expanded on mount with error present
    expect(screen.getByText('Failed to parse certificate')).toBeInTheDocument();

    // Click to collapse
    fireEvent.click(screen.getByText('처리 오류').closest('button')!);
    expect(screen.queryByText('Failed to parse certificate')).not.toBeInTheDocument();

    // Click to expand again
    fireEvent.click(screen.getByText('처리 오류').closest('button')!);
    expect(screen.getByText('Failed to parse certificate')).toBeInTheDocument();
  });

  it('should show parse error badge when parseErrorCount > 0', () => {
    const { container } = render(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    // The parse error count badge (orange) should be present
    const orangeBadges = container.querySelectorAll('.bg-orange-50');
    expect(orangeBadges.length).toBeGreaterThan(0);
  });

  it('should show DB error badge when dbSaveErrorCount > 0', () => {
    const { container } = render(
      <ProcessingErrorsPanel
        errors={[makeError({ errorType: 'DB_SAVE_FAILED', message: 'DB error' })]}
        totalErrorCount={1}
        parseErrorCount={0}
        dbSaveErrorCount={1}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    const redBadges = container.querySelectorAll('.bg-red-50');
    expect(redBadges.length).toBeGreaterThan(0);
  });

  it('should show LDAP error badge when ldapSaveErrorCount > 0', () => {
    const { container } = render(
      <ProcessingErrorsPanel
        errors={[makeError({ errorType: 'LDAP_SAVE_FAILED', message: 'LDAP error' })]}
        totalErrorCount={1}
        parseErrorCount={0}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={1}
        isProcessing={false}
      />
    );

    const purpleBadges = container.querySelectorAll('.bg-purple-50');
    expect(purpleBadges.length).toBeGreaterThan(0);
  });

  it('should show spinner when isProcessing is true', () => {
    const { container } = render(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={true}
      />
    );

    expect(container.querySelector('.animate-spin')).toBeInTheDocument();
  });

  it('should not show spinner when isProcessing is false', () => {
    const { container } = render(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    expect(container.querySelector('.animate-spin')).not.toBeInTheDocument();
  });

  it('should display country code and certificate type badges in error rows', () => {
    render(
      <ProcessingErrorsPanel
        errors={[makeError({ countryCode: 'DE', certificateType: 'CSCA' })]}
        totalErrorCount={1}
        parseErrorCount={1}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    expect(screen.getByText('DE')).toBeInTheDocument();
    expect(screen.getByText('CSCA')).toBeInTheDocument();
  });

  it('should show overflow indicator when totalErrorCount > errors.length', () => {
    render(
      <ProcessingErrorsPanel
        errors={[makeError()]}
        totalErrorCount={10}
        parseErrorCount={10}
        dbSaveErrorCount={0}
        ldapSaveErrorCount={0}
        isProcessing={false}
      />
    );

    expect(screen.getByText(/더 있음/)).toBeInTheDocument();
  });
});
