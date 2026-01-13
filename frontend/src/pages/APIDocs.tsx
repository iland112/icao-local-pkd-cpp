import { useParams, useNavigate } from 'react-router-dom';
import { useEffect } from 'react';

type APIService = 'pkd-management' | 'pa-service' | 'sync-service';

const serviceInfo: Record<APIService, { name: string; version: string; description: string }> = {
  'pkd-management': {
    name: 'PKD Management API',
    version: 'v1.5.10',
    description: 'Certificate upload, validation, and LDAP synchronization'
  },
  'pa-service': {
    name: 'PA Service API',
    version: 'v1.2.0',
    description: 'ICAO 9303 Passive Authentication verification'
  },
  'sync-service': {
    name: 'Sync Service API',
    version: 'v1.2.0',
    description: 'DB-LDAP synchronization monitoring'
  }
};

export default function APIDocs() {
  const { service } = useParams<{ service: APIService }>();
  const navigate = useNavigate();

  // Default to pkd-management if no service specified
  useEffect(() => {
    if (!service) {
      navigate('/api-docs/pkd-management', { replace: true });
    }
  }, [service, navigate]);

  if (!service || !(service in serviceInfo)) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center">
          <h2 className="text-2xl font-bold text-gray-900 dark:text-white mb-4">Invalid API Service</h2>
          <p className="text-gray-600 dark:text-gray-400 mb-4">Please select a valid API service.</p>
          <button
            onClick={() => navigate('/api-docs/pkd-management')}
            className="px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700"
          >
            Go to PKD Management API
          </button>
        </div>
      </div>
    );
  }

  const info = serviceInfo[service];
  const swaggerUrl = `http://${window.location.hostname}:8888?url=http://${window.location.hostname}:8888/api/docs/${service}.yaml`;

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="bg-white dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700 px-6 py-4">
        <div className="flex items-center justify-between">
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{info.name}</h1>
            <p className="text-sm text-gray-600 dark:text-gray-400 mt-1">
              {info.description} • <span className="font-semibold">{info.version}</span>
            </p>
          </div>
          <div className="flex gap-2">
            <a
              href={swaggerUrl}
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-2 px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition-colors"
            >
              <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M10 6H6a2 2 0 00-2 2v10a2 2 0 002 2h10a2 2 0 002-2v-4M14 4h6m0 0v6m0-6L10 14" />
              </svg>
              Open in New Tab
            </a>
            <a
              href={`http://${window.location.hostname}:8888/api/docs/${service}.yaml`}
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-2 px-4 py-2 border border-gray-300 dark:border-gray-600 text-gray-700 dark:text-gray-300 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
            >
              <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
              </svg>
              Download YAML
            </a>
          </div>
        </div>
      </div>

      {/* Swagger UI iframe */}
      <div className="flex-1 bg-white dark:bg-gray-900">
        <iframe
          src={swaggerUrl}
          className="w-full h-full border-0"
          title={`${info.name} Documentation`}
          sandbox="allow-same-origin allow-scripts allow-forms"
        />
      </div>
    </div>
  );
}
