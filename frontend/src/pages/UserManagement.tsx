import { useTranslation } from 'react-i18next';
import { useState, useEffect } from 'react';
import { Users, Plus, Edit, Trash2, Key, Shield, Search, X, Check, AlertCircle, Mail, Clock, User, FileText, Activity } from 'lucide-react';
import { useNavigate } from 'react-router-dom';
import { createAuthenticatedClient } from '@/services/authApi';
import { authApi } from '@/services/api';
import { PERMISSION_GROUPS, AVAILABLE_PERMISSIONS } from '@/utils/permissions';
import { formatDateTime } from '@/utils/dateFormat';

interface UserData {
  id: string;
  username: string;
  email: string;
  full_name: string;
  is_admin: boolean;
  is_active: boolean;
  permissions: string[];
  created_at: string;
  last_login_at?: string;
}

interface UserFormData {
  username: string;
  password: string;
  email: string;
  full_name: string;
  is_admin: boolean;
  permissions: string[];
}

const authClient = createAuthenticatedClient('/api/auth');

export function UserManagement() {
  const { t } = useTranslation(['admin', 'common']);
  const navigate = useNavigate();
  const [users, setUsers] = useState<UserData[]>([]);
  const [loading, setLoading] = useState(true);
  const [searchQuery, setSearchQuery] = useState('');

  // Modal states
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [showPasswordModal, setShowPasswordModal] = useState(false);

  const [selectedUser, setSelectedUser] = useState<UserData | null>(null);
  const [formData, setFormData] = useState<UserFormData>({
    username: '',
    password: '',
    email: '',
    full_name: '',
    is_admin: false,
    permissions: [],
  });

  const [passwordData, setPasswordData] = useState({
    currentPassword: '',
    newPassword: '',
    confirmPassword: '',
  });

  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  useEffect(() => {
    fetchUsers();
  }, []);

  // Ensure permissions is always an array (Oracle may return JSON string or null)
  const parsePermissions = (perms: unknown): string[] => {
    if (Array.isArray(perms)) return perms;
    if (typeof perms === 'string') {
      try { const parsed = JSON.parse(perms); return Array.isArray(parsed) ? parsed : []; } catch { return []; }
    }
    return [];
  };

  const fetchUsers = async () => {
    try {
      setLoading(true);
      setError('');
      const { data } = await authClient.get('/users');
      const rawUsers = data.data || data.users || [];
      setUsers(rawUsers.map((u: UserData) => ({ ...u, permissions: parsePermissions(u.permissions) })));
    } catch (error) {
      if (import.meta.env.DEV) console.error('Error fetching users:', error);
      setError(t('admin:userMgmt.fetchFailed'));
    } finally {
      setLoading(false);
    }
  };

  const handleCreateUser = async () => {
    try {
      setError('');
      await authClient.post('/users', formData);
      setSuccess(t('admin:userMgmt.createSuccess'));
      setShowCreateModal(false);
      fetchUsers();
      resetForm();
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || t('admin:userMgmt.createFailed'));
    }
  };

  const handleUpdateUser = async () => {
    if (!selectedUser) return;

    try {
      setError('');
      const { password, ...updateData } = formData;
      await authClient.put(`/users/${selectedUser.id}`, updateData);
      setSuccess(t('admin:userMgmt.updateSuccess'));
      setShowEditModal(false);
      fetchUsers();
      resetForm();
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || t('admin:userMgmt.updateFailed'));
    }
  };

  const handleDeleteUser = async () => {
    if (!selectedUser) return;

    try {
      setError('');
      await authClient.delete(`/users/${selectedUser.id}`);
      setSuccess(t('admin:userMgmt.deleteSuccess'));
      setShowDeleteModal(false);
      fetchUsers();
      setSelectedUser(null);
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || t('admin:userMgmt.deleteFailed'));
    }
  };

  const handleChangePassword = async () => {
    if (!selectedUser) return;

    const currentUser = authApi.getStoredUser();
    const isSelf = currentUser?.id === selectedUser.id;

    if (isSelf && !passwordData.currentPassword) {
      setError(t('admin:userMgmt.currentPasswordRequired'));
      return;
    }

    if (passwordData.newPassword !== passwordData.confirmPassword) {
      setError(t('common:validation.passwordMismatch'));
      return;
    }

    try {
      setError('');
      const payload: Record<string, string> = { new_password: passwordData.newPassword };
      if (isSelf) {
        payload.current_password = passwordData.currentPassword;
      }
      await authClient.put(`/users/${selectedUser.id}/password`, payload);
      setSuccess(t('admin:userMgmt.passwordChanged'));
      setShowPasswordModal(false);
      setPasswordData({ currentPassword: '', newPassword: '', confirmPassword: '' });
      setSelectedUser(null);
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || t('admin:userMgmt.passwordChangeFailed'));
    }
  };

  const resetForm = () => {
    setFormData({
      username: '',
      password: '',
      email: '',
      full_name: '',
      is_admin: false,
      permissions: [],
    });
    setSelectedUser(null);
  };

  const openEditModal = (user: UserData) => {
    setSelectedUser(user);
    setFormData({
      username: user.username,
      password: '',
      email: user.email,
      full_name: user.full_name,
      is_admin: user.is_admin,
      permissions: parsePermissions(user.permissions),
    });
    setShowEditModal(true);
  };

  const openDeleteModal = (user: UserData) => {
    setSelectedUser(user);
    setShowDeleteModal(true);
  };

  const openPasswordModal = (user: UserData) => {
    setSelectedUser(user);
    setShowPasswordModal(true);
  };

  const togglePermission = (permission: string) => {
    setFormData(prev => ({
      ...prev,
      permissions: prev.permissions.includes(permission)
        ? prev.permissions.filter(p => p !== permission)
        : [...prev.permissions, permission],
    }));
  };

  const filteredUsers = users.filter(user =>
    user.username.toLowerCase().includes(searchQuery.toLowerCase()) ||
    user.email?.toLowerCase().includes(searchQuery.toLowerCase()) ||
    user.full_name?.toLowerCase().includes(searchQuery.toLowerCase())
  );

  const getInitials = (user: UserData) => {
    if (user.full_name) return user.full_name.charAt(0).toUpperCase();
    return user.username.charAt(0).toUpperCase();
  };

  // Oracle may return "1"/"0" strings instead of booleans
  const toBool = (v: unknown): boolean => v === true || v === 1 || v === '1' || v === 'true';
  const adminCount = users.filter(u => toBool(u.is_admin)).length;
  const activeCount = users.filter(u => toBool(u.is_active)).length;

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="mb-6">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-blue-500 to-indigo-600 shadow-lg">
            <Users className="w-7 h-7 text-white" />
          </div>
          <div className="flex-1">
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
              {t('admin:userMgmt.title')}
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              {t('admin:userMgmt.subtitle')}
            </p>
          </div>
          <button
            onClick={() => setShowCreateModal(true)}
            className="flex items-center gap-2 px-4 py-2 bg-gradient-to-r from-blue-500 to-indigo-500 text-white rounded-xl hover:from-blue-600 hover:to-indigo-600 transition-all shadow-md hover:shadow-lg"
          >
            <Plus className="w-4 h-4" />
            {t('admin:userMgmt.addUser')}
          </button>
        </div>
      </div>

      {/* Success/Error Messages */}
      {success && (
        <div className="bg-green-50 dark:bg-green-900/30 border border-green-200 dark:border-green-800 rounded-xl p-4 flex items-center gap-2">
          <Check className="w-5 h-5 text-green-600 dark:text-green-400 flex-shrink-0" />
          <span className="text-green-700 dark:text-green-300">{success}</span>
          <button onClick={() => setSuccess('')} className="ml-auto">
            <X className="w-4 h-4 text-green-600 dark:text-green-400" />
          </button>
        </div>
      )}

      {error && (
        <div className="bg-red-50 dark:bg-red-900/30 border border-red-200 dark:border-red-800 rounded-xl p-4 flex items-center gap-2">
          <AlertCircle className="w-5 h-5 text-red-600 dark:text-red-400 flex-shrink-0" />
          <span className="text-red-700 dark:text-red-300">{error}</span>
          <button onClick={() => setError('')} className="ml-auto">
            <X className="w-4 h-4 text-red-600 dark:text-red-400" />
          </button>
        </div>
      )}

      {/* Summary Stats + Search */}
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-blue-500">
          <p className="text-sm text-gray-500 dark:text-gray-400">{t('admin:userManagement.totalUsers')}</p>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{users.length}</p>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-indigo-500">
          <p className="text-sm text-gray-500 dark:text-gray-400">{t('admin:userManagement.adminUsers')}</p>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{(adminCount ?? 0).toLocaleString()}</p>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4 border-l-4 border-green-500">
          <p className="text-sm text-gray-500 dark:text-gray-400">{t('admin:userManagement.activeUsers')}</p>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{(activeCount ?? 0).toLocaleString()}</p>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-4">
          <label htmlFor="user-search" className="text-sm text-gray-500 dark:text-gray-400 mb-2 block">{t('common:button.search')}</label>
          <div className="relative">
            <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 w-4 h-4 text-gray-400" />
            <input
              id="user-search"
              name="searchQuery"
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder={t('admin:userMgmt.searchPlaceholder')}
              className="w-full pl-9 pr-4 py-1.5 text-sm border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
        </div>
      </div>

      {/* Users Grid */}
      {loading ? (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-12 text-center">
          <div className="animate-spin w-8 h-8 border-2 border-blue-500 border-t-transparent rounded-full mx-auto mb-3" />
          <p className="text-gray-500 dark:text-gray-400">{t('admin:userMgmt.loadingUsers')}</p>
        </div>
      ) : filteredUsers.length === 0 ? (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-12 text-center">
          <Users className="w-12 h-12 text-gray-300 dark:text-gray-600 mx-auto mb-3" />
          <p className="text-gray-500 dark:text-gray-400">
            {searchQuery ? t('common:table.noResults') : t('admin:userMgmt.noUsers')}
          </p>
        </div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          {filteredUsers.map((user) => (
            <div
              key={user.id}
              className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 hover:shadow-xl transition-shadow"
            >
              {/* Card Header — Avatar + Identity */}
              <div className="p-5 pb-4">
                <div className="flex items-start gap-4">
                  <div className={`w-12 h-12 rounded-xl flex items-center justify-center text-white font-bold text-lg flex-shrink-0 ${
                    toBool(user.is_admin)
                      ? 'bg-gradient-to-br from-blue-500 to-indigo-600'
                      : 'bg-gradient-to-br from-gray-400 to-gray-500'
                  }`}>
                    {getInitials(user)}
                  </div>
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center gap-2">
                      <h3 className="text-base font-semibold text-gray-900 dark:text-white truncate">
                        {user.full_name || user.username}
                      </h3>
                      {toBool(user.is_admin) && (
                        <span className="inline-flex items-center gap-1 px-2 py-0.5 bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300 text-xs font-medium rounded-full flex-shrink-0">
                          <Shield className="w-3 h-3" />
                          {t('admin:userManagement.adminUsers')}
                        </span>
                      )}
                    </div>
                    <p className="text-sm text-gray-500 dark:text-gray-400 truncate">
                      @{user.username}
                    </p>
                  </div>
                </div>
              </div>

              {/* Card Body — Info Grid */}
              <div className="px-5 pb-4 space-y-3">
                <div className="grid grid-cols-1 gap-2">
                  <div className="flex items-center gap-2 text-sm">
                    <Mail className="w-4 h-4 text-gray-400 flex-shrink-0" />
                    <span className="text-gray-600 dark:text-gray-400 truncate">
                      {user.email || t('admin:userMgmt.emailNotSet')}
                    </span>
                  </div>
                  <div className="flex items-center gap-2 text-sm">
                    <Clock className="w-4 h-4 text-gray-400 flex-shrink-0" />
                    <span className="text-gray-600 dark:text-gray-400">
                      {formatDateTime(user.last_login_at, t('admin:userMgmt.noLoginRecord'))}
                    </span>
                  </div>
                </div>

                {/* Permissions */}
                <div className="flex flex-wrap gap-1">
                  {user.permissions.map((perm) => (
                    <span
                      key={perm}
                      className="px-2 py-0.5 bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400 text-xs rounded-full"
                    >
                      {t(AVAILABLE_PERMISSIONS.find(p => p.value === perm)?.label ?? '') || perm}
                    </span>
                  ))}
                  {user.permissions.length === 0 && (
                    toBool(user.is_admin)
                      ? <span className="px-2 py-0.5 bg-amber-50 dark:bg-amber-900/20 text-amber-600 dark:text-amber-400 text-xs rounded-full">{t('common:label.allPermissions')}</span>
                      : <span className="text-xs text-gray-400 dark:text-gray-500 italic">{t('common:label.noPermissions')}</span>
                  )}
                </div>
              </div>

              {/* Card Footer — Actions */}
              <div className="px-5 py-3 border-t border-gray-100 dark:border-gray-700/50 flex items-center justify-between">
                <div className="flex items-center gap-0.5">
                  <button
                    onClick={() => navigate(`/admin/audit-log?username=${encodeURIComponent(user.username)}`)}
                    className="inline-flex items-center gap-1 px-2 py-1.5 text-xs font-medium text-gray-500 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors"
                    title={t('admin:auditLog.title')}
                  >
                    <FileText className="w-3.5 h-3.5" />
                  </button>
                  <button
                    onClick={() => navigate(`/admin/operation-audit?username=${encodeURIComponent(user.username)}`)}
                    className="inline-flex items-center gap-1 px-2 py-1.5 text-xs font-medium text-gray-500 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors"
                    title={t('admin:operationAudit.title')}
                  >
                    <Activity className="w-3.5 h-3.5" />
                  </button>
                </div>
                <div className="flex items-center gap-1">
                  <button
                    onClick={() => openEditModal(user)}
                    className="inline-flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/30 rounded-lg transition-colors"
                  >
                    <Edit className="w-3.5 h-3.5" />
                    {t('common:button.edit')}
                  </button>
                  <button
                    onClick={() => openPasswordModal(user)}
                    className="inline-flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-purple-600 dark:text-purple-400 hover:bg-purple-50 dark:hover:bg-purple-900/30 rounded-lg transition-colors"
                  >
                    <Key className="w-3.5 h-3.5" />
                    {t('common:label.password')}
                  </button>
                  <button
                    onClick={() => openDeleteModal(user)}
                    className="inline-flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-red-600 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-900/30 rounded-lg transition-colors"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                    {t('common:button.delete')}
                  </button>
                </div>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Create User Modal */}
      {showCreateModal && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-[70] p-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-3xl w-full max-h-[90vh] flex flex-col">
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-3 flex-shrink-0">
              <div className="p-1.5 rounded-lg bg-blue-100 dark:bg-blue-900/30">
                <Plus className="w-4 h-4 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <h2 className="text-base font-bold text-gray-900 dark:text-white">{t('admin:userMgmt.addUser')}</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">{t('admin:userMgmt.registerNewUser')}</p>
              </div>
            </div>
            <div className="px-5 py-4 space-y-3 overflow-y-auto flex-1">
              {/* 2-column grid for basic fields */}
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                <div>
                  <label htmlFor="create-username" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    {t('common:label.username')} <span className="text-red-500">*</span>
                  </label>
                  <div className="relative">
                    <User className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      id="create-username"
                      name="username"
                      type="text"
                      value={formData.username}
                      onChange={(e) => setFormData({ ...formData, username: e.target.value })}
                      placeholder={t('admin:userMgmt.usernamePlaceholder')}
                      className="w-full pl-9 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label htmlFor="create-password" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    {t('common:label.password')} <span className="text-red-500">*</span>
                  </label>
                  <div className="relative">
                    <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      id="create-password"
                      name="password"
                      type="password"
                      value={formData.password}
                      onChange={(e) => setFormData({ ...formData, password: e.target.value })}
                      placeholder={t('admin:userMgmt.minChars8')}
                      className="w-full pl-9 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label htmlFor="create-fullname" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    {t('common:label.name')}
                  </label>
                  <input
                    id="create-fullname"
                    name="fullName"
                    type="text"
                    value={formData.full_name}
                    onChange={(e) => setFormData({ ...formData, full_name: e.target.value })}
                    placeholder={t('admin:userMgmt.fullNamePlaceholder')}
                    className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                  />
                </div>
                <div>
                  <label htmlFor="create-email" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    {t('common:label.email')}
                  </label>
                  <div className="relative">
                    <Mail className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      id="create-email"
                      name="email"
                      type="email"
                      value={formData.email}
                      onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                      placeholder="user@example.com"
                      className="w-full pl-9 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
              </div>

              {/* Admin toggle — inline with permissions row */}
              <div className="flex items-center gap-3 p-2.5 bg-gray-50 dark:bg-gray-900/50 rounded-xl">
                <label htmlFor="create-is-admin" className="relative inline-flex items-center cursor-pointer">
                  <input
                    id="create-is-admin"
                    name="isAdmin"
                    type="checkbox"
                    checked={formData.is_admin}
                    onChange={(e) => setFormData({ ...formData, is_admin: e.target.checked })}
                    className="sr-only peer"
                  />
                  <div className="w-9 h-5 bg-gray-300 peer-focus:outline-none peer-focus:ring-2 peer-focus:ring-blue-500 rounded-full peer dark:bg-gray-600 peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-4 after:w-4 after:transition-all peer-checked:bg-blue-600" />
                </label>
                <div>
                  <p className="text-sm font-medium text-gray-700 dark:text-gray-300">{t('admin:userMgmt.adminPrivilege')}</p>
                  <p className="text-xs text-gray-500 dark:text-gray-400">{t('admin:userMgmt.adminPrivilegeDesc')}</p>
                </div>
              </div>

              {/* Permissions Grid — grouped by menu section */}
              <div>
                <span className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                  {t('common:label.permissionSettings')}
                </span>
                <div className="space-y-3">
                  {PERMISSION_GROUPS.map((group) => (
                    <div key={group.label}>
                      <p className="text-xs font-semibold text-gray-400 dark:text-gray-500 uppercase tracking-wider mb-1.5">{t(group.label)}</p>
                      <div className="grid grid-cols-2 lg:grid-cols-3 gap-1.5">
                        {group.permissions.map((perm) => (
                          <label
                            key={perm.value}
                            htmlFor={`create-perm-${perm.value}`}
                            className={`flex items-center gap-2 px-2.5 py-2 border rounded-lg cursor-pointer transition-colors ${
                              formData.permissions.includes(perm.value)
                                ? 'border-blue-300 dark:border-blue-700 bg-blue-50 dark:bg-blue-900/20'
                                : 'border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50'
                            }`}
                          >
                            <input
                              id={`create-perm-${perm.value}`}
                              name={`perm-${perm.value}`}
                              type="checkbox"
                              checked={formData.permissions.includes(perm.value)}
                              onChange={() => togglePermission(perm.value)}
                              className="w-3.5 h-3.5 text-blue-600 border-gray-300 rounded focus:ring-blue-500"
                            />
                            <span className="text-sm text-gray-700 dark:text-gray-300">{t(perm.label)}</span>
                          </label>
                        ))}
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => { setShowCreateModal(false); resetForm(); }}
                className="px-4 py-2 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl transition-colors text-sm"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleCreateUser}
                disabled={!formData.username || !formData.password}
                className="px-5 py-2 bg-gradient-to-r from-blue-500 to-indigo-500 text-white rounded-xl hover:from-blue-600 hover:to-indigo-600 transition-all shadow-md disabled:opacity-50 disabled:cursor-not-allowed text-sm font-medium"
              >
                {t('admin:userMgmt.addUser')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Edit User Modal */}
      {showEditModal && selectedUser && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-[70] p-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-3xl w-full max-h-[90vh] flex flex-col">
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-2.5 flex-shrink-0">
              <div className="p-1.5 rounded-lg bg-blue-100 dark:bg-blue-900/30">
                <Edit className="w-4 h-4 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <h2 className="text-base font-bold text-gray-900 dark:text-white">{t('admin:userManagement.editUser')}</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">@{selectedUser.username}</p>
              </div>
            </div>
            <div className="px-5 py-4 space-y-3 overflow-y-auto flex-1">
              {/* 3-column grid for basic fields */}
              <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
                <div>
                  <label htmlFor="edit-username" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">{t('common:label.username')}</label>
                  <div className="relative">
                    <User className="absolute left-3 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-gray-400" />
                    <input
                      id="edit-username"
                      name="username"
                      type="text"
                      value={formData.username}
                      disabled
                      className="w-full pl-8 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-gray-100 dark:bg-gray-900 text-gray-500 dark:text-gray-400 cursor-not-allowed text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label htmlFor="edit-email" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">{t('common:label.email')}</label>
                  <div className="relative">
                    <Mail className="absolute left-3 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-gray-400" />
                    <input
                      id="edit-email"
                      name="email"
                      type="email"
                      value={formData.email}
                      onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                      placeholder="user@example.com"
                      className="w-full pl-8 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label htmlFor="edit-fullname" className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1">{t('admin:userManagement.fullName')}</label>
                  <input
                    id="edit-fullname"
                    name="fullName"
                    type="text"
                    value={formData.full_name}
                    onChange={(e) => setFormData({ ...formData, full_name: e.target.value })}
                    placeholder={t('admin:userMgmt.fullNamePlaceholder')}
                    className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                  />
                </div>
              </div>

              {/* Admin toggle — compact inline */}
              <div className="flex items-center gap-3 px-2.5 py-2 bg-gray-50 dark:bg-gray-900/50 rounded-lg">
                <label htmlFor="edit-is-admin" className="relative inline-flex items-center cursor-pointer">
                  <input
                    id="edit-is-admin"
                    name="isAdmin"
                    type="checkbox"
                    checked={formData.is_admin}
                    onChange={(e) => setFormData({ ...formData, is_admin: e.target.checked })}
                    className="sr-only peer"
                  />
                  <div className="w-9 h-5 bg-gray-300 peer-focus:outline-none peer-focus:ring-2 peer-focus:ring-blue-500 rounded-full peer dark:bg-gray-600 peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-4 after:w-4 after:transition-all peer-checked:bg-blue-600" />
                </label>
                <p className="text-sm font-medium text-gray-700 dark:text-gray-300">{t('admin:userMgmt.adminPrivilege')}</p>
                <p className="text-xs text-gray-500 dark:text-gray-400">— {t('admin:userMgmt.adminPrivilegeDesc')}</p>
              </div>

              {/* Permissions Grid — compact 3-col, no descriptions */}
              <div>
                <span className="block text-xs font-medium text-gray-700 dark:text-gray-300 mb-1.5">{t('common:label.permissionSettings')}</span>
                <div className="space-y-2">
                  {PERMISSION_GROUPS.map((group) => (
                    <div key={group.label}>
                      <p className="text-xs font-semibold text-gray-400 dark:text-gray-500 uppercase tracking-wider mb-1">{t(group.label)}</p>
                      <div className="grid grid-cols-2 lg:grid-cols-3 gap-1.5">
                        {group.permissions.map((perm) => (
                          <label
                            key={perm.value}
                            htmlFor={`edit-perm-${perm.value}`}
                            className={`flex items-center gap-2 px-2.5 py-1.5 border rounded-lg cursor-pointer transition-colors ${
                              formData.permissions.includes(perm.value)
                                ? 'border-blue-300 dark:border-blue-700 bg-blue-50 dark:bg-blue-900/20'
                                : 'border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50'
                            }`}
                          >
                            <input
                              id={`edit-perm-${perm.value}`}
                              name={`perm-${perm.value}`}
                              type="checkbox"
                              checked={formData.permissions.includes(perm.value)}
                              onChange={() => togglePermission(perm.value)}
                              className="w-3.5 h-3.5 text-blue-600 border-gray-300 rounded focus:ring-blue-500"
                            />
                            <span className="text-sm text-gray-700 dark:text-gray-300">{t(perm.label)}</span>
                          </label>
                        ))}
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => { setShowEditModal(false); resetForm(); }}
                className="px-4 py-1.5 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors text-sm"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleUpdateUser}
                className="px-5 py-1.5 bg-gradient-to-r from-blue-500 to-indigo-500 text-white rounded-lg hover:from-blue-600 hover:to-indigo-600 transition-all shadow-md text-sm font-medium"
              >
                {t('common:button.save')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Delete Confirmation Modal */}
      {showDeleteModal && selectedUser && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-[70] p-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-md w-full">
            <div className="p-5 text-center">
              <div className="w-12 h-12 mx-auto mb-3 rounded-xl bg-red-100 dark:bg-red-900/30 flex items-center justify-center">
                <Trash2 className="w-6 h-6 text-red-600 dark:text-red-400" />
              </div>
              <h2 className="text-base font-bold text-gray-900 dark:text-white mb-2">{t('admin:userManagement.deleteUser')}</h2>
              <p className="text-sm text-gray-600 dark:text-gray-400 mb-1">
                {t('admin:userMgmt.deleteConfirmQuestion')}
              </p>
              <div className="inline-flex items-center gap-2 mt-2 mb-4 px-3 py-1.5 bg-gray-100 dark:bg-gray-700 rounded-lg">
                <User className="w-4 h-4 text-gray-500" />
                <span className="text-sm font-medium text-gray-900 dark:text-white">{selectedUser.full_name || selectedUser.username}</span>
                <span className="text-xs text-gray-500">@{selectedUser.username}</span>
              </div>
              <p className="text-xs text-red-500 dark:text-red-400">
                {t('admin:userMgmt.cannotUndo')}
              </p>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex gap-2">
              <button
                onClick={() => { setShowDeleteModal(false); setSelectedUser(null); }}
                className="flex-1 px-4 py-1.5 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors text-sm font-medium border border-gray-200 dark:border-gray-600"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleDeleteUser}
                className="flex-1 px-4 py-1.5 bg-red-600 text-white rounded-lg hover:bg-red-700 transition-colors text-sm font-medium"
              >
                {t('common:button.delete')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Change Password Modal */}
      {showPasswordModal && selectedUser && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-[70] p-4">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-xl max-w-md w-full">
            <div className="px-5 py-3 border-b border-gray-200 dark:border-gray-700 flex items-center gap-3">
              <div className="p-1.5 rounded-lg bg-purple-100 dark:bg-purple-900/30">
                <Key className="w-4 h-4 text-purple-600 dark:text-purple-400" />
              </div>
              <div>
                <h2 className="text-base font-bold text-gray-900 dark:text-white">{t('admin:userMgmt.changePassword')}</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">@{selectedUser.username}</p>
              </div>
            </div>
            <div className="px-5 py-4 space-y-3">
              {authApi.getStoredUser()?.id === selectedUser.id && (
                <div>
                  <label htmlFor="pw-current" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                    {t('admin:userMgmt.currentPassword')}
                  </label>
                  <div className="relative">
                    <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      id="pw-current"
                      name="currentPassword"
                      type="password"
                      value={passwordData.currentPassword}
                      onChange={(e) => setPasswordData({ ...passwordData, currentPassword: e.target.value })}
                      placeholder={t('admin:userMgmt.currentPasswordInput')}
                      className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                    />
                  </div>
                </div>
              )}
              <div>
                <label htmlFor="pw-new" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                  {t('admin:userMgmt.newPassword')}
                </label>
                <div className="relative">
                  <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                  <input
                    id="pw-new"
                    name="newPassword"
                    type="password"
                    value={passwordData.newPassword}
                    onChange={(e) => setPasswordData({ ...passwordData, newPassword: e.target.value })}
                    placeholder={t('admin:userMgmt.minChars8Input')}
                    className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                  />
                </div>
              </div>
              <div>
                <label htmlFor="pw-confirm" className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                  {t('admin:userMgmt.confirmPassword')}
                </label>
                <div className="relative">
                  <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                  <input
                    id="pw-confirm"
                    name="confirmPassword"
                    type="password"
                    value={passwordData.confirmPassword}
                    onChange={(e) => setPasswordData({ ...passwordData, confirmPassword: e.target.value })}
                    placeholder={t('admin:userMgmt.confirmPasswordInput')}
                    className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                  />
                </div>
                {passwordData.confirmPassword && passwordData.newPassword !== passwordData.confirmPassword && (
                  <p className="mt-1.5 text-xs text-red-500 flex items-center gap-1">
                    <AlertCircle className="w-3 h-3" />
                    {t('common:validation.passwordMismatch')}
                  </p>
                )}
              </div>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => { setShowPasswordModal(false); setPasswordData({ currentPassword: '', newPassword: '', confirmPassword: '' }); setSelectedUser(null); }}
                className="px-4 py-1.5 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-lg transition-colors text-sm"
              >
                {t('common:button.cancel')}
              </button>
              <button
                onClick={handleChangePassword}
                disabled={!passwordData.newPassword || passwordData.newPassword !== passwordData.confirmPassword || (authApi.getStoredUser()?.id === selectedUser.id && !passwordData.currentPassword)}
                className="px-5 py-1.5 bg-gradient-to-r from-purple-500 to-violet-500 text-white rounded-lg hover:from-purple-600 hover:to-violet-600 transition-all shadow-md disabled:opacity-50 disabled:cursor-not-allowed text-sm font-medium"
              >
                {t('admin:userMgmt.change')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default UserManagement;
