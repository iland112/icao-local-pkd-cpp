import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { UploadDetailModal } from '../UploadDetailModal';
import type { UploadedFile, UploadIssues } from '@/types';

// Mock i18n
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: 'ko' },
  }),
}));

// Mock heavy child components
vi.mock('../MasterListStructure', () => ({
  MasterListStructure: ({ uploadId }: any) => (
    <div data-testid="masterlist-structure">MasterListStructure:{uploadId}</div>
  ),
}));

vi.mock('../LdifStructure', () => ({
  LdifStructure: ({ uploadId }: any) => (
    <div data-testid="ldif-structure">LdifStructure:{uploadId}</div>
  ),
}));

vi.mock('../DuplicateCertificatesTree', () => ({
  DuplicateCertificatesTree: () => (
    <div data-testid="duplicate-tree">DuplicateCertificatesTree</div>
  ),
}));

vi.mock('../ValidationSummaryPanel', () => ({
  ValidationSummaryPanel: () => (
    <div data-testid="validation-summary">ValidationSummaryPanel</div>
  ),
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: any) => <span>{children}</span>,
}));

vi.mock('@/utils/dateFormat', () => ({
  formatDateTime: (dt: string) => dt || '—',
}));

vi.mock('@/utils/csvExport', () => ({
  exportDuplicatesToCsv: vi.fn(),
  exportDuplicateStatisticsToCsv: vi.fn(),
}));

const makeUpload = (overrides: Partial<UploadedFile> = {}): UploadedFile => ({
  id: 'upload-001',
  fileName: 'test-collection.ldif',
  fileFormat: 'LDIF',
  fileSize: 1024000,
  status: 'COMPLETED',
  processingMode: 'AUTO',
  createdAt: '2026-01-01T09:00:00Z',
  completedAt: '2026-01-01T09:01:30Z',
  ...overrides,
});

const makeIssues = (overrides: Partial<UploadIssues> = {}): UploadIssues => ({
  success: true,
  uploadId: 'upload-001',
  duplicates: [],
  totalDuplicates: 0,
  byType: { CSCA: 0, DSC: 0, DSC_NC: 0, MLSC: 0, CRL: 0 },
  ...overrides,
} as UploadIssues);

const defaultProps = {
  open: true,
  upload: makeUpload(),
  uploadIssues: null,
  complianceViolations: undefined,
  detailDuplicateCount: 0,
  loadingIssues: false,
  onClose: vi.fn(),
};

describe('UploadDetailModal', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should render nothing when open is false', () => {
    const { container } = render(
      <UploadDetailModal {...defaultProps} open={false} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render modal header with upload detail label', () => {
    render(<UploadDetailModal {...defaultProps} />);
    expect(screen.getByText('upload:history.uploadDetail')).toBeInTheDocument();
  });

  it('should render file name in header', () => {
    render(<UploadDetailModal {...defaultProps} />);
    expect(screen.getByText('test-collection.ldif')).toBeInTheDocument();
  });

  it('should call onClose when backdrop clicked', () => {
    const onClose = vi.fn();
    const { container } = render(<UploadDetailModal {...defaultProps} onClose={onClose} />);
    const backdrop = container.querySelector('.absolute.inset-0');
    if (backdrop) fireEvent.click(backdrop);
    expect(onClose).toHaveBeenCalled();
  });

  it('should call onClose when X button clicked', () => {
    const onClose = vi.fn();
    render(<UploadDetailModal {...defaultProps} onClose={onClose} />);
    const closeBtn = screen.getAllByRole('button')[0];
    fireEvent.click(closeBtn);
    expect(onClose).toHaveBeenCalled();
  });

  it('should show structure tab for LDIF files', () => {
    render(<UploadDetailModal {...defaultProps} upload={makeUpload({ fileFormat: 'LDIF' })} />);
    expect(screen.getByText('upload:history.ldifStructure')).toBeInTheDocument();
  });

  it('should show structure tab for MASTER_LIST files', () => {
    render(<UploadDetailModal {...defaultProps} upload={makeUpload({ fileFormat: 'MASTER_LIST' })} />);
    expect(screen.getByText('upload:history.mlStructure')).toBeInTheDocument();
  });

  it('should show structure tab for ML files', () => {
    render(<UploadDetailModal {...defaultProps} upload={makeUpload({ fileFormat: 'ML' })} />);
    expect(screen.getByText('upload:history.mlStructure')).toBeInTheDocument();
  });

  it('should not show structure tab for PEM files', () => {
    render(<UploadDetailModal {...defaultProps} upload={makeUpload({ fileFormat: 'PEM' })} />);
    expect(screen.queryByText('upload:history.ldifStructure')).not.toBeInTheDocument();
    expect(screen.queryByText('upload:history.mlStructure')).not.toBeInTheDocument();
  });

  it('should show duplicates tab when detailDuplicateCount > 0', () => {
    render(
      <UploadDetailModal
        {...defaultProps}
        detailDuplicateCount={5}
      />
    );
    expect(screen.getByText('upload:detail.duplicateCertificates')).toBeInTheDocument();
    expect(screen.getByText('5')).toBeInTheDocument();
  });

  it('should not show duplicates tab when detailDuplicateCount is 0', () => {
    render(<UploadDetailModal {...defaultProps} detailDuplicateCount={0} />);
    expect(screen.queryByText('upload:detail.duplicateCertificates')).not.toBeInTheDocument();
  });

  it('should switch to structure tab on click and show LDIF structure', () => {
    render(<UploadDetailModal {...defaultProps} upload={makeUpload({ fileFormat: 'LDIF' })} />);

    fireEvent.click(screen.getByText('upload:history.ldifStructure'));
    expect(screen.getByTestId('ldif-structure')).toBeInTheDocument();
  });

  it('should switch to structure tab on click and show MasterList structure', () => {
    render(
      <UploadDetailModal
        {...defaultProps}
        upload={makeUpload({ fileFormat: 'MASTER_LIST', id: 'upload-ml' })}
      />
    );

    fireEvent.click(screen.getByText('upload:history.mlStructure'));
    expect(screen.getByTestId('masterlist-structure')).toBeInTheDocument();
  });

  it('should switch to duplicates tab on click', () => {
    render(
      <UploadDetailModal
        {...defaultProps}
        upload={makeUpload({ fileFormat: 'LDIF' })}
        detailDuplicateCount={3}
        uploadIssues={makeIssues({
          totalDuplicates: 3,
          byType: { CSCA: 1, DSC: 2, DSC_NC: 0, MLSC: 0, CRL: 0 },
        })}
      />
    );
    fireEvent.click(screen.getByText('upload:detail.duplicateCertificates'));
    expect(screen.getByTestId('duplicate-tree')).toBeInTheDocument();
  });

  it('should show ValidationSummaryPanel in details tab', () => {
    render(
      <UploadDetailModal
        {...defaultProps}
        upload={makeUpload({
          validation: {
            validCount: 10, invalidCount: 0, pendingCount: 0, expiredValidCount: 0,
            errorCount: 0, trustChainValidCount: 10, trustChainInvalidCount: 0,
            cscaNotFoundCount: 0, expiredCount: 0, revokedCount: 0,
          },
        })}
      />
    );
    expect(screen.getByTestId('validation-summary')).toBeInTheDocument();
  });

  it('should show COMPLETED status steps', () => {
    render(<UploadDetailModal {...defaultProps} upload={makeUpload({ status: 'COMPLETED' })} />);
    // All steps should be rendered
    expect(screen.getByText('upload:history.progressStatus')).toBeInTheDocument();
  });

  it('should show FAILED status indicator', () => {
    const { container } = render(
      <UploadDetailModal
        {...defaultProps}
        upload={makeUpload({ status: 'FAILED', errorMessage: 'Upload failed due to parse error' })}
      />
    );
    expect(container.querySelector('.bg-red-500')).toBeInTheDocument();
  });

  it('should display error message for FAILED uploads', () => {
    render(
      <UploadDetailModal
        {...defaultProps}
        upload={makeUpload({ status: 'FAILED', errorMessage: 'Connection refused' })}
      />
    );
    expect(screen.getByText('Connection refused')).toBeInTheDocument();
  });

  it('should render details tab by default', () => {
    render(
      <UploadDetailModal
        {...defaultProps}
        upload={makeUpload({
          fileFormat: 'LDIF',
          validation: {
            validCount: 5, invalidCount: 0, pendingCount: 0, expiredValidCount: 0,
            errorCount: 0, trustChainValidCount: 5, trustChainInvalidCount: 0,
            cscaNotFoundCount: 0, expiredCount: 0, revokedCount: 0,
          },
        })}
      />
    );
    expect(screen.getByTestId('validation-summary')).toBeInTheDocument();
  });
});
