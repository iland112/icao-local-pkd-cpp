import { useState, useEffect } from 'react';
import { RefreshCw, AlertCircle, CheckCircle, Clock, Download, FileText, Database } from 'lucide-react';

interface IcaoVersion {
  id: number;
  collection_type: string;
  file_name: string;
  file_version: number;
  status: string;
  status_description: string;
  detected_at: string;
  downloaded_at?: string;
  imported_at?: string;
  notification_sent: boolean;
  notification_sent_at?: string;
  import_upload_id?: string;
  certificate_count?: number;
  error_message?: string;
}

interface ApiResponse {
  success: boolean;
  count?: number;
  limit?: number;
  versions: IcaoVersion[];
  error?: string;
  message?: string;
}

export default function IcaoStatus() {
  const [latestVersions, setLatestVersions] = useState<IcaoVersion[]>([]);
  const [versionHistory, setVersionHistory] = useState<IcaoVersion[]>([]);
  const [loading, setLoading] = useState(true);
  const [checking, setChecking] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [lastChecked, setLastChecked] = useState<Date | null>(null);

  useEffect(() => {
    fetchLatestVersions();
    fetchVersionHistory();
  }, []);

  const fetchLatestVersions = async () => {
    try {
      const response = await fetch('http://localhost:8080/api/icao/latest');
      const data: ApiResponse = await response.json();

      if (data.success) {
        setLatestVersions(data.versions);
        setLastChecked(new Date());
      } else {
        setError(data.message || 'Failed to fetch latest versions');
      }
    } catch (err) {
      setError('Network error: Unable to fetch latest versions');
      console.error('Fetch error:', err);
    } finally {
      setLoading(false);
    }
  };

  const fetchVersionHistory = async () => {
    try {
      const response = await fetch('http://localhost:8080/api/icao/history?limit=10');
      const data: ApiResponse = await response.json();

      if (data.success) {
        setVersionHistory(data.versions);
      }
    } catch (err) {
      console.error('Failed to fetch version history:', err);
    }
  };

  const handleCheckUpdates = async () => {
    setChecking(true);
    setError(null);

    try {
      const response = await fetch('http://localhost:8080/api/icao/check-updates', {
        method: 'POST',
      });

      if (response.ok) {
        // Wait a moment for async processing
        setTimeout(() => {
          fetchLatestVersions();
          fetchVersionHistory();
          setChecking(false);
        }, 2000);
      } else {
        setError('Failed to trigger version check');
        setChecking(false);
      }
    } catch (err) {
      setError('Network error: Unable to check for updates');
      console.error('Check updates error:', err);
      setChecking(false);
    }
  };

  const getStatusIcon = (status: string) => {
    switch (status) {
      case 'DETECTED':
        return <AlertCircle className="w-5 h-5 text-yellow-500" />;
      case 'NOTIFIED':
        return <Clock className="w-5 h-5 text-blue-500" />;
      case 'DOWNLOADED':
        return <Download className="w-5 h-5 text-indigo-500" />;
      case 'IMPORTED':
        return <CheckCircle className="w-5 h-5 text-green-500" />;
      case 'FAILED':
        return <AlertCircle className="w-5 h-5 text-red-500" />;
      default:
        return <Clock className="w-5 h-5 text-gray-500" />;
    }
  };

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'DETECTED':
        return 'bg-yellow-100 text-yellow-800 border-yellow-300';
      case 'NOTIFIED':
        return 'bg-blue-100 text-blue-800 border-blue-300';
      case 'DOWNLOADED':
        return 'bg-indigo-100 text-indigo-800 border-indigo-300';
      case 'IMPORTED':
        return 'bg-green-100 text-green-800 border-green-300';
      case 'FAILED':
        return 'bg-red-100 text-red-800 border-red-300';
      default:
        return 'bg-gray-100 text-gray-800 border-gray-300';
    }
  };

  const formatTimestamp = (timestamp: string) => {
    return new Date(timestamp).toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center min-h-screen">
        <div className="animate-spin rounded-full h-12 w-12 border-b-2 border-blue-600"></div>
      </div>
    );
  }

  return (
    <div className="min-h-screen bg-gray-50 p-6">
      <div className="max-w-7xl mx-auto">
        {/* Header */}
        <div className="mb-8">
          <div className="flex items-center justify-between">
            <div>
              <h1 className="text-3xl font-bold text-gray-900 mb-2">
                ICAO PKD Auto Sync Status
              </h1>
              <p className="text-gray-600">
                Monitor ICAO PKD version updates and download status
              </p>
            </div>
            <button
              onClick={handleCheckUpdates}
              disabled={checking}
              className="flex items-center gap-2 px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:bg-gray-400 disabled:cursor-not-allowed transition-colors"
            >
              <RefreshCw className={`w-5 h-5 ${checking ? 'animate-spin' : ''}`} />
              {checking ? 'Checking...' : 'Check for Updates'}
            </button>
          </div>
          {lastChecked && (
            <p className="text-sm text-gray-500 mt-2">
              Last checked: {formatTimestamp(lastChecked.toISOString())}
            </p>
          )}
        </div>

        {/* Error Alert */}
        {error && (
          <div className="mb-6 p-4 bg-red-50 border border-red-200 rounded-lg flex items-start gap-3">
            <AlertCircle className="w-5 h-5 text-red-600 flex-shrink-0 mt-0.5" />
            <div>
              <h3 className="font-semibold text-red-900">Error</h3>
              <p className="text-red-700">{error}</p>
            </div>
          </div>
        )}

        {/* Latest Versions Cards */}
        <div className="mb-8">
          <h2 className="text-xl font-semibold text-gray-900 mb-4">Latest Detected Versions</h2>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {latestVersions.length === 0 ? (
              <div className="col-span-2 p-8 bg-white rounded-lg border border-gray-200 text-center">
                <FileText className="w-12 h-12 text-gray-400 mx-auto mb-3" />
                <p className="text-gray-500">No versions detected yet</p>
                <p className="text-sm text-gray-400 mt-1">
                  Click "Check for Updates" to fetch latest versions from ICAO portal
                </p>
              </div>
            ) : (
              latestVersions.map((version) => (
                <div
                  key={version.id}
                  className="bg-white rounded-lg border border-gray-200 shadow-sm hover:shadow-md transition-shadow p-6"
                >
                  <div className="flex items-start justify-between mb-4">
                    <div>
                      <h3 className="text-lg font-semibold text-gray-900 mb-1">
                        {version.collection_type === 'DSC_CRL'
                          ? 'DSC/CRL Collection'
                          : 'CSCA Master List'}
                      </h3>
                      <p className="text-sm text-gray-500">{version.file_name}</p>
                    </div>
                    {getStatusIcon(version.status)}
                  </div>

                  <div className="space-y-3">
                    <div className="flex items-center justify-between">
                      <span className="text-sm text-gray-600">Version</span>
                      <span className="text-2xl font-bold text-blue-600">
                        {version.file_version.toString().padStart(6, '0')}
                      </span>
                    </div>

                    <div>
                      <span
                        className={`inline-flex items-center gap-2 px-3 py-1 rounded-full border text-sm font-medium ${getStatusColor(
                          version.status
                        )}`}
                      >
                        {version.status}
                      </span>
                    </div>

                    <div className="pt-3 border-t border-gray-200">
                      <p className="text-sm text-gray-600 mb-2">
                        {version.status_description}
                      </p>
                      <p className="text-xs text-gray-500">
                        Detected: {formatTimestamp(version.detected_at)}
                      </p>
                      {version.imported_at && (
                        <p className="text-xs text-gray-500">
                          Imported: {formatTimestamp(version.imported_at)}
                        </p>
                      )}
                      {version.certificate_count && (
                        <div className="mt-2 flex items-center gap-2 text-sm text-gray-700">
                          <Database className="w-4 h-4" />
                          <span>{version.certificate_count} certificates</span>
                        </div>
                      )}
                    </div>

                    {version.status === 'DETECTED' && (
                      <div className="pt-3 border-t border-gray-200">
                        <a
                          href="https://pkddownloadsg.icao.int/"
                          target="_blank"
                          rel="noopener noreferrer"
                          className="inline-flex items-center gap-2 text-sm text-blue-600 hover:text-blue-700 font-medium"
                        >
                          <Download className="w-4 h-4" />
                          Download from ICAO Portal
                        </a>
                      </div>
                    )}
                  </div>
                </div>
              ))
            )}
          </div>
        </div>

        {/* Version History Table */}
        <div>
          <h2 className="text-xl font-semibold text-gray-900 mb-4">Version Detection History</h2>
          <div className="bg-white rounded-lg border border-gray-200 shadow-sm overflow-hidden">
            <div className="overflow-x-auto">
              <table className="min-w-full divide-y divide-gray-200">
                <thead className="bg-gray-50">
                  <tr>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      Collection
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      File Name
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      Version
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      Status
                    </th>
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                      Detected At
                    </th>
                  </tr>
                </thead>
                <tbody className="bg-white divide-y divide-gray-200">
                  {versionHistory.length === 0 ? (
                    <tr>
                      <td colSpan={5} className="px-6 py-8 text-center text-gray-500">
                        No version history available
                      </td>
                    </tr>
                  ) : (
                    versionHistory.map((version) => (
                      <tr key={version.id} className="hover:bg-gray-50">
                        <td className="px-6 py-4 whitespace-nowrap">
                          <span className="text-sm font-medium text-gray-900">
                            {version.collection_type === 'DSC_CRL' ? 'DSC/CRL' : 'Master List'}
                          </span>
                        </td>
                        <td className="px-6 py-4 whitespace-nowrap">
                          <span className="text-sm text-gray-700 font-mono">
                            {version.file_name}
                          </span>
                        </td>
                        <td className="px-6 py-4 whitespace-nowrap">
                          <span className="text-sm font-semibold text-blue-600">
                            {version.file_version.toString().padStart(6, '0')}
                          </span>
                        </td>
                        <td className="px-6 py-4 whitespace-nowrap">
                          <span
                            className={`inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full border text-xs font-medium ${getStatusColor(
                              version.status
                            )}`}
                          >
                            {version.status}
                          </span>
                        </td>
                        <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                          {formatTimestamp(version.detected_at)}
                        </td>
                      </tr>
                    ))
                  )}
                </tbody>
              </table>
            </div>
          </div>
        </div>

        {/* Info Section */}
        <div className="mt-8 p-6 bg-blue-50 border border-blue-200 rounded-lg">
          <h3 className="text-lg font-semibold text-blue-900 mb-2">About ICAO Auto Sync</h3>
          <div className="text-sm text-blue-800 space-y-2">
            <p>
              This system automatically monitors the ICAO PKD portal for new certificate versions.
              When a new version is detected, you will be notified to download and import it manually.
            </p>
            <div className="mt-4">
              <h4 className="font-semibold mb-1">Status Lifecycle:</h4>
              <ul className="list-disc list-inside space-y-1 ml-2">
                <li><strong>DETECTED</strong>: New version found on ICAO portal</li>
                <li><strong>NOTIFIED</strong>: Administrator has been notified</li>
                <li><strong>DOWNLOADED</strong>: File downloaded from portal</li>
                <li><strong>IMPORTED</strong>: Successfully imported to Local PKD</li>
                <li><strong>FAILED</strong>: Import failed (check error message)</li>
              </ul>
            </div>
            <div className="mt-4 pt-4 border-t border-blue-300">
              <p className="text-xs">
                <strong>Note</strong>: This is Tier 1 implementation (Manual Download).
                Automatic downloading is not enabled to comply with ICAO Terms of Service.
              </p>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
