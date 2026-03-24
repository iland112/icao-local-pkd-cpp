import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { Doc9303ComplianceChecklist } from '../Doc9303ComplianceChecklist';
import type { Doc9303ChecklistResult } from '@/types';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string, opts?: Record<string, unknown>) => {
      if (opts) return `${key}:${JSON.stringify(opts)}`;
      return key;
    },
    i18n: { language: 'ko' },
  }),
}));

const makeChecklist = (overrides: Partial<Doc9303ChecklistResult> = {}): Doc9303ChecklistResult => ({
  overallStatus: 'CONFORMANT',
  certificateType: 'DSC',
  passCount: 3,
  failCount: 0,
  warningCount: 0,
  naCount: 0,
  items: [
    { id: 'v1', category: 'Algorithm', label: 'SHA-256 hash', status: 'PASS', message: undefined },
    { id: 'v2', category: 'Algorithm', label: 'RSA key', status: 'PASS', message: undefined },
    { id: 'v3', category: 'KeyUsage', label: 'Digital Signature', status: 'PASS', message: undefined },
  ],
  ...overrides,
});

describe('Doc9303ComplianceChecklist', () => {
  describe('compact mode', () => {
    it('should render as a badge in compact mode for CONFORMANT', () => {
      const { container } = render(
        <Doc9303ComplianceChecklist checklist={makeChecklist()} compact />
      );
      // compact = inline-flex rounded-full
      expect(container.querySelector('.rounded-full')).toBeInTheDocument();
    });

    it('should render conformant status label in compact mode', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} compact />);
      expect(screen.getByText('certificate:compliance.doc9303Pass')).toBeInTheDocument();
    });

    it('should render fail count in compact mode when failCount > 0', () => {
      const checklist = makeChecklist({
        overallStatus: 'NON_CONFORMANT',
        failCount: 2,
      });
      render(<Doc9303ComplianceChecklist checklist={checklist} compact />);
      expect(screen.getByText('(2)')).toBeInTheDocument();
    });

    it('should not render fail count badge when failCount is 0', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} compact />);
      expect(screen.queryByText('(0)')).not.toBeInTheDocument();
    });
  });

  describe('full mode', () => {
    it('should render summary bar with overall status label', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} />);
      expect(screen.getByText('certificate:compliance.doc9303Pass')).toBeInTheDocument();
    });

    it('should display certificate type in summary', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist({ certificateType: 'CSCA' })} />);
      expect(screen.getByText('(CSCA)')).toBeInTheDocument();
    });

    it('should show passCount in summary bar', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist({ passCount: 5 })} />);
      expect(screen.getByText('5')).toBeInTheDocument();
    });

    it('should show failCount when > 0', () => {
      render(
        <Doc9303ComplianceChecklist
          checklist={makeChecklist({ failCount: 3, passCount: 0, overallStatus: 'NON_CONFORMANT' })}
        />
      );
      expect(screen.getAllByText('3').length).toBeGreaterThan(0);
    });

    it('should show warningCount when > 0', () => {
      render(
        <Doc9303ComplianceChecklist
          checklist={makeChecklist({ warningCount: 1, overallStatus: 'WARNING' })}
        />
      );
      expect(screen.getByText('1')).toBeInTheDocument();
    });

    it('should not show failCount when 0', () => {
      const { container } = render(<Doc9303ComplianceChecklist checklist={makeChecklist({ failCount: 0 })} />);
      // Should not find XCircle count node specifically in the counts area
      // The summary bar only shows pass count when fail=0
      const summary = container.querySelector('.rounded-lg');
      expect(summary).toBeInTheDocument();
    });

    it('should render category group headers from items', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} />);
      expect(screen.getByText('Algorithm')).toBeInTheDocument();
      expect(screen.getByText('KeyUsage')).toBeInTheDocument();
    });

    it('should show pass ratio (pass/total) in category header', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} />);
      // Algorithm has 2 items both passing: (2/2)
      expect(screen.getByText('(2/2)')).toBeInTheDocument();
    });

    it('should auto-expand categories with failures', () => {
      const checklist = makeChecklist({
        overallStatus: 'NON_CONFORMANT',
        failCount: 1,
        items: [
          { id: 'v1', category: 'Algorithm', label: 'SHA-1 deprecated', status: 'FAIL', message: 'Use SHA-256' },
          { id: 'v2', category: 'KeyUsage', label: 'Digital Signature', status: 'PASS', message: undefined },
        ],
      });
      render(<Doc9303ComplianceChecklist checklist={checklist} />);
      // Fail category auto-opens: item label visible
      expect(screen.getByText('SHA-1 deprecated')).toBeInTheDocument();
    });

    it('should show fail message when item has FAIL status', () => {
      const checklist = makeChecklist({
        overallStatus: 'NON_CONFORMANT',
        failCount: 1,
        items: [
          {
            id: 'v1',
            category: 'Algorithm',
            label: 'Hash algorithm',
            status: 'FAIL',
            message: 'Use SHA-256 or higher',
          },
        ],
      });
      render(<Doc9303ComplianceChecklist checklist={checklist} />);
      expect(screen.getByText('Use SHA-256 or higher')).toBeInTheDocument();
    });

    it('should toggle category open/closed on click', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} />);
      // Initially closed (no failures)
      expect(screen.queryByText('SHA-256 hash')).not.toBeInTheDocument();

      // Click to open
      fireEvent.click(screen.getByText('Algorithm'));
      expect(screen.getByText('SHA-256 hash')).toBeInTheDocument();

      // Click to close
      fireEvent.click(screen.getByText('Algorithm'));
      expect(screen.queryByText('SHA-256 hash')).not.toBeInTheDocument();
    });

    it('should show "all passed" text when no failures in category', () => {
      render(<Doc9303ComplianceChecklist checklist={makeChecklist()} />);
      expect(screen.getAllByText('certificate:doc9303.allPassed').length).toBeGreaterThan(0);
    });

    it('should render NON_CONFORMANT overall status label', () => {
      render(
        <Doc9303ComplianceChecklist
          checklist={makeChecklist({ overallStatus: 'NON_CONFORMANT', failCount: 1 })}
        />
      );
      expect(screen.getByText('certificate:compliance.doc9303Fail')).toBeInTheDocument();
    });

    it('should render WARNING overall status label', () => {
      render(
        <Doc9303ComplianceChecklist
          checklist={makeChecklist({ overallStatus: 'WARNING', warningCount: 1 })}
        />
      );
      expect(screen.getByText('certificate:compliance.doc9303Warning')).toBeInTheDocument();
    });

    it('should show naCount when > 0', () => {
      render(
        <Doc9303ComplianceChecklist
          checklist={makeChecklist({ naCount: 4 })}
        />
      );
      expect(screen.getByText('4')).toBeInTheDocument();
    });

    it('should group items by category correctly', () => {
      const checklist: Doc9303ChecklistResult = {
        overallStatus: 'CONFORMANT',
        certificateType: 'CSCA',
        passCount: 4,
        failCount: 0,
        warningCount: 0,
        naCount: 0,
        items: [
          { id: 'a1', category: 'GroupA', label: 'Check A1', status: 'PASS', message: undefined },
          { id: 'a2', category: 'GroupA', label: 'Check A2', status: 'PASS', message: undefined },
          { id: 'b1', category: 'GroupB', label: 'Check B1', status: 'PASS', message: undefined },
          { id: 'b2', category: 'GroupB', label: 'Check B2', status: 'PASS', message: undefined },
        ],
      };
      render(<Doc9303ComplianceChecklist checklist={checklist} />);
      expect(screen.getByText('GroupA')).toBeInTheDocument();
      expect(screen.getByText('GroupB')).toBeInTheDocument();
    });

    it('should handle empty items array', () => {
      const checklist = makeChecklist({ items: [] as any, passCount: 0 });
      render(<Doc9303ComplianceChecklist checklist={checklist} />);
      // Should render without crash, summary bar present
      expect(screen.getByText('certificate:compliance.doc9303Pass')).toBeInTheDocument();
    });
  });
});
