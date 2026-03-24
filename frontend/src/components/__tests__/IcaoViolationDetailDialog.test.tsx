import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { IcaoViolationDetailDialog } from '../IcaoViolationDetailDialog';

// Mock i18n
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, unknown>) => {
      if (opts) return `${key}:${JSON.stringify(opts)}`;
      return key;
    },
    i18n: { language: 'ko' },
  }),
}));

// Mock validationApi
vi.mock('@/api/validationApi', () => ({
  getUploadValidations: vi.fn(),
}));

// Mock countryNames utility
vi.mock('@/utils/countryNames', () => ({
  getCountryName: (code: string) => code,
}));

import { getUploadValidations } from '@/api/validationApi';

const defaultProps = {
  open: true,
  onClose: vi.fn(),
  uploadId: 'upload-abc-123',
  violations: { algorithm: 5, keySize: 3, keyUsage: 2 },
};

describe('IcaoViolationDetailDialog', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should render nothing when open is false', () => {
    const { container } = render(
      <IcaoViolationDetailDialog {...defaultProps} open={false} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render dialog header when open', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    render(<IcaoViolationDetailDialog {...defaultProps} />);
    expect(screen.getByText('ICAO Doc 9303 미준수 상세')).toBeInTheDocument();
  });

  it('should display total non-compliant count', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 5, keySize: 3 }} />);
    expect(screen.getByText(/8건/)).toBeInTheDocument();
  });

  it('should render all violation category entries', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 5, keySize: 3 }} />);
    // algorithm label
    expect(screen.getByText('upload:validationSummary.violationAlgorithm')).toBeInTheDocument();
    // keySize label
    expect(screen.getByText('upload:validationSummary.violationKeySize')).toBeInTheDocument();
  });

  it('should not render categories with zero count', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    render(
      <IcaoViolationDetailDialog
        {...defaultProps}
        violations={{ algorithm: 5, keySize: 0 }}
      />
    );
    expect(screen.queryByText('upload:validationSummary.violationKeySize')).not.toBeInTheDocument();
  });

  it('should call onClose when background overlay clicked', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    const onClose = vi.fn();
    const { container } = render(
      <IcaoViolationDetailDialog {...defaultProps} onClose={onClose} />
    );
    // Click the outer fixed backdrop
    const backdrop = container.querySelector('.fixed.inset-0');
    if (backdrop) fireEvent.click(backdrop);
    expect(onClose).toHaveBeenCalled();
  });

  it('should call onClose when close button clicked', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    const onClose = vi.fn();
    render(<IcaoViolationDetailDialog {...defaultProps} onClose={onClose} />);
    // Click footer "닫기" button
    fireEvent.click(screen.getByText('닫기'));
    expect(onClose).toHaveBeenCalled();
  });

  it('should expand category on click and show detail + loader', async () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {})); // pending
    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 5 }} />);

    fireEvent.click(screen.getByText('upload:validationSummary.violationAlgorithm'));

    await waitFor(() => {
      expect(screen.getByText('조회 중...')).toBeInTheDocument();
    });
  });

  it('should show certificate list after load', async () => {
    const mockCerts = [
      {
        id: 'cert-1',
        countryCode: 'KR',
        certificateType: 'DSC',
        subjectDn: 'CN=Test,C=KR',
        signatureAlgorithm: 'SHA256withRSA',
        icaoViolations: 'SHA-1 is deprecated: SHA-1',
        isExpired: false,
        isNotYetValid: false,
      },
    ];
    vi.mocked(getUploadValidations).mockResolvedValue({ validations: mockCerts } as any);

    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 1 }} />);
    fireEvent.click(screen.getByText('upload:validationSummary.violationAlgorithm'));

    await waitFor(() => {
      expect(screen.getByText('KR')).toBeInTheDocument();
      expect(screen.getByText('DSC')).toBeInTheDocument();
    });
  });

  it('should collapse category when clicked again', async () => {
    vi.mocked(getUploadValidations).mockResolvedValue({ validations: [] } as any);

    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 3 }} />);

    const catButton = screen.getByText('upload:validationSummary.violationAlgorithm');
    fireEvent.click(catButton);

    await waitFor(() => {
      expect(screen.queryByText('조회 중...')).not.toBeInTheDocument();
    });

    fireEvent.click(catButton);
    // Expanded detail should no longer be visible
    expect(screen.queryByText('해당 인증서 목록 (최대 200건)')).not.toBeInTheDocument();
  });

  it('should use synthetic nonConformant category when violations is empty but totalNonCompliantCount > 0', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    render(
      <IcaoViolationDetailDialog
        {...defaultProps}
        violations={{}}
        totalNonCompliantCount={10}
      />
    );
    expect(screen.getByText('ICAO PKD 부적합 (DSC_NC)')).toBeInTheDocument();
  });

  it('should render info banner about ICAO Doc 9303', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    render(<IcaoViolationDetailDialog {...defaultProps} />);
    expect(screen.getByText(/ICAO Doc 9303은 전자여권 인증서의 표준 규격을 정의합니다/)).toBeInTheDocument();
  });

  it('should render percentage bars for each category', () => {
    vi.mocked(getUploadValidations).mockReturnValue(new Promise(() => {}));
    const { container } = render(
      <IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 8, keySize: 2 }} />
    );
    // Progress bars
    const bars = container.querySelectorAll('.bg-red-400');
    expect(bars.length).toBeGreaterThan(0);
  });

  it('should display "empty result" message when no certs returned', async () => {
    vi.mocked(getUploadValidations).mockResolvedValue({ validations: [] } as any);

    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 1 }} />);
    fireEvent.click(screen.getByText('upload:validationSummary.violationAlgorithm'));

    await waitFor(() => {
      expect(
        screen.getByText('개별 인증서 데이터를 조회할 수 없습니다 (업로드 완료 후 확인 가능)')
      ).toBeInTheDocument();
    });
  });

  it('should show category description when expanded', async () => {
    vi.mocked(getUploadValidations).mockResolvedValue({ validations: [] } as any);

    render(<IcaoViolationDetailDialog {...defaultProps} violations={{ algorithm: 1 }} />);
    fireEvent.click(screen.getByText('upload:validationSummary.violationAlgorithm'));

    await waitFor(() => {
      expect(screen.getByText(/ICAO가 승인하지 않은 암호화 알고리즘/)).toBeInTheDocument();
    });
  });
});
