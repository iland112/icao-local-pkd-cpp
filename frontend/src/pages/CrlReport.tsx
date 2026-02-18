import { useState, useEffect, useCallback, useMemo } from 'react';
import {
  AlertCircle, AlertTriangle, CheckCircle, Download, Globe,
  Loader2, RefreshCw, FileWarning, X, ChevronLeft, ChevronRight,
  Filter, Clock, XCircle, FileText, ShieldAlert,
} from 'lucide-react';
import {
  BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer,
  PieChart, Pie, Cell,
} from 'recharts';
import countries from 'i18n-iso-countries';
import ko from 'i18n-iso-countries/langs/ko.json';
import { certificateApi } from '@/services/pkdApi';
import { exportCrlReportToCsv } from '@/utils/csvExport';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';

countries.registerLocale(ko);

const getCountryName = (code: string): string => countries.getName(code, 'ko') || code;

// --- Types ---

interface CrlSummary {
  totalCrls: number;
  countryCount: number;
  validCount: number;
  expiredCount: number;
  totalRevokedCertificates: number;
}

interface CrlCountryEntry { countryCode: string; crlCount: number; revokedCount: number; }
interface AlgorithmEntry { algorithm: string; count: number; }
interface RevocationReasonEntry { reason: string; count: number; }

interface CrlItem {
  id: string;
  countryCode: string;
  issuerDn: string;
  thisUpdate: string;
  nextUpdate: string;
  crlNumber: string;
  status: string;
  revokedCount: number;
  signatureAlgorithm: string;
  fingerprint: string;
  storedInLdap: boolean;
  createdAt: string;
}

interface CrlReportData {
  success: boolean;
  summary: CrlSummary;
  byCountry: CrlCountryEntry[];
  bySignatureAlgorithm: AlgorithmEntry[];
  byRevocationReason: RevocationReasonEntry[];
  crls: { total: number; page: number; size: number; items: CrlItem[] };
}

interface RevokedCertificateItem {
  serialNumber: string;
  revocationDate: string;
  revocationReason: string;
}

interface CrlDetailData {
  success: boolean;
  crl: CrlItem;
  revokedCertificates: { total: number; items: RevokedCertificateItem[] };
}

// --- Constants ---

const PIE_COLORS = ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899', '#06b6d4', '#84cc16'];

const REASON_KO: Record<string, string> = {
  unspecified: '미지정', keyCompromise: '키 손상', cACompromise: 'CA 손상',
  affiliationChanged: '소속 변경', superseded: '대체됨', cessationOfOperation: '운영 중단',
  certificateHold: '인증서 보류', removeFromCRL: 'CRL에서 제거',
  privilegeWithdrawn: '권한 철회', aACompromise: 'AA 손상', unknown: '알 수 없음',
};

const ALGORITHM_MAP: Record<string, string> = {
  'sha256WithRSAEncryption': 'RSA-SHA256',
  'sha384WithRSAEncryption': 'RSA-SHA384',
  'sha512WithRSAEncryption': 'RSA-SHA512',
  'sha1WithRSAEncryption': 'RSA-SHA1',
  'ecdsa-with-SHA256': 'ECDSA-SHA256',
  'ecdsa-with-SHA384': 'ECDSA-SHA384',
  'ecdsa-with-SHA512': 'ECDSA-SHA512',
  'RSAWithSHA256': 'RSA-SHA256',
};

const formatAlgorithm = (alg?: string): string => {
  if (!alg) return '-';
  return ALGORITHM_MAP[alg] || alg;
};

const truncateDn = (s: string, n: number) => s && s.length > n ? s.slice(0, n) + '\u2026' : s;

// --- Component ---

export default function CrlReport() {
  const [reportData, setReportData] = useState<CrlReportData | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [countryFilter, setCountryFilter] = useState('');
  const [statusFilter, setStatusFilter] = useState('');
  const [page, setPage] = useState(1);
  const pageSize = 50;

  const [selectedCrl, setSelectedCrl] = useState<CrlItem | null>(null);
  const [detailData, setDetailData] = useState<CrlDetailData | null>(null);
  const [detailLoading, setDetailLoading] = useState(false);
  const [dialogOpen, setDialogOpen] = useState(false);

  const fetchReport = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const params: Record<string, string | number> = { page, size: pageSize };
      if (countryFilter) params.country = countryFilter;
      if (statusFilter) params.status = statusFilter;
      const res = await certificateApi.getCrlReport(params as any);
      setReportData(res.data);
    } catch (e: unknown) {
      const message = e instanceof Error ? e.message : 'Failed to load CRL report';
      setError(message);
    } finally {
      setLoading(false);
    }
  }, [countryFilter, statusFilter, page]);

  useEffect(() => { fetchReport(); }, [fetchReport]);

  const handleRowClick = async (crl: CrlItem) => {
    setSelectedCrl(crl);
    setDialogOpen(true);
    setDetailLoading(true);
    setDetailData(null);
    try {
      const res = await certificateApi.getCrlDetail(crl.id);
      setDetailData(res.data);
    } catch {
      setDetailData(null);
    } finally {
      setDetailLoading(false);
    }
  };

  const closeDialog = () => { setDialogOpen(false); setSelectedCrl(null); setDetailData(null); };
  const resetFilters = () => { setCountryFilter(''); setStatusFilter(''); setPage(1); };
  const hasActiveFilters = countryFilter || statusFilter;

  const handleExport = () => {
    if (!reportData) return;
    exportCrlReportToCsv(reportData.crls.items, `crl-report-${new Date().toISOString().slice(0, 10)}.csv`);
  };

  const summary = reportData?.summary;
  const totalPages = reportData ? Math.ceil(reportData.crls.total / pageSize) : 0;

  const expiredRate = useMemo(() => {
    if (!summary || summary.totalCrls === 0) return 0;
    return (summary.expiredCount / summary.totalCrls) * 100;
  }, [summary]);

  // Sort copies (avoid mutating frozen React state)
  const sortedByCountry = useMemo(() =>
    reportData ? [...reportData.byCountry].sort((a, b) => b.revokedCount - a.revokedCount).slice(0, 20) : [],
    [reportData]
  );
  const sortedByReason = useMemo(() =>
    reportData ? [...reportData.byRevocationReason].sort((a, b) => b.count - a.count) : [],
    [reportData]
  );

  // --- Loading / Error ---

  if (loading && !reportData) {
    return (
      <div className="w-full px-4 lg:px-6 py-4 flex items-center justify-center py-20">
        <Loader2 className="w-8 h-8 animate-spin text-amber-500" />
      </div>
    );
  }

  if (error && !reportData) {
    return (
      <div className="w-full px-4 lg:px-6 py-4 space-y-6">
        <PageHeader onRefresh={fetchReport} onExport={handleExport} loading={loading} />
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

  if (!reportData || !summary) return null;

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <PageHeader onRefresh={fetchReport} onExport={handleExport} loading={loading} />

      {/* Summary Cards */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-blue-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">전체 CRL</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">{summary.totalCrls.toLocaleString()}</p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">Certificate Revocation Lists</p>
            </div>
            <div className="p-3 rounded-xl bg-blue-50 dark:bg-blue-900/30">
              <FileText className="w-8 h-8 text-blue-500" />
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-orange-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">국가 수</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">{summary.countryCount}</p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">발급 국가</p>
            </div>
            <div className="p-3 rounded-xl bg-orange-50 dark:bg-orange-900/30">
              <Globe className="w-8 h-8 text-orange-500" />
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-red-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">폐기 인증서</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">{summary.totalRevokedCertificates.toLocaleString()}</p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">총 폐기된 인증서</p>
            </div>
            <div className="p-3 rounded-xl bg-red-50 dark:bg-red-900/30">
              <AlertTriangle className="w-8 h-8 text-red-500" />
            </div>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-rose-500">
          <div className="flex items-center justify-between">
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">만료율</p>
              <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">{expiredRate.toFixed(1)}%</p>
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                {summary.expiredCount}건 만료 / {summary.validCount}건 유효
              </p>
            </div>
            <div className="p-3 rounded-xl bg-rose-50 dark:bg-rose-900/30">
              <Clock className="w-8 h-8 text-rose-500" />
            </div>
          </div>
        </div>
      </div>

      {/* Validity Status Bar */}
      <ValidityBar validCount={summary.validCount} expiredCount={summary.expiredCount} total={summary.totalCrls} />

      {/* Charts Row 1: Country + Revocation Reason */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Country Distribution */}
        {sortedByCountry.length > 0 && (
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <div className="flex items-center gap-2 mb-4">
              <Globe className="w-5 h-5 text-orange-500" />
              <h3 className="text-base font-bold text-gray-900 dark:text-white">국가별 폐기 인증서</h3>
              <span className="text-sm text-gray-500 dark:text-gray-400">(상위 20개국)</span>
            </div>
            <ResponsiveContainer width="100%" height={Math.max(250, sortedByCountry.length * 30)}>
              <BarChart data={sortedByCountry} layout="vertical" margin={{ left: 70, right: 20, top: 5, bottom: 5 }}>
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
                        {flagPath && <image href={flagPath} x={-62} y={-8} width={20} height={14} style={{ borderRadius: 2 }} />}
                        <text x={-8} y={4} textAnchor="end" fill="#6B7280" fontSize={12}>{code}</text>
                      </g>
                    );
                  }) as any}
                />
                <Tooltip
                  content={({ active, payload }) => {
                    if (!active || !payload?.length) return null;
                    const item = payload[0].payload as CrlCountryEntry;
                    const flagPath = getFlagSvgPath(item.countryCode);
                    return (
                      <div className="bg-gray-900 dark:bg-gray-700 border border-gray-700 dark:border-gray-600 rounded-lg px-3 py-2 shadow-lg">
                        <div className="flex items-center gap-2 mb-1.5">
                          {flagPath && <img src={flagPath} alt={item.countryCode} className="w-5 h-3.5 object-cover rounded-sm shadow-sm" />}
                          <span className="text-sm font-semibold text-white">{item.countryCode}</span>
                          <span className="text-xs text-gray-400">{getCountryName(item.countryCode)}</span>
                        </div>
                        <div className="flex gap-3 text-xs">
                          <span className="text-red-400">폐기: {item.revokedCount.toLocaleString()}</span>
                          <span className="text-gray-300">CRL: {item.crlCount}</span>
                        </div>
                      </div>
                    );
                  }}
                />
                <Bar dataKey="revokedCount" name="폐기 인증서" fill="#ef4444" radius={[0, 4, 4, 0]} />
              </BarChart>
            </ResponsiveContainer>
          </div>
        )}

        {/* Revocation Reason */}
        {sortedByReason.length > 0 && (
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
            <div className="flex items-center gap-2 mb-4">
              <AlertTriangle className="w-5 h-5 text-amber-500" />
              <h3 className="text-base font-bold text-gray-900 dark:text-white">폐기 사유별 분포</h3>
            </div>
            <ResponsiveContainer width="100%" height={300}>
              <BarChart data={sortedByReason} margin={{ left: 10, right: 20, top: 5, bottom: 60 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
                <XAxis dataKey="reason" tick={{ fill: '#6B7280', fontSize: 10 }} tickFormatter={(v: string) => REASON_KO[v] || v} angle={-35} textAnchor="end" height={70} />
                <YAxis tick={{ fill: '#6B7280', fontSize: 12 }} />
                <Tooltip
                  content={({ active, payload }) => {
                    if (!active || !payload?.length) return null;
                    const item = payload[0].payload as RevocationReasonEntry;
                    return (
                      <div className="bg-gray-900 dark:bg-gray-700 border border-gray-700 dark:border-gray-600 rounded-lg px-3 py-2 shadow-lg">
                        <p className="text-sm font-semibold text-white mb-0.5">{REASON_KO[item.reason] || item.reason}</p>
                        <p className="text-xs text-gray-400">{item.reason}</p>
                        <p className="text-sm text-amber-400 mt-1">{item.count.toLocaleString()}건</p>
                      </div>
                    );
                  }}
                />
                <Bar dataKey="count" fill="#f59e0b" radius={[4, 4, 0, 0]} />
              </BarChart>
            </ResponsiveContainer>
          </div>
        )}
      </div>

      {/* Signature Algorithm Pie */}
      {reportData.bySignatureAlgorithm.length > 0 && (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-6">
          <div className="flex items-center gap-2 mb-4">
            <FileText className="w-5 h-5 text-violet-500" />
            <h3 className="text-base font-bold text-gray-900 dark:text-white">서명 알고리즘 분포</h3>
          </div>
          <ResponsiveContainer width="100%" height={280}>
            <PieChart>
              <Pie
                data={reportData.bySignatureAlgorithm as any[]}
                dataKey="count"
                nameKey="algorithm"
                cx="50%"
                cy="50%"
                outerRadius={100}
                label={((props: any) => {
                  const alg = formatAlgorithm(String(props.algorithm || ''));
                  const pct = Number(props.percent || 0);
                  return `${alg} ${(pct * 100).toFixed(0)}%`;
                }) as any}
                labelLine={false}
              >
                {reportData.bySignatureAlgorithm.map((_, i) => (
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
      )}

      {/* Filters */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-amber-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 필터</h3>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">국가</label>
            <select
              value={countryFilter}
              onChange={(e) => { setCountryFilter(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-amber-500"
            >
              <option value="">전체</option>
              {[...reportData.byCountry].sort((a, b) => a.countryCode.localeCompare(b.countryCode)).map(c => (
                <option key={c.countryCode} value={c.countryCode}>
                  {c.countryCode} - {getCountryName(c.countryCode)} ({c.crlCount})
                </option>
              ))}
            </select>
          </div>
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">상태</label>
            <select
              value={statusFilter}
              onChange={(e) => { setStatusFilter(e.target.value); setPage(1); }}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 dark:text-gray-200 focus:outline-none focus:ring-2 focus:ring-amber-500"
            >
              <option value="">전체</option>
              <option value="valid">유효</option>
              <option value="expired">만료</option>
            </select>
          </div>
          <div className="col-span-2 flex items-end gap-3">
            {hasActiveFilters && (
              <button
                onClick={resetFilters}
                className="px-3 py-2 text-xs font-medium text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
              >
                초기화
              </button>
            )}
            <span className="ml-auto text-sm text-gray-500 dark:text-gray-400 pb-2">
              {reportData.crls.total.toLocaleString()}건
            </span>
          </div>
        </div>
      </div>

      {/* CRL Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2">
          <ShieldAlert className="w-4 h-4 text-amber-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">CRL 목록</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-amber-100 dark:bg-amber-900/30 text-amber-600 dark:text-amber-400">
            {reportData.crls.total.toLocaleString()}건
          </span>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full">
            <thead className="bg-gray-50 dark:bg-gray-700/50">
              <tr>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">국가</th>
                <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">Issuer</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">This Update</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">Next Update</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">상태</th>
                <th className="px-5 py-3 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">폐기 수</th>
                <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">알고리즘</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
              {reportData.crls.items.map(crl => (
                <tr key={crl.id} onClick={() => handleRowClick(crl)}
                  className="hover:bg-gray-50 dark:hover:bg-gray-700/50 cursor-pointer transition-colors">
                  <td className="px-5 py-3 whitespace-nowrap">
                    <div className="flex items-center justify-center gap-2">
                      {getFlagSvgPath(crl.countryCode) && (
                        <img src={getFlagSvgPath(crl.countryCode)} alt={crl.countryCode}
                          className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500"
                          onError={(e) => { e.currentTarget.style.display = 'none'; }} />
                      )}
                      <span className="text-sm font-medium text-gray-900 dark:text-white">{crl.countryCode}</span>
                    </div>
                  </td>
                  <td className="px-5 py-3 max-w-[220px]" title={crl.issuerDn}>
                    <span className="text-sm text-gray-600 dark:text-gray-300">{truncateDn(crl.issuerDn, 45)}</span>
                  </td>
                  <td className="px-5 py-3 text-center whitespace-nowrap text-sm text-gray-600 dark:text-gray-400">{crl.thisUpdate?.slice(0, 10)}</td>
                  <td className="px-5 py-3 text-center whitespace-nowrap text-sm text-gray-600 dark:text-gray-400">{crl.nextUpdate?.slice(0, 10) || '-'}</td>
                  <td className="px-5 py-3 text-center whitespace-nowrap">
                    <StatusBadge status={crl.status} />
                  </td>
                  <td className="px-5 py-3 text-center whitespace-nowrap">
                    {crl.revokedCount > 0 ? (
                      <span className="inline-flex items-center gap-1 px-2 py-1 rounded-full text-xs font-medium bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400">
                        <XCircle className="w-3 h-3" />
                        {crl.revokedCount.toLocaleString()}
                      </span>
                    ) : <span className="text-sm text-gray-400">0</span>}
                  </td>
                  <td className="px-5 py-3 whitespace-nowrap text-sm text-gray-600 dark:text-gray-300">{formatAlgorithm(crl.signatureAlgorithm)}</td>
                </tr>
              ))}
              {reportData.crls.items.length === 0 && (
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
              총 {reportData.crls.total.toLocaleString()}개 중{' '}
              {((page - 1) * pageSize + 1).toLocaleString()}-{Math.min(page * pageSize, reportData.crls.total).toLocaleString()}개 표시
            </p>
            <div className="flex items-center gap-1">
              <button
                onClick={() => setPage(p => Math.max(1, p - 1))}
                disabled={page <= 1}
                className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="px-3 text-sm text-gray-600 dark:text-gray-300">{page} / {totalPages}</span>
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

      {/* Detail Dialog */}
      {dialogOpen && selectedCrl && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm" onClick={closeDialog}>
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-4xl max-h-[85vh] flex flex-col" onClick={e => e.stopPropagation()}>
            {/* Dialog Header */}
            <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200 dark:border-gray-700">
              <div className="flex items-center gap-3">
                <div className="p-2 rounded-lg bg-amber-50 dark:bg-amber-900/30">
                  <FileWarning className="w-5 h-5 text-amber-600" />
                </div>
                <div>
                  <div className="flex items-center gap-2">
                    <h2 className="text-lg font-bold text-gray-900 dark:text-white">CRL 상세</h2>
                    {getFlagSvgPath(selectedCrl.countryCode) && (
                      <img src={getFlagSvgPath(selectedCrl.countryCode)} alt={selectedCrl.countryCode}
                        className="w-6 h-4 object-cover rounded shadow-sm border border-gray-300 dark:border-gray-500" />
                    )}
                    <span className="text-sm font-medium text-gray-600 dark:text-gray-300">
                      {selectedCrl.countryCode} - {getCountryName(selectedCrl.countryCode)}
                    </span>
                    <StatusBadge status={selectedCrl.status} />
                  </div>
                  <p className="text-xs text-gray-500 dark:text-gray-400 mt-0.5">Fingerprint: {selectedCrl.fingerprint}</p>
                </div>
              </div>
              <button onClick={closeDialog} className="p-1.5 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors">
                <X className="w-5 h-5" />
              </button>
            </div>

            {/* Dialog Body */}
            <div className="flex-1 overflow-y-auto p-6 space-y-5">
              {detailLoading ? (
                <div className="flex items-center justify-center py-16">
                  <Loader2 className="w-6 h-6 animate-spin text-blue-500" />
                </div>
              ) : detailData ? (
                <>
                  {/* CRL Metadata */}
                  <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-5">
                    <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
                      <FileText className="w-4 h-4 text-blue-500" />
                      CRL 메타데이터
                    </h3>
                    <div className="grid grid-cols-2 gap-4 text-sm">
                      <InfoRow label="Issuer DN" value={detailData.crl.issuerDn} mono />
                      <InfoRow label="서명 알고리즘" value={formatAlgorithm(detailData.crl.signatureAlgorithm)} />
                      <InfoRow label="This Update" value={detailData.crl.thisUpdate} />
                      <InfoRow label="Next Update" value={detailData.crl.nextUpdate || '-'} />
                      <InfoRow label="CRL Number" value={detailData.crl.crlNumber || '-'} />
                      <InfoRow label="폐기 인증서 수" value={detailData.crl.revokedCount.toLocaleString()} highlight />
                    </div>
                  </div>

                  {/* Revoked Certificates Table */}
                  <div>
                    <div className="flex items-center gap-2 mb-3">
                      <AlertTriangle className="w-4 h-4 text-red-500" />
                      <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">폐기 인증서 목록</h3>
                      <span className="px-2 py-0.5 text-xs rounded-full bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400">
                        {detailData.revokedCertificates.total.toLocaleString()}건
                      </span>
                    </div>
                    {detailData.revokedCertificates.total === 0 ? (
                      <div className="flex flex-col items-center text-gray-500 dark:text-gray-400 py-10">
                        <CheckCircle className="w-10 h-10 mb-3 text-green-500 opacity-50" />
                        <p className="text-sm">폐기된 인증서가 없습니다</p>
                      </div>
                    ) : (
                      <div className="max-h-[400px] overflow-y-auto border border-gray-200 dark:border-gray-600 rounded-xl">
                        <table className="w-full">
                          <thead className="bg-gray-50 dark:bg-gray-700/50 sticky top-0">
                            <tr>
                              <th className="px-4 py-2.5 text-center text-xs font-semibold text-gray-500 dark:text-gray-400 w-12">#</th>
                              <th className="px-4 py-2.5 text-left text-xs font-semibold text-gray-500 dark:text-gray-400">Serial Number</th>
                              <th className="px-4 py-2.5 text-left text-xs font-semibold text-gray-500 dark:text-gray-400">폐기 일자</th>
                              <th className="px-4 py-2.5 text-left text-xs font-semibold text-gray-500 dark:text-gray-400">폐기 사유</th>
                            </tr>
                          </thead>
                          <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                            {detailData.revokedCertificates.items.map((rev, i) => (
                              <tr key={i} className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
                                <td className="px-4 py-2 text-center text-xs text-gray-400">{i + 1}</td>
                                <td className="px-4 py-2 font-mono text-sm text-gray-900 dark:text-white">{rev.serialNumber}</td>
                                <td className="px-4 py-2 text-sm text-gray-600 dark:text-gray-400">{rev.revocationDate}</td>
                                <td className="px-4 py-2">
                                  <span className={cn(
                                    'inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium',
                                    rev.revocationReason === 'keyCompromise' || rev.revocationReason === 'cACompromise'
                                      ? 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400'
                                      : 'bg-amber-100 dark:bg-amber-900/30 text-amber-700 dark:text-amber-400',
                                  )}>
                                    {REASON_KO[rev.revocationReason] || rev.revocationReason}
                                  </span>
                                </td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      </div>
                    )}
                  </div>
                </>
              ) : (
                <div className="flex flex-col items-center text-gray-500 dark:text-gray-400 py-16">
                  <AlertCircle className="w-10 h-10 mb-3 opacity-50" />
                  <p className="text-sm">데이터를 불러올 수 없습니다</p>
                </div>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

// ─── Sub Components ──────────────────────────────────────────────────────────

function PageHeader({ onRefresh, onExport, loading }: { onRefresh: () => void; onExport: () => void; loading: boolean }) {
  return (
    <div className="mb-2">
      <div className="flex items-center gap-4">
        <div className="p-3 rounded-xl bg-gradient-to-br from-amber-500 to-orange-600 shadow-lg">
          <FileWarning className="w-7 h-7 text-white" />
        </div>
        <div className="flex-1">
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">CRL 보고서</h1>
          <p className="text-sm text-gray-500 dark:text-gray-400">
            Certificate Revocation List 분석 및 폐기 인증서 현황
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

function ValidityBar({ validCount, expiredCount, total }: { validCount: number; expiredCount: number; total: number }) {
  if (total === 0) return null;

  const segments = [
    { key: 'VALID', label: 'Valid', count: validCount, color: 'bg-green-500', icon: <CheckCircle className="w-3 h-3" /> },
    { key: 'EXPIRED', label: 'Expired', count: expiredCount, color: 'bg-red-500', icon: <XCircle className="w-3 h-3" /> },
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

function StatusBadge({ status }: { status: string }) {
  const isValid = status === 'VALID';
  return (
    <span className={cn(
      'inline-flex items-center gap-1 px-2 py-1 rounded-full text-xs font-medium',
      isValid
        ? 'bg-green-100 dark:bg-green-900/30 text-green-600 dark:text-green-400'
        : 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400',
    )}>
      {isValid ? <CheckCircle className="w-3 h-3" /> : <XCircle className="w-3 h-3" />}
      {isValid ? '유효' : '만료'}
    </span>
  );
}

function InfoRow({ label, value, mono, highlight }: { label: string; value: string; mono?: boolean; highlight?: boolean }) {
  return (
    <div>
      <p className="text-xs font-medium text-gray-500 dark:text-gray-400 mb-0.5">{label}</p>
      <p className={cn(
        'text-gray-900 dark:text-white',
        mono && 'font-mono break-all text-xs',
        highlight && 'font-semibold text-red-600 dark:text-red-400',
      )}>{value}</p>
    </div>
  );
}
