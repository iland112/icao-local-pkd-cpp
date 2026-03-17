import { useTranslation } from 'react-i18next';
import { useState, useEffect, useCallback } from 'react';
import { Key, Plus, RefreshCw, Trash2, Edit2, Copy, Check, Shield, Clock, Activity, X, Eye, EyeOff, BarChart3, Inbox, CheckCircle, XCircle, User, Building2, Mail, Phone, FileText, Server, Monitor, Smartphone, HelpCircle, ChevronDown, AlertCircle } from 'lucide-react';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, Cell } from 'recharts';
import { apiClientApi, type ApiClient, type UsageStats, type CreateApiClientRequest, type UpdateApiClientRequest } from '@/api/apiClientApi';
import { apiClientRequestApi, type ApiClientRequestItem, type ApproveRequestPayload } from '@/api/apiClientRequestApi';
import { toast } from '@/stores/toastStore';
import { formatDate } from '@/utils/dateFormat';
import { ConfirmDialog } from '@/components/common';

const AVAILABLE_PERMISSIONS_KEYS = [
  { value: 'cert:read', labelKey: 'admin:apiClient.permissions.certRead', descKey: 'admin:apiClient.permDesc.certRead' },
  { value: 'cert:export', labelKey: 'admin:apiClient.permissions.certExport', descKey: 'admin:apiClient.permDesc.certExport' },
  { value: 'pa:verify', labelKey: 'admin:apiClient.permissions.paVerify', descKey: 'admin:apiClient.permDesc.paVerify' },
  { value: 'pa:read', labelKey: 'admin:apiClient.permissions.paRead', descKey: 'admin:apiClient.permDesc.paRead' },
  { value: 'upload:read', labelKey: 'admin:apiClient.permissions.uploadRead', descKey: 'admin:apiClient.permDesc.uploadRead' },
  { value: 'upload:write', labelKey: 'admin:apiClient.permissions.uploadWrite', descKey: 'admin:apiClient.permDesc.uploadWrite' },
  { value: 'report:read', labelKey: 'admin:apiClient.permissions.reportRead', descKey: 'admin:apiClient.permDesc.reportRead' },
  { value: 'ai:read', labelKey: 'admin:apiClient.permissions.aiRead', descKey: 'admin:apiClient.permDesc.aiRead' },
  { value: 'sync:read', labelKey: 'admin:apiClient.permissions.syncRead', descKey: 'admin:apiClient.permDesc.syncRead' },
  { value: 'icao:read', labelKey: 'admin:apiClient.permissions.icaoRead', descKey: 'admin:apiClient.permDesc.icaoRead' },
];

const DEVICE_TYPES_KEYS = [
  { value: 'SERVER' as const, labelKey: 'admin:apiClientRequest.deviceTypes.server', descKey: 'admin:apiClient.deviceDesc.server', icon: Server },
  { value: 'DESKTOP' as const, labelKey: 'admin:apiClientRequest.deviceTypes.desktop', descKey: 'admin:apiClient.deviceDesc.desktop', icon: Monitor },
  { value: 'MOBILE' as const, labelKey: 'admin:apiClientRequest.deviceTypes.mobile', descKey: 'admin:apiClient.deviceDesc.mobile', icon: Smartphone },
  { value: 'OTHER' as const, labelKey: 'admin:apiClient.deviceDesc.other', descKey: 'admin:apiClient.deviceDesc.otherDesc', icon: HelpCircle },
];

export default function ApiClientManagement() {
  const { t } = useTranslation(['admin', 'common']);
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
        toast.warning(t('admin:apiClient.rateLimited'), t('admin:apiClient.rateLimitedMsg'));
      } else {
        toast.error(t('admin:apiClient.apiKeyReissueFailed'), t('admin:apiClient.apiKeyReissueFailed'));
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
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">{t('admin:apiClient.title')}</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">{t('admin:apiClient.subtitle')}</p>
          </div>
        </div>
        {activeTab === 'clients' && (
          <button
            onClick={() => setShowCreate(true)}
            className="flex items-center gap-2 px-4 py-2.5 bg-blue-600 hover:bg-blue-700 text-white rounded-xl font-medium transition-colors"
          >
            <Plus className="w-4 h-4" />
            {t('admin:apiClient.registerClient')}
          </button>
        )}
      </div>

      {/* Stats Cards (always visible above tabs) */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <StatCard label={t('admin:apiClient.totalClients')} value={total} color="blue" />
        <StatCard label={t('common:status.active')} value={activeCount} color="green" />
        <StatCard label={t('common:status.inactive')} value={total - activeCount} color="gray" />
        <StatCard label={t('admin:apiClient.totalAccumulatedRequests')} value={todayRequests.toLocaleString()} color="purple" />
      </div>

      {/* Tabs */}
      <div className="flex gap-1 bg-gray-100 dark:bg-gray-800 rounded-xl p-1">
        {([
          { key: 'clients' as const, labelKey: 'common:label.registeredClients', icon: Shield, count: total },
          { key: 'requests' as const, labelKey: 'common:label.requestList', icon: Inbox, count: requestsTotal },
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
            {t(tab.labelKey)}
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
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white">{t('common:label.registeredClients')}</h2>
            </div>

            {loading ? (
              <div className="flex justify-center py-12">
                <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-blue-600" />
              </div>
            ) : clients.length === 0 ? (
              <div className="text-center py-12 text-gray-500 dark:text-gray-400">
                {t('admin:apiClient.noClients')}
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
              { value: '', labelKey: 'common:label.all' },
              { value: 'PENDING', labelKey: 'common:status.pending' },
              { value: 'APPROVED', labelKey: 'common:status.approved' },
              { value: 'REJECTED', labelKey: 'common:status.rejected' },
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
                {t(opt.labelKey)}
              </button>
            ))}
          </div>

          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden">
            <div className="px-6 py-4 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white">{t('common:label.requestList')}</h2>
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
                {t('admin:apiClient.noRequests')}
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
        title={t('admin:apiClient.apiKeyReissue')}
        message={t('admin:apiClient.regenerateConfirm')}
        confirmLabel={t('admin:apiClient.regenerateKey')}
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
  const { t } = useTranslation(['admin', 'common']);
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
              {client.is_active ? t('common:status.active') : t('common:status.inactive')}
            </span>
          </div>
          <div className="flex items-center gap-3 text-xs text-gray-500 dark:text-gray-400 mt-1">
            <span className="font-mono bg-gray-100 dark:bg-gray-700 px-1.5 py-0.5 rounded">{client.api_key_prefix}...</span>
            <span className="flex items-center gap-1"><Activity className="w-3 h-3" />{t('admin:apiClient.requestCount', { num: client.total_requests.toLocaleString() })}</span>
            {client.last_used_at && (
              <span className="flex items-center gap-1"><Clock className="w-3 h-3" />{t('admin:apiClient.recentPrefix')}: {formatDate(client.last_used_at)}</span>
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
        <button onClick={onUsage} title={t('admin:apiClient.usageHistory')} className="p-2 text-gray-400 hover:text-purple-600 hover:bg-purple-50 dark:hover:bg-purple-900/20 rounded-lg transition-colors">
          <BarChart3 className="w-4 h-4" />
        </button>
        <button onClick={onRegenerate} title={t('admin:apiClient.keyReissue')} className="p-2 text-gray-400 hover:text-amber-600 hover:bg-amber-50 dark:hover:bg-amber-900/20 rounded-lg transition-colors">
          <RefreshCw className="w-4 h-4" />
        </button>
        <button onClick={onEdit} title={t('common:button.edit')} className="p-2 text-gray-400 hover:text-blue-600 hover:bg-blue-50 dark:hover:bg-blue-900/20 rounded-lg transition-colors">
          <Edit2 className="w-4 h-4" />
        </button>
        <button onClick={onDelete} title={t('admin:apiClient.deleteClient')} className="p-2 text-gray-400 hover:text-red-600 hover:bg-red-50 dark:hover:bg-red-900/20 rounded-lg transition-colors">
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
  const { t } = useTranslation(['admin', 'common']);
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
        setError(t('admin:apiClient.apiKeyCreateFailed'));
      }
    } catch (e: unknown) {
      const axiosErr = e as { response?: { status?: number; data?: { message?: string } } };
      if (axiosErr.response?.status === 503 || axiosErr.response?.status === 429) {
        setError(t('admin:apiClient.rateLimitedMsg'));
      } else if (axiosErr.response?.data?.message) {
        setError(axiosErr.response.data.message);
      } else {
        setError(t('admin:apiClient.apiKeyCreateError'));
      }
    } finally {
      setSaving(false);
    }
  };

  return (
    <DialogWrapper onClose={onClose} title={t('admin:apiClient.registerClient')} wide>
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-5 max-h-[70vh] overflow-y-auto px-0.5">
        {/* Left Column: Client Info + Device + IPs + Rate Limits */}
        <div className="space-y-4">
          {/* Client Config */}
          <div>
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">{t('common:label.clientSettings')}</h4>
            <div className="space-y-3">
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                <InputField label={t('admin:apiClient.clientName_label')} value={form.client_name} onChange={v => setForm({ ...form, client_name: v })} placeholder={t('admin:apiClientRequest.clientNamePlaceholder')} required />
                <InputField label={t('common:label.description')} value={form.description || ''} onChange={v => setForm({ ...form, description: v })} placeholder={t('admin:apiClient.apiUsagePlaceholder')} />
              </div>
            </div>
          </div>

          {/* Device Type */}
          <div>
            <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">{t('common:label.deviceTypeLabel')}</span>
            <div className="grid grid-cols-4 gap-1.5">
              {DEVICE_TYPES_KEYS.map(dt => {
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
                    <span className={`text-xs font-medium ${selected ? 'text-blue-700 dark:text-blue-300' : 'text-gray-700 dark:text-gray-300'}`}>{t(dt.labelKey)}</span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Allowed IPs */}
          {showIpField && (
            <div>
              <InputField label={t('admin:apiClient.allowedIpsComma')} value={ipsText} onChange={setIpsText} placeholder="10.0.0.0/24, 192.168.1.100" />
              <p className="text-xs text-gray-400 dark:text-gray-500 mt-1">{t('common:label.emptyAllowAllIps')}</p>
            </div>
          )}
          {deviceType === 'MOBILE' && (
            <div className="flex items-start gap-2 p-2.5 bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-xl">
              <AlertCircle className="w-4 h-4 text-amber-500 flex-shrink-0 mt-0.5" />
              <p className="text-xs text-amber-700 dark:text-amber-300">{t('common:label.mobileIpDynamic')}</p>
            </div>
          )}

          {/* Rate Limits */}
          <div>
            <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">{t('admin:apiClient.rateLimit')}</span>
            <div className="grid grid-cols-3 gap-2">
              <InputField label={t('admin:apiClient.rateLimitPerMinute')} value={String(form.rate_limit_per_minute)} onChange={v => setForm({ ...form, rate_limit_per_minute: parseInt(v) || 60 })} type="number" />
              <InputField label={t('admin:apiClient.rateLimitPerHour')} value={String(form.rate_limit_per_hour)} onChange={v => setForm({ ...form, rate_limit_per_hour: parseInt(v) || 1000 })} type="number" />
              <InputField label={t('admin:apiClient.rateLimitPerDay')} value={String(form.rate_limit_per_day)} onChange={v => setForm({ ...form, rate_limit_per_day: parseInt(v) || 10000 })} type="number" />
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
              {t('admin:apiClient.requesterInfoOffline')}
            </button>
            {showRequester && (
              <div className="mt-3 space-y-3 pl-1">
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <InputField label={t('admin:apiClient.requesterName')} value={requesterName} onChange={setRequesterName} placeholder={t('admin:apiClient.namePlaceholder')} />
                  <InputField label={t('admin:apiClient.orgDept')} value={requesterOrg} onChange={setRequesterOrg} placeholder={t('admin:apiClientRequest.orgPlaceholder')} />
                </div>
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <InputField label={t('admin:apiClient.contactNumber')} value={requesterPhone} onChange={setRequesterPhone} placeholder="02-1234-5678" />
                  <InputField label={t('common:label.email')} value={requesterEmail} onChange={setRequesterEmail} placeholder="hong@example.go.kr" type="email" />
                </div>
                <div>
                  <label htmlFor="create-reason" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">{t('common:label.registrationReason')}</label>
                  <textarea
                    id="create-reason"
                    name="create-reason"
                    value={requestReason}
                    onChange={e => setRequestReason(e.target.value)}
                    rows={2}
                    placeholder={t('admin:apiClient.proxyRegistrationReasonPlaceholder')}
                    className="w-full px-3 py-2 bg-gray-50 dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                  />
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Right Column: Permissions */}
        <div>
          <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-3">{t('common:label.permissionSettings')}</h4>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
            {AVAILABLE_PERMISSIONS_KEYS.map(p => (
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
                  <span className="text-sm font-medium text-gray-900 dark:text-white">{t(p.labelKey)}</span>
                  <p className="text-xs text-gray-500 dark:text-gray-400">{t(p.descKey)}</p>
                </div>
              </label>
            ))}
          </div>
          <div className="mt-3 px-3 py-2 bg-blue-50 dark:bg-blue-900/10 border border-blue-100 dark:border-blue-900/30 rounded-lg">
            <p className="text-xs text-blue-600 dark:text-blue-400">{t('admin:apiClient.advancedSettingsNote')}</p>
          </div>
        </div>
      </div>

      {error && (
        <div className="mt-4 p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl">
          <p className="text-sm text-red-700 dark:text-red-300">{error}</p>
        </div>
      )}

      <div className="flex justify-end gap-3 mt-5 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">{t('common:button.cancel')}</button>
        <button onClick={handleSubmit} disabled={saving || !form.client_name.trim()} className="flex items-center gap-2 px-5 py-2 bg-[#02385e] text-white rounded-xl hover:bg-[#024b7a] disabled:opacity-50 transition-colors active:scale-[0.98]">
          <Key className="w-4 h-4" />
          {saving ? t('admin:apiClient.creating') : t('admin:apiClient.apiKeyIssue')}
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
  const { t } = useTranslation(['admin', 'common']);
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
      const msg = e?.response?.data?.message || e?.message || t('admin:apiClient.updateFailed');
      setEditError(msg);
      if (import.meta.env.DEV) console.error('Update failed', e);
    } finally {
      setSaving(false);
    }
  };

  return (
    <DialogWrapper onClose={onClose} title={t('admin:apiClient.editClient')}>
      {editError && (
        <div className="mb-4 p-3 bg-red-50 dark:bg-red-900/30 border border-red-200 dark:border-red-800 rounded-lg text-red-700 dark:text-red-300 text-sm">{editError}</div>
      )}
      <div className="space-y-4">
        <InputField label={t('admin:apiClient.clientName_label')} value={form.client_name || ''} onChange={v => setForm({ ...form, client_name: v })} />
        <InputField label={t('common:label.description')} value={form.description || ''} onChange={v => setForm({ ...form, description: v })} />

        <div>
          <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">{t('admin:userManagement.permissions')}</span>
          <div className="grid grid-cols-2 gap-2">
            {AVAILABLE_PERMISSIONS_KEYS.map(p => (
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
                <span className="text-gray-700 dark:text-gray-300">{t(p.labelKey)}</span>
              </label>
            ))}
          </div>
        </div>

        <InputField label={t('admin:apiClient.allowedIpsComma')} value={ipsText} onChange={setIpsText} placeholder={t('common:label.emptyAllowAllIps')} />

        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
          <InputField label={t('admin:apiClient.rateLimitPerMinuteLabel')} value={String(form.rate_limit_per_minute)} onChange={v => setForm({ ...form, rate_limit_per_minute: parseInt(v) || 60 })} type="number" />
          <InputField label={t('admin:apiClient.rateLimitPerHourLabel')} value={String(form.rate_limit_per_hour)} onChange={v => setForm({ ...form, rate_limit_per_hour: parseInt(v) || 1000 })} type="number" />
          <InputField label={t('admin:apiClient.rateLimitPerDayLabel')} value={String(form.rate_limit_per_day)} onChange={v => setForm({ ...form, rate_limit_per_day: parseInt(v) || 10000 })} type="number" />
        </div>

        <label htmlFor="edit-is-active" className="flex items-center gap-2 cursor-pointer">
          <input id="edit-is-active" name="isActive" type="checkbox" checked={form.is_active} onChange={e => setForm({ ...form, is_active: e.target.checked })} className="rounded border-gray-300" />
          <span className="text-sm text-gray-700 dark:text-gray-300">{t('common:label.activeStatus')}</span>
        </label>
      </div>

      <div className="flex justify-end gap-3 mt-6 pt-4 border-t border-gray-200 dark:border-gray-700">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">{t('common:button.cancel')}</button>
        <button onClick={handleSubmit} disabled={saving} className="px-4 py-2 bg-blue-600 text-white rounded-xl hover:bg-blue-700 disabled:opacity-50 transition-colors">
          {saving ? t('common:label.saving') : t('common:button.save')}
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
  const { t } = useTranslation(['admin', 'common']);
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState('');

  return (
    <DialogWrapper onClose={onClose} title={t('admin:apiClient.deleteClient')} small>
      {deleteError && (
        <div className="mb-3 p-3 bg-red-50 dark:bg-red-900/30 border border-red-200 dark:border-red-800 rounded-lg text-red-700 dark:text-red-300 text-sm">{deleteError}</div>
      )}
      <div className="text-center py-4">
        <div className="w-14 h-14 mx-auto bg-red-100 dark:bg-red-900/30 rounded-full flex items-center justify-center mb-4">
          <Trash2 className="w-7 h-7 text-red-600" />
        </div>
        <p className="text-gray-700 dark:text-gray-300">
          <span className="font-semibold">{client.client_name}</span>{t('admin:apiClient.deactivateConfirmMsg')}</p>
        <p className="text-sm text-gray-500 dark:text-gray-400 mt-2">{t('admin:apiClient.accessBlocked')}</p>
      </div>
      <div className="flex justify-center gap-3 mt-4">
        <button onClick={onClose} className="px-4 py-2 text-gray-700 dark:text-gray-300 bg-gray-100 dark:bg-gray-700 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">{t('common:button.cancel')}</button>
        <button
          onClick={async () => {
            setDeleting(true);
            setDeleteError('');
            try { await apiClientApi.deactivate(client.id); onDeleted(); }
            catch (e: any) {
              const msg = e?.response?.data?.message || e?.message || t('admin:apiClient.deleteFailed');
              setDeleteError(msg);
              if (import.meta.env.DEV) console.error('Delete failed', e);
            }
            finally { setDeleting(false); }
          }}
          disabled={deleting}
          className="px-4 py-2 bg-red-600 text-white rounded-xl hover:bg-red-700 disabled:opacity-50 transition-colors"
        >
          {deleting ? t('common:status.processing') : t('admin:apiClient.deleteClient')}
        </button>
      </div>
    </DialogWrapper>
  );
}

const BAR_COLORS = ['#6366f1', '#8b5cf6', '#a78bfa', '#c4b5fd', '#ddd6fe', '#ede9fe', '#818cf8', '#7c3aed', '#6d28d9', '#5b21b6'];

function UsageDialog({ client, onClose }: {
  client: ApiClient;
  onClose: () => void;
}) {
  const { t } = useTranslation(['admin', 'common']);
  const [days, setDays] = useState(7);
  const [usage, setUsage] = useState<UsageStats | null>(null);
  const [loading, setLoading] = useState(true);

  const PERIOD_OPTIONS = [
    { days: 7, labelKey: 'admin:apiClient.days7' },
    { days: 30, labelKey: 'admin:apiClient.days30' },
    { days: 90, labelKey: 'admin:apiClient.days90' },
  ];

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
    <DialogWrapper onClose={onClose} title={t('admin:apiClient.usageDialogTitle', { name: client.client_name })}>
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
            {t(opt.labelKey)}
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
          <p className="text-gray-500 dark:text-gray-400">{t('admin:apiClient.noUsageHistory', { days })}</p>
        </div>
      ) : (
        <div className="space-y-3">
          {/* Summary cards */}
          <div className="grid grid-cols-2 gap-2">
            <div className="bg-indigo-50 dark:bg-indigo-900/20 rounded-lg p-3">
              <p className="text-xs text-indigo-600 dark:text-indigo-400 font-medium">{t('admin:apiClient.usageDialog.totalRequests')}</p>
              <p className="text-xl font-bold text-indigo-700 dark:text-indigo-300">{totalRequests.toLocaleString()}</p>
            </div>
            <div className="bg-purple-50 dark:bg-purple-900/20 rounded-lg p-3">
              <p className="text-xs text-purple-600 dark:text-purple-400 font-medium">{t('common:label.usageEndpoints')}</p>
              <p className="text-xl font-bold text-purple-700 dark:text-purple-300">{endpoints.length}</p>
            </div>
          </div>

          {/* Horizontal bar chart */}
          <div>
            <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">{t('common:label.endpointRequests')}</h4>
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
                    formatter={(value) => [`${Number(value).toLocaleString()} ${t('admin:apiClient.usageDialog.requests')}`, '']}
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
            <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">{t('common:label.detailedHistory')}</h4>
            <div className="border border-gray-200 dark:border-gray-700 rounded-lg overflow-hidden">
              <table className="w-full text-xs">
                <thead className="bg-slate-100 dark:bg-gray-700">
                  <tr>
                    <th className="text-center px-3 py-1.5 font-medium text-gray-600 dark:text-gray-300">{t('admin:apiClient.usageDialog.endpoint')}</th>
                    <th className="text-center px-3 py-1.5 font-medium text-gray-600 dark:text-gray-300 w-20">{t('admin:apiClient.usageDialog.requests')}</th>
                    <th className="text-center px-3 py-1.5 font-medium text-gray-600 dark:text-gray-300 w-16">{t('admin:apiClient.usageDialog.percentage')}</th>
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
        <button onClick={onClose} className="px-4 py-2 bg-gray-100 dark:bg-gray-700 text-gray-700 dark:text-gray-300 rounded-xl hover:bg-gray-200 dark:hover:bg-gray-600 transition-colors">{t('common:button.close')}</button>
      </div>
    </DialogWrapper>
  );
}

function ApiKeyDialog({ clientName, apiKey, onClose }: {
  clientName: string;
  apiKey: string;
  onClose: () => void;
}) {
  const { t } = useTranslation(['admin', 'common']);
  const [copied, setCopied] = useState(false);
  const [visible, setVisible] = useState(false);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(apiKey);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <DialogWrapper onClose={onClose} title={t('admin:apiClient.apiKeyIssueComplete')}>
      <div className="space-y-4">
        <div className="bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-xl p-4">
          <p className="text-sm text-amber-800 dark:text-amber-300 font-medium">
            {t('admin:apiClient.copyKeyWarning')}
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
        <button onClick={onClose} className="px-4 py-2 bg-blue-600 text-white rounded-xl hover:bg-blue-700 transition-colors">{t('common:button.confirm')}</button>
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

const STATUS_LABEL_KEYS: Record<string, string> = {
  PENDING: 'common:status.pending',
  APPROVED: 'common:status.approved',
  REJECTED: 'common:status.rejected',
};

const DEVICE_TYPE_INFO_KEYS: Record<string, { labelKey: string; icon: typeof Server }> = {
  SERVER: { labelKey: 'admin:apiClientRequest.deviceTypes.server', icon: Server },
  DESKTOP: { labelKey: 'admin:apiClientRequest.deviceTypes.desktop', icon: Monitor },
  MOBILE: { labelKey: 'admin:apiClientRequest.deviceTypes.mobile', icon: Smartphone },
  OTHER: { labelKey: 'common:label.other', icon: HelpCircle },
};

function RequestRow({ request, onClick }: { request: ApiClientRequestItem; onClick: () => void }) {
  const { t } = useTranslation(['admin', 'common']);
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
              {t(STATUS_LABEL_KEYS[request.status] || 'common:status.unknown')}
            </span>
          </div>
          <div className="flex items-center gap-3 text-xs text-gray-500 dark:text-gray-400 mt-1">
            <span className="flex items-center gap-1"><User className="w-3 h-3" />{request.requester_name}</span>
            <span className="flex items-center gap-1"><Building2 className="w-3 h-3" />{request.requester_org}</span>
            {(() => { const dt = DEVICE_TYPE_INFO_KEYS[request.device_type]; if (!dt) return null; const Icon = dt.icon; return <span className="flex items-center gap-1"><Icon className="w-3 h-3" />{t(dt.labelKey)}</span>; })()}
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
  const { t } = useTranslation(['admin', 'common']);
  const [reviewComment, setReviewComment] = useState('');
  const [processing, setProcessing] = useState(false);
  const [error, setError] = useState('');
  const [commentError, setCommentError] = useState(false);

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
        toast.success(t('admin:apiClient.approveComplete'), t('admin:apiClient.approveClientCreated', { name: request.client_name }));
        onApproved(res.client?.api_key);
      } else {
        setError(res.message || t('admin:apiClient.approveFailed'));
      }
    } catch (e: any) {
      setError(e?.response?.data?.message || t('admin:apiClient.approveError'));
    } finally {
      setProcessing(false);
    }
  };

  const handleReject = async () => {
    if (!reviewComment.trim()) {
      setError(t('admin:apiClient.rejectReasonRequired'));
      setCommentError(true);
      return;
    }
    setCommentError(false);
    setProcessing(true);
    setError('');
    try {
      const res = await apiClientRequestApi.reject(request.id, reviewComment);
      if (res.success) {
        toast.info(t('admin:apiClient.rejectProcessed'), t('admin:apiClient.rejectProcessedMsg', { name: request.client_name }));
        onRejected();
      } else {
        setError(res.message || t('admin:apiClient.rejectFailed'));
      }
    } catch (e: any) {
      setError(e?.response?.data?.message || t('admin:apiClient.rejectError'));
    } finally {
      setProcessing(false);
    }
  };

  const isPending = request.status === 'PENDING';
  const dtInfo = DEVICE_TYPE_INFO_KEYS[request.device_type];
  const DeviceIcon = dtInfo?.icon;

  return (
    <DialogWrapper onClose={onClose} title={t('admin:apiClient.requestDetailTitle')}>
      <div className="space-y-4 max-h-[70vh] overflow-y-auto">
        {/* Status badge */}
        <div className="flex items-center gap-2">
          <span className={`text-sm px-3 py-1 rounded-full font-medium ${STATUS_STYLES[request.status] || ''}`}>
            {t(STATUS_LABEL_KEYS[request.status] || 'common:status.unknown')}
          </span>
          <span className="text-xs text-gray-500 dark:text-gray-400">{t('common:label.requestDate')}: {formatDate(request.created_at)}</span>
        </div>

        {/* Requester Info */}
        <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4 space-y-2">
          <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">{t('admin:apiClientRequest.requesterInfo')}</h4>
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
          <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">{t('common:label.requestedClientSettings')}</h4>
          <div className="text-sm space-y-1.5">
            <div><span className="text-gray-500 dark:text-gray-400">{t('common:label.nameColon')}</span> <span className="font-medium text-gray-900 dark:text-white">{request.client_name}</span></div>
            {request.description && <div><span className="text-gray-500 dark:text-gray-400">{t('common:label.descriptionColon')}</span> <span className="text-gray-700 dark:text-gray-300">{request.description}</span></div>}
            <div className="flex items-center gap-1.5">
              <span className="text-gray-500 dark:text-gray-400">{t('common:label.deviceTypeLabelColon')}</span>
              {DeviceIcon && <DeviceIcon className="w-3.5 h-3.5 text-gray-500" />}
              <span className="text-gray-700 dark:text-gray-300">{dtInfo ? t(dtInfo.labelKey) : request.device_type}</span>
            </div>
            <div className="flex items-center gap-1 flex-wrap">
              <span className="text-gray-500 dark:text-gray-400">{t('common:label.permissionsColon')}</span>
              {request.permissions.map(p => (
                <span key={p} className="text-xs px-1.5 py-0.5 bg-blue-50 text-blue-600 dark:bg-blue-900/20 dark:text-blue-400 rounded">{p}</span>
              ))}
            </div>
            {request.allowed_ips.length > 0 && (
              <div><span className="text-gray-500 dark:text-gray-400">{t('common:label.suggestedIpColon')}</span> <span className="font-mono text-xs text-gray-700 dark:text-gray-300">{request.allowed_ips.join(', ')}</span></div>
            )}
          </div>
        </div>

        {/* Review section (for non-pending, show existing review) */}
        {!isPending && request.reviewed_by && (
          <div className="bg-gray-50 dark:bg-gray-700/50 rounded-xl p-4">
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white mb-2">{t('admin:apiClient.reviewResult')}</h4>
            <div className="text-sm space-y-1">
              {request.reviewed_at && <div><span className="text-gray-500 dark:text-gray-400">{t('common:label.reviewDateColon')}</span> <span className="text-gray-700 dark:text-gray-300">{formatDate(request.reviewed_at)}</span></div>}
              {request.review_comment && <div><span className="text-gray-500 dark:text-gray-400">{t('common:label.commentColon')}</span> <span className="text-gray-700 dark:text-gray-300">{request.review_comment}</span></div>}
            </div>
          </div>
        )}

        {/* Admin approval settings (only for PENDING) */}
        {isPending && (
          <div className="bg-blue-50 dark:bg-blue-900/10 rounded-xl p-4 space-y-3">
            <h4 className="text-sm font-semibold text-gray-900 dark:text-white">{t('admin:apiClient.approvalSettingsAdmin')}</h4>
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
              <div>
                <label htmlFor="approve-rpm" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">{t('admin:apiClient.rateLimitPerMinuteLabel')}</label>
                <input id="approve-rpm" type="number" value={rateLimitPerMinute} onChange={e => setRateLimitPerMinute(parseInt(e.target.value) || 60)}
                  className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
              </div>
              <div>
                <label htmlFor="approve-rph" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">{t('admin:apiClient.rateLimitPerHourLabel')}</label>
                <input id="approve-rph" type="number" value={rateLimitPerHour} onChange={e => setRateLimitPerHour(parseInt(e.target.value) || 1000)}
                  className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
              </div>
              <div>
                <label htmlFor="approve-rpd" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">{t('admin:apiClient.rateLimitPerDayLabel')}</label>
                <input id="approve-rpd" type="number" value={rateLimitPerDay} onChange={e => setRateLimitPerDay(parseInt(e.target.value) || 10000)}
                  className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
              </div>
            </div>
            <div>
              <label htmlFor="approve-days" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">{t('admin:apiClient.usagePeriodDays')}</label>
              <input id="approve-days" type="number" value={requestedDays} onChange={e => setRequestedDays(parseInt(e.target.value) || 365)}
                className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
            </div>
            <div>
              <label htmlFor="approve-endpoints" className="block text-xs text-gray-500 dark:text-gray-400 mb-1">{t('admin:apiClient.allowedEndpointsLabel')}</label>
              <input id="approve-endpoints" type="text" value={allowedEndpointsText} onChange={e => setAllowedEndpointsText(e.target.value)}
                placeholder="/api/pa/*, /api/certificates/*"
                className="w-full px-2 py-1.5 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg text-sm text-gray-900 dark:text-white" />
            </div>
          </div>
        )}

        {/* Admin review comment */}
        {isPending && (
          <div>
            <label htmlFor="review-comment" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">{t('admin:pendingDsc.reviewComment')}</label>
            <textarea
              id="review-comment"
              name="review-comment"
              value={reviewComment}
              onChange={e => { setReviewComment(e.target.value); if (commentError) setCommentError(false); }}
              rows={2}
              placeholder={t('admin:apiClient.reviewCommentPlaceholder')}
              className={`w-full px-3 py-2 bg-gray-50 dark:bg-gray-700 border rounded-lg text-sm text-gray-900 dark:text-white focus:ring-2 focus:ring-blue-500 focus:border-transparent ${commentError ? 'border-red-500 dark:border-red-400' : 'border-gray-200 dark:border-gray-600'}`}
            />
            {commentError && (
              <p className="mt-1 text-xs text-red-600 dark:text-red-400">{t('admin:apiClient.rejectReasonRequired')}</p>
            )}
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
          {t('common:button.close')}
        </button>
        {isPending && (
          <>
            <button
              onClick={handleReject}
              disabled={processing}
              className="flex items-center gap-1.5 px-4 py-2 bg-red-600 text-white rounded-xl hover:bg-red-700 disabled:opacity-50 transition-colors"
            >
              <XCircle className="w-4 h-4" />
              {processing ? t('common:status.processing') : t('common:button.reject')}
            </button>
            <button
              onClick={handleApprove}
              disabled={processing}
              className="flex items-center gap-1.5 px-4 py-2 bg-green-600 text-white rounded-xl hover:bg-green-700 disabled:opacity-50 transition-colors"
            >
              <CheckCircle className="w-4 h-4" />
              {processing ? t('common:status.processing') : t('common:button.approve')}
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
