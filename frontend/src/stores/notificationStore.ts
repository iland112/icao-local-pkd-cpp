import { create } from 'zustand';
import { toast } from '@/stores/toastStore';

export interface SystemNotification {
  id: string;
  type: string;
  title: string;
  message: string;
  data?: Record<string, unknown>;
  timestamp: string;
  read: boolean;
}

interface NotificationState {
  notifications: SystemNotification[];
  unreadCount: number;
  addNotification: (n: Omit<SystemNotification, 'id' | 'read'>) => void;
  markAsRead: (id: string) => void;
  markAllAsRead: () => void;
  clearAll: () => void;
}

const MAX_NOTIFICATIONS = 50;

export const useNotificationStore = create<NotificationState>((set) => ({
  notifications: [],
  unreadCount: 0,

  addNotification: (notification) => {
    const id = Math.random().toString(36).substring(2, 9);
    const newNotification: SystemNotification = { ...notification, id, read: false };

    set((state) => {
      const updated = [newNotification, ...state.notifications].slice(0, MAX_NOTIFICATIONS);
      return {
        notifications: updated,
        unreadCount: updated.filter((n) => !n.read).length,
      };
    });

    // Show toast notification
    const toastType = notification.type.includes('FAILED') ? 'warning' : 'info';
    if (toastType === 'warning') {
      toast.warning(notification.title, notification.message);
    } else {
      toast.info(notification.title, notification.message);
    }
  },

  markAsRead: (id) =>
    set((state) => {
      const updated = state.notifications.map((n) =>
        n.id === id ? { ...n, read: true } : n
      );
      return {
        notifications: updated,
        unreadCount: updated.filter((n) => !n.read).length,
      };
    }),

  markAllAsRead: () =>
    set((state) => ({
      notifications: state.notifications.map((n) => ({ ...n, read: true })),
      unreadCount: 0,
    })),

  clearAll: () => set({ notifications: [], unreadCount: 0 }),
}));
