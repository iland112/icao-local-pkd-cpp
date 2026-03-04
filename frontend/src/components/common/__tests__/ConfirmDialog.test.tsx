import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@/test/test-utils';
import userEvent from '@testing-library/user-event';
import { ConfirmDialog } from '../ConfirmDialog';

describe('ConfirmDialog', () => {
  it('should not render when isOpen is false', () => {
    render(
      <ConfirmDialog
        isOpen={false}
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Delete?"
        message="Are you sure?"
      />
    );

    expect(screen.queryByText('Delete?')).not.toBeInTheDocument();
  });

  it('should render title and message when open', () => {
    render(
      <ConfirmDialog
        isOpen={true}
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Upload delete"
        message="This upload will be permanently deleted."
      />
    );

    expect(screen.getByText('Upload delete')).toBeInTheDocument();
    expect(screen.getByText('This upload will be permanently deleted.')).toBeInTheDocument();
  });

  it('should display default button labels in Korean', () => {
    render(
      <ConfirmDialog
        isOpen={true}
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Confirm"
        message="Proceed?"
      />
    );

    expect(screen.getByText('확인')).toBeInTheDocument();
    expect(screen.getByText('취소')).toBeInTheDocument();
  });

  it('should use custom button labels when provided', () => {
    render(
      <ConfirmDialog
        isOpen={true}
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Delete"
        message="Delete upload?"
        confirmLabel="삭제"
        cancelLabel="돌아가기"
      />
    );

    expect(screen.getByText('삭제')).toBeInTheDocument();
    expect(screen.getByText('돌아가기')).toBeInTheDocument();
  });

  it('should call onClose when cancel button is clicked', async () => {
    const user = userEvent.setup();
    const onClose = vi.fn();

    render(
      <ConfirmDialog
        isOpen={true}
        onClose={onClose}
        onConfirm={vi.fn()}
        title="Cancel test"
        message="Test"
      />
    );

    await user.click(screen.getByText('취소'));
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it('should call onConfirm and onClose when confirm button is clicked', async () => {
    const user = userEvent.setup();
    const onClose = vi.fn();
    const onConfirm = vi.fn();

    render(
      <ConfirmDialog
        isOpen={true}
        onClose={onClose}
        onConfirm={onConfirm}
        title="Confirm test"
        message="Test"
      />
    );

    await user.click(screen.getByText('확인'));
    expect(onConfirm).toHaveBeenCalledTimes(1);
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it('should call onClose when backdrop is clicked', async () => {
    const user = userEvent.setup();
    const onClose = vi.fn();

    const { container } = render(
      <ConfirmDialog
        isOpen={true}
        onClose={onClose}
        onConfirm={vi.fn()}
        title="Backdrop test"
        message="Test"
      />
    );

    // Backdrop is the first div with fixed positioning
    const backdrop = container.querySelector('.fixed.inset-0.z-\\[80\\]');
    expect(backdrop).toBeInTheDocument();
    await user.click(backdrop!);
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it('should apply danger variant styles', () => {
    render(
      <ConfirmDialog
        isOpen={true}
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Danger"
        message="Dangerous action"
        variant="danger"
      />
    );

    // Confirm button should have red styling
    const confirmButton = screen.getByText('확인');
    expect(confirmButton.className).toContain('bg-red-600');
  });

  it('should apply info variant styles', () => {
    render(
      <ConfirmDialog
        isOpen={true}
        onClose={vi.fn()}
        onConfirm={vi.fn()}
        title="Info"
        message="Info action"
        variant="info"
      />
    );

    const confirmButton = screen.getByText('확인');
    expect(confirmButton.className).toContain('bg-blue-600');
  });
});
