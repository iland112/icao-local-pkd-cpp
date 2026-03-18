import { useTranslation } from 'react-i18next';
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
function convertDnTreeToTreeNode(dnNode: DnTreeNode, nodeId: string, t: (key: string, opts?: Record<string, unknown>) => string): TreeNode {
  const children: TreeNode[] = [];

  // Add child DN nodes
  dnNode.children.forEach((childDnNode, rdn) => {
    children.push(convertDnTreeToTreeNode(childDnNode, `${nodeId}-dn-${rdn}`, t));
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
    value: dnNode.entries.length > 0 ? t('upload:ldifStructure.entriesCount', { num: dnNode.entries.length }) : undefined,
    icon: isRoot ? 'shield' : 'key',
    children: children.length > 0 ? children : undefined,
  };
}

/**
 * LDIF Structure Main Component
 */
export const LdifStructure: React.FC<LdifStructureProps> = ({ uploadId }) => {
  const { t } = useTranslation(['upload', 'common']);
  const [data, setData] = useState<LdifStructureData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [maxEntries, setMaxEntries] = useState(100);

  // Build DN tree and convert to TreeNode
  const treeData = useMemo((): TreeNode[] => {
    if (!data) return [];
    const dnTree = buildDnTree(data.entries);
    return [convertDnTreeToTreeNode(dnTree, 'root', t)];
  }, [data, t]);

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
        if (import.meta.env.DEV) console.error('LDIF structure fetch error:', err);
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
          {t('upload:ldifStructure.loading')}
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
            {t('upload:ldifStructure.loadFailed')}
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
        {t('upload:ldifStructure.noData')}
      </div>
    );
  }

  return (
    <div className="space-y-2.5">
      {/* Summary + ObjectClass + Limit — compact single row */}
      <div className="flex items-center justify-between gap-3 bg-blue-50 dark:bg-blue-900/20 rounded-lg border border-blue-200 dark:border-blue-800 px-3 py-2">
        <div className="flex items-center gap-4">
          <div className="text-center">
            <div className="text-lg font-bold text-blue-600 dark:text-blue-400">{data.totalEntries.toLocaleString()}</div>
            <div className="text-xs text-gray-500 dark:text-gray-400">{t('upload:ldifStructure.totalEntries')}</div>
          </div>
          <div className="text-center">
            <div className="text-lg font-bold text-green-600 dark:text-green-400">{data.displayedEntries.toLocaleString()}</div>
            <div className="text-xs text-gray-500 dark:text-gray-400">{t('upload:ldifStructure.displayed')}</div>
          </div>
          <div className="text-center">
            <div className="text-lg font-bold text-purple-600 dark:text-purple-400">{data.totalAttributes.toLocaleString()}</div>
            <div className="text-xs text-gray-500 dark:text-gray-400">{t('upload:ldifStructure.totalAttributes')}</div>
          </div>
        </div>
        <div className="flex items-center gap-2 flex-wrap">
          {Object.entries(data.objectClassCounts).map(([className, count]) => (
            <span key={className} className="px-2 py-0.5 bg-purple-100 dark:bg-purple-900 text-purple-700 dark:text-purple-300 rounded-full text-xs font-medium">
              {className}: {count.toLocaleString()}
            </span>
          ))}
        </div>
        <div className="flex items-center gap-2 flex-shrink-0">
          <label className="text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">{t('upload:ldifStructure.displayCount')}:</label>
          <select
            value={maxEntries}
            onChange={(e) => setMaxEntries(Number(e.target.value))}
            className="px-2 py-0.5 border border-gray-300 dark:border-gray-600 rounded bg-white dark:bg-gray-800 text-xs"
          >
            <option value="50">50</option>
            <option value="100">100</option>
            <option value="500">500</option>
            <option value="1000">1,000</option>
            <option value="10000">{t('upload:ldifStructure.all')}</option>
          </select>
        </div>
      </div>

      {/* Truncation Warning — compact */}
      {data.truncated && (
        <div className="flex items-center gap-2 px-3 py-1.5 bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800 rounded text-xs text-yellow-800 dark:text-yellow-300">
          <AlertCircle className="w-3.5 h-3.5 text-yellow-600 dark:text-yellow-400 flex-shrink-0" />
          <span><strong>{t('upload:ldifStructure.truncatedLabel')}</strong> {t('upload:ldifStructure.truncatedMessage', { total: data.totalEntries.toLocaleString(), displayed: data.displayedEntries.toLocaleString() })}</span>
        </div>
      )}

      {/* DN Hierarchy Tree — compact mode */}
      <TreeViewer
        data={treeData}
        height="480px"
        compact
      />

      {/* Entry Count Footer */}
      <div className="text-center text-xs text-gray-400 dark:text-gray-500">
        {t('upload:ldifStructure.entriesDisplayed', { num: data.entries.length.toLocaleString() })}
      </div>
    </div>
  );
};
