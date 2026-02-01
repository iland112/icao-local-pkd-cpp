import React from 'react';
import { ChevronRight, Shield, ShieldCheck, Link as LinkIcon } from 'lucide-react';

interface TrustChainVisualizationProps {
  trustChainPath: string;
  trustChainValid: boolean;
  compact?: boolean; // Compact mode for table cells
  className?: string;
}

interface ChainNode {
  level: number;
  dn: string;
  isLink: boolean;
  isRoot: boolean;
}

/**
 * Parse trust chain path into structured nodes
 * Example input: "DSC → CN=CSCA Latvia,serialNumber=003 → CN=CSCA Latvia,serialNumber=002 → CN=CSCA Latvia,serialNumber=001"
 */
const parseTrustChainPath = (path: string): ChainNode[] => {
  if (!path || path.trim() === '') {
    return [];
  }

  const parts = path.split('→').map(p => p.trim());
  const nodes: ChainNode[] = [];

  parts.forEach((part, index) => {
    // First node is always DSC/end-entity
    const isRoot = index === parts.length - 1;
    // Link certificates are intermediate nodes (not first, not last)
    const isLink = index > 0 && index < parts.length - 1;

    nodes.push({
      level: index,
      dn: part,
      isLink,
      isRoot
    });
  });

  return nodes;
};

/**
 * Extract CN (Common Name) from DN string
 * Example: "CN=CSCA Latvia,serialNumber=003,..." -> "CSCA Latvia"
 */
const extractCN = (dn: string): string => {
  const cnMatch = dn.match(/CN=([^,]+)/);
  return cnMatch ? cnMatch[1] : dn;
};

/**
 * Extract serial number from DN if present
 * Example: "CN=...,serialNumber=003,..." -> "003"
 */
const extractSerialNumber = (dn: string): string | null => {
  const snMatch = dn.match(/serialNumber=([^,]+)/i);
  return snMatch ? snMatch[1] : null;
};

/**
 * TrustChainVisualization Component
 *
 * Sprint 3 Task 3.6: Visualize certificate trust chain path
 *
 * Displays trust chain path in a user-friendly format:
 * - Compact mode: Single line with arrows (for table cells)
 * - Full mode: Vertical card layout with icons and details
 */
export const TrustChainVisualization: React.FC<TrustChainVisualizationProps> = ({
  trustChainPath,
  trustChainValid,
  compact = false,
  className = ''
}) => {
  const nodes = parseTrustChainPath(trustChainPath);

  if (nodes.length === 0) {
    return (
      <div className={`text-sm text-gray-500 dark:text-gray-400 ${className}`}>
        No trust chain information
      </div>
    );
  }

  // Compact mode: Single line for table cells
  if (compact) {
    return (
      <div className={`flex items-center gap-1 text-xs ${className}`}>
        {nodes.map((node, index) => (
          <React.Fragment key={index}>
            {index > 0 && (
              <ChevronRight className="w-3 h-3 text-gray-400 dark:text-gray-500 flex-shrink-0" />
            )}
            <span
              className={`truncate ${
                node.isRoot
                  ? 'font-semibold text-green-600 dark:text-green-400'
                  : node.isLink
                  ? 'text-blue-600 dark:text-blue-400'
                  : 'text-gray-700 dark:text-gray-300'
              }`}
              title={node.dn}
            >
              {extractCN(node.dn)}
              {extractSerialNumber(node.dn) && (
                <span className="text-gray-500 dark:text-gray-400 ml-1">
                  (#{extractSerialNumber(node.dn)})
                </span>
              )}
            </span>
          </React.Fragment>
        ))}
      </div>
    );
  }

  // Full mode: Vertical card layout
  return (
    <div className={`space-y-2 ${className}`}>
      {/* Trust Chain Header */}
      <div className="flex items-center gap-2 mb-3">
        {trustChainValid ? (
          <ShieldCheck className="w-5 h-5 text-green-500" />
        ) : (
          <Shield className="w-5 h-5 text-red-500" />
        )}
        <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-200">
          Trust Chain Path ({nodes.length} levels)
        </h4>
      </div>

      {/* Trust Chain Nodes */}
      <div className="space-y-2">
        {nodes.map((node, index) => (
          <div key={index} className="relative">
            {/* Connecting Line (except for last node) */}
            {index < nodes.length - 1 && (
              <div className="absolute left-4 top-10 bottom-0 w-0.5 bg-gradient-to-b from-blue-300 to-green-300 dark:from-blue-600 dark:to-green-600" />
            )}

            {/* Node Card */}
            <div
              className={`relative flex items-start gap-3 p-3 rounded-lg border transition-colors ${
                node.isRoot
                  ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
                  : node.isLink
                  ? 'bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800'
                  : 'bg-gray-50 dark:bg-gray-800/50 border-gray-200 dark:border-gray-700'
              }`}
            >
              {/* Node Icon */}
              <div
                className={`flex-shrink-0 w-8 h-8 rounded-full flex items-center justify-center ${
                  node.isRoot
                    ? 'bg-green-100 dark:bg-green-900 text-green-700 dark:text-green-300'
                    : node.isLink
                    ? 'bg-blue-100 dark:bg-blue-900 text-blue-700 dark:text-blue-300'
                    : 'bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300'
                }`}
              >
                {node.isRoot ? (
                  <ShieldCheck className="w-5 h-5" />
                ) : node.isLink ? (
                  <LinkIcon className="w-4 h-4" />
                ) : (
                  <Shield className="w-4 h-4" />
                )}
              </div>

              {/* Node Details */}
              <div className="flex-1 min-w-0">
                {/* Node Type Badge */}
                <div className="flex items-center gap-2 mb-1">
                  <span
                    className={`inline-flex items-center px-2 py-0.5 rounded text-xs font-medium ${
                      node.isRoot
                        ? 'bg-green-100 dark:bg-green-900 text-green-800 dark:text-green-200'
                        : node.isLink
                        ? 'bg-blue-100 dark:bg-blue-900 text-blue-800 dark:text-blue-200'
                        : 'bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-200'
                    }`}
                  >
                    {node.isRoot
                      ? 'Root CSCA'
                      : node.isLink
                      ? 'Link Certificate'
                      : index === 0
                      ? 'DSC (End Entity)'
                      : 'Intermediate CSCA'}
                  </span>
                  <span className="text-xs text-gray-500 dark:text-gray-400">
                    Level {index}
                  </span>
                </div>

                {/* Common Name */}
                <div className="font-medium text-sm text-gray-900 dark:text-gray-100 mb-1">
                  {extractCN(node.dn)}
                  {extractSerialNumber(node.dn) && (
                    <span className="ml-2 text-xs text-gray-600 dark:text-gray-400 font-normal">
                      Serial: {extractSerialNumber(node.dn)}
                    </span>
                  )}
                </div>

                {/* Full DN (truncated) */}
                <div
                  className="text-xs text-gray-600 dark:text-gray-400 truncate"
                  title={node.dn}
                >
                  {node.dn}
                </div>
              </div>
            </div>
          </div>
        ))}
      </div>

      {/* Trust Chain Summary */}
      <div className="mt-3 p-2 bg-gray-50 dark:bg-gray-800/50 rounded border border-gray-200 dark:border-gray-700">
        <div className="text-xs text-gray-600 dark:text-gray-400">
          <div className="flex items-center justify-between">
            <span>Chain Length:</span>
            <span className="font-medium text-gray-900 dark:text-gray-100">
              {nodes.length} certificate{nodes.length !== 1 ? 's' : ''}
            </span>
          </div>
          <div className="flex items-center justify-between mt-1">
            <span>Link Certificates:</span>
            <span className="font-medium text-gray-900 dark:text-gray-100">
              {nodes.filter(n => n.isLink).length}
            </span>
          </div>
          <div className="flex items-center justify-between mt-1">
            <span>Validation Status:</span>
            <span
              className={`font-medium ${
                trustChainValid
                  ? 'text-green-600 dark:text-green-400'
                  : 'text-red-600 dark:text-red-400'
              }`}
            >
              {trustChainValid ? '✓ Valid' : '✗ Invalid'}
            </span>
          </div>
        </div>
      </div>
    </div>
  );
};

export default TrustChainVisualization;
