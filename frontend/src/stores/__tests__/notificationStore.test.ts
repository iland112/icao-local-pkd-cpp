import { describe, it, expect, beforeEach, vi } from 'vitest';
import { useNotificationStore } from '../notificationStore';

// Mock toast store
vi.mock('@/stores/toastStore', () => ({
  toast: {
    info: vi.fn(),
    warning: vi.fn(),
    success: vi.fn(),
    error: vi.fn(),
  },
}));

import { toast } from '@/stores/toastStore';

describe('notificationStore', () => {
  beforeEach(() => {
    useNotificationStore.setState({ notifications: [], unreadCount: 0 });
    vi.clearAllMocks();
  });

  describe('addNotification', () => {
    it('should add a notification and increment unread count', () => {
      useNotificationStore.getState().addNotification({
        type: 'SYNC_COMPLETED',
        title: 'Sync Done',
        message: 'Database synced.',
        timestamp: new Date().toISOString(),
      });

      const state = useNotificationStore.getState();
      expect(state.notifications).toHaveLength(1);
      expect(state.unreadCount).toBe(1);
      expect(state.notifications[0].read).toBe(false);
      expect(state.notifications[0].title).toBe('Sync Done');
    });

    it('should prepend new notifications (newest first)', () => {
      const { addNotification } = useNotificationStore.getState();
      addNotification({ type: 'INFO', title: 'First', message: '', timestamp: '2026-01-01T00:00:00Z' });
      addNotification({ type: 'INFO', title: 'Second', message: '', timestamp: '2026-01-02T00:00:00Z' });

      const { notifications } = useNotificationStore.getState();
      expect(notifications[0].title).toBe('Second');
      expect(notifications[1].title).toBe('First');
    });

    it('should cap at MAX_NOTIFICATIONS (50)', () => {
      const { addNotification } = useNotificationStore.getState();
      for (let i = 0; i < 55; i++) {
        addNotification({ type: 'INFO', title: `N${i}`, message: '', timestamp: new Date().toISOString() });
      }

      expect(useNotificationStore.getState().notifications).toHaveLength(50);
    });

    it('should show warning toast for FAILED type', () => {
      useNotificationStore.getState().addNotification({
        type: 'SYNC_FAILED',
        title: 'Fail',
        message: 'Error occurred',
        timestamp: new Date().toISOString(),
      });

      expect(toast.warning).toHaveBeenCalledWith('Fail', 'Error occurred');
      expect(toast.info).not.toHaveBeenCalled();
    });

    it('should show info toast for non-FAILED type', () => {
      useNotificationStore.getState().addNotification({
        type: 'SYNC_COMPLETED',
        title: 'OK',
        message: 'Done',
        timestamp: new Date().toISOString(),
      });

      expect(toast.info).toHaveBeenCalledWith('OK', 'Done');
      expect(toast.warning).not.toHaveBeenCalled();
    });

    it('should generate unique id for each notification', () => {
      const { addNotification } = useNotificationStore.getState();
      addNotification({ type: 'A', title: 'T', message: '', timestamp: '' });
      addNotification({ type: 'B', title: 'T', message: '', timestamp: '' });

      const ids = useNotificationStore.getState().notifications.map((n) => n.id);
      expect(new Set(ids).size).toBe(2);
    });
  });

  describe('markAsRead', () => {
    it('should mark a specific notification as read', () => {
      useNotificationStore.getState().addNotification({
        type: 'INFO',
        title: 'Test',
        message: '',
        timestamp: '',
      });

      const id = useNotificationStore.getState().notifications[0].id;
      useNotificationStore.getState().markAsRead(id);

      const state = useNotificationStore.getState();
      expect(state.notifications[0].read).toBe(true);
      expect(state.unreadCount).toBe(0);
    });

    it('should not affect other notifications', () => {
      const { addNotification } = useNotificationStore.getState();
      addNotification({ type: 'A', title: 'A', message: '', timestamp: '' });
      addNotification({ type: 'B', title: 'B', message: '', timestamp: '' });

      const id = useNotificationStore.getState().notifications[0].id;
      useNotificationStore.getState().markAsRead(id);

      const state = useNotificationStore.getState();
      expect(state.notifications[0].read).toBe(true);
      expect(state.notifications[1].read).toBe(false);
      expect(state.unreadCount).toBe(1);
    });

    it('should handle non-existent id gracefully', () => {
      useNotificationStore.getState().addNotification({
        type: 'INFO',
        title: 'Test',
        message: '',
        timestamp: '',
      });

      useNotificationStore.getState().markAsRead('non-existent-id');
      expect(useNotificationStore.getState().unreadCount).toBe(1);
    });
  });

  describe('markAllAsRead', () => {
    it('should mark all notifications as read and set unreadCount to 0', () => {
      const { addNotification } = useNotificationStore.getState();
      addNotification({ type: 'A', title: 'A', message: '', timestamp: '' });
      addNotification({ type: 'B', title: 'B', message: '', timestamp: '' });
      addNotification({ type: 'C', title: 'C', message: '', timestamp: '' });

      useNotificationStore.getState().markAllAsRead();

      const state = useNotificationStore.getState();
      expect(state.unreadCount).toBe(0);
      expect(state.notifications.every((n) => n.read)).toBe(true);
    });

    it('should work on empty notifications', () => {
      useNotificationStore.getState().markAllAsRead();
      expect(useNotificationStore.getState().unreadCount).toBe(0);
    });
  });

  describe('clearAll', () => {
    it('should clear all notifications', () => {
      const { addNotification } = useNotificationStore.getState();
      addNotification({ type: 'A', title: 'A', message: '', timestamp: '' });
      addNotification({ type: 'B', title: 'B', message: '', timestamp: '' });

      useNotificationStore.getState().clearAll();

      const state = useNotificationStore.getState();
      expect(state.notifications).toHaveLength(0);
      expect(state.unreadCount).toBe(0);
    });
  });
});
