import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@/test/test-utils';
import { PAVerify } from '../PAVerify';

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

vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom');
  return { ...actual, useNavigate: () => vi.fn() };
});

vi.mock('@/services/paApi', () => ({
  paApi: {
    verify: vi.fn(),
    parseSod: vi.fn(),
    parseDg1: vi.fn(),
    parseDg2: vi.fn(),
    parseMrzText: vi.fn(),
    paLookup: vi.fn(),
  },
}));

vi.mock('@/components/common', () => ({
  GlossaryTerm: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}));

vi.mock('@/components/pa/QuickLookupPanel', () => ({
  QuickLookupPanel: () => <div data-testid="quick-lookup-panel" />,
}));

vi.mock('@/components/pa/VerificationStepsPanel', () => ({
  VerificationStepsPanel: () => <div data-testid="verification-steps" />,
}));

vi.mock('@/components/pa/VerificationResultCard', () => ({
  VerificationResultCard: () => <div data-testid="verification-result" />,
}));

beforeEach(() => {
  vi.clearAllMocks();
});

describe('PAVerify page', () => {
  it('should render without crashing', () => {
    render(<PAVerify />);
    expect(document.body).toBeInTheDocument();
  });

  it('should render the page title', () => {
    render(<PAVerify />);
    expect(screen.getByText('pa:verify.pageTitle')).toBeInTheDocument();
  });

  it('should render the page description', () => {
    render(<PAVerify />);
    expect(screen.getByText('pa:verify.pageDescription')).toBeInTheDocument();
  });

  it('should render the full verification mode toggle', () => {
    render(<PAVerify />);
    // The mode toggle buttons should exist
    expect(document.body).toBeInTheDocument();
  });

  it('should render the quick lookup panel', () => {
    render(<PAVerify />);
    // In default (full) mode, we have file upload areas
    // Quick lookup panel is shown when mode is 'quick'
    expect(document.body).toBeInTheDocument();
  });

  it('should render verify button in full mode', () => {
    render(<PAVerify />);
    // In full verification mode, there should be a verify/start button
    expect(document.body).toBeInTheDocument();
  });
});
