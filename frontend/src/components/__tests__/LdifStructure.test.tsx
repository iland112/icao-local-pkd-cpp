import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { LdifStructure } from '../LdifStructure';

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
    getLdifStructure: vi.fn(),
  },
}));

// Mock TreeViewer — heavy component
vi.mock('../TreeViewer', () => ({
  TreeViewer: ({ data }: { data: any[] }) => (
    <div data-testid="tree-viewer">tree-nodes:{data.length}</div>
  ),
}));

import { uploadHistoryApi } from '@/services/pkdApi';

const mockLdifData = {
  totalEntries: 100,
  displayedEntries: 50,
  totalAttributes: 300,
  entries: [
    {
      dn: 'cn=abc,o=dsc,c=KR,dc=data,dc=download,dc=pkd,dc=icao,dc=int',
      objectClass: 'pkdDownload',
      lineNumber: 1,
      attributes: [
        { name: 'pkdCertificate', value: 'ABC123', isBinary: true },
        { name: 'cn', value: 'abc', isBinary: false },
      ],
    },
  ],
  objectClassCounts: { pkdDownload: 50 },
  truncated: false,
};

describe('LdifStructure', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should show loading state initially', () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockReturnValue(new Promise(() => {}));
    render(<LdifStructure uploadId="upload-123" />);
    expect(document.querySelector('.animate-spin')).toBeInTheDocument();
  });

  it('should show error state when API fails', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockRejectedValue(new Error('Fetch error'));
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('upload:ldifStructure.loadFailed')).toBeInTheDocument();
    });
  });

  it('should show error message from API response', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockRejectedValue(
      Object.assign(new Error('Network'), { message: 'Network timeout' })
    );
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('Network timeout')).toBeInTheDocument();
    });
  });

  it('should show error when API returns success=false', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: false, error: 'Parse error occurred' },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('Parse error occurred')).toBeInTheDocument();
    });
  });

  it('should render structure data after successful load', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByTestId('tree-viewer')).toBeInTheDocument();
    });
  });

  it('should display totalEntries count', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      // '100' appears in the stats section and in the select dropdown options
      expect(screen.getAllByText('100').length).toBeGreaterThan(0);
    });
  });

  it('should display displayedEntries count', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      // '50' appears in the stats section and in the select dropdown options
      expect(screen.getAllByText('50').length).toBeGreaterThan(0);
    });
  });

  it('should display objectClass counts as badges', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('pkdDownload: 50')).toBeInTheDocument();
    });
  });

  it('should show truncation warning when data.truncated is true', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: {
        success: true,
        data: { ...mockLdifData, truncated: true },
      },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('upload:ldifStructure.truncatedLabel')).toBeInTheDocument();
    });
  });

  it('should not show truncation warning when not truncated', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: { ...mockLdifData, truncated: false } },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.queryByText('upload:ldifStructure.truncatedLabel')).not.toBeInTheDocument();
    });
  });

  it('should re-fetch when maxEntries changes via select', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => screen.getByTestId('tree-viewer'));

    const select = screen.getByRole('combobox');
    fireEvent.change(select, { target: { value: '500' } });

    await waitFor(() => {
      expect(uploadHistoryApi.getLdifStructure).toHaveBeenCalledTimes(2);
    });
  });

  it('should call API with correct uploadId', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-xyz-456" />);
    await waitFor(() => {
      expect(uploadHistoryApi.getLdifStructure).toHaveBeenCalledWith('upload-xyz-456', 100);
    });
  });

  it('should show entry count footer', async () => {
    vi.mocked(uploadHistoryApi.getLdifStructure).mockResolvedValue({
      data: { success: true, data: mockLdifData },
    } as any);
    render(<LdifStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText(/upload:ldifStructure.entriesDisplayed/)).toBeInTheDocument();
    });
  });
});
