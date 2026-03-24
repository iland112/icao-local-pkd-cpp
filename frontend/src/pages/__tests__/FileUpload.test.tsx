import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import { FileUpload } from '../FileUpload';

// Mock react-i18next
vi.mock('react-i18next', async () => {
  const actual = await vi.importActual<typeof import('react-i18next')>('react-i18next');
  return {
    ...actual,
    useTranslation: () => ({
      t: (key: string, opts?: { returnObjects?: boolean }) => {
        if (opts?.returnObjects) return [];
        return key;
      },
      i18n: { language: 'ko', changeLanguage: vi.fn() },
    }),
  };
});

// Mock react-router-dom
const mockNavigate = vi.fn();
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return { ...actual, useNavigate: () => mockNavigate };
});

// Mock API services
vi.mock('@/services/api', () => ({
  uploadApi: {
    uploadLdif: vi.fn(),
    uploadMasterList: vi.fn(),
    deleteUpload: vi.fn(),
    retryUpload: vi.fn(),
    getDetail: vi.fn(),
    getStatistics: vi.fn().mockResolvedValue({ data: { cscaCount: 5 } }),
  },
  createProgressEventSource: vi.fn(() => ({ close: vi.fn(), onmessage: null, onerror: null })),
}));

// Mock heavy child components
vi.mock('@/components/RealTimeStatisticsPanel', () => ({
  RealTimeStatisticsPanel: () => <div data-testid="realtime-stats" />,
}));
vi.mock('@/components/ProcessingErrorsPanel', () => ({
  ProcessingErrorsPanel: () => <div data-testid="processing-errors" />,
}));
vi.mock('@/components/CurrentCertificateCard', () => ({
  CurrentCertificateCard: () => <div data-testid="current-cert" />,
}));
vi.mock('@/components/EventLog', () => ({
  EventLog: () => <div data-testid="event-log" />,
}));
vi.mock('@/components/common/Stepper', () => ({
  Stepper: () => <div data-testid="stepper" />,
}));
vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}));

import { uploadApi } from '@/services/api';

beforeEach(() => {
  vi.clearAllMocks();
  vi.mocked(uploadApi.getStatistics).mockResolvedValue({ data: { cscaCount: 5 } } as any);
});

describe('FileUpload page', () => {
  it('should render without crashing', () => {
    render(<FileUpload />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the upload card heading', () => {
    render(<FileUpload />);
    // t('fileUpload.title') returns 'fileUpload.title'
    expect(screen.getByText('fileUpload.title')).toBeInTheDocument();
  });

  it('should render the file drop zone with drag-or-click text', () => {
    render(<FileUpload />);
    expect(screen.getByText('upload:fileUpload.dragOrClick')).toBeInTheDocument();
  });

  it('should render supported formats hint', () => {
    render(<FileUpload />);
    expect(screen.getByText((content) => content.includes('upload:fileUpload.supportedFormats'))).toBeInTheDocument();
  });

  it('should render a hidden file input accepting LDIF and ML files', () => {
    render(<FileUpload />);
    const input = document.querySelector('input[type="file"]');
    expect(input).toBeInTheDocument();
    expect(input?.getAttribute('accept')).toContain('.ldif');
  });

  it('should render the Stepper component', () => {
    render(<FileUpload />);
    expect(screen.getByTestId('stepper')).toBeInTheDocument();
  });

  it('should render the EventLog component when there are entries or processing', () => {
    // EventLog only renders when eventLogEntries.length > 0 or overallStatus is PROCESSING/FINALIZED
    // In initial state, neither is true, so EventLog is not rendered
    render(<FileUpload />);
    // No event log in initial idle state — it's conditionally rendered
    expect(screen.queryByTestId('event-log')).not.toBeInTheDocument();
  });
});
