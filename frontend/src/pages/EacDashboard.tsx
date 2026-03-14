/**
 * EAC Dashboard
 * BSI TR-03110 CVC Certificate Management — Experimental
 */
import { useState, useCallback } from 'react';
import { useQuery } from '@tanstack/react-query';
import {
  Shield, Upload, Search, ChevronDown, ChevronUp,
  CheckCircle, XCircle, Clock, AlertTriangle, FileKey,
} from 'lucide-react';
import {
  getEacStatistics, getEacCountries, searchEacCertificates,
  getEacChain, uploadCvc, previewCvc,
  type CvcCertificate, type ChainResult,
} from '../api/eacApi';

// ─── helpers ──────────────────────────────────────────────────────────────────

const CVC_TYPE_LABELS: Record<string, string> = {
  CVCA: 'CVCA (루트 CA)',
  DV_DOMESTIC: 'DV 국내',
  DV_FOREIGN: 'DV 해외',
  IS: 'IS (검사 시스템)',
};

const STATUS_COLOR: Record<string, string> = {
  VALID: 'text-green-600 bg-green-50',
  INVALID: 'text-red-600 bg-red-50',
  EXPIRED: 'text-yellow-600 bg-yellow-50',
  PENDING: 'text-gray-500 bg-gray-50',
};

const StatusBadge = ({ status }: { status: string }) => (
  <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded text-xs font-medium ${STATUS_COLOR[status] ?? 'text-gray-500 bg-gray-50'}`}>
    {status === 'VALID' && <CheckCircle className="w-3 h-3" />}
    {status === 'INVALID' && <XCircle className="w-3 h-3" />}
    {status === 'EXPIRED' && <AlertTriangle className="w-3 h-3" />}
    {status === 'PENDING' && <Clock className="w-3 h-3" />}
    {status}
  </span>
);

// ─── Upload panel ─────────────────────────────────────────────────────────────

function UploadPanel({ onUploaded }: { onUploaded: () => void }) {
  const [file, setFile] = useState<File | null>(null);
  const [preview, setPreview] = useState<Record<string, unknown> | null>(null);
  const [status, setStatus] = useState<'idle' | 'previewing' | 'uploading' | 'done' | 'error'>('idle');
  const [message, setMessage] = useState('');

  const handleFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const f = e.target.files?.[0];
    if (f) { setFile(f); setPreview(null); setStatus('idle'); setMessage(''); }
  };

  const handlePreview = async () => {
    if (!file) return;
    setStatus('previewing');
    try {
      const res = await previewCvc(file);
      setPreview(res.data.certificate ?? null);
      setStatus('idle');
    } catch {
      setStatus('error');
      setMessage('CVC 파싱 실패. 올바른 CVC 바이너리 파일인지 확인하세요.');
    }
  };

  const handleUpload = async () => {
    if (!file) return;
    setStatus('uploading');
    try {
      const res = await uploadCvc(file);
      if (res.data.success) {
        setStatus('done');
        setMessage(`저장 완료: CHR=${res.data.certificate?.chr ?? '-'}`);
        onUploaded();
      } else {
        setStatus('error');
        setMessage(res.data.error ?? '저장 실패');
      }
    } catch (err: unknown) {
      setStatus('error');
      const axiosErr = err as { response?: { data?: { error?: string } } };
      setMessage(axiosErr.response?.data?.error ?? '업로드 중 오류 발생');
    }
  };

  return (
    <div className="bg-white rounded-xl border border-gray-200 p-5">
      <h2 className="text-sm font-semibold text-gray-700 mb-4 flex items-center gap-2">
        <Upload className="w-4 h-4 text-blue-500" /> CVC 인증서 업로드
      </h2>

      <div className="flex items-center gap-3 mb-4">
        <label className="cursor-pointer">
          <span className="px-3 py-1.5 text-xs border border-gray-300 rounded bg-gray-50 hover:bg-gray-100 text-gray-700">
            파일 선택
          </span>
          <input type="file" className="hidden" accept=".cvc,.bin,.der" onChange={handleFile} />
        </label>
        {file && <span className="text-xs text-gray-600">{file.name} ({(file.size / 1024).toFixed(1)} KB)</span>}
      </div>

      {file && (
        <div className="flex gap-2 mb-4">
          <button
            onClick={handlePreview}
            disabled={status === 'previewing'}
            className="px-3 py-1.5 text-xs bg-gray-100 hover:bg-gray-200 text-gray-700 rounded border disabled:opacity-50"
          >
            {status === 'previewing' ? '파싱 중…' : '미리보기'}
          </button>
          <button
            onClick={handleUpload}
            disabled={status === 'uploading'}
            className="px-3 py-1.5 text-xs bg-blue-600 hover:bg-blue-700 text-white rounded disabled:opacity-50"
          >
            {status === 'uploading' ? '저장 중…' : 'DB 저장'}
          </button>
        </div>
      )}

      {status === 'done' && (
        <p className="text-xs text-green-600 bg-green-50 rounded px-3 py-2">{message}</p>
      )}
      {status === 'error' && (
        <p className="text-xs text-red-600 bg-red-50 rounded px-3 py-2">{message}</p>
      )}

      {preview && (
        <div className="mt-3 bg-gray-50 rounded border p-3 text-xs font-mono overflow-auto max-h-60">
          <pre>{JSON.stringify(preview, null, 2)}</pre>
        </div>
      )}
    </div>
  );
}

// ─── Certificate row ──────────────────────────────────────────────────────────

function CertRow({ cert }: { cert: CvcCertificate }) {
  const [open, setOpen] = useState(false);
  const [chain, setChain] = useState<ChainResult | null>(null);
  const [chainLoading, setChainLoading] = useState(false);

  const loadChain = async () => {
    if (chain) { setOpen(!open); return; }
    setChainLoading(true);
    try {
      const res = await getEacChain(cert.id);
      setChain(res.data.chain ?? null);
    } catch {
      /* ignore */
    } finally {
      setChainLoading(false);
      setOpen(true);
    }
  };

  return (
    <>
      <tr className="border-t border-gray-100 hover:bg-gray-50 text-xs">
        <td className="px-3 py-2 font-medium text-gray-700">{cert.country_code}</td>
        <td className="px-3 py-2">
          <span className="px-1.5 py-0.5 rounded bg-indigo-50 text-indigo-700 text-[11px]">
            {CVC_TYPE_LABELS[cert.cvc_type] ?? cert.cvc_type}
          </span>
        </td>
        <td className="px-3 py-2 font-mono text-gray-600">{cert.chr}</td>
        <td className="px-3 py-2 font-mono text-gray-500">{cert.car}</td>
        <td className="px-3 py-2">{cert.public_key_algorithm}</td>
        <td className="px-3 py-2">{cert.expiration_date}</td>
        <td className="px-3 py-2"><StatusBadge status={cert.validation_status} /></td>
        <td className="px-3 py-2">
          <button
            onClick={loadChain}
            className="text-blue-600 hover:underline flex items-center gap-1"
          >
            {chainLoading ? '…' : open ? <ChevronUp className="w-3 h-3" /> : <ChevronDown className="w-3 h-3" />}
            체인
          </button>
        </td>
      </tr>
      {open && chain && (
        <tr className="border-t border-gray-100 bg-blue-50">
          <td colSpan={8} className="px-4 py-3 text-xs">
            <div className="flex items-center gap-2 mb-1">
              {chain.chainValid
                ? <CheckCircle className="w-3.5 h-3.5 text-green-500" />
                : <XCircle className="w-3.5 h-3.5 text-red-500" />}
              <span className={chain.chainValid ? 'text-green-700 font-medium' : 'text-red-600 font-medium'}>
                {chain.message}
              </span>
            </div>
            {chain.chainPath && (
              <p className="font-mono text-gray-600 text-[11px]">{chain.chainPath}</p>
            )}
          </td>
        </tr>
      )}
    </>
  );
}

// ─── Main Page ────────────────────────────────────────────────────────────────

export default function EacDashboard() {
  const [country, setCountry] = useState('');
  const [type, setType] = useState('');
  const [page, setPage] = useState(1);
  const pageSize = 20;

  const { data: statsData } = useQuery({
    queryKey: ['eac-stats'],
    queryFn: () => getEacStatistics().then(r => r.data),
    staleTime: 30_000,
  });

  const { data: countriesData } = useQuery({
    queryKey: ['eac-countries'],
    queryFn: () => getEacCountries().then(r => r.data),
    staleTime: 60_000,
  });

  const { data: certsData, refetch } = useQuery({
    queryKey: ['eac-certs', country, type, page],
    queryFn: () => searchEacCertificates({ country, type, page, pageSize }).then(r => r.data),
    staleTime: 10_000,
  });

  const stats = statsData?.statistics;
  const countries = countriesData?.countries ?? [];
  const certs = certsData?.data ?? [];
  const total = certsData?.total ?? 0;
  const totalPages = certsData?.totalPages ?? 0;

  const handleUploaded = useCallback(() => refetch(), [refetch]);

  return (
    <div className="px-4 lg:px-6 py-4 space-y-5">
      {/* Header */}
      <div className="flex items-center gap-3">
        <div className="w-9 h-9 rounded-lg bg-gradient-to-br from-indigo-500 to-purple-600 flex items-center justify-center">
          <FileKey className="w-5 h-5 text-white" />
        </div>
        <div>
          <h1 className="text-lg font-bold text-gray-900">EAC 인증서 관리</h1>
          <p className="text-xs text-gray-500">BSI TR-03110 · Card Verifiable Certificate (실험적)</p>
        </div>
        <span className="ml-2 px-2 py-0.5 rounded-full text-[10px] font-medium bg-amber-100 text-amber-700 border border-amber-200">
          EXPERIMENTAL
        </span>
      </div>

      {/* Stats */}
      {stats && (
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
          {[
            { label: '전체 CVC', value: stats.total, icon: <Shield className="w-4 h-4 text-indigo-500" /> },
            { label: 'CVCA', value: stats.byType?.CVCA ?? 0, icon: <Shield className="w-4 h-4 text-purple-500" /> },
            { label: '유효', value: stats.validCount, icon: <CheckCircle className="w-4 h-4 text-green-500" /> },
            { label: '만료', value: stats.expiredCount, icon: <AlertTriangle className="w-4 h-4 text-yellow-500" /> },
          ].map(({ label, value, icon }) => (
            <div key={label} className="bg-white rounded-xl border border-gray-200 p-4 flex items-center gap-3">
              {icon}
              <div>
                <p className="text-xs text-gray-500">{label}</p>
                <p className="text-xl font-bold text-gray-900">{value.toLocaleString()}</p>
              </div>
            </div>
          ))}
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5">
        {/* Upload */}
        <div className="lg:col-span-1">
          <UploadPanel onUploaded={handleUploaded} />
        </div>

        {/* Search + Table */}
        <div className="lg:col-span-2 space-y-4">
          {/* Filters */}
          <div className="bg-white rounded-xl border border-gray-200 p-4">
            <h2 className="text-sm font-semibold text-gray-700 mb-3 flex items-center gap-2">
              <Search className="w-4 h-4 text-blue-500" /> 검색
            </h2>
            <div className="flex flex-wrap gap-3">
              <select
                value={country}
                onChange={e => { setCountry(e.target.value); setPage(1); }}
                className="border border-gray-200 rounded px-2 py-1.5 text-xs text-gray-700"
              >
                <option value="">전체 국가</option>
                {countries.map(c => (
                  <option key={c.country_code} value={c.country_code}>{c.country_code}</option>
                ))}
              </select>
              <select
                value={type}
                onChange={e => { setType(e.target.value); setPage(1); }}
                className="border border-gray-200 rounded px-2 py-1.5 text-xs text-gray-700"
              >
                <option value="">전체 유형</option>
                {Object.entries(CVC_TYPE_LABELS).map(([k, v]) => (
                  <option key={k} value={k}>{v}</option>
                ))}
              </select>
            </div>
          </div>

          {/* Table */}
          <div className="bg-white rounded-xl border border-gray-200 overflow-hidden">
            <div className="px-4 py-3 border-b border-gray-100 flex items-center justify-between">
              <span className="text-sm font-semibold text-gray-700">CVC 인증서 목록</span>
              <span className="text-xs text-gray-500">총 {total.toLocaleString()}건</span>
            </div>
            <div className="overflow-x-auto">
              <table className="w-full">
                <thead>
                  <tr className="bg-gray-50 text-left text-[11px] text-gray-500 uppercase tracking-wide">
                    <th className="px-3 py-2">국가</th>
                    <th className="px-3 py-2">유형</th>
                    <th className="px-3 py-2">CHR</th>
                    <th className="px-3 py-2">CAR</th>
                    <th className="px-3 py-2">알고리즘</th>
                    <th className="px-3 py-2">만료일</th>
                    <th className="px-3 py-2">상태</th>
                    <th className="px-3 py-2">신뢰체인</th>
                  </tr>
                </thead>
                <tbody>
                  {certs.length === 0 ? (
                    <tr>
                      <td colSpan={8} className="px-4 py-8 text-center text-xs text-gray-400">
                        등록된 CVC 인증서가 없습니다.
                      </td>
                    </tr>
                  ) : (
                    certs.map(cert => <CertRow key={cert.id} cert={cert} />)
                  )}
                </tbody>
              </table>
            </div>

            {/* Pagination */}
            {totalPages > 1 && (
              <div className="px-4 py-3 border-t border-gray-100 flex items-center justify-between">
                <button
                  onClick={() => setPage(p => Math.max(1, p - 1))}
                  disabled={page === 1}
                  className="px-3 py-1 text-xs border rounded disabled:opacity-40 hover:bg-gray-50"
                >이전</button>
                <span className="text-xs text-gray-500">{page} / {totalPages}</span>
                <button
                  onClick={() => setPage(p => Math.min(totalPages, p + 1))}
                  disabled={page === totalPages}
                  className="px-3 py-1 text-xs border rounded disabled:opacity-40 hover:bg-gray-50"
                >다음</button>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
