import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { CountryStatisticsDialog } from '../CountryStatisticsDialog';

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

// Mock pkdApi
vi.mock('@/services/pkdApi', () => ({
  uploadHistoryApi: {
    getDetailedCountryStatistics: vi.fn(),
  },
}));

// Mock SortableHeader
vi.mock('@/components/common/SortableHeader', () => ({
  SortableHeader: ({ label, onSort, sortKey, ...rest }: any) => (
    <th onClick={() => onSort(sortKey)} {...rest}>{label}</th>
  ),
}));

// Mock useSortableTable
vi.mock('@/hooks/useSortableTable', () => ({
  useSortableTable: (data: any[]) => ({
    sortedData: data,
    sortConfig: null,
    requestSort: vi.fn(),
  }),
}));

import { uploadHistoryApi } from '@/services/pkdApi';

const mockCountries = [
  { countryCode: 'KR', mlsc: 1, cscaSelfSigned: 2, cscaLinkCert: 0, dsc: 100, dscNc: 3, crl: 1, totalCerts: 107 },
  { countryCode: 'US', mlsc: 0, cscaSelfSigned: 1, cscaLinkCert: 1, dsc: 200, dscNc: 0, crl: 2, totalCerts: 204 },
];

describe('CountryStatisticsDialog', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should render nothing when isOpen is false', () => {
    const { container } = render(
      <CountryStatisticsDialog isOpen={false} onClose={vi.fn()} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should show loader while fetching data', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockReturnValue(
      new Promise(() => {}) // never resolves
    );
    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);
    // Loader spinner is present
    const loaderEl = document.querySelector('.animate-spin');
    expect(loaderEl).toBeInTheDocument();
  });

  it('should render country data after successful load', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: mockCountries },
    } as any);

    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);

    await waitFor(() => {
      expect(screen.getByText('KR')).toBeInTheDocument();
      expect(screen.getByText('US')).toBeInTheDocument();
    });
  });

  it('should render totals row', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: mockCountries },
    } as any);

    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);

    await waitFor(() => {
      // Total certs = 107 + 204 = 311
      expect(screen.getByText('311')).toBeInTheDocument();
    });
  });

  it('should show empty state when no data', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: [] },
    } as any);

    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);

    await waitFor(() => {
      expect(screen.getByText('common:table.noData')).toBeInTheDocument();
    });
  });

  it('should show error message on fetch failure', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockRejectedValue(
      new Error('Network error')
    );

    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);

    await waitFor(() => {
      expect(screen.getByText('common:error.loadFailed')).toBeInTheDocument();
    });
  });

  it('should call onClose when close button clicked', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: [] },
    } as any);
    const onClose = vi.fn();
    render(<CountryStatisticsDialog isOpen={true} onClose={onClose} />);

    // Wait for loading to complete
    await waitFor(() => screen.getByText('common:table.noData'));

    // Click the X button (top right)
    const closeButtons = screen.getAllByRole('button');
    fireEvent.click(closeButtons[closeButtons.length - 1]); // last button is close in footer
    expect(onClose).toHaveBeenCalled();
  });

  it('should disable CSV button when loading', () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockReturnValue(
      new Promise(() => {})
    );
    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);
    const csvButton = screen.getByText('CSV').closest('button');
    expect(csvButton).toBeDisabled();
  });

  it('should retry fetch when retry button clicked on error', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics)
      .mockRejectedValueOnce(new Error('fail'))
      .mockResolvedValue({ data: { countries: mockCountries } } as any);

    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);

    await waitFor(() => screen.getByText('common:error.loadFailed'));

    fireEvent.click(screen.getByText('common:button.retry'));

    await waitFor(() => {
      expect(vi.mocked(uploadHistoryApi.getDetailedCountryStatistics)).toHaveBeenCalledTimes(2);
    });
  });

  it('should re-fetch when isOpen changes from false to true', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: mockCountries },
    } as any);

    const { rerender } = render(<CountryStatisticsDialog isOpen={false} onClose={vi.fn()} />);
    expect(uploadHistoryApi.getDetailedCountryStatistics).not.toHaveBeenCalled();

    rerender(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);
    await waitFor(() => {
      expect(uploadHistoryApi.getDetailedCountryStatistics).toHaveBeenCalledTimes(1);
    });
  });

  it('should render subtitle with country count', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: mockCountries },
    } as any);
    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);
    await waitFor(() => {
      expect(screen.getByText('KR')).toBeInTheDocument();
    });
    // Subtitle uses t() with num interpolation
    expect(
      screen.getByText((content) =>
        content.includes('dashboard:countryStats.detailSubtitle')
      )
    ).toBeInTheDocument();
  });

  it('should display dash for zero values in numeric columns', async () => {
    vi.mocked(uploadHistoryApi.getDetailedCountryStatistics).mockResolvedValue({
      data: { countries: [
        { countryCode: 'DE', mlsc: 0, cscaSelfSigned: 0, cscaLinkCert: 0, dsc: 50, dscNc: 0, crl: 0, totalCerts: 50 },
      ] },
    } as any);
    render(<CountryStatisticsDialog isOpen={true} onClose={vi.fn()} />);
    await waitFor(() => screen.getByText('DE'));
    const dashes = screen.getAllByText('-');
    expect(dashes.length).toBeGreaterThan(0);
  });
});
