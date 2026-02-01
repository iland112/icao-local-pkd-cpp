import { useState, useEffect, useMemo } from 'react';
import { Loader2, FileCode, AlertCircle } from 'lucide-react';
import axios from 'axios';
import { TreeViewer } from './TreeViewer';
import type { TreeNode } from './TreeViewer';

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

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
  };

  // Convert ASN.1 nodes to TreeNode format (recursive)
  function convertAsn1ToTreeNode(node: Asn1Node, path: string): TreeNode {
    const tlvInfo = `T:${node.tag} L:${node.length}`;
    const nodeValue = node.value ? `${tlvInfo} : ${node.value}` : tlvInfo;

    return {
      id: path,
      name: `${node.offset}`,
      value: nodeValue,
      icon: node.isConstructed ? 'shield' : 'key',
      children: node.children?.map((child, idx) =>
        convertAsn1ToTreeNode(child, `${path}-${idx}`)
      ),
    };
  }

  // Build tree data from ASN.1 structure
  const treeData = useMemo((): TreeNode[] => {
    if (!data?.asn1Tree) return [];
    return data.asn1Tree.map((node, idx) =>
      convertAsn1ToTreeNode(node, `${idx}`)
    );
  }, [data]);

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
      <div>
        <div className="bg-gray-50 dark:bg-gray-800 px-4 py-2 border border-gray-200 dark:border-gray-700 rounded-t-lg">
          <div className="flex items-center justify-between">
            <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">
              TLV (Tag-Length-Value) Tree
            </span>
            <div className="flex items-center gap-2 text-xs">
              <div className="flex items-center gap-1.5">
                <div className="w-3 h-3 rounded bg-blue-500"></div>
                <span className="text-gray-600 dark:text-gray-400">Constructed</span>
              </div>
              <div className="flex items-center gap-1.5">
                <div className="w-3 h-3 rounded bg-green-500"></div>
                <span className="text-gray-600 dark:text-gray-400">Primitive</span>
              </div>
            </div>
          </div>
        </div>

        <TreeViewer
          data={treeData}
          height="400px"
        />
      </div>
    </div>
  );
}
