import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { Dialog } from '../Dialog';

describe('Dialog', () => {
  describe('visibility', () => {
    it('should not render when isOpen is false', () => {
      render(
        <Dialog isOpen={false} onClose={vi.fn()} title="Test Dialog">
          <p>Content</p>
        </Dialog>
      );
      expect(screen.queryByText('Test Dialog')).not.toBeInTheDocument();
      expect(screen.queryByText('Content')).not.toBeInTheDocument();
    });

    it('should render when isOpen is true', () => {
      render(
        <Dialog isOpen={true} onClose={vi.fn()} title="Test Dialog">
          <p>Content</p>
        </Dialog>
      );
      expect(screen.getByText('Test Dialog')).toBeInTheDocument();
      expect(screen.getByText('Content')).toBeInTheDocument();
    });
  });

  describe('title', () => {
    it('should render the title in header', () => {
      render(
        <Dialog isOpen={true} onClose={vi.fn()} title="Certificate Details">
          <p>body</p>
        </Dialog>
      );
      expect(screen.getByRole('heading', { name: 'Certificate Details' })).toBeInTheDocument();
    });
  });

  describe('children', () => {
    it('should render children inside the content area', () => {
      render(
        <Dialog isOpen={true} onClose={vi.fn()} title="Dialog">
          <button>Action Button</button>
          <p>Some paragraph</p>
        </Dialog>
      );
      expect(screen.getByText('Action Button')).toBeInTheDocument();
      expect(screen.getByText('Some paragraph')).toBeInTheDocument();
    });
  });

  describe('close button', () => {
    it('should call onClose when X button is clicked', async () => {
      const user = userEvent.setup();
      const onClose = vi.fn();
      render(
        <Dialog isOpen={true} onClose={onClose} title="Dialog">
          <p>body</p>
        </Dialog>
      );
      // The close button contains the X icon — find by its parent button
      const closeButton = screen.getByRole('button');
      await user.click(closeButton);
      expect(onClose).toHaveBeenCalledTimes(1);
    });
  });

  describe('backdrop', () => {
    it('should call onClose when backdrop is clicked', () => {
      const onClose = vi.fn();
      const { container } = render(
        <Dialog isOpen={true} onClose={onClose} title="Dialog">
          <p>body</p>
        </Dialog>
      );
      // Backdrop is fixed inset-0 z-[80]
      const backdrop = container.querySelector('.fixed.inset-0.z-\\[80\\]');
      expect(backdrop).toBeInTheDocument();
      fireEvent.click(backdrop!);
      expect(onClose).toHaveBeenCalledTimes(1);
    });

    it('should NOT call onClose when clicking the dialog content (stopPropagation)', () => {
      const onClose = vi.fn();
      render(
        <Dialog isOpen={true} onClose={onClose} title="Dialog">
          <p>body</p>
        </Dialog>
      );
      // Click the dialog heading — should not propagate to backdrop
      fireEvent.click(screen.getByRole('heading', { name: 'Dialog' }));
      expect(onClose).not.toHaveBeenCalled();
    });
  });

  describe('size prop', () => {
    const sizes = [
      { size: 'sm' as const, cls: 'max-w-sm' },
      { size: 'md' as const, cls: 'max-w-md' },
      { size: 'lg' as const, cls: 'max-w-lg' },
      { size: 'xl' as const, cls: 'max-w-xl' },
      { size: '2xl' as const, cls: 'max-w-2xl' },
      { size: '3xl' as const, cls: 'max-w-3xl' },
      { size: '4xl' as const, cls: 'max-w-4xl' },
    ];

    sizes.forEach(({ size, cls }) => {
      it(`should apply ${cls} class for size="${size}"`, () => {
        const { container } = render(
          <Dialog isOpen={true} onClose={vi.fn()} title="Dialog" size={size}>
            <p>body</p>
          </Dialog>
        );
        const dialogBox = container.querySelector(`.${cls}`);
        expect(dialogBox).toBeInTheDocument();
      });
    });

    it('should default to max-w-md when no size prop is provided', () => {
      const { container } = render(
        <Dialog isOpen={true} onClose={vi.fn()} title="Dialog">
          <p>body</p>
        </Dialog>
      );
      expect(container.querySelector('.max-w-md')).toBeInTheDocument();
    });
  });

  describe('idempotency', () => {
    it('should render consistently when opened twice', () => {
      const onClose = vi.fn();
      const { rerender } = render(
        <Dialog isOpen={false} onClose={onClose} title="Dialog">
          <p>body</p>
        </Dialog>
      );
      expect(screen.queryByText('Dialog')).not.toBeInTheDocument();

      rerender(
        <Dialog isOpen={true} onClose={onClose} title="Dialog">
          <p>body</p>
        </Dialog>
      );
      expect(screen.getByText('Dialog')).toBeInTheDocument();
    });
  });
});
