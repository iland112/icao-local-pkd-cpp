import { useState, useEffect } from 'react';
import { Activity, Cpu, HardDrive, MemoryStick, Network, Server, AlertCircle, CheckCircle2, XCircle, Clock, RefreshCw, Loader2, Gauge, Database, Globe } from 'lucide-react';
import { monitoringServiceApi, type SystemMetrics, type ServiceHealth, type LoadSnapshot, type HistoryPoint } from '@/services/monitoringApi';
import { healthApi } from '@/services/pkdApi';
import { cn } from '@/utils/cn';
import ActiveConnectionsCard from '@/components/monitoring/ActiveConnectionsCard';
import RequestRateChart from '@/components/monitoring/RequestRateChart';
import LatencyTrendChart from '@/components/monitoring/LatencyTrendChart';
import ConnectionPoolChart from '@/components/monitoring/ConnectionPoolChart';

interface InfraHealth {
  name: string;
  status: 'UP' | 'DOWN';
  responseTimeMs: number;
  version?: string;
  type?: string;
  errorMessage?: string;
}

const SERVICE_DEFS = [
  { name: 'PKD Management', url: '/api/health', port: 8081, desc: '업로드, 인증서 관리, 인증' },
  { name: 'PA Service', url: '/api/pa/health', port: 8082, desc: '여권 Passive Authentication' },
  { name: 'PKD Relay', url: '/api/sync/health', port: 8083, desc: 'DB-LDAP 동기화' },
  { name: 'Monitoring', url: '/api/monitoring/health', port: 8084, desc: '시스템 메트릭 수집' },
  { name: 'AI Analysis', url: '/api/ai/health', port: 8085, desc: 'ML 이상 탐지 분석' },
];

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

export default function MonitoringDashboard() {
  const [metrics, setMetrics] = useState<SystemMetrics | null>(null);
  const [services, setServices] = useState<ServiceHealth[]>([]);
  const [infraHealth, setInfraHealth] = useState<InfraHealth[]>([]);
  const [loadSnapshot, setLoadSnapshot] = useState<LoadSnapshot | null>(null);
  const [loadHistory, setLoadHistory] = useState<HistoryPoint[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [lastUpdate, setLastUpdate] = useState<Date>(new Date());

  const checkServiceHealth = async (name: string, url: string): Promise<ServiceHealth> => {
    const startTime = Date.now();
    try {
      await fetch(url, { method: 'GET', signal: AbortSignal.timeout(5000) });
      return {
        serviceName: name,
        status: 'UP',
        responseTimeMs: Date.now() - startTime,
        checkedAt: new Date().toISOString(),
      };
    } catch (err) {
      return {
        serviceName: name,
        status: 'DOWN',
        responseTimeMs: Date.now() - startTime,
        errorMessage: err instanceof Error ? err.message : 'Connection failed',
        checkedAt: new Date().toISOString(),
      };
    }
  };

  const fetchData = async () => {
    try {
      setError(null);

      // Fetch system metrics
      const metricsResponse = await monitoringServiceApi.getSystemOverview();
      setMetrics(metricsResponse.data as any);

      // Health checks — all 5 services
      const serviceResults = await Promise.all(
        SERVICE_DEFS.map(s => checkServiceHealth(s.name, s.url))
      );
      setServices(serviceResults);

      // Infrastructure health (Database + LDAP)
      const infra: InfraHealth[] = [];

      try {
        const startDb = Date.now();
        const dbRes = await healthApi.checkDatabase();
        infra.push({
          name: dbRes.data.type || 'Database',
          status: dbRes.data.status === 'UP' ? 'UP' : 'DOWN',
          responseTimeMs: dbRes.data.responseTimeMs ?? (Date.now() - startDb),
          version: dbRes.data.version,
          type: dbRes.data.type,
        });
      } catch {
        infra.push({ name: 'Database', status: 'DOWN', responseTimeMs: 0, errorMessage: 'Connection failed' });
      }

      try {
        const startLdap = Date.now();
        const ldapRes = await healthApi.checkLdap();
        infra.push({
          name: 'LDAP',
          status: ldapRes.data.status === 'UP' ? 'UP' : 'DOWN',
          responseTimeMs: (ldapRes.data as any).responseTimeMs ?? ldapRes.data.responseTime ?? (Date.now() - startLdap),
        });
      } catch {
        infra.push({ name: 'LDAP', status: 'DOWN', responseTimeMs: 0, errorMessage: 'Connection failed' });
      }

      setInfraHealth(infra);

      // Fetch load metrics (non-blocking — don't fail the whole page)
      try {
        const [snapRes, histRes] = await Promise.all([
          monitoringServiceApi.getLoadSnapshot(),
          monitoringServiceApi.getLoadHistory(30),
        ]);
        setLoadSnapshot(snapRes.data);
        setLoadHistory(histRes.data?.data ?? []);
      } catch {
        // Load metrics are supplementary — don't show error
      }

      setLastUpdate(new Date());
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch monitoring data:', err);
      setError('모니터링 데이터를 불러오는데 실패했습니다.');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchData();
    const interval = setInterval(fetchData, 10000);
    return () => clearInterval(interval);
  }, []);

  const upCount = services.filter(s => s.status === 'UP').length;
  const totalCount = services.length;
  const infraUp = infraHealth.filter(i => i.status === 'UP').length;
  const allHealthy = upCount === totalCount && infraUp === infraHealth.length;

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-cyan-600 shadow-lg">
            <Gauge className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">시스템 모니터링</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              실시간 시스템 리소스 및 서비스 상태를 모니터링합니다.
            </p>
          </div>
          <div className="flex gap-2 items-center">
            <div className="flex items-center gap-2 text-sm text-gray-600 dark:text-gray-400 mr-2">
              <Clock className="w-4 h-4" />
              <span>{lastUpdate.toLocaleTimeString('ko-KR')}</span>
            </div>
            <button
              onClick={fetchData}
              disabled={loading}
              className="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-sm font-medium transition-all duration-200 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700"
            >
              <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
            </button>
          </div>
        </div>
      </div>

      {loading ? (
        <div className="flex items-center justify-center py-20">
          <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
        </div>
      ) : error || !metrics ? (
        <div className="flex items-center justify-center py-20">
          <div className="text-center">
            <AlertCircle className="w-16 h-16 text-red-500 mx-auto mb-4" />
            <p className="text-xl text-gray-800 dark:text-white mb-2">모니터링 데이터 로드 실패</p>
            <p className="text-gray-600 dark:text-gray-400 mb-4">{error || '알 수 없는 오류가 발생했습니다.'}</p>
            <button
              onClick={fetchData}
              className="px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition-colors"
            >
              다시 시도
            </button>
          </div>
        </div>
      ) : (
        <>
          {/* Overall Status Summary */}
          <div className={cn(
            'flex items-center gap-4 px-4 py-3 rounded-xl mb-6 border',
            allHealthy
              ? 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800'
              : 'bg-amber-50 dark:bg-amber-900/20 border-amber-200 dark:border-amber-800'
          )}>
            {allHealthy ? (
              <CheckCircle2 className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
            ) : (
              <AlertCircle className="w-5 h-5 text-amber-600 dark:text-amber-400 flex-shrink-0" />
            )}
            <div className="flex flex-wrap gap-x-6 gap-y-1 text-sm">
              <span className={cn(
                'font-medium',
                upCount === totalCount ? 'text-green-700 dark:text-green-300' : 'text-amber-700 dark:text-amber-300'
              )}>
                서비스 {upCount}/{totalCount} 정상
              </span>
              {infraHealth.map(ih => (
                <span key={ih.name} className={cn(
                  'font-medium',
                  ih.status === 'UP' ? 'text-green-700 dark:text-green-300' : 'text-red-700 dark:text-red-300'
                )}>
                  {ih.name} {ih.status === 'UP' ? '정상' : '중단'}
                </span>
              ))}
            </div>
          </div>

          {/* System Metrics Cards */}
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4 mb-6">
            <MetricCard
              title="CPU"
              icon={<Cpu className="w-6 h-6 text-blue-500" />}
              value={metrics.cpu.usagePercent.toFixed(1)}
              unit="%"
              details={[
                { label: 'Load 1m', value: metrics.cpu.load1min.toFixed(2) },
                { label: 'Load 5m', value: metrics.cpu.load5min.toFixed(2) },
                { label: 'Load 15m', value: metrics.cpu.load15min.toFixed(2) },
              ]}
              percentage={metrics.cpu.usagePercent}
            />
            <MetricCard
              title="메모리"
              icon={<MemoryStick className="w-6 h-6 text-green-500" />}
              value={metrics.memory.usagePercent.toFixed(1)}
              unit="%"
              details={[
                { label: '전체', value: `${metrics.memory.totalMb.toLocaleString()} MB` },
                { label: '사용', value: `${metrics.memory.usedMb.toLocaleString()} MB` },
                { label: '여유', value: `${metrics.memory.freeMb.toLocaleString()} MB` },
              ]}
              percentage={metrics.memory.usagePercent}
            />
            <MetricCard
              title="디스크"
              icon={<HardDrive className="w-6 h-6 text-purple-500" />}
              value={metrics.disk.usagePercent.toFixed(1)}
              unit="%"
              details={[
                { label: '전체', value: `${metrics.disk.totalGb.toFixed(0)} GB` },
                { label: '사용', value: `${metrics.disk.usedGb.toFixed(0)} GB` },
                { label: '여유', value: `${metrics.disk.freeGb.toFixed(0)} GB` },
              ]}
              percentage={metrics.disk.usagePercent}
            />
            <MetricCard
              title="네트워크 I/O"
              icon={<Network className="w-6 h-6 text-orange-500" />}
              value={formatBytes(metrics.network.bytesSent + metrics.network.bytesRecv)}
              unit=""
              details={[
                { label: '송신', value: formatBytes(metrics.network.bytesSent) },
                { label: '수신', value: formatBytes(metrics.network.bytesRecv) },
                { label: '패킷', value: `${((metrics.network.packetsSent + metrics.network.packetsRecv) / 1000).toFixed(0)}K` },
              ]}
              percentage={null}
            />
          </div>

          {/* Load Monitoring Section */}
          {loadSnapshot && (
            <>
              {/* Row: Active Connections + Connection Pool */}
              <div className="grid grid-cols-1 lg:grid-cols-3 gap-4 mb-6">
                <ActiveConnectionsCard
                  nginx={loadSnapshot.nginx}
                  requestsPerSecond={loadSnapshot.nginx.requestsPerSecond}
                />
                <div className="lg:col-span-2">
                  <ConnectionPoolChart services={loadSnapshot.services} />
                </div>
              </div>

              {/* Row: Request Rate Trend (full width) */}
              {loadHistory.length > 0 && (
                <div className="mb-6">
                  <RequestRateChart data={loadHistory} />
                </div>
              )}

              {/* Row: Latency Trend */}
              {loadHistory.length > 0 && (
                <div className="mb-6">
                  <LatencyTrendChart data={loadHistory} />
                </div>
              )}
            </>
          )}

          {/* Infrastructure Health */}
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mb-6">
            {infraHealth.map(ih => (
              <div key={ih.name} className={cn(
                'border rounded-xl p-4 transition-all duration-200',
                ih.status === 'UP'
                  ? 'bg-white dark:bg-gray-800 border-gray-200 dark:border-gray-700'
                  : 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800'
              )}>
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-3">
                    {ih.name === 'LDAP' ? (
                      <Globe className="w-5 h-5 text-blue-500" />
                    ) : (
                      <Database className="w-5 h-5 text-indigo-500" />
                    )}
                    <div>
                      <h4 className="font-semibold text-gray-800 dark:text-white">{ih.name}</h4>
                      {ih.version && (
                        <p className="text-xs text-gray-500 dark:text-gray-400 mt-0.5">{ih.version}</p>
                      )}
                    </div>
                  </div>
                  <div className="flex items-center gap-3">
                    <span className="text-sm text-gray-500 dark:text-gray-400">{ih.responseTimeMs}ms</span>
                    <span className={cn(
                      'px-2 py-1 rounded text-xs font-medium',
                      ih.status === 'UP'
                        ? 'bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300'
                        : 'bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300'
                    )}>
                      {ih.status}
                    </span>
                  </div>
                </div>
                {ih.errorMessage && (
                  <div className="mt-2 p-2 bg-white dark:bg-gray-900/40 rounded text-xs text-red-600 dark:text-red-400 border border-red-200 dark:border-red-800">
                    {ih.errorMessage}
                  </div>
                )}
              </div>
            ))}
          </div>

          {/* Service Health Status */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-6">
            <div className="flex items-center gap-2 mb-4">
              <Server className="w-5 h-5 text-gray-700 dark:text-gray-300" />
              <h2 className="text-lg font-semibold text-gray-800 dark:text-white">서비스 상태</h2>
              <span className="text-sm text-gray-500 dark:text-gray-400 ml-auto">{upCount}/{totalCount} 정상</span>
            </div>

            {services.length === 0 ? (
              <div className="text-center py-8 text-gray-500 dark:text-gray-400">
                <Activity className="w-12 h-12 mx-auto mb-2 text-gray-400" />
                <p>서비스 상태를 확인하는 중...</p>
              </div>
            ) : (
              <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4">
                {services.map((service) => {
                  const def = SERVICE_DEFS.find(d => d.name === service.serviceName);
                  return (
                    <ServiceCard key={service.serviceName} service={service} port={def?.port} description={def?.desc} />
                  );
                })}
              </div>
            )}
          </div>

          {/* Timestamp */}
          <div className="text-center text-sm text-gray-500 dark:text-gray-400 mt-6">
            메트릭 수집 시간: {new Date(metrics.timestamp).toLocaleString('ko-KR')}
          </div>
        </>
      )}
    </div>
  );
}

// Metric Card Component
interface MetricCardProps {
  title: string;
  icon: React.ReactNode;
  value: string;
  unit: string;
  details: { label: string; value: string }[];
  percentage: number | null;
}

function MetricCard({ title, icon, value, unit, details, percentage }: MetricCardProps) {
  const getColorClass = (pct: number | null) => {
    if (pct === null) return 'bg-blue-500';
    if (pct < 60) return 'bg-green-500';
    if (pct < 80) return 'bg-yellow-500';
    return 'bg-red-500';
  };

  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-5 hover:shadow-xl transition-all duration-200">
      <div className="flex items-center justify-between mb-3">
        <div className="flex items-center gap-2">
          {icon}
          <h3 className="font-semibold text-gray-800 dark:text-white">{title}</h3>
        </div>
      </div>

      <div className="mb-3">
        <div className="flex items-baseline gap-1">
          <span className="text-3xl font-bold text-gray-900 dark:text-white">{value}</span>
          {unit && <span className="text-sm text-gray-600 dark:text-gray-400">{unit}</span>}
        </div>
      </div>

      {percentage !== null && (
        <div className="mb-3">
          <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-2">
            <div
              className={`h-2 rounded-full transition-all ${getColorClass(percentage)}`}
              style={{ width: `${Math.min(percentage, 100)}%` }}
            />
          </div>
        </div>
      )}

      <div className="space-y-1 text-sm">
        {details.map((detail, idx) => (
          <div key={idx} className="flex justify-between text-gray-600 dark:text-gray-400">
            <span>{detail.label}:</span>
            <span className="font-medium text-gray-800 dark:text-gray-200">{detail.value}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

// Service Card Component
interface ServiceCardProps {
  service: ServiceHealth;
  port?: number;
  description?: string;
}

function ServiceCard({ service, port, description }: ServiceCardProps) {
  const getStatusIcon = (status: string) => {
    switch (status) {
      case 'UP':
        return <CheckCircle2 className="w-5 h-5 text-green-500" />;
      case 'DEGRADED':
        return <AlertCircle className="w-5 h-5 text-yellow-500" />;
      case 'DOWN':
        return <XCircle className="w-5 h-5 text-red-500" />;
      default:
        return <Activity className="w-5 h-5 text-gray-400" />;
    }
  };

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'UP':
        return 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800';
      case 'DEGRADED':
        return 'bg-yellow-50 dark:bg-yellow-900/20 border-yellow-200 dark:border-yellow-800';
      case 'DOWN':
        return 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800';
      default:
        return 'bg-gray-50 dark:bg-gray-800 border-gray-200 dark:border-gray-700';
    }
  };

  const getStatusBadgeColor = (status: string) => {
    switch (status) {
      case 'UP':
        return 'bg-green-100 dark:bg-green-900/40 text-green-800 dark:text-green-300';
      case 'DEGRADED':
        return 'bg-yellow-100 dark:bg-yellow-900/40 text-yellow-800 dark:text-yellow-300';
      case 'DOWN':
        return 'bg-red-100 dark:bg-red-900/40 text-red-800 dark:text-red-300';
      default:
        return 'bg-gray-100 dark:bg-gray-700 text-gray-800 dark:text-gray-300';
    }
  };

  return (
    <div className={`border rounded-xl p-4 transition-all duration-200 ${getStatusColor(service.status)}`}>
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-2">
          {getStatusIcon(service.status)}
          <div>
            <h4 className="font-semibold text-gray-800 dark:text-white">{service.serviceName}</h4>
          </div>
        </div>
        <span className={`px-2 py-1 rounded text-xs font-medium ${getStatusBadgeColor(service.status)}`}>
          {service.status}
        </span>
      </div>

      {(description || port) && (
        <p className="text-xs text-gray-500 dark:text-gray-400 mb-2">
          {description}{port ? ` (:${port})` : ''}
        </p>
      )}

      <div className="space-y-1 text-sm text-gray-600 dark:text-gray-400">
        <div className="flex justify-between">
          <span>응답 시간:</span>
          <span className="font-medium text-gray-800 dark:text-gray-200">{service.responseTimeMs}ms</span>
        </div>
        <div className="flex justify-between">
          <span>마지막 체크:</span>
          <span className="font-medium text-gray-800 dark:text-gray-200">{new Date(service.checkedAt).toLocaleTimeString('ko-KR')}</span>
        </div>
        {service.errorMessage && (
          <div className="mt-2 p-2 bg-white dark:bg-gray-900/40 rounded text-xs text-red-600 dark:text-red-400 border border-red-200 dark:border-red-800">
            {service.errorMessage}
          </div>
        )}
      </div>
    </div>
  );
}
