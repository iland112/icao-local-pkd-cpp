import { useState, useEffect, useMemo } from 'react';
import {
  AlertCircle,
  AlertTriangle,
  CheckCircle,
  Download,
  Globe,
  Hash,
  Loader2,
  RefreshCw,
  ShieldAlert,
  Clock,
  Filter,
  ChevronLeft,
  ChevronRight,
  FileText,
  HelpCircle,
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
  PieChart,
  Pie,
  Cell,
  Legend,
} from 'recharts';
import { certificateApi } from '@/services/pkdApi';
import { exportDscNcReportToCsv } from '@/utils/csvExport';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';

countries.registerLocale(ko);

const getCountryName = (code: string): string => {
  return countries.getName(code, 'ko') || code;
};

const ALGORITHM_MAP: Record<string, string> = {
  'sha256WithRSAEncryption': 'RSA-SHA256',
  'sha384WithRSAEncryption': 'RSA-SHA384',
  'sha512WithRSAEncryption': 'RSA-SHA512',
  'sha1WithRSAEncryption': 'RSA-SHA1',
  'ecdsa-with-SHA256': 'ECDSA-SHA256',
  'ecdsa-with-SHA384': 'ECDSA-SHA384',
  'ecdsa-with-SHA512': 'ECDSA-SHA512',
};

const formatAlgorithm = (alg?: string): string => {
  if (!alg) return '-';
  return ALGORITHM_MAP[alg] || alg;
};

const PIE_COLORS = ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899', '#06b6d4', '#84cc16'];

interface DscNcSummary {
  totalDscNc: number;
  countryCount: number;
  conformanceCodeCount: number;
  validityBreakdown: { VALID: number; EXPIRED: number; NOT_YET_VALID: number; UNKNOWN: number };
}

interface ConformanceCodeEntry {
  code: string;
  description: string;
  count: number;
}

interface CountryEntry {
  countryCode: string;
  count: number;
  validCount: number;
  expiredCount: number;
}

interface YearEntry {
  year: number;
  count: number;
}

interface AlgorithmEntry {
  algorithm: string;
  count: number;
}

interface CertificateItem {
  fingerprint: string;
  countryCode: string;
  subjectDn: string;
  issuerDn: string;
  serialNumber: string;
  notBefore: string;
  notAfter: string;
  validity: string;
  signatureAlgorithm?: string;
  publicKeyAlgorithm?: string;
  publicKeySize?: number;
  pkdConformanceCode?: string;
  pkdConformanceText?: string;
  pkdVersion?: string;
}

interface DscNcReportData {
  success: boolean;
  summary: DscNcSummary;
  conformanceCodes: ConformanceCodeEntry[];
  byCountry: CountryEntry[];
  byYear: YearEntry[];
  bySignatureAlgorithm: AlgorithmEntry[];
  byPublicKeyAlgorithm: AlgorithmEntry[];
  certificates: {
    total: number;
    page: number;
    size: number;
    items: CertificateItem[];
  };
}

export default function DscNcReport() {
  const [data, setData] = useState<DscNcReportData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Filters
  const [countryFilter, setCountryFilter] = useState('');
  const [codeFilter, setCodeFilter] = useState('');
  const [page, setPage] = useState(1);
  const pageSize = 50;

  const fetchReport = async () => {
    setLoading(true);
    setError(null);
    try {
      const params: { country?: string; conformanceCode?: string; page: number; size: number } = {
        page,
        size: pageSize,
      };
      if (countryFilter) params.country = countryFilter;
      if (codeFilter) params.conformanceCode = codeFilter;

      const response = await certificateApi.getDscNcReport(params);
      setData(response.data as DscNcReportData);
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : 'Failed to load report';
      setError(message);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchReport();
  }, [page, countryFilter, codeFilter]);

  const handleExportCsv = async () => {
    try {
      const params: { page: number; size: number; country?: string; conformanceCode?: string } = {
        page: 1,
        size: 1000,
      };
      if (countryFilter) params.country = countryFilter;
      if (codeFilter) params.conformanceCode = codeFilter;

      const response = await certificateApi.getDscNcReport(params);
      const reportData = response.data as DscNcReportData;
      const timestamp = new Date().toISOString().slice(0, 10);
      exportDscNcReportToCsv(reportData.certificates.items, `dsc-nc-report-${timestamp}.csv`);
    } catch (err) {
      if (import.meta.env.DEV) console.error('CSV export failed:', err);
    }
  };

  const expiredRate = useMemo(() => {
    if (!data) return 0;
    const total = data.summary.totalDscNc;
    return total > 0 ? ((data.summary.validityBreakdown.EXPIRED / total) * 100) : 0;
  }, [data]);

  const hasActiveFilters = countryFilter || codeFilter;
  const totalPages = data ? Math.ceil(data.certificates.total / pageSize) : 0;

  if (loading && !data) {
    return (
      <div className="w-full px-4 lg:px-6 py-4 flex items-center justify-center py-20">
        <Loader2 className="w-8 h-8 animate-spin text-amber-500" />
      </div>
    );
  }

  if (error && !data) {
    return (
      <div className="w-full px-4 lg:px-6 py-4 space-y-6">
        <PageHeader onRefresh={fetchReport} onExport={handleExportCsv} loading={loading} />
        <div className="p-4 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl flex items-start gap-3">
          <AlertCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0 mt-0.5" />
          <div>
            <h3 className="font-semibold text-red-900 dark:text-red-300">데이터 로드 실패</h3>
            <p className="text-sm text-red-700 dark:text-red-400">{error}</p>
          </div>
        </div>
      </div>
    );
  }

  if (!data) return null;

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <PageHeader onRefresh={fetchReport} onExport={handleExportCsv} loading={loading} />

      {/* Summary Cards */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-orange-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">국가 수</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                {data.summary.countryCount}
              </p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">발급 국가</p>
            </div>
            <div className="p-3 rounded-xl bg-orange-50 dark:bg-orange-900/30">
              <Globe className="w-8 h-8 text-orange-500" />
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-amber-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">총 DSC_NC</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                {data.summary.totalDscNc.toLocaleString()}
              </p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">비준수 인증서</p>
            </div>
            <div className="p-3 rounded-xl bg-amber-50 dark:bg-amber-900/30">
              <ShieldAlert className="w-8 h-8 text-amber-500" />
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-red-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">비준수 코드</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                {data.summary.conformanceCodeCount}
              </p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">유형 분류</p>
            </div>
            <div className="p-3 rounded-xl bg-red-50 dark:bg-red-900/30">
              <Hash className="w-8 h-8 text-red-500" />
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-rose-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">만료율</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                {expiredRate.toFixed(1)}%
              </p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                {data.summary.validityBreakdown.EXPIRED.toLocaleString()}건 만료
              </p>
            </div>
            <div className="p-3 rounded-xl bg-rose-50 dark:bg-rose-900/30">
              <Clock className="w-8 h-8 text-rose-500" />
            </div>
          </div>
        </div>
      </div>

      {/* Validity Status Bar */}
      <ValidityBar breakdown={data.summary.validityBreakdown} total={data.summary.totalDscNc} />

      {/* Charts Row 1 */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Conformance Code Chart */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <AlertTriangle className="w-5 h-5 text-amber-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">비준수 코드별 분포</h3>
          </div>
          <ResponsiveContainer width="100%" height={Math.max(250, data.conformanceCodes.length * 32)}>
            <BarChart data={data.conformanceCodes} layout="vertical" margin={{ left: 130, right: 20, top: 5, bottom: 5 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
              <XAxis type="number" tick={{ fill: '#6B7280', fontSize: 12 }} />
              <YAxis type="category" dataKey="code" tick={{ fill: '#6B7280', fontSize: 11 }} width={125} />
              <Tooltip
                content={({ active, payload }) => {
                  if (!active || !payload?.length) return null;
                  const item = payload[0].payload as ConformanceCodeEntry;
                  return (
                    <div className="bg-gray-900 dark:bg-gray-700 border border-gray-700 dark:border-gray-600 rounded-lg px-3 py-2 shadow-lg max-w-[320px]">
                      <div className="flex items-center gap-1.5 mb-1">
                        <span className="text-xs font-mono text-amber-400">{item.code}</span>
                        <HelpCircle className="w-3 h-3 text-gray-400" />
                      </div>
                      <p className="text-xs text-gray-300 mb-1.5">{item.description}</p>
                      <p className="text-sm font-semibold text-white">{item.count}건</p>
                    </div>
                  );
                }}
              />
              <Bar dataKey="count" fill="#f59e0b" radius={[0, 4, 4, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>

        {/* Country Chart */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <Globe className="w-5 h-5 text-orange-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">국가별 분포</h3>
            <span className="text-sm text-gray-500 dark:text-gray-400">(상위 20개국)</span>
          </div>
          <ResponsiveContainer width="100%" height={Math.max(250, Math.min(data.byCountry.length, 20) * 32)}>
            <BarChart data={data.byCountry.slice(0, 20)} layout="vertical" margin={{ left: 70, right: 20, top: 5, bottom: 5 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
              <XAxis type="number" tick={{ fill: '#6B7280', fontSize: 12 }} />
              <YAxis
                type="category"
                dataKey="countryCode"
                width={65}
                tick={((props: any) => {
                  const { x, y, payload } = props;
                  const code = payload.value;
                  const flagPath = getFlagSvgPath(code);
                  return (
                    <g transform={`translate(${x},${y})`}>
                      {flagPath && (
                        <image href={flagPath} x={-62} y={-8} width={20} height={14} style={{ borderRadius: 2 }} />
                      )}
                      <text x={-8} y={4} textAnchor="end" fill="#6B7280" fontSize={12}>
                        {code}
                      </text>
                    </g>
                  );
                }) as any}
              />
              <Tooltip
                content={({ active, payload }) => {
                  if (!active || !payload?.length) return null;
                  const item = payload[0].payload as CountryEntry;
                  const flagPath = getFlagSvgPath(item.countryCode);
                  return (
                    <div className="bg-gray-900 dark:bg-gray-700 border border-gray-700 dark:border-gray-600 rounded-lg px-3 py-2 shadow-lg">
                      <div className="flex items-center gap-2 mb-1.5">
                        {flagPath && <img src={flagPath} alt={item.countryCode} className="w-5 h-3.5 object-cover rounded-sm shadow-sm" />}
                        <span className="text-sm font-semibold text-white">{item.countryCode}</span>
                        <span className="text-xs text-gray-400">{getCountryName(item.countryCode)}</span>
                      </div>
                      <div className="flex gap-3 text-xs">
                        <span className="text-green-400">Valid: {item.validCount}</span>
                        <span className="text-red-400">Expired: {item.expiredCount}</span>
                        <span className="text-gray-300 font-medium">Total: {item.count}</span>
                      </div>
                    </div>
                  );
                }}
              />
              <Legend />
              <Bar dataKey="validCount" name="Valid" stackId="a" fill="#10b981" />
              <Bar dataKey="expiredCount" name="Expired" stackId="a" fill="#ef4444" radius={[0, 4, 4, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>
      </div>

      {/* Charts Row 2 */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Year Chart */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <Clock className="w-5 h-5 text-blue-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">발급년도별 분포</h3>
          </div>
          <ResponsiveContainer width="100%" height={250}>
            <BarChart data={data.byYear} margin={{ left: 10, right: 10, top: 5, bottom: 5 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
              <XAxis dataKey="year" tick={{ fill: '#6B7280', fontSize: 11 }} />
              <YAxis tick={{ fill: '#6B7280', fontSize: 12 }} />
              <Tooltip
                contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px' }}
                labelStyle={{ color: '#F3F4F6' }}
                itemStyle={{ color: '#F3F4F6' }}
              />
              <Bar dataKey="count" fill="#3b82f6" radius={[4, 4, 0, 0]} />
            </BarChart>
          </ResponsiveContainer>
        </div>

        {/* Signature Algorithm Pie */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <FileText className="w-5 h-5 text-violet-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">서명 알고리즘</h3>
          </div>
          <ResponsiveContainer width="100%" height={250}>
            <PieChart>
              <Pie
                data={data.bySignatureAlgorithm as any[]}
                dataKey="count"
                nameKey="algorithm"
                cx="50%"
                cy="50%"
                outerRadius={80}
                label={((props: any) => {
                  const alg = String(props.algorithm || '');
                  const pct = Number(props.percent || 0);
                  return `${alg.replace('WithRSAEncryption', '').replace('with', '/')} ${(pct * 100).toFixed(0)}%`;
                }) as any}
                labelLine={false}
              >
                {data.bySignatureAlgorithm.map((_, i) => (
                  <Cell key={i} fill={PIE_COLORS[i % PIE_COLORS.length]} />
                ))}
              </Pie>
              <Tooltip
                contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px' }}
                labelStyle={{ color: '#F3F4F6' }}
                itemStyle={{ color: '#F3F4F6' }}
              />
            </PieChart>
          </ResponsiveContainer>
        </div>

        {/* Public Key Algorithm Pie */}
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <FileText className="w-5 h-5 text-teal-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">공개키 알고리즘</h3>
          </div>
          <ResponsiveContainer width="100%" height={250}>
            <PieChart>
              <Pie
                data={data.byPublicKeyAlgorithm as any[]}
                dataKey="count"
                nameKey="algorithm"
                cx="50%"
                cy="50%"
                outerRadius={80}
                label={((props: any) => {
                  const alg = String(props.algorithm || '');
                  const pct = Number(props.percent || 0);
                  return `${alg} ${(pct * 100).toFixed(0)}%`;
                }) as any}
                labelLine={false}
              >
                {data.byPublicKeyAlgorithm.map((_, i) => (
                  <Cell key={i} fill={PIE_COLORS[i % PIE_COLORS.length]} />
                ))}
              </Pie>
              <Tooltip
                contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px' }}
                labelStyle={{ color: '#F3F4F6' }}
                itemStyle={{ color: '#F3F4F6' }}
              />
            </PieChart>
          </ResponsiveContainer>
        </div>
      </div>

      {/* Filters */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-amber-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 필터</h3>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
          {/* Country Filter */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">국가</label>
            <select
              value={countryFilter}
              onChange={(e) => { setCountryFilter(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-amber-500"
            >
              <option value="">전체</option>
              {data.byCountry.map(c => (
                <option key={c.countryCode} value={c.countryCode}>
                  {c.countryCode} - {getCountryName(c.countryCode)} ({c.count})
                </option>
              ))}
            </select>
          </div>

          {/* Conformance Code Filter */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">비준수 코드</label>
            <select
              value={codeFilter}
              onChange={(e) => { setCodeFilter(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-amber-500"
            >
              <option value="">전체</option>
              {data.conformanceCodes.map(c => (
                <option key={c.code} value={c.code}>
                  {c.code} ({c.count})
                </option>
              ))}
            </select>
          </div>

          {/* Result count + Reset */}
          <div className="col-span-2 flex items-end gap-3">
            {hasActiveFilters && (
              <button
                onClick={() => { setCountryFilter(''); setCodeFilter(''); setPage(1); }}
                className="px-3 py-2 text-xs font-medium text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
              >
                초기화
              </button>
            )}
            <span className="ml-auto text-sm text-gray-500 dark:text-gray-400 pb-2">
              {data.certificates.total.toLocaleString()}건
            </span>
          </div>
        </div>
      </div>

      {/* Certificate Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        {/* Table Header */}
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2">
          <ShieldAlert className="w-4 h-4 text-amber-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">비준수 인증서 목록</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-amber-100 dark:bg-amber-900/30 text-amber-600 dark:text-amber-400">
            {data.certificates.total.toLocaleString()}건
          </span>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full">
            <thead className="bg-gray-50 dark:bg-gray-700/50">
              <tr>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">국가</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">발급년도</th>
                <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">서명 알고리즘</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">공개키</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">유효기간</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">상태</th>
                <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">비준수 코드</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
              {data.certificates.items.map((cert) => (
                <tr key={cert.fingerprint} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
                  {/* 국가 */}
                  <td className="px-5 py-3 whitespace-nowrap">
                    <div className="flex items-center justify-center gap-2">
                      {getFlagSvgPath(cert.countryCode) && (
                        <img
                          src={getFlagSvgPath(cert.countryCode)}
                          alt={cert.countryCode}
                          className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                          onError={(e) => { e.currentTarget.style.display = 'none'; }}
                        />
                      )}
                      <span className="text-sm font-medium text-gray-900 dark:text-white">{cert.countryCode}</span>
                    </div>
                  </td>
                  {/* 발급년도 */}
                  <td className="px-5 py-3 text-center whitespace-nowrap">
                    <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
                      {cert.notBefore ? new Date(cert.notBefore).getFullYear() : '-'}
                    </span>
                  </td>
                  {/* 서명 알고리즘 */}
                  <td className="px-5 py-3 text-sm text-gray-600 dark:text-gray-300 whitespace-nowrap">
                    {formatAlgorithm(cert.signatureAlgorithm)}
                  </td>
                  {/* 공개키 알고리즘 + 크기 */}
                  <td className="px-5 py-3 text-center whitespace-nowrap">
                    <span className="text-sm text-gray-600 dark:text-gray-300">
                      {cert.publicKeyAlgorithm || '-'}
                    </span>
                    {cert.publicKeySize != null && (
                      <span className={cn(
                        'ml-1.5 text-xs font-medium px-1.5 py-0.5 rounded',
                        cert.publicKeySize < 2048
                          ? 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400'
                          : 'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-400'
                      )}>
                        {cert.publicKeySize}bit
                      </span>
                    )}
                  </td>
                  {/* 유효기간 */}
                  <td className="px-5 py-3 text-center whitespace-nowrap">
                    <span className="text-xs text-gray-600 dark:text-gray-300">
                      {cert.notBefore ? new Date(cert.notBefore).toLocaleDateString() : '-'}
                      <span className="text-gray-400 dark:text-gray-500 mx-1">~</span>
                      {cert.notAfter ? new Date(cert.notAfter).toLocaleDateString() : '-'}
                    </span>
                  </td>
                  {/* 상태 */}
                  <td className="px-5 py-3 text-center whitespace-nowrap">
                    <ValidityBadge validity={cert.validity} />
                  </td>
                  {/* 비준수 코드 + ? 도움말 */}
                  <td className="px-5 py-3 whitespace-nowrap">
                    <div className="flex items-center gap-1.5">
                      <span className="text-xs text-amber-700 dark:text-amber-400 font-mono">
                        {cert.pkdConformanceCode || '-'}
                      </span>
                      {cert.pkdConformanceText && (
                        <span className="relative group">
                          <HelpCircle className="w-3.5 h-3.5 text-gray-400 dark:text-gray-500 cursor-help hover:text-amber-500 dark:hover:text-amber-400 transition-colors" />
                          <span className="absolute bottom-full right-0 mb-2 px-3 py-2 text-xs text-white bg-gray-900 dark:bg-gray-700 rounded-lg shadow-lg whitespace-normal max-w-[300px] w-max opacity-0 invisible group-hover:opacity-100 group-hover:visible transition-all duration-200 z-50 pointer-events-none">
                            {cert.pkdConformanceText}
                            <span className="absolute top-full right-3 border-4 border-transparent border-t-gray-900 dark:border-t-gray-700" />
                          </span>
                        </span>
                      )}
                    </div>
                  </td>
                </tr>
              ))}
              {data.certificates.items.length === 0 && (
                <tr>
                  <td colSpan={7} className="px-5 py-16 text-center">
                    <div className="flex flex-col items-center text-gray-500 dark:text-gray-400">
                      <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
                      <p className="text-lg font-medium">검색 결과 없음</p>
                      <p className="text-sm mt-1">필터 조건을 변경해 보세요</p>
                    </div>
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
            <p className="text-xs text-gray-500 dark:text-gray-400">
              총 {data.certificates.total.toLocaleString()}개 중{' '}
              {((page - 1) * pageSize + 1).toLocaleString()}-{Math.min(page * pageSize, data.certificates.total).toLocaleString()}개 표시
            </p>
            <div className="flex items-center gap-1">
              <button
                onClick={() => setPage(p => Math.max(1, p - 1))}
                disabled={page <= 1}
                className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="px-3 text-sm text-gray-600 dark:text-gray-300">
                {page} / {totalPages}
              </span>
              <button
                onClick={() => setPage(p => Math.min(totalPages, p + 1))}
                disabled={page >= totalPages}
                className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
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

// ─── Sub Components ──────────────────────────────────────────────────────────

function PageHeader({ onRefresh, onExport, loading }: { onRefresh: () => void; onExport: () => void; loading: boolean }) {
  return (
    <div className="mb-6">
      <div className="flex items-center gap-4">
        <div className="p-3 rounded-xl bg-gradient-to-br from-amber-500 to-orange-600 shadow-lg">
          <ShieldAlert className="w-7 h-7 text-white" />
        </div>
        <div className="flex-1">
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">DSC_NC 비준수 인증서 보고서</h1>
          <p className="text-sm text-gray-500 dark:text-gray-400">
            ICAO PKD Non-Conformant DSC 인증서 분석
          </p>
        </div>
        <div className="flex gap-2">
          <button
            onClick={onRefresh}
            disabled={loading}
            className="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-sm font-medium transition-all duration-200 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700"
          >
            <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
          </button>
          <button
            onClick={onExport}
            className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-amber-500 to-orange-500 hover:from-amber-600 hover:to-orange-600 transition-all duration-200 shadow-md hover:shadow-lg"
          >
            <Download className="w-4 h-4" />
            CSV Export
          </button>
        </div>
      </div>
    </div>
  );
}

function ValidityBar({ breakdown, total }: { breakdown: DscNcSummary['validityBreakdown']; total: number }) {
  if (total === 0) return null;

  const segments = [
    { key: 'VALID', label: 'Valid', count: breakdown.VALID, color: 'bg-green-500', icon: <CheckCircle className="w-3 h-3" /> },
    { key: 'EXPIRED', label: 'Expired', count: breakdown.EXPIRED, color: 'bg-red-500', icon: <XCircle className="w-3 h-3" /> },
    { key: 'NOT_YET_VALID', label: 'Not Yet Valid', count: breakdown.NOT_YET_VALID, color: 'bg-yellow-500', icon: <Clock className="w-3 h-3" /> },
    { key: 'UNKNOWN', label: 'Unknown', count: breakdown.UNKNOWN, color: 'bg-gray-400', icon: <AlertCircle className="w-3 h-3" /> },
  ].filter(s => s.count > 0);

  return (
    <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
      <div className="flex items-center gap-2 mb-3">
        <CheckCircle className="w-5 h-5 text-green-500" />
        <h3 className="text-base font-bold text-gray-900 dark:text-white">유효성 상태</h3>
      </div>
      <div className="flex rounded-full overflow-hidden h-7 shadow-inner">
        {segments.map(seg => (
          <div
            key={seg.key}
            className={cn(seg.color, 'flex items-center justify-center text-xs font-medium text-white transition-all')}
            style={{ width: `${(seg.count / total) * 100}%` }}
            title={`${seg.label}: ${seg.count} (${((seg.count / total) * 100).toFixed(1)}%)`}
          >
            {(seg.count / total) > 0.08 && `${seg.label} ${seg.count}`}
          </div>
        ))}
      </div>
      <div className="flex gap-5 mt-3">
        {segments.map(seg => (
          <span key={seg.key} className="flex items-center gap-1.5 text-xs text-gray-600 dark:text-gray-400">
            <span className={cn('w-2.5 h-2.5 rounded-full', seg.color)} />
            {seg.label}: {seg.count.toLocaleString()} ({((seg.count / total) * 100).toFixed(1)}%)
          </span>
        ))}
      </div>
    </div>
  );
}

function ValidityBadge({ validity }: { validity: string }) {
  const config: Record<string, { bg: string; text: string; icon: React.ReactNode }> = {
    VALID: {
      bg: 'bg-green-100 dark:bg-green-900/30',
      text: 'text-green-600 dark:text-green-400',
      icon: <CheckCircle className="w-3 h-3" />,
    },
    EXPIRED: {
      bg: 'bg-red-100 dark:bg-red-900/30',
      text: 'text-red-600 dark:text-red-400',
      icon: <XCircle className="w-3 h-3" />,
    },
    NOT_YET_VALID: {
      bg: 'bg-yellow-100 dark:bg-yellow-900/30',
      text: 'text-yellow-600 dark:text-yellow-400',
      icon: <AlertCircle className="w-3 h-3" />,
    },
    UNKNOWN: {
      bg: 'bg-gray-100 dark:bg-gray-700',
      text: 'text-gray-600 dark:text-gray-400',
      icon: <AlertCircle className="w-3 h-3" />,
    },
  };

  const style = config[validity] || config.UNKNOWN;

  return (
    <span className={cn('inline-flex items-center gap-1 px-2 py-1 rounded-full text-xs font-medium', style.bg, style.text)}>
      {style.icon}
      {validity}
    </span>
  );
}
