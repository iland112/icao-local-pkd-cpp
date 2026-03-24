import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { Skeleton, CardSkeleton, TableSkeleton, DashboardSkeleton } from '../Skeleton';

describe('Skeleton', () => {
  describe('basic Skeleton', () => {
    it('should render a div with animate-pulse class', () => {
      const { container } = render(<Skeleton />);
      const el = container.firstChild as HTMLElement;
      expect(el).toBeInTheDocument();
      expect(el.className).toContain('animate-pulse');
    });

    it('should render a div with rounded-md class', () => {
      const { container } = render(<Skeleton />);
      const el = container.firstChild as HTMLElement;
      expect(el.className).toContain('rounded-md');
    });

    it('should render a div with background color class', () => {
      const { container } = render(<Skeleton />);
      const el = container.firstChild as HTMLElement;
      expect(el.className).toContain('bg-gray-200');
    });

    it('should apply additional className when provided', () => {
      const { container } = render(<Skeleton className="h-4 w-32" />);
      const el = container.firstChild as HTMLElement;
      expect(el.className).toContain('h-4');
      expect(el.className).toContain('w-32');
    });

    it('should still have animate-pulse when custom className is provided', () => {
      const { container } = render(<Skeleton className="custom-class" />);
      const el = container.firstChild as HTMLElement;
      expect(el.className).toContain('animate-pulse');
      expect(el.className).toContain('custom-class');
    });

    it('should render without className prop (no error)', () => {
      expect(() => render(<Skeleton />)).not.toThrow();
    });
  });

  describe('CardSkeleton', () => {
    it('should render without error', () => {
      expect(() => render(<CardSkeleton />)).not.toThrow();
    });

    it('should contain multiple Skeleton elements', () => {
      const { container } = render(<CardSkeleton />);
      const skeletons = container.querySelectorAll('.animate-pulse');
      expect(skeletons.length).toBeGreaterThan(1);
    });

    it('should have a white background card wrapper', () => {
      const { container } = render(<CardSkeleton />);
      const card = container.querySelector('.bg-white');
      expect(card).toBeInTheDocument();
    });

    it('should have a rounded card wrapper', () => {
      const { container } = render(<CardSkeleton />);
      const card = container.querySelector('.rounded-2xl');
      expect(card).toBeInTheDocument();
    });
  });

  describe('TableSkeleton', () => {
    it('should render without error', () => {
      expect(() => render(<TableSkeleton />)).not.toThrow();
    });

    it('should render 5 rows by default', () => {
      const { container } = render(<TableSkeleton />);
      // Each row has a .flex.items-center.gap-4 div (body rows only, not header/footer)
      const rowInners = container.querySelectorAll('.divide-y > div');
      expect(rowInners.length).toBe(5);
    });

    it('should render the specified number of rows', () => {
      const { container } = render(<TableSkeleton rows={3} />);
      const rowInners = container.querySelectorAll('.divide-y > div');
      expect(rowInners.length).toBe(3);
    });

    it('should render 0 rows when rows=0', () => {
      const { container } = render(<TableSkeleton rows={0} />);
      const rowInners = container.querySelectorAll('.divide-y > div');
      expect(rowInners.length).toBe(0);
    });

    it('should have a header section', () => {
      const { container } = render(<TableSkeleton />);
      const header = container.querySelector('.bg-gray-50');
      expect(header).toBeInTheDocument();
    });

    it('should have a pagination section', () => {
      const { container } = render(<TableSkeleton />);
      // pagination area has border-t
      const pagination = container.querySelector('.border-t');
      expect(pagination).toBeInTheDocument();
    });
  });

  describe('DashboardSkeleton', () => {
    it('should render without error', () => {
      expect(() => render(<DashboardSkeleton />)).not.toThrow();
    });

    it('should contain at least 2 CardSkeleton components', () => {
      const { container } = render(<DashboardSkeleton />);
      // Each CardSkeleton has a .bg-white.rounded-2xl wrapper
      const cards = container.querySelectorAll('.bg-white.rounded-2xl');
      expect(cards.length).toBeGreaterThanOrEqual(2);
    });

    it('should render a header area with skeleton elements', () => {
      const { container } = render(<DashboardSkeleton />);
      // Header area contains flex items-center gap-4
      const header = container.querySelector('.flex.items-center.gap-4');
      expect(header).toBeInTheDocument();
    });

    it('should render using space-y-6 wrapper', () => {
      const { container } = render(<DashboardSkeleton />);
      expect(container.querySelector('.space-y-6')).toBeInTheDocument();
    });
  });
});
