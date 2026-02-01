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
}

/**
 * Icon mapping function
 */
const getIcon = (iconName?: string) => {
  const iconProps = { className: "w-4 h-4 flex-shrink-0" };

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
  onNodeClick
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

    return (
      <div
        style={style}
        className="flex items-center gap-2 px-2 py-1 hover:bg-gray-100 dark:hover:bg-gray-700/50 rounded transition-colors cursor-pointer"
        onClick={() => {
          if (isLeaf) {
            if (onNodeClick) {
              onNodeClick(nodeData);
            }
          } else {
            node.toggle();
          }
        }}
      >
        {/* Expand/Collapse Arrow */}
        {!isLeaf && (
          <div className="w-4 h-4 flex items-center justify-center text-gray-500">
            {node.isOpen ? (
              <ChevronDown className="w-4 h-4" />
            ) : (
              <ChevronRight className="w-4 h-4" />
            )}
          </div>
        )}
        {isLeaf && <div className="w-4" />}

        {/* Icon */}
        {nodeData.icon && (
          <div className="text-gray-600 dark:text-gray-400">
            {getIcon(nodeData.icon)}
          </div>
        )}

        {/* Name */}
        <span className="font-medium text-gray-700 dark:text-gray-300">
          {nodeData.name}:
        </span>

        {/* Value */}
        {nodeData.value && (
          <span className="text-gray-900 dark:text-white font-mono text-sm truncate flex-1">
            {nodeData.value}
          </span>
        )}

        {/* Copy Button */}
        {nodeData.copyable && nodeData.value && (
          <button
            onClick={(e) => {
              e.stopPropagation();
              copyToClipboard(nodeData.value!, nodeData.id);
            }}
            className="ml-auto p-1 hover:bg-gray-200 dark:hover:bg-gray-600 rounded transition-colors"
            title="Copy to clipboard"
          >
            {copiedId === nodeData.id ? (
              <CheckCircle className="w-4 h-4 text-green-500" />
            ) : (
              <Copy className="w-4 h-4 text-gray-400" />
            )}
          </button>
        )}

        {/* Link Button */}
        {nodeData.linkUrl && (
          <a
            href={nodeData.linkUrl}
            target="_blank"
            rel="noopener noreferrer"
            onClick={(e) => e.stopPropagation()}
            className="ml-auto p-1 hover:bg-gray-200 dark:hover:bg-gray-600 rounded transition-colors text-blue-600 dark:text-blue-400"
            title="Open link"
          >
            <ExternalLink className="w-4 h-4" />
          </a>
        )}
      </div>
    );
  };

  // Calculate tree height from container height (subtract padding)
  const treeHeight = parseInt(height) - 16; // p-2 = 8px padding * 2 = 16px

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
          indent={24}
          rowHeight={32}
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
