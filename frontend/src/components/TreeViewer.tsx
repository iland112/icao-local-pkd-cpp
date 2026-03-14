/**
 * @file TreeViewer.tsx
 * @brief Reusable Tree Component based on react-arborist
 *
 * Provides a consistent tree visualization across the application with:
 * - Icon support
 * - Copy to clipboard functionality
 * - Clickable links
 * - Dark mode support
 * - Expand/collapse all
 * - Keyboard navigation
 *
 * @version v2.3.0
 * @date 2026-02-01
 */

import React, { useState } from 'react';
import { Tree } from 'react-arborist';
import {
  FileText, Shield, User, Calendar, Key, Lock, Settings, Hash, Link2,
  Copy, CheckCircle, ChevronRight, ChevronDown, ExternalLink, AlertCircle
} from 'lucide-react';

/**
 * Tree Node Interface
 */
export interface TreeNode {
  id: string;
  name: string;
  value?: string;
  children?: TreeNode[];
  copyable?: boolean;
  linkUrl?: string;
  icon?: string;
}

/**
 * TreeViewer Props
 */
interface TreeViewerProps {
  data: TreeNode[];
  height?: string;
  className?: string;
  onNodeClick?: (node: TreeNode) => void;
  /** compact mode: 더 작은 폰트/아이콘/행 높이 */
  compact?: boolean;
}

/**
 * Icon mapping function
 */
const getIcon = (iconName?: string, small = false) => {
  const iconProps = { className: small ? "w-3 h-3 flex-shrink-0" : "w-4 h-4 flex-shrink-0" };

  // Handle country flag SVGs
  if (iconName?.startsWith('flag-')) {
    const countryCode = iconName.replace('flag-', '').toLowerCase();
    return (
      <img
        src={`/svg/${countryCode}.svg`}
        alt={countryCode.toUpperCase()}
        className="w-4 h-4 flex-shrink-0 rounded-sm"
        onError={(e) => {
          // Fallback to white flag if SVG not found
          e.currentTarget.src = 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><text y="18" font-size="20">🏳️</text></svg>';
        }}
      />
    );
  }

  switch (iconName) {
    case 'file-text': return <FileText {...iconProps} />;
    case 'shield': return <Shield {...iconProps} />;
    case 'user': return <User {...iconProps} />;
    case 'calendar': return <Calendar {...iconProps} />;
    case 'key': return <Key {...iconProps} />;
    case 'lock': return <Lock {...iconProps} />;
    case 'settings': return <Settings {...iconProps} />;
    case 'hash': return <Hash {...iconProps} />;
    case 'link-2': return <Link2 {...iconProps} />;
    case 'alert-circle': return <AlertCircle {...iconProps} />;
    case 'chevron-right': return <ChevronRight {...iconProps} />;
    case 'chevron-down': return <ChevronDown {...iconProps} />;
    default: return null;
  }
};

/**
 * TreeViewer Component
 */
export const TreeViewer: React.FC<TreeViewerProps> = ({
  data,
  height = '384px',
  className = '',
  onNodeClick,
  compact = false,
}) => {
  const [copiedId, setCopiedId] = useState<string | null>(null);

  /**
   * Copy to clipboard
   */
  const copyToClipboard = (text: string, nodeId: string) => {
    navigator.clipboard.writeText(text);
    setCopiedId(nodeId);
    setTimeout(() => setCopiedId(null), 2000);
  };

  /**
   * Render tree node
   */
  const renderNode = ({ node, style }: any) => {
    const nodeData = node.data as TreeNode;
    const isLeaf = !nodeData.children || nodeData.children.length === 0;
    const iconSz  = compact ? 'w-3 h-3' : 'w-4 h-4';
    const nameCls = compact ? 'text-[11px] font-medium text-gray-700 dark:text-gray-300' : 'font-medium text-gray-700 dark:text-gray-300';
    const valCls  = compact ? 'text-[11px] font-mono text-gray-900 dark:text-white truncate flex-1' : 'text-gray-900 dark:text-white font-mono text-sm truncate flex-1';

    return (
      <div
        style={style}
        className="flex items-center gap-1.5 px-2 py-0.5 hover:bg-gray-100 dark:hover:bg-gray-700/50 rounded transition-colors cursor-pointer"
        onClick={() => {
          if (isLeaf) { if (onNodeClick) onNodeClick(nodeData); }
          else node.toggle();
        }}
      >
        {/* Expand/Collapse Arrow */}
        {!isLeaf ? (
          <div className={`${iconSz} flex items-center justify-center text-gray-500 flex-shrink-0`}>
            {node.isOpen ? <ChevronDown className={iconSz} /> : <ChevronRight className={iconSz} />}
          </div>
        ) : (
          <div className={iconSz} />
        )}

        {/* Icon */}
        {nodeData.icon && (
          <div className="text-gray-600 dark:text-gray-400 flex-shrink-0">
            {getIcon(nodeData.icon, compact)}
          </div>
        )}

        {/* Name */}
        <span className={nameCls}>{nodeData.name}:</span>

        {/* Value */}
        {nodeData.value && (
          <span className={valCls}>{nodeData.value}</span>
        )}

        {/* Copy Button */}
        {nodeData.copyable && nodeData.value && (
          <button
            onClick={(e) => { e.stopPropagation(); copyToClipboard(nodeData.value!, nodeData.id); }}
            className="ml-auto p-0.5 hover:bg-gray-200 dark:hover:bg-gray-600 rounded transition-colors flex-shrink-0"
            title="Copy to clipboard"
          >
            {copiedId === nodeData.id
              ? <CheckCircle className={`${iconSz} text-green-500`} />
              : <Copy        className={`${iconSz} text-gray-400`} />}
          </button>
        )}

        {/* Link Button */}
        {nodeData.linkUrl && (
          <a href={nodeData.linkUrl} target="_blank" rel="noopener noreferrer"
            onClick={(e) => e.stopPropagation()}
            className="ml-auto p-0.5 hover:bg-gray-200 dark:hover:bg-gray-600 rounded transition-colors text-blue-600 dark:text-blue-400 flex-shrink-0"
            title="Open link"
          >
            <ExternalLink className={iconSz} />
          </a>
        )}
      </div>
    );
  };

  // Calculate tree height from container height (subtract padding)
  const treeHeight = parseInt(height) - 16; // p-2 = 8px padding * 2 = 16px
  const rowHeight  = compact ? 24 : 32;
  const indent     = compact ? 18 : 24;

  return (
    <div className={`border border-gray-300 dark:border-gray-600 rounded-lg overflow-hidden ${className}`}>
      <div
        className="bg-white dark:bg-gray-800 p-2"
        style={{ height }}
      >
        <Tree
          data={data}
          openByDefault={false}
          width="100%"
          height={treeHeight}
          indent={indent}
          rowHeight={rowHeight}
          overscanCount={5}
          className="react-arborist"
        >
          {renderNode}
        </Tree>
      </div>
    </div>
  );
};

export default TreeViewer;
