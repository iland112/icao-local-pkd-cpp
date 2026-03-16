import { useState, useEffect, useCallback } from 'react';
import {
  FileKey, Plus, Download, Trash2, Copy, Check, Loader2,
  ShieldCheck, X, Eye, Upload, Award,
} from 'lucide-react';
import { csrApiService, type CsrRequest, type CsrGenerateRequest } from '@/services/csrApi';

const STATUS_COLORS: Record<string, string> = {
  CREATED: 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300',
  SUBMITTED: 'bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-300',
  ISSUED: 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300',
  REVOKED: 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300',
};

const STATUS_LABELS: Record<string, string> = {
  CREATED: '생성됨',
  SUBMITTED: '제출됨',
  ISSUED: '발급됨',
  REVOKED: '폐기됨',
};

export default function CsrManagement() {
  const [csrList, setCsrList] = useState<CsrRequest[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [loading, setLoading] = useState(false);

  // Generate dialog
  const [generateOpen, setGenerateOpen] = useState(false);
  const [generating, setGenerating] = useState(false);
  const [genForm, setGenForm] = useState<CsrGenerateRequest>({
    countryCode: 'KR', organization: '', commonName: '', memo: '',
  });
  const [genResult, setGenResult] = useState<{ csrPem: string; id: string } | null>(null);

  // Detail dialog
  const [detailOpen, setDetailOpen] = useState(false);
  const [selectedCsr, setSelectedCsr] = useState<CsrRequest | null>(null);
  const [detailLoading, setDetailLoading] = useState(false);

  // Certificate registration dialog
  const [certRegOpen, setCertRegOpen] = useState(false);
  const [certRegCsrId, setCertRegCsrId] = useState<string>('');
  const [certPemInput, setCertPemInput] = useState('');
  const [registering, setRegistering] = useState(false);
  const [certRegError, setCertRegError] = useState('');

  // Delete confirm
  const [deleteId, setDeleteId] = useState<string | null>(null);
  const [deleting, setDeleting] = useState(false);

  // Copy feedback
  const [copied, setCopied] = useState(false);

  const pageSize = 10;

  const fetchList = useCallback(async () => {
    setLoading(true);
    try {
      const resp = await csrApiService.list(page, pageSize);
      setCsrList(resp.data.data);
      setTotal(resp.data.total);
    } catch {
      /* silent */
    } finally {
      setLoading(false);
    }
  }, [page]);

  useEffect(() => { fetchList(); }, [fetchList]);

  const handleGenerate = async () => {
    if (!genForm.countryCode && !genForm.organization && !genForm.commonName) return;
    setGenerating(true);
    try {
      const resp = await csrApiService.generate(genForm);
      if (resp.data.success && resp.data.data) {
        setGenResult({ csrPem: resp.data.data.csrPem, id: resp.data.data.id });
        fetchList();
      }
    } catch {
      /* silent */
    } finally {
      setGenerating(false);
    }
  };

  const handleViewDetail = async (id: string) => {
    setDetailLoading(true);
    setDetailOpen(true);
    try {
      const resp = await csrApiService.getById(id);
      if (resp.data.success) {
        setSelectedCsr(resp.data.data);
      }
    } catch {
      /* silent */
    } finally {
      setDetailLoading(false);
    }
  };

  const handleRegisterCert = async () => {
    if (!certRegCsrId || !certPemInput.trim()) return;
    setRegistering(true);
    setCertRegError('');
    try {
      const resp = await csrApiService.registerCertificate(certRegCsrId, certPemInput.trim());
      if (resp.data.success) {
        setCertRegOpen(false);
        setCertPemInput('');
        fetchList();
        // Refresh detail if open
        if (selectedCsr?.id === certRegCsrId) {
          handleViewDetail(certRegCsrId);
        }
      } else {
        setCertRegError(resp.data.error || '인증서 등록 실패');
      }
    } catch (err: unknown) {
      const axiosErr = err as { response?: { data?: { error?: string } } };
      setCertRegError(axiosErr.response?.data?.error || '인증서 등록 중 오류 발생');
    } finally {
      setRegistering(false);
    }
  };

  const handleExport = async (id: string, format: 'pem' | 'der') => {
    try {
      const resp = format === 'pem'
        ? await csrApiService.exportPem(id)
        : await csrApiService.exportDer(id);
      const blob = new Blob([resp.data]);
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = format === 'pem' ? 'request.csr' : 'request.der';
      a.click();
      URL.revokeObjectURL(url);
    } catch {
      /* silent */
    }
  };

  const handleDelete = async () => {
    if (!deleteId) return;
    setDeleting(true);
    try {
      await csrApiService.deleteById(deleteId);
      setDeleteId(null);
      fetchList();
    } catch {
      /* silent */
    } finally {
      setDeleting(false);
    }
  };

  const handleCopy = (text: string) => {
    navigator.clipboard.writeText(text);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const totalPages = Math.ceil(total / pageSize);

  return (
    <div className="px-4 lg:px-6 py-4 space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <div className="p-2 bg-indigo-50 dark:bg-indigo-900/30 rounded-lg">
            <FileKey className="w-5 h-5 text-indigo-600 dark:text-indigo-400" />
          </div>
          <div>
            <h1 className="text-lg font-bold text-gray-900 dark:text-gray-100">
              CSR 관리
            </h1>
            <p className="text-xs text-gray-500 dark:text-gray-400">
              ICAO PKD 인증서 서명 요청 (Certificate Signing Request)
            </p>
          </div>
        </div>
        <button
          onClick={() => { setGenerateOpen(true); setGenResult(null); }}
          className="flex items-center gap-1.5 px-3 py-1.5 bg-indigo-600 hover:bg-indigo-700 text-white text-sm rounded-lg transition-colors"
        >
          <Plus className="w-4 h-4" />
          CSR 생성
        </button>
      </div>

      {/* Info banner */}
      <div className="flex items-start gap-2 bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-3">
        <ShieldCheck className="w-4 h-4 text-blue-500 mt-0.5 shrink-0" />
        <div className="text-xs text-blue-700 dark:text-blue-300">
          <p className="font-medium">ICAO PKD 요구사항</p>
          <p className="mt-0.5">RSA 2048 bit 공개키 · SHA256withRSA 서명 · Base64(PEM) 인코딩 · Subject DN 제한 없음</p>
        </div>
      </div>

      {/* Table */}
      <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead className="bg-gray-50 dark:bg-gray-700/50">
              <tr>
                <th className="px-4 py-2.5 text-left font-medium text-gray-500 dark:text-gray-400">Subject DN</th>
                <th className="px-4 py-2.5 text-left font-medium text-gray-500 dark:text-gray-400">알고리즘</th>
                <th className="px-4 py-2.5 text-left font-medium text-gray-500 dark:text-gray-400">상태</th>
                <th className="px-4 py-2.5 text-left font-medium text-gray-500 dark:text-gray-400">생성일</th>
                <th className="px-4 py-2.5 text-left font-medium text-gray-500 dark:text-gray-400">생성자</th>
                <th className="px-4 py-2.5 text-center font-medium text-gray-500 dark:text-gray-400">작업</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-100 dark:divide-gray-700">
              {loading ? (
                <tr>
                  <td colSpan={6} className="py-8 text-center text-gray-400">
                    <Loader2 className="w-5 h-5 animate-spin mx-auto" />
                  </td>
                </tr>
              ) : csrList.length === 0 ? (
                <tr>
                  <td colSpan={6} className="py-8 text-center text-gray-400 text-sm">
                    생성된 CSR이 없습니다
                  </td>
                </tr>
              ) : csrList.map((csr) => (
                <tr key={csr.id} className="hover:bg-gray-50 dark:hover:bg-gray-700/30">
                  <td className="px-4 py-2.5">
                    <button onClick={() => handleViewDetail(csr.id)} className="text-indigo-600 dark:text-indigo-400 hover:underline text-left">
                      {csr.subjectDn}
                    </button>
                    {csr.memo && <p className="text-[10px] text-gray-400 mt-0.5">{csr.memo}</p>}
                  </td>
                  <td className="px-4 py-2.5 text-xs text-gray-600 dark:text-gray-400">
                    {csr.keyAlgorithm}
                  </td>
                  <td className="px-4 py-2.5">
                    <span className={`inline-flex px-2 py-0.5 rounded-full text-[10px] font-medium ${STATUS_COLORS[csr.status] || 'bg-gray-100 text-gray-700'}`}>
                      {STATUS_LABELS[csr.status] || csr.status}
                    </span>
                  </td>
                  <td className="px-4 py-2.5 text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">
                    {csr.createdAt?.substring(0, 19).replace('T', ' ')}
                  </td>
                  <td className="px-4 py-2.5 text-xs text-gray-500 dark:text-gray-400">
                    {csr.createdBy}
                  </td>
                  <td className="px-4 py-2.5">
                    <div className="flex items-center justify-center gap-1">
                      <button onClick={() => handleViewDetail(csr.id)} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded" title="상세 보기">
                        <Eye className="w-3.5 h-3.5 text-gray-500" />
                      </button>
                      <button onClick={() => handleExport(csr.id, 'pem')} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded" title="PEM 다운로드">
                        <Download className="w-3.5 h-3.5 text-blue-500" />
                      </button>
                      <button onClick={() => handleExport(csr.id, 'der')} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded" title="DER 다운로드">
                        <Download className="w-3.5 h-3.5 text-green-500" />
                      </button>
                      <button onClick={() => setDeleteId(csr.id)} className="p-1 hover:bg-red-50 dark:hover:bg-red-900/20 rounded" title="삭제">
                        <Trash2 className="w-3.5 h-3.5 text-red-400" />
                      </button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="flex items-center justify-between px-4 py-2 border-t border-gray-200 dark:border-gray-700">
            <span className="text-xs text-gray-500">총 {total}건</span>
            <div className="flex gap-1">
              {Array.from({ length: totalPages }, (_, i) => i + 1).map((p) => (
                <button
                  key={p}
                  onClick={() => setPage(p)}
                  className={`px-2 py-0.5 text-xs rounded ${p === page ? 'bg-indigo-600 text-white' : 'text-gray-500 hover:bg-gray-100 dark:hover:bg-gray-700'}`}
                >
                  {p}
                </button>
              ))}
            </div>
          </div>
        )}
      </div>

      {/* Generate Dialog */}
      {generateOpen && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={() => setGenerateOpen(false)}>
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-xl max-w-lg w-full mx-4" onClick={(e) => e.stopPropagation()}>
            <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700">
              <h2 className="text-sm font-semibold text-gray-900 dark:text-gray-100">CSR 생성</h2>
              <button onClick={() => setGenerateOpen(false)} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded">
                <X className="w-4 h-4 text-gray-500" />
              </button>
            </div>

            <div className="px-5 py-4 space-y-3">
              {!genResult ? (
                <>
                  {/* Algorithm info (read-only) */}
                  <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-3 text-xs space-y-1">
                    <div className="flex justify-between"><span className="text-gray-500">키 알고리즘</span><span className="font-mono font-medium">RSA-2048</span></div>
                    <div className="flex justify-between"><span className="text-gray-500">서명 알고리즘</span><span className="font-mono font-medium">SHA256withRSA</span></div>
                    <div className="flex justify-between"><span className="text-gray-500">인코딩</span><span className="font-mono font-medium">Base64 (PEM)</span></div>
                  </div>

                  {/* Subject DN fields */}
                  <div>
                    <label htmlFor="csr-country" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">Country (C)</label>
                    <input id="csr-country" type="text" maxLength={2} value={genForm.countryCode}
                      onChange={(e) => setGenForm({ ...genForm, countryCode: e.target.value.toUpperCase() })}
                      className="w-full px-3 py-1.5 text-sm border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:ring-1 focus:ring-indigo-500"
                      placeholder="KR" />
                  </div>
                  <div>
                    <label htmlFor="csr-org" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">Organization (O)</label>
                    <input id="csr-org" type="text" value={genForm.organization}
                      onChange={(e) => setGenForm({ ...genForm, organization: e.target.value })}
                      className="w-full px-3 py-1.5 text-sm border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:ring-1 focus:ring-indigo-500"
                      placeholder="Government of Korea" />
                  </div>
                  <div>
                    <label htmlFor="csr-cn" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">Common Name (CN)</label>
                    <input id="csr-cn" type="text" value={genForm.commonName}
                      onChange={(e) => setGenForm({ ...genForm, commonName: e.target.value })}
                      className="w-full px-3 py-1.5 text-sm border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:ring-1 focus:ring-indigo-500"
                      placeholder="Korea PKD" />
                  </div>
                  <div>
                    <label htmlFor="csr-memo" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">메모</label>
                    <input id="csr-memo" type="text" value={genForm.memo}
                      onChange={(e) => setGenForm({ ...genForm, memo: e.target.value })}
                      className="w-full px-3 py-1.5 text-sm border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:ring-1 focus:ring-indigo-500"
                      placeholder="ICAO PKD 등록용" />
                  </div>
                </>
              ) : (
                /* Result */
                <div className="space-y-3">
                  <div className="flex items-center gap-2 text-green-600">
                    <ShieldCheck className="w-5 h-5" />
                    <span className="text-sm font-medium">CSR 생성 완료</span>
                  </div>
                  <div className="relative">
                    <pre className="bg-gray-50 dark:bg-gray-900 border border-gray-200 dark:border-gray-700 rounded-lg p-3 text-[10px] font-mono overflow-x-auto max-h-48 overflow-y-auto whitespace-pre-wrap break-all">
                      {genResult.csrPem}
                    </pre>
                    <button
                      onClick={() => handleCopy(genResult.csrPem)}
                      className="absolute top-2 right-2 p-1 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded hover:bg-gray-100 dark:hover:bg-gray-700"
                      title="복사"
                    >
                      {copied ? <Check className="w-3 h-3 text-green-500" /> : <Copy className="w-3 h-3 text-gray-500" />}
                    </button>
                  </div>
                  <div className="flex gap-2">
                    <button onClick={() => handleExport(genResult.id, 'pem')} className="flex-1 flex items-center justify-center gap-1.5 px-3 py-1.5 bg-blue-50 hover:bg-blue-100 dark:bg-blue-900/20 dark:hover:bg-blue-900/30 text-blue-700 dark:text-blue-300 text-xs rounded-lg transition-colors">
                      <Download className="w-3.5 h-3.5" /> PEM 다운로드
                    </button>
                    <button onClick={() => handleExport(genResult.id, 'der')} className="flex-1 flex items-center justify-center gap-1.5 px-3 py-1.5 bg-green-50 hover:bg-green-100 dark:bg-green-900/20 dark:hover:bg-green-900/30 text-green-700 dark:text-green-300 text-xs rounded-lg transition-colors">
                      <Download className="w-3.5 h-3.5" /> DER 다운로드
                    </button>
                  </div>
                </div>
              )}
            </div>

            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button onClick={() => setGenerateOpen(false)} className="px-3 py-1.5 text-xs bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 dark:hover:bg-gray-600 rounded-lg text-gray-700 dark:text-gray-300">
                닫기
              </button>
              {!genResult && (
                <button
                  onClick={handleGenerate}
                  disabled={generating || (!genForm.countryCode && !genForm.organization && !genForm.commonName)}
                  className="flex items-center gap-1.5 px-3 py-1.5 text-xs bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  {generating ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <FileKey className="w-3.5 h-3.5" />}
                  {generating ? '생성 중...' : 'CSR 생성'}
                </button>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Detail Dialog */}
      {detailOpen && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={() => setDetailOpen(false)}>
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-xl max-w-lg w-full mx-4 max-h-[90vh] flex flex-col" onClick={(e) => e.stopPropagation()}>
            <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700">
              <h2 className="text-sm font-semibold text-gray-900 dark:text-gray-100">CSR 상세</h2>
              <button onClick={() => setDetailOpen(false)} className="p-1 hover:bg-gray-100 dark:hover:bg-gray-700 rounded">
                <X className="w-4 h-4 text-gray-500" />
              </button>
            </div>
            <div className="overflow-y-auto flex-1 px-5 py-4 space-y-3">
              {detailLoading ? (
                <div className="flex items-center justify-center py-8">
                  <Loader2 className="w-5 h-5 animate-spin text-gray-400" />
                </div>
              ) : selectedCsr && (
                <>
                  <div className="grid grid-cols-2 gap-2 text-xs">
                    <div><span className="text-gray-500">Subject DN</span><p className="font-medium mt-0.5">{selectedCsr.subjectDn}</p></div>
                    <div><span className="text-gray-500">상태</span><p className="mt-0.5"><span className={`inline-flex px-2 py-0.5 rounded-full text-[10px] font-medium ${STATUS_COLORS[selectedCsr.status]}`}>{STATUS_LABELS[selectedCsr.status]}</span></p></div>
                    <div><span className="text-gray-500">키 알고리즘</span><p className="font-mono mt-0.5">{selectedCsr.keyAlgorithm}</p></div>
                    <div><span className="text-gray-500">서명 알고리즘</span><p className="font-mono mt-0.5">{selectedCsr.signatureAlgorithm}</p></div>
                    <div className="col-span-2"><span className="text-gray-500">공개키 핑거프린트</span><p className="font-mono mt-0.5 text-[10px] break-all">{selectedCsr.publicKeyFingerprint}</p></div>
                    <div><span className="text-gray-500">생성자</span><p className="mt-0.5">{selectedCsr.createdBy}</p></div>
                    <div><span className="text-gray-500">생성일</span><p className="mt-0.5">{selectedCsr.createdAt?.substring(0, 19).replace('T', ' ')}</p></div>
                    {selectedCsr.memo && <div className="col-span-2"><span className="text-gray-500">메모</span><p className="mt-0.5">{selectedCsr.memo}</p></div>}
                  </div>
                  {selectedCsr.csrPem && (
                    <div className="relative">
                      <p className="text-xs text-gray-500 mb-1">CSR (PEM)</p>
                      <pre className="bg-gray-50 dark:bg-gray-900 border border-gray-200 dark:border-gray-700 rounded-lg p-3 text-[10px] font-mono overflow-x-auto max-h-40 overflow-y-auto whitespace-pre-wrap break-all">
                        {selectedCsr.csrPem}
                      </pre>
                      <button
                        onClick={() => handleCopy(selectedCsr.csrPem!)}
                        className="absolute top-6 right-2 p-1 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded hover:bg-gray-100 dark:hover:bg-gray-700"
                      >
                        {copied ? <Check className="w-3 h-3 text-green-500" /> : <Copy className="w-3 h-3 text-gray-500" />}
                      </button>
                    </div>
                  )}
                  <div className="flex gap-2">
                    <button onClick={() => handleExport(selectedCsr.id, 'pem')} className="flex-1 flex items-center justify-center gap-1.5 px-3 py-1.5 bg-blue-50 hover:bg-blue-100 dark:bg-blue-900/20 text-blue-700 dark:text-blue-300 text-xs rounded-lg">
                      <Download className="w-3.5 h-3.5" /> PEM 다운로드
                    </button>
                    <button onClick={() => handleExport(selectedCsr.id, 'der')} className="flex-1 flex items-center justify-center gap-1.5 px-3 py-1.5 bg-green-50 hover:bg-green-100 dark:bg-green-900/20 text-green-700 dark:text-green-300 text-xs rounded-lg">
                      <Download className="w-3.5 h-3.5" /> DER 다운로드
                    </button>
                  </div>

                  {/* Issued certificate info */}
                  {selectedCsr.status === 'ISSUED' && selectedCsr.certificate_issuer_dn && (
                    <div className="border border-green-200 dark:border-green-800 rounded-lg p-3 bg-green-50 dark:bg-green-900/20 space-y-1">
                      <div className="flex items-center gap-1.5 text-green-700 dark:text-green-300 text-xs font-medium">
                        <Award className="w-3.5 h-3.5" /> 발급된 인증서
                      </div>
                      <div className="grid grid-cols-2 gap-1 text-[10px]">
                        <div><span className="text-gray-500">발급자</span><p className="font-mono">{selectedCsr.certificate_issuer_dn}</p></div>
                        <div><span className="text-gray-500">일련번호</span><p className="font-mono">{selectedCsr.certificate_serial}</p></div>
                        <div><span className="text-gray-500">유효 시작</span><p>{selectedCsr.certificate_not_before?.substring(0, 19)}</p></div>
                        <div><span className="text-gray-500">유효 만료</span><p>{selectedCsr.certificate_not_after?.substring(0, 19)}</p></div>
                      </div>
                    </div>
                  )}

                  {/* Register certificate button (only for CREATED/SUBMITTED) */}
                  {selectedCsr.status !== 'ISSUED' && selectedCsr.status !== 'REVOKED' && (
                    <button
                      onClick={() => { setCertRegCsrId(selectedCsr.id); setCertRegOpen(true); setCertPemInput(''); setCertRegError(''); }}
                      className="w-full flex items-center justify-center gap-1.5 px-3 py-2 bg-amber-50 hover:bg-amber-100 dark:bg-amber-900/20 dark:hover:bg-amber-900/30 text-amber-700 dark:text-amber-300 text-xs rounded-lg border border-amber-200 dark:border-amber-800 transition-colors"
                    >
                      <Upload className="w-3.5 h-3.5" /> ICAO 발급 인증서 등록
                    </button>
                  )}
                </>
              )}
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end">
              <button onClick={() => setDetailOpen(false)} className="px-3 py-1.5 text-xs bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 rounded-lg text-gray-700 dark:text-gray-300">닫기</button>
            </div>
          </div>
        </div>
      )}

      {/* Delete Confirm Dialog */}
      {deleteId && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={() => setDeleteId(null)}>
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-xl max-w-sm w-full mx-4 p-5" onClick={(e) => e.stopPropagation()}>
            <p className="text-sm text-gray-900 dark:text-gray-100 font-medium">CSR을 삭제하시겠습니까?</p>
            <p className="text-xs text-gray-500 mt-1">삭제된 CSR은 복구할 수 없습니다. 개인키도 함께 삭제됩니다.</p>
            <div className="flex justify-end gap-2 mt-4">
              <button onClick={() => setDeleteId(null)} className="px-3 py-1.5 text-xs bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 rounded-lg text-gray-700 dark:text-gray-300">취소</button>
              <button onClick={handleDelete} disabled={deleting} className="flex items-center gap-1 px-3 py-1.5 text-xs bg-red-600 hover:bg-red-700 text-white rounded-lg disabled:opacity-50">
                {deleting ? <Loader2 className="w-3 h-3 animate-spin" /> : <Trash2 className="w-3 h-3" />}
                삭제
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Certificate Registration Dialog */}
      {certRegOpen && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/50" onClick={() => setCertRegOpen(false)}>
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-xl max-w-lg w-full mx-4" onClick={(e) => e.stopPropagation()}>
            <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700 bg-amber-50 dark:bg-amber-900/20 rounded-t-lg">
              <div className="flex items-center gap-2">
                <Award className="w-4 h-4 text-amber-600" />
                <h2 className="text-sm font-semibold text-gray-900 dark:text-gray-100">ICAO 발급 인증서 등록</h2>
              </div>
              <button onClick={() => setCertRegOpen(false)} className="p-1 hover:bg-amber-100 dark:hover:bg-amber-800/30 rounded">
                <X className="w-4 h-4 text-gray-500" />
              </button>
            </div>

            <div className="px-5 py-4 space-y-3">
              <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg p-2.5 text-xs text-blue-700 dark:text-blue-300">
                ICAO에서 발급받은 인증서를 PEM 형식으로 붙여넣으세요. 인증서의 공개키가 CSR의 공개키와 일치하는지 자동 검증됩니다.
              </div>

              <div>
                <label htmlFor="cert-pem" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">인증서 PEM</label>
                <textarea
                  id="cert-pem"
                  rows={10}
                  value={certPemInput}
                  onChange={(e) => setCertPemInput(e.target.value)}
                  className="w-full px-3 py-2 text-[11px] font-mono border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 focus:ring-1 focus:ring-amber-500 resize-none"
                  placeholder="-----BEGIN CERTIFICATE-----&#10;MIIDxTCCAq2gAwIBAgI...&#10;-----END CERTIFICATE-----"
                />
              </div>

              {certRegError && (
                <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg p-2.5 text-xs text-red-700 dark:text-red-300">
                  {certRegError}
                </div>
              )}
            </div>

            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button onClick={() => setCertRegOpen(false)} className="px-3 py-1.5 text-xs bg-gray-100 dark:bg-gray-700 hover:bg-gray-200 rounded-lg text-gray-700 dark:text-gray-300">
                취소
              </button>
              <button
                onClick={handleRegisterCert}
                disabled={registering || !certPemInput.trim()}
                className="flex items-center gap-1.5 px-3 py-1.5 text-xs bg-amber-600 hover:bg-amber-700 text-white rounded-lg disabled:opacity-50 disabled:cursor-not-allowed"
              >
                {registering ? <Loader2 className="w-3.5 h-3.5 animate-spin" /> : <Award className="w-3.5 h-3.5" />}
                {registering ? '등록 중...' : '인증서 등록'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
