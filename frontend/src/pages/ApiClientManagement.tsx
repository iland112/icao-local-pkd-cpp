import { useState, useEffect, useCallback } from 'react';
import { Key, Plus, RefreshCw, Trash2, Edit2, Copy, Check, Shield, Clock, Activity, X, Eye, EyeOff, BarChart3 } from 'lucide-react';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import { apiClientApi, type ApiClient, type UsageStats, type CreateApiClientRequest, type UpdateApiClientRequest } from '@/api/apiClientApi';

const AVAILABLE_PERMISSIONS = [
  { value: 'cert:read', label: '인증서 검색' },
  { value: 'cert:export', label: '인증서 내보내기' },
  { value: 'pa:verify', label: 'PA 검증' },
  { value: 'pa:read', label: 'PA 이력 조회' },
  { value: 'upload:read', label: '업로드 조회' },
  { value: 'upload:write', label: '파일 업로드' },
  { value: 'report:read', label: '보고서 조회' },
  { value: 'ai:read', label: 'AI 분석 조회' },
  { value: 'sync:read', label: 'Sync 조회' },
  { value: 'icao:read', label: 'ICAO 조회' },
];

export default function ApiClientManagement() {
  const [clients, setClients] = useState<ApiClient[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [showCreate, setShowCreate] = useState(false);
  const [showEdit, setShowEdit] = useState<ApiClient | null>(null);
  const [showDelete, setShowDelete] = useState<ApiClient | null>(null);
  const [showKey, setShowKey] = useState<{ client: ApiClient; key: string } | null>(null);
  const [showUsage, setShowUsage] = useState<ApiClient | null>(null);

  const fetchClients = useCallback(async () => {
    setLoading(true);
    try {
      const res = await apiClientApi.getAll();
      setClients(res.clients || []);
      setTotal(res.total || 0);
    } catch (e) {
      console.error('Failed to fetch clients', e);
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
        <button
          onClick={() => setShowCreate(true)}
          className="flex items-center gap-2 px-4 py-2.5 bg-blue-600 hover:bg-blue-700 text-white rounded-xl font-medium transition-colors"
        >
          <Plus className="w-4 h-4" />
          클라이언트 등록
        </button>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <StatCard label="전체 클라이언트" value={total} color="blue" />
        <StatCard label="활성" value={activeCount} color="green" />
        <StatCard label="비활성" value={total - activeCount} color="gray" />
        <StatCard label="총 누적 요청" value={todayRequests.toLocaleString()} color="purple" />
      </div>

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
                onRegenerate={async () => {
                  if (!confirm('API Key를 재발급하시겠습니까? 기존 키는 즉시 무효화됩니다.')) return;
                  try {
                    const res = await apiClientApi.regenerate(client.id);
                    if (res.client.api_key) {
                      setShowKey({ client: res.client, key: res.client.api_key });
                    }
                    fetchClients();
                  } catch (e) {
                    console.error('Regenerate failed', e);
                  }
                }}
              />
            ))}
          </div>
        )}
      </div>

      {/* Dialogs */}
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
              <span className="flex items-center gap-1"><Clock className="w-3 h-3" />최근: {new Date(client.last_used_at).toLocaleDateString('ko-KR')}</span>
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
  const [saving, setSaving] = useState(false);

  const handleSubmit = async () => {
    if (!form.client_name.trim()) return;
    setSaving(true);
    try {
      const req = { ...form, allowed_ips: ipsText ? ipsText.split(',').map(s => s.trim()).filter(Boolean) : [] };
      const res = await apiClientApi.create(req);
      if (res.success && res.client.api_key) {
        onCreated(res.client, res.client.api_key);
      }
    } catch (e) {
      console.error('Create failed', e);
    } finally {
      setSaving(false);
    }
  };

  return (
    <DialogWrapper onClose={onClose} title="API 클라이언트 등록">
      <div className="space-y-4">
        <InputField label="클라이언트 이름" value={form.client_name} onChange={v => setForm({ ...form, client_name: v })} placeholder="출입국관리시스템" required />
        <InputField label="설명" value={form.description || ''} onChange={v => setForm({ ...form, description: v })} placeholder="용도 설명" />

        <div>
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">권한</label>
          <div className="grid grid-cols-2 gap-2">
            {AVAILABLE_PERMISSIONS.map(p => (
              <label key={p.value} className="flex items-center gap-2 text-sm cursor-pointer">
                <input
                  type="checkbox"
                  checked={form.permissions.includes(p.value)}
                  onChange={e => {
                    const perms = e.target.checked
                      ? [...form.permissions, p.value]
                      : form.permissions.filter(x => x !== p.value);
                    setForm({ ...form, permissions: perms });
                  }}
                  className="rounded border-gray-300"
                />
                <span className="text-gray-700 dark:text-gray-300">{p.label}</span>
              </label>
            ))}
          </div>
        </div>

        <InputField label="허용 IP (콤마 구분)" value={ipsText} onChange={setIpsText} placeholder="192.168.1.100, 10.0.0.0/24" />

        <div className="grid grid-cols-3 gap-3">
          <InputField label="분당 제한" value={String(form.rate_limit_per_minute)} onChange={v => setForm({ ...form, rate_limit_per_minute: parseInt(v) || 60 })} type="number" />
          <InputField label="시간당 제한" value={String(form.rate_limit_per_hour)} onChange={v => setForm({ ...form, rate_limit_per_hour: parseInt(v) || 1000 })} type="number" />
          <InputField label="일당 제한" value={String(form.rate_limit_per_day)} onChange={v => setForm({ ...form, rate_limit_per_day: parseInt(v) || 10000 })} type="number" />
        </div>
      </div>

      <div className="flex justify-end gap-3 mt-6 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">취소</button>
        <button onClick={handleSubmit} disabled={saving || !form.client_name.trim()} className="px-4 py-2 bg-blue-600 text-white rounded-xl hover:bg-blue-700 disabled:opacity-50 transition-colors">
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

  const handleSubmit = async () => {
    setSaving(true);
    try {
      const req = { ...form, allowed_ips: ipsText ? ipsText.split(',').map(s => s.trim()).filter(Boolean) : [] };
      await apiClientApi.update(client.id, req);
      onUpdated();
    } catch (e) {
      console.error('Update failed', e);
    } finally {
      setSaving(false);
    }
  };

  return (
    <DialogWrapper onClose={onClose} title="클라이언트 수정">
      <div className="space-y-4">
        <InputField label="클라이언트 이름" value={form.client_name || ''} onChange={v => setForm({ ...form, client_name: v })} />
        <InputField label="설명" value={form.description || ''} onChange={v => setForm({ ...form, description: v })} />

        <div>
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">권한</label>
          <div className="grid grid-cols-2 gap-2">
            {AVAILABLE_PERMISSIONS.map(p => (
              <label key={p.value} className="flex items-center gap-2 text-sm cursor-pointer">
                <input
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

        <div className="grid grid-cols-3 gap-3">
          <InputField label="분당 제한" value={String(form.rate_limit_per_minute)} onChange={v => setForm({ ...form, rate_limit_per_minute: parseInt(v) || 60 })} type="number" />
          <InputField label="시간당 제한" value={String(form.rate_limit_per_hour)} onChange={v => setForm({ ...form, rate_limit_per_hour: parseInt(v) || 1000 })} type="number" />
          <InputField label="일당 제한" value={String(form.rate_limit_per_day)} onChange={v => setForm({ ...form, rate_limit_per_day: parseInt(v) || 10000 })} type="number" />
        </div>

        <label className="flex items-center gap-2 cursor-pointer">
          <input type="checkbox" checked={form.is_active} onChange={e => setForm({ ...form, is_active: e.target.checked })} className="rounded border-gray-300" />
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

  return (
    <DialogWrapper onClose={onClose} title="클라이언트 비활성화" small>
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
            try { await apiClientApi.deactivate(client.id); onDeleted(); }
            catch (e) { console.error('Delete failed', e); }
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
      <div className="flex gap-2 mb-4">
        {PERIOD_OPTIONS.map(opt => (
          <button
            key={opt.days}
            onClick={() => setDays(opt.days)}
            className={`px-3 py-1.5 text-sm rounded-lg font-medium transition-colors ${
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
        <div className="space-y-5">
          {/* Summary cards */}
          <div className="grid grid-cols-2 gap-3">
            <div className="bg-indigo-50 dark:bg-indigo-900/20 rounded-xl p-4">
              <p className="text-xs text-indigo-600 dark:text-indigo-400 font-medium">총 요청</p>
              <p className="text-2xl font-bold text-indigo-700 dark:text-indigo-300 mt-1">{totalRequests.toLocaleString()}</p>
            </div>
            <div className="bg-purple-50 dark:bg-purple-900/20 rounded-xl p-4">
              <p className="text-xs text-purple-600 dark:text-purple-400 font-medium">사용 엔드포인트</p>
              <p className="text-2xl font-bold text-purple-700 dark:text-purple-300 mt-1">{endpoints.length}</p>
            </div>
          </div>

          {/* Horizontal bar chart */}
          <div>
            <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">엔드포인트별 요청 수</h4>
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-3">
              <ResponsiveContainer width="100%" height={Math.max(endpoints.length * 36, 120)}>
                <BarChart data={endpoints} layout="vertical" margin={{ left: 0, right: 40, top: 4, bottom: 4 }}>
                  <XAxis type="number" hide />
                  <YAxis
                    type="category"
                    dataKey="endpoint"
                    width={180}
                    tick={{ fontSize: 11, fill: '#6b7280' }}
                    tickFormatter={(v: string) => v.length > 28 ? '...' + v.slice(-25) : v}
                  />
                  <Tooltip
                    formatter={(value) => [`${Number(value).toLocaleString()} 요청`, '']}
                    labelFormatter={(label) => String(label)}
                    contentStyle={{ borderRadius: '8px', border: 'none', boxShadow: '0 4px 12px rgba(0,0,0,0.1)' }}
                  />
                  <Bar dataKey="count" radius={[0, 4, 4, 0]} barSize={20}>
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
            <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">상세 내역</h4>
            <div className="border border-gray-200 dark:border-gray-700 rounded-xl overflow-hidden">
              <table className="w-full text-sm">
                <thead className="bg-gray-50 dark:bg-gray-700/80">
                  <tr>
                    <th className="text-left px-4 py-2 font-medium text-gray-600 dark:text-gray-300">엔드포인트</th>
                    <th className="text-right px-4 py-2 font-medium text-gray-600 dark:text-gray-300 w-24">요청 수</th>
                    <th className="text-right px-4 py-2 font-medium text-gray-600 dark:text-gray-300 w-20">비율</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-100 dark:divide-gray-700">
                  {endpoints.map((ep, i) => (
                    <tr key={ep.endpoint} className="hover:bg-gray-50 dark:hover:bg-gray-700/30">
                      <td className="px-4 py-2 font-mono text-xs text-gray-700 dark:text-gray-300">
                        {i < 3 && <span className="inline-block w-5 h-5 text-center text-xs font-bold text-white rounded-full mr-2" style={{ backgroundColor: BAR_COLORS[i] }}>{i + 1}</span>}
                        {ep.endpoint}
                      </td>
                      <td className="text-right px-4 py-2 font-semibold text-gray-900 dark:text-white">{ep.count.toLocaleString()}</td>
                      <td className="text-right px-4 py-2 text-gray-500 dark:text-gray-400">{(ep.count / totalRequests * 100).toFixed(1)}%</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      <div className="flex justify-end mt-5 pt-4 border-t border-gray-200 dark:border-gray-700">
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
// Shared UI Components
// ============================================================================

function DialogWrapper({ children, onClose, title, small }: {
  children: React.ReactNode;
  onClose: () => void;
  title: string;
  small?: boolean;
}) {
  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center">
      <div className="absolute inset-0 bg-black/50 backdrop-blur-sm" onClick={onClose} />
      <div className={`relative bg-white dark:bg-gray-800 rounded-2xl shadow-xl ${small ? 'max-w-md' : 'max-w-2xl'} w-full mx-4 p-6`}>
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">{title}</h3>
          <button onClick={onClose} className="p-1.5 text-gray-400 hover:text-gray-600 rounded-lg transition-colors">
            <X className="w-5 h-5" />
          </button>
        </div>
        {children}
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
  return (
    <div>
      <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">{label}</label>
      <input
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
