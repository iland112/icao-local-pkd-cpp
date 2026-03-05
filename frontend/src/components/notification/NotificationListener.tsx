import { useEffect, useRef } from 'react';
import { useNotificationStore } from '@/stores/notificationStore';

const SSE_URL = '/api/sync/notifications/stream';
const RECONNECT_DELAYS = [5000, 5000, 5000, 30000]; // 5s x3, then 30s

/**
 * NotificationListener — Global SSE listener for real-time system notifications
 *
 * Mounted in Layout.tsx, active on all protected routes.
 * Connects to pkd-relay-service SSE endpoint and forwards events to Zustand store.
 * Auto-reconnects on connection failure with exponential backoff.
 */
export function NotificationListener() {
  const addNotification = useNotificationStore((s) => s.addNotification);
  const addNotificationRef = useRef(addNotification);
  addNotificationRef.current = addNotification;

  const retryCountRef = useRef(0);
  const eventSourceRef = useRef<EventSource | null>(null);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    function connect() {
      // Cleanup previous
      if (eventSourceRef.current) {
        eventSourceRef.current.close();
        eventSourceRef.current = null;
      }

      if (import.meta.env.DEV) {
        console.log('[Notification] Connecting to SSE:', SSE_URL);
      }

      const eventSource = new EventSource(SSE_URL);
      eventSourceRef.current = eventSource;

      eventSource.addEventListener('connected', () => {
        retryCountRef.current = 0; // Reset retry count on successful connection
        if (import.meta.env.DEV) {
          console.log('[Notification] SSE connected');
        }
      });

      eventSource.addEventListener('notification', (event) => {
        try {
          const data = JSON.parse(event.data);
          addNotificationRef.current({
            type: data.type || 'UNKNOWN',
            title: data.title || 'System Notification',
            message: data.message || '',
            data: data.data,
            timestamp: data.timestamp || new Date().toISOString(),
          });
        } catch (e) {
          if (import.meta.env.DEV) {
            console.error('[Notification] Failed to parse event:', e);
          }
        }
      });

      eventSource.onerror = () => {
        eventSource.close();
        eventSourceRef.current = null;

        const retryIndex = Math.min(retryCountRef.current, RECONNECT_DELAYS.length - 1);
        const delay = RECONNECT_DELAYS[retryIndex];
        retryCountRef.current++;

        if (import.meta.env.DEV) {
          console.log(`[Notification] SSE error, reconnecting in ${delay}ms (attempt ${retryCountRef.current})`);
        }

        reconnectTimerRef.current = setTimeout(connect, delay);
      };
    }

    connect();

    return () => {
      if (eventSourceRef.current) {
        eventSourceRef.current.close();
        eventSourceRef.current = null;
      }
      if (reconnectTimerRef.current) {
        clearTimeout(reconnectTimerRef.current);
        reconnectTimerRef.current = null;
      }
    };
  }, []);

  return null; // No UI — pure side-effect component
}
