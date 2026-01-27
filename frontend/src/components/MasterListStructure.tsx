import { useState, useEffect } from 'react';
import { Loader2, FileText, ChevronDown, ChevronRight, Shield, Link2 } from 'lucide-react';
import axios from 'axios';
import { cn } from '@/utils/cn';

interface MasterListStructureProps {
  uploadId: string;
}

interface SignerInfo {
  subject: string;
  issuer: string;
  serialNumber: string;
  isSelfSigned: boolean;
}

interface Certificate {
  subject: string;
  issuer: string;
  serialNumber: string;
  isSelfSigned: boolean;
  type: string;
}

interface MasterListStructure {
  signerInfoCount: number;
  signerInfos: SignerInfo[];
  pkiDataCertCount: number;
  pkiDataSelfSignedCount: number;
  pkiDataLinkCertCount: number;
  pkiDataCertificates: Certificate[];
}

interface ApiResponse {
  success: boolean;
  fileName?: string;
  fileSize?: number;
  structure?: MasterListStructure;
  error?: string;
}

export function MasterListStructure({ uploadId }: MasterListStructureProps) {
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [data, setData] = useState<ApiResponse | null>(null);
  const [expandedSections, setExpandedSections] = useState({
    signerInfo: true,
    pkiData: false,
  });

  useEffect(() => {
    fetchStructure();
  }, [uploadId]);

  const fetchStructure = async () => {
    setLoading(true);
    setError(null);

    try {
      const response = await axios.get<ApiResponse>(
        `/api/upload/${uploadId}/masterlist-structure`
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

  const toggleSection = (section: 'signerInfo' | 'pkiData') => {
    setExpandedSections(prev => ({
      ...prev,
      [section]: !prev[section],
    }));
  };

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center py-12">
        <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        <span className="ml-3 text-gray-600 dark:text-gray-400">
          Master List 구조 분석 중...
        </span>
      </div>
    );
  }

  if (error) {
    return (
      <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-4">
        <p className="text-red-600 dark:text-red-400">{error}</p>
      </div>
    );
  }

  if (!data || !data.structure) {
    return (
      <div className="text-center py-8 text-gray-500 dark:text-gray-400">
        Master List 구조 정보를 찾을 수 없습니다.
      </div>
    );
  }

  const { structure, fileName, fileSize } = data;

  return (
    <div className="space-y-4">
      {/* Header Info */}
      <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-4">
        <h3 className="text-sm font-semibold text-blue-900 dark:text-blue-100 mb-2">
          Master List CMS 구조
        </h3>
        <div className="grid grid-cols-2 gap-3 text-sm">
          <div>
            <span className="text-blue-700 dark:text-blue-300">파일명:</span>
            <span className="ml-2 text-blue-900 dark:text-blue-100 font-medium">{fileName}</span>
          </div>
          <div>
            <span className="text-blue-700 dark:text-blue-300">크기:</span>
            <span className="ml-2 text-blue-900 dark:text-blue-100 font-medium">
              {fileSize ? formatBytes(fileSize) : 'N/A'}
            </span>
          </div>
        </div>
      </div>

      {/* SignerInfo Section */}
      <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
        <button
          onClick={() => toggleSection('signerInfo')}
          className="w-full flex items-center justify-between px-4 py-3 bg-purple-50 dark:bg-purple-900/20 hover:bg-purple-100 dark:hover:bg-purple-900/30 transition-colors"
        >
          <div className="flex items-center gap-2">
            {expandedSections.signerInfo ? (
              <ChevronDown className="w-5 h-5 text-purple-600 dark:text-purple-400" />
            ) : (
              <ChevronRight className="w-5 h-5 text-purple-600 dark:text-purple-400" />
            )}
            <Shield className="w-5 h-5 text-purple-600 dark:text-purple-400" />
            <span className="font-semibold text-purple-900 dark:text-purple-100">
              SignerInfo (MLSC)
            </span>
            <span className="ml-2 px-2 py-0.5 bg-purple-200 dark:bg-purple-800 text-purple-800 dark:text-purple-200 text-xs font-medium rounded">
              {structure.signerInfoCount}개
            </span>
          </div>
        </button>

        {expandedSections.signerInfo && structure.signerInfos.length > 0 && (
          <div className="p-4 space-y-3 bg-white dark:bg-gray-800">
            {structure.signerInfos.map((signer, idx) => (
              <div
                key={idx}
                className="p-3 bg-purple-50 dark:bg-purple-900/10 border border-purple-200 dark:border-purple-800 rounded-lg"
              >
                <div className="text-xs space-y-2">
                  <div>
                    <span className="font-medium text-purple-700 dark:text-purple-300">Subject:</span>
                    <p className="mt-1 text-purple-900 dark:text-purple-100 font-mono text-[11px] break-all">
                      {signer.subject}
                    </p>
                  </div>
                  <div>
                    <span className="font-medium text-purple-700 dark:text-purple-300">Issuer:</span>
                    <p className="mt-1 text-purple-900 dark:text-purple-100 font-mono text-[11px] break-all">
                      {signer.issuer}
                    </p>
                  </div>
                  <div className="flex items-center justify-between">
                    <div>
                      <span className="font-medium text-purple-700 dark:text-purple-300">Serial:</span>
                      <span className="ml-2 text-purple-900 dark:text-purple-100 font-mono text-[11px]">
                        {signer.serialNumber}
                      </span>
                    </div>
                    <span
                      className={cn(
                        'px-2 py-0.5 text-xs font-medium rounded',
                        signer.isSelfSigned
                          ? 'bg-green-200 dark:bg-green-800 text-green-800 dark:text-green-200'
                          : 'bg-cyan-200 dark:bg-cyan-800 text-cyan-800 dark:text-cyan-200'
                      )}
                    >
                      {signer.isSelfSigned ? 'Self-signed' : 'Cross-signed'}
                    </span>
                  </div>
                </div>
              </div>
            ))}
          </div>
        )}

        {expandedSections.signerInfo && structure.signerInfos.length === 0 && (
          <div className="p-4 text-center text-sm text-gray-500 dark:text-gray-400">
            SignerInfo가 없습니다.
          </div>
        )}
      </div>

      {/* pkiData Section */}
      <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
        <button
          onClick={() => toggleSection('pkiData')}
          className="w-full flex items-center justify-between px-4 py-3 bg-gray-50 dark:bg-gray-700/50 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
        >
          <div className="flex items-center gap-2">
            {expandedSections.pkiData ? (
              <ChevronDown className="w-5 h-5 text-gray-600 dark:text-gray-400" />
            ) : (
              <ChevronRight className="w-5 h-5 text-gray-600 dark:text-gray-400" />
            )}
            <FileText className="w-5 h-5 text-gray-600 dark:text-gray-400" />
            <span className="font-semibold text-gray-900 dark:text-white">
              pkiData (CSCA & Link Certificates)
            </span>
            <div className="flex items-center gap-2 ml-2">
              <span className="px-2 py-0.5 bg-green-200 dark:bg-green-800 text-green-800 dark:text-green-200 text-xs font-medium rounded">
                CSCA: {structure.pkiDataSelfSignedCount}
              </span>
              <span className="px-2 py-0.5 bg-cyan-200 dark:bg-cyan-800 text-cyan-800 dark:text-cyan-200 text-xs font-medium rounded">
                LC: {structure.pkiDataLinkCertCount}
              </span>
            </div>
          </div>
        </button>

        {expandedSections.pkiData && structure.pkiDataCertificates.length > 0 && (
          <div className="p-4 max-h-96 overflow-y-auto space-y-2 bg-white dark:bg-gray-800">
            {structure.pkiDataCertificates.map((cert, idx) => (
              <div
                key={idx}
                className={cn(
                  'p-2 border rounded text-xs',
                  cert.isSelfSigned
                    ? 'bg-green-50 dark:bg-green-900/10 border-green-200 dark:border-green-800'
                    : 'bg-cyan-50 dark:bg-cyan-900/10 border-cyan-200 dark:border-cyan-800'
                )}
              >
                <div className="flex items-center justify-between mb-2">
                  <div className="flex items-center gap-2">
                    {cert.isSelfSigned ? (
                      <Shield className="w-4 h-4 text-green-600 dark:text-green-400" />
                    ) : (
                      <Link2 className="w-4 h-4 text-cyan-600 dark:text-cyan-400" />
                    )}
                    <span className="font-medium text-gray-900 dark:text-white">
                      #{idx + 1} - {cert.type}
                    </span>
                  </div>
                  <span className="text-[10px] font-mono text-gray-500 dark:text-gray-400">
                    {cert.serialNumber.substring(0, 16)}...
                  </span>
                </div>
                <div className="space-y-1">
                  <div>
                    <span className="text-gray-600 dark:text-gray-400">Subject:</span>
                    <p className="mt-0.5 text-gray-900 dark:text-white font-mono text-[10px] break-all">
                      {cert.subject}
                    </p>
                  </div>
                  {!cert.isSelfSigned && (
                    <div>
                      <span className="text-gray-600 dark:text-gray-400">Issuer:</span>
                      <p className="mt-0.5 text-gray-900 dark:text-white font-mono text-[10px] break-all">
                        {cert.issuer}
                      </p>
                    </div>
                  )}
                </div>
              </div>
            ))}
          </div>
        )}

        {expandedSections.pkiData && structure.pkiDataCertificates.length === 0 && (
          <div className="p-4 text-center text-sm text-gray-500 dark:text-gray-400">
            pkiData 인증서가 없습니다.
          </div>
        )}
      </div>
    </div>
  );
}
