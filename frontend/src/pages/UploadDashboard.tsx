import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import {
  BarChart3,
  Upload,
  FileText,
  Clock,
  Database,
  Globe,
  Loader2,
  RefreshCw,
  Shield,
  Key,
  CheckCircle,
  XCircle,
  AlertTriangle,
  HardDrive,
  TrendingUp,
  Award,
  AlertCircle,
  Info,
  PackageOpen,
} from 'lucide-react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from 'recharts';
import { uploadApi } from '@/services/api';
import { uploadHistoryApi } from '@/services/pkdApi';
import type { UploadStatisticsOverview, UploadChange } from '@/types';
import { cn } from '@/utils/cn';
import { formatDate } from '@/utils/dateFormat';
import { getFlagSvgPath } from '@/utils/countryCode';
import { getCountryName } from '@/utils/countryNames';
import { GlossaryTerm, getGlossaryTooltip } from '@/components/common';

interface ValidationReasonEntry {
  status: string;
  reason: string;
  countryCode: string;
  count: number;
}

interface ExpiredEntry {
  countryCode: string;
  expireYear: number;
  count: number;
}

interface RevokedEntry {
  countryCode: string;
  count: number;
}

type DetailDialogType = 'INVALID' | 'PENDING' | 'EXPIRED' | 'REVOKED' | null;

export function UploadDashboard() {
  const { t } = useTranslation(['upload', 'common']);
  const [stats, setStats] = useState<UploadStatisticsOverview | null>(null);
  const [recentChanges, setRecentChanges] = useState<UploadChange[]>([]);
  const [loading, setLoading] = useState(true);
  const [reasonDialogOpen, setReasonDialogOpen] = useState<DetailDialogType>(null);
  const [validationReasons, setValidationReasons] = useState<ValidationReasonEntry[]>([]);
  const [expiredData, setExpiredData] = useState<ExpiredEntry[]>([]);
  const [revokedData, setRevokedData] = useState<RevokedEntry[]>([]);
  const [reasonsLoading, setReasonsLoading] = useState(false);
  const [dataFetched, setDataFetched] = useState(false);

  useEffect(() => {
    fetchDashboardData();
  }, []);

  const fetchDashboardData = async () => {
    setLoading(true);
    try {
      const [statsResponse, changesResponse] = await Promise.all([
        uploadApi.getStatistics(),
        uploadApi.getChanges(10), // Get 10 most recent changes for chart
      ]);
      setStats(statsResponse.data);
      setRecentChanges(changesResponse.data.changes || []);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch upload dashboard data:', error);
    } finally {
      setLoading(false);
    }
  };

  const openDetailDialog = async (type: DetailDialogType) => {
    setReasonDialogOpen(type);
    if (!dataFetched) {
      setReasonsLoading(true);
      try {
        const resp = await uploadHistoryApi.getValidationReasons();
        setValidationReasons(resp.data.reasons || []);
        setExpiredData(resp.data.expired || []);
        setRevokedData(resp.data.revoked || []);
        setDataFetched(true);
      } catch (e) {
        if (import.meta.env.DEV) console.error('Failed to fetch validation detail:', e);
      } finally {
        setReasonsLoading(false);
      }
    }
  };

  const translateReason = (reason: string): string => {
    if (reason.includes('Trust chain signature verification failed')) return t('upload:dashboard.sigVerifyFailed');
    if (reason.includes('Chain broken') || reason.includes('Failed to build trust chain')) return t('upload:dashboard.trustChainBroken');
    if (reason.includes('CSCA not found')) return t('upload:dashboard.cscaNotRegistered');
    if (reason.includes('not yet valid')) return t('upload:dashboard.notYetValid');
    if (reason.includes('certificates expired')) return t('upload:dashboard.expiredButSigValid');
    if (reason === 'Trust chain validation failed') return t('upload:dashboard.trustChainValidationFailed');
    return reason;
  };

  // Transform data for timeline chart
  const chartData = recentChanges
    .map((change) => ({
      date: formatDate(change.uploadTime),
      fullDate: formatDate(change.uploadTime),
      DSC: change.counts.dsc,
      CSCA: change.counts.csca,
      CRL: change.counts.crl,
      DSC_NC: change.counts.dscNc,
      ML: change.counts.ml,
      collection: change.collectionNumber,
      fileName: change.fileName,
    }))
    .reverse(); // Chronological order (oldest to newest)

  // Calculate percentages
  const totalCerts = stats?.totalCertificates || 0;
  const cscaPercent = totalCerts > 0 ? ((stats?.cscaCount || 0) / totalCerts * 100).toFixed(1) : '0';
  const dscPercent = totalCerts > 0 ? ((stats?.dscCount || 0) / totalCerts * 100).toFixed(1) : '0';
  const dscNcPercent = totalCerts > 0 ? ((stats?.dscNcCount || 0) / totalCerts * 100).toFixed(1) : '0';

  const totalValidation = (stats?.validation?.validCount || 0) + (stats?.validation?.invalidCount || 0) + (stats?.validation?.pendingCount || 0);
  const validPercent = totalValidation > 0 ? ((stats?.validation?.validCount || 0) / totalValidation * 100).toFixed(1) : '0';

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-violet-500 to-purple-600 shadow-lg">
            <BarChart3 className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('upload:dashboard.pkdStatsDashboard')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('upload:dashboard.subtitle')}
            </p>
          </div>
          {/* Quick Actions */}
          <div className="flex gap-2">
            <Link
              to="/upload"
              className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl text-sm font-medium text-white bg-gradient-to-r from-violet-500 to-purple-500 hover:from-violet-600 hover:to-purple-600 transition-all duration-200 shadow-md hover:shadow-lg"
            >
              <Upload className="w-4 h-4" />
              {t('upload:fileUpload.title_short')}
            </Link>
            <Link
              to="/upload-history"
              className="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-medium transition-all duration-200 border text-gray-700 dark:text-gray-300 border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700"
            >
              <Clock className="w-4 h-4" />
              {t('upload:history.title')}
            </Link>
            <button
              onClick={fetchDashboardData}
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
      ) : (
        <>
          {/* Overview Stats - Large Cards */}
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-4 mb-6">
            {/* Countries */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-cyan-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">{t('upload:dashboard.registeredCountries')}</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {(stats?.countriesCount ?? 0).toLocaleString()}
                  </p>
                  <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                    {t('upload:dashboard.icaoMemberStates')}
                  </p>
                </div>
                <div className="p-3 rounded-xl bg-cyan-50 dark:bg-cyan-900/30">
                  <Globe className="w-8 h-8 text-cyan-500" />
                </div>
              </div>
            </div>

            {/* Total Certificates */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-blue-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">{ t('upload:dashboard.totalCertificates') }</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {(stats?.totalCertificates ?? 0).toLocaleString()}
                  </p>
                  <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                    {t('upload:dashboard.countriesSuffix', { num: stats?.countriesCount || 0 })}
                  </p>
                </div>
                <div className="p-3 rounded-xl bg-blue-50 dark:bg-blue-900/30">
                  <HardDrive className="w-8 h-8 text-blue-500" />
                </div>
              </div>
            </div>

            {/* Total Uploads */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-green-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">{t('upload:dashboard.uploadStatus')}</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {((stats?.successfulUploads ?? 0) + (stats?.failedUploads ?? 0)).toLocaleString()}
                  </p>
                  <div className="flex items-center gap-2 mt-1">
                    <span className="text-xs text-green-600 dark:text-green-400">
                      {t('upload:dashboard.successLabel', { num: stats?.successfulUploads ?? 0 })}
                    </span>
                    <span className="text-xs text-gray-300 dark:text-gray-600">|</span>
                    <span className="text-xs text-red-600 dark:text-red-400">
                      {t('upload:dashboard.failedLabel', { num: stats?.failedUploads ?? 0 })}
                    </span>
                  </div>
                </div>
                <div className="p-3 rounded-xl bg-green-50 dark:bg-green-900/30">
                  <Upload className="w-8 h-8 text-green-500" />
                </div>
              </div>
            </div>

            {/* Validation Rate */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-violet-500">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-medium text-gray-500 dark:text-gray-400">{ t('upload:dashboard.successRate') }</p>
                  <p className="text-3xl font-bold text-gray-900 dark:text-white mt-1">
                    {validPercent}%
                  </p>
                  <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">
                    {t('upload:dashboard.validCountSuffix', { num: (stats?.validation?.validCount || 0).toLocaleString() })}
                  </p>
                </div>
                <div className="p-3 rounded-xl bg-violet-50 dark:bg-violet-900/30">
                  <Award className="w-8 h-8 text-violet-500" />
                </div>
              </div>
            </div>
          </div>

          {/* Master List Extraction Statistics (v2.1.1) */}
          {(stats?.cscaExtractedFromMl || stats?.cscaDuplicates) && (
            <div className="bg-gradient-to-br from-indigo-50 to-purple-50 dark:from-indigo-900/20 dark:to-purple-900/20 rounded-2xl shadow-lg p-4 mb-6 border border-indigo-200 dark:border-indigo-800">
              <div className="flex items-center gap-2 mb-4">
                <Database className="w-5 h-5 text-indigo-600 dark:text-indigo-400" />
                <h3 className="text-lg font-bold text-gray-900 dark:text-white">{t('upload:dashboard.mlExtractionStats')}</h3>
                <span className="px-2 py-0.5 text-xs font-medium rounded-full bg-indigo-100 dark:bg-indigo-900/50 text-indigo-700 dark:text-indigo-300">
                  v2.1.1
                </span>
              </div>
              <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                {/* Total Extracted */}
                <div className="bg-white dark:bg-gray-800 rounded-xl p-4 border border-indigo-200 dark:border-indigo-700">
                  <div className="flex items-center gap-2 mb-2">
                    <TrendingUp className="w-5 h-5 text-indigo-600 dark:text-indigo-400" />
                    <span className="text-sm font-semibold text-indigo-700 dark:text-indigo-300">{t('upload:dashboard.extractedCerts')}</span>
                  </div>
                  <p className="text-3xl font-bold text-indigo-800 dark:text-indigo-200">
                    {(stats?.cscaExtractedFromMl ?? 0).toLocaleString()}
                  </p>
                  <p className="text-xs text-indigo-600 dark:text-indigo-400 mt-1" title={`${getGlossaryTooltip('MLSC')} / ${getGlossaryTooltip('CSCA')} / ${getGlossaryTooltip('Link Certificate')}`}>MLSC + CSCA + LC</p>
                </div>

                {/* Duplicates Detected */}
                <div className="bg-white dark:bg-gray-800 rounded-xl p-4 border border-amber-200 dark:border-amber-700">
                  <div className="flex items-center gap-2 mb-2">
                    <AlertCircle className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                    <span className="text-sm font-semibold text-amber-700 dark:text-amber-300">{t('upload:dashboard.duplicatesDetected')}</span>
                  </div>
                  <p className="text-3xl font-bold text-amber-800 dark:text-amber-200">
                    {(stats?.cscaDuplicates ?? 0).toLocaleString()}
                  </p>
                  <p className="text-xs text-amber-600 dark:text-amber-400 mt-1">{t('upload:dashboard.existingDuplicate')}</p>
                </div>

                {/* Duplicate Rate */}
                <div className="bg-white dark:bg-gray-800 rounded-xl p-4 border border-violet-200 dark:border-violet-700">
                  <div className="flex items-center gap-2 mb-2">
                    <Award className="w-5 h-5 text-violet-600 dark:text-violet-400" />
                    <span className="text-sm font-semibold text-violet-700 dark:text-violet-300">{t('upload:dashboard.duplicateRate')}</span>
                  </div>
                  <p className="text-3xl font-bold text-violet-800 dark:text-violet-200">
                    {stats.cscaExtractedFromMl && stats.cscaExtractedFromMl > 0
                      ? ((stats.cscaDuplicates ?? 0) / stats.cscaExtractedFromMl * 100).toFixed(1)
                      : '0.0'}%
                  </p>
                  <p className="text-xs text-violet-600 dark:text-violet-400 mt-1">
                    {stats.cscaExtractedFromMl && stats.cscaExtractedFromMl > 0
                      ? t('upload:dashboard.percentNew', { pct: ((stats.cscaExtractedFromMl - (stats.cscaDuplicates ?? 0)) / stats.cscaExtractedFromMl * 100).toFixed(1) })
                      : 'N/A'}
                  </p>
                </div>
              </div>
            </div>
          )}

          {/* Certificate Breakdown + Source Statistics - 2 column layout */}
          <div className="grid grid-cols-1 lg:grid-cols-3 gap-6 mb-6">
            {/* Certificate Type Breakdown - 2/3 width */}
            <div className="lg:col-span-2 bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 h-full">
              <div className="flex items-center gap-2 mb-5">
                <Shield className="w-5 h-5 text-violet-500" />
                <h3 className="text-lg font-bold text-gray-900 dark:text-white">{t('upload:dashboard.certTypeStatus')}</h3>
              </div>
              <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-5 gap-3">
                {/* CSCA */}
                <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-green-50 to-emerald-100 dark:from-green-900/20 dark:to-emerald-900/30 p-4 border border-green-200 dark:border-green-800">
                  <div className="flex items-center justify-between mb-2">
                    <div className="flex items-center gap-2">
                      <Shield className="w-5 h-5 text-green-600 dark:text-green-400" />
                      <GlossaryTerm term="CSCA" className="text-sm font-semibold text-green-700 dark:text-green-300" />
                    </div>
                    <span className="text-xs text-green-600 dark:text-green-400">{cscaPercent}%</span>
                  </div>
                  <p className="text-2xl font-bold text-green-800 dark:text-green-200 mb-3">{(stats?.cscaCount ?? 0).toLocaleString()}</p>
                  <div className="mb-2">
                    <div className="flex justify-between text-xs mb-1">
                      <span className="text-green-700 dark:text-green-300">Self-signed</span>
                      <span className="font-medium text-green-800 dark:text-green-200">
                        {(stats?.cscaBreakdown?.selfSigned ?? 0).toLocaleString()}
                        {stats?.cscaBreakdown?.total ? ` (${((stats.cscaBreakdown.selfSigned / stats.cscaBreakdown.total) * 100).toFixed(0)}%)` : ''}
                      </span>
                    </div>
                    <div className="h-1.5 bg-green-200 dark:bg-green-900/40 rounded-full overflow-hidden">
                      <div
                        className="h-full bg-green-600 dark:bg-green-500 rounded-full transition-all duration-500"
                        style={{ width: stats?.cscaBreakdown?.total ? `${(stats.cscaBreakdown.selfSigned / stats.cscaBreakdown.total) * 100}%` : '0%' }}
                      />
                    </div>
                  </div>
                  <div className="mb-1">
                    <div className="flex justify-between text-xs mb-1">
                      <GlossaryTerm term="Link Certificate" label="Link Cert" className="text-green-700 dark:text-green-300" />
                      <span className="font-medium text-green-800 dark:text-green-200">
                        {(stats?.cscaBreakdown?.linkCertificates ?? 0).toLocaleString()}
                        {stats?.cscaBreakdown?.total ? ` (${((stats.cscaBreakdown.linkCertificates / stats.cscaBreakdown.total) * 100).toFixed(0)}%)` : ''}
                      </span>
                    </div>
                    <div className="h-1.5 bg-green-200 dark:bg-green-900/40 rounded-full overflow-hidden">
                      <div
                        className="h-full bg-emerald-500 dark:bg-emerald-400 rounded-full transition-all duration-500"
                        style={{ width: stats?.cscaBreakdown?.total ? `${(stats.cscaBreakdown.linkCertificates / stats.cscaBreakdown.total) * 100}%` : '0%' }}
                      />
                    </div>
                  </div>
                  <div className="absolute -right-2 -bottom-2 opacity-10">
                    <Shield className="w-16 h-16 text-green-600" />
                  </div>
                </div>

                {/* MLSC */}
                <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-indigo-50 to-blue-100 dark:from-indigo-900/20 dark:to-blue-900/30 p-4 border border-indigo-200 dark:border-indigo-800">
                  <div className="flex items-center gap-2 mb-2">
                    <FileText className="w-5 h-5 text-indigo-600 dark:text-indigo-400" />
                    <GlossaryTerm term="MLSC" className="text-sm font-semibold text-indigo-700 dark:text-indigo-300" />
                  </div>
                  <p className="text-2xl font-bold text-indigo-800 dark:text-indigo-200">{(stats?.mlscCount ?? 0).toLocaleString()}</p>
                  <p className="text-xs text-indigo-600 dark:text-indigo-400 mt-1">ML Signer</p>
                  <div className="absolute -right-2 -bottom-2 opacity-10">
                    <FileText className="w-16 h-16 text-indigo-600" />
                  </div>
                </div>

                {/* DSC */}
                <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-violet-50 to-purple-100 dark:from-violet-900/20 dark:to-purple-900/30 p-4 border border-violet-200 dark:border-violet-800">
                  <div className="flex items-center gap-2 mb-2">
                    <Key className="w-5 h-5 text-violet-600 dark:text-violet-400" />
                    <GlossaryTerm term="DSC" className="text-sm font-semibold text-violet-700 dark:text-violet-300" />
                  </div>
                  <p className="text-2xl font-bold text-violet-800 dark:text-violet-200">{(stats?.dscCount ?? 0).toLocaleString()}</p>
                  <p className="text-xs text-violet-600 dark:text-violet-400 mt-1">{dscPercent}%</p>
                  <div className="absolute -right-2 -bottom-2 opacity-10">
                    <Key className="w-16 h-16 text-violet-600" />
                  </div>
                </div>

                {/* DSC_NC */}
                <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-amber-50 to-orange-100 dark:from-amber-900/20 dark:to-orange-900/30 p-4 border border-amber-200 dark:border-amber-800">
                  <div className="flex items-center gap-2 mb-2">
                    <AlertTriangle className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                    <GlossaryTerm term="DSC_NC" className="text-sm font-semibold text-amber-700 dark:text-amber-300" />
                  </div>
                  <p className="text-2xl font-bold text-amber-800 dark:text-amber-200">{(stats?.dscNcCount ?? 0).toLocaleString()}</p>
                  <p className="text-xs text-amber-600 dark:text-amber-400 mt-1">{dscNcPercent}%</p>
                  <div className="absolute -right-2 -bottom-2 opacity-10">
                    <AlertTriangle className="w-16 h-16 text-amber-600" />
                  </div>
                </div>

                {/* CRL */}
                <div className="relative overflow-hidden rounded-xl bg-gradient-to-br from-orange-50 to-red-100 dark:from-orange-900/20 dark:to-red-900/30 p-4 border border-orange-200 dark:border-orange-800">
                  <div className="flex items-center gap-2 mb-2">
                    <FileText className="w-5 h-5 text-orange-600 dark:text-orange-400" />
                    <GlossaryTerm term="CRL" className="text-sm font-semibold text-orange-700 dark:text-orange-300" />
                  </div>
                  <p className="text-2xl font-bold text-orange-800 dark:text-orange-200">{(stats?.crlCount ?? 0).toLocaleString()}</p>
                  <p className="text-xs text-orange-600 dark:text-orange-400 mt-1">{t('upload:dashboard.revocationList')}</p>
                  <div className="absolute -right-2 -bottom-2 opacity-10">
                    <FileText className="w-16 h-16 text-orange-600" />
                  </div>
                </div>
              </div>
            </div>

            {/* Source Statistics - 1/3 width */}
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 h-full">
              <div className="flex items-center gap-2 mb-5">
                <PackageOpen className="w-5 h-5 text-amber-500" />
                <h3 className="text-lg font-bold text-gray-900 dark:text-white">{t('upload:dashboard.sourceStatus')}</h3>
              </div>
              {(() => {
                const bySource = stats?.bySource;
                if (!bySource || Object.keys(bySource).length === 0) {
                  return (
                    <div className="flex flex-col items-center justify-center py-8 text-gray-400 dark:text-gray-500">
                      <PackageOpen className="w-10 h-10 mb-2" />
                      <p className="text-sm">{t('upload:dashboard.noSourceData')}</p>
                    </div>
                  );
                }
                const sourceLabels: Record<string, { label: string; color: string; bg: string }> = {
                  LDIF_PARSED: { label: t('upload:dashboard.ldifUpload'), color: '#3B82F6', bg: 'bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800' },
                  ML_PARSED: { label: 'Master List', color: '#8B5CF6', bg: 'bg-purple-50 dark:bg-purple-900/20 border-purple-200 dark:border-purple-800' },
                  FILE_UPLOAD: { label: t('upload:dashboard.fileUpload'), color: '#22C55E', bg: 'bg-green-50 dark:bg-green-900/20 border-green-200 dark:border-green-800' },
                  PA_EXTRACTED: { label: t('upload:dashboard.paExtracted'), color: '#F59E0B', bg: 'bg-amber-50 dark:bg-amber-900/20 border-amber-200 dark:border-amber-800' },
                  DL_PARSED: { label: t('upload:dashboard.deviationList'), color: '#EF4444', bg: 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800' },
                };
                const total = Object.values(bySource).reduce((a, b) => a + b, 0);
                const sorted = Object.entries(bySource).sort(([, a], [, b]) => b - a);
                const maxCount = sorted[0]?.[1] || 1;

                return (
                  <div className="space-y-2.5">
                    {sorted.map(([sourceType, count]) => {
                      const info = sourceLabels[sourceType] || { label: sourceType, color: '#6B7280', bg: 'bg-gray-50 dark:bg-gray-700 border-gray-200 dark:border-gray-600' };
                      const pct = ((count / total) * 100).toFixed(1);
                      const barWidth = Math.max(6, (count / maxCount) * 100);

                      return (
                        <div key={sourceType} className={cn('rounded-lg border p-2.5', info.bg)}>
                          <div className="flex items-center justify-between mb-1.5">
                            <span className="text-xs font-semibold text-gray-700 dark:text-gray-300" title={sourceType === 'ML_PARSED' ? getGlossaryTooltip('Master List') : undefined}>
                              {info.label}
                            </span>
                            <div className="flex items-center gap-2">
                              <span className="text-xs font-bold text-gray-800 dark:text-gray-200">
                                {count.toLocaleString()}
                              </span>
                              <span className="text-xs text-gray-500 dark:text-gray-400 w-10 text-right">
                                {pct}%
                              </span>
                            </div>
                          </div>
                          <div className="h-2 rounded-full overflow-hidden bg-white/60 dark:bg-gray-700/60">
                            <div
                              className="h-full rounded-full transition-all duration-500"
                              style={{ width: `${barWidth}%`, background: info.color }}
                            />
                          </div>
                        </div>
                      );
                    })}
                    <div className="pt-2 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
                      <span className="text-xs text-gray-500 dark:text-gray-400">{ t('upload:dashboard.totalCertificates') }</span>
                      <span className="text-sm font-bold text-gray-800 dark:text-gray-200">{t('upload:dashboard.itemCount', { num: total.toLocaleString() })}</span>
                    </div>
                  </div>
                );
              })()}
            </div>
          </div>

          {/* Validation Statistics */}
          {stats?.validation && (
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-5">
              {/* Validation Status */}
              <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
                <div className="flex items-center gap-2 mb-5">
                  <CheckCircle className="w-5 h-5 text-green-500" />
                  <h3 className="text-lg font-bold text-gray-900 dark:text-white">{t('upload:dashboard.dscTrustChainStatus')}</h3>
                </div>
                <div className="space-y-4">
                  {/* Valid */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <CheckCircle className="w-4 h-4 text-green-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">{t('common:status.valid')}</span>
                      <div className="group relative">
                        <Info className="w-3.5 h-3.5 text-gray-400 hover:text-gray-600 dark:hover:text-gray-300 cursor-help" />
                        <div className="absolute left-0 bottom-full mb-2 hidden group-hover:block w-64 p-3 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg shadow-lg z-10">
                          <div className="space-y-1">
                            <div className="font-semibold mb-1">{t('upload:dashboard.trustChainSuccess')}</div>
                            <div>{t('upload:dashboard.cscaFound')}</div>
                            <div>{t('upload:dashboard.dscSignedByCsca')}</div>
                            <div>{t('upload:dashboard.signatureValid')}</div>
                            <div>{t('upload:dashboard.withinValidityPeriod')}</div>
                          </div>
                          <div className="absolute left-4 top-full w-0 h-0 border-l-4 border-r-4 border-t-4 border-transparent border-t-gray-900 dark:border-t-gray-700"></div>
                        </div>
                      </div>
                    </div>
                    <div className="flex-1">
                      {(() => {
                        const pct = Number(validPercent);
                        const count = (stats.validation?.validCount ?? 0).toLocaleString();
                        const wide = pct >= 10;
                        return (
                          <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg relative">
                            <div
                              className={`h-full bg-gradient-to-r from-green-400 to-emerald-500 rounded-lg${wide ? ' flex items-center justify-end pr-3' : ''}`}
                              style={{ width: `${Math.max(pct, 2)}%` }}
                            >
                              {wide && <span className="text-xs font-bold text-white">{count}</span>}
                            </div>
                            {!wide && <span className="absolute top-0 h-full flex items-center text-xs font-bold text-green-600 dark:text-green-400 pl-1.5" style={{ left: `${Math.max(pct, 2)}%` }}>{count}</span>}
                          </div>
                        );
                      })()}
                    </div>
                    <span className="text-sm font-semibold text-green-600 dark:text-green-400 w-16 text-right">{validPercent}%</span>
                  </div>

                  {/* Invalid */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <XCircle className="w-4 h-4 text-red-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">{t('common:status.invalid')}</span>
                      <div className="group relative">
                        <Info className="w-3.5 h-3.5 text-gray-400 hover:text-gray-600 dark:hover:text-gray-300 cursor-help" />
                        <div className="absolute left-0 bottom-full mb-2 hidden group-hover:block w-64 p-3 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg shadow-lg z-10">
                          <div className="space-y-1">
                            <div className="font-semibold mb-1">{ t('upload:dashboard.trustChainValidationFailed') }</div>
                            <div>{t('upload:dashboard.cscaFound')}</div>
                            <div className="text-red-400">{t('upload:dashboard.signatureFailedOrExpired')}</div>
                          </div>
                          <div className="absolute left-4 top-full w-0 h-0 border-l-4 border-r-4 border-t-4 border-transparent border-t-gray-900 dark:border-t-gray-700"></div>
                        </div>
                      </div>
                    </div>
                    <div className="flex-1">
                      {(() => {
                        const pct = totalValidation > 0 ? (stats.validation?.invalidCount || 0) / totalValidation * 100 : 0;
                        const count = (stats.validation?.invalidCount ?? 0).toLocaleString();
                        const wide = pct >= 10;
                        return (
                          <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg relative">
                            <div
                              className={`h-full bg-gradient-to-r from-red-400 to-rose-500 rounded-lg${wide ? ' flex items-center justify-end pr-3' : ''}`}
                              style={{ width: `${Math.max(pct, 2)}%` }}
                            >
                              {wide && <span className="text-xs font-bold text-white">{count}</span>}
                            </div>
                            {!wide && <span className="absolute top-0 h-full flex items-center text-xs font-bold text-red-600 dark:text-red-400 pl-1.5" style={{ left: `${Math.max(pct, 2)}%` }}>{count}</span>}
                          </div>
                        );
                      })()}
                    </div>
                    <span className="text-sm font-semibold text-red-600 dark:text-red-400 w-16 text-right">
                      {totalValidation > 0 ? ((stats.validation?.invalidCount || 0) / totalValidation * 100).toFixed(1) : '0'}%
                    </span>
                  </div>

                  {/* Pending */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <Clock className="w-4 h-4 text-yellow-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">{ t('pa:steps.cscaNotFound') }</span>
                      <div className="group relative">
                        <Info className="w-3.5 h-3.5 text-gray-400 hover:text-gray-600 dark:hover:text-gray-300 cursor-help" />
                        <div className="absolute left-0 bottom-full mb-2 hidden group-hover:block w-72 p-3 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg shadow-lg z-10">
                          <div className="space-y-1">
                            <div className="font-semibold mb-1">{t('upload:dashboard.cscaNotFoundTitle')}</div>
                            <div>{t('upload:dashboard.cscaNotFoundDesc')}</div>
                            <div className="text-yellow-400">{t('upload:dashboard.cscaNotFoundDetail')}</div>
                          </div>
                          <div className="absolute left-4 top-full w-0 h-0 border-l-4 border-r-4 border-t-4 border-transparent border-t-gray-900 dark:border-t-gray-700"></div>
                        </div>
                      </div>
                    </div>
                    <div className="flex-1">
                      {(() => {
                        const pct = totalValidation > 0 ? (stats.validation?.pendingCount || 0) / totalValidation * 100 : 0;
                        const count = (stats.validation?.pendingCount ?? 0).toLocaleString();
                        const wide = pct >= 10;
                        return (
                          <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg relative">
                            <div
                              className={`h-full bg-gradient-to-r from-yellow-400 to-amber-500 rounded-lg${wide ? ' flex items-center justify-end pr-3' : ''}`}
                              style={{ width: `${Math.max(pct, 2)}%` }}
                            >
                              {wide && <span className="text-xs font-bold text-white">{count}</span>}
                            </div>
                            {!wide && <span className="absolute top-0 h-full flex items-center text-xs font-bold text-yellow-600 dark:text-yellow-400 pl-1.5" style={{ left: `${Math.max(pct, 2)}%` }}>{count}</span>}
                          </div>
                        );
                      })()}
                    </div>
                    <span className="text-sm font-semibold text-yellow-600 dark:text-yellow-400 w-16 text-right">
                      {totalValidation > 0 ? ((stats.validation?.pendingCount || 0) / totalValidation * 100).toFixed(1) : '0'}%
                    </span>
                  </div>

                  {/* Error */}
                  <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2 w-24">
                      <AlertCircle className="w-4 h-4 text-gray-500" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">{ t('sync:dashboard.error') }</span>
                      <div className="group relative">
                        <Info className="w-3.5 h-3.5 text-gray-400 hover:text-gray-600 dark:hover:text-gray-300 cursor-help" />
                        <div className="absolute left-0 bottom-full mb-2 hidden group-hover:block w-56 p-3 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg shadow-lg z-10">
                          <div className="space-y-1">
                            <div className="font-semibold mb-1">{t('upload:dashboard.technicalErrorTitle')}</div>
                            <div>{t('upload:dashboard.technicalErrorDesc')}</div>
                          </div>
                          <div className="absolute left-4 top-full w-0 h-0 border-l-4 border-r-4 border-t-4 border-transparent border-t-gray-900 dark:border-t-gray-700"></div>
                        </div>
                      </div>
                    </div>
                    <div className="flex-1">
                      {(() => {
                        const pct = totalValidation > 0 ? (stats.validation?.errorCount || 0) / totalValidation * 100 : 0;
                        const count = (stats.validation?.errorCount ?? 0).toLocaleString();
                        const wide = pct >= 10;
                        return (
                          <div className="h-8 bg-gray-100 dark:bg-gray-700 rounded-lg relative">
                            <div
                              className={`h-full bg-gradient-to-r from-gray-400 to-gray-500 rounded-lg${wide ? ' flex items-center justify-end pr-3' : ''}`}
                              style={{ width: `${Math.max(pct, 2)}%` }}
                            >
                              {wide && <span className="text-xs font-bold text-white">{count}</span>}
                            </div>
                            {!wide && <span className="absolute top-0 h-full flex items-center text-xs font-bold text-gray-600 dark:text-gray-400 pl-1.5" style={{ left: `${Math.max(pct, 2)}%` }}>{count}</span>}
                          </div>
                        );
                      })()}
                    </div>
                    <span className="text-sm font-semibold text-gray-600 dark:text-gray-400 w-16 text-right">
                      {totalValidation > 0 ? ((stats.validation?.errorCount || 0) / totalValidation * 100).toFixed(1) : '0'}%
                    </span>
                  </div>
                </div>
              </div>

              {/* Trust Chain Validation */}
              <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
                <div className="flex items-center gap-2 mb-5">
                  <TrendingUp className="w-5 h-5 text-violet-500" />
                  <h3 className="text-lg font-bold text-gray-900 dark:text-white">{ t('pa:steps.step5') }</h3>
                </div>
                <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
                  <div className="p-4 rounded-xl bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800">
                    <div className="flex items-center gap-2 mb-2">
                      <CheckCircle className="w-5 h-5 text-green-600 dark:text-green-400" />
                      <span className="text-sm font-medium text-green-700 dark:text-green-300">{ t('pa:result.passed') }</span>
                    </div>
                    <p className="text-2xl font-bold text-green-800 dark:text-green-200">{(stats.validation?.trustChainValidCount ?? 0).toLocaleString()}</p>
                    {(stats.validation?.expiredValidCount ?? 0) > 0 && (
                      <p className="text-xs text-amber-600 dark:text-amber-400 mt-1">{t('upload:dashboard.expiredValidPrefix', { num: stats.validation!.expiredValidCount.toLocaleString() })}</p>
                    )}
                  </div>
                  <div className="p-4 rounded-xl bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
                    <div className="flex items-center gap-2 mb-2">
                      <XCircle className="w-5 h-5 text-red-600 dark:text-red-400" />
                      <span className="text-sm font-medium text-red-700 dark:text-red-300">{ t('pa:result.failed') }</span>
                    </div>
                    <p className="text-2xl font-bold text-red-800 dark:text-red-200">{(stats.validation?.invalidCount ?? 0).toLocaleString()}</p>
                    {(stats.validation?.invalidCount ?? 0) > 0 && (
                      <button onClick={() => openDetailDialog('INVALID')} className="text-xs text-red-500 hover:text-red-700 dark:text-red-400 dark:hover:text-red-300 underline mt-1">
                        {t('upload:dashboard.viewDetail')}
                      </button>
                    )}
                  </div>
                  <div className="p-4 rounded-xl bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800">
                    <div className="flex items-center gap-2 mb-2">
                      <AlertTriangle className="w-5 h-5 text-amber-600 dark:text-amber-400" />
                      <span className="text-sm font-medium text-amber-700 dark:text-amber-300">{t('upload:dashboard.cscaNotFound')}</span>
                    </div>
                    <p className="text-2xl font-bold text-amber-800 dark:text-amber-200">{(stats.validation?.cscaNotFoundCount ?? 0).toLocaleString()}</p>
                    {(stats.validation?.cscaNotFoundCount ?? 0) > 0 && (
                      <button onClick={() => openDetailDialog('PENDING')} className="text-xs text-amber-500 hover:text-amber-700 dark:text-amber-400 dark:hover:text-amber-300 underline mt-1">
                        {t('upload:dashboard.viewDetail')}
                      </button>
                    )}
                  </div>
                  <div className="p-4 rounded-xl bg-orange-50 dark:bg-orange-900/20 border border-orange-200 dark:border-orange-800">
                    <div className="flex items-center gap-2 mb-2">
                      <Clock className="w-5 h-5 text-orange-600 dark:text-orange-400" />
                      <span className="text-sm font-medium text-orange-700 dark:text-orange-300">{t('upload:dashboard.expired')}</span>
                    </div>
                    <p className="text-2xl font-bold text-orange-800 dark:text-orange-200">{(stats.validation?.expiredCount ?? 0).toLocaleString()}</p>
                    {(stats.validation?.expiredCount ?? 0) > 0 && (
                      <button onClick={() => openDetailDialog('EXPIRED')} className="text-xs text-orange-500 hover:text-orange-700 dark:text-orange-400 dark:hover:text-orange-300 underline mt-1">
                        {t('upload:dashboard.viewDetail')}
                      </button>
                    )}
                  </div>
                  <div className="p-4 rounded-xl bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-600">
                    <div className="flex items-center gap-2 mb-2">
                      <XCircle className="w-5 h-5 text-gray-600 dark:text-gray-400" />
                      <span className="text-sm font-medium text-gray-700 dark:text-gray-300">{t('upload:dashboard.revoked')}</span>
                    </div>
                    <p className="text-2xl font-bold text-gray-800 dark:text-gray-200">{(stats.validation?.revokedCount ?? 0).toLocaleString()}</p>
                    {(stats.validation?.revokedCount ?? 0) > 0 && (
                      <button onClick={() => openDetailDialog('REVOKED')} className="text-xs text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-300 underline mt-1">
                        {t('upload:dashboard.viewDetail')}
                      </button>
                    )}
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* Timeline Chart Section */}
          {chartData.length > 0 && (
            <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
              <div className="flex items-center gap-2 mb-6">
                <TrendingUp className="w-5 h-5 text-violet-500" />
                <h3 className="text-lg font-bold text-gray-900 dark:text-white">{t('upload:dashboard.certTrend')}</h3>
                <span className="text-sm text-gray-500 dark:text-gray-400">
                  {t('upload:dashboard.recentUploads', { num: chartData.length })}
                </span>
              </div>
              <ResponsiveContainer width="100%" height={350}>
                <LineChart
                  data={chartData}
                  margin={{ top: 5, right: 30, left: 20, bottom: 5 }}
                >
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.2} />
                  <XAxis
                    dataKey="date"
                    tick={{ fill: '#6B7280', fontSize: 12 }}
                    stroke="#6B7280"
                  />
                  <YAxis
                    tick={{ fill: '#6B7280', fontSize: 12 }}
                    stroke="#6B7280"
                    label={{
                      value: t('upload:dashboard.certCount'),
                      angle: -90,
                      position: 'insideLeft',
                      style: { fill: '#6B7280', fontSize: 12 },
                    }}
                  />
                  <Tooltip
                    contentStyle={{
                      backgroundColor: '#1F2937',
                      border: '1px solid #374151',
                      borderRadius: '8px',
                      color: '#F9FAFB',
                    }}
                    labelStyle={{ color: '#F9FAFB', fontWeight: 'bold' }}
                    formatter={(value) => Number(value).toLocaleString()}
                  />
                  <Legend
                    wrapperStyle={{ paddingTop: '20px' }}
                    iconType="line"
                  />
                  <Line type="monotone" dataKey="DSC" stroke="#8B5CF6" strokeWidth={2} dot={{ fill: '#8B5CF6', r: 4 }} activeDot={{ r: 6 }} name="DSC" />
                  <Line type="monotone" dataKey="CSCA" stroke="#10B981" strokeWidth={2} dot={{ fill: '#10B981', r: 4 }} activeDot={{ r: 6 }} name="CSCA" />
                  <Line type="monotone" dataKey="CRL" stroke="#F59E0B" strokeWidth={2} dot={{ fill: '#F59E0B', r: 4 }} activeDot={{ r: 6 }} name="CRL" />
                  <Line type="monotone" dataKey="DSC_NC" stroke="#EF4444" strokeWidth={2} dot={{ fill: '#EF4444', r: 4 }} activeDot={{ r: 6 }} name="DSC_NC" />
                  <Line type="monotone" dataKey="ML" stroke="#3B82F6" strokeWidth={2} dot={{ fill: '#3B82F6', r: 4 }} activeDot={{ r: 6 }} name="ML" />
                </LineChart>
              </ResponsiveContainer>
            </div>
          )}
        </>
      )}

      {/* Validation Reason Detail Dialog */}
      {reasonDialogOpen && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm z-[70] flex items-center justify-center p-4" onClick={() => setReasonDialogOpen(null)}>
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-2xl w-full max-h-[80vh] overflow-hidden" onClick={e => e.stopPropagation()}>
            {/* Dialog Header */}
            <div className={cn(
              'px-5 py-3 border-b',
              reasonDialogOpen === 'INVALID' && 'bg-red-50 dark:bg-red-900/20 border-red-200 dark:border-red-800',
              reasonDialogOpen === 'PENDING' && 'bg-amber-50 dark:bg-amber-900/20 border-amber-200 dark:border-amber-800',
              reasonDialogOpen === 'EXPIRED' && 'bg-orange-50 dark:bg-orange-900/20 border-orange-200 dark:border-orange-800',
              reasonDialogOpen === 'REVOKED' && 'bg-gray-50 dark:bg-gray-800/50 border-gray-200 dark:border-gray-700',
            )}>
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-2">
                  {reasonDialogOpen === 'INVALID' && <XCircle className="w-4 h-4 text-red-600 dark:text-red-400" />}
                  {reasonDialogOpen === 'PENDING' && <AlertTriangle className="w-4 h-4 text-amber-600 dark:text-amber-400" />}
                  {reasonDialogOpen === 'EXPIRED' && <Clock className="w-4 h-4 text-orange-600 dark:text-orange-400" />}
                  {reasonDialogOpen === 'REVOKED' && <XCircle className="w-4 h-4 text-gray-600 dark:text-gray-400" />}
                  <h3 className="text-base font-bold text-gray-900 dark:text-white">
                    {reasonDialogOpen === 'INVALID' && t('upload:dashboard.invalidDetail')}
                    {reasonDialogOpen === 'PENDING' && t('upload:dashboard.pendingDetail')}
                    {reasonDialogOpen === 'EXPIRED' && t('upload:dashboard.expiredDetail')}
                    {reasonDialogOpen === 'REVOKED' && t('upload:dashboard.revokedDetail')}
                  </h3>
                </div>
                <button onClick={() => setReasonDialogOpen(null)} className="text-gray-400 hover:text-gray-600 dark:hover:text-gray-300">
                  <XCircle className="w-5 h-5" />
                </button>
              </div>
              <p className="text-sm text-gray-500 dark:text-gray-400 mt-1">
                {reasonDialogOpen === 'INVALID' && t('upload:dashboard.invalidExplanation')}
                {reasonDialogOpen === 'PENDING' && t('upload:dashboard.pendingExplanation')}
                {reasonDialogOpen === 'EXPIRED' && t('upload:dashboard.expiredExplanation')}
                {reasonDialogOpen === 'REVOKED' && t('upload:dashboard.revokedExplanation')}
              </p>
            </div>

            {/* Dialog Body */}
            <div className="px-5 py-4 overflow-y-auto max-h-[55vh]">
              {reasonsLoading ? (
                <div className="flex items-center justify-center py-8">
                  <Loader2 className="w-6 h-6 animate-spin text-gray-400" />
                  <span className="ml-2 text-gray-500">{t('common:button.loading')}</span>
                </div>
              ) : (reasonDialogOpen === 'INVALID' || reasonDialogOpen === 'PENDING') ? (() => {
                const filtered = validationReasons.filter(r => r.status === reasonDialogOpen);
                if (filtered.length === 0) {
                  return <p className="text-center text-gray-400 py-8">{ t('common:table.noData') }</p>;
                }

                const grouped = new Map<string, { reason: string; totalCount: number; countries: { code: string; count: number }[] }>();
                for (const entry of filtered) {
                  const key = translateReason(entry.reason);
                  if (!grouped.has(key)) {
                    grouped.set(key, { reason: key, totalCount: 0, countries: [] });
                  }
                  const g = grouped.get(key)!;
                  g.totalCount += entry.count;
                  // Merge same country across different raw reasons
                  const existing = g.countries.find(c => c.code === entry.countryCode);
                  if (existing) {
                    existing.count += entry.count;
                  } else {
                    g.countries.push({ code: entry.countryCode, count: entry.count });
                  }
                }
                for (const g of grouped.values()) {
                  g.countries.sort((a, b) => b.count - a.count);
                }
                const sorted = [...grouped.values()].sort((a, b) => b.totalCount - a.totalCount);
                const grandTotal = sorted.reduce((s, r) => s + r.totalCount, 0);

                const getDescription = (reason: string) => {
                  if (reason === t('upload:dashboard.sigVerifyFailed')) return t('upload:dashboard.cscaPublicKeyMismatch');
                  if (reason === t('upload:dashboard.trustChainBroken')) return t('upload:dashboard.trustChainBrokenDesc');
                  if (reason === t('upload:dashboard.cscaNotRegistered')) return t('upload:dashboard.cscaNotRegisteredDesc');
                  if (reason === t('upload:dashboard.notYetValid')) return t('upload:dashboard.notYetValidDesc');
                  if (reason === t('upload:dashboard.expiredButSigValid')) return t('upload:dashboard.expiredValidDesc');
                  if (reason === t('upload:dashboard.trustChainValidationFailed')) return t('upload:dashboard.cscaExistsButFailed');
                  return '';
                };
                const isInvalid = reasonDialogOpen === 'INVALID';

                return (
                  <div className="space-y-3">
                    {sorted.map((row, idx) => (
                      <div key={idx} className={cn(
                        'rounded-xl border p-4',
                        isInvalid
                          ? 'border-red-100 dark:border-red-900/40 bg-red-50/50 dark:bg-red-900/10'
                          : 'border-amber-100 dark:border-amber-900/40 bg-amber-50/50 dark:bg-amber-900/10'
                      )}>
                        <div className="flex items-start justify-between gap-3 mb-2.5">
                          <div className="flex items-center gap-2 min-w-0">
                            <span className={cn(
                              'shrink-0 w-6 h-6 rounded-full flex items-center justify-center text-xs font-bold text-white',
                              isInvalid ? 'bg-red-400 dark:bg-red-500' : 'bg-amber-400 dark:bg-amber-500'
                            )}>
                              {idx + 1}
                            </span>
                            <span className="font-semibold text-gray-900 dark:text-white">
                              {row.reason}
                            </span>
                          </div>
                          <span className={cn(
                            'shrink-0 px-3 py-1 rounded-full text-sm font-bold',
                            isInvalid
                              ? 'bg-red-100 dark:bg-red-900/40 text-red-700 dark:text-red-300'
                              : 'bg-amber-100 dark:bg-amber-900/40 text-amber-700 dark:text-amber-300'
                          )}>
                            {t('upload:dashboard.itemCount', { num: row.totalCount.toLocaleString() })}
                          </span>
                        </div>
                        <p className="text-xs text-gray-500 dark:text-gray-400 mb-2.5 ml-8">
                          {getDescription(row.reason)}
                        </p>
                        <div className="flex flex-wrap gap-1.5 ml-8">
                          {row.countries.map((c, ci) => (
                            <span key={ci} className="inline-flex items-center gap-1 px-2 py-0.5 rounded-md text-xs font-medium bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 text-gray-700 dark:text-gray-300">
                              {getFlagSvgPath(c.code) && <img src={getFlagSvgPath(c.code)} alt={c.code} title={getCountryName(c.code)} className="w-4 h-3 object-cover rounded-sm" />}
                              <span className="font-bold">{c.code}</span>
                              <span className="text-gray-400 dark:text-gray-500">{c.count.toLocaleString()}</span>
                            </span>
                          ))}
                        </div>
                      </div>
                    ))}
                    <div className={cn(
                      'flex items-center justify-between pt-2 border-t',
                      isInvalid ? 'border-red-200 dark:border-red-800' : 'border-amber-200 dark:border-amber-800'
                    )}>
                      <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">
                        {t('upload:dashboard.totalReasons', { num: sorted.length })}
                      </span>
                      <span className={cn(
                        'text-lg font-bold',
                        isInvalid ? 'text-red-600 dark:text-red-400' : 'text-amber-600 dark:text-amber-400'
                      )}>
                        {t('upload:dashboard.totalSum', { num: grandTotal.toLocaleString() })}
                      </span>
                    </div>
                  </div>
                );
              })() : reasonDialogOpen === 'EXPIRED' ? (() => {
                if (expiredData.length === 0) {
                  return <p className="text-center text-gray-400 py-8">{ t('common:table.noData') }</p>;
                }

                // Group by country, collect years
                const byCountry = new Map<string, { totalCount: number; years: { year: number; count: number }[] }>();
                for (const entry of expiredData) {
                  if (!byCountry.has(entry.countryCode)) {
                    byCountry.set(entry.countryCode, { totalCount: 0, years: [] });
                  }
                  const g = byCountry.get(entry.countryCode)!;
                  g.totalCount += entry.count;
                  g.years.push({ year: entry.expireYear, count: entry.count });
                }
                for (const g of byCountry.values()) {
                  g.years.sort((a, b) => a.year - b.year);
                }
                const sorted = [...byCountry.entries()]
                  .map(([code, data]) => ({ code, ...data }))
                  .sort((a, b) => b.totalCount - a.totalCount);
                const grandTotal = sorted.reduce((s, r) => s + r.totalCount, 0);

                return (
                  <div className="space-y-3">
                    {sorted.map((row, idx) => (
                      <div key={idx} className="rounded-xl border border-orange-100 dark:border-orange-900/40 bg-orange-50/50 dark:bg-orange-900/10 p-4">
                        <div className="flex items-start justify-between gap-3 mb-2.5">
                          <div className="flex items-center gap-2 min-w-0">
                            <span className="shrink-0 w-6 h-6 rounded-full flex items-center justify-center text-xs font-bold text-white bg-orange-400 dark:bg-orange-500">
                              {idx + 1}
                            </span>
                            {getFlagSvgPath(row.code) && <img src={getFlagSvgPath(row.code)} alt={row.code} title={getCountryName(row.code)} className="w-5 h-4 object-cover rounded-sm" />}
                            <span className="font-semibold text-gray-900 dark:text-white text-lg">
                              {row.code}
                            </span>
                          </div>
                          <span className="shrink-0 px-3 py-1 rounded-full text-sm font-bold bg-orange-100 dark:bg-orange-900/40 text-orange-700 dark:text-orange-300">
                            {t('upload:dashboard.itemCount', { num: row.totalCount.toLocaleString() })}
                          </span>
                        </div>
                        <div className="flex flex-wrap gap-1.5 ml-8">
                          {row.years.map((y, yi) => (
                            <span key={yi} className="inline-flex items-center gap-1 px-2 py-0.5 rounded-md text-xs font-medium bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 text-gray-700 dark:text-gray-300">
                              <span className="font-bold">{t('upload:dashboard.year', { year: y.year })}</span>
                              <span className="text-gray-400 dark:text-gray-500">{y.count.toLocaleString()}</span>
                            </span>
                          ))}
                        </div>
                      </div>
                    ))}
                    <div className="flex items-center justify-between pt-2 border-t border-orange-200 dark:border-orange-800">
                      <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">
                        {t('upload:dashboard.totalCountries', { num: sorted.length })}
                      </span>
                      <span className="text-lg font-bold text-orange-600 dark:text-orange-400">
                        {t('upload:dashboard.totalSum', { num: grandTotal.toLocaleString() })}
                      </span>
                    </div>
                  </div>
                );
              })() : reasonDialogOpen === 'REVOKED' ? (() => {
                if (revokedData.length === 0) {
                  return <p className="text-center text-gray-400 py-8">{ t('common:table.noData') }</p>;
                }

                const sorted = [...revokedData].sort((a, b) => b.count - a.count);
                const grandTotal = sorted.reduce((s, r) => s + r.count, 0);

                return (
                  <div className="space-y-3">
                    {sorted.map((row, idx) => (
                      <div key={idx} className="rounded-xl border border-gray-200 dark:border-gray-600 bg-gray-50/50 dark:bg-gray-700/30 p-4">
                        <div className="flex items-center justify-between">
                          <div className="flex items-center gap-2">
                            <span className="shrink-0 w-6 h-6 rounded-full flex items-center justify-center text-xs font-bold text-white bg-gray-400 dark:bg-gray-500">
                              {idx + 1}
                            </span>
                            {getFlagSvgPath(row.countryCode) && <img src={getFlagSvgPath(row.countryCode)} alt={row.countryCode} title={getCountryName(row.countryCode)} className="w-5 h-4 object-cover rounded-sm" />}
                            <span className="font-semibold text-gray-900 dark:text-white text-lg">
                              {row.countryCode}
                            </span>
                          </div>
                          <span className="px-3 py-1 rounded-full text-sm font-bold bg-gray-200 dark:bg-gray-600 text-gray-700 dark:text-gray-300">
                            {t('upload:dashboard.itemCount', { num: row.count.toLocaleString() })}
                          </span>
                        </div>
                      </div>
                    ))}
                    <div className="flex items-center justify-between pt-2 border-t border-gray-300 dark:border-gray-600">
                      <span className="text-sm font-semibold text-gray-700 dark:text-gray-300">
                        {t('upload:dashboard.totalCountries', { num: sorted.length })}
                      </span>
                      <span className="text-lg font-bold text-gray-600 dark:text-gray-400">
                        {t('upload:dashboard.totalSum', { num: grandTotal.toLocaleString() })}
                      </span>
                    </div>
                  </div>
                );
              })() : null}
            </div>

            {/* Dialog Footer */}
            <div className={cn(
              'px-6 py-3 border-t flex justify-end',
              reasonDialogOpen === 'INVALID' && 'border-red-200 dark:border-red-800',
              reasonDialogOpen === 'PENDING' && 'border-amber-200 dark:border-amber-800',
              reasonDialogOpen === 'EXPIRED' && 'border-orange-200 dark:border-orange-800',
              reasonDialogOpen === 'REVOKED' && 'border-gray-300 dark:border-gray-600',
            )}>
              <button
                onClick={() => setReasonDialogOpen(null)}
                className="px-4 py-2 text-sm font-medium text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 dark:hover:bg-gray-600 rounded-lg transition-colors"
              >
                {t('icao:banner.dismiss')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default UploadDashboard;
