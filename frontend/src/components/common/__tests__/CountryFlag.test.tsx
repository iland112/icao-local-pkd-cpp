import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { CountryFlag } from '../CountryFlag';

// Mock dependencies
vi.mock('@/utils/countryCode', () => ({
  getFlagSvgPath: vi.fn((code: string) => {
    const map: Record<string, string> = {
      KR: '/svg/kr.svg',
      US: '/svg/us.svg',
      KOR: '/svg/kr.svg',
    };
    return map[code] ?? '';
  }),
}));

vi.mock('@/utils/countryNames', () => ({
  getCountryName: vi.fn((code: string) => {
    const map: Record<string, string> = {
      KR: 'South Korea',
      US: 'United States',
      KOR: 'South Korea',
    };
    return map[code] ?? code;
  }),
}));

describe('CountryFlag', () => {
  describe('null / empty code', () => {
    it('should render nothing when code is undefined', () => {
      const { container } = render(<CountryFlag code={undefined} />);
      expect(container.firstChild).toBeNull();
    });

    it('should render nothing when code is null', () => {
      const { container } = render(<CountryFlag code={null} />);
      expect(container.firstChild).toBeNull();
    });
  });

  describe('happy path — known code', () => {
    it('should render flag img with correct src', () => {
      render(<CountryFlag code="KR" />);
      const img = screen.getByRole('img');
      expect(img).toHaveAttribute('src', '/svg/kr.svg');
    });

    it('should use the code as the img alt text', () => {
      render(<CountryFlag code="KR" />);
      const img = screen.getByRole('img');
      expect(img).toHaveAttribute('alt', 'KR');
    });

    it('should display the country code text by default', () => {
      render(<CountryFlag code="KR" />);
      expect(screen.getByText('KR')).toBeInTheDocument();
    });

    it('should set title tooltip to "CODE — Country Name"', () => {
      const { container } = render(<CountryFlag code="KR" />);
      const wrapper = container.querySelector('span[title]');
      expect(wrapper).toHaveAttribute('title', 'KR — South Korea');
    });
  });

  describe('showCode prop', () => {
    it('should hide code text when showCode=false', () => {
      render(<CountryFlag code="KR" showCode={false} />);
      expect(screen.queryByText('KR')).not.toBeInTheDocument();
    });

    it('should show code text when showCode=true (default)', () => {
      render(<CountryFlag code="US" showCode={true} />);
      expect(screen.getByText('US')).toBeInTheDocument();
    });
  });

  describe('size prop', () => {
    it('should apply xs size classes', () => {
      const { container } = render(<CountryFlag code="KR" size="xs" />);
      const img = container.querySelector('img');
      expect(img?.className).toContain('w-3.5');
      expect(img?.className).toContain('h-2.5');
    });

    it('should apply sm size classes (default)', () => {
      const { container } = render(<CountryFlag code="KR" />);
      const img = container.querySelector('img');
      expect(img?.className).toContain('w-4');
      expect(img?.className).toContain('h-3');
    });

    it('should apply md size classes', () => {
      const { container } = render(<CountryFlag code="KR" size="md" />);
      const img = container.querySelector('img');
      expect(img?.className).toContain('w-5');
      expect(img?.className).toContain('h-3.5');
    });
  });

  describe('className prop', () => {
    it('should apply custom className to wrapper span', () => {
      const { container } = render(<CountryFlag code="KR" className="my-custom-class" />);
      const wrapper = container.querySelector('.my-custom-class');
      expect(wrapper).toBeInTheDocument();
    });
  });

  describe('unknown code — no flag path', () => {
    it('should render code text even when no flag svg path returned', () => {
      // getFlagSvgPath returns '' for unknown codes per the mock
      render(<CountryFlag code="UNKNOWN" />);
      // No img rendered (flagPath is falsy), but code text still shows
      expect(screen.queryByRole('img')).not.toBeInTheDocument();
      expect(screen.getByText('UNKNOWN')).toBeInTheDocument();
    });

    it('should set tooltip to just the code when countryName equals code', () => {
      // getCountryName returns code itself for UNKNOWN per the mock
      const { container } = render(<CountryFlag code="UNKNOWN" />);
      const wrapper = container.querySelector('span[title]');
      expect(wrapper).toHaveAttribute('title', 'UNKNOWN');
    });
  });

  describe('image error handler', () => {
    it('should hide img on error (onError handler hides element)', () => {
      const { container } = render(<CountryFlag code="KR" />);
      const img = container.querySelector('img') as HTMLImageElement;
      expect(img).not.toBeNull();
      // Simulate image load failure via React's synthetic event system
      fireEvent.error(img);
      // The onError handler sets display=none
      expect(img.style.display).toBe('none');
    });
  });
});
