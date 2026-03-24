import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ValidationSummaryPanel } from '../ValidationSummaryPanel';
import type { ValidationSummaryData } from '../ValidationSummaryPanel';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, unknown>) => {
      const map: Record<string, string> = {
        'upload:validationSummary.title': '검증 요약',
        'upload:validationSummary.trustChain': 'Trust Chain',
        'upload:validationSummary.icaoCompliance': 'ICAO 준수',
        'upload:validationSummary.cscaNotFound': 'CSCA 미발견',
        'upload:validationSummary.fileTotal': '전체',
        'upload:validationSummary.newlyProcessed': '신규',
        'upload:validationSummary.validationLabel': '검증',
        'upload:validationSummary.revoked': '폐기',
        'upload:validationSummary.violationAlgorithm': '알고리즘',
        'upload:validationSummary.violationKeySize': '키 크기',
        'upload:validationSummary.violationValidityPeriod': '유효기간',
        'upload:validationSummary.violationDnFormat': 'DN 형식',
        'upload:validationSummary.violationExtensions': '확장',
        'upload:validationSummary.processedOf': `처리됨/${opts?.num ?? '?'}`,
        'upload:validationSummary.fullFileAnalysis': `전체 ${opts?.num ?? '?'}개`,
        'upload:validationSummary.duplicatePercent': `중복 (${opts?.pct ?? 0}%)`,
        'upload:fileUpload.processing': '처리 중',
        'upload:fileUpload.expiredValid': '만료(서명유효)',
        'upload:dashboard.pendingCount': '대기',
        'upload:statistics.invalidCount': '유효하지 않음',
        'upload:statistics.realTimeTitle': '실시간 통계',
        'upload:statistics.totalFailed': '실패',
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

vi.mock('../IcaoViolationDetailDialog', () => ({
  IcaoViolationDetailDialog: ({ open }: { open: boolean }) =>
    open ? <div data-testid="violation-dialog">Violation Dialog</div> : null,
}));

vi.mock('@/components/common', () => ({
  getGlossaryTooltip: (term: string) => term,
}));

const baseData: ValidationSummaryData = {
  validCount: 0,
  invalidCount: 0,
  pendingCount: 0,
  trustChainValidCount: 0,
  trustChainInvalidCount: 0,
  cscaNotFoundCount: 0,
  expiredCount: 0,
};

describe('ValidationSummaryPanel', () => {
  it('should render with default title when no title prop', () => {
    render(<ValidationSummaryPanel data={baseData} />);
    expect(screen.getByText('검증 요약')).toBeInTheDocument();
  });

  it('should render custom title when provided', () => {
    render(<ValidationSummaryPanel data={baseData} title="Custom Title" />);
    expect(screen.getByText('Custom Title')).toBeInTheDocument();
  });

  it('should show processing indicator when isProcessing=true', () => {
    render(<ValidationSummaryPanel data={baseData} isProcessing />);
    expect(screen.getByText('처리 중')).toBeInTheDocument();
  });

  it('should not show processing indicator when isProcessing=false', () => {
    render(<ValidationSummaryPanel data={baseData} isProcessing={false} />);
    expect(screen.queryByText('처리 중')).not.toBeInTheDocument();
  });

  it('should show valid count card when validCount > 0', () => {
    render(<ValidationSummaryPanel data={{ ...baseData, validCount: 100 }} />);
    expect(screen.getByText('100')).toBeInTheDocument();
    expect(screen.getByText('유효')).toBeInTheDocument();
  });

  it('should show invalid count card when invalidCount > 0', () => {
    render(<ValidationSummaryPanel data={{ ...baseData, invalidCount: 5 }} />);
    expect(screen.getByText('5')).toBeInTheDocument();
  });

  it('should show trust chain section when trust chain data exists', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, trustChainValidCount: 80, trustChainInvalidCount: 20 }}
      />
    );
    expect(screen.getByText('Trust Chain')).toBeInTheDocument();
  });

  it('should show CSCA not found count when cscaNotFoundCount > 0', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, trustChainValidCount: 0, cscaNotFoundCount: 15 }}
      />
    );
    expect(screen.getByText('CSCA 미발견')).toBeInTheDocument();
    expect(screen.getByText('15')).toBeInTheDocument();
  });

  it('should show ICAO compliance section when icao data exists', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, icaoCompliantCount: 90, icaoNonCompliantCount: 10 }}
      />
    );
    expect(screen.getByText('ICAO 준수')).toBeInTheDocument();
  });

  it('should show expiration section when expiration data exists', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, expiredCount: 30, validPeriodCount: 70 }}
      />
    );
    expect(screen.getByText('유효기간')).toBeInTheDocument();
    expect(screen.getByText('30')).toBeInTheDocument();
  });

  it('should show certificate type distribution when certificateTypes provided', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, certificateTypes: { DSC: 100, CSCA: 20 } }}
      />
    );
    expect(screen.getByText('인증서 유형')).toBeInTheDocument();
    // certTypeLabel('DSC') returns 'DSC', certTypeLabel('CSCA') returns 'CSCA (Self-Signed)'
    expect(screen.getByText('DSC')).toBeInTheDocument();
  });

  it('should show signature algorithm distribution', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, signatureAlgorithms: { 'SHA256withRSA': 50 } }}
      />
    );
    expect(screen.getByText('서명 알고리즘')).toBeInTheDocument();
    expect(screen.getByText('SHA256withRSA')).toBeInTheDocument();
  });

  it('should render success rate bar when counts > 0', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, validCount: 80, invalidCount: 20 }}
      />
    );
    expect(screen.getByText('성공률')).toBeInTheDocument();
    expect(screen.getByText('80%')).toBeInTheDocument();
  });

  it('should not render success rate bar when all counts are 0', () => {
    render(<ValidationSummaryPanel data={baseData} />);
    expect(screen.queryByText('성공률')).not.toBeInTheDocument();
  });

  describe('duplicate flow (Path A)', () => {
    it('should render duplicate funnel when duplicateCount + totalCertificates + processedCount are set', () => {
      render(
        <ValidationSummaryPanel
          data={{
            ...baseData,
            totalCertificates: 100,
            processedCount: 60,
            duplicateCount: 40,
            validCount: 60,
          }}
        />
      );
      expect(screen.getByText('전체')).toBeInTheDocument();
      expect(screen.getByText('신규')).toBeInTheDocument();
    });

    it('should compute newCount correctly (total - duplicates)', () => {
      render(
        <ValidationSummaryPanel
          data={{
            ...baseData,
            totalCertificates: 100,
            processedCount: 70,
            duplicateCount: 30,
          }}
        />
      );
      // newCount = 100 - 30 = 70
      expect(screen.getByText('70')).toBeInTheDocument();
    });
  });

  it('should open violation dialog when non-compliant row is clicked', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, icaoCompliantCount: 90, icaoNonCompliantCount: 10 }}
        uploadId="upload-123"
      />
    );

    const nonCompliantBtn = screen.getByText('미준수').closest('button');
    expect(nonCompliantBtn).toBeTruthy();
    fireEvent.click(nonCompliantBtn!);

    expect(screen.getByTestId('violation-dialog')).toBeInTheDocument();
  });

  it('should show revoked count when revokedCount > 0', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, expiredCount: 0, revokedCount: 5 }}
      />
    );
    expect(screen.getByText('폐기')).toBeInTheDocument();
    expect(screen.getByText('5')).toBeInTheDocument();
  });

  it('should show trust chain success rate percentage', () => {
    render(
      <ValidationSummaryPanel
        data={{ ...baseData, trustChainValidCount: 80, trustChainInvalidCount: 20 }}
      />
    );
    expect(screen.getByText('80 (80%)')).toBeInTheDocument();
  });
});
