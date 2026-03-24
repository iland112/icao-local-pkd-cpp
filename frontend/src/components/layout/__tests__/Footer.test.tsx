import { describe, it, expect } from 'vitest';
import { render, screen } from '@/test/test-utils';
import { Footer } from '../Footer';

describe('Footer', () => {
  describe('rendering', () => {
    it('should render a footer element', () => {
      render(<Footer />);
      expect(screen.getByRole('contentinfo')).toBeInTheDocument();
    });

    it('should render system operational status using i18n key', () => {
      render(<Footer />);
      // In test env, i18n returns keys as-is
      expect(screen.getByText('footer.systemOperational')).toBeInTheDocument();
    });

    it('should render ICAO compliance text using i18n key', () => {
      render(<Footer />);
      expect(screen.getByText('footer.icaoCompliant')).toBeInTheDocument();
    });

    it('should render copyright text using i18n key', () => {
      render(<Footer />);
      // Copyright symbol + i18n key
      expect(screen.getByText(/footer\.copyright/)).toBeInTheDocument();
    });

    it('should render copyright © symbol', () => {
      render(<Footer />);
      const footer = screen.getByRole('contentinfo');
      expect(footer.textContent).toContain('©');
    });
  });

  describe('status indicator', () => {
    it('should render the green status pulse dot', () => {
      const { container } = render(<Footer />);
      const dot = container.querySelector('.bg-green-500');
      expect(dot).toBeInTheDocument();
    });

    it('should render the status-pulse animation class on the dot', () => {
      const { container } = render(<Footer />);
      const dot = container.querySelector('.status-pulse');
      expect(dot).toBeInTheDocument();
    });
  });

  describe('layout', () => {
    it('should have a separator pipe between items', () => {
      render(<Footer />);
      // The pipe characters separate the footer sections
      const pipes = screen.getAllByText('|');
      expect(pipes.length).toBeGreaterThanOrEqual(2);
    });

    it('should render items aligned to the right', () => {
      const { container } = render(<Footer />);
      const innerDiv = container.querySelector('.justify-end');
      expect(innerDiv).toBeInTheDocument();
    });

    it('should have border-t for top border', () => {
      const { container } = render(<Footer />);
      const footer = container.querySelector('footer');
      expect(footer?.className).toContain('border-t');
    });
  });
});
