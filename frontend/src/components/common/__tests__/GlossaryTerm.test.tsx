import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { GlossaryTerm, getGlossaryTooltip } from '../GlossaryTerm';

describe('GlossaryTerm', () => {
  describe('unknown term', () => {
    it('should render just the term text with no help icon when term is not in glossary', () => {
      const { container } = render(<GlossaryTerm term="UNKNOWN_TERM" />);
      expect(screen.getByText('UNKNOWN_TERM')).toBeInTheDocument();
      // No help circle icon rendered (no svg in DOM)
      expect(container.querySelector('svg')).not.toBeInTheDocument();
    });

    it('should render custom label when provided for unknown term', () => {
      render(<GlossaryTerm term="UNKNOWN_TERM" label="Custom Label" />);
      expect(screen.getByText('Custom Label')).toBeInTheDocument();
    });

    it('should apply className for unknown term', () => {
      const { container } = render(
        <GlossaryTerm term="UNKNOWN_TERM" className="my-class" />
      );
      expect(container.querySelector('.my-class')).toBeInTheDocument();
    });
  });

  describe('known term', () => {
    it('should render the term text', () => {
      render(<GlossaryTerm term="CSCA" />);
      expect(screen.getByText('CSCA')).toBeInTheDocument();
    });

    it('should render custom label instead of term when label is provided', () => {
      render(<GlossaryTerm term="CSCA" label="국가 서명 인증서" />);
      expect(screen.getByText('국가 서명 인증서')).toBeInTheDocument();
      // The term text itself should not appear (label replaces it)
      // Note: term name still shows in tooltip, but not as visible text in span
    });

    it('should render the help circle icon for known terms', () => {
      const { container } = render(<GlossaryTerm term="DSC" />);
      expect(container.querySelector('svg')).toBeInTheDocument();
    });

    it('should apply custom className to wrapper span', () => {
      const { container } = render(
        <GlossaryTerm term="CSCA" className="text-blue-600" />
      );
      const wrapper = container.querySelector('.text-blue-600');
      expect(wrapper).toBeInTheDocument();
    });
  });

  describe('tooltip visibility', () => {
    it('should not show tooltip by default', () => {
      render(<GlossaryTerm term="CSCA" />);
      // Tooltip content (Korean definition) should not be visible initially
      expect(screen.queryByText(/Country Signing Certificate Authority/)).not.toBeInTheDocument();
    });

    it('should show tooltip on mouseenter', () => {
      render(<GlossaryTerm term="CSCA" />);
      const wrapper = screen.getByText('CSCA').closest('span[class*="inline-flex"]')!;
      fireEvent.mouseEnter(wrapper);
      // Tooltip renders the term name as font-semibold text
      expect(screen.getByText('CSCA', { selector: 'span.font-semibold' })).toBeInTheDocument();
    });

    it('should hide tooltip on mouseleave', () => {
      render(<GlossaryTerm term="CSCA" />);
      const wrapper = screen.getByText('CSCA').closest('span[class*="inline-flex"]')!;
      fireEvent.mouseEnter(wrapper);
      // Tooltip visible
      expect(screen.getByText('CSCA', { selector: 'span.font-semibold' })).toBeInTheDocument();
      fireEvent.mouseLeave(wrapper);
      // Tooltip hidden
      expect(screen.queryByText('CSCA', { selector: 'span.font-semibold' })).not.toBeInTheDocument();
    });
  });

  describe('multiple known terms', () => {
    const knownTerms = ['CSCA', 'DSC', 'DSC_NC', 'MLSC', 'CRL', 'SOD', 'MRZ', 'PA', 'PKD', 'LDIF', 'LDAP', 'ML', 'CSR'];
    knownTerms.forEach((term) => {
      it(`should render help icon for known term "${term}"`, () => {
        const { container } = render(<GlossaryTerm term={term} />);
        expect(container.querySelector('svg')).toBeInTheDocument();
      });
    });
  });

  describe('iconSize prop', () => {
    it('should apply xs icon classes when iconSize="xs" (default)', () => {
      const { container } = render(<GlossaryTerm term="CSCA" iconSize="xs" />);
      const svg = container.querySelector('svg');
      // SVG className is SVGAnimatedString — use getAttribute('class') for string comparison
      const cls = svg?.getAttribute('class') ?? '';
      expect(cls).toContain('w-3');
      expect(cls).toContain('h-3');
    });

    it('should apply sm icon classes when iconSize="sm"', () => {
      const { container } = render(<GlossaryTerm term="CSCA" iconSize="sm" />);
      const svg = container.querySelector('svg');
      const cls = svg?.getAttribute('class') ?? '';
      expect(cls).toContain('w-3.5');
      expect(cls).toContain('h-3.5');
    });
  });

  describe('tooltip positioning', () => {
    it('should use getBoundingClientRect for positioning on mouseenter', () => {
      const getBoundingClientRect = vi.fn().mockReturnValue({
        top: 100,
        bottom: 120,
        left: 50,
        right: 200,
      });

      render(<GlossaryTerm term="CSCA" />);
      const wrapper = screen.getByText('CSCA').closest('span[class*="inline-flex"]')!;
      // Attach mock to the ref element
      Object.defineProperty(wrapper, 'getBoundingClientRect', {
        value: getBoundingClientRect,
        writable: true,
      });
      fireEvent.mouseEnter(wrapper);
      expect(getBoundingClientRect).toHaveBeenCalled();
    });
  });
});

describe('getGlossaryTooltip', () => {
  it('should return term itself for unknown terms', () => {
    expect(getGlossaryTooltip('UNKNOWN_XYZ')).toBe('UNKNOWN_XYZ');
  });

  it('should return formatted tooltip for known terms', () => {
    const tooltip = getGlossaryTooltip('CSCA');
    expect(tooltip).toContain('CSCA:');
    expect(tooltip).toContain('Country Signing Certificate Authority');
  });

  it('should return tooltip for DSC', () => {
    const tooltip = getGlossaryTooltip('DSC');
    expect(tooltip).toContain('DSC:');
    expect(tooltip).toContain('Document Signer Certificate');
  });

  it('should return tooltip for MRZ', () => {
    const tooltip = getGlossaryTooltip('MRZ');
    expect(tooltip).toContain('MRZ:');
  });

  it('should return tooltip for compound keys like "Trust Chain"', () => {
    const tooltip = getGlossaryTooltip('Trust Chain');
    expect(tooltip).toContain('Trust Chain:');
  });
});
