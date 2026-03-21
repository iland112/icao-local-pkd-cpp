import { useTranslation } from 'react-i18next';
import { useState, useEffect, useCallback } from 'react';
import { Key, Plus, RefreshCw, Trash2, Edit2, Shield, Clock, Activity, BarChart3, Inbox } from 'lucide-react';
import { apiClientApi, type ApiClient } from '@/api/apiClientApi';
import { apiClientRequestApi, type ApiClientRequestItem } from '@/api/apiClientRequestApi';
import { toast } from '@/stores/toastStore';
import { formatDate } from '@/utils/dateFormat';
import { ConfirmDialog } from '@/components/common';
import {
  CreateDialog,
  EditDialog,
  DeleteDialog,
  UsageDialog,
  ApiKeyDialog,
  RequestRow,
  RequestDetailDialog,
} from '@/components/apiClient/ApiClientDialogs';

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
      <CreateDialog
        open={showCreate}
        onClose={() => setShowCreate(false)}
        onSuccess={(client, key) => {
          setShowCreate(false);
          setShowKey({ client, key });
          fetchClients();
        }}
      />
      <EditDialog
        client={showEdit}
        onClose={() => setShowEdit(null)}
        onSuccess={() => { setShowEdit(null); fetchClients(); }}
      />
      <DeleteDialog
        client={showDelete}
        onClose={() => setShowDelete(null)}
        onSuccess={() => { setShowDelete(null); fetchClients(); }}
      />
      {showKey && (
        <ApiKeyDialog
          clientName={showKey.client.client_name}
          apiKey={showKey.key}
          onClose={() => setShowKey(null)}
        />
      )}
      <UsageDialog
        client={showUsage}
        onClose={() => setShowUsage(null)}
      />
      <RequestDetailDialog
        request={showRequestDetail}
        onClose={() => setShowRequestDetail(null)}
        onApproved={(apiKey) => {
          const reqName = showRequestDetail?.client_name || '';
          setShowRequestDetail(null);
          if (apiKey) setShowApproveKey({ clientName: reqName, apiKey });
          fetchRequests();
          fetchClients();
        }}
        onRejected={() => { setShowRequestDetail(null); fetchRequests(); }}
      />
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
    <div className={`bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 ${colors[color] || colors.blue}`}>
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
