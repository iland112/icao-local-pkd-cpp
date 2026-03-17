/**
 * EAC Dashboard
 * BSI TR-03110 CVC Certificate Management — Experimental
 */
import { useState, useCallback, useRef, useEffect } from 'react';
import { createPortal } from 'react-dom';
import { useQuery } from '@tanstack/react-query';
import {
  Shield, Upload, Search, ChevronDown, ChevronUp,
  CheckCircle, XCircle, Clock, AlertTriangle, FileKey,
  CloudUpload, FileText, Eye, Database, RotateCcw,
  Loader2, Hash, Key, ShieldCheck, ShieldX, List, HelpCircle,
  ArrowDown, Trash2,
} from 'lucide-react';
import {
  getEacStatistics, getEacCountries, searchEacCertificates,
  getEacChain, uploadCvc, previewCvc, deleteEacCertificate,
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
  CVCA:       'bg-purple-100 text-purple-700',
  DV_DOMESTIC:'bg-blue-100 text-blue-700',
  DV_FOREIGN: 'bg-cyan-100 text-cyan-700',
  IS:         'bg-indigo-100 text-indigo-700',
};

const CVC_TYPE_BORDER: Record<string, string> = {
  CVCA:       'border-purple-200 bg-purple-50',
  DV_DOMESTIC:'border-blue-200 bg-blue-50',
  DV_FOREIGN: 'border-cyan-200 bg-cyan-50',
  IS:         'border-indigo-200 bg-indigo-50',
};

const STATUS_COLOR: Record<string, string> = {
  VALID:   'text-green-600 bg-green-50',
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
    {status === 'VALID'   && <CheckCircle   className="w-3 h-3" />}
    {status === 'INVALID' && <XCircle       className="w-3 h-3" />}
    {status === 'EXPIRED' && <AlertTriangle className="w-3 h-3" />}
    {status === 'PENDING' && <Clock         className="w-3 h-3" />}
    {status}
  </span>
);

// ─── InfoTooltip ──────────────────────────────────────────────────────────────
// position: fixed + createPortal → overflow:hidden 컨테이너에서도 클리핑 없음

function InfoTooltip({ content, side = 'right' }: { content: React.ReactNode; side?: 'right' | 'bottom' }) {
  const [show, setShow] = useState(false);
  const [pos, setPos]   = useState<{ top: number; left: number } | null>(null);
  const btnRef = useRef<HTMLButtonElement>(null);

  const calcPos = () => {
    if (!btnRef.current) return;
    const r = btnRef.current.getBoundingClientRect();
    if (side === 'right') {
      setPos({ top: r.top + r.height / 2, left: r.right + 10 });
    } else {
      // bottom: 아이콘 아래에 중앙 정렬
      setPos({ top: r.bottom + 8, left: r.left + r.width / 2 });
    }
  };

  // 스크롤 시 닫기
  useEffect(() => {
    if (!show) return;
    const close = () => setShow(false);
    window.addEventListener('scroll', close, true);
    return () => window.removeEventListener('scroll', close, true);
  }, [show]);

  const tooltipStyle: React.CSSProperties = pos
    ? side === 'right'
      ? { position: 'fixed', top: pos.top, left: pos.left, transform: 'translateY(-50%)', zIndex: 9999 }
      : { position: 'fixed', top: pos.top, left: pos.left, transform: 'translateX(-50%)',  zIndex: 9999 }
    : { display: 'none' };

  return (
    <span className="relative inline-flex items-center">
      <button
        ref={btnRef}
        type="button"
        onMouseEnter={() => { calcPos(); setShow(true);  }}
        onMouseLeave={() => setShow(false)}
        onClick={() => { calcPos(); setShow(v => !v); }}
        className="text-gray-400 hover:text-indigo-500 transition-colors focus:outline-none"
        aria-label="도움말"
      >
        <HelpCircle className="w-3.5 h-3.5" />
      </button>

      {show && pos && createPortal(
        <div
          style={tooltipStyle}
          className="w-72 bg-gray-900 text-white text-[11px] rounded-xl p-3.5 shadow-2xl pointer-events-none leading-relaxed"
        >
          {content}
          {/* 말풍선 꼬리 */}
          {side === 'right' && (
            <div className="absolute right-full top-1/2 -translate-y-1/2 border-4 border-transparent border-r-gray-900" />
          )}
          {side === 'bottom' && (
            <div className="absolute bottom-full left-1/2 -translate-x-1/2 border-4 border-transparent border-b-gray-900" />
          )}
        </div>,
        document.body,
      )}
    </span>
  );
}

const CHR_TOOLTIP = (
  <div className="space-y-2">
    <p className="font-bold text-white text-xs">CHR — Certificate Holder Reference</p>
    <p className="text-gray-300">이 인증서의 <span className="text-indigo-300 font-semibold">소유자(holder) 식별자</span>입니다. X.509의 Subject DN에 해당합니다.</p>
    <div className="bg-gray-800 rounded-lg p-2 font-mono text-[10px] text-green-300">
      <p>DETESTATDE019</p>
      <p className="text-gray-400 mt-1">DE  = 국가코드</p>
      <p className="text-gray-400">TEST = 테스트 환경</p>
      <p className="text-gray-400">AT   = 소유자 코드</p>
      <p className="text-gray-400">DE   = 발급 국가</p>
      <p className="text-gray-400">019  = 일련번호 (5자리)</p>
    </div>
  </div>
);

const CAR_TOOLTIP = (
  <div className="space-y-2">
    <p className="font-bold text-white text-xs">CAR — Certification Authority Reference</p>
    <p className="text-gray-300">이 인증서에 <span className="text-indigo-300 font-semibold">서명한 상위 CA의 CHR</span>입니다. X.509의 Issuer DN에 해당합니다.</p>
    <div className="bg-gray-800 rounded-lg p-2 font-mono text-[10px] space-y-1">
      <p><span className="text-purple-300">CVCA</span> <span className="text-gray-400">CAR = 자신의 CHR (자체 서명)</span></p>
      <p><span className="text-blue-300">DV  </span> <span className="text-gray-400">CAR = CVCA의 CHR</span></p>
      <p><span className="text-indigo-300">IS  </span> <span className="text-gray-400">CAR = DV의 CHR</span></p>
    </div>
    <p className="text-gray-400 text-[10px]">CAR가 상위 인증서의 CHR와 일치하면 체인이 연결됩니다.</p>
  </div>
);

// ─── Chain Diagram ────────────────────────────────────────────────────────────

function ChainDiagram({ chain }: { chain: ChainResult }) {
  // chainPath: "IS_CHR → DV_CHR → CVCA_CHR" (end-cert → root 방향)
  const raw = chain.chainPath
    ? chain.chainPath.split(/\s*[→➔>]\s*/).filter(Boolean)
    : [];

  if (raw.length === 0) return null;

  // 루트(CVCA)를 위에, IS를 아래에 표시 (발급 계층 구조)
  const nodes = [...raw].reverse();

  const certMap = new Map<string, CvcCertificate>();
  chain.certificates?.forEach(c => certMap.set(c.chr, c));

  const guessType = (chr: string): string | undefined => {
    const c = certMap.get(chr);
    if (c) return c.cvc_type;
    // CHR 패턴으로 추정
    const upper = chr.toUpperCase();
    if (upper.includes('CVCA')) return 'CVCA';
    if (upper.includes('DV'))   return 'DV_DOMESTIC';
    if (upper.includes('AT') || upper.includes('IS')) return 'IS';
    return undefined;
  };

  return (
    <div className="mt-2">
      <p className="text-[10px] text-gray-400 mb-2 flex items-center gap-1">
        <Shield className="w-3 h-3" /> 신뢰 체인 구조
        <span className="ml-auto font-mono">depth = {chain.chainDepth}</span>
      </p>

      <div className="flex flex-col items-start gap-0">
        {nodes.map((chr, i) => {
          const certType = guessType(chr);
          const cert = certMap.get(chr);
          const isRoot = i === 0;           // 최상위 (CVCA)
          const isLeaf = i === nodes.length - 1; // 말단 (IS)

          return (
            <div key={chr} className="w-full flex flex-col items-start">
              {/* 노드 박스 */}
              <div className={cn(
                'w-full flex items-center gap-2.5 px-3 py-2.5 rounded-xl border text-xs shadow-sm',
                certType ? (CVC_TYPE_BORDER[certType] ?? 'border-gray-200 bg-gray-50') : 'border-gray-200 bg-gray-50',
              )}>
                {/* 타입 배지 */}
                {certType && (
                  <span className={cn('px-1.5 py-0.5 rounded text-[10px] font-bold flex-shrink-0', CVC_TYPE_COLOR[certType] ?? 'bg-gray-100 text-gray-700')}>
                    {CVC_TYPE_LABELS[certType] ?? certType}
                  </span>
                )}

                {/* CHR */}
                <span className="font-mono text-gray-800 font-medium flex-1 truncate">{chr}</span>

                {/* 태그 */}
                <div className="flex items-center gap-1.5 flex-shrink-0">
                  {isRoot && isLeaf && (
                    <span className="text-[10px] px-1.5 py-0.5 rounded bg-purple-100 text-purple-600">자체 서명</span>
                  )}
                  {isRoot && !isLeaf && (
                    <span className="text-[10px] px-1.5 py-0.5 rounded bg-purple-100 text-purple-600">루트 CA · 자체 서명</span>
                  )}
                  {isLeaf && !isRoot && (
                    <span className="text-[10px] px-1.5 py-0.5 rounded bg-gray-100 text-gray-500">이 인증서</span>
                  )}
                  {cert?.validation_status && (
                    <StatusBadge status={cert.validation_status} />
                  )}
                </div>
              </div>

              {/* 연결 화살표 (마지막 노드 제외) */}
              {i < nodes.length - 1 && (
                <div className="flex items-center gap-2 ml-5 py-1">
                  <div className="flex flex-col items-center">
                    <div className="w-px h-2 bg-gray-300" />
                    <ArrowDown className="w-3 h-3 text-gray-400" />
                    <div className="w-px h-2 bg-gray-300" />
                  </div>
                  <span className="text-[10px] text-gray-400">발급</span>
                </div>
              )}
            </div>
          );
        })}
      </div>

      {/* 체인 유효성 요약 */}
      <div className={cn(
        'mt-3 flex items-center gap-2 px-3 py-2 rounded-lg text-xs border',
        chain.chainValid
          ? 'bg-green-50 border-green-200 text-green-700'
          : 'bg-red-50 border-red-200 text-red-700',
      )}>
        {chain.chainValid
          ? <CheckCircle className="w-3.5 h-3.5 flex-shrink-0" />
          : <XCircle     className="w-3.5 h-3.5 flex-shrink-0" />}
        <span className="font-medium">{chain.message}</span>
      </div>
    </div>
  );
}

// ─── InfoRow ──────────────────────────────────────────────────────────────────

function InfoRow({
  label, value, mono = false, tooltip,
}: {
  label: string; value?: string | null; mono?: boolean; tooltip?: React.ReactNode;
}) {
  if (!value) return null;
  return (
    <div className="flex items-start gap-2 py-1.5 border-b border-gray-50 last:border-0">
      <span className="text-xs text-gray-500 w-32 flex-shrink-0 pt-0.5 flex items-center gap-1">
        {label}
        {tooltip && <InfoTooltip content={tooltip} />}
      </span>
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
    ? Object.entries(parsedPermissions).filter(([, v]) =>  v).map(([k]) => k)
    : [];
  const deniedPerms  = parsedPermissions
    ? Object.entries(parsedPermissions).filter(([, v]) => !v).map(([k]) => k)
    : [];

  const chatChildren: TreeNode[] = [
    { id: 'chat-oid',  name: 'Role OID',  value: cert.chat_oid   || '(없음)', icon: 'hash',   copyable: !!cert.chat_oid },
    { id: 'chat-role', name: 'Role',       value: cert.chat_role  || '(없음)', icon: 'shield' },
  ];
  if (grantedPerms.length > 0) chatChildren.push({
    id: 'chat-granted', name: `Granted Permissions (${grantedPerms.length})`, icon: 'check-circle',
    children: grantedPerms.map((p, i) => ({ id: `gp-${i}`, name: p, icon: 'check' })),
  });
  if (deniedPerms.length > 0) chatChildren.push({
    id: 'chat-denied', name: `Denied Permissions (${deniedPerms.length})`, icon: 'x-circle',
    children: deniedPerms.map((p, i) => ({ id: `dp-${i}`, name: p, icon: 'x' })),
  });

  const bodyChildren: TreeNode[] = [
    { id: 'car',  name: 'CAR (Certification Authority Reference)', value: cert.car, icon: 'user', copyable: true },
    {
      id: 'pk', name: 'Public Key', icon: 'key',
      children: [
        { id: 'pk-algo',     name: 'Algorithm',     value: cert.public_key_algorithm || '(없음)', icon: 'shield' },
        { id: 'pk-algo-oid', name: 'Algorithm OID', value: cert.public_key_oid       || '(없음)', icon: 'hash', copyable: !!cert.public_key_oid },
      ],
    },
    { id: 'chr',  name: 'CHR (Certificate Holder Reference)', value: cert.chr, icon: 'user', copyable: true },
    { id: 'chat', name: 'CHAT (Certificate Holder Authorization Template)', icon: 'shield', children: chatChildren },
    {
      id: 'validity', name: 'Validity', icon: 'calendar',
      children: [
        { id: 'eff-date', name: 'Effective Date',  value: cert.effective_date  || '(없음)' },
        { id: 'exp-date', name: 'Expiration Date', value: cert.expiration_date || '(없음)' },
      ],
    },
  ];

  const rootChildren: TreeNode[] = [
    { id: 'body', name: 'Certificate Body (7F4E)', icon: 'file-text', children: bodyChildren },
    {
      id: 'sig', name: 'Signature', icon: 'shield',
      children: [
        { id: 'sig-valid', name: 'Signature Valid', value: cert.signature_valid !== undefined ? String(cert.signature_valid) : '-' },
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

  const tabCls = (active: boolean) => cn(
    'px-5 py-2 text-xs font-medium transition-colors border-b-2',
    active ? 'border-indigo-600 text-indigo-600 bg-white' : 'border-transparent text-gray-500 hover:text-gray-700',
  );

  return (
    <div className="rounded-xl bg-white border border-gray-200 shadow-sm overflow-hidden">
      {/* header */}
      <div className="flex items-center gap-3 px-5 py-3.5 bg-gradient-to-r from-indigo-50 to-purple-50 border-b border-indigo-100">
        <div className="w-9 h-9 rounded-lg bg-gradient-to-br from-indigo-500 to-purple-600 flex items-center justify-center flex-shrink-0">
          <FileKey className="w-5 h-5 text-white" />
        </div>
        <div className="flex-1 min-w-0">
          <p className="text-sm font-bold text-gray-900 truncate">{cert.chr ?? '-'}</p>
          <p className="text-[10px] text-gray-500">Certificate Holder Reference</p>
        </div>
        <div className="flex items-center gap-2 flex-shrink-0">
          {cert.cvc_type && (
            <span className={cn('px-2 py-0.5 text-[10px] font-bold rounded', CVC_TYPE_COLOR[cert.cvc_type] ?? 'bg-gray-100 text-gray-700')}>
              {CVC_TYPE_LABELS[cert.cvc_type] ?? cert.cvc_type}
            </span>
          )}
          {cert.signature_valid !== undefined && (
            cert.signature_valid
              ? <ShieldCheck className="w-4 h-4 text-green-500" />
              : <ShieldX     className="w-4 h-4 text-red-500" />
          )}
        </div>
      </div>

      {/* tabs */}
      <div className="flex border-b border-gray-200 px-2 bg-gray-50">
        <button onClick={() => setTab('general')} className={tabCls(tab === 'general')}>일반</button>
        <button onClick={() => setTab('detail')}  className={tabCls(tab === 'detail')}>상세</button>
      </div>

      {/* tab: 일반 */}
      {tab === 'general' && (
        <div className="px-5 py-3">
          <InfoRow label="CHR" value={cert.chr} mono tooltip={CHR_TOOLTIP} />
          <InfoRow label="CAR" value={cert.car} mono tooltip={CAR_TOOLTIP} />
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
              <span className="text-xs text-gray-500 w-32 flex-shrink-0 pt-0.5 flex items-center gap-1">
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

      {/* tab: 상세 */}
      {tab === 'detail' && (
        <div className="p-4">
          <TreeViewer data={buildCvcTree(cert)} height="420px" compact />
        </div>
      )}
    </div>
  );
}

// ─── Upload Tab ───────────────────────────────────────────────────────────────

type UploadStep = 'IDLE' | 'FILE_SELECTED' | 'PREVIEWING' | 'PREVIEW_READY' | 'PREVIEW_ERROR' | 'SAVING' | 'DONE' | 'SAVE_ERROR';

function UploadTab({ onUploaded }: { onUploaded: () => void }) {
  const [file,      setFile]      = useState<File | null>(null);
  const [preview,   setPreview]   = useState<Partial<CvcCertificate> | null>(null);
  const [step,      setStep]      = useState<UploadStep>('IDLE');
  const [message,   setMessage]   = useState('');
  const [isDragging,setIsDragging]= useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const handleFileSelect = (f: File) => { setFile(f); setPreview(null); setStep('FILE_SELECTED'); setMessage(''); };
  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault(); setIsDragging(false);
    const f = e.dataTransfer.files[0]; if (f) handleFileSelect(f);
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
    setFile(null); setPreview(null); setStep('IDLE'); setMessage('');
    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  const stepDone = step === 'PREVIEW_READY' || step === 'SAVING' || step === 'DONE' || step === 'SAVE_ERROR';

  return (
    <div className="max-w-3xl mx-auto space-y-4 py-2">

      {/* Step 1 */}
      <div className="rounded-xl bg-white border border-gray-200 shadow-sm p-5">
        <div className="flex items-center gap-2 mb-4">
          <div className="w-6 h-6 rounded-full bg-indigo-100 flex items-center justify-center text-xs font-bold text-indigo-600">1</div>
          <h2 className="text-sm font-bold text-gray-900">CVC 파일 선택</h2>
        </div>

        <div
          className={cn(
            'relative border-2 border-dashed rounded-xl text-center cursor-pointer transition-all duration-200',
            file ? 'p-4 border-blue-300 bg-blue-50/50' : 'p-8',
            isDragging && 'border-indigo-500 bg-indigo-50 scale-[1.01]',
            !file && !isDragging && 'border-gray-300 hover:border-indigo-400 hover:bg-gray-50',
          )}
          onDragOver={(e) => { e.preventDefault(); setIsDragging(true); }}
          onDragLeave={(e) => { e.preventDefault(); setIsDragging(false); }}
          onDrop={handleDrop}
          onClick={() => fileInputRef.current?.click()}
        >
          <input ref={fileInputRef} type="file" accept=".cvc,.cvcert,.bin,.der" className="hidden"
            onChange={(e) => { const f = e.target.files?.[0]; if (f) handleFileSelect(f); }} />
          {file ? (
            <div className="flex items-center justify-center gap-3">
              <FileText className="w-8 h-8 text-blue-500 flex-shrink-0" />
              <div className="flex items-center gap-2 min-w-0">
                <span className="px-1.5 py-0.5 text-[10px] font-bold rounded bg-indigo-100 text-indigo-700 flex-shrink-0">CVC</span>
                <span className="text-sm font-semibold text-gray-900 truncate">{file.name}</span>
                <span className="text-xs text-gray-400 flex-shrink-0">({formatFileSize(file.size)})</span>
              </div>
              <span className="text-[10px] text-gray-400 flex-shrink-0">클릭하여 변경</span>
            </div>
          ) : (
            <div className="flex flex-col items-center gap-2">
              <CloudUpload className="w-10 h-10 text-gray-300" />
              <p className="text-sm text-gray-500">파일을 드래그하거나 클릭하여 선택</p>
              <p className="text-[11px] text-gray-400">.cvc .cvcert .bin .der</p>
            </div>
          )}
        </div>

        {step === 'FILE_SELECTED' && (
          <div className="mt-4 flex justify-end">
            <button onClick={handlePreview} className="flex items-center gap-2 px-5 py-2 bg-indigo-600 hover:bg-indigo-700 text-white rounded-lg text-sm font-medium transition-colors">
              <Eye className="w-4 h-4" /> 미리보기
            </button>
          </div>
        )}
        {step === 'PREVIEWING' && (
          <div className="mt-4 flex items-center justify-center gap-2 text-indigo-600">
            <Loader2 className="w-4 h-4 animate-spin" /><span className="text-sm">파싱 중…</span>
          </div>
        )}
      </div>

      {/* Step 2 */}
      {stepDone && preview && (
        <div className="space-y-3">
          <div className="flex items-center gap-2">
            <div className="w-6 h-6 rounded-full bg-blue-100 flex items-center justify-center text-xs font-bold text-blue-600">2</div>
            <h2 className="text-sm font-bold text-gray-900">파싱 결과</h2>
            {preview.cvc_type && (
              <span className={cn('ml-auto px-2 py-0.5 text-[10px] font-bold rounded', CVC_TYPE_COLOR[preview.cvc_type] ?? 'bg-gray-100 text-gray-700')}>
                {CVC_TYPE_LABELS[preview.cvc_type] ?? preview.cvc_type}
              </span>
            )}
          </div>
          <CvcPreviewCard cert={preview} />
          {step === 'PREVIEW_READY' && (
            <div className="flex items-center justify-end gap-2 pt-1">
              <button onClick={handleReset} className="flex items-center gap-1.5 px-4 py-2 border border-gray-300 text-gray-600 hover:bg-gray-50 rounded-lg text-sm transition-colors">
                <RotateCcw className="w-3.5 h-3.5" /> 취소
              </button>
              <button onClick={handleSave} className="flex items-center gap-1.5 px-5 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg text-sm font-medium transition-colors">
                <Database className="w-3.5 h-3.5" /> DB 저장
              </button>
            </div>
          )}
          {step === 'SAVING' && (
            <div className="flex items-center justify-center gap-2 text-blue-600 py-2">
              <Loader2 className="w-4 h-4 animate-spin" /><span className="text-sm">저장 중…</span>
            </div>
          )}
        </div>
      )}

      {/* Step 3 */}
      {step === 'DONE' && (
        <div className="rounded-xl bg-green-50 border border-green-200 p-4 flex items-center gap-3">
          <div className="w-6 h-6 rounded-full bg-green-100 flex items-center justify-center text-xs font-bold text-green-600 flex-shrink-0">3</div>
          <CheckCircle className="w-5 h-5 text-green-500 flex-shrink-0" />
          <div className="flex-1 min-w-0">
            <p className="text-sm font-semibold text-green-700">저장 완료</p>
            <p className="text-xs text-gray-500 mt-0.5 truncate">{message}</p>
          </div>
          <button onClick={handleReset} className="flex items-center gap-1.5 px-3.5 py-2 border border-green-300 text-green-700 hover:bg-green-100 rounded-lg text-xs transition-colors flex-shrink-0">
            <Upload className="w-3 h-3" /> 추가 업로드
          </button>
        </div>
      )}

      {/* Errors */}
      {(step === 'PREVIEW_ERROR' || step === 'SAVE_ERROR') && (
        <div className="rounded-xl bg-red-50 border border-red-200 p-4 flex items-start gap-3">
          <XCircle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
          <div className="flex-1">
            <p className="text-sm font-semibold text-red-700">{step === 'PREVIEW_ERROR' ? '파싱 오류' : '저장 오류'}</p>
            <p className="text-xs text-red-600 mt-0.5">{message}</p>
          </div>
          <button onClick={handleReset} className="flex items-center gap-1 text-xs text-red-600 hover:text-red-800">
            <RotateCcw className="w-3 h-3" /> 다시 시도
          </button>
        </div>
      )}
    </div>
  );
}

// ─── Certificate row ──────────────────────────────────────────────────────────

function CertRow({ cert, onDeleted }: { cert: CvcCertificate; onDeleted: () => void }) {
  const [open,          setOpen]          = useState(false);
  const [chain,         setChain]         = useState<ChainResult | null>(null);
  const [chainLoading,  setChainLoading]  = useState(false);
  const [deleteConfirm, setDeleteConfirm] = useState(false);
  const [deleting,      setDeleting]      = useState(false);

  const loadChain = async () => {
    if (chain) { setOpen(v => !v); return; }
    setChainLoading(true);
    try {
      const res = await getEacChain(cert.id);
      setChain(res.data.chain ?? null);
    } catch { /* ignore */ }
    finally { setChainLoading(false); setOpen(true); }
  };

  const handleDelete = async () => {
    setDeleting(true);
    try {
      await deleteEacCertificate(cert.id);
      onDeleted();
    } catch {
      setDeleting(false);
      setDeleteConfirm(false);
    }
  };

  return (
    <>
      <tr className="border-t border-gray-100 hover:bg-gray-50 text-xs">
        <td className="px-4 py-2.5 font-medium text-gray-700">{cert.country_code}</td>
        <td className="px-4 py-2.5">
          <span className={cn('px-1.5 py-0.5 rounded text-[11px] font-medium', CVC_TYPE_COLOR[cert.cvc_type] ?? 'bg-gray-100 text-gray-700')}>
            {CVC_TYPE_LABELS[cert.cvc_type] ?? cert.cvc_type}
          </span>
        </td>
        <td className="px-4 py-2.5 font-mono text-gray-700">{cert.chr}</td>
        <td className="px-4 py-2.5 font-mono text-gray-500">{cert.car}</td>
        <td className="px-4 py-2.5 text-gray-600">{cert.public_key_algorithm || <span className="text-gray-300 italic">—</span>}</td>
        <td className="px-4 py-2.5 text-gray-600">{cert.effective_date}</td>
        <td className="px-4 py-2.5 text-gray-600">{cert.expiration_date}</td>
        <td className="px-4 py-2.5"><StatusBadge status={cert.validation_status} /></td>
        <td className="px-4 py-2.5">
          <button onClick={loadChain} className="text-blue-600 hover:underline flex items-center gap-1">
            {chainLoading ? <Loader2 className="w-3 h-3 animate-spin" /> : open ? <ChevronUp className="w-3 h-3" /> : <ChevronDown className="w-3 h-3" />}
            체인
          </button>
        </td>
        <td className="px-4 py-2.5">
          {deleteConfirm ? (
            <div className="flex items-center gap-1">
              <button
                onClick={handleDelete}
                disabled={deleting}
                className="px-1.5 py-0.5 bg-red-500 text-white text-[10px] rounded hover:bg-red-600 disabled:opacity-50 flex items-center gap-0.5"
              >
                {deleting ? <Loader2 className="w-2.5 h-2.5 animate-spin" /> : null}
                확인
              </button>
              <button
                onClick={() => setDeleteConfirm(false)}
                className="px-1.5 py-0.5 bg-gray-200 text-gray-600 text-[10px] rounded hover:bg-gray-300"
              >
                취소
              </button>
            </div>
          ) : (
            <button
              onClick={() => setDeleteConfirm(true)}
              className="text-gray-300 hover:text-red-400 transition-colors"
              title="삭제 (재업로드 가능)"
            >
              <Trash2 className="w-3.5 h-3.5" />
            </button>
          )}
        </td>
      </tr>
      {open && chain && (
        <tr className="border-t border-gray-100 bg-gray-50/60">
          <td colSpan={10} className="px-5 py-4">
            <ChainDiagram chain={chain} />
          </td>
        </tr>
      )}
    </>
  );
}

// ─── Certificate List Tab ─────────────────────────────────────────────────────

function CertListTab({
  countries, certs, total, totalPages, page, country, type,
  onCountry, onType, onPage, onRefresh,
}: {
  countries: { country_code: string }[];
  certs: CvcCertificate[];
  total: number; totalPages: number; page: number; country: string; type: string;
  onCountry: (v: string) => void; onType: (v: string) => void; onPage: (v: number) => void;
  onRefresh: () => void;
}) {
  const selectCls = 'border border-gray-200 rounded-lg px-3 py-1.5 text-xs text-gray-700 bg-white hover:border-gray-300 focus:outline-none focus:border-indigo-400';

  return (
    <div className="space-y-3 py-2">
      {/* filter bar */}
      <div className="flex flex-wrap items-center gap-2">
        <Search className="w-4 h-4 text-gray-400" />
        <select value={country} onChange={e => { onCountry(e.target.value); onPage(1); }} className={selectCls}>
          <option value="">전체 국가</option>
          {countries.map(c => <option key={c.country_code} value={c.country_code}>{c.country_code}</option>)}
        </select>
        <select value={type} onChange={e => { onType(e.target.value); onPage(1); }} className={selectCls}>
          <option value="">전체 유형</option>
          {Object.entries(CVC_TYPE_LABELS).map(([k, v]) => <option key={k} value={k}>{v}</option>)}
        </select>
        <span className="ml-auto text-xs text-gray-400">총 {total.toLocaleString()}건</span>
      </div>

      {/* table */}
      <div className="bg-white rounded-xl border border-gray-200 shadow-sm overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full">
            <thead className="bg-slate-100 dark:bg-gray-700">
              <tr className="bg-gray-50 text-center text-[11px] text-gray-500 uppercase tracking-wide">
                <th className="px-4 py-2.5">국가</th>
                <th className="px-4 py-2.5">유형</th>
                <th className="px-4 py-2.5 whitespace-nowrap">
                  <span className="flex items-center gap-1">
                    CHR <InfoTooltip content={CHR_TOOLTIP} side="bottom" />
                  </span>
                </th>
                <th className="px-4 py-2.5 whitespace-nowrap">
                  <span className="flex items-center gap-1">
                    CAR <InfoTooltip content={CAR_TOOLTIP} side="bottom" />
                  </span>
                </th>
                <th className="px-4 py-2.5">알고리즘</th>
                <th className="px-4 py-2.5">유효 시작일</th>
                <th className="px-4 py-2.5">만료일</th>
                <th className="px-4 py-2.5">상태</th>
                <th className="px-4 py-2.5">신뢰체인</th>
                <th className="px-4 py-2.5"></th>
              </tr>
            </thead>
            <tbody>
              {certs.length === 0 ? (
                <tr>
                  <td colSpan={10} className="px-4 py-16 text-center">
                    <FileKey className="w-10 h-10 text-gray-200 mx-auto mb-2" />
                    <p className="text-sm text-gray-400">등록된 CVC 인증서가 없습니다.</p>
                  </td>
                </tr>
              ) : (
                certs.map(cert => <CertRow key={cert.id} cert={cert} onDeleted={onRefresh} />)
              )}
            </tbody>
          </table>
        </div>

        {totalPages > 1 && (
          <div className="px-4 py-3 border-t border-gray-100 flex items-center justify-between bg-gray-50">
            <button onClick={() => onPage(Math.max(1, page - 1))} disabled={page === 1}
              className="px-3 py-1.5 text-xs border border-gray-200 rounded-lg disabled:opacity-40 hover:bg-white transition-colors">이전</button>
            <span className="text-xs text-gray-500">{page} / {totalPages} 페이지</span>
            <button onClick={() => onPage(Math.min(totalPages, page + 1))} disabled={page === totalPages}
              className="px-3 py-1.5 text-xs border border-gray-200 rounded-lg disabled:opacity-40 hover:bg-white transition-colors">다음</button>
          </div>
        )}
      </div>
    </div>
  );
}

// ─── Main Page ────────────────────────────────────────────────────────────────

type PageTab = 'upload' | 'list';

export default function EacDashboard() {
  const [pageTab, setPageTab] = useState<PageTab>('upload');
  const [country, setCountry] = useState('');
  const [type,    setType]    = useState('');
  const [page,    setPage]    = useState(1);
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

  const stats      = statsData?.statistics;
  const countries  = countriesData?.countries ?? [];
  const certs      = certsData?.data          ?? [];
  const total      = certsData?.total         ?? 0;
  const totalPages = certsData?.totalPages    ?? 0;

  const handleUploaded = useCallback(() => {
    refetch();
    setPageTab('list');
  }, [refetch]);

  const tabCls = (active: boolean) => cn(
    'flex items-center gap-2 px-5 py-3 text-sm font-medium transition-colors border-b-2',
    active
      ? 'border-indigo-600 text-indigo-600'
      : 'border-transparent text-gray-500 hover:text-gray-700 hover:border-gray-300',
  );

  return (
    <div className="px-4 lg:px-6 py-4 space-y-4">

      {/* Gradient header + stats */}
      <div className="rounded-2xl bg-gradient-to-r from-indigo-600 to-purple-700 p-5 shadow-md">
        <div className="flex items-center gap-3 mb-4">
          <div className="p-2 rounded-xl bg-white/20">
            <FileKey className="w-5 h-5 text-white" />
          </div>
          <div>
            <h1 className="text-lg font-bold text-white">EAC 인증서 관리</h1>
            <p className="text-xs text-indigo-200">BSI TR-03110 · Card Verifiable Certificate</p>
          </div>
          <span className="ml-auto px-2.5 py-1 rounded-full text-[10px] font-semibold bg-white/20 text-white border border-white/30">
            EXPERIMENTAL
          </span>
        </div>
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
          {[
            { label: '전체 CVC', value: stats?.total        ?? 0, icon: <Shield        className="w-4 h-4 text-indigo-200" /> },
            { label: 'CVCA',    value: stats?.byType?.CVCA  ?? 0, icon: <Shield        className="w-4 h-4 text-purple-300" /> },
            { label: '유효',    value: stats?.validCount    ?? 0, icon: <CheckCircle   className="w-4 h-4 text-green-300"  /> },
            { label: '만료',    value: stats?.expiredCount  ?? 0, icon: <AlertTriangle className="w-4 h-4 text-yellow-300" /> },
          ].map(({ label, value, icon }) => (
            <div key={label} className="bg-white/10 rounded-xl px-4 py-3 flex items-center gap-3 border border-white/10">
              {icon}
              <div>
                <p className="text-[11px] text-indigo-200">{label}</p>
                <p className="text-xl font-bold text-white">{value.toLocaleString()}</p>
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Tab bar */}
      <div className="bg-white rounded-xl border border-gray-200 shadow-sm overflow-hidden">
        <div className="flex border-b border-gray-200 px-2">
          <button onClick={() => setPageTab('upload')} className={tabCls(pageTab === 'upload')}>
            <Upload className="w-4 h-4" /> CVC 업로드
          </button>
          <button onClick={() => setPageTab('list')} className={tabCls(pageTab === 'list')}>
            <List className="w-4 h-4" /> 인증서 목록
            {total > 0 && (
              <span className="ml-1 px-1.5 py-0.5 rounded-full text-[10px] font-bold bg-indigo-100 text-indigo-600">
                {total}
              </span>
            )}
          </button>
        </div>

        <div className="p-4 lg:p-5">
          {pageTab === 'upload' && <UploadTab onUploaded={handleUploaded} />}
          {pageTab === 'list'   && (
            <CertListTab
              countries={countries} certs={certs}
              total={total} totalPages={totalPages} page={page}
              country={country} type={type}
              onCountry={setCountry} onType={setType} onPage={setPage}
              onRefresh={refetch}
            />
          )}
        </div>
      </div>
    </div>
  );
}
