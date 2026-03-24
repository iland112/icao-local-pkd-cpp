import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { PADetailModal } from '../PADetailModal';
import type { PAHistoryItem } from '@/types';

// Mock i18n
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: 'ko' },
  }),
}));

// Mock PA API
vi.mock('@/services/paApi', () => ({
  paApi: {
    getDataGroups: vi.fn(),
  },
}));

// Mock utility functions
vi.mock('@/utils/countryCode', () => ({
  getFlagSvgPath: (code: string) => `/svg/${code?.toLowerCase()}.svg`,
}));

vi.mock('@/utils/countryNames', () => ({
  getCountryName: (code: string) => code,
}));

vi.mock('@/utils/dateFormat', () => ({
  formatDateTime: (dt: string) => dt || '—',
}));

import { paApi } from '@/services/paApi';

const makeRecord = (overrides: Partial<PAHistoryItem> = {}): PAHistoryItem => ({
  id: 'pa-001',
  verificationId: 'ver-00000001-aaaa-bbbb-cccc-000000000001',
  status: 'VALID',
  verificationTimestamp: '2026-01-01T10:00:00Z',
  issuingCountry: 'KR',
  verificationType: 'FULL',
  documentNumber: 'M12345678',
  requestedBy: 'admin',
  clientIp: '192.168.1.1',
  processingDurationMs: 120,
  ...overrides,
} as PAHistoryItem);

describe('PADetailModal', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should render nothing when open is false', () => {
    const { container } = render(
      <PADetailModal open={false} record={makeRecord()} onClose={vi.fn()} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render nothing when record is null', () => {
    const { container } = render(
      <PADetailModal open={true} record={null} onClose={vi.fn()} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render modal header with verification detail label', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    render(<PADetailModal open={true} record={makeRecord()} onClose={vi.fn()} />);
    expect(screen.getByText('pa:history.verificationDetail')).toBeInTheDocument();
  });

  it('should render FULL verification type badge', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    render(<PADetailModal open={true} record={makeRecord({ verificationType: 'FULL' })} onClose={vi.fn()} />);
    expect(screen.getByText('pa:history.typeFull')).toBeInTheDocument();
  });

  it('should render LOOKUP verification type badge', () => {
    render(<PADetailModal open={true} record={makeRecord({ verificationType: 'LOOKUP' })} onClose={vi.fn()} />);
    expect(screen.getByText('pa:history.typeLookup')).toBeInTheDocument();
  });

  it('should render VALID status badge', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    render(<PADetailModal open={true} record={makeRecord({ status: 'VALID' })} onClose={vi.fn()} />);
    expect(screen.getByText('common:status.valid')).toBeInTheDocument();
  });

  it('should render INVALID status badge', () => {
    render(<PADetailModal open={true} record={makeRecord({ status: 'INVALID' })} onClose={vi.fn()} />);
    expect(screen.getAllByText('common:status.invalid').length).toBeGreaterThan(0);
  });

  it('should render EXPIRED_VALID status badge', () => {
    render(<PADetailModal open={true} record={makeRecord({ status: 'EXPIRED_VALID' })} onClose={vi.fn()} />);
    expect(screen.getByText('common:status.expiredValid')).toBeInTheDocument();
  });

  it('should render ERROR status badge', () => {
    render(<PADetailModal open={true} record={makeRecord({ status: 'ERROR' })} onClose={vi.fn()} />);
    expect(screen.getByText('pa:history.errorVerifications')).toBeInTheDocument();
  });

  it('should show verification ID (truncated)', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    render(<PADetailModal open={true} record={makeRecord()} onClose={vi.fn()} />);
    // verificationId substring(0, 12) = "ver-00000001"
    expect(screen.getByText(/ver-00000001/)).toBeInTheDocument();
  });

  it('should call onClose when close button clicked', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    const onClose = vi.fn();
    render(<PADetailModal open={true} record={makeRecord()} onClose={onClose} />);

    const closeBtn = screen.getAllByRole('button').find(
      (b) => b.classList.contains('rounded-lg') && b.querySelector('svg')
    );
    if (closeBtn) fireEvent.click(closeBtn);
    expect(onClose).toHaveBeenCalled();
  });

  it('should call onClose when backdrop clicked', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    const onClose = vi.fn();
    const { container } = render(<PADetailModal open={true} record={makeRecord()} onClose={onClose} />);

    const backdrop = container.querySelector('.absolute.inset-0');
    if (backdrop) fireEvent.click(backdrop);
    expect(onClose).toHaveBeenCalled();
  });

  it('should fetch DG data when record is VALID with verificationId', async () => {
    vi.mocked(paApi.getDataGroups).mockResolvedValue({
      data: {
        hasDg1: true,
        hasDg2: false,
        dg1: {
          surname: 'KIM',
          givenNames: 'MINSU',
          documentNumber: 'M12345678',
          nationality: 'KOR',
          sex: 'M',
          dateOfBirth: '900101',
          expirationDate: '290101',
        },
      },
    } as any);

    render(<PADetailModal open={true} record={makeRecord({ status: 'VALID', verificationType: 'FULL' })} onClose={vi.fn()} />);

    await waitFor(() => {
      expect(paApi.getDataGroups).toHaveBeenCalledWith('ver-00000001-aaaa-bbbb-cccc-000000000001');
    });
  });

  it('should not fetch DG data for LOOKUP type', () => {
    render(
      <PADetailModal
        open={true}
        record={makeRecord({ status: 'VALID', verificationType: 'LOOKUP' })}
        onClose={vi.fn()}
      />
    );
    expect(paApi.getDataGroups).not.toHaveBeenCalled();
  });

  it('should not fetch DG data for INVALID status', () => {
    render(
      <PADetailModal
        open={true}
        record={makeRecord({ status: 'INVALID' })}
        onClose={vi.fn()}
      />
    );
    expect(paApi.getDataGroups).not.toHaveBeenCalled();
  });

  it('should show DG load error message when fetch fails', async () => {
    vi.mocked(paApi.getDataGroups).mockRejectedValue(new Error('DG load failed'));
    render(
      <PADetailModal
        open={true}
        record={makeRecord({ status: 'VALID', verificationType: 'FULL' })}
        onClose={vi.fn()}
      />
    );
    await waitFor(() => {
      expect(screen.getByText('pa:history.dgLoadFailed')).toBeInTheDocument();
    });
  });

  it('should display issuing country code', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    render(<PADetailModal open={true} record={makeRecord({ issuingCountry: 'JP' })} onClose={vi.fn()} />);
    expect(screen.getByText('JP')).toBeInTheDocument();
  });

  it('should display requestedBy field', () => {
    vi.mocked(paApi.getDataGroups).mockReturnValue(new Promise(() => {}));
    render(<PADetailModal open={true} record={makeRecord({ requestedBy: 'operator1' })} onClose={vi.fn()} />);
    expect(screen.getByText('operator1')).toBeInTheDocument();
  });
});
