import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { MasterListStructure } from '../MasterListStructure';

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

// Mock TreeViewer
vi.mock('../TreeViewer', () => ({
  TreeViewer: ({ data }: { data: any[] }) => (
    <div data-testid="tree-viewer">nodes:{data.length}</div>
  ),
}));

// Mock axios
const mockAxiosGet = vi.hoisted(() => vi.fn());
vi.mock('axios', () => ({
  default: {
    get: mockAxiosGet,
  },
}));

const mockAsn1Tree = [
  {
    offset: 0,
    depth: 0,
    headerLength: 4,
    length: 1000,
    tag: 'SEQUENCE',
    isConstructed: true,
    children: [
      {
        offset: 4,
        depth: 1,
        headerLength: 2,
        length: 10,
        tag: 'INTEGER',
        isConstructed: false,
        value: '01',
        children: [],
      },
    ],
  },
];

const mockApiResponse = {
  success: true,
  fileName: 'icao.ml',
  fileSize: 204800,
  asn1Tree: mockAsn1Tree,
  statistics: {
    totalNodes: 150,
    constructedNodes: 50,
    primitiveNodes: 100,
  },
  maxLines: 100,
  truncated: false,
};

describe('MasterListStructure', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should show loading state initially', () => {
    mockAxiosGet.mockReturnValue(new Promise(() => {}));
    render(<MasterListStructure uploadId="upload-123" />);
    expect(document.querySelector('.animate-spin')).toBeInTheDocument();
    expect(screen.getByText('upload:masterListStructure.loading')).toBeInTheDocument();
  });

  it('should show error state on fetch failure', async () => {
    mockAxiosGet.mockRejectedValue({
      response: { data: { error: 'Server error' } },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('common:error.parseFailed')).toBeInTheDocument();
      expect(screen.getByText('Server error')).toBeInTheDocument();
    });
  });

  it('should show generic error when no response detail', async () => {
    mockAxiosGet.mockRejectedValue(new Error('Network failure'));
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('Failed to fetch Master List structure')).toBeInTheDocument();
    });
  });

  it('should show no-data state when API returns success=false', async () => {
    mockAxiosGet.mockResolvedValue({ data: { success: false, error: 'Not found' } });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('Not found')).toBeInTheDocument();
    });
  });

  it('should show no-data state when asn1Tree is empty', async () => {
    mockAxiosGet.mockResolvedValue({ data: { success: true, asn1Tree: [] } });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('upload:masterListStructure.noData')).toBeInTheDocument();
    });
  });

  it('should render tree viewer after successful load', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByTestId('tree-viewer')).toBeInTheDocument();
    });
  });

  it('should display file name', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('icao.ml')).toBeInTheDocument();
    });
  });

  it('should display formatted file size', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      // 204800 bytes = 200 KB
      expect(screen.getByText('200 KB')).toBeInTheDocument();
    });
  });

  it('should display statistics when provided', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('150')).toBeInTheDocument();
    });
  });

  it('should show truncation warning when truncated=true', async () => {
    mockAxiosGet.mockResolvedValue({
      data: { ...mockApiResponse, truncated: true, maxLines: 100 },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(
        screen.getByText(/upload:masterListStructure.truncatedMessage/)
      ).toBeInTheDocument();
    });
  });

  it('should show maxLines selector only when truncated', async () => {
    mockAxiosGet.mockResolvedValue({
      data: { ...mockApiResponse, truncated: true },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      const selects = screen.getAllByRole('combobox');
      expect(selects.length).toBeGreaterThan(0);
    });
  });

  it('should not show maxLines selector when not truncated', async () => {
    mockAxiosGet.mockResolvedValue({
      data: { ...mockApiResponse, truncated: false },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.queryByRole('combobox')).not.toBeInTheDocument();
    });
  });

  it('should re-fetch when maxLines changes', async () => {
    mockAxiosGet.mockResolvedValue({
      data: { ...mockApiResponse, truncated: true },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => screen.getByTestId('tree-viewer'));

    const select = screen.getByRole('combobox');
    fireEvent.change(select, { target: { value: '500' } });

    await waitFor(() => {
      expect(mockAxiosGet).toHaveBeenCalledTimes(2);
    });
  });

  it('should call API with correct uploadId', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-xyz-789" />);
    await waitFor(() => {
      expect(mockAxiosGet).toHaveBeenCalledWith(
        '/api/upload/upload-xyz-789/masterlist-structure?maxLines=100'
      );
    });
  });

  it('should render "Unknown" when fileName is missing', async () => {
    mockAxiosGet.mockResolvedValue({
      data: { ...mockApiResponse, fileName: undefined },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('Unknown')).toBeInTheDocument();
    });
  });

  it('should render "N/A" when fileSize is missing', async () => {
    mockAxiosGet.mockResolvedValue({
      data: { ...mockApiResponse, fileSize: undefined },
    });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('N/A')).toBeInTheDocument();
    });
  });

  it('should render TLV Tree label', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      expect(screen.getByText('TLV (Tag-Length-Value) Tree')).toBeInTheDocument();
    });
  });

  it('should pass correct number of tree nodes to TreeViewer', async () => {
    mockAxiosGet.mockResolvedValue({ data: mockApiResponse });
    render(<MasterListStructure uploadId="upload-123" />);
    await waitFor(() => {
      // mockApiResponse has 1 top-level asn1Tree node
      expect(screen.getByText('nodes:1')).toBeInTheDocument();
    });
  });
});
