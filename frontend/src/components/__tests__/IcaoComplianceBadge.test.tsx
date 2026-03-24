import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { IcaoComplianceBadge } from '../IcaoComplianceBadge';
import type { IcaoComplianceStatus } from '@/types';

// IcaoComplianceBadge uses useTranslation — mock i18n
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => {
      const map: Record<string, string> = {
        'certificate:compliance.icaoCompliant': '준수',
        'certificate:compliance.icaoWarning': '경고',
        'certificate:compliance.icaoNonCompliant': '미준수',
        'certificate:compliance.icaoUnknown': '알 수 없음',
        'certificate:filters.violationItems': '위반 항목',
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

const baseCompliance: IcaoComplianceStatus = {
  isCompliant: true,
  complianceLevel: 'CONFORMANT',
  violations: [],
  keyUsageCompliant: true,
  algorithmCompliant: true,
  keySizeCompliant: true,
  validityPeriodCompliant: true,
  dnFormatCompliant: true,
  extensionsCompliant: true,
};

describe('IcaoComplianceBadge', () => {
  it('should render CONFORMANT label', () => {
    render(<IcaoComplianceBadge compliance={baseCompliance} />);
    expect(screen.getByText('준수')).toBeInTheDocument();
  });

  it('should render WARNING label', () => {
    render(<IcaoComplianceBadge compliance={{ ...baseCompliance, complianceLevel: 'WARNING' }} />);
    expect(screen.getByText('경고')).toBeInTheDocument();
  });

  it('should render NON_CONFORMANT label', () => {
    render(
      <IcaoComplianceBadge compliance={{ ...baseCompliance, complianceLevel: 'NON_CONFORMANT' }} />
    );
    expect(screen.getByText('미준수')).toBeInTheDocument();
  });

  it('should render unknown label for unrecognized level', () => {
    render(
      <IcaoComplianceBadge compliance={{ ...baseCompliance, complianceLevel: 'UNKNOWN' }} />
    );
    expect(screen.getByText('알 수 없음')).toBeInTheDocument();
  });

  it('should not show details section when showDetails is false (default)', () => {
    const compliance = { ...baseCompliance, violations: ['Bad algorithm'] };
    render(<IcaoComplianceBadge compliance={compliance} />);
    expect(screen.queryByText('위반 항목')).not.toBeInTheDocument();
  });

  it('should show violations when showDetails=true and violations exist', () => {
    const compliance = {
      ...baseCompliance,
      complianceLevel: 'NON_CONFORMANT',
      violations: ['Bad algorithm', 'Invalid key size'],
    };
    render(<IcaoComplianceBadge compliance={compliance} showDetails />);

    expect(screen.getByText('위반 항목')).toBeInTheDocument();
    expect(screen.getByText('Bad algorithm')).toBeInTheDocument();
    expect(screen.getByText('Invalid key size')).toBeInTheDocument();
  });

  it('should show pkdConformanceCode when showDetails=true', () => {
    const compliance = {
      ...baseCompliance,
      complianceLevel: 'NON_CONFORMANT',
      violations: ['error'],
      pkdConformanceCode: 'ERR:CSCA.CDP.14',
    };
    render(<IcaoComplianceBadge compliance={compliance} showDetails />);
    expect(screen.getByText('ERR:CSCA.CDP.14')).toBeInTheDocument();
  });

  it('should show compliance detail grid when showDetails=true', () => {
    render(<IcaoComplianceBadge compliance={baseCompliance} showDetails />);

    expect(screen.getByText(/Key Usage/)).toBeInTheDocument();
    expect(screen.getByText(/Algorithm/)).toBeInTheDocument();
    expect(screen.getByText(/Key Size/)).toBeInTheDocument();
    expect(screen.getByText(/Validity/)).toBeInTheDocument();
  });

  it('should show check marks for compliant fields and X for non-compliant', () => {
    const compliance = {
      ...baseCompliance,
      keyUsageCompliant: false,
      algorithmCompliant: true,
    };
    render(<IcaoComplianceBadge compliance={compliance} showDetails />);

    // ✓ and ✗ are text nodes inside divs together with field labels
    const bodyText = document.body.textContent || '';
    expect(bodyText).toContain('✓');
    expect(bodyText).toContain('✗');
  });

  it('should apply sm size classes', () => {
    const { container } = render(
      <IcaoComplianceBadge compliance={baseCompliance} size="sm" />
    );
    // sm size uses text-xs on the badge
    expect(container.querySelector('.text-xs')).toBeInTheDocument();
  });

  it('should apply lg size classes', () => {
    const { container } = render(
      <IcaoComplianceBadge compliance={baseCompliance} size="lg" />
    );
    expect(container.querySelector('.text-base')).toBeInTheDocument();
  });
});
