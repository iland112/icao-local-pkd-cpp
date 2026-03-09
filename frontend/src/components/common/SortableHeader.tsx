import { ChevronUp, ChevronDown, ChevronsUpDown } from 'lucide-react';
import type { SortConfig } from '@/hooks/useSortableTable';

interface SortableHeaderProps {
  label: string;
  sortKey: string;
  sortConfig: SortConfig | null;
  onSort: (key: string) => void;
  className?: string;
  as?: 'th' | 'div';
  children?: React.ReactNode;
}

export function SortableHeader({
  label,
  sortKey,
  sortConfig,
  onSort,
  className = '',
  as: Tag = 'th',
  children,
}: SortableHeaderProps) {
  const isActive = sortConfig?.key === sortKey;
  const direction = isActive ? sortConfig.direction : null;

  return (
    <Tag
      className={`cursor-pointer select-none group ${className}`}
      onClick={() => onSort(sortKey)}
    >
      <div className="inline-flex items-center gap-0.5">
        {children || label}
        <span className={`inline-flex ${isActive ? 'text-blue-500' : 'text-gray-500 dark:text-gray-300 opacity-0 group-hover:opacity-100'} transition-opacity`}>
          {direction === 'asc' ? (
            <ChevronUp className="w-3 h-3" />
          ) : direction === 'desc' ? (
            <ChevronDown className="w-3 h-3" />
          ) : (
            <ChevronsUpDown className="w-3 h-3" />
          )}
        </span>
      </div>
    </Tag>
  );
}
