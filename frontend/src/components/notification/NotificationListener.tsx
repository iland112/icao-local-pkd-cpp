import { useEffect, useRef } from 'react';
import { useNotificationStore } from '@/stores/notificationStore';

const SSE_URL = '/api/sync/notifications/stream';
const RECONNECT_DELAYS = [3000, 5000, 10000, 30000]; // 3s, 5s, 10s, then 30s
const MAX_RETRIES = 20; // Stop after 20 consecutive failures

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
          // Validate expected fields are strings (defense against malformed server data)
          const type = typeof data.type === 'string' ? data.type : 'UNKNOWN';
          const title = typeof data.title === 'string' ? data.title.slice(0, 200) : 'System Notification';
          const message = typeof data.message === 'string' ? data.message.slice(0, 1000) : '';
          const timestamp = typeof data.timestamp === 'string' ? data.timestamp : new Date().toISOString();
          addNotificationRef.current({ type, title, message, data: data.data, timestamp });
        } catch (e) {
          if (import.meta.env.DEV) {
            console.error('[Notification] Failed to parse event:', e);
          }
        }
      });

      eventSource.onerror = () => {
        eventSource.close();
        eventSourceRef.current = null;

        // Stop reconnecting after too many failures
        if (retryCountRef.current >= MAX_RETRIES) {
          if (import.meta.env.DEV) {
            console.warn('[Notification] SSE max retries reached, stopping reconnection');
          }
          return;
        }

        const retryIndex = Math.min(retryCountRef.current, RECONNECT_DELAYS.length - 1);
        const delay = RECONNECT_DELAYS[retryIndex];
        retryCountRef.current++;

        if (import.meta.env.DEV) {
          console.log(`[Notification] SSE reconnecting in ${delay}ms (attempt ${retryCountRef.current})`);
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
