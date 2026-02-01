/**
 * @file LdifStructure.tsx
 * @brief LDIF File Structure Visualization Component (Tree View)
 *
 * Displays LDIF file structure in a hierarchical tree view based on DN hierarchy.
 * Shows DN components, objectClass, attributes, and binary data indicators.
 *
 * @version v2.2.2
 * @date 2026-02-01
 */

import React, { useState, useEffect, useMemo } from 'react';
import { ChevronDown, ChevronRight, AlertCircle, Loader2, ChevronsDown, ChevronsUp } from 'lucide-react';
import { uploadHistoryApi } from '@/services/pkdApi';
import type { LdifStructureData, LdifEntry } from '@/types';

interface LdifStructureProps {
  uploadId: string;
}

/**
 * Tree Node Structure
 */
interface TreeNode {
  rdn: string;  // Relative Distinguished Name (e.g., "dc=int")
  fullDn: string;  // Complete DN path
  children: Map<string, TreeNode>;
  entries: LdifEntry[];  // Actual LDIF entries at this DN
  level: number;  // Tree depth level
}

/**
 * Unescape LDAP special characters
 */
const unescapeRdn = (rdn: string): string => {
  return rdn.replace(/\\(.)/g, '$1');
};

/**
 * Truncate long RDN for display
 */
const truncateRdn = (rdn: string, maxLength: number = 60): string => {
  const unescaped = unescapeRdn(rdn);
  if (unescaped.length <= maxLength) {
    return unescaped;
  }
  return unescaped.substring(0, maxLength) + '...';
};

/**
 * Split DN by unescaped commas (handles LDAP escaping)
 */
const splitDn = (dn: string): string[] => {
  const components: string[] = [];
  let current = '';
  let escaped = false;

  for (let i = 0; i < dn.length; i++) {
    const char = dn[i];

    if (escaped) {
      current += char;
      escaped = false;
    } else if (char === '\\') {
      current += char;
      escaped = true;
    } else if (char === ',') {
      components.push(current.trim());
      current = '';
    } else {
      current += char;
    }
  }

  if (current) {
    components.push(current.trim());
  }

  return components;
};

/**
 * Remove common base DN suffix from components
 */
const removeBaseDn = (components: string[]): string[] => {
  // Common base DN (in reverse order): dc=int, dc=icao, dc=pkd, dc=download
  const baseDnSuffix = ['dc=int', 'dc=icao', 'dc=pkd', 'dc=download'];

  // Check if components end with base DN
  let hasBaseDn = true;
  for (let i = 0; i < baseDnSuffix.length; i++) {
    if (components[i] !== baseDnSuffix[i]) {
      hasBaseDn = false;
      break;
    }
  }

  // Remove base DN suffix if present
  if (hasBaseDn && components.length > baseDnSuffix.length) {
    return components.slice(baseDnSuffix.length);
  }

  return components;
};

/**
 * Build hierarchical tree from LDIF entries
 */
const buildDnTree = (entries: LdifEntry[]): TreeNode => {
  const root: TreeNode = {
    rdn: 'ROOT',
    fullDn: '',
    children: new Map(),
    entries: [],
    level: 0
  };

  entries.forEach(entry => {
    // Parse DN: "serialNumber=009667006,ou=people,dc=pkd,dc=icao,dc=int"
    // Split by unescaped commas and reverse to get top-down hierarchy
    let components = splitDn(entry.dn).reverse();

    // Remove common base DN to reduce tree depth
    components = removeBaseDn(components);

    // If no components left after removing base DN, skip entry
    if (components.length === 0) {
      root.entries.push(entry);
      return;
    }

    let currentNode = root;
    let fullDnPath = '';

    components.forEach((rdn, index) => {
      fullDnPath = fullDnPath ? `${rdn},${fullDnPath}` : rdn;

      if (!currentNode.children.has(rdn)) {
        currentNode.children.set(rdn, {
          rdn,
          fullDn: fullDnPath,
          children: new Map(),
          entries: [],
          level: index + 1
        });
      }
      currentNode = currentNode.children.get(rdn)!;
    });

    // Add entry to leaf node
    currentNode.entries.push(entry);
  });

  return root;
};

/**
 * Tree Node Component
 */
const TreeNodeComponent: React.FC<{
  node: TreeNode;
  expandedNodes: Set<string>;
  expandedEntries: Set<number>;
  onToggleNode: (dn: string) => void;
  onToggleEntry: (index: number) => void;
}> = ({ node, expandedNodes, expandedEntries, onToggleNode, onToggleEntry }) => {
  const isExpanded = expandedNodes.has(node.fullDn);
  const hasChildren = node.children.size > 0;
  const hasEntries = node.entries.length > 0;

  // Indent based on level
  const indent = node.level * 16;

  const displayRdn = node.rdn === 'ROOT' ? 'dc=download,dc=pkd,dc=icao,dc=int' : node.rdn;
  const isRoot = node.rdn === 'ROOT';

  return (
    <div style={{ marginLeft: `${isRoot ? 0 : indent}px` }}>
      {/* DN Component Node */}
      <div
        className="flex items-center gap-2 py-1 px-2 hover:bg-gray-100 dark:hover:bg-gray-800 rounded cursor-pointer group"
        onClick={() => onToggleNode(node.fullDn)}
      >
        {(hasChildren || hasEntries) ? (
          isExpanded ? (
            <ChevronDown className="w-4 h-4 text-gray-500" />
          ) : (
            <ChevronRight className="w-4 h-4 text-gray-500" />
          )
        ) : (
          <span className="w-4" />
        )}
        <span className={`font-mono text-sm font-semibold ${isRoot ? 'text-purple-600 dark:text-purple-400' : 'text-blue-600 dark:text-blue-400'}`} title={isRoot ? displayRdn : unescapeRdn(node.rdn)}>
          {isRoot ? displayRdn : truncateRdn(node.rdn)}
        </span>
        {hasEntries && (
          <span className="text-xs text-gray-500 dark:text-gray-400">
            ({node.entries.length}개 엔트리)
          </span>
        )}
      </div>

      {/* Expanded content */}
      {(node.rdn === 'ROOT' || isExpanded) && (
        <>
          {/* Child DN nodes */}
          {Array.from(node.children.values()).map(childNode => (
            <TreeNodeComponent
              key={childNode.fullDn}
              node={childNode}
              expandedNodes={expandedNodes}
              expandedEntries={expandedEntries}
              onToggleNode={onToggleNode}
              onToggleEntry={onToggleEntry}
            />
          ))}

          {/* Entries at this DN level */}
          {node.entries.map((entry) => {
            const globalIndex = entry.lineNumber;  // Use lineNumber as unique ID
            const entryExpanded = expandedEntries.has(globalIndex);

            return (
              <div
                key={globalIndex}
                className="ml-4 border-l-2 border-gray-300 dark:border-gray-600 pl-3 my-1"
              >
                {/* Entry header */}
                <div
                  className="flex items-start gap-2 py-1 px-2 hover:bg-gray-50 dark:hover:bg-gray-900 rounded cursor-pointer"
                  onClick={() => onToggleEntry(globalIndex)}
                >
                  {entryExpanded ? (
                    <ChevronDown className="w-3 h-3 text-gray-500 mt-0.5" />
                  ) : (
                    <ChevronRight className="w-3 h-3 text-gray-500 mt-0.5" />
                  )}
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center gap-2 flex-wrap">
                      <span className="text-xs px-2 py-0.5 bg-purple-100 dark:bg-purple-900 text-purple-700 dark:text-purple-300 rounded-full font-mono">
                        {entry.objectClass}
                      </span>
                      <span className="text-xs text-gray-500 dark:text-gray-400">
                        Line {entry.lineNumber}
                      </span>
                      <span className="text-xs text-gray-400 dark:text-gray-500 font-mono truncate">
                        {entry.dn}
                      </span>
                    </div>
                  </div>
                </div>

                {/* Entry attributes */}
                {entryExpanded && (
                  <div className="ml-5 mt-1 space-y-0.5 bg-gray-50 dark:bg-gray-900 rounded p-2">
                    {entry.attributes.map((attr, attrIdx) => (
                      <div key={attrIdx} className="flex gap-2 text-xs font-mono">
                        <span className="text-green-600 dark:text-green-400 font-semibold min-w-[180px]">
                          {attr.name}:
                        </span>
                        {attr.isBinary ? (
                          <span className="text-orange-600 dark:text-orange-400 font-semibold">
                            {attr.value}
                          </span>
                        ) : (
                          <span className="text-gray-700 dark:text-gray-300 break-all">
                            {attr.value}
                          </span>
                        )}
                      </div>
                    ))}
                  </div>
                )}
              </div>
            );
          })}
        </>
      )}
    </div>
  );
};

/**
 * LDIF Structure Main Component
 */
export const LdifStructure: React.FC<LdifStructureProps> = ({ uploadId }) => {
  const [data, setData] = useState<LdifStructureData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [expandedNodes, setExpandedNodes] = useState<Set<string>>(new Set());
  const [expandedEntries, setExpandedEntries] = useState<Set<number>>(new Set());
  const [maxEntries, setMaxEntries] = useState(100);

  // Build tree structure
  const tree = useMemo(() => {
    if (!data) return null;
    return buildDnTree(data.entries);
  }, [data]);

  // Fetch LDIF structure
  useEffect(() => {
    const fetchStructure = async () => {
      setLoading(true);
      setError(null);

      try {
        const response = await uploadHistoryApi.getLdifStructure(uploadId, maxEntries);

        if (response.data.success && response.data.data) {
          setData(response.data.data);
        } else {
          setError(response.data.error || 'Failed to load LDIF structure');
        }
      } catch (err: any) {
        console.error('LDIF structure fetch error:', err);
        setError(err.message || 'Network error');
      } finally {
        setLoading(false);
      }
    };

    fetchStructure();
  }, [uploadId, maxEntries]);

  // Toggle DN node
  const toggleNode = (dn: string) => {
    setExpandedNodes((prev) => {
      const newSet = new Set(prev);
      if (newSet.has(dn)) {
        newSet.delete(dn);
      } else {
        newSet.add(dn);
      }
      return newSet;
    });
  };

  // Toggle entry expansion
  const toggleEntry = (index: number) => {
    setExpandedEntries((prev) => {
      const newSet = new Set(prev);
      if (newSet.has(index)) {
        newSet.delete(index);
      } else {
        newSet.add(index);
      }
      return newSet;
    });
  };

  // Expand all nodes
  const expandAll = () => {
    if (!tree) return;

    const allNodes = new Set<string>();
    const allEntries = new Set<number>();

    const traverse = (node: TreeNode) => {
      if (node.fullDn) {
        allNodes.add(node.fullDn);
      }
      node.entries.forEach(e => allEntries.add(e.lineNumber));
      node.children.forEach(child => traverse(child));
    };

    traverse(tree);
    setExpandedNodes(allNodes);
    setExpandedEntries(allEntries);
  };

  // Collapse all nodes
  const collapseAll = () => {
    setExpandedNodes(new Set());
    setExpandedEntries(new Set());
  };

  const isAllExpanded = tree &&
    expandedNodes.size > 0 &&
    expandedEntries.size === data?.entries.length;

  // Loading state
  if (loading) {
    return (
      <div className="flex items-center justify-center py-12">
        <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        <span className="ml-3 text-gray-600 dark:text-gray-400">
          LDIF 구조 로딩 중...
        </span>
      </div>
    );
  }

  // Error state
  if (error) {
    return (
      <div className="flex items-start gap-3 p-4 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg">
        <AlertCircle className="w-5 h-5 text-red-600 dark:text-red-400 mt-0.5" />
        <div>
          <div className="font-semibold text-red-800 dark:text-red-300">
            LDIF 구조 로드 실패
          </div>
          <div className="text-sm text-red-700 dark:text-red-400 mt-1">{error}</div>
        </div>
      </div>
    );
  }

  // No data
  if (!data || !tree) {
    return (
      <div className="text-center py-12 text-gray-500 dark:text-gray-400">
        LDIF 구조 데이터가 없습니다.
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* Summary Section */}
      <div className="grid grid-cols-3 gap-4 p-4 bg-blue-50 dark:bg-blue-900/20 rounded-lg border border-blue-200 dark:border-blue-800">
        <div className="text-center">
          <div className="text-2xl font-bold text-blue-600 dark:text-blue-400">
            {data.totalEntries.toLocaleString()}
          </div>
          <div className="text-sm text-gray-600 dark:text-gray-400">총 엔트리</div>
        </div>
        <div className="text-center">
          <div className="text-2xl font-bold text-green-600 dark:text-green-400">
            {data.displayedEntries.toLocaleString()}
          </div>
          <div className="text-sm text-gray-600 dark:text-gray-400">표시 중</div>
        </div>
        <div className="text-center">
          <div className="text-2xl font-bold text-purple-600 dark:text-purple-400">
            {data.totalAttributes.toLocaleString()}
          </div>
          <div className="text-sm text-gray-600 dark:text-gray-400">총 속성</div>
        </div>
      </div>

      {/* ObjectClass Counts */}
      <div className="flex flex-wrap gap-2">
        {Object.entries(data.objectClassCounts).map(([className, count]) => (
          <div
            key={className}
            className="px-3 py-1.5 bg-purple-100 dark:bg-purple-900 text-purple-700 dark:text-purple-300 rounded-full text-sm"
          >
            <span className="font-semibold">{className}</span>:{' '}
            <span>{count.toLocaleString()}개</span>
          </div>
        ))}
      </div>

      {/* Entry Limit Selector */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <label className="text-sm font-medium text-gray-700 dark:text-gray-300">
            표시 개수:
          </label>
          <select
            value={maxEntries}
            onChange={(e) => setMaxEntries(Number(e.target.value))}
            className="px-3 py-1.5 border border-gray-300 dark:border-gray-600 rounded-md bg-white dark:bg-gray-800 text-sm"
          >
            <option value="50">50 엔트리</option>
            <option value="100">100 엔트리</option>
            <option value="500">500 엔트리</option>
            <option value="1000">1000 엔트리</option>
            <option value="10000">전체</option>
          </select>
        </div>

        <div className="flex gap-2">
          <button
            onClick={isAllExpanded ? collapseAll : expandAll}
            className="px-3 py-1.5 text-sm bg-blue-100 dark:bg-blue-900 text-blue-700 dark:text-blue-300 rounded-md hover:bg-blue-200 dark:hover:bg-blue-800 transition-colors flex items-center gap-1.5"
          >
            {isAllExpanded ? (
              <>
                <ChevronsUp className="w-4 h-4" />
                모두 접기
              </>
            ) : (
              <>
                <ChevronsDown className="w-4 h-4" />
                모두 펼치기
              </>
            )}
          </button>
        </div>
      </div>

      {/* Truncation Warning */}
      {data.truncated && (
        <div className="flex items-start gap-2 p-3 bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded-lg">
          <AlertCircle className="w-5 h-5 text-yellow-600 dark:text-yellow-400 mt-0.5" />
          <div className="text-sm text-yellow-800 dark:text-yellow-300">
            <strong>표시 제한:</strong> 전체 {data.totalEntries.toLocaleString()}개 중{' '}
            {data.displayedEntries.toLocaleString()}개만 표시됩니다. 더 많은 엔트리를
            보려면 표시 개수를 늘려주세요.
          </div>
        </div>
      )}

      {/* DN Hierarchy Tree */}
      <div className="border border-gray-300 dark:border-gray-600 rounded-lg p-4 bg-white dark:bg-gray-900 max-h-[600px] overflow-y-auto">
        <TreeNodeComponent
          node={tree}
          expandedNodes={expandedNodes}
          expandedEntries={expandedEntries}
          onToggleNode={toggleNode}
          onToggleEntry={toggleEntry}
        />
      </div>

      {/* Entry Count Footer */}
      <div className="text-center text-sm text-gray-500 dark:text-gray-400">
        {data.entries.length.toLocaleString()}개 엔트리 표시됨
      </div>
    </div>
  );
};
