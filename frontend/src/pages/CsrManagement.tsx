import { useState, useEffect, useCallback } from 'react';
import {
  FileKey, Plus, Download, Trash2, Copy, Check, Loader2,
  ShieldCheck, X, Eye, Upload, Award, Import, RotateCcw,
} from 'lucide-react';
import { csrApiService, type CsrRecord, type CsrGenerateRequest } from '@/services/csrApi';

const STATUS_COLORS: Record<string, string> = {
  CREATED: 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-300',
  SUBMITTED: 'bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-300',
  ISSUED: 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-300',
  REVOKED: 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-300',
};

const STATUS_LABELS: Record<string, string> = {
  CREATED: '생성됨',
  SUBMITTED: '제출됨',
  ISSUED: '발급됨',
  REVOKED: '폐기됨',
};

const inputClass = "w-full px-3 py-2 text-sm border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500 transition-colors";
const textareaClass = "w-full px-3 py-2 text-[11px] font-mono border border-gray-200 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:ring-2 focus:ring-indigo-500 focus:border-indigo-500 resize-none transition-colors";
const labelClass = "block text-xs font-medium text-gray-600 dark:text-gray-400 mb-1.5";
const btnSecondary = "px-4 py-2 text-sm text-gray-600 dark:text-gray-300 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors";
const btnDanger = "flex items-center gap-1.5 px-4 py-2 text-sm text-white bg-red-500 hover:bg-red-600 rounded-xl transition-colors disabled:opacity-50";

export default function CsrManagement() {
  const [csrList, setCsrList] = useState<CsrRecord[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [loading, setLoading] = useState(false);

  const [generateOpen, setGenerateOpen] = useState(false);
  const [generating, setGenerating] = useState(false);
  const [genForm, setGenForm] = useState<CsrGenerateRequest>({ countryCode: 'KR', organization: '', commonName: '', memo: '' });
  const [genResult, setGenResult] = useState<{ csrPem: string; id: string } | null>(null);

  const [importOpen, setImportOpen] = useState(false);
  const [importing, setImporting] = useState(false);
  const [importCsrPem, setImportCsrPem] = useState('');
  const [importKeyPem, setImportKeyPem] = useState('');
  const [importMemo, setImportMemo] = useState('');
  const [importError, setImportError] = useState('');

  const [detailOpen, setDetailOpen] = useState(false);
  const [selectedCsr, setSelectedCsr] = useState<CsrRecord | null>(null);
  const [detailLoading, setDetailLoading] = useState(false);

  const [certRegOpen, setCertRegOpen] = useState(false);
  const [certRegCsrId, setCertRegCsrId] = useState('');
  const [certPemInput, setCertPemInput] = useState('');
  const [registering, setRegistering] = useState(false);
  const [certRegError, setCertRegError] = useState('');

  const [deleteId, setDeleteId] = useState<string | null>(null);
  const [deleting, setDeleting] = useState(false);
  const [copied, setCopied] = useState(false);

  // Filters
  const [filterStatus, setFilterStatus] = useState('');
  const [filterCreatedBy, setFilterCreatedBy] = useState('');
  const [filterDateFrom, setFilterDateFrom] = useState('');
  const [filterDateTo, setFilterDateTo] = useState('');

  const pageSize = 10;

  const fetchList = useCallback(async () => {
    setLoading(true);
    try { const r = await csrApiService.list(page, pageSize); setCsrList(r.data.data); setTotal(r.data.total); } catch {} finally { setLoading(false); }
  }, [page]);

  useEffect(() => { fetchList(); }, [fetchList]);

  const handleGenerate = async () => {
    if (!genForm.countryCode && !genForm.organization && !genForm.commonName) return;
    setGenerating(true);
    try { const r = await csrApiService.generate(genForm); if (r.data.success && r.data.data) { setGenResult({ csrPem: r.data.data.csrPem, id: r.data.data.id }); fetchList(); } } catch {} finally { setGenerating(false); }
  };

  const handleImport = async () => {
    if (!importCsrPem.trim() || !importKeyPem.trim()) return;
    setImporting(true); setImportError('');
    try {
      const r = await csrApiService.import({ csrPem: importCsrPem.trim(), privateKeyPem: importKeyPem.trim(), memo: importMemo });
      if (r.data.success) { setImportOpen(false); setImportCsrPem(''); setImportKeyPem(''); setImportMemo(''); fetchList(); }
      else setImportError(r.data.error || 'Import 실패');
    } catch (e: unknown) { setImportError((e as { response?: { data?: { error?: string } } }).response?.data?.error || 'Import 중 오류 발생'); } finally { setImporting(false); }
  };

  const handleViewDetail = async (id: string) => {
    setDetailLoading(true); setDetailOpen(true);
    try { const r = await csrApiService.getById(id); if (r.data.success) setSelectedCsr(r.data.data); } catch {} finally { setDetailLoading(false); }
  };

  const handleRegisterCert = async () => {
    if (!certRegCsrId || !certPemInput.trim()) return;
    setRegistering(true); setCertRegError('');
    try {
      const r = await csrApiService.registerCertificate(certRegCsrId, certPemInput.trim());
      if (r.data.success) { setCertRegOpen(false); setCertPemInput(''); fetchList(); if (selectedCsr?.id === certRegCsrId) handleViewDetail(certRegCsrId); }
      else setCertRegError(r.data.error || '인증서 등록 실패');
    } catch (e: unknown) { setCertRegError((e as { response?: { data?: { error?: string } } }).response?.data?.error || '인증서 등록 중 오류 발생'); } finally { setRegistering(false); }
  };

  const handleExportPem = async (id: string) => {
    try { const r = await csrApiService.exportPem(id); const u = URL.createObjectURL(new Blob([r.data])); const a = document.createElement('a'); a.href = u; a.download = 'request.csr'; a.click(); URL.revokeObjectURL(u); } catch {}
  };

  const handleDelete = async () => {
    if (!deleteId) return; setDeleting(true);
    try { await csrApiService.deleteById(deleteId); setDeleteId(null); fetchList(); } catch {} finally { setDeleting(false); }
  };

  const handleCopy = (text: string) => { navigator.clipboard.writeText(text); setCopied(true); setTimeout(() => setCopied(false), 2000); };

  const resetFilters = () => { setFilterStatus(''); setFilterCreatedBy(''); setFilterDateFrom(''); setFilterDateTo(''); };
  const hasFilters = filterStatus || filterCreatedBy || filterDateFrom || filterDateTo;

  const filteredList = csrList.filter((csr) => {
    if (filterStatus && csr.status !== filterStatus) return false;
    if (filterCreatedBy && !(csr.created_by || '').toLowerCase().includes(filterCreatedBy.toLowerCase())) return false;
    if (filterDateFrom && csr.created_at && csr.created_at.substring(0, 10) < filterDateFrom) return false;
    if (filterDateTo && csr.created_at && csr.created_at.substring(0, 10) > filterDateTo) return false;
    return true;
  });

  const totalPages = Math.ceil(total / pageSize);

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* ── Header ── */}
      <div className="flex items-center justify-between flex-wrap gap-4">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-lg">
            <FileKey className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">CSR 관리</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">ICAO PKD 인증서 서명 요청 (Certificate Signing Request)</p>
          </div>
        </div>
        <div className="flex gap-2">
          <button onClick={() => { setImportOpen(true); setImportError(''); setImportCsrPem(''); setImportKeyPem(''); setImportMemo(''); }}
            className="flex items-center gap-2 px-4 py-2.5 text-sm text-gray-600 dark:text-gray-300 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors font-medium">
            <Import className="w-4 h-4" /> CSR 가져오기
          </button>
          <button onClick={() => { setGenerateOpen(true); setGenResult(null); setGenForm(f => ({ ...f, organization: '', commonName: '', memo: '' })); }}
            className="flex items-center gap-2 px-4 py-2.5 bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 text-white rounded-xl font-medium shadow-md hover:shadow-lg transition-all text-sm">
            <Plus className="w-4 h-4" /> CSR 생성
          </button>
        </div>
      </div>

      {/* ── ICAO Info ── */}
      <div className="flex items-start gap-3 bg-gradient-to-r from-blue-50 to-indigo-50 dark:from-blue-900/20 dark:to-indigo-900/20 border border-blue-200 dark:border-blue-800 rounded-xl p-4">
        <ShieldCheck className="w-5 h-5 text-blue-600 dark:text-blue-400 mt-0.5 shrink-0" />
        <div className="text-sm text-blue-700 dark:text-blue-300">
          <p className="font-semibold">ICAO PKD 요구사항</p>
          <p className="mt-1 text-xs">RSA 2048 bit 공개키 · SHA256withRSA 서명 · Base64(PEM) 인코딩 · Subject DN 제한 없음</p>
        </div>
      </div>

      {/* ── Filters ── */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
        <div className="flex flex-wrap items-end gap-3">
          <div className="flex-1 min-w-[120px]">
            <label htmlFor="filter-status" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">상태</label>
            <select id="filter-status" value={filterStatus} onChange={(e) => setFilterStatus(e.target.value)}
              className={inputClass}>
              <option value="">전체</option>
              <option value="CREATED">생성됨</option>
              <option value="SUBMITTED">제출됨</option>
              <option value="ISSUED">발급됨</option>
              <option value="REVOKED">폐기됨</option>
            </select>
          </div>
          <div className="flex-1 min-w-[120px]">
            <label htmlFor="filter-created-by" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">생성자</label>
            <input id="filter-created-by" type="text" value={filterCreatedBy} onChange={(e) => setFilterCreatedBy(e.target.value)}
              className={inputClass} placeholder="검색..." />
          </div>
          <div className="flex-1 min-w-[130px]">
            <label htmlFor="filter-date-from" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">생성일 (시작)</label>
            <input id="filter-date-from" type="date" value={filterDateFrom} onChange={(e) => setFilterDateFrom(e.target.value)}
              className={inputClass} />
          </div>
          <div className="flex-1 min-w-[130px]">
            <label htmlFor="filter-date-to" className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">생성일 (종료)</label>
            <input id="filter-date-to" type="date" value={filterDateTo} onChange={(e) => setFilterDateTo(e.target.value)}
              className={inputClass} />
          </div>
          {hasFilters && (
            <button onClick={resetFilters}
              className="flex items-center gap-1.5 px-3 py-2 text-xs text-gray-500 hover:text-gray-700 dark:hover:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-lg transition-colors">
              <RotateCcw className="w-3.5 h-3.5" /> 초기화
            </button>
          )}
        </div>
      </div>

      {/* ── Table Card ── */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead className="bg-slate-100 dark:bg-gray-700">
              <tr>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase">Subject DN</th>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase">알고리즘</th>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase">상태</th>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase">생성일</th>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase">생성자</th>
                <th className="px-6 py-3 text-center text-xs font-medium text-gray-500 dark:text-gray-400 uppercase">작업</th>
              </tr>
            </thead>
            <tbody>
              {loading ? (
                <tr><td colSpan={6} className="py-12 text-center"><div className="animate-spin rounded-full h-8 w-8 border-b-2 border-indigo-600 mx-auto" /></td></tr>
              ) : filteredList.length === 0 ? (
                <tr><td colSpan={6} className="py-12 text-center text-gray-500 dark:text-gray-400">{hasFilters ? '필터 조건에 맞는 CSR이 없습니다' : '생성된 CSR이 없습니다'}</td></tr>
              ) : filteredList.map((csr) => (
                <tr key={csr.id} className="hover:bg-gray-50 dark:hover:bg-gray-900/30 transition-colors border-b border-gray-100 dark:border-gray-700">
                  <td className="px-6 py-3">
                    <button onClick={() => handleViewDetail(csr.id)} className="text-indigo-600 dark:text-indigo-400 hover:underline text-left font-medium">
                      {csr.subject_dn}
                    </button>
                    {csr.memo && <p className="text-[10px] text-gray-400 mt-0.5">{csr.memo}</p>}
                  </td>
                  <td className="px-6 py-3 text-xs font-mono text-gray-600 dark:text-gray-400">{csr.key_algorithm}</td>
                  <td className="px-6 py-3">
                    <span className={`inline-flex px-2.5 py-0.5 rounded-full text-xs font-medium ${STATUS_COLORS[csr.status] || 'bg-gray-100 text-gray-600'}`}>
                      {STATUS_LABELS[csr.status] || csr.status}
                    </span>
                  </td>
                  <td className="px-6 py-3 text-xs text-gray-500 dark:text-gray-400 whitespace-nowrap">{csr.created_at?.substring(0, 19).replace('T', ' ')}</td>
                  <td className="px-6 py-3 text-xs text-gray-500 dark:text-gray-400">{csr.created_by}</td>
                  <td className="px-6 py-3">
                    <div className="flex items-center justify-center gap-1">
                      <button onClick={() => handleViewDetail(csr.id)} className="p-2 text-gray-400 hover:text-indigo-600 rounded-lg transition-colors" title="상세"><Eye className="w-4 h-4" /></button>
                      <button onClick={() => handleExportPem(csr.id)} className="p-2 text-gray-400 hover:text-blue-600 rounded-lg transition-colors" title="PEM"><Download className="w-4 h-4" /></button>
                      <button onClick={() => setDeleteId(csr.id)} className="p-2 text-gray-400 hover:text-red-500 rounded-lg transition-colors" title="삭제"><Trash2 className="w-4 h-4" /></button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        {totalPages > 1 && (
          <div className="flex items-center justify-between px-6 py-3 border-t border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-900/50">
            <span className="text-xs text-gray-500">총 {hasFilters ? `${filteredList.length}/${total}` : total}건</span>
            <div className="flex gap-1">
              {Array.from({ length: totalPages }, (_, i) => i + 1).map((p) => (
                <button key={p} onClick={() => setPage(p)}
                  className={`px-2.5 py-1 text-xs rounded-lg transition-colors ${p === page ? 'bg-indigo-600 text-white' : 'text-gray-500 hover:bg-gray-200 dark:hover:bg-gray-700'}`}>{p}</button>
              ))}
            </div>
          </div>
        )}
      </div>

      {/* ── Generate Dialog ── */}
      {generateOpen && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setGenerateOpen(false)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-lg bg-white dark:bg-gray-800 rounded-xl shadow-2xl" onClick={(e) => e.stopPropagation()}>
              <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200 dark:border-gray-700">
                <div className="flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-gradient-to-br from-indigo-500 to-purple-600"><FileKey className="w-4 h-4 text-white" /></div>
                  <h2 className="text-lg font-semibold text-gray-900 dark:text-white">CSR 생성</h2>
                </div>
                <button onClick={() => setGenerateOpen(false)} className="p-2 text-gray-400 hover:text-gray-600 rounded-lg transition-colors"><X className="w-5 h-5" /></button>
              </div>
              <div className="px-6 py-5 space-y-4">
                {!genResult ? (
                  <>
                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4 text-sm space-y-2">
                      <div className="flex justify-between"><span className="text-gray-500">키 알고리즘</span><span className="font-mono font-semibold text-gray-900 dark:text-white">RSA-2048</span></div>
                      <div className="flex justify-between"><span className="text-gray-500">서명 알고리즘</span><span className="font-mono font-semibold text-gray-900 dark:text-white">SHA256withRSA</span></div>
                      <div className="flex justify-between"><span className="text-gray-500">인코딩</span><span className="font-mono font-semibold text-gray-900 dark:text-white">Base64 (PEM)</span></div>
                    </div>
                    <div><label htmlFor="csr-country" className={labelClass}>Country (C)</label><input id="csr-country" type="text" maxLength={2} value={genForm.countryCode} onChange={(e) => setGenForm({ ...genForm, countryCode: e.target.value.toUpperCase() })} className={inputClass} placeholder="KR" /></div>
                    <div><label htmlFor="csr-org" className={labelClass}>Organization (O)</label><input id="csr-org" type="text" value={genForm.organization} onChange={(e) => setGenForm({ ...genForm, organization: e.target.value })} className={inputClass} placeholder="Government of Korea" /></div>
                    <div><label htmlFor="csr-cn" className={labelClass}>Common Name (CN)</label><input id="csr-cn" type="text" value={genForm.commonName} onChange={(e) => setGenForm({ ...genForm, commonName: e.target.value })} className={inputClass} placeholder="Korea PKD" /></div>
                    <div><label htmlFor="csr-memo" className={labelClass}>메모</label><input id="csr-memo" type="text" value={genForm.memo} onChange={(e) => setGenForm({ ...genForm, memo: e.target.value })} className={inputClass} placeholder="ICAO PKD 등록용" /></div>
                  </>
                ) : (
                  <div className="space-y-4">
                    <div className="flex items-center gap-3 p-3 bg-green-50 dark:bg-green-900/20 rounded-xl border border-green-200 dark:border-green-800">
                      <ShieldCheck className="w-5 h-5 text-green-600" /><span className="text-sm font-semibold text-green-700 dark:text-green-300">CSR 생성 완료</span>
                    </div>
                    <div className="relative">
                      <pre className="bg-gray-50 dark:bg-gray-900 border border-gray-200 dark:border-gray-700 rounded-xl p-4 text-[10px] font-mono overflow-x-auto max-h-48 overflow-y-auto whitespace-pre-wrap break-all">{genResult.csrPem}</pre>
                      <button onClick={() => handleCopy(genResult.csrPem)} className="absolute top-3 right-3 p-1.5 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-100 shadow-sm" title="복사">
                        {copied ? <Check className="w-3.5 h-3.5 text-green-500" /> : <Copy className="w-3.5 h-3.5 text-gray-500" />}
                      </button>
                    </div>
                    <button onClick={() => handleExportPem(genResult.id)} className="w-full flex items-center justify-center gap-2 px-4 py-2.5 bg-gradient-to-r from-blue-500 to-indigo-500 hover:from-blue-600 hover:to-indigo-600 text-white rounded-xl font-medium shadow-md transition-all text-sm">
                      <Download className="w-4 h-4" /> PEM 다운로드
                    </button>
                  </div>
                )}
              </div>
              <div className="flex justify-end gap-2 px-6 py-4 border-t border-gray-200 dark:border-gray-700">
                <button onClick={() => setGenerateOpen(false)} className={btnSecondary}>닫기</button>
                {!genResult && (
                  <button onClick={handleGenerate} disabled={generating || (!genForm.countryCode && !genForm.organization && !genForm.commonName)}
                    className="flex items-center gap-2 px-4 py-2 bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 text-white rounded-xl font-medium shadow-md transition-all text-sm disabled:opacity-50 disabled:cursor-not-allowed">
                    {generating ? <Loader2 className="w-4 h-4 animate-spin" /> : <FileKey className="w-4 h-4" />}
                    {generating ? '생성 중...' : 'CSR 생성'}
                  </button>
                )}
              </div>
            </div>
          </div>
        </div>
      )}

      {/* ── Import Dialog ── */}
      {importOpen && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setImportOpen(false)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-lg bg-white dark:bg-gray-800 rounded-xl shadow-2xl max-h-[90vh] flex flex-col" onClick={(e) => e.stopPropagation()}>
              <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200 dark:border-gray-700">
                <div className="flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-gradient-to-br from-gray-500 to-gray-700"><Import className="w-4 h-4 text-white" /></div>
                  <h2 className="text-lg font-semibold text-gray-900 dark:text-white">CSR 가져오기</h2>
                </div>
                <button onClick={() => setImportOpen(false)} className="p-2 text-gray-400 hover:text-gray-600 rounded-lg transition-colors"><X className="w-5 h-5" /></button>
              </div>
              <div className="overflow-y-auto flex-1 px-6 py-5 space-y-4">
                <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-xl p-3 text-xs text-blue-700 dark:text-blue-300">
                  외부에서 생성된 CSR 파일과 개인키 파일을 PEM 형식으로 붙여넣으세요. CSR 서명 검증 및 키 매칭을 자동 수행합니다.
                </div>
                <div><label htmlFor="import-csr" className={labelClass}>CSR (PEM)</label><textarea id="import-csr" rows={6} value={importCsrPem} onChange={(e) => setImportCsrPem(e.target.value)} className={textareaClass} placeholder={"-----BEGIN CERTIFICATE REQUEST-----\n...\n-----END CERTIFICATE REQUEST-----"} /></div>
                <div><label htmlFor="import-key" className={labelClass}>개인키 (PEM)</label><textarea id="import-key" rows={6} value={importKeyPem} onChange={(e) => setImportKeyPem(e.target.value)} className={textareaClass} placeholder={"-----BEGIN PRIVATE KEY-----\n...\n-----END PRIVATE KEY-----"} /></div>
                <div><label htmlFor="import-memo" className={labelClass}>메모 (선택)</label><input id="import-memo" type="text" value={importMemo} onChange={(e) => setImportMemo(e.target.value)} className={inputClass} placeholder="외부 생성 CSR" /></div>
                {importError && <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl p-3 text-xs text-red-700 dark:text-red-300">{importError}</div>}
              </div>
              <div className="flex justify-end gap-2 px-6 py-4 border-t border-gray-200 dark:border-gray-700">
                <button onClick={() => setImportOpen(false)} className={btnSecondary}>취소</button>
                <button onClick={handleImport} disabled={importing || !importCsrPem.trim() || !importKeyPem.trim()}
                  className="flex items-center gap-2 px-4 py-2 bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 text-white rounded-xl font-medium shadow-md transition-all text-sm disabled:opacity-50 disabled:cursor-not-allowed">
                  {importing ? <Loader2 className="w-4 h-4 animate-spin" /> : <Import className="w-4 h-4" />}
                  {importing ? '가져오는 중...' : 'CSR 가져오기'}
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* ── Detail Dialog ── */}
      {detailOpen && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setDetailOpen(false)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-2xl bg-white dark:bg-gray-800 rounded-xl shadow-2xl max-h-[90vh] flex flex-col" onClick={(e) => e.stopPropagation()}>
              <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200 dark:border-gray-700">
                <div className="flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-gradient-to-br from-indigo-500 to-purple-600"><Eye className="w-4 h-4 text-white" /></div>
                  <div>
                    <h2 className="text-lg font-semibold text-gray-900 dark:text-white">CSR 상세</h2>
                    {selectedCsr && <p className="text-xs text-gray-500">{selectedCsr.subject_dn}</p>}
                  </div>
                </div>
                <button onClick={() => setDetailOpen(false)} className="p-2 text-gray-400 hover:text-gray-600 rounded-lg transition-colors"><X className="w-5 h-5" /></button>
              </div>
              <div className="overflow-y-auto flex-1 px-6 py-5 space-y-4">
                {detailLoading ? (
                  <div className="flex justify-center py-12"><div className="animate-spin rounded-full h-8 w-8 border-b-2 border-indigo-600" /></div>
                ) : selectedCsr && (
                  <>
                    {/* Info grid */}
                    <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
                      <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                        <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">Subject DN</p>
                        <p className="text-sm font-semibold text-gray-900 dark:text-white">{selectedCsr.subject_dn}</p>
                      </div>
                      <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                        <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">상태</p>
                        <span className={`inline-flex px-2.5 py-0.5 rounded-full text-xs font-medium ${STATUS_COLORS[selectedCsr.status]}`}>{STATUS_LABELS[selectedCsr.status]}</span>
                      </div>
                      <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                        <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">키 알고리즘</p>
                        <p className="text-sm font-mono font-semibold text-gray-900 dark:text-white">{selectedCsr.key_algorithm}</p>
                      </div>
                      <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                        <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">서명 알고리즘</p>
                        <p className="text-sm font-mono font-semibold text-gray-900 dark:text-white">{selectedCsr.signature_algorithm}</p>
                      </div>
                    </div>

                    <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
                      <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">공개키 핑거프린트 (SHA-256)</p>
                      <p className="text-xs font-mono text-gray-700 dark:text-gray-300 break-all">{selectedCsr.public_key_fingerprint}</p>
                    </div>

                    <div className="grid grid-cols-2 gap-4 text-sm">
                      <div><p className="text-xs text-gray-500 mb-0.5">생성자</p><p className="font-medium text-gray-900 dark:text-white">{selectedCsr.created_by}</p></div>
                      <div><p className="text-xs text-gray-500 mb-0.5">생성일</p><p className="font-medium text-gray-900 dark:text-white">{selectedCsr.created_at?.substring(0, 19).replace('T', ' ')}</p></div>
                      {selectedCsr.memo && <div className="col-span-2"><p className="text-xs text-gray-500 mb-0.5">메모</p><p className="font-medium text-gray-900 dark:text-white">{selectedCsr.memo}</p></div>}
                    </div>

                    {/* CSR PEM */}
                    {selectedCsr.csr_pem && (
                      <div className="relative">
                        <p className="text-xs font-medium text-gray-500 dark:text-gray-400 mb-2">CSR (PEM)</p>
                        <pre className="bg-gray-900 dark:bg-gray-950 text-green-400 border border-gray-700 rounded-xl p-4 text-[10px] font-mono overflow-x-auto max-h-44 overflow-y-auto whitespace-pre-wrap break-all">{selectedCsr.csr_pem}</pre>
                        <button onClick={() => handleCopy(selectedCsr.csr_pem!)} className="absolute top-8 right-3 p-1.5 bg-gray-800 border border-gray-600 rounded-lg hover:bg-gray-700 shadow-sm">
                          {copied ? <Check className="w-3.5 h-3.5 text-green-400" /> : <Copy className="w-3.5 h-3.5 text-gray-400" />}
                        </button>
                      </div>
                    )}

                    <button onClick={() => handleExportPem(selectedCsr.id)} className="w-full flex items-center justify-center gap-2 px-4 py-2.5 bg-gradient-to-r from-blue-500 to-indigo-500 hover:from-blue-600 hover:to-indigo-600 text-white rounded-xl font-medium shadow-md transition-all text-sm">
                      <Download className="w-4 h-4" /> PEM 다운로드
                    </button>

                    {/* Issued cert */}
                    {selectedCsr.status === 'ISSUED' && selectedCsr.certificate_issuer_dn && (
                      <div className="border border-green-200 dark:border-green-800 rounded-xl p-4 bg-gradient-to-r from-green-50 to-emerald-50 dark:from-green-900/20 dark:to-emerald-900/20 space-y-3">
                        <div className="flex items-center gap-2 text-green-700 dark:text-green-300 font-semibold text-sm">
                          <Award className="w-4 h-4" /> 발급된 인증서
                        </div>
                        <div className="grid grid-cols-2 gap-3 text-xs">
                          <div><p className="text-gray-500">발급자</p><p className="font-mono font-medium text-gray-700 dark:text-gray-300 mt-0.5">{selectedCsr.certificate_issuer_dn}</p></div>
                          <div><p className="text-gray-500">일련번호</p><p className="font-mono font-medium text-gray-700 dark:text-gray-300 mt-0.5">{selectedCsr.certificate_serial}</p></div>
                          <div><p className="text-gray-500">유효 시작</p><p className="font-medium text-gray-700 dark:text-gray-300 mt-0.5">{selectedCsr.certificate_not_before?.substring(0, 19)}</p></div>
                          <div><p className="text-gray-500">유효 만료</p><p className="font-medium text-gray-700 dark:text-gray-300 mt-0.5">{selectedCsr.certificate_not_after?.substring(0, 19)}</p></div>
                        </div>
                      </div>
                    )}

                    {/* Register cert button */}
                    {selectedCsr.status !== 'ISSUED' && selectedCsr.status !== 'REVOKED' && (
                      <button onClick={() => { setCertRegCsrId(selectedCsr.id); setCertRegOpen(true); setCertPemInput(''); setCertRegError(''); }}
                        className="w-full flex items-center justify-center gap-2 px-4 py-2.5 bg-gradient-to-r from-amber-500 to-orange-500 hover:from-amber-600 hover:to-orange-600 text-white rounded-xl font-medium shadow-md transition-all text-sm">
                        <Upload className="w-4 h-4" /> ICAO 발급 인증서 등록
                      </button>
                    )}
                  </>
                )}
              </div>
              <div className="flex justify-end px-6 py-4 border-t border-gray-200 dark:border-gray-700">
                <button onClick={() => setDetailOpen(false)} className={btnSecondary}>닫기</button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* ── Delete Dialog ── */}
      {deleteId && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setDeleteId(null)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-sm bg-white dark:bg-gray-800 rounded-xl shadow-2xl p-6" onClick={(e) => e.stopPropagation()}>
              <div className="flex items-center gap-3 mb-4">
                <div className="p-2 rounded-lg bg-red-100 dark:bg-red-900/30"><Trash2 className="w-5 h-5 text-red-600" /></div>
                <h3 className="text-lg font-semibold text-gray-900 dark:text-white">CSR 삭제</h3>
              </div>
              <p className="text-sm text-gray-600 dark:text-gray-400">삭제된 CSR은 복구할 수 없습니다. 개인키도 함께 영구 삭제됩니다.</p>
              <div className="flex justify-end gap-2 mt-6">
                <button onClick={() => setDeleteId(null)} className={btnSecondary}>취소</button>
                <button onClick={handleDelete} disabled={deleting} className={btnDanger}>
                  {deleting ? <Loader2 className="w-4 h-4 animate-spin" /> : <Trash2 className="w-4 h-4" />} 삭제
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* ── Cert Registration Dialog ── */}
      {certRegOpen && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setCertRegOpen(false)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-lg bg-white dark:bg-gray-800 rounded-xl shadow-2xl" onClick={(e) => e.stopPropagation()}>
              <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200 dark:border-gray-700">
                <div className="flex items-center gap-3">
                  <div className="p-2 rounded-lg bg-gradient-to-br from-amber-500 to-orange-600"><Award className="w-4 h-4 text-white" /></div>
                  <h2 className="text-lg font-semibold text-gray-900 dark:text-white">ICAO 발급 인증서 등록</h2>
                </div>
                <button onClick={() => setCertRegOpen(false)} className="p-2 text-gray-400 hover:text-gray-600 rounded-lg transition-colors"><X className="w-5 h-5" /></button>
              </div>
              <div className="px-6 py-5 space-y-4">
                <div className="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-xl p-3 text-xs text-blue-700 dark:text-blue-300">
                  ICAO에서 발급받은 인증서를 PEM 형식으로 붙여넣으세요. 공개키 매칭을 자동 검증합니다.
                </div>
                <div><label htmlFor="cert-pem" className={labelClass}>인증서 PEM</label><textarea id="cert-pem" rows={10} value={certPemInput} onChange={(e) => setCertPemInput(e.target.value)} className={textareaClass} placeholder={"-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----"} /></div>
                {certRegError && <div className="bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl p-3 text-xs text-red-700 dark:text-red-300">{certRegError}</div>}
              </div>
              <div className="flex justify-end gap-2 px-6 py-4 border-t border-gray-200 dark:border-gray-700">
                <button onClick={() => setCertRegOpen(false)} className={btnSecondary}>취소</button>
                <button onClick={handleRegisterCert} disabled={registering || !certPemInput.trim()}
                  className="flex items-center gap-2 px-4 py-2 bg-gradient-to-r from-amber-500 to-orange-500 hover:from-amber-600 hover:to-orange-600 text-white rounded-xl font-medium shadow-md transition-all text-sm disabled:opacity-50 disabled:cursor-not-allowed">
                  {registering ? <Loader2 className="w-4 h-4 animate-spin" /> : <Award className="w-4 h-4" />}
                  {registering ? '등록 중...' : '인증서 등록'}
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
