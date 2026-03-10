/**
 * PendingDscNotifier - Polls pending DSC stats and notifies admin when new pending DSCs arrive.
 * Mounted in Layout.tsx alongside NotificationListener.
 * Returns null (pure side-effect component).
 */
import { useEffect, useRef } from 'react';
import { pendingDscApi } from '@/api/pendingDscApi';
import { useNotificationStore } from '@/stores/notificationStore';

const POLL_INTERVAL_MS = 30_000; // 30 seconds

export function PendingDscNotifier() {
  const lastPendingCount = useRef<number | null>(null);
  const addNotification = useNotificationStore((s) => s.addNotification);

  useEffect(() => {
    let timer: ReturnType<typeof setInterval>;
    let mounted = true;

    const check = async () => {
      try {
        const res = await pendingDscApi.getStats();
        if (!mounted) return;

        const current = res.data?.data?.pendingCount ?? 0;

        // Only notify when count increases (not on first load)
        if (lastPendingCount.current !== null && current > lastPendingCount.current) {
          const diff = current - lastPendingCount.current;
          addNotification({
            type: 'DSC_PENDING_CREATED',
            title: 'DSC 등록 승인 대기',
            message: `신규 DSC ${diff}건이 승인 대기 중입니다. (전체 ${current}건)`,
            data: { pendingCount: current, newCount: diff },
            timestamp: new Date().toISOString(),
          });
        }

        lastPendingCount.current = current;
      } catch {
        // Silently ignore — stats endpoint may be temporarily unavailable
      }
    };

    // Initial check (populate baseline)
    check();
    timer = setInterval(check, POLL_INTERVAL_MS);

    return () => {
      mounted = false;
      clearInterval(timer);
    };
  }, [addNotification]);

  return null;
}

export default PendingDscNotifier;
