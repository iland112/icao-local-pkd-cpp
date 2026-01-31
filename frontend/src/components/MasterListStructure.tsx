import { useState, useEffect } from 'react';
import { Loader2, ChevronDown, ChevronRight, FileCode, AlertCircle } from 'lucide-react';
import axios from 'axios';
import { cn } from '@/utils/cn';

interface MasterListStructureProps {
  uploadId: string;
}

interface Asn1Node {
  offset: number;
  depth: number;
  headerLength: number;
  length: number;
  tag: string;
  isConstructed: boolean;
  value?: string;
  children: Asn1Node[];
}

interface ApiResponse {
  success: boolean;
  fileName?: string;
  fileSize?: number;
  asn1Tree?: Asn1Node[];
  statistics?: {
    totalNodes: number;
    constructedNodes: number;
    primitiveNodes: number;
  };
  maxLines?: number;
  truncated?: boolean;
  error?: string;
}

export function MasterListStructure({ uploadId }: MasterListStructureProps) {
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [data, setData] = useState<ApiResponse | null>(null);
  const [maxLines, setMaxLines] = useState(100);
  const [expandedNodes, setExpandedNodes] = useState<Set<string>>(new Set(['0']));

  useEffect(() => {
    fetchStructure();
  }, [uploadId, maxLines]);

  const fetchStructure = async () => {
    setLoading(true);
    setError(null);

    try {
      const response = await axios.get<ApiResponse>(
        `/api/upload/${uploadId}/masterlist-structure?maxLines=${maxLines}`
      );

      if (response.data.success) {
        setData(response.data);
        // Auto-expand first level
        setExpandedNodes(new Set(['0', '0-0', '0-1']));
      } else {
        setError(response.data.error || 'Failed to fetch Master List structure');
      }
    } catch (err: any) {
      setError(err.response?.data?.error || 'Failed to fetch Master List structure');
      console.error('Failed to fetch Master List structure:', err);
    } finally {
      setLoading(false);
    }
  };

  const toggleNode = (nodeId: string) => {
    setExpandedNodes(prev => {
      const next = new Set(prev);
      if (next.has(nodeId)) {
        next.delete(nodeId);
      } else {
        next.add(nodeId);
      }
      return next;
    });
  };

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
  };

  const renderNode = (node: Asn1Node, path: string, depth: number): React.ReactElement => {
    const nodeId = path;
    const isExpanded = expandedNodes.has(nodeId);
    const hasChildren = node.children && node.children.length > 0;

    // TLV display
    const tlvInfo = `T:${node.tag} L:${node.length}`;

    return (
      <div key={nodeId} className="select-text">
        <div
          className={cn(
            'flex items-start gap-2 py-1 px-2 rounded hover:bg-gray-100 dark:hover:bg-gray-700 cursor-pointer',
            depth === 0 && 'bg-blue-50 dark:bg-blue-900/20'
          )}
          onClick={() => hasChildren && toggleNode(nodeId)}
        >
          {/* Indentation */}
          <div style={{ width: `${depth * 20}px` }} className="flex-shrink-0" />

          {/* Expand/Collapse Icon */}
          <div className="flex-shrink-0 w-4">
            {hasChildren && (
              isExpanded ? (
                <ChevronDown className="w-4 h-4 text-gray-500" />
              ) : (
                <ChevronRight className="w-4 h-4 text-gray-500" />
              )
            )}
          </div>

          {/* Node Info */}
          <div className="flex-1 min-w-0">
            <div className="flex items-center gap-2 flex-wrap">
              <span className="text-xs font-mono text-gray-500 dark:text-gray-400">
                {node.offset}:
              </span>
              <span className={cn(
                'text-xs font-mono px-1.5 py-0.5 rounded',
                node.isConstructed
                  ? 'bg-blue-100 dark:bg-blue-900/50 text-blue-800 dark:text-blue-200'
                  : 'bg-green-100 dark:bg-green-900/50 text-green-800 dark:text-green-200'
              )}>
                {node.tag}
              </span>
              <span className="text-xs text-gray-600 dark:text-gray-400">
                {tlvInfo}
              </span>
              {node.value && (
                <span className="text-xs text-gray-700 dark:text-gray-300 font-mono truncate">
                  : {node.value}
                </span>
              )}
            </div>
          </div>
        </div>

        {/* Children */}
        {hasChildren && isExpanded && (
          <div>
            {node.children.map((child, idx) =>
              renderNode(child, `${path}-${idx}`, depth + 1)
            )}
          </div>
        )}
      </div>
    );
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center py-12">
        <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        <span className="ml-3 text-gray-600 dark:text-gray-400">
          ASN.1 구조 분석 중...
        </span>
      </div>
    );
  }

  if (error) {
    return (
      <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-4">
        <div className="flex items-start gap-2">
          <AlertCircle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
          <div>
            <p className="text-sm font-medium text-red-800 dark:text-red-300">파싱 실패</p>
            <p className="text-sm text-red-600 dark:text-red-400 mt-1">{error}</p>
          </div>
        </div>
      </div>
    );
  }

  if (!data || !data.asn1Tree || data.asn1Tree.length === 0) {
    return (
      <div className="text-center py-8 text-gray-500 dark:text-gray-400">
        ASN.1 구조 정보를 찾을 수 없습니다.
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* Header Info */}
      <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-4">
        <div className="flex items-center justify-between mb-3">
          <div className="flex items-center gap-2">
            <FileCode className="w-5 h-5 text-blue-600 dark:text-blue-400" />
            <h3 className="text-sm font-semibold text-blue-900 dark:text-blue-100">
              Master List ASN.1/DER 구조 (TLV)
            </h3>
          </div>
          {data.truncated && (
            <div className="flex items-center gap-2">
              <select
                value={maxLines}
                onChange={(e) => setMaxLines(Number(e.target.value))}
                className="text-xs border border-blue-300 dark:border-blue-700 rounded px-2 py-1 bg-white dark:bg-gray-800"
              >
                <option value={50}>50 라인</option>
                <option value={100}>100 라인</option>
                <option value={500}>500 라인</option>
                <option value={1000}>1,000 라인</option>
                <option value={5000}>5,000 라인</option>
                <option value={0}>전체 (느림)</option>
              </select>
            </div>
          )}
        </div>

        <div className="grid grid-cols-2 md:grid-cols-4 gap-3 text-xs">
          <div>
            <span className="text-blue-700 dark:text-blue-300">파일:</span>
            <span className="ml-2 text-blue-900 dark:text-blue-100 font-medium">
              {data.fileName || 'Unknown'}
            </span>
          </div>
          <div>
            <span className="text-blue-700 dark:text-blue-300">크기:</span>
            <span className="ml-2 text-blue-900 dark:text-blue-100 font-medium">
              {data.fileSize ? formatBytes(data.fileSize) : 'N/A'}
            </span>
          </div>
          {data.statistics && (
            <>
              <div>
                <span className="text-blue-700 dark:text-blue-300">총 노드:</span>
                <span className="ml-2 text-blue-900 dark:text-blue-100 font-medium">
                  {data.statistics.totalNodes}
                </span>
              </div>
              <div>
                <span className="text-blue-700 dark:text-blue-300">구조:</span>
                <span className="ml-2 text-blue-900 dark:text-blue-100 font-medium">
                  {data.statistics.constructedNodes}개 / Prim {data.statistics.primitiveNodes}개
                </span>
              </div>
            </>
          )}
        </div>

        {data.truncated && (
          <div className="mt-3 flex items-center gap-2 text-xs text-yellow-700 dark:text-yellow-300 bg-yellow-100 dark:bg-yellow-900/30 px-3 py-2 rounded">
            <AlertCircle className="w-4 h-4 flex-shrink-0" />
            <span>
              출력이 {data.maxLines}개 라인으로 제한되었습니다. 전체 구조를 보려면 위에서 "전체"를 선택하세요.
            </span>
          </div>
        )}
      </div>

      {/* ASN.1 Tree */}
      <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
        <div className="bg-gray-50 dark:bg-gray-800 px-4 py-2 border-b border-gray-200 dark:border-gray-700">
          <div className="flex items-center justify-between">
            <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">
              TLV (Tag-Length-Value) Tree
            </span>
            <div className="flex items-center gap-2 text-xs">
              <span className="px-2 py-1 bg-blue-100 dark:bg-blue-900/50 text-blue-800 dark:text-blue-200 rounded">
                Constructed
              </span>
              <span className="px-2 py-1 bg-green-100 dark:bg-green-900/50 text-green-800 dark:text-green-200 rounded">
                Primitive
              </span>
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-900 p-3 max-h-96 overflow-y-auto font-mono text-xs">
          {data.asn1Tree.map((node, idx) =>
            renderNode(node, `${idx}`, 0)
          )}
        </div>
      </div>
    </div>
  );
}
