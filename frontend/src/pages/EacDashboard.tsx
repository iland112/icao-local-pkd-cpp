/**
 * EAC Dashboard
 * BSI TR-03110 CVC Certificate Management — Experimental
 */
import { useState, useCallback, useRef } from 'react';
import { useQuery } from '@tanstack/react-query';
import {
  Shield, Upload, Search, ChevronDown, ChevronUp,
  CheckCircle, XCircle, Clock, AlertTriangle, FileKey,
  CloudUpload, FileText, Eye, Database, RotateCcw,
  Loader2, Hash, Key, ShieldCheck, ShieldX,
} from 'lucide-react';
import {
  getEacStatistics, getEacCountries, searchEacCertificates,
  getEacChain, uploadCvc, previewCvc,
  type CvcCertificate, type ChainResult,
} from '../api/eacApi';
import { cn } from '@/utils/cn';
import { TreeViewer, type TreeNode } from '@/components/TreeViewer';

// ─── helpers ──────────────────────────────────────────────────────────────────

const CVC_TYPE_LABELS: Record<string, string> = {
  CVCA: 'CVCA (루트 CA)',
  DV_DOMESTIC: 'DV 국내',
  DV_FOREIGN: 'DV 해외',
  IS: 'IS (검사 시스템)',
};

const CVC_TYPE_COLOR: Record<string, string> = {
  CVCA: 'bg-purple-100 text-purple-700',
  DV_DOMESTIC: 'bg-blue-100 text-blue-700',
  DV_FOREIGN: 'bg-cyan-100 text-cyan-700',
  IS: 'bg-indigo-100 text-indigo-700',
};

const STATUS_COLOR: Record<string, string> = {
  VALID: 'text-green-600 bg-green-50',
  INVALID: 'text-red-600 bg-red-50',
  EXPIRED: 'text-yellow-600 bg-yellow-50',
  PENDING: 'text-gray-500 bg-gray-50',
};

function formatFileSize(bytes: number): string {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

const StatusBadge = ({ status }: { status: string }) => (
  <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded text-xs font-medium ${STATUS_COLOR[status] ?? 'text-gray-500 bg-gray-50'}`}>
    {status === 'VALID' && <CheckCircle className="w-3 h-3" />}
    {status === 'INVALID' && <XCircle className="w-3 h-3" />}
    {status === 'EXPIRED' && <AlertTriangle className="w-3 h-3" />}
    {status === 'PENDING' && <Clock className="w-3 h-3" />}
    {status}
  </span>
);

// ─── InfoRow ──────────────────────────────────────────────────────────────────

function InfoRow({ label, value, mono = false }: { label: string; value?: string | null; mono?: boolean }) {
  if (!value) return null;
  return (
    <div className="flex items-start gap-2 py-1.5 border-b border-gray-50 last:border-0">
      <span className="text-xs text-gray-500 w-28 flex-shrink-0 pt-0.5">{label}</span>
      <span className={cn('text-xs text-gray-800 break-all', mono && 'font-mono')}>{value}</span>
    </div>
  );
}

// ─── CVC tree builder ─────────────────────────────────────────────────────────

function buildCvcTree(cert: Partial<CvcCertificate>): TreeNode[] {
  let parsedPermissions: Record<string, boolean> | null = null;
  if (cert.chat_permissions) {
    try { parsedPermissions = JSON.parse(cert.chat_permissions); } catch { /* ignore */ }
  }
  const grantedPerms = parsedPermissions
    ? Object.entries(parsedPermissions).filter(([, v]) => v).map(([k]) => k)
    : [];
  const deniedPerms = parsedPermissions
    ? Object.entries(parsedPermissions).filter(([, v]) => !v).map(([k]) => k)
    : [];

  const chatChildren: TreeNode[] = [
    { id: 'chat-oid',  name: 'Role OID',  value: cert.chat_oid,  icon: 'hash', copyable: true },
    { id: 'chat-role', name: 'Role',       value: cert.chat_role, icon: 'shield' },
  ];
  if (grantedPerms.length > 0) {
    chatChildren.push({
      id: 'chat-granted', name: `Granted Permissions (${grantedPerms.length})`, icon: 'check-circle',
      children: grantedPerms.map((p, i) => ({ id: `gp-${i}`, name: p, icon: 'check' })),
    });
  }
  if (deniedPerms.length > 0) {
    chatChildren.push({
      id: 'chat-denied', name: `Denied Permissions (${deniedPerms.length})`, icon: 'x-circle',
      children: deniedPerms.map((p, i) => ({ id: `dp-${i}`, name: p, icon: 'x' })),
    });
  }

  const bodyChildren: TreeNode[] = [
    { id: 'car',  name: 'CAR (Certification Authority Reference)', value: cert.car,  icon: 'user', copyable: true },
    {
      id: 'pk', name: 'Public Key', icon: 'key',
      children: [
        { id: 'pk-algo',     name: 'Algorithm',     value: cert.public_key_algorithm, icon: 'shield' },
        { id: 'pk-algo-oid', name: 'Algorithm OID', value: cert.public_key_oid,       icon: 'hash', copyable: true },
      ],
    },
    { id: 'chr',  name: 'CHR (Certificate Holder Reference)', value: cert.chr, icon: 'user', copyable: true },
    { id: 'chat', name: 'CHAT (Certificate Holder Authorization Template)', icon: 'shield', children: chatChildren },
    {
      id: 'validity', name: 'Validity', icon: 'calendar',
      children: [
        { id: 'eff-date', name: 'Effective Date',  value: cert.effective_date  },
        { id: 'exp-date', name: 'Expiration Date', value: cert.expiration_date },
      ],
    },
  ];

  const rootChildren: TreeNode[] = [
    { id: 'body', name: 'Certificate Body (7F4E)', icon: 'file-text', children: bodyChildren },
    {
      id: 'sig', name: 'Signature', icon: 'shield',
      children: [
        { id: 'sig-valid', name: 'Signature Valid', value: cert.signature_valid !== undefined ? (cert.signature_valid ? 'true' : 'false') : '-' },
      ],
    },
  ];

  if (cert.fingerprint_sha256) {
    rootChildren.push({ id: 'fp', name: 'Fingerprint (SHA-256)', value: cert.fingerprint_sha256, icon: 'hash', copyable: true });
  }

  return [{ id: 'cvc', name: 'CV Certificate (7F21)', icon: 'file-text', children: rootChildren }];
}

// ─── CVC Preview Card ─────────────────────────────────────────────────────────

type CvcTab = 'general' | 'detail';

function CvcPreviewCard({ cert }: { cert: Partial<CvcCertificate> }) {
  const [tab, setTab] = useState<CvcTab>('general');

  let parsedPermissions: Record<string, boolean> | null = null;
  if (cert.chat_permissions) {
    try { parsedPermissions = JSON.parse(cert.chat_permissions); } catch { /* ignore */ }
  }
  const grantedPermissions = parsedPermissions
    ? Object.entries(parsedPermissions).filter(([, v]) => v).map(([k]) => k)
    : [];

  const tabClass = (active: boolean) => cn(
    'px-4 py-2 text-xs font-medium rounded-t-lg transition-colors border-b-2',
    active
      ? 'border-indigo-600 text-indigo-600 bg-white'
      : 'border-transparent text-gray-500 hover:text-gray-800'
  );

  return (
    <div className="rounded-xl bg-white border border-gray-200 shadow-sm overflow-hidden">
      {/* Card header */}
      <div className="flex items-center gap-3 px-4 py-3 bg-gradient-to-r from-indigo-50 to-purple-50 border-b border-indigo-100">
        <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-indigo-500 to-purple-600 flex items-center justify-center flex-shrink-0">
          <FileKey className="w-4 h-4 text-white" />
        </div>
        <div className="flex-1 min-w-0">
          <p className="text-sm font-bold text-gray-900 truncate">{cert.chr ?? '-'}</p>
          <p className="text-[10px] text-gray-500">Certificate Holder Reference</p>
        </div>
        <div className="flex items-center gap-2 flex-shrink-0">
          {cert.cvc_type && (
            <span className={cn('px-2 py-0.5 text-[10px] font-bold rounded', CVC_TYPE_COLOR[cert.cvc_type] ?? 'bg-gray-100 text-gray-700')}>
              {cert.cvc_type}
            </span>
          )}
          {cert.signature_valid !== undefined && (
            cert.signature_valid
              ? <ShieldCheck className="w-4 h-4 text-green-500" />
              : <ShieldX className="w-4 h-4 text-red-500" />
          )}
        </div>
      </div>

      {/* Tabs */}
      <div className="flex border-b border-gray-200 px-4 bg-gray-50">
        <button onClick={() => setTab('general')} className={tabClass(tab === 'general')}>일반</button>
        <button onClick={() => setTab('detail')}  className={tabClass(tab === 'detail')}>상세</button>
      </div>

      {/* Tab: 일반 */}
      {tab === 'general' && (
        <div className="px-4 py-3 space-y-0">
          <InfoRow label="CHR" value={cert.chr} mono />
          <InfoRow label="CAR" value={cert.car} mono />
          <InfoRow label="국가" value={cert.country_code} />
          <InfoRow label="유형" value={cert.cvc_type ? (CVC_TYPE_LABELS[cert.cvc_type] ?? cert.cvc_type) : undefined} />
          <InfoRow label="알고리즘" value={cert.public_key_algorithm} />
          <InfoRow label="알고리즘 OID" value={cert.public_key_oid} mono />
          <InfoRow label="유효 시작일" value={cert.effective_date} />
          <InfoRow label="만료일" value={cert.expiration_date} />
          <InfoRow label="CHAT 역할" value={cert.chat_role} />
          <InfoRow label="CHAT OID" value={cert.chat_oid} mono />
          {cert.fingerprint_sha256 && (
            <div className="flex items-start gap-2 py-1.5">
              <span className="text-xs text-gray-500 w-28 flex-shrink-0 pt-0.5 flex items-center gap-1">
                <Hash className="w-3 h-3" /> SHA-256
              </span>
              <span className="text-[10px] font-mono text-gray-600 break-all">{cert.fingerprint_sha256}</span>
            </div>
          )}
          {grantedPermissions.length > 0 && (
            <div className="pt-2 mt-1 border-t border-gray-50">
              <p className="text-xs text-gray-500 mb-1.5 flex items-center gap-1">
                <Key className="w-3 h-3" /> CHAT 부여 권한 ({grantedPermissions.length}개)
              </p>
              <div className="flex flex-wrap gap-1">
                {grantedPermissions.map(p => (
                  <span key={p} className="px-1.5 py-0.5 text-[10px] bg-indigo-50 text-indigo-700 rounded font-mono">{p}</span>
                ))}
              </div>
            </div>
          )}
        </div>
      )}

      {/* Tab: 상세 (TreeViewer) */}
      {tab === 'detail' && (
        <div className="p-3">
          <TreeViewer data={buildCvcTree(cert)} height="380px" />
        </div>
      )}
    </div>
  );
}

// ─── Upload panel ─────────────────────────────────────────────────────────────

type UploadStep = 'IDLE' | 'FILE_SELECTED' | 'PREVIEWING' | 'PREVIEW_READY' | 'PREVIEW_ERROR' | 'SAVING' | 'DONE' | 'SAVE_ERROR';

function UploadPanel({ onUploaded }: { onUploaded: () => void }) {
  const [file, setFile] = useState<File | null>(null);
  const [preview, setPreview] = useState<Partial<CvcCertificate> | null>(null);
  const [step, setStep] = useState<UploadStep>('IDLE');
  const [message, setMessage] = useState('');
  const [isDragging, setIsDragging] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const handleFileSelect = (f: File) => {
    setFile(f);
    setPreview(null);
    setStep('FILE_SELECTED');
    setMessage('');
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
    const f = e.dataTransfer.files[0];
    if (f) handleFileSelect(f);
  };

  const handlePreview = async () => {
    if (!file) return;
    setStep('PREVIEWING');
    try {
      const res = await previewCvc(file);
      setPreview(res.data.certificate ?? null);
      setStep('PREVIEW_READY');
    } catch {
      setStep('PREVIEW_ERROR');
      setMessage('CVC 파싱 실패. 올바른 CVC 바이너리 파일인지 확인하세요.');
    }
  };

  const handleSave = async () => {
    if (!file) return;
    setStep('SAVING');
    try {
      const res = await uploadCvc(file);
      if (res.data.success) {
        setStep('DONE');
        setMessage(`저장 완료: CHR=${res.data.certificate?.chr ?? '-'}`);
        onUploaded();
      } else {
        setStep('SAVE_ERROR');
        setMessage(res.data.error ?? '저장 실패');
      }
    } catch (err: unknown) {
      setStep('SAVE_ERROR');
      const axiosErr = err as { response?: { data?: { error?: string } } };
      setMessage(axiosErr.response?.data?.error ?? '업로드 중 오류 발생');
    }
  };

  const handleReset = () => {
    setFile(null);
    setPreview(null);
    setStep('IDLE');
    setMessage('');
    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  return (
    <div className="space-y-4">
      {/* Step 1: File selection */}
      <div className="rounded-xl bg-white border border-gray-200 shadow-sm p-4">
        <div className="flex items-center gap-2 mb-3">
          <div className="w-6 h-6 rounded-full bg-indigo-100 flex items-center justify-center text-xs font-bold text-indigo-600">1</div>
          <h2 className="text-sm font-bold text-gray-900">CVC 파일 선택</h2>
        </div>

        <div
          className={cn(
            'relative border-2 border-dashed rounded-lg text-center cursor-pointer transition-all duration-200',
            file ? 'p-3 border-blue-300 bg-blue-50/50' : 'p-5',
            isDragging && 'border-indigo-500 bg-indigo-50 scale-[1.01]',
            !file && !isDragging && 'border-gray-300 hover:border-indigo-400 hover:bg-gray-50',
          )}
          onDragOver={(e) => { e.preventDefault(); setIsDragging(true); }}
          onDragLeave={(e) => { e.preventDefault(); setIsDragging(false); }}
          onDrop={handleDrop}
          onClick={() => fileInputRef.current?.click()}
        >
          <input
            ref={fileInputRef}
            type="file"
            accept=".cvc,.bin,.der"
            className="hidden"
            onChange={(e) => { const f = e.target.files?.[0]; if (f) handleFileSelect(f); }}
          />
          {file ? (
            <div className="flex items-center justify-center gap-3">
              <FileText className="w-7 h-7 text-blue-500 flex-shrink-0" />
              <div className="flex items-center gap-2">
                <span className="px-1.5 py-0.5 text-[10px] font-bold rounded bg-indigo-100 text-indigo-700">CVC</span>
                <span className="text-sm font-semibold text-gray-900">{file.name}</span>
                <span className="text-xs text-gray-400">({formatFileSize(file.size)})</span>
              </div>
              <span className="text-[10px] text-gray-400 ml-1">클릭하여 변경</span>
            </div>
          ) : (
            <div className="flex flex-col items-center gap-1.5">
              <CloudUpload className="w-8 h-8 text-gray-400" />
              <p className="text-sm text-gray-600">파일을 드래그하거나 클릭하여 선택</p>
              <p className="text-[10px] text-gray-400">.cvc .bin .der</p>
            </div>
          )}
        </div>

        <div className="mt-3 flex items-center justify-end gap-2">
          {step === 'FILE_SELECTED' && (
            <button
              onClick={handlePreview}
              className="flex items-center gap-1.5 px-4 py-2 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-sm font-medium transition-colors"
            >
              <Eye className="w-4 h-4" /> 미리보기
            </button>
          )}
          {step === 'PREVIEWING' && (
            <div className="flex items-center gap-2 text-indigo-600">
              <Loader2 className="w-4 h-4 animate-spin" />
              <span className="text-sm">파싱 중…</span>
            </div>
          )}
        </div>
      </div>

      {/* Step 2: Preview */}
      {(step === 'PREVIEW_READY' || step === 'SAVING' || step === 'DONE' || step === 'SAVE_ERROR') && preview && (
        <div className="rounded-xl bg-white border border-gray-200 shadow-sm overflow-hidden">
          <div className="flex items-center gap-2.5 px-4 py-3 border-b border-gray-100">
            <div className="w-6 h-6 rounded-full bg-blue-100 flex items-center justify-center text-xs font-bold text-blue-600">2</div>
            <h2 className="text-sm font-bold text-gray-900">파싱 결과</h2>
            <div className="ml-auto flex items-center gap-2">
              {preview.cvc_type && (
                <span className={cn('px-2 py-0.5 text-[10px] font-bold rounded', CVC_TYPE_COLOR[preview.cvc_type] ?? 'bg-gray-100 text-gray-700')}>
                  {CVC_TYPE_LABELS[preview.cvc_type] ?? preview.cvc_type}
                </span>
              )}
            </div>
          </div>

          <div className="p-4">
            <CvcPreviewCard cert={preview} />
          </div>

          {/* Action bar */}
          <div className="px-4 pb-4">
            {step === 'PREVIEW_READY' && (
              <div className="flex items-center justify-end gap-2 pt-3 border-t border-gray-100">
                <button
                  onClick={handleReset}
                  className="flex items-center gap-1.5 px-3.5 py-2 border border-gray-300 text-gray-600 hover:bg-gray-50 rounded-lg text-sm transition-colors"
                >
                  <RotateCcw className="w-3.5 h-3.5" /> 취소
                </button>
                <button
                  onClick={handleSave}
                  className="flex items-center gap-1.5 px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm font-medium transition-colors"
                >
                  <Database className="w-3.5 h-3.5" /> DB 저장
                </button>
              </div>
            )}
            {step === 'SAVING' && (
              <div className="flex items-center justify-center gap-2 pt-3 border-t border-gray-100 text-blue-600">
                <Loader2 className="w-4 h-4 animate-spin" />
                <span className="text-sm">저장 중…</span>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Step 3: Result */}
      {step === 'DONE' && (
        <div className="rounded-xl bg-white border border-gray-200 shadow-sm overflow-hidden">
          <div className="flex items-center gap-2.5 px-4 py-3 border-b border-gray-100">
            <div className="w-6 h-6 rounded-full bg-green-100 flex items-center justify-center text-xs font-bold text-green-600">3</div>
            <h2 className="text-sm font-bold text-gray-900">저장 결과</h2>
          </div>
          <div className="p-4 flex items-center gap-3">
            <CheckCircle className="w-5 h-5 text-green-500 flex-shrink-0" />
            <div className="flex-1">
              <p className="text-sm font-semibold text-green-700">저장 완료</p>
              <p className="text-xs text-gray-500 mt-0.5">{message}</p>
            </div>
            <button
              onClick={handleReset}
              className="flex items-center gap-1.5 px-3 py-1.5 border border-gray-300 text-gray-600 hover:bg-gray-50 rounded-lg text-xs transition-colors"
            >
              <Upload className="w-3 h-3" /> 추가 업로드
            </button>
          </div>
        </div>
      )}

      {/* Error messages */}
      {(step === 'PREVIEW_ERROR' || step === 'SAVE_ERROR') && (
        <div className="rounded-xl bg-red-50 border border-red-200 p-4 flex items-start gap-3">
          <XCircle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
          <div className="flex-1">
            <p className="text-sm font-semibold text-red-700">
              {step === 'PREVIEW_ERROR' ? '파싱 오류' : '저장 오류'}
            </p>
            <p className="text-xs text-red-600 mt-0.5">{message}</p>
          </div>
          <button
            onClick={handleReset}
            className="flex items-center gap-1 text-xs text-red-600 hover:text-red-800"
          >
            <RotateCcw className="w-3 h-3" /> 다시 시도
          </button>
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
          <span className={cn('px-1.5 py-0.5 rounded text-[11px] font-medium', CVC_TYPE_COLOR[cert.cvc_type] ?? 'bg-gray-100 text-gray-700')}>
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
            {chainLoading ? <Loader2 className="w-3 h-3 animate-spin" /> : open ? <ChevronUp className="w-3 h-3" /> : <ChevronDown className="w-3 h-3" />}
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
              <span className="text-gray-400 ml-2">depth={chain.chainDepth}</span>
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
        <div className="p-2.5 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-600 shadow-md">
          <FileKey className="w-6 h-6 text-white" />
        </div>
        <div>
          <h1 className="text-xl font-bold text-gray-900">EAC 인증서 관리</h1>
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
