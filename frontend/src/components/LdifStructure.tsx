/**
 * @file LdifStructure.tsx
 * @brief LDIF File Structure Visualization Component (TreeViewer)
 *
 * Displays LDIF file structure in a hierarchical tree view based on DN hierarchy.
 * Shows DN components, objectClass, attributes, and binary data indicators.
 *
 * @version v2.3.0 (Refactored with TreeViewer)
 * @date 2026-02-01
 */

import React, { useState, useEffect, useMemo } from 'react';
import { Loader2, AlertCircle } from 'lucide-react';
import { uploadHistoryApi } from '@/services/pkdApi';
import type { LdifStructureData, LdifEntry } from '@/types';
import { TreeViewer } from './TreeViewer';
import type { TreeNode } from './TreeViewer';

interface LdifStructureProps {
  uploadId: string;
}

/**
 * Tree Node Structure (from original buildDnTree)
 */
interface DnTreeNode {
  rdn: string;
  fullDn: string;
  children: Map<string, DnTreeNode>;
  entries: LdifEntry[];
  level: number;
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
  const baseDnSuffix = ['dc=int', 'dc=icao', 'dc=pkd', 'dc=download'];

  let hasBaseDn = true;
  for (let i = 0; i < baseDnSuffix.length; i++) {
    if (components[i] !== baseDnSuffix[i]) {
      hasBaseDn = false;
      break;
    }
  }

  if (hasBaseDn && components.length > baseDnSuffix.length) {
    return components.slice(baseDnSuffix.length);
  }

  return components;
};

/**
 * Build hierarchical tree from LDIF entries
 */
const buildDnTree = (entries: LdifEntry[]): DnTreeNode => {
  const root: DnTreeNode = {
    rdn: 'ROOT',
    fullDn: '',
    children: new Map(),
    entries: [],
    level: 0
  };

  entries.forEach(entry => {
    let components = splitDn(entry.dn).reverse();
    components = removeBaseDn(components);

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

    currentNode.entries.push(entry);
  });

  return root;
};

/**
 * Convert DN tree to TreeNode format (recursive)
 */
function convertDnTreeToTreeNode(dnNode: DnTreeNode, nodeId: string): TreeNode {
  const children: TreeNode[] = [];

  // Add child DN nodes
  dnNode.children.forEach((childDnNode, rdn) => {
    children.push(convertDnTreeToTreeNode(childDnNode, `${nodeId}-dn-${rdn}`));
  });

  // Add entries at this DN level
  dnNode.entries.forEach((entry, entryIdx) => {
    const attributeNodes: TreeNode[] = entry.attributes.map((attr, attrIdx) => ({
      id: `${nodeId}-entry-${entryIdx}-attr-${attrIdx}`,
      name: attr.name,
      value: attr.isBinary ? `[Binary: ${attr.value}]` : attr.value,
      icon: attr.isBinary ? 'file-text' : 'hash',
    }));

    children.push({
      id: `${nodeId}-entry-${entryIdx}`,
      name: `${entry.objectClass} (Line ${entry.lineNumber})`,
      value: truncateRdn(entry.dn, 80),
      icon: 'file-text',
      children: attributeNodes,
    });
  });

  const isRoot = dnNode.rdn === 'ROOT';
  const displayRdn = isRoot ? 'dc=download,dc=pkd,dc=icao,dc=int' : truncateRdn(dnNode.rdn);

  return {
    id: nodeId,
    name: displayRdn,
    value: dnNode.entries.length > 0 ? `${dnNode.entries.length}개 엔트리` : undefined,
    icon: isRoot ? 'shield' : 'key',
    children: children.length > 0 ? children : undefined,
  };
}

/**
 * LDIF Structure Main Component
 */
export const LdifStructure: React.FC<LdifStructureProps> = ({ uploadId }) => {
  const [data, setData] = useState<LdifStructureData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [maxEntries, setMaxEntries] = useState(100);

  // Build DN tree and convert to TreeNode
  const treeData = useMemo((): TreeNode[] => {
    if (!data) return [];
    const dnTree = buildDnTree(data.entries);
    return [convertDnTreeToTreeNode(dnTree, 'root')];
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
  if (!data) {
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
      <TreeViewer
        data={treeData}
        height="600px"
      />

      {/* Entry Count Footer */}
      <div className="text-center text-sm text-gray-500 dark:text-gray-400">
        {data.entries.length.toLocaleString()}개 엔트리 표시됨
      </div>
    </div>
  );
};
