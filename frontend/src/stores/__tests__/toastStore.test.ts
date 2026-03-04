import { describe, it, expect, vi, beforeEach } from 'vitest';
import { useToastStore, toast } from '../toastStore';

beforeEach(() => {
  // Reset store state between tests
  useToastStore.setState({ toasts: [] });
});

describe('useToastStore', () => {
  it('should start with an empty toasts array', () => {
    const state = useToastStore.getState();
    expect(state.toasts).toEqual([]);
  });

  it('should add a toast with generated id', () => {
    useToastStore.getState().addToast({
      type: 'success',
      title: 'Upload completed',
      message: '31,212 certificates processed',
    });

    const state = useToastStore.getState();
    expect(state.toasts).toHaveLength(1);
    expect(state.toasts[0].type).toBe('success');
    expect(state.toasts[0].title).toBe('Upload completed');
    expect(state.toasts[0].message).toBe('31,212 certificates processed');
    expect(state.toasts[0].id).toBeTruthy();
  });

  it('should add multiple toasts', () => {
    useToastStore.getState().addToast({ type: 'success', title: 'First' });
    useToastStore.getState().addToast({ type: 'error', title: 'Second' });
    useToastStore.getState().addToast({ type: 'warning', title: 'Third' });

    const state = useToastStore.getState();
    expect(state.toasts).toHaveLength(3);
  });

  it('should remove a toast by id', () => {
    useToastStore.getState().addToast({ type: 'info', title: 'Test' });
    const toastId = useToastStore.getState().toasts[0].id;

    useToastStore.getState().removeToast(toastId);

    const state = useToastStore.getState();
    expect(state.toasts).toHaveLength(0);
  });

  it('should clear all toasts', () => {
    useToastStore.getState().addToast({ type: 'success', title: 'A' });
    useToastStore.getState().addToast({ type: 'error', title: 'B' });
    expect(useToastStore.getState().toasts).toHaveLength(2);

    useToastStore.getState().clearToasts();
    expect(useToastStore.getState().toasts).toHaveLength(0);
  });

  it('should auto-remove toast after duration', () => {
    vi.useFakeTimers();

    useToastStore.getState().addToast({
      type: 'info',
      title: 'Auto-dismiss',
      duration: 3000,
    });

    expect(useToastStore.getState().toasts).toHaveLength(1);

    vi.advanceTimersByTime(3000);

    expect(useToastStore.getState().toasts).toHaveLength(0);

    vi.useRealTimers();
  });

  it('should use default 5000ms duration when not specified', () => {
    vi.useFakeTimers();

    useToastStore.getState().addToast({ type: 'success', title: 'Default' });

    // Still present at 4999ms
    vi.advanceTimersByTime(4999);
    expect(useToastStore.getState().toasts).toHaveLength(1);

    // Gone at 5000ms
    vi.advanceTimersByTime(1);
    expect(useToastStore.getState().toasts).toHaveLength(0);

    vi.useRealTimers();
  });

  it('should not auto-remove when duration is 0 (persistent)', () => {
    vi.useFakeTimers();

    useToastStore.getState().addToast({
      type: 'error',
      title: 'Persistent error',
      duration: 0,
    });

    vi.advanceTimersByTime(60000); // 1 minute
    expect(useToastStore.getState().toasts).toHaveLength(1);

    vi.useRealTimers();
  });
});

describe('toast helper functions', () => {
  it('should create success toast via helper', () => {
    toast.success('Upload complete', 'All certificates saved');

    const state = useToastStore.getState();
    expect(state.toasts).toHaveLength(1);
    expect(state.toasts[0].type).toBe('success');
    expect(state.toasts[0].title).toBe('Upload complete');
    expect(state.toasts[0].message).toBe('All certificates saved');
  });

  it('should create error toast via helper', () => {
    toast.error('Connection failed');

    const state = useToastStore.getState();
    expect(state.toasts[0].type).toBe('error');
    expect(state.toasts[0].title).toBe('Connection failed');
  });

  it('should create warning toast via helper', () => {
    toast.warning('Token expiring soon');

    const state = useToastStore.getState();
    expect(state.toasts[0].type).toBe('warning');
  });

  it('should create info toast via helper', () => {
    toast.info('New version available');

    const state = useToastStore.getState();
    expect(state.toasts[0].type).toBe('info');
  });
});
