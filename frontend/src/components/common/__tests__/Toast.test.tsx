import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, act, fireEvent } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { ToastContainer } from '../Toast';
import { useToastStore } from '@/stores/toastStore';

// Reset the store before each test
beforeEach(() => {
  useToastStore.setState({ toasts: [] });
  vi.useFakeTimers();
});

afterEach(() => {
  vi.runAllTimers();
  vi.useRealTimers();
});

describe('ToastContainer', () => {
  describe('empty state', () => {
    it('should render nothing when there are no toasts', () => {
      const { container } = render(<ToastContainer />);
      expect(container.firstChild).toBeNull();
    });
  });

  describe('rendering toasts', () => {
    it('should render a success toast', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'success',
          title: 'Operation successful',
          duration: 0,
        });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Operation successful')).toBeInTheDocument();
    });

    it('should render an error toast', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'error',
          title: 'Something went wrong',
          duration: 0,
        });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Something went wrong')).toBeInTheDocument();
    });

    it('should render a warning toast', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'warning',
          title: 'Warning message',
          duration: 0,
        });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Warning message')).toBeInTheDocument();
    });

    it('should render an info toast', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'info',
          title: 'Info message',
          duration: 0,
        });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Info message')).toBeInTheDocument();
    });

    it('should render toast with message (subtitle)', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'success',
          title: 'Upload complete',
          message: 'All 150 certificates were processed.',
          duration: 0,
        });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Upload complete')).toBeInTheDocument();
      expect(screen.getByText('All 150 certificates were processed.')).toBeInTheDocument();
    });

    it('should NOT render message paragraph when no message provided', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'info',
          title: 'Title only',
          duration: 0,
        });
      });
      const { container } = render(<ToastContainer />);
      expect(screen.getByText('Title only')).toBeInTheDocument();
      // There should be exactly one <p> element (the title, no message)
      const paragraphs = container.querySelectorAll('p');
      expect(paragraphs).toHaveLength(1);
    });

    it('should render multiple toasts simultaneously', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'success', title: 'Toast 1', duration: 0 });
        useToastStore.getState().addToast({ type: 'error', title: 'Toast 2', duration: 0 });
        useToastStore.getState().addToast({ type: 'info', title: 'Toast 3', duration: 0 });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Toast 1')).toBeInTheDocument();
      expect(screen.getByText('Toast 2')).toBeInTheDocument();
      expect(screen.getByText('Toast 3')).toBeInTheDocument();
    });
  });

  describe('variant styling', () => {
    it('should apply green styling for success toasts', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'success', title: 'OK', duration: 0 });
      });
      const { container } = render(<ToastContainer />);
      const toastEl = container.querySelector('.bg-green-50');
      expect(toastEl).toBeInTheDocument();
    });

    it('should apply red styling for error toasts', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'error', title: 'Err', duration: 0 });
      });
      const { container } = render(<ToastContainer />);
      const toastEl = container.querySelector('.bg-red-50');
      expect(toastEl).toBeInTheDocument();
    });

    it('should apply yellow styling for warning toasts', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'warning', title: 'Warn', duration: 0 });
      });
      const { container } = render(<ToastContainer />);
      const toastEl = container.querySelector('.bg-yellow-50');
      expect(toastEl).toBeInTheDocument();
    });

    it('should apply blue styling for info toasts', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'info', title: 'Info', duration: 0 });
      });
      const { container } = render(<ToastContainer />);
      const toastEl = container.querySelector('.bg-blue-50');
      expect(toastEl).toBeInTheDocument();
    });
  });

  describe('close button', () => {
    it('should remove toast when close button is clicked', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'info', title: 'Closeable', duration: 0 });
      });
      render(<ToastContainer />);
      expect(screen.getByText('Closeable')).toBeInTheDocument();

      const closeButton = screen.getByRole('button');
      act(() => {
        fireEvent.click(closeButton);
      });

      // After 200ms animation delay, removeToast is called — advance fake timers
      act(() => {
        vi.advanceTimersByTime(300);
      });

      expect(screen.queryByText('Closeable')).not.toBeInTheDocument();
    });

    it('should render one close button per toast', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'success', title: 'T1', duration: 0 });
        useToastStore.getState().addToast({ type: 'error', title: 'T2', duration: 0 });
      });
      render(<ToastContainer />);
      const buttons = screen.getAllByRole('button');
      expect(buttons).toHaveLength(2);
    });
  });

  describe('container positioning', () => {
    it('should render in a fixed top-right container', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'info', title: 'Test', duration: 0 });
      });
      const { container } = render(<ToastContainer />);
      const wrapper = container.querySelector('.fixed.top-4.right-4');
      expect(wrapper).toBeInTheDocument();
    });

    it('should render with high z-index', () => {
      act(() => {
        useToastStore.getState().addToast({ type: 'info', title: 'Test', duration: 0 });
      });
      const { container } = render(<ToastContainer />);
      const wrapper = container.querySelector('.z-\\[9999\\]');
      expect(wrapper).toBeInTheDocument();
    });
  });

  describe('Korean text support', () => {
    it('should correctly render Korean title and message', () => {
      act(() => {
        useToastStore.getState().addToast({
          type: 'success',
          title: '업로드 완료',
          message: '인증서 150개가 처리되었습니다.',
          duration: 0,
        });
      });
      render(<ToastContainer />);
      expect(screen.getByText('업로드 완료')).toBeInTheDocument();
      expect(screen.getByText('인증서 150개가 처리되었습니다.')).toBeInTheDocument();
    });
  });
});
