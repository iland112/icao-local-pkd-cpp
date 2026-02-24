import { authApi } from '@/services/api';
import { User, Mail, Shield, Key } from 'lucide-react';

export function Profile() {
  const user = authApi.getStoredUser();

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
        <div className="flex flex-wrap gap-2">
          {user.permissions.map((permission) => (
            <span
              key={permission}
              className="px-3 py-1 bg-blue-50 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300 text-sm font-medium rounded-full"
            >
              {permission}
            </span>
          ))}
        </div>
      </div>

      {/* Change Password Card */}
      <div className="bg-white dark:bg-gray-800 rounded-2xl shadow-lg border border-gray-200 dark:border-gray-700 p-6">
        <div className="flex items-center gap-3 mb-4">
          <Key className="w-5 h-5 text-gray-400" />
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">
            비밀번호 변경
          </h3>
        </div>
        <p className="text-sm text-gray-600 dark:text-gray-400 mb-4">
          비밀번호 변경 기능은 Phase 4에서 구현될 예정입니다.
        </p>
        <button
          disabled
          className="px-4 py-2 bg-gray-100 dark:bg-gray-700 text-gray-400 dark:text-gray-500 rounded-lg cursor-not-allowed"
        >
          비밀번호 변경
        </button>
      </div>
    </div>
  );
}

export default Profile;
