import { useState, useEffect } from 'react';
import { Users, Plus, Edit, Trash2, Key, Shield, Search, X, Check, AlertCircle, Mail, Clock, User } from 'lucide-react';
import { createAuthenticatedClient } from '@/services/authApi';

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

const AVAILABLE_PERMISSIONS = [
  { value: 'upload:read', label: '업로드 조회', desc: '업로드 이력 조회' },
  { value: 'upload:write', label: '파일 업로드', desc: '파일 업로드 실행' },
  { value: 'cert:read', label: '인증서 조회', desc: '인증서 검색 및 조회' },
  { value: 'cert:export', label: '인증서 내보내기', desc: '인증서 파일 내보내기' },
  { value: 'pa:verify', label: 'PA 검증', desc: 'Passive Authentication 검증' },
  { value: 'sync:read', label: '동기화 조회', desc: 'DB-LDAP 동기화 상태 조회' },
  { value: 'sync:write', label: '동기화 실행', desc: 'DB-LDAP 수동 동기화 실행' },
];

export function UserManagement() {
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
    newPassword: '',
    confirmPassword: '',
  });

  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  useEffect(() => {
    fetchUsers();
  }, []);

  const fetchUsers = async () => {
    try {
      setLoading(true);
      const { data } = await authClient.get('/users');
      setUsers(data.users || []);
    } catch (error) {
      if (import.meta.env.DEV) console.error('Error fetching users:', error);
      setError('사용자 목록을 불러오는데 실패했습니다.');
    } finally {
      setLoading(false);
    }
  };

  const handleCreateUser = async () => {
    try {
      setError('');
      await authClient.post('/users', formData);
      setSuccess('사용자가 생성되었습니다.');
      setShowCreateModal(false);
      fetchUsers();
      resetForm();
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || '사용자 생성에 실패했습니다.');
    }
  };

  const handleUpdateUser = async () => {
    if (!selectedUser) return;

    try {
      setError('');
      const { password, ...updateData } = formData;
      await authClient.put(`/users/${selectedUser.id}`, updateData);
      setSuccess('사용자 정보가 수정되었습니다.');
      setShowEditModal(false);
      fetchUsers();
      resetForm();
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || '사용자 수정에 실패했습니다.');
    }
  };

  const handleDeleteUser = async () => {
    if (!selectedUser) return;

    try {
      setError('');
      await authClient.delete(`/users/${selectedUser.id}`);
      setSuccess('사용자가 삭제되었습니다.');
      setShowDeleteModal(false);
      fetchUsers();
      setSelectedUser(null);
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || '사용자 삭제에 실패했습니다.');
    }
  };

  const handleChangePassword = async () => {
    if (!selectedUser) return;

    if (passwordData.newPassword !== passwordData.confirmPassword) {
      setError('비밀번호가 일치하지 않습니다.');
      return;
    }

    try {
      setError('');
      await authClient.put(`/users/${selectedUser.id}/password`, {
        new_password: passwordData.newPassword,
      });
      setSuccess('비밀번호가 변경되었습니다.');
      setShowPasswordModal(false);
      setPasswordData({ newPassword: '', confirmPassword: '' });
      setSelectedUser(null);
    } catch (error: any) {
      setError(error.response?.data?.message || error.message || '비밀번호 변경에 실패했습니다.');
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
      permissions: user.permissions,
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

  const formatDateTime = (dateString: string) => {
    if (!dateString) return '로그인 기록 없음';
    const date = new Date(dateString);
    return date.toLocaleDateString('ko-KR', {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit',
    });
  };

  const getInitials = (user: UserData) => {
    if (user.full_name) return user.full_name.charAt(0).toUpperCase();
    return user.username.charAt(0).toUpperCase();
  };

  const adminCount = users.filter(u => u.is_admin).length;
  const activeCount = users.filter(u => u.is_active).length;

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
              사용자 관리
            </h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">
              시스템 사용자 및 권한 관리
            </p>
          </div>
          <button
            onClick={() => setShowCreateModal(true)}
            className="flex items-center gap-2 px-4 py-2 bg-gradient-to-r from-blue-500 to-indigo-500 text-white rounded-xl hover:from-blue-600 hover:to-indigo-600 transition-all shadow-md hover:shadow-lg"
          >
            <Plus className="w-4 h-4" />
            사용자 추가
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
      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-blue-500">
          <p className="text-sm text-gray-500 dark:text-gray-400">전체 사용자</p>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{users.length}</p>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-indigo-500">
          <p className="text-sm text-gray-500 dark:text-gray-400">관리자</p>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{adminCount}</p>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-green-500">
          <p className="text-sm text-gray-500 dark:text-gray-400">활성 사용자</p>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{activeCount}</p>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5">
          <p className="text-sm text-gray-500 dark:text-gray-400 mb-2">검색</p>
          <div className="relative">
            <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 w-4 h-4 text-gray-400" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="사용자명, 이메일, 이름..."
              className="w-full pl-9 pr-4 py-1.5 text-sm border border-gray-300 dark:border-gray-600 rounded-lg bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
        </div>
      </div>

      {/* Users Grid */}
      {loading ? (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-12 text-center">
          <div className="animate-spin w-8 h-8 border-2 border-blue-500 border-t-transparent rounded-full mx-auto mb-3" />
          <p className="text-gray-500 dark:text-gray-400">사용자 목록 로딩 중...</p>
        </div>
      ) : filteredUsers.length === 0 ? (
        <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-12 text-center">
          <Users className="w-12 h-12 text-gray-300 dark:text-gray-600 mx-auto mb-3" />
          <p className="text-gray-500 dark:text-gray-400">
            {searchQuery ? '검색 결과가 없습니다.' : '등록된 사용자가 없습니다.'}
          </p>
        </div>
      ) : (
        <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4">
          {filteredUsers.map((user) => (
            <div
              key={user.id}
              className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 hover:shadow-xl transition-shadow"
            >
              {/* Card Header — Avatar + Identity */}
              <div className="p-5 pb-4">
                <div className="flex items-start gap-4">
                  <div className={`w-12 h-12 rounded-xl flex items-center justify-center text-white font-bold text-lg flex-shrink-0 ${
                    user.is_admin
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
                      {user.is_admin && (
                        <span className="inline-flex items-center gap-1 px-2 py-0.5 bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300 text-xs font-medium rounded-full flex-shrink-0">
                          <Shield className="w-3 h-3" />
                          관리자
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
                      {user.email || '이메일 미설정'}
                    </span>
                  </div>
                  <div className="flex items-center gap-2 text-sm">
                    <Clock className="w-4 h-4 text-gray-400 flex-shrink-0" />
                    <span className="text-gray-600 dark:text-gray-400">
                      {formatDateTime(user.last_login_at || '')}
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
                      {AVAILABLE_PERMISSIONS.find(p => p.value === perm)?.label || perm}
                    </span>
                  ))}
                  {user.permissions.length === 0 && (
                    <span className="text-xs text-gray-400 dark:text-gray-500 italic">권한 없음</span>
                  )}
                </div>
              </div>

              {/* Card Footer — Actions */}
              <div className="px-5 py-3 border-t border-gray-100 dark:border-gray-700/50 flex items-center justify-end gap-1">
                <button
                  onClick={() => openEditModal(user)}
                  className="inline-flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-blue-600 dark:text-blue-400 hover:bg-blue-50 dark:hover:bg-blue-900/30 rounded-lg transition-colors"
                >
                  <Edit className="w-3.5 h-3.5" />
                  수정
                </button>
                <button
                  onClick={() => openPasswordModal(user)}
                  className="inline-flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-purple-600 dark:text-purple-400 hover:bg-purple-50 dark:hover:bg-purple-900/30 rounded-lg transition-colors"
                >
                  <Key className="w-3.5 h-3.5" />
                  비밀번호
                </button>
                <button
                  onClick={() => openDeleteModal(user)}
                  className="inline-flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-red-600 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-900/30 rounded-lg transition-colors"
                >
                  <Trash2 className="w-3.5 h-3.5" />
                  삭제
                </button>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Create User Modal */}
      {showCreateModal && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl max-w-3xl w-full">
            <div className="px-5 py-4 border-b border-gray-200 dark:border-gray-700 flex items-center gap-3">
              <div className="p-2 rounded-lg bg-blue-100 dark:bg-blue-900/30">
                <Plus className="w-5 h-5 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">사용자 추가</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">새로운 시스템 사용자 등록</p>
              </div>
            </div>
            <div className="px-5 py-4 space-y-4">
              {/* 2-column grid for basic fields */}
              <div className="grid grid-cols-2 gap-3">
                <div>
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    사용자명 <span className="text-red-500">*</span>
                  </label>
                  <div className="relative">
                    <User className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      type="text"
                      value={formData.username}
                      onChange={(e) => setFormData({ ...formData, username: e.target.value })}
                      placeholder="영문 소문자, 숫자"
                      className="w-full pl-9 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    비밀번호 <span className="text-red-500">*</span>
                  </label>
                  <div className="relative">
                    <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      type="password"
                      value={formData.password}
                      onChange={(e) => setFormData({ ...formData, password: e.target.value })}
                      placeholder="8자 이상"
                      className="w-full pl-9 pr-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    이름
                  </label>
                  <input
                    type="text"
                    value={formData.full_name}
                    onChange={(e) => setFormData({ ...formData, full_name: e.target.value })}
                    placeholder="사용자 이름"
                    className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                  />
                </div>
                <div>
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1">
                    이메일
                  </label>
                  <div className="relative">
                    <Mail className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
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
                <label className="relative inline-flex items-center cursor-pointer">
                  <input
                    type="checkbox"
                    checked={formData.is_admin}
                    onChange={(e) => setFormData({ ...formData, is_admin: e.target.checked })}
                    className="sr-only peer"
                  />
                  <div className="w-9 h-5 bg-gray-300 peer-focus:outline-none peer-focus:ring-2 peer-focus:ring-blue-500 rounded-full peer dark:bg-gray-600 peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-4 after:w-4 after:transition-all peer-checked:bg-blue-600" />
                </label>
                <div>
                  <p className="text-sm font-medium text-gray-700 dark:text-gray-300">관리자 권한</p>
                  <p className="text-xs text-gray-500 dark:text-gray-400">모든 시스템 기능에 대한 전체 접근 권한</p>
                </div>
              </div>

              {/* Permissions Grid — compact 3-column */}
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                  권한 설정
                </label>
                <div className="grid grid-cols-2 lg:grid-cols-3 gap-1.5">
                  {AVAILABLE_PERMISSIONS.map((perm) => (
                    <label
                      key={perm.value}
                      className={`flex items-center gap-2 px-2.5 py-2 border rounded-lg cursor-pointer transition-colors ${
                        formData.permissions.includes(perm.value)
                          ? 'border-blue-300 dark:border-blue-700 bg-blue-50 dark:bg-blue-900/20'
                          : 'border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50'
                      }`}
                    >
                      <input
                        type="checkbox"
                        checked={formData.permissions.includes(perm.value)}
                        onChange={() => togglePermission(perm.value)}
                        className="w-3.5 h-3.5 text-blue-600 border-gray-300 rounded focus:ring-blue-500"
                      />
                      <span className="text-sm text-gray-700 dark:text-gray-300">{perm.label}</span>
                    </label>
                  ))}
                </div>
              </div>
            </div>
            <div className="px-5 py-3 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => { setShowCreateModal(false); resetForm(); }}
                className="px-4 py-2 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl transition-colors text-sm"
              >
                취소
              </button>
              <button
                onClick={handleCreateUser}
                disabled={!formData.username || !formData.password}
                className="px-5 py-2 bg-gradient-to-r from-blue-500 to-indigo-500 text-white rounded-xl hover:from-blue-600 hover:to-indigo-600 transition-all shadow-md disabled:opacity-50 disabled:cursor-not-allowed text-sm font-medium"
              >
                사용자 추가
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Edit User Modal */}
      {showEditModal && selectedUser && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl max-w-2xl w-full max-h-[90vh] overflow-y-auto">
            <div className="p-6 border-b border-gray-200 dark:border-gray-700 flex items-center gap-3">
              <div className="p-2 rounded-lg bg-blue-100 dark:bg-blue-900/30">
                <Edit className="w-5 h-5 text-blue-600 dark:text-blue-400" />
              </div>
              <div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">사용자 수정</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">@{selectedUser.username}</p>
              </div>
            </div>
            <div className="p-6 space-y-5">
              {/* 2-column grid for basic fields */}
              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                <div>
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                    사용자명
                  </label>
                  <div className="relative">
                    <User className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      type="text"
                      value={formData.username}
                      disabled
                      className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-gray-100 dark:bg-gray-900 text-gray-500 dark:text-gray-400 cursor-not-allowed text-sm"
                    />
                  </div>
                </div>
                <div>
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                    이메일
                  </label>
                  <div className="relative">
                    <Mail className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                    <input
                      type="email"
                      value={formData.email}
                      onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                      placeholder="user@example.com"
                      className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                    />
                  </div>
                </div>
                <div className="md:col-span-2">
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                    이름
                  </label>
                  <input
                    type="text"
                    value={formData.full_name}
                    onChange={(e) => setFormData({ ...formData, full_name: e.target.value })}
                    placeholder="사용자 이름"
                    className="w-full px-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm"
                  />
                </div>
              </div>

              {/* Admin toggle */}
              <div className="flex items-center gap-3 p-3 bg-gray-50 dark:bg-gray-900/50 rounded-xl">
                <label className="relative inline-flex items-center cursor-pointer">
                  <input
                    type="checkbox"
                    checked={formData.is_admin}
                    onChange={(e) => setFormData({ ...formData, is_admin: e.target.checked })}
                    className="sr-only peer"
                  />
                  <div className="w-9 h-5 bg-gray-300 peer-focus:outline-none peer-focus:ring-2 peer-focus:ring-blue-500 rounded-full peer dark:bg-gray-600 peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-4 after:w-4 after:transition-all peer-checked:bg-blue-600" />
                </label>
                <div>
                  <p className="text-sm font-medium text-gray-700 dark:text-gray-300">관리자 권한</p>
                  <p className="text-xs text-gray-500 dark:text-gray-400">모든 시스템 기능에 대한 전체 접근 권한</p>
                </div>
              </div>

              {/* Permissions Grid */}
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  권한 설정
                </label>
                <div className="grid grid-cols-1 md:grid-cols-2 gap-2">
                  {AVAILABLE_PERMISSIONS.map((perm) => (
                    <label
                      key={perm.value}
                      className={`flex items-center gap-3 p-3 border rounded-xl cursor-pointer transition-colors ${
                        formData.permissions.includes(perm.value)
                          ? 'border-blue-300 dark:border-blue-700 bg-blue-50 dark:bg-blue-900/20'
                          : 'border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50'
                      }`}
                    >
                      <input
                        type="checkbox"
                        checked={formData.permissions.includes(perm.value)}
                        onChange={() => togglePermission(perm.value)}
                        className="w-4 h-4 text-blue-600 border-gray-300 rounded focus:ring-blue-500"
                      />
                      <div className="min-w-0">
                        <p className="text-sm font-medium text-gray-700 dark:text-gray-300">{perm.label}</p>
                        <p className="text-xs text-gray-500 dark:text-gray-400">{perm.desc}</p>
                      </div>
                    </label>
                  ))}
                </div>
              </div>
            </div>
            <div className="p-6 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => { setShowEditModal(false); resetForm(); }}
                className="px-4 py-2 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl transition-colors text-sm"
              >
                취소
              </button>
              <button
                onClick={handleUpdateUser}
                className="px-5 py-2 bg-gradient-to-r from-blue-500 to-indigo-500 text-white rounded-xl hover:from-blue-600 hover:to-indigo-600 transition-all shadow-md text-sm font-medium"
              >
                저장
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Delete Confirmation Modal */}
      {showDeleteModal && selectedUser && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl max-w-md w-full">
            <div className="p-6 text-center">
              <div className="w-14 h-14 mx-auto mb-4 rounded-2xl bg-red-100 dark:bg-red-900/30 flex items-center justify-center">
                <Trash2 className="w-7 h-7 text-red-600 dark:text-red-400" />
              </div>
              <h2 className="text-lg font-bold text-gray-900 dark:text-white mb-2">사용자 삭제</h2>
              <p className="text-sm text-gray-600 dark:text-gray-400 mb-1">
                다음 사용자를 삭제하시겠습니까?
              </p>
              <div className="inline-flex items-center gap-2 mt-2 mb-4 px-3 py-1.5 bg-gray-100 dark:bg-gray-700 rounded-lg">
                <User className="w-4 h-4 text-gray-500" />
                <span className="text-sm font-medium text-gray-900 dark:text-white">{selectedUser.full_name || selectedUser.username}</span>
                <span className="text-xs text-gray-500">@{selectedUser.username}</span>
              </div>
              <p className="text-xs text-red-500 dark:text-red-400">
                이 작업은 되돌릴 수 없습니다.
              </p>
            </div>
            <div className="px-6 pb-6 flex gap-2">
              <button
                onClick={() => { setShowDeleteModal(false); setSelectedUser(null); }}
                className="flex-1 px-4 py-2.5 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl transition-colors text-sm font-medium border border-gray-200 dark:border-gray-600"
              >
                취소
              </button>
              <button
                onClick={handleDeleteUser}
                className="flex-1 px-4 py-2.5 bg-red-600 text-white rounded-xl hover:bg-red-700 transition-colors text-sm font-medium"
              >
                삭제
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Change Password Modal */}
      {showPasswordModal && selectedUser && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
          <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-xl max-w-md w-full">
            <div className="p-6 border-b border-gray-200 dark:border-gray-700 flex items-center gap-3">
              <div className="p-2 rounded-lg bg-purple-100 dark:bg-purple-900/30">
                <Key className="w-5 h-5 text-purple-600 dark:text-purple-400" />
              </div>
              <div>
                <h2 className="text-lg font-bold text-gray-900 dark:text-white">비밀번호 변경</h2>
                <p className="text-xs text-gray-500 dark:text-gray-400">@{selectedUser.username}</p>
              </div>
            </div>
            <div className="p-6 space-y-4">
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                  새 비밀번호
                </label>
                <div className="relative">
                  <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                  <input
                    type="password"
                    value={passwordData.newPassword}
                    onChange={(e) => setPasswordData({ ...passwordData, newPassword: e.target.value })}
                    placeholder="8자 이상 입력"
                    className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                  />
                </div>
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                  비밀번호 확인
                </label>
                <div className="relative">
                  <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                  <input
                    type="password"
                    value={passwordData.confirmPassword}
                    onChange={(e) => setPasswordData({ ...passwordData, confirmPassword: e.target.value })}
                    placeholder="비밀번호 다시 입력"
                    className="w-full pl-9 pr-3 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                  />
                </div>
                {passwordData.confirmPassword && passwordData.newPassword !== passwordData.confirmPassword && (
                  <p className="mt-1.5 text-xs text-red-500 flex items-center gap-1">
                    <AlertCircle className="w-3 h-3" />
                    비밀번호가 일치하지 않습니다
                  </p>
                )}
              </div>
            </div>
            <div className="p-6 border-t border-gray-200 dark:border-gray-700 flex justify-end gap-2">
              <button
                onClick={() => { setShowPasswordModal(false); setPasswordData({ newPassword: '', confirmPassword: '' }); setSelectedUser(null); }}
                className="px-4 py-2 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl transition-colors text-sm"
              >
                취소
              </button>
              <button
                onClick={handleChangePassword}
                disabled={!passwordData.newPassword || passwordData.newPassword !== passwordData.confirmPassword}
                className="px-5 py-2 bg-gradient-to-r from-purple-500 to-violet-500 text-white rounded-xl hover:from-purple-600 hover:to-violet-600 transition-all shadow-md disabled:opacity-50 disabled:cursor-not-allowed text-sm font-medium"
              >
                변경
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default UserManagement;
