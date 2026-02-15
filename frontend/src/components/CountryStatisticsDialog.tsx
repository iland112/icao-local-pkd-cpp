import { useState, useEffect } from 'react';
import { X, Globe, Download, Loader2 } from 'lucide-react';
import { uploadHistoryApi } from '@/services/pkdApi';

interface CountryStatistics {
  countryCode: string;
  mlsc: number;
  cscaSelfSigned: number;
  cscaLinkCert: number;
  dsc: number;
  dscNc: number;
  crl: number;
  totalCerts: number;
}

interface CountryStatisticsDialogProps {
  isOpen: boolean;
  onClose: () => void;
}

export function CountryStatisticsDialog({ isOpen, onClose }: CountryStatisticsDialogProps) {
  const [data, setData] = useState<CountryStatistics[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (isOpen) {
      fetchData();
    }
  }, [isOpen]);

  const fetchData = async () => {
    setLoading(true);
    setError(null);
    try {
      const response = await uploadHistoryApi.getDetailedCountryStatistics(0); // 0 = all countries
      setData(response.data.countries || []);
    } catch (err) {
      if (import.meta.env.DEV) console.error('Failed to fetch detailed country statistics:', err);
      setError('데이터를 불러오는데 실패했습니다.');
    } finally {
      setLoading(false);
    }
  };

  const exportToCSV = () => {
    const headers = ['Country', 'MLSC', 'CSCA (Self-signed)', 'CSCA (Link Cert)', 'DSC', 'DSC_NC', 'CRL', 'Total Certs'];
    const csvContent = [
      headers.join(','),
      ...data.map((row) =>
        [
          row.countryCode,
          row.mlsc,
          row.cscaSelfSigned,
          row.cscaLinkCert,
          row.dsc,
          row.dscNc,
          row.crl,
          row.totalCerts,
        ].join(',')
      ),
    ].join('\n');

    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = `country_statistics_${new Date().toISOString().split('T')[0]}.csv`;
    link.click();
  };

  const totals = data.reduce(
    (acc, row) => ({
      mlsc: acc.mlsc + row.mlsc,
      cscaSelfSigned: acc.cscaSelfSigned + row.cscaSelfSigned,
      cscaLinkCert: acc.cscaLinkCert + row.cscaLinkCert,
      dsc: acc.dsc + row.dsc,
      dscNc: acc.dscNc + row.dscNc,
      crl: acc.crl + row.crl,
      totalCerts: acc.totalCerts + row.totalCerts,
    }),
    { mlsc: 0, cscaSelfSigned: 0, cscaLinkCert: 0, dsc: 0, dscNc: 0, crl: 0, totalCerts: 0 }
  );

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm">
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-2xl w-full max-w-4xl max-h-[90vh] flex flex-col overflow-hidden m-4">
        {/* Header */}
        <div className="px-6 py-4 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between bg-gradient-to-r from-blue-50 to-purple-50 dark:from-gray-700 dark:to-gray-800">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-lg bg-gradient-to-r from-cyan-400 to-blue-500">
              <Globe className="w-6 h-6 text-white" />
            </div>
            <div>
              <h2 className="text-xl font-bold text-gray-900 dark:text-white">
                국가별 인증서 상세 통계
              </h2>
              <p className="text-sm text-gray-600 dark:text-gray-400">
                전체 {data.length}개 국가의 인증서 종류별 현황
              </p>
            </div>
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={exportToCSV}
              disabled={loading || data.length === 0}
              className="px-4 py-2 rounded-lg bg-green-500 hover:bg-green-600 disabled:bg-gray-300 disabled:cursor-not-allowed text-white text-sm font-medium flex items-center gap-2 transition-colors"
            >
              <Download className="w-4 h-4" />
              CSV 다운로드
            </button>
            <button
              onClick={onClose}
              className="p-2 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-700 transition-colors"
            >
              <X className="w-5 h-5 text-gray-500 dark:text-gray-400" />
            </button>
          </div>
        </div>

        {/* Content */}
        <div className="flex-1 overflow-auto">
          {loading ? (
            <div className="flex items-center justify-center py-20">
              <Loader2 className="w-10 h-10 animate-spin text-blue-500" />
            </div>
          ) : error ? (
            <div className="text-center py-20">
              <p className="text-red-500 font-medium">{error}</p>
              <button
                onClick={fetchData}
                className="mt-4 px-4 py-2 bg-blue-500 hover:bg-blue-600 text-white rounded-lg transition-colors"
              >
                다시 시도
              </button>
            </div>
          ) : data.length === 0 ? (
            <div className="text-center py-20 text-gray-500">
              데이터가 없습니다.
            </div>
          ) : (
              <table className="w-full border-collapse text-xs">
                <thead>
                  <tr className="bg-gray-100 dark:bg-gray-700">
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-left font-semibold text-gray-700 dark:text-gray-200 border-b-2 border-gray-300 dark:border-gray-600 w-8">
                      #
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-left font-semibold text-gray-700 dark:text-gray-200 border-b-2 border-gray-300 dark:border-gray-600 w-16">
                      국가
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-purple-600 dark:text-purple-300 border-b-2 border-gray-300 dark:border-gray-600">
                      MLSC
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-blue-600 dark:text-blue-300 border-b-2 border-gray-300 dark:border-gray-600">
                      CSCA(SS)
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-cyan-600 dark:text-cyan-300 border-b-2 border-gray-300 dark:border-gray-600">
                      CSCA(LC)
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-green-600 dark:text-green-300 border-b-2 border-gray-300 dark:border-gray-600">
                      DSC
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-amber-600 dark:text-amber-300 border-b-2 border-gray-300 dark:border-gray-600">
                      DSC_NC
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-red-600 dark:text-red-300 border-b-2 border-gray-300 dark:border-gray-600">
                      CRL
                    </th>
                    <th className="sticky top-0 z-10 bg-gray-100 dark:bg-gray-700 px-1.5 py-2 text-right font-semibold text-gray-700 dark:text-gray-200 border-b-2 border-gray-300 dark:border-gray-600">
                      총계
                    </th>
                  </tr>
                </thead>
                <tbody>
                  {data.map((row, index) => (
                    <tr
                      key={row.countryCode}
                      className="border-b border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors"
                    >
                      <td className="px-1.5 py-1.5 text-gray-400">
                        {index + 1}
                      </td>
                      <td className="px-1.5 py-1.5 font-medium text-gray-900 dark:text-white">
                        <div className="flex items-center gap-1">
                          <img
                            src={`/svg/${row.countryCode.toLowerCase()}.svg`}
                            alt={row.countryCode}
                            className="w-5 h-3.5 object-cover rounded shadow-sm border border-gray-200 dark:border-gray-600"
                            onError={(e) => {
                              (e.target as HTMLImageElement).style.display = 'none';
                            }}
                          />
                          {row.countryCode}
                        </div>
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums text-gray-700 dark:text-gray-300">
                        {row.mlsc > 0 ? (
                          <span className="text-purple-600 dark:text-purple-300 font-medium">{row.mlsc.toLocaleString()}</span>
                        ) : (
                          <span className="text-gray-300 dark:text-gray-600">-</span>
                        )}
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums text-gray-700 dark:text-gray-300">
                        {row.cscaSelfSigned > 0 ? (
                          <span className="text-blue-600 dark:text-blue-300 font-medium">{row.cscaSelfSigned.toLocaleString()}</span>
                        ) : (
                          <span className="text-gray-300 dark:text-gray-600">-</span>
                        )}
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums text-gray-700 dark:text-gray-300">
                        {row.cscaLinkCert > 0 ? (
                          <span className="text-cyan-600 dark:text-cyan-300 font-medium">{row.cscaLinkCert.toLocaleString()}</span>
                        ) : (
                          <span className="text-gray-300 dark:text-gray-600">-</span>
                        )}
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums text-gray-700 dark:text-gray-300">
                        {row.dsc > 0 ? (
                          <span className="text-green-600 dark:text-green-300 font-medium">{row.dsc.toLocaleString()}</span>
                        ) : (
                          <span className="text-gray-300 dark:text-gray-600">-</span>
                        )}
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums text-gray-700 dark:text-gray-300">
                        {row.dscNc > 0 ? (
                          <span className="text-amber-600 dark:text-amber-300 font-medium">{row.dscNc.toLocaleString()}</span>
                        ) : (
                          <span className="text-gray-300 dark:text-gray-600">-</span>
                        )}
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums text-gray-700 dark:text-gray-300">
                        {row.crl > 0 ? (
                          <span className="text-red-600 dark:text-red-300 font-medium">{row.crl.toLocaleString()}</span>
                        ) : (
                          <span className="text-gray-300 dark:text-gray-600">-</span>
                        )}
                      </td>
                      <td className="px-1.5 py-1.5 text-right tabular-nums font-bold text-gray-900 dark:text-white">
                        {row.totalCerts.toLocaleString()}
                      </td>
                    </tr>
                  ))}
                </tbody>
                <tfoot>
                  <tr className="bg-gradient-to-r from-blue-50 to-purple-50 dark:from-gray-700 dark:to-gray-800 border-t-2 border-gray-300 dark:border-gray-600">
                    <td className="px-1.5 py-2 font-bold text-gray-900 dark:text-white" colSpan={2}>
                      총계
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-purple-600 dark:text-purple-300">
                      {totals.mlsc.toLocaleString()}
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-blue-600 dark:text-blue-300">
                      {totals.cscaSelfSigned.toLocaleString()}
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-cyan-600 dark:text-cyan-300">
                      {totals.cscaLinkCert.toLocaleString()}
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-green-600 dark:text-green-300">
                      {totals.dsc.toLocaleString()}
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-amber-600 dark:text-amber-300">
                      {totals.dscNc.toLocaleString()}
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-red-600 dark:text-red-300">
                      {totals.crl.toLocaleString()}
                    </td>
                    <td className="px-1.5 py-2 text-right tabular-nums font-bold text-gray-900 dark:text-white text-sm">
                      {totals.totalCerts.toLocaleString()}
                    </td>
                  </tr>
                </tfoot>
              </table>
          )}
        </div>

        {/* Footer */}
        <div className="px-6 py-4 border-t border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-800/50">
          <div className="flex items-center justify-between text-xs text-gray-600 dark:text-gray-400">
            <div className="flex items-center gap-4">
              <span><strong>SS:</strong> Self-signed (자체 서명)</span>
              <span><strong>LC:</strong> Link Certificate (연결 인증서)</span>
            </div>
            <button
              onClick={onClose}
              className="px-4 py-2 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 text-gray-700 dark:text-gray-300 rounded-lg transition-colors font-medium"
            >
              닫기
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
