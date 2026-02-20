import { useState, useMemo } from 'react';
import { CheckCircle, XCircle, AlertTriangle, MinusCircle, ChevronRight, ChevronDown } from 'lucide-react';
import type { Doc9303ChecklistResult, Doc9303CheckItem } from '@/types';
import { cn } from '@/utils/cn';

interface Props {
  checklist: Doc9303ChecklistResult;
  compact?: boolean;
}

interface CategoryGroup {
  category: string;
  items: Doc9303CheckItem[];
  passCount: number;
  failCount: number;
  warningCount: number;
  naCount: number;
}

const statusIcon = (status: string, size = 'w-4 h-4') => {
  switch (status) {
    case 'PASS':
      return <CheckCircle className={cn(size, 'text-green-500 dark:text-green-400 shrink-0')} />;
    case 'FAIL':
      return <XCircle className={cn(size, 'text-red-500 dark:text-red-400 shrink-0')} />;
    case 'WARNING':
      return <AlertTriangle className={cn(size, 'text-yellow-500 dark:text-yellow-400 shrink-0')} />;
    case 'NA':
      return <MinusCircle className={cn(size, 'text-gray-400 dark:text-gray-500 shrink-0')} />;
    default:
      return <MinusCircle className={cn(size, 'text-gray-400 shrink-0')} />;
  }
};

const overallStatusStyle = (status: string) => {
  switch (status) {
    case 'CONFORMANT':
      return 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800 text-green-700 dark:text-green-300';
    case 'NON_CONFORMANT':
      return 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800 text-red-700 dark:text-red-300';
    case 'WARNING':
      return 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800 text-yellow-700 dark:text-yellow-300';
    default:
      return 'bg-gray-50 dark:bg-gray-900/20 border-gray-200 dark:border-gray-800 text-gray-700 dark:text-gray-300';
  }
};

const overallStatusLabel = (status: string) => {
  switch (status) {
    case 'CONFORMANT': return 'Doc 9303 적합';
    case 'NON_CONFORMANT': return 'Doc 9303 부적합';
    case 'WARNING': return 'Doc 9303 경고';
    default: return status;
  }
};

function CategorySection({ group, defaultOpen }: { group: CategoryGroup; defaultOpen: boolean }) {
  const [open, setOpen] = useState(defaultOpen);
  const total = group.items.length;
  const hasIssues = group.failCount > 0 || group.warningCount > 0;

  return (
    <div className="border-b border-gray-100 dark:border-gray-700/50 last:border-b-0">
      <button
        onClick={() => setOpen(!open)}
        className="w-full flex items-center justify-between px-3 py-2 hover:bg-gray-50 dark:hover:bg-gray-700/30 transition-colors text-sm"
      >
        <div className="flex items-center gap-2">
          {open
            ? <ChevronDown className="w-3.5 h-3.5 text-gray-400" />
            : <ChevronRight className="w-3.5 h-3.5 text-gray-400" />
          }
          <span className="font-medium text-gray-700 dark:text-gray-200">{group.category}</span>
          <span className="text-xs text-gray-400">({group.passCount}/{total})</span>
        </div>
        <div className="flex items-center gap-1">
          {hasIssues ? (
            <>
              {group.failCount > 0 && (
                <span className="text-xs text-red-500 dark:text-red-400 font-medium">
                  {group.failCount}건 실패
                </span>
              )}
              {group.warningCount > 0 && (
                <span className="text-xs text-yellow-500 dark:text-yellow-400 font-medium">
                  {group.warningCount}건 경고
                </span>
              )}
            </>
          ) : (
            <span className="text-xs text-green-500 dark:text-green-400">모두 통과</span>
          )}
        </div>
      </button>
      {open && (
        <div className="pl-8 pr-3 pb-2 space-y-1">
          {group.items.map((item) => (
            <div key={item.id} className="space-y-0.5">
              <div className="flex items-start gap-2 py-0.5">
                {statusIcon(item.status, 'w-3.5 h-3.5 mt-0.5')}
                <span className={cn(
                  'text-sm',
                  item.status === 'PASS' && 'text-gray-600 dark:text-gray-400',
                  item.status === 'FAIL' && 'text-red-600 dark:text-red-400 font-medium',
                  item.status === 'WARNING' && 'text-yellow-600 dark:text-yellow-400',
                  item.status === 'NA' && 'text-gray-400 dark:text-gray-500'
                )}>
                  {item.label}
                </span>
              </div>
              {item.status === 'FAIL' && item.message && (
                <div className="ml-5.5 pl-1 text-xs text-red-500 dark:text-red-400/80 border-l-2 border-red-200 dark:border-red-800 ml-[22px]">
                  {item.message}
                </div>
              )}
              {item.status === 'WARNING' && item.message && (
                <div className="pl-1 text-xs text-yellow-600 dark:text-yellow-400/80 border-l-2 border-yellow-200 dark:border-yellow-800 ml-[22px]">
                  {item.message}
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function Doc9303ComplianceChecklist({ checklist, compact = false }: Props) {
  const groups = useMemo<CategoryGroup[]>(() => {
    const map = new Map<string, Doc9303CheckItem[]>();
    for (const item of checklist.items) {
      const existing = map.get(item.category) || [];
      existing.push(item);
      map.set(item.category, existing);
    }
    return Array.from(map.entries()).map(([category, items]) => ({
      category,
      items,
      passCount: items.filter(i => i.status === 'PASS').length,
      failCount: items.filter(i => i.status === 'FAIL').length,
      warningCount: items.filter(i => i.status === 'WARNING').length,
      naCount: items.filter(i => i.status === 'NA').length,
    }));
  }, [checklist.items]);

  if (compact) {
    return (
      <div className={cn(
        'inline-flex items-center gap-2 rounded-full border px-3 py-1 text-xs font-medium',
        overallStatusStyle(checklist.overallStatus)
      )}>
        {statusIcon(checklist.overallStatus === 'CONFORMANT' ? 'PASS' : checklist.overallStatus === 'NON_CONFORMANT' ? 'FAIL' : 'WARNING', 'w-3.5 h-3.5')}
        <span>{overallStatusLabel(checklist.overallStatus)}</span>
        {checklist.failCount > 0 && (
          <span className="text-red-500 dark:text-red-400">({checklist.failCount})</span>
        )}
      </div>
    );
  }

  return (
    <div className="space-y-3">
      {/* Summary Bar */}
      <div className={cn(
        'flex items-center justify-between rounded-lg border px-4 py-2.5',
        overallStatusStyle(checklist.overallStatus)
      )}>
        <div className="flex items-center gap-2">
          {statusIcon(
            checklist.overallStatus === 'CONFORMANT' ? 'PASS' : checklist.overallStatus === 'NON_CONFORMANT' ? 'FAIL' : 'WARNING',
            'w-5 h-5'
          )}
          <span className="font-semibold text-sm">{overallStatusLabel(checklist.overallStatus)}</span>
          <span className="text-xs opacity-70">({checklist.certificateType})</span>
        </div>
        <div className="flex items-center gap-3 text-xs">
          <span className="flex items-center gap-1">
            <CheckCircle className="w-3 h-3 text-green-500" /> {checklist.passCount}
          </span>
          {checklist.failCount > 0 && (
            <span className="flex items-center gap-1">
              <XCircle className="w-3 h-3 text-red-500" /> {checklist.failCount}
            </span>
          )}
          {checklist.warningCount > 0 && (
            <span className="flex items-center gap-1">
              <AlertTriangle className="w-3 h-3 text-yellow-500" /> {checklist.warningCount}
            </span>
          )}
          {checklist.naCount > 0 && (
            <span className="flex items-center gap-1">
              <MinusCircle className="w-3 h-3 text-gray-400" /> {checklist.naCount}
            </span>
          )}
        </div>
      </div>

      {/* Category Groups */}
      <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden bg-white dark:bg-gray-800">
        {groups.map((group) => (
          <CategorySection
            key={group.category}
            group={group}
            defaultOpen={group.failCount > 0 || group.warningCount > 0}
          />
        ))}
      </div>
    </div>
  );
}
