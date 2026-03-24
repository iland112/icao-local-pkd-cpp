import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { RealTimeStatisticsPanel } from '../RealTimeStatisticsPanel';
import type { ValidationStatistics } from '@/types';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => {
      const map: Record<string, string> = {
        'upload:statistics.realTimeTitle': '실시간 통계',
        'upload:fileUpload.processing': '처리 중',
        'upload:validationSummary.title': '검증 요약',
        'upload:validationSummary.trustChain': 'Trust Chain',
        'upload:validationSummary.icaoCompliance': 'ICAO 준수',
        'upload:validationSummary.cscaNotFound': 'CSCA 미발견',
        'upload:statistics.totalFailed': '실패',
        'upload:statistics.invalidCount': '유효하지 않음',
        'upload:fileUpload.expiredValid': '만료(서명유효)',
        'upload:dashboard.pendingCount': '대기',
        'common:status.valid': '유효',
        'common:status.expired': '만료',
        'common:label.validPeriod': '유효기간',
        'sync:dashboard.error': '오류',
        'sync:reconciliation.successCount': '성공',
        'dashboard:successRate': '성공률',
        'certificate:doc9303.compliant': '준수',
        'certificate:doc9303.nonCompliant': '미준수',
        'certificate:detail.certificateType': '인증서 유형',
        'certificate:detail.signatureAlgorithm': '서명 알고리즘',
        'ai:forensic.categories.keySize': '키 크기',
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

// ValidationSummaryPanel is the real dependency — mock it for isolation
vi.mock('../ValidationSummaryPanel', () => ({
  ValidationSummaryPanel: ({
    data,
    title,
    isProcessing,
  }: {
    data: unknown;
    title: string;
    isProcessing: boolean;
  }) => (
    <div data-testid="validation-summary-panel">
      <span data-testid="title">{title}</span>
      <span data-testid="is-processing">{isProcessing ? 'true' : 'false'}</span>
      <span data-testid="valid-count">{(data as Record<string, number>).validCount}</span>
    </div>
  ),
}));

const makeStats = (overrides: Partial<ValidationStatistics> = {}): ValidationStatistics => ({
  totalCertificates: 100,
  processedCount: 80,
  validCount: 70,
  invalidCount: 5,
  pendingCount: 5,
  trustChainValidCount: 65,
  trustChainInvalidCount: 5,
  cscaNotFoundCount: 10,
  expiredCount: 3,
  notYetValidCount: 0,
  validPeriodCount: 77,
  revokedCount: 0,
  notRevokedCount: 70,
  crlNotCheckedCount: 10,
  icaoCompliantCount: 60,
  icaoNonCompliantCount: 5,
  icaoWarningCount: 2,
  complianceViolations: {},
  signatureAlgorithms: { 'SHA256withRSA': 70 },
  keySizes: { '2048': 70 },
  certificateTypes: { DSC: 70, CSCA: 10 },
  ...overrides,
});

describe('RealTimeStatisticsPanel', () => {
  it('should render ValidationSummaryPanel', () => {
    render(
      <RealTimeStatisticsPanel
        statistics={makeStats()}
        isProcessing={false}
      />
    );
    expect(screen.getByTestId('validation-summary-panel')).toBeInTheDocument();
  });

  it('should pass realTimeTitle to ValidationSummaryPanel', () => {
    render(
      <RealTimeStatisticsPanel
        statistics={makeStats()}
        isProcessing={false}
      />
    );
    expect(screen.getByTestId('title').textContent).toBe('실시간 통계');
  });

  it('should forward isProcessing=true to ValidationSummaryPanel', () => {
    render(
      <RealTimeStatisticsPanel
        statistics={makeStats()}
        isProcessing={true}
      />
    );
    expect(screen.getByTestId('is-processing').textContent).toBe('true');
  });

  it('should forward isProcessing=false to ValidationSummaryPanel', () => {
    render(
      <RealTimeStatisticsPanel
        statistics={makeStats()}
        isProcessing={false}
      />
    );
    expect(screen.getByTestId('is-processing').textContent).toBe('false');
  });

  it('should map validCount from statistics to ValidationSummaryPanel data', () => {
    render(
      <RealTimeStatisticsPanel
        statistics={makeStats({ validCount: 42 })}
        isProcessing={false}
      />
    );
    expect(screen.getByTestId('valid-count').textContent).toBe('42');
  });

  it('should render without uploadId (optional prop)', () => {
    expect(() =>
      render(
        <RealTimeStatisticsPanel
          statistics={makeStats()}
          isProcessing={false}
        />
      )
    ).not.toThrow();
  });

  it('should render with uploadId prop', () => {
    expect(() =>
      render(
        <RealTimeStatisticsPanel
          statistics={makeStats()}
          isProcessing={false}
          uploadId="upload-abc-123"
        />
      )
    ).not.toThrow();
  });
});
