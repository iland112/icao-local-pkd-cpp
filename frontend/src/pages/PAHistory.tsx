import { useState, useEffect, useMemo } from 'react';
import { Link } from 'react-router-dom';
import {
  History,
  ShieldCheck,
  CheckCircle,
  XCircle,
  AlertCircle,
  ChevronLeft,
  ChevronRight,
  Search,
  Filter,
  Eye,
  Loader2,
  Calendar,
  RefreshCw,
  FileText,
  X,
  Info,
  User,
  AlertTriangle,
} from 'lucide-react';
import { paApi } from '@/services/paApi';
import type { PAHistoryItem, PAStatus } from '@/types';
import { cn } from '@/utils/cn';
import { getFlagSvgPath } from '@/utils/countryCode';

// DG1 MRZ Data interface
interface DG1Data {
  surname?: string;
  givenNames?: string;
  documentNumber?: string;
  nationality?: string;
  sex?: string;
  dateOfBirth?: string;
  expirationDate?: string;
}

// DG2 Face Image interface
interface DG2Data {
  faceImages?: Array<{
    imageDataUrl?: string;
    imageFormat?: string;
  }>;
}

// DG Data response interface
interface DGDataResponse {
  hasDg1: boolean;
  hasDg2: boolean;
  dg1?: DG1Data;
  dg2?: DG2Data;
}

export function PAHistory() {
  const [history, setHistory] = useState<PAHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState(0);
  const [totalPages, setTotalPages] = useState(0);
  const [totalElements, setTotalElements] = useState(0);

  // Filters
  const [searchTerm, setSearchTerm] = useState('');
  const [statusFilter, setStatusFilter] = useState<PAStatus | ''>('');
  const [countryFilter, setCountryFilter] = useState('');
  const [dateFrom, setDateFrom] = useState('');
  const [dateTo, setDateTo] = useState('');

  // Modal state
  const [selectedRecord, setSelectedRecord] = useState<PAHistoryItem | null>(null);
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [dgData, setDgData] = useState<DGDataResponse | null>(null);
  const [dgLoading, setDgLoading] = useState(false);
  const [dgError, setDgError] = useState<string | null>(null);

  const pageSize = 10;

  useEffect(() => {
    fetchHistory();
  }, [page]);

  const fetchHistory = async () => {
    setLoading(true);
    try {
      const response = await paApi.getHistory({
        page,
        size: pageSize,
        sort: 'verifiedAt',
        direction: 'DESC',
      });
      // Backend returns: { success, total, page, size, data: [...] }
      const resData = response.data;
      const items = resData.data ?? [];
      const total = resData.total ?? 0;
      setHistory(items);
      setTotalPages(Math.ceil(total / pageSize) || 1);
      setTotalElements(total);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Failed to fetch PA history:', error);
      setHistory([]);
    } finally {
      setLoading(false);
    }
  };

  // Calculate statistics from current page data
  const stats = useMemo(() => {
    const valid = history.filter((h) => h.status === 'VALID').length;
    const invalid = history.filter((h) => h.status === 'INVALID').length;
    const error = history.filter((h) => h.status === 'ERROR').length;
    const total = history.length;

    return {
      total: totalElements,
      valid,
      invalid,
      error,
      validPercent: total > 0 ? Math.round((valid / total) * 100) : 0,
      invalidPercent: total > 0 ? Math.round((invalid / total) * 100) : 0,
      errorPercent: total > 0 ? Math.round((error / total) * 100) : 0,
    };
  }, [history, totalElements]);

  // Extract unique countries from history
  const uniqueCountries = useMemo(() => {
    const countries = new Set<string>();
    history.forEach((item) => {
      if (item.issuingCountry) {
        countries.add(item.issuingCountry);
      }
    });
    return Array.from(countries).sort();
  }, [history]);

  const getStatusBadge = (status: PAStatus) => {
    const styles: Record<PAStatus, { bg: string; text: string; icon: React.ReactNode }> = {
      VALID: {
        bg: 'bg-green-100 dark:bg-green-900/30',
        text: 'text-green-600 dark:text-green-400',
        icon: <CheckCircle className="w-3 h-3" />,
      },
      INVALID: {
        bg: 'bg-red-100 dark:bg-red-900/30',
        text: 'text-red-600 dark:text-red-400',
        icon: <XCircle className="w-3 h-3" />,
      },
      ERROR: {
        bg: 'bg-yellow-100 dark:bg-yellow-900/30',
        text: 'text-yellow-600 dark:text-yellow-400',
        icon: <AlertCircle className="w-3 h-3" />,
      },
    };

    const style = styles[status];
    const label = {
      VALID: 'Valid',
      INVALID: 'Invalid',
      ERROR: 'Error',
    }[status];

    return (
      <span className={cn('inline-flex items-center gap-1 px-2 py-1 rounded-full text-xs font-medium', style.bg, style.text)}>
        {style.icon}
        {label}
      </span>
    );
  };

  const formatDate = (dateString: string): string => {
    return new Date(dateString).toLocaleString('ko-KR', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
    });
  };

  const filteredHistory = history.filter((item) => {
    const matchesSearch =
      !searchTerm ||
      item.documentNumber?.toLowerCase().includes(searchTerm.toLowerCase()) ||
      item.issuingCountry?.toLowerCase().includes(searchTerm.toLowerCase());
    const matchesStatus = !statusFilter || item.status === statusFilter;
    const matchesCountry = !countryFilter || item.issuingCountry === countryFilter;

    // Date range filter
    let matchesDateFrom = true;
    let matchesDateTo = true;
    if (dateFrom && item.verificationTimestamp) {
      matchesDateFrom = new Date(item.verificationTimestamp) >= new Date(dateFrom);
    }
    if (dateTo && item.verificationTimestamp) {
      matchesDateTo = new Date(item.verificationTimestamp) <= new Date(dateTo + 'T23:59:59');
    }

    return matchesSearch && matchesStatus && matchesCountry && matchesDateFrom && matchesDateTo;
  });

  const clearFilters = () => {
    setSearchTerm('');
    setStatusFilter('');
    setCountryFilter('');
    setDateFrom('');
    setDateTo('');
  };

  const hasActiveFilters = searchTerm || statusFilter || countryFilter || dateFrom || dateTo;

  // Open detail modal
  const openDetailModal = async (record: PAHistoryItem) => {
    setSelectedRecord(record);
    setIsModalOpen(true);
    setDgData(null);
    setDgError(null);

    // Load DG data for VALID records
    if (record.status === 'VALID' && record.verificationId) {
      setDgLoading(true);
      try {
        const response = await paApi.getDataGroups(record.verificationId);
        setDgData(response.data as DGDataResponse);
      } catch (error) {
        if (import.meta.env.DEV) console.error('Failed to load DG data:', error);
        setDgError('DG 데이터를 불러올 수 없습니다.');
      } finally {
        setDgLoading(false);
      }
    }
  };

  // Close detail modal
  const closeDetailModal = () => {
    setIsModalOpen(false);
    setSelectedRecord(null);
    setDgData(null);
    setDgError(null);
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4">
      {/* Page Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-purple-500 to-pink-600 shadow-lg">
            <History className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">PA 검증 이력</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              Passive Authentication 검증 이력을 조회하고 필터링합니다.
            </p>
          </div>
          <div className="flex gap-2">
            <button
              onClick={fetchHistory}
              disabled={loading}
              className="inline-flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
            >
              <RefreshCw className={cn('w-4 h-4', loading && 'animate-spin')} />
            </button>
            <Link
              to="/pa/verify"
              className="inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500 hover:from-teal-600 hover:to-cyan-600 transition-all shadow-md hover:shadow-lg"
            >
              <ShieldCheck className="w-4 h-4" />
              새 검증
            </Link>
          </div>
        </div>
      </div>

      {/* Statistics Cards */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-3 mb-4">
        {/* Total */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-blue-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/30">
              <FileText className="w-5 h-5 text-blue-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">총 검증</p>
              <p className="text-xl font-bold text-blue-600 dark:text-blue-400">{stats.total.toLocaleString()}</p>
            </div>
          </div>
        </div>

        {/* Valid */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-green-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-green-50 dark:bg-green-900/30">
              <CheckCircle className="w-5 h-5 text-green-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">Valid</p>
              <p className="text-xl font-bold text-green-600 dark:text-green-400">{stats.valid}</p>
              <p className="text-xs text-gray-400">{stats.validPercent}%</p>
            </div>
          </div>
        </div>

        {/* Invalid */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-red-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-red-50 dark:bg-red-900/30">
              <XCircle className="w-5 h-5 text-red-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">Invalid</p>
              <p className="text-xl font-bold text-red-600 dark:text-red-400">{stats.invalid}</p>
              <p className="text-xs text-gray-400">{stats.invalidPercent}%</p>
            </div>
          </div>
        </div>

        {/* Error */}
        <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md p-4 border-l-4 border-yellow-500">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-yellow-50 dark:bg-yellow-900/30">
              <AlertCircle className="w-5 h-5 text-yellow-500" />
            </div>
            <div>
              <p className="text-xs text-gray-500 dark:text-gray-400 font-medium">Error</p>
              <p className="text-xl font-bold text-yellow-600 dark:text-yellow-400">{stats.error}</p>
              <p className="text-xs text-gray-400">{stats.errorPercent}%</p>
            </div>
          </div>
        </div>
      </div>

      {/* Filters Card */}
      <div className="bg-white dark:bg-gray-800 rounded-xl shadow-md mb-4 p-4">
        <div className="flex items-center gap-2 mb-3">
          <Filter className="w-4 h-4 text-blue-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검색 필터</h3>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-5 gap-3">
          {/* Country Filter */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              발급 국가
            </label>
            <select
              value={countryFilter}
              onChange={(e) => setCountryFilter(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">전체</option>
              {uniqueCountries.map((country) => (
                <option key={country} value={country}>{country}</option>
              ))}
            </select>
          </div>

          {/* Status Filter */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              검증 결과
            </label>
            <select
              value={statusFilter}
              onChange={(e) => setStatusFilter(e.target.value as PAStatus | '')}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="">전체</option>
              <option value="VALID">Valid</option>
              <option value="INVALID">Invalid</option>
              <option value="ERROR">Error</option>
            </select>
          </div>

          {/* Date From */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              시작 날짜
            </label>
            <input
              type="date"
              value={dateFrom}
              onChange={(e) => setDateFrom(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Date To */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              종료 날짜
            </label>
            <input
              type="date"
              value={dateTo}
              onChange={(e) => setDateTo(e.target.value)}
              className="w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {/* Search & Actions */}
          <div>
            <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">
              검색
            </label>
            <div className="flex gap-2">
              <div className="relative flex-1">
                <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  type="text"
                  placeholder="여권번호..."
                  value={searchTerm}
                  onChange={(e) => setSearchTerm(e.target.value)}
                  className="w-full pl-8 pr-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
              {hasActiveFilters && (
                <button
                  onClick={clearFilters}
                  className="px-3 py-2 text-xs font-medium text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700"
                >
                  초기화
                </button>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl overflow-hidden">
        <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2">
          <FileText className="w-4 h-4 text-purple-500" />
          <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300">검증 이력</h3>
          <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
            {filteredHistory.length}건
          </span>
        </div>

        {loading ? (
          <div className="flex items-center justify-center py-20">
            <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
          </div>
        ) : filteredHistory.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-20 text-gray-500 dark:text-gray-400">
            <AlertCircle className="w-12 h-12 mb-4 opacity-50" />
            <p className="text-lg font-medium">검증 이력이 없습니다.</p>
            <p className="text-sm">PA 검증을 수행하거나 필터를 조정하세요.</p>
            <Link
              to="/pa/verify"
              className="mt-4 inline-flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium text-white bg-gradient-to-r from-teal-500 to-cyan-500"
            >
              <ShieldCheck className="w-4 h-4" />
              검증 수행하기
            </Link>
          </div>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead className="bg-gray-50 dark:bg-gray-700/50">
                  <tr>
                    <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      검증 ID
                    </th>
                    <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      국가
                    </th>
                    <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      여권번호
                    </th>
                    <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      결과
                    </th>
                    <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      검증 시각
                    </th>
                    <th className="px-5 py-3 text-left text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      요청자
                    </th>
                    <th className="px-5 py-3 text-right text-xs font-semibold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                      상세
                    </th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-200 dark:divide-gray-700">
                  {filteredHistory.map((item) => (
                    <tr
                      key={item.verificationId}
                      className="hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                    >
                      <td className="px-5 py-3">
                        <span className="font-mono text-xs text-gray-600 dark:text-gray-300">
                          {item.verificationId ? `${item.verificationId.substring(0, 8)}...` : '-'}
                        </span>
                      </td>
                      <td className="px-5 py-3">
                        {item.issuingCountry ? (
                          <div className="flex items-center gap-2">
                            {getFlagSvgPath(item.issuingCountry) && (
                              <img
                                src={getFlagSvgPath(item.issuingCountry)}
                                alt={item.issuingCountry}
                                className="w-6 h-4 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                                onError={(e) => {
                                  (e.target as HTMLImageElement).style.display = 'none';
                                }}
                              />
                            )}
                            <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
                              {item.issuingCountry}
                            </span>
                          </div>
                        ) : (
                          <span className="text-gray-400">-</span>
                        )}
                      </td>
                      <td className="px-5 py-3 text-sm text-gray-600 dark:text-gray-300 font-mono">
                        {item.documentNumber || '-'}
                      </td>
                      <td className="px-5 py-3">{getStatusBadge(item.status)}</td>
                      <td className="px-5 py-3">
                        <span className="inline-flex items-center gap-1 text-xs text-gray-500 dark:text-gray-400">
                          <Calendar className="w-3 h-3" />
                          {formatDate(item.verificationTimestamp)}
                        </span>
                      </td>
                      <td className="px-5 py-3 text-xs text-gray-500 dark:text-gray-400">
                        {item.requestedBy ? (
                          <span>{item.requestedBy}</span>
                        ) : (
                          <div>
                            <span className="text-gray-400 dark:text-gray-500">anonymous</span>
                            {item.clientIp && (
                              <span className="ml-1.5 text-[10px] text-gray-400 dark:text-gray-500 font-mono">
                                ({item.clientIp})
                              </span>
                            )}
                          </div>
                        )}
                      </td>
                      <td className="px-5 py-3 text-right">
                        <button
                          onClick={() => openDetailModal(item)}
                          className="inline-flex items-center gap-1 px-2.5 py-1 rounded-lg text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/20 transition-colors"
                        >
                          <Eye className="w-3 h-3" />
                          보기
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            {/* Pagination */}
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <p className="text-xs text-gray-500 dark:text-gray-400">
                총 {totalElements}개 중 {page * pageSize + 1}-{Math.min((page + 1) * pageSize, totalElements)}개 표시
              </p>
              <div className="flex items-center gap-1">
                <button
                  onClick={() => setPage((p) => Math.max(0, p - 1))}
                  disabled={page === 0}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <span className="px-3 text-sm text-gray-600 dark:text-gray-300">
                  {page + 1} / {totalPages || 1}
                </span>
                <button
                  onClick={() => setPage((p) => Math.min(totalPages - 1, p + 1))}
                  disabled={page >= totalPages - 1}
                  className="p-1.5 rounded-lg border border-gray-200 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
                >
                  <ChevronRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          </>
        )}
      </div>

      {/* Detail Modal */}
      {isModalOpen && selectedRecord && (
        <div className="fixed inset-0 z-50 flex items-center justify-center">
          {/* Backdrop */}
          <div
            className="absolute inset-0 bg-black/50 backdrop-blur-sm"
            onClick={closeDetailModal}
          />

          {/* Modal Content */}
          <div className="relative bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-4xl max-h-[90vh] overflow-y-auto mx-4">
            {/* Modal Header */}
            <div className="sticky top-0 z-10 px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between bg-white dark:bg-gray-800">
              <div className="flex items-center gap-2.5">
                <div className="p-1.5 rounded-lg bg-gradient-to-br from-blue-500 to-indigo-600">
                  <Info className="w-4 h-4 text-white" />
                </div>
                <h2 className="text-base font-bold text-gray-900 dark:text-white">검증 상세 정보</h2>
                {getStatusBadge(selectedRecord.status)}
              </div>
              <button
                onClick={closeDetailModal}
                className="p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors"
              >
                <X className="w-4 h-4 text-gray-500" />
              </button>
            </div>

            {/* Modal Body */}
            <div className="p-4 space-y-3">

              {/* Section 1: 기본 정보 - inline row */}
              <div className="grid grid-cols-5 gap-2">
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-[10px] font-semibold text-gray-400 uppercase tracking-wider">검증 ID</p>
                  <p className="font-mono text-xs text-gray-900 dark:text-white truncate mt-0.5" title={selectedRecord.verificationId}>
                    {selectedRecord.verificationId.substring(0, 12)}...
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-[10px] font-semibold text-gray-400 uppercase tracking-wider">검증 시각</p>
                  <p className="text-xs text-gray-900 dark:text-white mt-0.5">
                    {formatDate(selectedRecord.verificationTimestamp)}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-[10px] font-semibold text-gray-400 uppercase tracking-wider">발급 국가</p>
                  <div className="flex items-center gap-1.5 mt-0.5">
                    {selectedRecord.issuingCountry && getFlagSvgPath(selectedRecord.issuingCountry) && (
                      <img
                        src={getFlagSvgPath(selectedRecord.issuingCountry)}
                        alt={selectedRecord.issuingCountry}
                        className="w-5 h-3.5 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                        onError={(e) => { (e.target as HTMLImageElement).style.display = 'none'; }}
                      />
                    )}
                    <span className="text-xs font-medium text-gray-900 dark:text-white">
                      {selectedRecord.issuingCountry || '-'}
                    </span>
                  </div>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-[10px] font-semibold text-gray-400 uppercase tracking-wider">여권 번호</p>
                  <p className="font-mono text-xs font-medium text-blue-600 dark:text-blue-400 mt-0.5">
                    {selectedRecord.documentNumber || '-'}
                  </p>
                </div>
                <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg px-2.5 py-2">
                  <p className="text-[10px] font-semibold text-gray-400 uppercase tracking-wider">요청자</p>
                  {selectedRecord.requestedBy ? (
                    <p className="text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">
                      {selectedRecord.requestedBy}
                    </p>
                  ) : (
                    <div className="mt-0.5">
                      <p className="text-xs text-gray-400 dark:text-gray-500">anonymous</p>
                      {selectedRecord.clientIp && (
                        <p className="text-[10px] font-mono text-gray-400 dark:text-gray-500 mt-0.5">
                          IP: {selectedRecord.clientIp}
                        </p>
                      )}
                      {selectedRecord.userAgent && (
                        <p className="text-[10px] text-gray-400 dark:text-gray-500 mt-0.5 truncate" title={selectedRecord.userAgent}>
                          UA: {selectedRecord.userAgent.length > 40 ? selectedRecord.userAgent.substring(0, 40) + '...' : selectedRecord.userAgent}
                        </p>
                      )}
                    </div>
                  )}
                </div>
              </div>

              {/* Section 2: 검증 결과 */}
              <div>
                <div className="flex items-center gap-1.5 mb-2">
                  <div className="w-1 h-3.5 rounded-full bg-green-500" />
                  <h3 className="text-xs font-bold text-gray-800 dark:text-gray-200">검증 결과</h3>
                </div>

                <div className="grid grid-cols-3 gap-2">
                  {/* SOD Signature */}
                  <div className={cn(
                    'rounded-lg px-3 py-2.5 border-l-3',
                    selectedRecord.sodSignatureValid
                      ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                      : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                  )}>
                    <p className="text-[10px] font-semibold text-gray-500 dark:text-gray-400 mb-1">SOD 서명 검증</p>
                    <div className="flex items-center gap-1.5">
                      {selectedRecord.sodSignatureValid ? (
                        <CheckCircle className="w-4 h-4 text-green-500" />
                      ) : (
                        <XCircle className="w-4 h-4 text-red-500" />
                      )}
                      <span className={cn(
                        'text-xs font-bold',
                        selectedRecord.sodSignatureValid
                          ? 'text-green-600 dark:text-green-400'
                          : 'text-red-600 dark:text-red-400'
                      )}>
                        {selectedRecord.sodSignatureValid ? 'Valid' : 'Invalid'}
                      </span>
                    </div>
                  </div>

                  {/* Certificate Chain */}
                  <div className={cn(
                    'rounded-lg px-3 py-2.5 border-l-3',
                    selectedRecord.trustChainValid
                      ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                      : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                  )}>
                    <p className="text-[10px] font-semibold text-gray-500 dark:text-gray-400 mb-1">인증서 체인 검증</p>
                    <div className="flex items-center gap-1.5">
                      {selectedRecord.trustChainValid ? (
                        <CheckCircle className="w-4 h-4 text-green-500" />
                      ) : (
                        <XCircle className="w-4 h-4 text-red-500" />
                      )}
                      <span className={cn(
                        'text-xs font-bold',
                        selectedRecord.trustChainValid
                          ? 'text-green-600 dark:text-green-400'
                          : 'text-red-600 dark:text-red-400'
                      )}>
                        {selectedRecord.trustChainValid ? 'Valid' : 'Invalid'}
                      </span>
                    </div>
                  </div>

                  {/* Data Group Hash */}
                  <div className={cn(
                    'rounded-lg px-3 py-2.5 border-l-3',
                    selectedRecord.dgHashesValid
                      ? 'bg-green-50 dark:bg-green-900/20 border-green-500'
                      : 'bg-red-50 dark:bg-red-900/20 border-red-500'
                  )}>
                    <p className="text-[10px] font-semibold text-gray-500 dark:text-gray-400 mb-1">DG 해시 검증</p>
                    <div className="flex items-center gap-1.5">
                      {selectedRecord.dgHashesValid ? (
                        <CheckCircle className="w-4 h-4 text-green-500" />
                      ) : (
                        <XCircle className="w-4 h-4 text-red-500" />
                      )}
                      <span className={cn(
                        'text-xs font-bold',
                        selectedRecord.dgHashesValid
                          ? 'text-green-600 dark:text-green-400'
                          : 'text-red-600 dark:text-red-400'
                      )}>
                        {selectedRecord.dgHashesValid ? 'Valid' : 'Invalid'}
                      </span>
                    </div>
                  </div>
                </div>

                {/* CRL Status - Show when not VALID */}
                {selectedRecord.crlStatus && selectedRecord.crlStatus !== 'VALID' && (
                  <div className={cn(
                    'rounded-lg px-3 py-2 mt-2 flex items-center gap-2',
                    selectedRecord.crlStatus === 'REVOKED' || selectedRecord.crlStatus === 'CRL_INVALID'
                      ? 'bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800'
                      : 'bg-yellow-50 dark:bg-yellow-900/20 border border-yellow-200 dark:border-yellow-800'
                  )}>
                    {selectedRecord.crlStatus === 'REVOKED' || selectedRecord.crlStatus === 'CRL_INVALID' ? (
                      <AlertTriangle className="w-4 h-4 text-red-500 flex-shrink-0" />
                    ) : (
                      <AlertCircle className="w-4 h-4 text-yellow-500 flex-shrink-0" />
                    )}
                    <span className={cn(
                      'text-xs font-bold',
                      selectedRecord.crlStatus === 'REVOKED' || selectedRecord.crlStatus === 'CRL_INVALID'
                        ? 'text-red-700 dark:text-red-400'
                        : 'text-yellow-700 dark:text-yellow-400'
                    )}>
                      CRL: {selectedRecord.crlStatus?.replace(/_/g, ' ')}
                    </span>
                  </div>
                )}
              </div>

              {/* DSC Non-Conformant Warning Banner */}
              {selectedRecord.dscNonConformant && (
                <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg px-3 py-2.5">
                  <div className="flex items-center gap-2">
                    <AlertTriangle className="w-4 h-4 text-amber-500 flex-shrink-0" />
                    <div>
                      <span className="text-xs font-bold text-amber-700 dark:text-amber-400">
                        Non-Conformant DSC
                      </span>
                      {selectedRecord.pkdConformanceCode && (
                        <span className="ml-2 px-1.5 py-0.5 text-[10px] rounded bg-amber-100 dark:bg-amber-900/40 text-amber-600 dark:text-amber-300 font-mono">
                          {selectedRecord.pkdConformanceCode}
                        </span>
                      )}
                      {selectedRecord.pkdConformanceText ? (
                        <p className="text-[11px] text-amber-600 dark:text-amber-400 mt-0.5">
                          {selectedRecord.pkdConformanceText}
                        </p>
                      ) : (
                        <p className="text-[11px] text-amber-600 dark:text-amber-400 mt-0.5">
                          이 여권의 DSC 인증서는 ICAO PKD 비적합(nc-data) 목록에 등록되어 있습니다.
                        </p>
                      )}
                    </div>
                  </div>
                </div>
              )}

              {/* Section 3: 데이터 그룹 (DG1 + DG2) */}
              {selectedRecord.status === 'VALID' && (
                <div>
                  <div className="flex items-center gap-1.5 mb-2">
                    <div className="w-1 h-3.5 rounded-full bg-purple-500" />
                    <h3 className="text-xs font-bold text-gray-800 dark:text-gray-200">데이터 그룹</h3>
                  </div>

                  {dgLoading ? (
                    <div className="flex items-center justify-center py-6 bg-gray-50 dark:bg-gray-700/30 rounded-lg">
                      <Loader2 className="w-4 h-4 animate-spin text-blue-500" />
                      <span className="ml-2 text-xs text-gray-500">로딩 중...</span>
                    </div>
                  ) : dgError ? (
                    <div className="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg px-3 py-2 flex items-center gap-2">
                      <AlertCircle className="w-4 h-4 text-yellow-500" />
                      <span className="text-xs text-yellow-700 dark:text-yellow-400">{dgError}</span>
                    </div>
                  ) : (
                    <div className="flex gap-2">
                      {/* DG1 - MRZ */}
                      <div className="flex-1 bg-blue-50 dark:bg-blue-900/20 rounded-lg p-3 border border-blue-100 dark:border-blue-800">
                        <div className="flex items-center gap-1.5 mb-2">
                          <div className="w-1.5 h-1.5 rounded-full bg-blue-500" />
                          <p className="text-xs font-bold text-blue-700 dark:text-blue-300">DG1 - MRZ 데이터</p>
                        </div>
                        {dgData?.hasDg1 && dgData.dg1 ? (
                          <div className="grid grid-cols-4 gap-1.5">
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-[10px] text-gray-400 leading-none">성</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">{dgData.dg1.surname || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5 col-span-2">
                              <p className="text-[10px] text-gray-400 leading-none">이름</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white truncate mt-0.5">{dgData.dg1.givenNames || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-[10px] text-gray-400 leading-none">성별</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">
                                {dgData.dg1.sex === 'M' ? '남성' : dgData.dg1.sex === 'F' ? '여성' : dgData.dg1.sex || '-'}
                              </p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5 col-span-2">
                              <p className="text-[10px] text-gray-400 leading-none">여권번호</p>
                              <p className="font-mono text-xs font-bold text-blue-600 dark:text-blue-400 mt-0.5">{dgData.dg1.documentNumber || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-[10px] text-gray-400 leading-none">국적</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">{dgData.dg1.nationality || '-'}</p>
                            </div>
                            <div className="bg-white dark:bg-gray-800 rounded px-2 py-1.5">
                              <p className="text-[10px] text-gray-400 leading-none">생년월일</p>
                              <p className="font-mono text-xs font-medium text-gray-900 dark:text-white mt-0.5">{dgData.dg1.dateOfBirth || '-'}</p>
                            </div>
                          </div>
                        ) : (
                          <div className="text-xs text-gray-500 dark:text-gray-400 text-center py-3">
                            DG1 데이터 없음
                          </div>
                        )}
                      </div>

                      {/* DG2 - Face */}
                      <div className="w-40 flex-shrink-0 bg-purple-50 dark:bg-purple-900/20 rounded-lg p-3 border border-purple-100 dark:border-purple-800">
                        <div className="flex items-center gap-1.5 mb-2">
                          <div className="w-1.5 h-1.5 rounded-full bg-purple-500" />
                          <p className="text-xs font-bold text-purple-700 dark:text-purple-300">DG2 - 얼굴</p>
                        </div>
                        {dgData?.hasDg2 && dgData.dg2?.faceImages?.[0]?.imageDataUrl ? (
                          <div className="flex flex-col items-center">
                            <img
                              src={dgData.dg2.faceImages[0].imageDataUrl}
                              alt="Passport Face"
                              className="w-28 aspect-[3/4] object-cover rounded-lg shadow-md border-2 border-purple-200 dark:border-purple-700"
                            />
                            <span className="mt-1.5 px-1.5 py-0.5 text-[10px] rounded-full bg-purple-100 dark:bg-purple-900/50 text-purple-600 dark:text-purple-400 font-medium">
                              {dgData.dg2.faceImages[0].imageFormat || 'N/A'}
                            </span>
                          </div>
                        ) : (
                          <div className="flex flex-col items-center justify-center h-28 text-xs text-gray-500 dark:text-gray-400">
                            <User className="w-6 h-6 text-gray-300 dark:text-gray-600 mb-1" />
                            없음
                          </div>
                        )}
                      </div>
                    </div>
                  )}
                </div>
              )}
            </div>

            {/* Modal Footer */}
            <div className="sticky bottom-0 px-5 py-3 border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 flex justify-end">
              <button
                onClick={closeDetailModal}
                className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 transition-colors border border-gray-200 dark:border-gray-600"
              >
                <X className="w-3.5 h-3.5" />
                닫기
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default PAHistory;
