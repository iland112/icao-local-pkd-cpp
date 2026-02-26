import { useState } from 'react';
import { authApi } from '@/services/api';
import { createAuthenticatedClient } from '@/services/authApi';
import { PERMISSION_GROUPS } from '@/utils/permissions';
import { User, Mail, Shield, Key, Check, AlertCircle, Eye, EyeOff, CheckCircle, XCircle } from 'lucide-react';

const authClient = createAuthenticatedClient('/api/auth');

export function Profile() {
  const user = authApi.getStoredUser();
  const [showPasswordForm, setShowPasswordForm] = useState(false);
  const [passwordData, setPasswordData] = useState({
    currentPassword: '',
    newPassword: '',
    confirmPassword: '',
  });
  const [showCurrentPw, setShowCurrentPw] = useState(false);
  const [showNewPw, setShowNewPw] = useState(false);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');
  const [loading, setLoading] = useState(false);

  const resetPasswordForm = () => {
    setPasswordData({ currentPassword: '', newPassword: '', confirmPassword: '' });
    setError('');
    setSuccess('');
    setShowCurrentPw(false);
    setShowNewPw(false);
  };

  const handleChangePassword = async () => {
    if (!user) return;

    if (!passwordData.currentPassword) {
      setError('현재 비밀번호를 입력해주세요.');
      return;
    }
    if (passwordData.newPassword.length < 8) {
      setError('새 비밀번호는 8자 이상이어야 합니다.');
      return;
    }
    if (passwordData.newPassword !== passwordData.confirmPassword) {
      setError('새 비밀번호가 일치하지 않습니다.');
      return;
    }

    try {
      setLoading(true);
      setError('');
      setSuccess('');
      await authClient.put(`/users/${user.id}/password`, {
        current_password: passwordData.currentPassword,
        new_password: passwordData.newPassword,
      });
      setSuccess('비밀번호가 변경되었습니다.');
      resetPasswordForm();
      setShowPasswordForm(false);
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '비밀번호 변경에 실패했습니다.';
      if (msg.includes('incorrect')) {
        setError('현재 비밀번호가 올바르지 않습니다.');
      } else {
        setError(msg);
      }
    } finally {
      setLoading(false);
    }
  };

  if (!user) {
    return (
      <div className="p-6">
        <div className="text-center">
          <p className="text-gray-600 dark:text-gray-400">
            사용자 정보를 불러올 수 없습니다.
          </p>
        </div>
      </div>
    );
  }

  return (
    <div className="p-6 max-w-4xl mx-auto">
      {/* Header */}
      <div className="mb-6">
        <h1 className="text-2xl font-bold text-gray-900 dark:text-white mb-2">
          프로필
        </h1>
        <p className="text-gray-600 dark:text-gray-400">
          사용자 정보 및 계정 설정
        </p>
      </div>

      {/* Success / Error Banner */}
      {success && (
        <div className="mb-4 p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-xl flex items-center gap-2">
          <Check className="w-4 h-4 text-green-600 dark:text-green-400" />
          <span className="text-sm text-green-700 dark:text-green-300">{success}</span>
        </div>
      )}

      {/* User Info Card */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-6 mb-6">
        <div className="flex items-center gap-4 mb-6">
          <div className="w-20 h-20 bg-gradient-to-br from-blue-500 to-indigo-500 rounded-full flex items-center justify-center">
            <User className="w-10 h-10 text-white" />
          </div>
          <div>
            <h2 className="text-xl font-semibold text-gray-900 dark:text-white">
              {user.full_name || user.username}
            </h2>
            <p className="text-gray-600 dark:text-gray-400">
              @{user.username}
            </p>
            {user.is_admin && (
              <span className="inline-flex items-center gap-1 mt-2 px-2 py-1 bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300 text-xs font-medium rounded-full">
                <Shield className="w-3 h-3" />
                관리자
              </span>
            )}
          </div>
        </div>

        {/* Info Grid */}
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
          {/* Email */}
          <div className="flex items-start gap-3 p-4 bg-gray-50 dark:bg-gray-900/50 rounded-lg">
            <Mail className="w-5 h-5 text-gray-400 mt-0.5" />
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">
                이메일
              </p>
              <p className="text-sm text-gray-900 dark:text-white">
                {user.email || 'Not set'}
              </p>
            </div>
          </div>

          {/* User ID */}
          <div className="flex items-start gap-3 p-4 bg-gray-50 dark:bg-gray-900/50 rounded-lg">
            <User className="w-5 h-5 text-gray-400 mt-0.5" />
            <div>
              <p className="text-sm font-medium text-gray-500 dark:text-gray-400">
                사용자 ID
              </p>
              <p className="text-sm text-gray-900 dark:text-white font-mono">
                {user.id}
              </p>
            </div>
          </div>
        </div>
      </div>

      {/* Permissions Card */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-6 mb-6">
        <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">
          권한
        </h3>
        {user.is_admin ? (
          <div className="p-3 bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-xl flex items-center gap-2">
            <Shield className="w-4 h-4 text-blue-600 dark:text-blue-400" />
            <span className="text-sm font-medium text-blue-700 dark:text-blue-300">전체 권한 (관리자)</span>
          </div>
        ) : user.permissions.length > 0 ? (
          <div className="space-y-4">
            {PERMISSION_GROUPS.map((group) => {
              const granted = group.permissions.filter(p => user.permissions.includes(p.value));
              const denied = group.permissions.filter(p => !user.permissions.includes(p.value));
              if (granted.length === 0 && denied.length === 0) return null;
              return (
                <div key={group.label}>
                  <p className="text-xs font-semibold text-gray-400 dark:text-gray-500 uppercase tracking-wider mb-2">{group.label}</p>
                  <div className="grid grid-cols-2 lg:grid-cols-3 gap-1.5">
                    {group.permissions.map((perm) => {
                      const has = user.permissions.includes(perm.value);
                      return (
                        <div
                          key={perm.value}
                          className={`flex items-center gap-2 px-2.5 py-2 border rounded-lg ${
                            has
                              ? 'border-green-200 dark:border-green-800 bg-green-50 dark:bg-green-900/20'
                              : 'border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-900/30 opacity-50'
                          }`}
                        >
                          {has ? (
                            <CheckCircle className="w-3.5 h-3.5 text-green-500 dark:text-green-400 flex-shrink-0" />
                          ) : (
                            <XCircle className="w-3.5 h-3.5 text-gray-400 dark:text-gray-500 flex-shrink-0" />
                          )}
                          <span className={`text-sm ${has ? 'text-gray-800 dark:text-gray-200' : 'text-gray-400 dark:text-gray-500'}`}>
                            {perm.label}
                          </span>
                        </div>
                      );
                    })}
                  </div>
                </div>
              );
            })}
          </div>
        ) : (
          <p className="text-sm text-gray-500 dark:text-gray-400">권한 없음</p>
        )}
      </div>

      {/* Change Password Card */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-6">
        <div className="flex items-center gap-3 mb-4">
          <Key className="w-5 h-5 text-gray-400" />
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
            비밀번호 변경
          </h3>
        </div>

        {!showPasswordForm ? (
          <div>
            <p className="text-sm text-gray-600 dark:text-gray-400 mb-4">
              계정 보안을 위해 주기적으로 비밀번호를 변경해주세요.
            </p>
            <button
              onClick={() => { setShowPasswordForm(true); setError(''); setSuccess(''); }}
              className="px-4 py-2 bg-gradient-to-r from-purple-500 to-violet-500 text-white rounded-lg hover:from-purple-600 hover:to-violet-600 transition-all shadow-md text-sm font-medium"
            >
              비밀번호 변경
            </button>
          </div>
        ) : (
          <div className="space-y-4">
            {error && (
              <div className="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-xl flex items-center gap-2">
                <AlertCircle className="w-4 h-4 text-red-600 dark:text-red-400 flex-shrink-0" />
                <span className="text-sm text-red-700 dark:text-red-300">{error}</span>
              </div>
            )}

            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                현재 비밀번호
              </label>
              <div className="relative">
                <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  type={showCurrentPw ? 'text' : 'password'}
                  value={passwordData.currentPassword}
                  onChange={(e) => setPasswordData({ ...passwordData, currentPassword: e.target.value })}
                  placeholder="현재 비밀번호 입력"
                  className="w-full pl-9 pr-10 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                />
                <button type="button" onClick={() => setShowCurrentPw(!showCurrentPw)} className="absolute right-3 top-1/2 -translate-y-1/2 text-gray-400 hover:text-gray-600">
                  {showCurrentPw ? <EyeOff className="w-4 h-4" /> : <Eye className="w-4 h-4" />}
                </button>
              </div>
            </div>

            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                새 비밀번호
              </label>
              <div className="relative">
                <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  type={showNewPw ? 'text' : 'password'}
                  value={passwordData.newPassword}
                  onChange={(e) => setPasswordData({ ...passwordData, newPassword: e.target.value })}
                  placeholder="8자 이상 입력"
                  className="w-full pl-9 pr-10 py-2.5 border border-gray-300 dark:border-gray-600 rounded-xl bg-white dark:bg-gray-700 text-gray-900 dark:text-white focus:outline-none focus:ring-2 focus:ring-purple-500 text-sm"
                />
                <button type="button" onClick={() => setShowNewPw(!showNewPw)} className="absolute right-3 top-1/2 -translate-y-1/2 text-gray-400 hover:text-gray-600">
                  {showNewPw ? <EyeOff className="w-4 h-4" /> : <Eye className="w-4 h-4" />}
                </button>
              </div>
            </div>

            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1.5">
                새 비밀번호 확인
              </label>
              <div className="relative">
                <Key className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
                <input
                  type="password"
                  value={passwordData.confirmPassword}
                  onChange={(e) => setPasswordData({ ...passwordData, confirmPassword: e.target.value })}
                  placeholder="새 비밀번호 다시 입력"
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

            <div className="flex justify-end gap-2 pt-2">
              <button
                onClick={() => { setShowPasswordForm(false); resetPasswordForm(); }}
                className="px-4 py-2 text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 rounded-xl transition-colors text-sm"
              >
                취소
              </button>
              <button
                onClick={handleChangePassword}
                disabled={loading || !passwordData.currentPassword || !passwordData.newPassword || passwordData.newPassword !== passwordData.confirmPassword}
                className="px-5 py-2 bg-gradient-to-r from-purple-500 to-violet-500 text-white rounded-xl hover:from-purple-600 hover:to-violet-600 transition-all shadow-md disabled:opacity-50 disabled:cursor-not-allowed text-sm font-medium"
              >
                {loading ? '변경 중...' : '비밀번호 변경'}
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

export default Profile;
