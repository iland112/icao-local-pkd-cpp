import { useState, useEffect, useCallback } from 'react';
import { Key, Plus, RefreshCw, Trash2, Edit2, Copy, Check, Shield, Clock, Activity, X, Eye, EyeOff, BarChart3, Inbox, CheckCircle, XCircle, User, Building2, Mail, Phone, FileText, Server, Monitor, Smartphone, HelpCircle, ChevronDown, AlertCircle } from 'lucide-react';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import { apiClientApi, type ApiClient, type UsageStats, type CreateApiClientRequest, type UpdateApiClientRequest } from '@/api/apiClientApi';
import { apiClientRequestApi, type ApiClientRequestItem, type ApproveRequestPayload } from '@/api/apiClientRequestApi';
import { toast } from '@/stores/toastStore';
import { formatDate } from '@/utils/dateFormat';
import { ConfirmDialog } from '@/components/common';

const AVAILABLE_PERMISSIONS = [
  { value: 'cert:read', label: '인증서 검색', desc: 'CSCA/DSC 인증서 조회' },
  { value: 'cert:export', label: '인증서 내보내기', desc: 'PEM/DER 형식 다운로드' },
  { value: 'pa:verify', label: 'PA 검증', desc: 'Passive Authentication 수행' },
  { value: 'pa:read', label: 'PA 이력 조회', desc: '검증 이력 및 결과 조회' },
  { value: 'upload:read', label: '업로드 조회', desc: '업로드 이력 및 상태 조회' },
  { value: 'upload:write', label: '파일 업로드', desc: 'LDIF/ML/인증서 파일 업로드' },
  { value: 'report:read', label: '보고서 조회', desc: 'DSC_NC/CRL/Trust Chain 보고서' },
  { value: 'ai:read', label: 'AI 분석 조회', desc: 'AI 인증서 포렌식 분석 결과' },
  { value: 'sync:read', label: 'Sync 조회', desc: 'DB-LDAP 동기화 상태' },
  { value: 'icao:read', label: 'ICAO 조회', desc: 'ICAO PKD 버전 상태' },
];

const DEVICE_TYPES = [
  { value: 'SERVER' as const, label: '서버', desc: '서버 간 통신 (고정 IP)', icon: Server },
  { value: 'DESKTOP' as const, label: '데스크톱', desc: '데스크톱 애플리케이션 (사내 IP)', icon: Monitor },
  { value: 'MOBILE' as const, label: '모바일', desc: '모바일 앱 (IP 제한 불가)', icon: Smartphone },
  { value: 'OTHER' as const, label: '기타', desc: '기타 환경', icon: HelpCircle },
];

export default function ApiClientManagement() {
  const [activeTab, setActiveTab] = useState<'clients' | 'requests'>('clients');
  const [clients, setClients] = useState<ApiClient[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [showCreate, setShowCreate] = useState(false);
  const [showEdit, setShowEdit] = useState<ApiClient | null>(null);
  const [showDelete, setShowDelete] = useState<ApiClient | null>(null);
  const [showKey, setShowKey] = useState<{ client: ApiClient; key: string } | null>(null);
  const [showUsage, setShowUsage] = useState<ApiClient | null>(null);
  const [showRegenConfirm, setShowRegenConfirm] = useState<ApiClient | null>(null);

  // Request tab state
  const [requests, setRequests] = useState<ApiClientRequestItem[]>([]);
  const [requestsLoading, setRequestsLoading] = useState(false);
  const [requestsTotal, setRequestsTotal] = useState(0);
  const [requestStatusFilter, setRequestStatusFilter] = useState('');
  const [showRequestDetail, setShowRequestDetail] = useState<ApiClientRequestItem | null>(null);
  const [showApproveKey, setShowApproveKey] = useState<{ clientName: string; apiKey: string } | null>(null);

  const fetchRequests = useCallback(async () => {
    setRequestsLoading(true);
    try {
      const res = await apiClientRequestApi.getAll(requestStatusFilter);
      setRequests(res.requests || []);
      setRequestsTotal(res.total || 0);
    } catch (e) {
      if (import.meta.env.DEV) console.error('Failed to fetch requests', e);
    } finally {
      setRequestsLoading(false);
    }
  }, [requestStatusFilter]);

  useEffect(() => {
    if (activeTab === 'requests') fetchRequests();
  }, [activeTab, fetchRequests]);

  const handleRegenerateConfirm = async (client: ApiClient) => {
    try {
      const res = await apiClientApi.regenerate(client.id);
      if (res.client.api_key) {
        setShowKey({ client: res.client, key: res.client.api_key });
      }
      fetchClients();
    } catch (e) {
      const axiosErr = e as { response?: { status?: number } };
      if (axiosErr.response?.status === 503 || axiosErr.response?.status === 429) {
        toast.warning('요청 제한', '요청이 너무 많습니다. 잠시 후 다시 시도해주세요.');
      } else {
        toast.error('재발급 실패', 'API Key 재발급에 실패했습니다.');
      }
    }
  };

  const fetchClients = useCallback(async () => {
    setLoading(true);
    try {
      const res = await apiClientApi.getAll();
      setClients(res.clients || []);
      setTotal(res.total || 0);
    } catch (e) {
      if (import.meta.env.DEV) console.error('Failed to fetch clients', e);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { fetchClients(); }, [fetchClients]);

  const activeCount = clients.filter(c => c.is_active).length;
  const todayRequests = clients.reduce((sum, c) => sum + c.total_requests, 0);

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg">
            <Key className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">API 클라이언트 관리</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">외부 시스템 API Key 발급 및 접근 권한 관리</p>
          </div>
        </div>
        {activeTab === 'clients' && (
          <button
            onClick={() => setShowCreate(true)}
            className="flex items-center gap-2 px-4 py-2.5 bg-blue-600 hover:bg-blue-700 text-white rounded-xl font-medium transition-colors"
          >
            <Plus className="w-4 h-4" />
            클라이언트 등록
          </button>
        )}
      </div>

      {/* Stats Cards (always visible above tabs) */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <StatCard label="전체 클라이언트" value={total} color="blue" />
        <StatCard label="활성" value={activeCount} color="green" />
        <StatCard label="비활성" value={total - activeCount} color="gray" />
        <StatCard label="총 누적 요청" value={todayRequests.toLocaleString()} color="purple" />
      </div>

      {/* Tabs */}
      <div className="flex gap-1 bg-gray-100 dark:bg-gray-800 rounded-xl p-1">
        {([
          { key: 'clients' as const, label: '등록된 클라이언트', icon: Shield, count: total },
          { key: 'requests' as const, label: '등록 요청', icon: Inbox, count: requestsTotal },
        ]).map(tab => (
          <button
            key={tab.key}
            onClick={() => setActiveTab(tab.key)}
            className={`flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium transition-colors flex-1 justify-center ${
              activeTab === tab.key
                ? 'bg-white dark:bg-gray-700 text-gray-900 dark:text-white shadow-sm'
                : 'text-gray-500 dark:text-gray-400 hover:text-gray-700 dark:hover:text-gray-200'
            }`}
          >
            <tab.icon className="w-4 h-4" />
            {tab.label}
            {tab.count > 0 && (
              <span className={`text-xs px-1.5 py-0.5 rounded-full ${
                activeTab === tab.key ? 'bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400' : 'bg-gray-200 text-gray-600 dark:bg-gray-600 dark:text-gray-300'
              }`}>
                {tab.count}
              </span>
            )}
          </button>
        ))}
      </div>

      {/* === Clients Tab === */}
      {activeTab === 'clients' && (
        <>
          {/* Client List */}
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
            <div className="px-6 py-4 border-b border-gray-200 dark:border-gray-700">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white">등록된 클라이언트</h2>
            </div>

            {loading ? (
              <div className="flex justify-center py-12">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-blue-600" />
              </div>
            ) : clients.length === 0 ? (
              <div className="text-center py-12 text-gray-500 dark:text-gray-400">
                등록된 API 클라이언트가 없습니다
              </div>
            ) : (
              <div className="divide-y divide-gray-200 dark:divide-gray-700">
                {clients.map(client => (
                  <ClientRow
                    key={client.id}
                    client={client}
                    onEdit={() => setShowEdit(client)}
                    onDelete={() => setShowDelete(client)}
                    onUsage={() => setShowUsage(client)}
                    onRegenerate={() => setShowRegenConfirm(client)}
                  />
                ))}
              </div>
            )}
          </div>
        </>
      )}

      {/* === Requests Tab === */}
      {activeTab === 'requests' && (
        <>
          {/* Status filter */}
          <div className="flex gap-2 mb-4">
            {[
              { value: '', label: '전체' },
              { value: 'PENDING', label: '대기 중' },
              { value: 'APPROVED', label: '승인' },
              { value: 'REJECTED', label: '거절' },
            ].map(opt => (
              <button
                key={opt.value}
                onClick={() => setRequestStatusFilter(opt.value)}
                className={`px-3 py-1.5 text-sm rounded-lg font-medium transition-colors ${
                  requestStatusFilter === opt.value
                    ? 'bg-blue-600 text-white'
                    : 'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 hover:bg-gray-200 dark:hover:bg-gray-600'
                }`}
              >
                {opt.label}
              </button>
            ))}
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
            <div className="px-6 py-4 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white">등록 요청 목록</h2>
              <button onClick={fetchRequests} className="p-2 text-gray-400 hover:text-gray-600 rounded-lg transition-colors">
                <RefreshCw className={`w-4 h-4 ${requestsLoading ? 'animate-spin' : ''}`} />
              </button>
            </div>

            {requestsLoading ? (
              <div className="flex justify-center py-12">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-blue-600" />
              </div>
            ) : requests.length === 0 ? (
              <div className="text-center py-12 text-gray-500 dark:text-gray-400">
                <Inbox className="w-10 h-10 mx-auto mb-3 text-gray-300 dark:text-gray-600" />
                등록 요청이 없습니다
              </div>
            ) : (
              <div className="divide-y divide-gray-200 dark:divide-gray-700">
                {requests.map(req => (
                  <RequestRow key={req.id} request={req} onClick={() => setShowRequestDetail(req)} />
                ))}
              </div>
            )}
          </div>
        </>
      )}

      {/* Dialogs */}
      <ConfirmDialog
        isOpen={showRegenConfirm !== null}
        onClose={() => setShowRegenConfirm(null)}
        onConfirm={() => {
          if (showRegenConfirm) handleRegenerateConfirm(showRegenConfirm);
        }}
        title="API Key 재발급"
        message="API Key를 재발급하시겠습니까? 기존 키는 즉시 무효화됩니다."
        confirmLabel="재발급"
        variant="warning"
      />
      {showCreate && (
        <CreateDialog
          onClose={() => setShowCreate(false)}
          onCreated={(client, key) => {
            setShowCreate(false);
            setShowKey({ client, key });
            fetchClients();
          }}
        />
      )}
      {showEdit && (
        <EditDialog
          client={showEdit}
          onClose={() => setShowEdit(null)}
          onUpdated={() => { setShowEdit(null); fetchClients(); }}
        />
      )}
      {showDelete && (
        <DeleteDialog
          client={showDelete}
          onClose={() => setShowDelete(null)}
          onDeleted={() => { setShowDelete(null); fetchClients(); }}
        />
      )}
      {showKey && (
        <ApiKeyDialog
          clientName={showKey.client.client_name}
          apiKey={showKey.key}
          onClose={() => setShowKey(null)}
        />
      )}
      {showUsage && (
        <UsageDialog
          client={showUsage}
          onClose={() => setShowUsage(null)}
        />
      )}
      {showRequestDetail && (
        <RequestDetailDialog
          request={showRequestDetail}
          onClose={() => setShowRequestDetail(null)}
          onApproved={(apiKey) => {
            setShowRequestDetail(null);
            if (apiKey) setShowApproveKey({ clientName: showRequestDetail.client_name, apiKey });
            fetchRequests();
            fetchClients();
          }}
          onRejected={() => { setShowRequestDetail(null); fetchRequests(); }}
        />
      )}
      {showApproveKey && (
        <ApiKeyDialog
          clientName={showApproveKey.clientName}
          apiKey={showApproveKey.apiKey}
          onClose={() => setShowApproveKey(null)}
        />
      )}
    </div>
  );
}

// ============================================================================
// Sub-components
// ============================================================================

function StatCard({ label, value, color }: { label: string; value: number | string; color: string }) {
  const colors: Record<string, string> = {
    blue: 'border-blue-500 text-blue-600',
    green: 'border-green-500 text-green-600',
    gray: 'border-gray-400 text-gray-500',
    purple: 'border-purple-500 text-purple-600',
  };
  return (
    <div className={`bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 ${colors[color] || colors.blue}`}>
      <p className="text-sm text-gray-500 dark:text-gray-400">{label}</p>
      <p className="text-2xl font-bold mt-1">{value}</p>
    </div>
  );
}

function ClientRow({ client, onEdit, onDelete, onUsage, onRegenerate }: {
  client: ApiClient;
  onEdit: () => void;
  onDelete: () => void;
  onUsage: () => void;
  onRegenerate: () => void;
}) {
  return (
    <div className="px-6 py-4 flex items-center justify-between hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors">
      <div className="flex items-center gap-4 min-w-0 flex-1">
        <div className={`p-2 rounded-lg ${client.is_active ? 'bg-green-100 dark:bg-green-900/30' : 'bg-gray-100 dark:bg-gray-700'}`}>
          <Shield className={`w-5 h-5 ${client.is_active ? 'text-green-600' : 'text-gray-400'}`} />
        </div>
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            <span className="font-semibold text-gray-900 dark:text-white truncate">{client.client_name}</span>
            <span className={`text-xs px-2 py-0.5 rounded-full ${client.is_active ? 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400' : 'bg-gray-100 text-gray-500 dark:bg-gray-700 dark:text-gray-400'}`}>
              {client.is_active ? '활성' : '비활성'}
            </span>
          </div>
          <div className="flex items-center gap-3 text-xs text-gray-500 dark:text-gray-400 mt-1">
            <span className="font-mono bg-gray-100 dark:bg-gray-700 px-1.5 py-0.5 rounded">{client.api_key_prefix}...</span>
            <span className="flex items-center gap-1"><Activity className="w-3 h-3" />{client.total_requests.toLocaleString()} 요청</span>
            {client.last_used_at && (
              <span className="flex items-center gap-1"><Clock className="w-3 h-3" />최근: {formatDate(client.last_used_at)}</span>
            )}
          </div>
          <div className="flex flex-wrap gap-1 mt-1.5">
            {client.permissions.map(p => (
              <span key={p} className="text-xs px-1.5 py-0.5 bg-blue-50 text-blue-600 dark:bg-blue-900/20 dark:text-blue-400 rounded">
                {p}
              </span>
            ))}
          </div>
        </div>
      </div>
      <div className="flex items-center gap-1 ml-4">
        <button onClick={onUsage} title="사용 이력" className="p-2 text-gray-400 hover:text-purple-600 hover:bg-purple-50 dark:hover:bg-purple-900/20 rounded-lg transition-colors">
          <BarChart3 className="w-4 h-4" />
        </button>
        <button onClick={onRegenerate} title="Key 재발급" className="p-2 text-gray-400 hover:text-amber-600 hover:bg-amber-50 dark:hover:bg-amber-900/20 rounded-lg transition-colors">
          <RefreshCw className="w-4 h-4" />
        </button>
        <button onClick={onEdit} title="수정" className="p-2 text-gray-400 hover:text-blue-600 hover:bg-blue-50 dark:hover:bg-blue-900/20 rounded-lg transition-colors">
          <Edit2 className="w-4 h-4" />
        </button>
        <button onClick={onDelete} title="비활성화" className="p-2 text-gray-400 hover:text-red-600 hover:bg-red-50 dark:hover:bg-red-900/20 rounded-lg transition-colors">
          <Trash2 className="w-4 h-4" />
        </button>
      </div>
    </div>
  );
}

// ============================================================================
// Dialogs
// ============================================================================

function CreateDialog({ onClose, onCreated }: {
  onClose: () => void;
  onCreated: (client: ApiClient, key: string) => void;
}) {
  const [form, setForm] = useState<CreateApiClientRequest>({
    client_name: '',
    description: '',
    permissions: [],
    allowed_ips: [],
    rate_limit_per_minute: 60,
    rate_limit_per_hour: 1000,
    rate_limit_per_day: 10000,
  });
  const [ipsText, setIpsText] = useState('');
  const [deviceType, setDeviceType] = useState<'SERVER' | 'DESKTOP' | 'MOBILE' | 'OTHER'>('SERVER');
  const [showRequester, setShowRequester] = useState(false);
  const [requesterName, setRequesterName] = useState('');
  const [requesterOrg, setRequesterOrg] = useState('');
  const [requesterPhone, setRequesterPhone] = useState('');
  const [requesterEmail, setRequesterEmail] = useState('');
  const [requestReason, setRequestReason] = useState('');
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  const showIpField = deviceType === 'SERVER' || deviceType === 'DESKTOP';

  const handleSubmit = async () => {
    if (!form.client_name.trim()) return;
    setSaving(true);
    setError('');
    try {
      const req: CreateApiClientRequest = {
        ...form,
        allowed_ips: showIpField && ipsText ? ipsText.split(',').map(s => s.trim()).filter(Boolean) : [],
      };
      const res = await apiClientApi.create(req);
      if (res.success && res.client.api_key) {
        onCreated(res.client, res.client.api_key);
      } else {
        setError('API Key 생성에 실패했습니다.');
      }
    } catch (e: unknown) {
      const axiosErr = e as { response?: { status?: number; data?: { message?: string } } };
      if (axiosErr.response?.status === 503 || axiosErr.response?.status === 429) {
        setError('요청이 너무 많습니다. 잠시 후 다시 시도해주세요.');
      } else if (axiosErr.response?.data?.message) {
        setError(axiosErr.response.data.message);
      } else {
        setError('API Key 생성 중 오류가 발생했습니다.');
      }
    } finally {
      setSaving(false);
    }
  };

  return (
    <DialogWrapper onClose={onClose} title="API 클라이언트 등록" wide>
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-5 max-h-[70vh] overflow-y-auto px-0.5">
        {/* Left Column: Client Info + Device + IPs + Rate Limits */}
        <div className="space-y-4">
          {/* Client Config */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">클라이언트 설정</h4>
            <div className="space-y-3">
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                <InputField label="클라이언트 이름" value={form.client_name} onChange={v => setForm({ ...form, client_name: v })} placeholder="출입국관리시스템" required />
                <InputField label="설명" value={form.description || ''} onChange={v => setForm({ ...form, description: v })} placeholder="API 사용 용도" />
              </div>
            </div>
          </div>

          {/* Device Type */}
          <div>
            <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">사용 기기 타입</span>
            <div className="grid grid-cols-4 gap-1.5">
              {DEVICE_TYPES.map(dt => {
                const Icon = dt.icon;
                const selected = deviceType === dt.value;
                return (
                  <button
                    key={dt.value}
                    type="button"
                    onClick={() => setDeviceType(dt.value)}
                    className={`flex flex-col items-center gap-1 p-2 rounded-xl border-2 transition-all text-center ${
                      selected
                        ? 'border-blue-500 bg-blue-50 dark:bg-blue-900/20'
                        : 'border-gray-200 dark:border-gray-600 hover:border-gray-300 dark:hover:border-gray-500'
                    }`}
                  >
                    <Icon className={`w-4 h-4 ${selected ? 'text-blue-600 dark:text-blue-400' : 'text-gray-400'}`} />
                    <span className={`text-xs font-medium ${selected ? 'text-blue-700 dark:text-blue-300' : 'text-gray-700 dark:text-gray-300'}`}>{dt.label}</span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Allowed IPs */}
          {showIpField && (
            <div>
              <InputField label="허용 IP (콤마 구분)" value={ipsText} onChange={setIpsText} placeholder="10.0.0.0/24, 192.168.1.100" />
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">비워두면 모든 IP에서 접근 허용</p>
            </div>
          )}
          {deviceType === 'MOBILE' && (
            <div className="flex items-start gap-2 p-2.5 bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-xl">
              <AlertCircle className="w-4 h-4 text-amber-500 flex-shrink-0 mt-0.5" />
              <p className="text-xs text-amber-700 dark:text-amber-300">모바일 기기는 IP 유동으로 IP 제한이 적용되지 않습니다.</p>
            </div>
          )}

          {/* Rate Limits */}
          <div>
            <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Rate Limit</span>
            <div className="grid grid-cols-3 gap-2">
              <InputField label="분당" value={String(form.rate_limit_per_minute)} onChange={v => setForm({ ...form, rate_limit_per_minute: parseInt(v) || 60 })} type="number" />
              <InputField label="시간당" value={String(form.rate_limit_per_hour)} onChange={v => setForm({ ...form, rate_limit_per_hour: parseInt(v) || 1000 })} type="number" />
              <InputField label="일당" value={String(form.rate_limit_per_day)} onChange={v => setForm({ ...form, rate_limit_per_day: parseInt(v) || 10000 })} type="number" />
            </div>
          </div>

          {/* Optional: Requester Info (for offline registration) */}
          <div className="border-t border-gray-200 dark:border-gray-700 pt-3">
            <button
              type="button"
              onClick={() => setShowRequester(!showRequester)}
              className="flex items-center gap-2 text-sm font-medium text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200 transition-colors"
            >
              <ChevronDown className={`w-4 h-4 transition-transform ${showRequester ? '' : '-rotate-90'}`} />
              <User className="w-4 h-4" />
              요청자 정보 (오프라인 대리 등록 시)
            </button>
            {showRequester && (
              <div className="mt-3 space-y-3 pl-1">
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <InputField label="요청자 이름" value={requesterName} onChange={setRequesterName} placeholder="홍길동" />
                  <InputField label="소속 (기관·부서)" value={requesterOrg} onChange={setRequesterOrg} placeholder="법무부 출입국본부" />
                </div>
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <InputField label="연락처" value={requesterPhone} onChange={setRequesterPhone} placeholder="02-1234-5678" />
                  <InputField label="이메일" value={requesterEmail} onChange={setRequesterEmail} placeholder="hong@example.go.kr" type="email" />
                </div>
                <div>
                  <label htmlFor="create-reason" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">등록 사유</label>
                  <textarea
                    id="create-reason"
                    name="create-reason"
                    value={requestReason}
                    onChange={e => setRequestReason(e.target.value)}
                    rows={2}
                    placeholder="대리 등록 사유를 기재해 주세요"
                    className="w-full px-3 py-2 bg-gray-50 dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                  />
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Right Column: Permissions */}
        <div>
          <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">권한 설정</h4>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
            {AVAILABLE_PERMISSIONS.map(p => (
              <label key={p.value} htmlFor={`create-perm-${p.value}`} className={`flex items-start gap-2.5 p-2.5 rounded-lg cursor-pointer transition-colors ${
                form.permissions.includes(p.value)
                  ? 'bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-700'
                  : 'bg-gray-50 dark:bg-gray-700/50 border border-gray-200 dark:border-gray-600'
              }`}>
                <input
                  id={`create-perm-${p.value}`}
                  name={`permission-${p.value}`}
                  type="checkbox"
                  checked={form.permissions.includes(p.value)}
                  onChange={e => {
                    const perms = e.target.checked
                      ? [...form.permissions, p.value]
                      : form.permissions.filter(x => x !== p.value);
                    setForm({ ...form, permissions: perms });
                  }}
                  className="mt-0.5 w-4 h-4 rounded border-gray-300"
                />
                <div className="min-w-0">
                  <span className="text-sm font-medium text-gray-900 dark:text-white">{p.label}</span>
                  <p className="text-xs text-gray-500 dark:text-gray-400">{p.desc}</p>
                </div>
              </label>
            ))}
          </div>
          <div className="mt-3 px-3 py-2 bg-blue-50 dark:bg-blue-900/10 border border-blue-100 dark:border-blue-900/30 rounded-lg">
            <p className="text-xs text-blue-600 dark:text-blue-400">허용 엔드포인트, 사용 기간 등 고급 설정은 등록 후 수정 화면에서 설정할 수 있습니다.</p>
          </div>
        </div>
      </div>

      {error && (
        <div className="mt-4 p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl">
          <p className="text-sm text-red-700 dark:text-red-300">{error}</p>
        </div>
      )}

      <div className="flex justify-end gap-3 mt-5 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">취소</button>
        <button onClick={handleSubmit} disabled={saving || !form.client_name.trim()} className="flex items-center gap-2 px-5 py-2 bg-[#02385e] text-white rounded-xl hover:bg-[#024b7a] disabled:opacity-50 transition-colors active:scale-[0.98]">
          <Key className="w-4 h-4" />
          {saving ? '생성 중...' : 'API Key 발급'}
        </button>
      </div>
    </DialogWrapper>
  );
}

function EditDialog({ client, onClose, onUpdated }: {
  client: ApiClient;
  onClose: () => void;
  onUpdated: () => void;
}) {
  const [form, setForm] = useState<UpdateApiClientRequest>({
    client_name: client.client_name,
    description: client.description,
    permissions: [...client.permissions],
    allowed_ips: [...client.allowed_ips],
    rate_limit_per_minute: client.rate_limit_per_minute,
    rate_limit_per_hour: client.rate_limit_per_hour,
    rate_limit_per_day: client.rate_limit_per_day,
    is_active: client.is_active,
  });
  const [ipsText, setIpsText] = useState(client.allowed_ips.join(', '));
  const [saving, setSaving] = useState(false);
  const [editError, setEditError] = useState('');

  const handleSubmit = async () => {
    setSaving(true);
    setEditError('');
    try {
      const req = { ...form, allowed_ips: ipsText ? ipsText.split(',').map(s => s.trim()).filter(Boolean) : [] };
      await apiClientApi.update(client.id, req);
      onUpdated();
    } catch (e: any) {
      const msg = e?.response?.data?.message || e?.message || '수정에 실패했습니다.';
      setEditError(msg);
      if (import.meta.env.DEV) console.error('Update failed', e);
    } finally {
      setSaving(false);
    }
  };

  return (
    <DialogWrapper onClose={onClose} title="클라이언트 수정">
      {editError && (
        <div className="mb-4 p-3 bg-red-50 dark:bg-red-900/30 border border-red-200 dark:border-red-800 rounded-lg text-red-700 dark:text-red-300 text-sm">{editError}</div>
      )}
      <div className="space-y-4">
        <InputField label="클라이언트 이름" value={form.client_name || ''} onChange={v => setForm({ ...form, client_name: v })} />
        <InputField label="설명" value={form.description || ''} onChange={v => setForm({ ...form, description: v })} />

        <div>
          <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">권한</span>
          <div className="grid grid-cols-2 gap-2">
            {AVAILABLE_PERMISSIONS.map(p => (
              <label key={p.value} htmlFor={`edit-perm-${p.value}`} className="flex items-center gap-2 text-sm cursor-pointer">
                <input
                  id={`edit-perm-${p.value}`}
                  name={`permission-${p.value}`}
                  type="checkbox"
                  checked={form.permissions?.includes(p.value) || false}
                  onChange={e => {
                    const perms = e.target.checked
                      ? [...(form.permissions || []), p.value]
                      : (form.permissions || []).filter(x => x !== p.value);
                    setForm({ ...form, permissions: perms });
                  }}
                  className="rounded border-gray-300"
                />
                <span className="text-gray-700 dark:text-gray-300">{p.label}</span>
              </label>
            ))}
          </div>
        </div>

        <InputField label="허용 IP (콤마 구분)" value={ipsText} onChange={setIpsText} placeholder="비워두면 모든 IP 허용" />

        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
          <InputField label="분당 제한" value={String(form.rate_limit_per_minute)} onChange={v => setForm({ ...form, rate_limit_per_minute: parseInt(v) || 60 })} type="number" />
          <InputField label="시간당 제한" value={String(form.rate_limit_per_hour)} onChange={v => setForm({ ...form, rate_limit_per_hour: parseInt(v) || 1000 })} type="number" />
          <InputField label="일당 제한" value={String(form.rate_limit_per_day)} onChange={v => setForm({ ...form, rate_limit_per_day: parseInt(v) || 10000 })} type="number" />
        </div>

        <label htmlFor="edit-is-active" className="flex items-center gap-2 cursor-pointer">
          <input id="edit-is-active" name="isActive" type="checkbox" checked={form.is_active} onChange={e => setForm({ ...form, is_active: e.target.checked })} className="rounded border-gray-300" />
          <span className="text-sm text-gray-700 dark:text-gray-300">활성 상태</span>
        </label>
      </div>

      <div className="flex justify-end gap-3 mt-6 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">취소</button>
        <button onClick={handleSubmit} disabled={saving} className="px-4 py-2 bg-blue-600 text-white rounded-xl hover:bg-blue-700 disabled:opacity-50 transition-colors">
          {saving ? '저장 중...' : '저장'}
        </button>
      </div>
    </DialogWrapper>
  );
}

function DeleteDialog({ client, onClose, onDeleted }: {
  client: ApiClient;
  onClose: () => void;
  onDeleted: () => void;
}) {
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState('');

  return (
    <DialogWrapper onClose={onClose} title="클라이언트 비활성화" small>
      {deleteError && (
        <div className="mb-3 p-3 bg-red-50 dark:bg-red-900/30 border border-red-200 dark:border-red-800 rounded-lg text-red-700 dark:text-red-300 text-sm">{deleteError}</div>
      )}
      <div className="text-center py-4">
        <div className="w-14 h-14 mx-auto bg-red-100 dark:bg-red-900/30 rounded-full flex items-center justify-center mb-4">
          <Trash2 className="w-7 h-7 text-red-600" />
        </div>
        <p className="text-gray-700 dark:text-gray-300">
          <span className="font-semibold">{client.client_name}</span>을(를) 비활성화하시겠습니까?
        </p>
        <p className="text-sm text-gray-500 dark:text-gray-400 mt-2">해당 API Key로의 모든 접근이 차단됩니다.</p>
      </div>
      <div className="flex justify-center gap-3 mt-4">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">취소</button>
        <button
          onClick={async () => {
            setDeleting(true);
            setDeleteError('');
            try { await apiClientApi.deactivate(client.id); onDeleted(); }
            catch (e: any) {
              const msg = e?.response?.data?.message || e?.message || '비활성화에 실패했습니다.';
              setDeleteError(msg);
              if (import.meta.env.DEV) console.error('Delete failed', e);
            }
            finally { setDeleting(false); }
          }}
          disabled={deleting}
          className="px-4 py-2 bg-red-600 text-white rounded-xl hover:bg-red-700 disabled:opacity-50 transition-colors"
        >
          {deleting ? '처리 중...' : '비활성화'}
        </button>
      </div>
    </DialogWrapper>
  );
}

const PERIOD_OPTIONS = [
  { days: 7, label: '7일' },
  { days: 30, label: '30일' },
  { days: 90, label: '90일' },
];

const BAR_COLORS = ['#6366f1', '#8b5cf6', '#a78bfa', '#c4b5fd', '#ddd6fe', '#ede9fe', '#818cf8', '#7c3aed', '#6d28d9', '#5b21b6'];

function UsageDialog({ client, onClose }: {
  client: ApiClient;
  onClose: () => void;
}) {
  const [days, setDays] = useState(7);
  const [usage, setUsage] = useState<UsageStats | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    setLoading(true);
    apiClientApi.getUsage(client.id, days)
      .then(res => setUsage(res.usage))
      .catch(() => setUsage(null))
      .finally(() => setLoading(false));
  }, [client.id, days]);

  const totalRequests = usage?.totalRequests ?? 0;
  const endpoints = usage?.topEndpoints ?? [];

  return (
    <DialogWrapper onClose={onClose} title={`${client.client_name} — API 사용 이력`}>
      {/* Period selector */}
      <div className="flex gap-2 mb-3">
        {PERIOD_OPTIONS.map(opt => (
          <button
            key={opt.days}
            onClick={() => setDays(opt.days)}
            className={`px-3 py-1 text-sm rounded-lg font-medium transition-colors ${
              days === opt.days
                ? 'bg-indigo-600 text-white'
                : 'bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 hover:bg-gray-200 dark:hover:bg-gray-600'
            }`}
          >
            {opt.label}
          </button>
        ))}
      </div>

      {loading ? (
        <div className="flex justify-center py-12">
          <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-indigo-600" />
        </div>
      ) : totalRequests === 0 ? (
        <div className="text-center py-12">
          <Activity className="w-10 h-10 text-gray-300 dark:text-gray-600 mx-auto mb-3" />
          <p className="text-gray-500 dark:text-gray-400">최근 {days}일간 사용 이력이 없습니다</p>
        </div>
      ) : (
        <div className="space-y-3">
          {/* Summary cards */}
          <div className="grid grid-cols-2 gap-2">
            <div className="bg-indigo-50 dark:bg-indigo-900/20 rounded-lg p-3">
              <p className="text-xs text-indigo-600 dark:text-indigo-400 font-medium">총 요청</p>
              <p className="text-xl font-bold text-indigo-700 dark:text-indigo-300">{totalRequests.toLocaleString()}</p>
            </div>
            <div className="bg-purple-50 dark:bg-purple-900/20 rounded-lg p-3">
              <p className="text-xs text-purple-600 dark:text-purple-400 font-medium">사용 엔드포인트</p>
              <p className="text-xl font-bold text-purple-700 dark:text-purple-300">{endpoints.length}</p>
            </div>
          </div>

          {/* Horizontal bar chart */}
          <div>
            <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">엔드포인트별 요청 수</h4>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-2">
              <ResponsiveContainer width="100%" height={Math.max(endpoints.length * 28, 100)}>
                <BarChart data={endpoints} layout="vertical" margin={{ left: 0, right: 40, top: 2, bottom: 2 }}>
                  <XAxis type="number" hide />
                  <YAxis
                    type="category"
                    dataKey="endpoint"
                    width={160}
                    tick={{ fontSize: 10, fill: '#6b7280' }}
                    tickFormatter={(v: string) => v.length > 25 ? '...' + v.slice(-22) : v}
                  />
                  <Tooltip
                    formatter={(value) => [`${Number(value).toLocaleString()} 요청`, '']}
                    labelFormatter={(label) => String(label)}
                    contentStyle={{ borderRadius: '8px', border: 'none', boxShadow: '0 4px 12px rgba(0,0,0,0.1)' }}
                  />
                  <Bar dataKey="count" radius={[0, 4, 4, 0]} barSize={16}>
                    {endpoints.map((_entry, i) => (
                      <Cell key={i} fill={BAR_COLORS[i % BAR_COLORS.length]} />
                    ))}
                  </Bar>
                </BarChart>
              </ResponsiveContainer>
            </div>
          </div>

          {/* Table */}
          <div>
            <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">상세 내역</h4>
            <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
              <table className="w-full text-xs">
                <thead className="bg-gray-50 dark:bg-gray-700/80">
                  <tr>
                    <th className="text-left px-3 py-1.5 font-medium text-gray-600 dark:text-gray-300">엔드포인트</th>
                    <th className="text-right px-3 py-1.5 font-medium text-gray-600 dark:text-gray-300 w-20">요청 수</th>
                    <th className="text-right px-3 py-1.5 font-medium text-gray-600 dark:text-gray-300 w-16">비율</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-100 dark:divide-gray-700">
                  {endpoints.map((ep, i) => (
                    <tr key={ep.endpoint} className="hover:bg-gray-50 dark:hover:bg-gray-700/30">
                      <td className="px-3 py-1.5 font-mono text-xs text-gray-700 dark:text-gray-300">
                        <span className={`inline-flex items-center justify-center w-4.5 h-4.5 text-center text-[10px] font-bold rounded-full mr-1.5 ${i < 3 ? 'text-white' : 'text-gray-500 dark:text-gray-400 bg-gray-200 dark:bg-gray-600'}`} style={i < 3 ? { backgroundColor: BAR_COLORS[i], width: '18px', height: '18px' } : { width: '18px', height: '18px' }}>{i + 1}</span>
                        {ep.endpoint}
                      </td>
                      <td className="text-right px-3 py-1.5 font-semibold text-gray-900 dark:text-white">{ep.count.toLocaleString()}</td>
                      <td className="text-right px-3 py-1.5 text-gray-500 dark:text-gray-400">{(ep.count / totalRequests * 100).toFixed(1)}%</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      <div className="flex justify-end mt-3 pt-3 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">닫기</button>
      </div>
    </DialogWrapper>
  );
}

function ApiKeyDialog({ clientName, apiKey, onClose }: {
  clientName: string;
  apiKey: string;
  onClose: () => void;
}) {
  const [copied, setCopied] = useState(false);
  const [visible, setVisible] = useState(false);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(apiKey);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <DialogWrapper onClose={onClose} title="API Key 발급 완료">
      <div className="space-y-4">
        <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-xl p-4">
          <p className="text-sm text-amber-800 dark:text-amber-300 font-medium">
            이 API Key는 지금만 확인 가능합니다. 안전하게 보관하세요.
          </p>
        </div>

        <div>
          <p className="text-sm text-gray-500 dark:text-gray-400 mb-1">{clientName}</p>
          <div className="flex items-center gap-2">
            <code className="flex-1 bg-gray-100 dark:bg-gray-700 px-3 py-2 rounded-lg text-sm font-mono break-all text-gray-900 dark:text-white">
              {visible ? apiKey : apiKey.substring(0, 14) + '••••••••••••••••••••••••••••••••'}
            </code>
            <button onClick={() => setVisible(!visible)} className="p-2 text-gray-400 hover:text-gray-600 rounded-lg transition-colors">
              {visible ? <EyeOff className="w-4 h-4" /> : <Eye className="w-4 h-4" />}
            </button>
            <button onClick={handleCopy} className="p-2 text-gray-400 hover:text-blue-600 rounded-lg transition-colors">
              {copied ? <Check className="w-4 h-4 text-green-600" /> : <Copy className="w-4 h-4" />}
            </button>
          </div>
        </div>
      </div>

      <div className="flex justify-end mt-6 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 bg-blue-600 text-white rounded-xl hover:bg-blue-700 transition-colors">확인</button>
      </div>
    </DialogWrapper>
  );
}

// ============================================================================
// Request Components
// ============================================================================

const STATUS_STYLES: Record<string, string> = {
  PENDING: 'bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400',
  APPROVED: 'bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400',
  REJECTED: 'bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400',
};

const STATUS_LABELS: Record<string, string> = {
  PENDING: '대기 중',
  APPROVED: '승인',
  REJECTED: '거절',
};

const DEVICE_TYPE_INFO: Record<string, { label: string; icon: typeof Server }> = {
  SERVER: { label: '서버', icon: Server },
  DESKTOP: { label: '데스크톱', icon: Monitor },
  MOBILE: { label: '모바일', icon: Smartphone },
  OTHER: { label: '기타', icon: HelpCircle },
};

function RequestRow({ request, onClick }: { request: ApiClientRequestItem; onClick: () => void }) {
  return (
    <div
      onClick={onClick}
      className="px-6 py-4 flex items-center justify-between hover:bg-gray-50 dark:hover:bg-gray-700/50 transition-colors cursor-pointer"
    >
      <div className="flex items-center gap-4 min-w-0 flex-1">
        <div className="p-2 rounded-lg bg-blue-50 dark:bg-blue-900/20">
          <User className="w-5 h-5 text-blue-600 dark:text-blue-400" />
        </div>
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            <span className="font-semibold text-gray-900 dark:text-white truncate">{request.client_name}</span>
            <span className={`text-xs px-2 py-0.5 rounded-full font-medium ${STATUS_STYLES[request.status] || ''}`}>
              {STATUS_LABELS[request.status] || request.status}
            </span>
          </div>
          <div className="flex items-center gap-3 text-xs text-gray-500 dark:text-gray-400 mt-1">
            <span className="flex items-center gap-1"><User className="w-3 h-3" />{request.requester_name}</span>
            <span className="flex items-center gap-1"><Building2 className="w-3 h-3" />{request.requester_org}</span>
            {(() => { const dt = DEVICE_TYPE_INFO[request.device_type]; if (!dt) return null; const Icon = dt.icon; return <span className="flex items-center gap-1"><Icon className="w-3 h-3" />{dt.label}</span>; })()}
            <span className="flex items-center gap-1"><Clock className="w-3 h-3" />{formatDate(request.created_at)}</span>
          </div>
          <div className="flex flex-wrap gap-1 mt-1.5">
            {request.permissions.slice(0, 5).map(p => (
              <span key={p} className="text-xs px-1.5 py-0.5 bg-blue-50 text-blue-600 dark:bg-blue-900/20 dark:text-blue-400 rounded">{p}</span>
            ))}
            {request.permissions.length > 5 && (
              <span className="text-xs px-1.5 py-0.5 bg-gray-100 text-gray-500 dark:bg-gray-700 dark:text-gray-400 rounded">+{request.permissions.length - 5}</span>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

function RequestDetailDialog({ request, onClose, onApproved, onRejected }: {
  request: ApiClientRequestItem;
  onClose: () => void;
  onApproved: (apiKey?: string) => void;
  onRejected: () => void;
}) {
  const [reviewComment, setReviewComment] = useState('');
  const [processing, setProcessing] = useState(false);
  const [error, setError] = useState('');

  // Admin-configured settings (only used at approval)
  const [rateLimitPerMinute, setRateLimitPerMinute] = useState(60);
  const [rateLimitPerHour, setRateLimitPerHour] = useState(1000);
  const [rateLimitPerDay, setRateLimitPerDay] = useState(10000);
  const [requestedDays, setRequestedDays] = useState(365);
  const [allowedEndpointsText, setAllowedEndpointsText] = useState('');

  const handleApprove = async () => {
    setProcessing(true);
    setError('');
    try {
      const payload: ApproveRequestPayload = {
        review_comment: reviewComment,
        rate_limit_per_minute: rateLimitPerMinute,
        rate_limit_per_hour: rateLimitPerHour,
        rate_limit_per_day: rateLimitPerDay,
        requested_days: requestedDays,
        allowed_endpoints: allowedEndpointsText ? allowedEndpointsText.split(',').map(s => s.trim()).filter(Boolean) : [],
      };
      const res = await apiClientRequestApi.approve(request.id, payload);
      if (res.success) {
        toast.success('승인 완료', `${request.client_name} API 클라이언트가 생성되었습니다.`);
        onApproved(res.client?.api_key);
      } else {
        setError(res.message || '승인에 실패했습니다.');
      }
    } catch (e: any) {
      setError(e?.response?.data?.message || '승인 처리 중 오류가 발생했습니다.');
    } finally {
      setProcessing(false);
    }
  };

  const handleReject = async () => {
    if (!reviewComment.trim()) {
      setError('거절 사유를 입력해 주세요.');
      return;
    }
    setProcessing(true);
    setError('');
    try {
      const res = await apiClientRequestApi.reject(request.id, reviewComment);
      if (res.success) {
        toast.info('거절 처리', `${request.client_name} 요청이 거절되었습니다.`);
        onRejected();
      } else {
        setError(res.message || '거절에 실패했습니다.');
      }
    } catch (e: any) {
      setError(e?.response?.data?.message || '거절 처리 중 오류가 발생했습니다.');
    } finally {
      setProcessing(false);
    }
  };

  const isPending = request.status === 'PENDING';
  const dtInfo = DEVICE_TYPE_INFO[request.device_type];
  const DeviceIcon = dtInfo?.icon;

  return (
    <DialogWrapper onClose={onClose} title="등록 요청 상세">
      <div className="space-y-4 max-h-[70vh] overflow-y-auto">
        {/* Status badge */}
        <div className="flex items-center gap-2">
          <span className={`text-sm px-3 py-1 rounded-full font-medium ${STATUS_STYLES[request.status] || ''}`}>
            {STATUS_LABELS[request.status] || request.status}
          </span>
          <span className="text-xs text-gray-500 dark:text-gray-400">요청일: {formatDate(request.created_at)}</span>
        </div>

        {/* Requester Info */}
        <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4 space-y-2">
          <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">요청자 정보</h4>
          <div className="grid grid-cols-2 gap-2 text-sm">
            <div className="flex items-center gap-2 text-gray-600 dark:text-gray-300">
              <User className="w-3.5 h-3.5 text-gray-400" />{request.requester_name}
            </div>
            <div className="flex items-center gap-2 text-gray-600 dark:text-gray-300">
              <Building2 className="w-3.5 h-3.5 text-gray-400" />{request.requester_org}
            </div>
            <div className="flex items-center gap-2 text-gray-600 dark:text-gray-300">
              <Mail className="w-3.5 h-3.5 text-gray-400" />{request.requester_contact_email}
            </div>
            {request.requester_contact_phone && (
              <div className="flex items-center gap-2 text-gray-600 dark:text-gray-300">
                <Phone className="w-3.5 h-3.5 text-gray-400" />{request.requester_contact_phone}
              </div>
            )}
          </div>
          <div className="pt-2">
            <div className="flex items-start gap-2 text-sm text-gray-600 dark:text-gray-300">
              <FileText className="w-3.5 h-3.5 text-gray-400 mt-0.5 flex-shrink-0" />
              <span>{request.request_reason}</span>
            </div>
          </div>
        </div>

        {/* Client Config (from requester) */}
        <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4 space-y-2">
          <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">요청 클라이언트 설정</h4>
          <div className="text-sm space-y-1.5">
            <div><span className="text-gray-500 dark:text-gray-400">이름:</span> <span className="font-medium text-gray-900 dark:text-white">{request.client_name}</span></div>
            {request.description && <div><span className="text-gray-500 dark:text-gray-400">설명:</span> <span className="text-gray-700 dark:text-gray-300">{request.description}</span></div>}
            <div className="flex items-center gap-1.5">
              <span className="text-gray-500 dark:text-gray-400">기기 타입:</span>
              {DeviceIcon && <DeviceIcon className="w-3.5 h-3.5 text-gray-500" />}
              <span className="text-gray-700 dark:text-gray-300">{dtInfo?.label || request.device_type}</span>
            </div>
            <div className="flex items-center gap-1 flex-wrap">
              <span className="text-gray-500 dark:text-gray-400">권한:</span>
              {request.permissions.map(p => (
                <span key={p} className="text-xs px-1.5 py-0.5 bg-blue-50 text-blue-600 dark:bg-blue-900/20 dark:text-blue-400 rounded">{p}</span>
              ))}
            </div>
            {request.allowed_ips.length > 0 && (
              <div><span className="text-gray-500 dark:text-gray-400">제안 IP:</span> <span className="font-mono text-xs text-gray-700 dark:text-gray-300">{request.allowed_ips.join(', ')}</span></div>
            )}
          </div>
        </div>

        {/* Review section (for non-pending, show existing review) */}
        {!isPending && request.reviewed_by && (
          <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">검토 결과</h4>
            <div className="text-sm space-y-1">
              {request.reviewed_at && <div><span className="text-gray-500 dark:text-gray-400">검토일:</span> <span className="text-gray-700 dark:text-gray-300">{formatDate(request.reviewed_at)}</span></div>}
              {request.review_comment && <div><span className="text-gray-500 dark:text-gray-400">코멘트:</span> <span className="text-gray-700 dark:text-gray-300">{request.review_comment}</span></div>}
            </div>
          </div>
        )}

        {/* Admin approval settings (only for PENDING) */}
        {isPending && (
          <div className="bg-blue-50 dark:bg-blue-900/10 rounded-xl p-4 space-y-3">
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white">승인 설정 (관리자)</h4>
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
              <div>
                <label htmlFor="approve-rpm" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">분당 제한</label>
                <input id="approve-rpm" type="number" value={rateLimitPerMinute} onChange={e => setRateLimitPerMinute(parseInt(e.target.value) || 60)}
                  className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
              </div>
              <div>
                <label htmlFor="approve-rph" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">시간당 제한</label>
                <input id="approve-rph" type="number" value={rateLimitPerHour} onChange={e => setRateLimitPerHour(parseInt(e.target.value) || 1000)}
                  className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
              </div>
              <div>
                <label htmlFor="approve-rpd" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">일당 제한</label>
                <input id="approve-rpd" type="number" value={rateLimitPerDay} onChange={e => setRateLimitPerDay(parseInt(e.target.value) || 10000)}
                  className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
              </div>
            </div>
            <div>
              <label htmlFor="approve-days" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">사용 기간 (일)</label>
              <input id="approve-days" type="number" value={requestedDays} onChange={e => setRequestedDays(parseInt(e.target.value) || 365)}
                className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
            </div>
            <div>
              <label htmlFor="approve-endpoints" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">허용 엔드포인트 (콤마 구분, 비워두면 전체 허용)</label>
              <input id="approve-endpoints" type="text" value={allowedEndpointsText} onChange={e => setAllowedEndpointsText(e.target.value)}
                placeholder="/api/pa/*, /api/certificates/*"
                className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
            </div>
          </div>
        )}

        {/* Admin review comment */}
        {isPending && (
          <div>
            <label htmlFor="review-comment" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">검토 코멘트</label>
            <textarea
              id="review-comment"
              name="review-comment"
              value={reviewComment}
              onChange={e => setReviewComment(e.target.value)}
              rows={2}
              placeholder="승인 또는 거절 사유를 입력하세요 (거절 시 필수)"
              className="w-full px-3 py-2 bg-gray-50 dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 focus:border-transparent"
            />
          </div>
        )}

        {error && (
          <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl">
            <p className="text-sm text-red-700 dark:text-red-300">{error}</p>
          </div>
        )}
      </div>

      <div className="flex justify-end gap-3 mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">
          닫기
        </button>
        {isPending && (
          <>
            <button
              onClick={handleReject}
              disabled={processing}
              className="flex items-center gap-1.5 px-4 py-2 bg-red-600 text-white rounded-xl hover:bg-red-700 disabled:opacity-50 transition-colors"
            >
              <XCircle className="w-4 h-4" />
              {processing ? '처리 중...' : '거절'}
            </button>
            <button
              onClick={handleApprove}
              disabled={processing}
              className="flex items-center gap-1.5 px-4 py-2 bg-green-600 text-white rounded-xl hover:bg-green-700 disabled:opacity-50 transition-colors"
            >
              <CheckCircle className="w-4 h-4" />
              {processing ? '처리 중...' : '승인'}
            </button>
          </>
        )}
      </div>
    </DialogWrapper>
  );
}

// ============================================================================
// Shared UI Components
// ============================================================================

function DialogWrapper({ children, onClose, title, small, wide }: {
  children: React.ReactNode;
  onClose: () => void;
  title: string;
  small?: boolean;
  wide?: boolean;
}) {
  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center">
      <div className="absolute inset-0 bg-black/50 backdrop-blur-sm" onClick={onClose} />
      <div className={`relative bg-white dark:bg-gray-800 rounded-xl shadow-xl ${small ? 'max-w-md' : wide ? 'max-w-5xl' : 'max-w-2xl'} w-full mx-4`}>
        <div className="flex items-center justify-between px-5 py-3 border-b border-gray-200 dark:border-gray-700">
          <h3 className="text-base font-semibold text-gray-900 dark:text-white">{title}</h3>
          <button onClick={onClose} className="p-1 text-gray-400 hover:text-gray-600 rounded-lg transition-colors">
            <X className="w-4 h-4" />
          </button>
        </div>
        <div className="px-5 py-4">
          {children}
        </div>
      </div>
    </div>
  );
}

function InputField({ label, value, onChange, placeholder, type = 'text', required }: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  type?: string;
  required?: boolean;
}) {
  const fieldId = `apiclient-${label.replace(/\s+/g, '-').toLowerCase()}`;
  return (
    <div>
      <label htmlFor={fieldId} className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">{label}</label>
      <input
        id={fieldId}
        name={fieldId}
        type={type}
        value={value}
        onChange={e => onChange(e.target.value)}
        placeholder={placeholder}
        required={required}
        className="w-full px-3 py-2 bg-gray-50 dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 focus:border-transparent"
      />
    </div>
  );
}
