import { useTranslation } from 'react-i18next';
import { useState, useEffect, useRef } from 'react';
import { DEFAULT_PAGE_SIZE } from '@/config/pagination';
import { useSortableTable } from '@/hooks/useSortableTable';
import { SortableHeader } from '@/components/common/SortableHeader';
import {
  AlertCircle,
  AlertTriangle,
  CheckCircle,
  ChevronLeft,
  ChevronRight,
  Download,
  Filter,
  Loader2,
  RefreshCw,
  ShieldCheck,
  ShieldAlert,
  ShieldX,
  XCircle,
} from 'lucide-react';
import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  Cell,
} from 'recharts';
import { certificateApi } from '@/services/pkdApi';
import { exportQualityReportToCsv, type QualityCertItem } from '@/utils/csvExport';
import { getFlagSvgPath } from '@/utils/countryCode';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';

countries.registerLocale(ko);

const getCountryName = (code: string): string => {
  return countries.getName(code, 'ko') || code;
};

const CATEGORY_COLORS: Record<string, string> = {
  algorithm: '#ef4444',
  keySize: '#f97316',
  keyUsage: '#eab308',
  extensions: '#8b5cf6',
  validityPeriod: '#06b6d4',
};

interface QualitySummary {
  total: number;
  compliantCount: number;
  nonCompliantCount: number;
  warningCount: number;
}

interface CategoryItem {
  category: string;
  failCount: number;
}

interface CountryItem {
  countryCode: string;
  total: number;
  compliant: number;
  nonCompliant: number;
  warning: number;
}

interface CertTypeItem {
  certType: string;
  total: number;
  compliant: number;
  nonCompliant: number;
  warning: number;
}

interface ViolationItem {
  violation: string;
  count: number;
}

interface QualityReportData {
  summary: QualitySummary;
  byCategory: CategoryItem[];
  byCountry: CountryItem[];
  byCertType: CertTypeItem[];
  violations: ViolationItem[];
  certificates: {
    total: number;
    page: number;
    size: number;
    items: QualityCertItem[];
  };
}

// Translate backend violation messages using certificate i18n keys
function translateViolation(msg: string, t: (key: string, opts?: Record<string, string>) => string): string {
  const mappings: [RegExp, string, ((m: RegExpMatchArray) => Record<string, string>)?][] = [
    [/^Missing Key Usage extension$/i, 'certificate:violation.missingKeyUsageExtension'],
    [/^(\w+) missing required Basic Constraints extension$/i, 'certificate:violation.missingBasicConstraints', (m) => ({ type: m[1] })],
    [/^CSCA must have CA=TRUE$/i, 'certificate:violation.cscaMustCaTrue'],
    [/^DSC must have CA=FALSE$/i, 'certificate:violation.dscMustCaFalse'],
    [/^MLSC must have CA=TRUE$/i, 'certificate:violation.mlscMustCaTrue'],
    [/^MLSC must be self-signed$/i, 'certificate:violation.mlscMustSelfSigned'],
    [/^Missing required Key Usage: (.+)$/i, 'certificate:violation.missingRequiredKeyUsage', (m) => ({ value: m[1] })],
    // SHA-1 deprecated (new format)
    [/^SHA-1 is deprecated.*:\s*(.+)$/i, 'certificate:violation.sha1Deprecated', (m) => ({ value: m[1] })],
    // SHA-224 BSI TR-03110 (new format)
    [/^SHA-224 supported via BSI TR-03110.*:\s*(.+)$/i, 'certificate:violation.sha224BsiSupported', (m) => ({ value: m[1] })],
    // Hash algorithm not in Doc 9303 (updated format)
    [/^Signature hash algorithm not in Doc 9303.*:\s*(.+)$/i, 'certificate:violation.hashAlgorithmNotApproved', (m) => ({ value: m[1] })],
    // Legacy format fallback (old "not ICAO-approved" messages from existing DB data)
    [/^Signature hash algorithm not ICAO-approved.*:\s*(.+)$/i, 'certificate:violation.hashAlgorithmNotApproved', (m) => ({ value: m[1] })],
    // Public key algorithm
    [/^Public key algorithm not in Doc 9303.*:\s*(.+)$/i, 'certificate:violation.pubKeyAlgorithmNotApproved', (m) => ({ value: m[1] })],
    [/^Public key algorithm not ICAO-approved.*:\s*(.+)$/i, 'certificate:violation.pubKeyAlgorithmNotApproved', (m) => ({ value: m[1] })],
    [/^RSA key size below minimum.*:\s*(.+)$/i, 'certificate:violation.rsaKeySizeBelowMin', (m) => ({ value: m[1] })],
    [/^RSA key size exceeds.*:\s*(.+)$/i, 'certificate:violation.rsaKeySizeExceedsMax', (m) => ({ value: m[1] })],
    // Brainpool BSI TR-03110 (new format)
    [/^Brainpool curve supported via BSI TR-03110.*:\s*(.+)$/i, 'certificate:violation.brainpoolBsiSupported', (m) => ({ value: m[1] })],
    // ECDSA curve not in Doc 9303/BSI (updated format)
    [/^ECDSA curve not in Doc 9303.*:\s*(.+)$/i, 'certificate:violation.ecdsaCurveNotApproved', (m) => ({ value: m[1] })],
    // Legacy format fallback
    [/^ECDSA curve not ICAO-approved.*:\s*(.+)$/i, 'certificate:violation.ecdsaCurveNotApproved', (m) => ({ value: m[1] })],
    [/^ECDSA key size below minimum.*:\s*(.+)$/i, 'certificate:violation.ecdsaKeySizeBelowMin', (m) => ({ value: m[1] })],
    [/^CSCA validity period exceeds.*: (.+)$/i, 'certificate:violation.cscaValidityExceeds', (m) => ({ value: m[1] })],
    [/^DSC validity period exceeds.*: (.+)$/i, 'certificate:violation.dscValidityExceeds', (m) => ({ value: m[1] })],
    [/^Subject DN missing required Country.*$/i, 'certificate:violation.subjectDnMissingCountry'],
    [/^Certificate has no Subject DN$/i, 'certificate:violation.noSubjectDn'],
  ];

  for (const [pattern, key, paramsFn] of mappings) {
    const m = msg.match(pattern);
    if (m) {
      const params = paramsFn ? paramsFn(m) : undefined;
      return t(key, params);
    }
  }
  return msg;
}

export default function CertificateQualityReport() {
  const { t } = useTranslation(['report', 'common', 'certificate', 'nav']);
  const [data, setData] = useState<QualityReportData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [countryFilter, setCountryFilter] = useState('');
  const [certTypeFilter, setCertTypeFilter] = useState('');
  const [categoryFilter, setCategoryFilter] = useState('');
  const [page, setPage] = useState(1);
  const [refreshKey, setRefreshKey] = useState(0);
  const pageSize = DEFAULT_PAGE_SIZE;
  const abortControllerRef = useRef<AbortController | null>(null);

  const { sortedData: sortedCerts, sortConfig, requestSort } = useSortableTable(
    data?.certificates?.items ?? [],
    { key: 'complianceLevel', direction: 'asc' }
  );

  useEffect(() => {
    abortControllerRef.current?.abort();
    const controller = new AbortController();
    abortControllerRef.current = controller;

    const fetchReport = async () => {
      setLoading(true);
      setError(null);
      try {
        const params: Record<string, string | number> = { page, size: pageSize };
        if (countryFilter) params.country = countryFilter;
        if (certTypeFilter) params.certType = certTypeFilter;
        if (categoryFilter) params.category = categoryFilter;

        const response = await certificateApi.getQualityReport(params);
        if (!controller.signal.aborted) {
          setData(response.data as QualityReportData);
        }
      } catch (err) {
        if (!controller.signal.aborted) {
          setError(err instanceof Error ? err.message : t('common:label.dataLoadFailed'));
        }
      } finally {
        if (!controller.signal.aborted) {
          setLoading(false);
        }
      }
    };

    fetchReport();
    return () => controller.abort();
  }, [page, countryFilter, certTypeFilter, categoryFilter, refreshKey, pageSize, t]);

  const totalPages = data ? Math.ceil(data.certificates.total / pageSize) : 0;
  const complianceRate = data?.summary?.total
    ? ((data.summary.compliantCount / data.summary.total) * 100).toFixed(1)
    : '0';

  // Unique countries for filter
  const countryOptions = data?.byCountry?.map(c => c.countryCode) ?? [];
  const certTypeOptions = data?.byCertType?.map(c => c.certType) ?? [];

  if (loading && !data) {
    return (
      <div className="flex items-center justify-center h-96">
        <Loader2 className="w-8 h-8 text-blue-500 animate-spin" />
      </div>
    );
  }

  if (error && !data) {
    return (
      <div className="flex flex-col items-center justify-center h-96 text-gray-500">
        <AlertCircle className="w-12 h-12 mb-3 text-red-400" />
        <p className="text-sm">{error}</p>
        <button onClick={() => setRefreshKey(k => k + 1)} className="mt-3 text-sm text-blue-600 hover:underline">
          {t('common:label.retry')}
        </button>
      </div>
    );
  }

  if (!data || !data.summary || data.summary.total === 0) {
    return (
      <div className="flex flex-col items-center justify-center h-96 text-gray-500">
        <ShieldCheck className="w-12 h-12 mb-3 text-gray-300" />
        <p className="font-medium">{t('report:quality.noData')}</p>
        <p className="text-sm mt-1">{t('report:quality.noDataHint')}</p>
      </div>
    );
  }

  const { summary } = data;

  // Compliance status bar segments
  const statusSegments = [
    { label: t('report:quality.compliant'), count: summary.compliantCount, color: 'bg-emerald-500' },
    { label: t('report:quality.nonCompliant'), count: summary.nonCompliantCount, color: 'bg-red-500' },
    { label: t('report:quality.warning'), count: summary.warningCount, color: 'bg-amber-500' },
  ].filter(s => s.count > 0);

  // Cert type chart data for pie
  const certTypePieData = (data.byCertType ?? []).map(item => ({
    name: item.certType,
    compliant: item.compliant,
    nonCompliant: item.nonCompliant,
    warning: item.warning,
    total: item.total,
  }));

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-3">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 flex items-center justify-center shadow-lg">
            <ShieldCheck className="w-5 h-5 text-white" />
          </div>
          <div>
            <h1 className="text-xl font-bold text-gray-900 dark:text-white">{t('report:quality.title')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">{t('report:quality.subtitle')}</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <button
            onClick={() => {
              if (data?.certificates?.items) {
                exportQualityReportToCsv(data.certificates.items);
              }
            }}
            className="flex items-center gap-1.5 px-3 py-1.5 text-sm bg-emerald-600 text-white rounded-lg hover:bg-emerald-700 transition-colors"
          >
            <Download className="w-4 h-4" />
            {t('report:quality.exportCsv')}
          </button>
          <button
            onClick={() => { setPage(1); setRefreshKey(k => k + 1); }}
            className="flex items-center gap-1.5 px-3 py-1.5 text-sm bg-white dark:bg-gray-800 border border-gray-300 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
          >
            <RefreshCw className={`w-4 h-4 ${loading ? 'animate-spin' : ''}`} />
          </button>
        </div>
      </div>

      {/* Summary Cards */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-blue-500">
          <div className="flex items-center gap-2 mb-1">
            <ShieldCheck className="w-5 h-5 text-blue-500" />
            <span className="text-sm font-medium text-gray-500 dark:text-gray-400">{t('report:quality.totalCerts')}</span>
          </div>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{summary.total.toLocaleString()}</p>
          <p className="text-xs text-gray-400 mt-0.5">{t('report:quality.totalCertsDesc')}</p>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-emerald-500">
          <div className="flex items-center gap-2 mb-1">
            <CheckCircle className="w-5 h-5 text-emerald-500" />
            <span className="text-sm font-medium text-gray-500 dark:text-gray-400">{t('report:quality.compliant')}</span>
          </div>
          <p className="text-2xl font-bold text-emerald-600">{summary.compliantCount.toLocaleString()}</p>
          <p className="text-xs text-gray-400 mt-0.5">{t('report:quality.complianceRate')}: {complianceRate}%</p>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-red-500">
          <div className="flex items-center gap-2 mb-1">
            <XCircle className="w-5 h-5 text-red-500" />
            <span className="text-sm font-medium text-gray-500 dark:text-gray-400">{t('report:quality.nonCompliant')}</span>
          </div>
          <p className="text-2xl font-bold text-red-600">{summary.nonCompliantCount.toLocaleString()}</p>
          <p className="text-xs text-gray-400 mt-0.5">{t('report:quality.nonCompliantDesc')}</p>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-amber-500">
          <div className="flex items-center gap-2 mb-1">
            <AlertTriangle className="w-5 h-5 text-amber-500" />
            <span className="text-sm font-medium text-gray-500 dark:text-gray-400">{t('report:quality.warning')}</span>
          </div>
          <p className="text-2xl font-bold text-amber-600">{summary.warningCount.toLocaleString()}</p>
          <p className="text-xs text-gray-400 mt-0.5">{t('report:quality.warningDesc')}</p>
        </div>
      </div>

      {/* Compliance Status Bar */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
        <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">{t('report:quality.complianceStatus')}</h3>
        <div className="flex h-6 rounded-full overflow-hidden bg-gray-100 dark:bg-gray-700">
          {statusSegments.map((seg, i) => (
            <div
              key={i}
              className={`${seg.color} transition-all`}
              style={{ width: `${(seg.count / summary.total) * 100}%` }}
              title={`${seg.label}: ${seg.count.toLocaleString()}`}
            />
          ))}
        </div>
        <div className="flex flex-wrap gap-4 mt-2">
          {statusSegments.map((seg, i) => (
            <div key={i} className="flex items-center gap-1.5 text-xs text-gray-500 dark:text-gray-400">
              <span className={`w-2.5 h-2.5 rounded-full ${seg.color}`} />
              <span>{seg.label}: {seg.count.toLocaleString()} ({((seg.count / summary.total) * 100).toFixed(1)}%)</span>
            </div>
          ))}
        </div>
      </div>

      {/* Charts Row 1: Category + Violations */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Category Breakdown */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">{t('report:quality.byCategory')}</h3>
          <ResponsiveContainer width="100%" height={220}>
            <BarChart
              data={(data.byCategory ?? []).map(c => ({
                name: t(`report:quality.category.${c.category}`),
                count: c.failCount,
                fill: CATEGORY_COLORS[c.category] || '#6b7280',
              }))}
              layout="vertical"
              margin={{ left: 80, right: 20, top: 5, bottom: 5 }}
            >
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
              <XAxis type="number" tick={{ fill: '#6B7280', fontSize: 12 }} />
              <YAxis type="category" dataKey="name" tick={{ fill: '#6B7280', fontSize: 11 }} width={75} />
              <Tooltip
                contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px' }}
                labelStyle={{ color: '#F3F4F6' }}
                itemStyle={{ color: '#F3F4F6' }}
              />
              <Bar dataKey="count" radius={[0, 4, 4, 0]}>
                {(data.byCategory ?? []).map((c, i) => (
                  <Cell key={i} fill={CATEGORY_COLORS[c.category] || '#6b7280'} />
                ))}
              </Bar>
            </BarChart>
          </ResponsiveContainer>
          <div className="mt-2 space-y-1">
            {(data.byCategory ?? []).map((c) => (
              <div key={c.category} className="flex items-center justify-between text-xs text-gray-500 dark:text-gray-400">
                <span>{t(`report:quality.category.${c.category}Desc`)}</span>
                <span className="font-medium">{c.failCount.toLocaleString()}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Top Violations */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">{t('report:quality.violationRanking')}</h3>
          <div className="space-y-2 max-h-[340px] overflow-y-auto">
            {(data.violations ?? []).slice(0, 15).map((v, i) => {
              const maxCount = data.violations[0]?.count || 1;
              return (
                <div key={i} className="flex items-center gap-2">
                  <span className={`w-5 h-5 rounded-full flex items-center justify-center text-xs font-bold flex-shrink-0 ${
                    i < 3 ? 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400' : 'bg-gray-100 text-gray-500 dark:bg-gray-700 dark:text-gray-400'
                  }`}>
                    {i + 1}
                  </span>
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center justify-between">
                      <span className="text-xs text-gray-700 dark:text-gray-300 truncate" title={v.violation}>
                        {translateViolation(v.violation, t)}
                      </span>
                      <span className="text-xs font-medium text-gray-500 dark:text-gray-400 ml-2 flex-shrink-0">
                        {v.count.toLocaleString()}
                      </span>
                    </div>
                    <div className="w-full h-1.5 bg-gray-100 dark:bg-gray-700 rounded-full mt-0.5">
                      <div
                        className="h-full rounded-full bg-red-400"
                        style={{ width: `${(v.count / maxCount) * 100}%` }}
                      />
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      </div>

      {/* Charts Row 2: Country + Cert Type */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* By Country */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">{t('report:quality.byCountry')}</h3>
          <ResponsiveContainer width="100%" height={Math.max(250, (data.byCountry ?? []).slice(0, 20).length * 28)}>
            <BarChart
              data={(data.byCountry ?? []).slice(0, 20).map(c => ({
                country: c.countryCode,
                compliant: c.compliant,
                nonCompliant: c.nonCompliant,
                warning: c.warning,
              }))}
              layout="vertical"
              margin={{ left: 10, right: 20, top: 5, bottom: 5 }}
            >
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
              <XAxis type="number" tick={{ fill: '#6B7280', fontSize: 11 }} />
              <YAxis
                type="category"
                dataKey="country"
                interval={0}
                tick={({ x, y, payload }: { x: number; y: number; payload?: { value: string } }) => {
                  if (!payload) return <g />;
                  const flagPath = getFlagSvgPath(payload.value);
                  return (
                    <g transform={`translate(${x - 42},${y - 6})`}>
                      <image href={flagPath} width={14} height={10} y={1} />
                      <text x={17} y={10} fill="#6B7280" fontSize={9}>{payload.value}</text>
                    </g>
                  );
                }}
                width={45}
              />
              <Tooltip
                content={({ active, payload }) => {
                  if (!active || !payload?.length || !payload[0]) return null;
                  const d = payload[0].payload as { country: string; compliant: number; nonCompliant: number; warning: number };
                  return (
                    <div className="bg-gray-800 border border-gray-600 rounded-lg p-2 text-xs text-gray-200">
                      <div className="flex items-center gap-1.5 mb-1">
                        <img src={getFlagSvgPath(d.country)} alt="" className="w-4 h-3" />
                        <span className="font-medium">{getCountryName(d.country)} ({d.country})</span>
                      </div>
                      <div className="text-emerald-400">{t('report:quality.compliant')}: {d.compliant}</div>
                      <div className="text-red-400">{t('report:quality.nonCompliant')}: {d.nonCompliant}</div>
                      <div className="text-amber-400">{t('report:quality.warning')}: {d.warning}</div>
                    </div>
                  );
                }}
              />
              <Bar dataKey="nonCompliant" stackId="a" fill="#ef4444" radius={0} />
              <Bar dataKey="warning" stackId="a" fill="#f59e0b" radius={0} />
              <Bar dataKey="compliant" stackId="a" fill="#10b981" radius={[0, 4, 4, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>

        {/* By Cert Type */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3">{t('report:quality.byCertType')}</h3>
          <div className="space-y-3">
            {certTypePieData.map((item) => {
              const nonCompliantPct = item.total > 0 ? ((item.nonCompliant / item.total) * 100).toFixed(1) : '0';
              const warningPct = item.total > 0 ? ((item.warning / item.total) * 100).toFixed(1) : '0';
              const compliantPct = item.total > 0 ? ((item.compliant / item.total) * 100).toFixed(1) : '0';
              return (
                <div key={item.name} className="border border-gray-200 dark:border-gray-700 rounded-xl p-3">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm font-semibold text-gray-800 dark:text-gray-200">{item.name}</span>
                    <span className="text-xs text-gray-500">{item.total.toLocaleString()}</span>
                  </div>
                  <div className="flex h-3 rounded-full overflow-hidden bg-gray-100 dark:bg-gray-700">
                    {item.nonCompliant > 0 && (
                      <div className="bg-red-500" style={{ width: `${(item.nonCompliant / item.total) * 100}%` }} />
                    )}
                    {item.warning > 0 && (
                      <div className="bg-amber-500" style={{ width: `${(item.warning / item.total) * 100}%` }} />
                    )}
                    {item.compliant > 0 && (
                      <div className="bg-emerald-500" style={{ width: `${(item.compliant / item.total) * 100}%` }} />
                    )}
                  </div>
                  <div className="flex gap-3 mt-1.5 text-xs text-gray-500 dark:text-gray-400">
                    <span className="flex items-center gap-1">
                      <span className="w-2 h-2 rounded-full bg-emerald-500" />
                      {compliantPct}%
                    </span>
                    <span className="flex items-center gap-1">
                      <span className="w-2 h-2 rounded-full bg-red-500" />
                      {nonCompliantPct}%
                    </span>
                    <span className="flex items-center gap-1">
                      <span className="w-2 h-2 rounded-full bg-amber-500" />
                      {warningPct}%
                    </span>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      </div>

      {/* Filters */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-gray-400" />
          <span className="text-sm font-medium text-gray-700 dark:text-gray-300">{t('report:quality.certList')}</span>
          {data.certificates.total > 0 && (
            <span className="text-xs text-gray-400 bg-gray-100 dark:bg-gray-700 px-2 py-0.5 rounded-full">
              {data.certificates.total.toLocaleString()}
            </span>
          )}
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
          <select
            id="quality-country-filter"
            name="country"
            value={countryFilter}
            onChange={(e) => { setCountryFilter(e.target.value); setPage(1); }}
            className="text-sm rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-gray-700 dark:text-gray-300 px-3 py-1.5"
          >
            <option value="">{t('report:quality.allCountries')}</option>
            {countryOptions.map(c => (
              <option key={c} value={c}>{getCountryName(c)} ({c})</option>
            ))}
          </select>
          <select
            id="quality-certtype-filter"
            name="certType"
            value={certTypeFilter}
            onChange={(e) => { setCertTypeFilter(e.target.value); setPage(1); }}
            className="text-sm rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-gray-700 dark:text-gray-300 px-3 py-1.5"
          >
            <option value="">{t('report:quality.allTypes')}</option>
            {certTypeOptions.map(c => (
              <option key={c} value={c}>{c}</option>
            ))}
          </select>
          <select
            id="quality-category-filter"
            name="category"
            value={categoryFilter}
            onChange={(e) => { setCategoryFilter(e.target.value); setPage(1); }}
            className="text-sm rounded-lg border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-700 text-gray-700 dark:text-gray-300 px-3 py-1.5"
          >
            <option value="">{t('report:quality.allCategories')}</option>
            <option value="algorithm">{t('report:quality.category.algorithm')}</option>
            <option value="keySize">{t('report:quality.category.keySize')}</option>
            <option value="keyUsage">{t('report:quality.category.keyUsage')}</option>
            <option value="extensions">{t('report:quality.category.extensions')}</option>
            <option value="validityPeriod">{t('report:quality.category.validityPeriod')}</option>
          </select>
          <button
            onClick={() => { setCountryFilter(''); setCertTypeFilter(''); setCategoryFilter(''); setPage(1); }}
            className="text-sm text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-300 dark:border-gray-600 rounded-lg px-3 py-1.5 hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
          >
            {t('common:button.reset')}
          </button>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead className="bg-slate-100 dark:bg-gray-700">
              <tr className="bg-gray-100 dark:bg-gray-900/70 border-b-2 border-gray-300 dark:border-gray-600 [&>th]:px-3 [&>th]:py-2.5 [&>th]:text-center [&>th]:text-xs [&>th]:font-semibold [&>th]:text-gray-600 [&>th]:dark:text-gray-300 [&>th]:whitespace-nowrap">
                <SortableHeader label={t('certificate:search.country')} sortKey="countryCode" sortConfig={sortConfig} onSort={requestSort} />
                <SortableHeader label={t('certificate:search.certType')} sortKey="certificateType" sortConfig={sortConfig} onSort={requestSort} />
                <SortableHeader label={t('report:quality.complianceLevel')} sortKey="complianceLevel" sortConfig={sortConfig} onSort={requestSort} />
                <SortableHeader label={t('certificate:search.issuer')} sortKey="issuerDn" sortConfig={sortConfig} onSort={requestSort} />
                <SortableHeader label={t('report:quality.violations')} sortKey="violations" sortConfig={sortConfig} onSort={requestSort} />
                <SortableHeader label={t('certificate:search.signatureAlgorithm')} sortKey="signatureAlgorithm" sortConfig={sortConfig} onSort={requestSort} />
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-100 dark:divide-gray-800">
              {sortedCerts.map((cert, i) => {
                const levelColor = cert.complianceLevel === 'NON_CONFORMANT'
                  ? 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400'
                  : 'bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400';
                const LevelIcon = cert.complianceLevel === 'NON_CONFORMANT' ? ShieldX : ShieldAlert;

                // Parse pipe-separated violations
                const violationList = cert.violations?.split('|').map(v => v.trim()).filter(Boolean) ?? [];

                return (
                  <tr key={i} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
                    <td className="px-3 py-2">
                      <div className="flex items-center gap-1.5">
                        <img src={getFlagSvgPath(cert.countryCode)} alt="" className="w-4 h-3" />
                        <span className="text-xs text-gray-700 dark:text-gray-300">{cert.countryCode}</span>
                      </div>
                    </td>
                    <td className="px-3 py-2">
                      <span className="text-xs font-medium text-gray-600 dark:text-gray-400">{cert.certificateType}</span>
                    </td>
                    <td className="px-3 py-2">
                      <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium ${levelColor}`}>
                        <LevelIcon className="w-3 h-3" />
                        {cert.complianceLevel === 'NON_CONFORMANT' ? t('report:quality.nonCompliant') : t('report:quality.warning')}
                      </span>
                    </td>
                    <td className="px-3 py-2 max-w-[200px]">
                      <span className="text-xs text-gray-600 dark:text-gray-400 truncate block" title={cert.subjectDn}>
                        {cert.subjectDn?.split(',').find(p => p.trim().startsWith('CN='))?.replace('CN=', '') || cert.subjectDn?.substring(0, 40)}
                      </span>
                    </td>
                    <td className="px-3 py-2 max-w-[250px]">
                      <div className="flex flex-wrap gap-1">
                        {violationList.slice(0, 3).map((v, j) => (
                          <span key={j} className="text-xs bg-red-50 text-red-600 dark:bg-red-900/20 dark:text-red-400 px-1.5 py-0.5 rounded" title={v}>
                            {translateViolation(v, t).substring(0, 30)}{translateViolation(v, t).length > 30 ? '...' : ''}
                          </span>
                        ))}
                        {violationList.length > 3 && (
                          <span className="text-xs text-gray-400">+{violationList.length - 3}</span>
                        )}
                      </div>
                    </td>
                    <td className="px-3 py-2">
                      <span className="text-xs text-gray-600 dark:text-gray-400">{cert.signatureAlgorithm}</span>
                    </td>
                  </tr>
                );
              })}
              {sortedCerts.length === 0 && (
                <tr>
                  <td colSpan={6} className="px-4 py-8 text-center text-sm text-gray-400">
                    {t('report:quality.noData')}
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="flex items-center justify-between px-4 py-3 border-t border-gray-200 dark:border-gray-700">
            <p className="text-xs text-gray-500">
              {t('report:quality.paginationInfo', {
                total: data.certificates.total.toLocaleString(),
                start: ((page - 1) * pageSize + 1).toLocaleString(),
                end: Math.min(page * pageSize, data.certificates.total).toLocaleString()
              })}
            </p>
            <div className="flex items-center gap-1">
              <button
                onClick={() => setPage(p => Math.max(1, p - 1))}
                disabled={page <= 1}
                className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="px-3 text-sm text-gray-600 dark:text-gray-400">{page} / {totalPages}</span>
              <button
                onClick={() => setPage(p => Math.min(totalPages, p + 1))}
                disabled={page >= totalPages}
                className="p-1 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-30"
              >
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
