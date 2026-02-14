import { useRef, useEffect, useState, useCallback } from 'react';
import { ScrollText, ArrowDown, Trash2 } from 'lucide-react';
import { cn } from '@/utils/cn';

export interface EventLogEntry {
  id: number;
  timestamp: string;       // HH:MM:SS.mmm format
  eventName: string;       // e.g. "PARSING_IN_PROGRESS", "DB_SAVING_COMPLETED"
  detail: string;          // e.g. "DSC 500/30114", "COMPLETED"
  status: 'info' | 'success' | 'fail' | 'warning';
}

interface EventLogProps {
  events: EventLogEntry[];
  onClear: () => void;
  className?: string;
}

export function EventLog({ events, onClear, className }: EventLogProps) {
  const scrollRef = useRef<HTMLDivElement>(null);
  const [autoScroll, setAutoScroll] = useState(true);
  const userScrolledRef = useRef(false);

  // Auto-scroll to bottom when new events arrive
  useEffect(() => {
    if (autoScroll && scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [events, autoScroll]);

  // Detect user scroll to auto-disable auto-scroll
  const handleScroll = useCallback(() => {
    if (!scrollRef.current) return;
    const { scrollTop, scrollHeight, clientHeight } = scrollRef.current;
    const isAtBottom = scrollHeight - scrollTop - clientHeight < 30;

    if (!isAtBottom && !userScrolledRef.current) {
      userScrolledRef.current = true;
      setAutoScroll(false);
    } else if (isAtBottom && userScrolledRef.current) {
      userScrolledRef.current = false;
      setAutoScroll(true);
    }
  }, []);

  const toggleAutoScroll = () => {
    const next = !autoScroll;
    setAutoScroll(next);
    userScrolledRef.current = !next;
    if (next && scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  };

  const dotColor: Record<EventLogEntry['status'], string> = {
    info: 'bg-blue-500',
    success: 'bg-emerald-500',
    fail: 'bg-red-500',
    warning: 'bg-amber-500',
  };

  const lastTimestamp = events.length > 0 ? events[events.length - 1].timestamp : '-';

  return (
    <div className={cn('flex flex-col rounded-xl border border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 shadow-lg overflow-hidden', className)}>
      {/* Header */}
      <div className="flex items-center justify-between px-4 py-2.5 border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-800/80">
        <div className="flex items-center gap-2">
          <ScrollText className="w-4 h-4 text-gray-500 dark:text-gray-400" />
          <span className="text-sm font-semibold text-gray-700 dark:text-gray-200">Event Log</span>
        </div>
        <div className="flex items-center gap-3">
          <button
            onClick={toggleAutoScroll}
            className={cn(
              'flex items-center gap-1.5 text-xs font-medium px-2 py-1 rounded transition-colors',
              autoScroll
                ? 'text-blue-600 dark:text-blue-400 bg-blue-50 dark:bg-blue-900/30'
                : 'text-gray-500 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700'
            )}
          >
            <ArrowDown className="w-3 h-3" />
            Auto-scroll: {autoScroll ? 'ON' : 'OFF'}
          </button>
          <button
            onClick={onClear}
            className="flex items-center gap-1.5 text-xs font-medium px-2 py-1 rounded text-red-500 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-900/20 transition-colors"
          >
            <Trash2 className="w-3 h-3" />
            Clear
          </button>
        </div>
      </div>

      {/* Event List */}
      <div
        ref={scrollRef}
        onScroll={handleScroll}
        className="flex-1 overflow-y-auto min-h-[200px] max-h-[400px] font-mono text-[13px] leading-relaxed"
      >
        {events.length === 0 ? (
          <div className="flex items-center justify-center h-[200px] text-gray-400 dark:text-gray-500 text-sm">
            No events yet
          </div>
        ) : (
          <div className="py-1">
            {events.map((event) => (
              <div
                key={event.id}
                className="flex items-start gap-2.5 px-4 py-1 hover:bg-gray-50 dark:hover:bg-gray-700/40 transition-colors"
              >
                <span className={cn('w-2 h-2 rounded-full mt-1.5 shrink-0', dotColor[event.status])} />
                <span className="text-gray-400 dark:text-gray-500 shrink-0 select-all">
                  [{event.timestamp}]
                </span>
                <span className="text-gray-200 dark:text-gray-600 shrink-0 select-none">
                </span>
                <span className="text-gray-800 dark:text-gray-200 font-medium shrink-0">
                  {event.eventName}:
                </span>
                <span className={cn(
                  'break-all',
                  event.status === 'success' && 'text-emerald-600 dark:text-emerald-400',
                  event.status === 'fail' && 'text-red-600 dark:text-red-400',
                  event.status === 'warning' && 'text-amber-600 dark:text-amber-400',
                  event.status === 'info' && 'text-gray-600 dark:text-gray-300',
                )}>
                  {event.detail}
                </span>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Footer */}
      <div className="flex items-center justify-between px-4 py-1.5 border-t border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-800/80 text-xs text-gray-500 dark:text-gray-400">
        <span>Total events: {events.length}</span>
        <span>Last: {lastTimestamp}</span>
      </div>
    </div>
  );
}
