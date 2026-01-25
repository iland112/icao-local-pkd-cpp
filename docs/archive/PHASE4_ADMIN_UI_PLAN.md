# Phase 4: Admin UI & User Management Implementation Plan

**Version**: 2.0.0 (Phase 4)
**Created**: 2026-01-22
**Status**: 📋 Planning

---

## Executive Summary

Phase 3에서 JWT 인증 시스템이 완성되었으나, 사용자 관리 및 권한 관리 UI가 누락되었습니다. Phase 4에서는 Admin 전용 UI를 구축하여 완전한 인증 시스템을 완성합니다.

---

## Current Implementation Status (Phase 3)

### ✅ Backend Complete
- [x] JWT Service (token generation, validation, refresh)
- [x] Password Hashing (PBKDF2-HMAC-SHA256)
- [x] Authentication Endpoints (/login, /logout, /refresh, /me)
- [x] Database Schema (users, auth_audit_log tables)
- [x] Permission Filter (RBAC)
- [x] Auth Middleware
- [x] API Gateway Integration

### ✅ Frontend Complete
- [x] Login Page
- [x] PrivateRoute Guard
- [x] Auth API Client (token injection, auto-refresh)
- [x] Token Storage (localStorage)

### ❌ Frontend Missing (Phase 4 Scope)
- [ ] Logout UI (Header dropdown)
- [ ] User Management (Admin only)
  - [ ] User List
  - [ ] Create User
  - [ ] Edit User
  - [ ] Delete User
  - [ ] Password Reset
- [ ] Permission Management (Admin only)
  - [ ] View/Edit Permissions per User
  - [ ] Role-based Templates
- [ ] Authentication Audit Log (Admin only)
  - [ ] Login History
  - [ ] Failed Login Attempts
  - [ ] User Activity Tracking
- [ ] User Profile Page (Self-service)
  - [ ] View Profile
  - [ ] Change Password

---

## Phase 4 Implementation Plan

### Task 1: Logout UI Implementation (Day 1)

**Priority**: HIGH
**Estimated Time**: 2-3 hours

#### 1.1 Header Component Update
**File**: `frontend/src/components/layout/Header.tsx`

**Changes**:
- Replace hardcoded "Admin" with actual username from localStorage
- Implement logout handler using `authApi.logout()`
- Redirect to /login after logout
- Show user permissions badge (admin/user)

**Implementation**:
```tsx
// frontend/src/components/layout/Header.tsx
import { authApi } from '@/services/api';
import { useNavigate } from 'react-router-dom';

export function Header() {
  const navigate = useNavigate();
  const user = authApi.getStoredUser();

  const handleLogout = async () => {
    try {
      await authApi.logout();
      navigate('/login');
    } catch (error) {
      console.error('Logout failed:', error);
      // Force logout even if API fails
      localStorage.clear();
      navigate('/login');
    }
  };

  return (
    <header>
      {/* ... */}
      <div className="hs-dropdown relative inline-flex">
        <button>
          <span>{user?.username || 'User'}</span>
          {user?.is_admin && (
            <span className="badge">Admin</span>
          )}
        </button>

        <div className="hs-dropdown-menu">
          <a href="/profile">프로필</a>
          <a href="/admin/users">사용자 관리 (Admin)</a>
          <a href="/admin/audit-log">로그인 이력 (Admin)</a>
          <hr />
          <button onClick={handleLogout}>로그아웃</button>
        </div>
      </div>
    </header>
  );
}
```

#### 1.2 Testing
- [ ] Logout 버튼 클릭 → /login 리다이렉트
- [ ] localStorage 토큰 삭제 확인
- [ ] Backend `/api/auth/logout` 호출 확인
- [ ] auth_audit_log 테이블 LOGOUT 이벤트 기록 확인

---

### Task 2: Backend - User Management API (Day 2)

**Priority**: HIGH
**Estimated Time**: 4-5 hours

#### 2.1 User Management Endpoints

**File**: `services/pkd-management/src/handlers/user_handler.h/cpp`

**New Endpoints**:
```cpp
// GET /api/users - List all users (Admin only)
void handleGetUsers(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

// GET /api/users/{userId} - Get user details (Admin only)
void handleGetUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId);

// POST /api/users - Create new user (Admin only)
void handleCreateUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

// PUT /api/users/{userId} - Update user (Admin only)
void handleUpdateUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId);

// DELETE /api/users/{userId} - Delete user (Admin only)
void handleDeleteUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId);

// PUT /api/users/{userId}/password - Change user password (Admin or Self)
void handleChangePassword(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId);
```

**Permission Requirements**:
- All endpoints require `admin` permission
- `/password` endpoint: Admin OR self (userId matches token userId)

#### 2.2 Request/Response Schemas

**CreateUserRequest**:
```json
{
  "username": "john.doe",
  "password": "SecurePass123!",
  "email": "john@example.com",
  "full_name": "John Doe",
  "permissions": ["upload:read", "cert:read"],
  "is_admin": false
}
```

**UpdateUserRequest**:
```json
{
  "email": "newemail@example.com",
  "full_name": "John Doe Updated",
  "permissions": ["upload:read", "upload:write", "cert:read"],
  "is_active": true,
  "is_admin": false
}
```

**ChangePasswordRequest**:
```json
{
  "current_password": "OldPass123!",  // Required for self-service
  "new_password": "NewPass456!"
}
```

**UserResponse**:
```json
{
  "id": "uuid",
  "username": "john.doe",
  "email": "john@example.com",
  "full_name": "John Doe",
  "permissions": ["upload:read", "cert:read"],
  "is_admin": false,
  "is_active": true,
  "created_at": "2026-01-22T10:00:00Z",
  "last_login_at": "2026-01-22T12:00:00Z"
}
```

#### 2.3 Validation Rules
- Username: 3-50 chars, alphanumeric + underscore/dash
- Password: Min 8 chars, must contain uppercase, lowercase, number
- Email: Valid email format
- Permissions: Must be from predefined list (admin, upload:read, upload:write, cert:read, cert:export, pa:verify, sync:read, sync:write)

---

### Task 3: Frontend - User Management UI (Day 3-4)

**Priority**: HIGH
**Estimated Time**: 8-10 hours

#### 3.1 User List Page

**File**: `frontend/src/pages/admin/UserManagement.tsx`

**Features**:
- Table view with columns: Username, Email, Full Name, Permissions, Admin, Active, Last Login, Actions
- Search/Filter by username, email, status (active/inactive)
- Pagination (10/25/50 per page)
- Sort by: Username, Created At, Last Login
- Actions: Edit, Delete, Reset Password
- Create User button (opens modal)

**UI Components**:
```tsx
export function UserManagement() {
  const [users, setUsers] = useState<User[]>([]);
  const [loading, setLoading] = useState(true);
  const [searchTerm, setSearchTerm] = useState('');
  const [statusFilter, setStatusFilter] = useState<'all' | 'active' | 'inactive'>('all');

  // Fetch users from API
  useEffect(() => {
    fetchUsers();
  }, []);

  return (
    <div className="p-6">
      {/* Header */}
      <div className="flex justify-between items-center mb-6">
        <h1>사용자 관리</h1>
        <button onClick={() => setShowCreateModal(true)}>
          사용자 추가
        </button>
      </div>

      {/* Filters */}
      <div className="flex gap-4 mb-4">
        <input
          type="text"
          placeholder="검색 (사용자명, 이메일)"
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
        />
        <select
          value={statusFilter}
          onChange={(e) => setStatusFilter(e.target.value)}
        >
          <option value="all">전체</option>
          <option value="active">활성</option>
          <option value="inactive">비활성</option>
        </select>
      </div>

      {/* User Table */}
      <UserTable
        users={filteredUsers}
        onEdit={handleEdit}
        onDelete={handleDelete}
        onResetPassword={handleResetPassword}
      />

      {/* Create/Edit User Modal */}
      {showCreateModal && (
        <UserFormModal
          onClose={() => setShowCreateModal(false)}
          onSave={handleCreateUser}
        />
      )}
    </div>
  );
}
```

#### 3.2 User Form Modal (Create/Edit)

**File**: `frontend/src/components/admin/UserFormModal.tsx`

**Fields**:
- Username (required, disabled on edit)
- Password (required on create, optional on edit)
- Email (optional)
- Full Name (optional)
- Permissions (multi-select checkboxes)
- Is Admin (toggle)
- Is Active (toggle)

**Validation**:
- Frontend validation before API call
- Show inline errors
- Disable submit until valid

#### 3.3 User API Client

**File**: `frontend/src/services/userApi.ts`

```typescript
export const userApi = {
  // List all users (admin only)
  getUsers: async (params?: { search?: string; status?: string }) => {
    const response = await authenticatedClient.get('/users', { params });
    return response.data;
  },

  // Get user by ID
  getUser: async (userId: string) => {
    const response = await authenticatedClient.get(`/users/${userId}`);
    return response.data;
  },

  // Create user
  createUser: async (userData: CreateUserRequest) => {
    const response = await authenticatedClient.post('/users', userData);
    return response.data;
  },

  // Update user
  updateUser: async (userId: string, userData: UpdateUserRequest) => {
    const response = await authenticatedClient.put(`/users/${userId}`, userData);
    return response.data;
  },

  // Delete user
  deleteUser: async (userId: string) => {
    const response = await authenticatedClient.delete(`/users/${userId}`);
    return response.data;
  },

  // Change password
  changePassword: async (userId: string, passwords: ChangePasswordRequest) => {
    const response = await authenticatedClient.put(`/users/${userId}/password`, passwords);
    return response.data;
  },
};
```

---

### Task 4: Backend - Audit Log API (Day 5)

**Priority**: MEDIUM
**Estimated Time**: 3-4 hours

#### 4.1 Audit Log Endpoints

**File**: `services/pkd-management/src/handlers/audit_handler.h/cpp`

**New Endpoints**:
```cpp
// GET /api/audit/login-history - Get login history (Admin only)
void handleGetLoginHistory(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback);

// GET /api/audit/users/{userId}/activity - Get user activity (Admin only)
void handleGetUserActivity(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId);
```

**Query Parameters**:
- `user_id`: Filter by user ID
- `event_type`: LOGIN_SUCCESS, LOGIN_FAILED, LOGOUT, TOKEN_REFRESH
- `start_date`: Filter from date (ISO 8601)
- `end_date`: Filter to date (ISO 8601)
- `limit`: Pagination limit (default 50)
- `offset`: Pagination offset (default 0)

**Response Schema**:
```json
{
  "success": true,
  "total": 150,
  "logs": [
    {
      "id": "uuid",
      "user_id": "uuid",
      "username": "admin",
      "event_type": "LOGIN_SUCCESS",
      "ip_address": "192.168.1.100",
      "user_agent": "Mozilla/5.0...",
      "success": true,
      "error_message": null,
      "created_at": "2026-01-22T10:00:00Z"
    }
  ]
}
```

---

### Task 5: Frontend - Audit Log UI (Day 6)

**Priority**: MEDIUM
**Estimated Time**: 4-5 hours

#### 5.1 Login History Page

**File**: `frontend/src/pages/admin/LoginHistory.tsx`

**Features**:
- Table view with columns: Timestamp, Username, Event Type, IP Address, User Agent, Status
- Filters: Date range, Event type, Username, Success/Failed
- Export to CSV
- Color-coded status (green=success, red=failed)
- Detailed view modal (show full user agent, error message)

**Statistics Cards**:
- Total Logins (24h)
- Failed Attempts (24h)
- Unique Users (24h)
- Most Active User

---

### Task 6: Frontend - User Profile Page (Day 7)

**Priority**: MEDIUM
**Estimated Time**: 3-4 hours

#### 6.1 Profile Page (Self-Service)

**File**: `frontend/src/pages/Profile.tsx`

**Features**:
- View user info (username, email, full name, permissions)
- Change password form
- View own login history (last 10 logins)
- Session info (current token expiration)

**Change Password Flow**:
1. Enter current password
2. Enter new password (with strength indicator)
3. Confirm new password
4. Submit → API call → Show success message → Auto-logout after 3 seconds

---

### Task 7: Route Integration & Testing (Day 8)

#### 7.1 Add Routes to App.tsx

```tsx
// App.tsx
<Route path="/" element={<PrivateRoute><Layout /></PrivateRoute>}>
  {/* Existing routes */}
  <Route path="profile" element={<Profile />} />

  {/* Admin-only routes */}
  <Route path="admin/users" element={<AdminRoute><UserManagement /></AdminRoute>} />
  <Route path="admin/audit-log" element={<AdminRoute><LoginHistory /></AdminRoute>} />
</Route>
```

#### 7.2 Create AdminRoute Component

**File**: `frontend/src/components/common/AdminRoute.tsx`

```tsx
export function AdminRoute({ children }: { children: React.ReactNode }) {
  const isAdmin = authApi.isAdmin();

  if (!isAdmin) {
    return <Navigate to="/" replace />;
  }

  return <>{children}</>;
}
```

#### 7.3 Update Sidebar

**File**: `frontend/src/components/layout/Sidebar.tsx`

**Add Admin Section**:
```tsx
{authApi.isAdmin() && (
  <>
    <div className="px-3 py-2 text-xs font-semibold text-gray-400">
      관리
    </div>
    <NavItem to="/admin/users" icon={Users}>
      사용자 관리
    </NavItem>
    <NavItem to="/admin/audit-log" icon={FileText}>
      로그인 이력
    </NavItem>
  </>
)}
```

---

## Testing Strategy

### Unit Tests
- [ ] Backend: User CRUD operations
- [ ] Backend: Password validation
- [ ] Backend: Permission checks
- [ ] Frontend: User form validation

### Integration Tests
- [ ] Create user → Login → Verify permissions
- [ ] Admin creates user → New user logs in → Admin views audit log
- [ ] User changes password → Re-login with new password
- [ ] Delete user → Verify login fails

### E2E Tests (Manual)
1. **User Management Flow**:
   - Admin logs in
   - Creates new user with limited permissions
   - New user logs in
   - Tries to access admin page (403 Forbidden)
   - Admin edits user permissions
   - User refreshes and can access new endpoints

2. **Audit Trail Flow**:
   - Multiple users log in/out
   - Failed login attempts
   - Admin views audit log
   - Filter by date, user, event type
   - Export audit log to CSV

3. **Password Management Flow**:
   - User changes password
   - Old password fails
   - New password succeeds
   - Admin resets user password
   - User forced to change password on next login

---

## Database Schema Changes (Optional)

### Password Reset Tokens (Future)
```sql
CREATE TABLE password_reset_tokens (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    token VARCHAR(255) UNIQUE NOT NULL,
    expires_at TIMESTAMP NOT NULL,
    used BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_password_reset_token ON password_reset_tokens(token);
CREATE INDEX idx_password_reset_user ON password_reset_tokens(user_id);
```

### User Sessions (Optional)
```sql
CREATE TABLE user_sessions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    token_hash VARCHAR(255) NOT NULL,  -- SHA256 of JWT
    ip_address VARCHAR(45),
    user_agent TEXT,
    expires_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_user_sessions_user ON user_sessions(user_id);
CREATE INDEX idx_user_sessions_token ON user_sessions(token_hash);
```

---

## Security Considerations

### Input Validation
- ✅ Username: No SQL injection characters
- ✅ Password: Strong password policy
- ✅ Email: Valid email format
- ✅ Permissions: Whitelist only

### Access Control
- ✅ Admin-only endpoints protected by PermissionFilter
- ✅ Self-service password change: Verify current password
- ✅ User cannot elevate own permissions
- ✅ Prevent admin from deleting self

### Audit Trail
- ✅ Log all user management operations
- ✅ Log all password changes
- ✅ Log failed login attempts (rate limiting protection)
- ✅ Immutable audit log (INSERT only, no DELETE)

### Password Security
- ✅ PBKDF2-HMAC-SHA256 (310,000 iterations)
- ✅ Password strength requirements
- ✅ Password history (prevent reuse, future)
- ✅ Force password change after admin reset (future)

---

## Timeline Summary

| Task | Days | Complexity | Priority |
|------|------|------------|----------|
| 1. Logout UI | 0.5 | Low | HIGH |
| 2. User Management API | 1 | Medium | HIGH |
| 3. User Management UI | 2 | High | HIGH |
| 4. Audit Log API | 0.5 | Low | MEDIUM |
| 5. Audit Log UI | 1 | Medium | MEDIUM |
| 6. User Profile Page | 0.5 | Low | MEDIUM |
| 7. Route Integration | 0.5 | Low | HIGH |
| 8. Testing | 1 | Medium | HIGH |
| **Total** | **7 days** | | |

**Critical Path**: Tasks 1-3-7 (Logout + User Management + Integration)

---

## Success Criteria

### Functional Requirements
- ✅ Admin can create/edit/delete users
- ✅ Admin can view login history and audit logs
- ✅ Users can change their own password
- ✅ Users can view their own profile
- ✅ Logout button works and clears session
- ✅ Permission-based UI rendering (show/hide admin sections)

### Non-Functional Requirements
- ✅ Responsive UI (mobile-friendly)
- ✅ Accessible (WCAG 2.1 AA)
- ✅ Fast response times (<500ms for user list)
- ✅ Secure (no XSS, CSRF, SQL injection)

---

## Future Enhancements (Phase 5)

- [ ] Role Templates (predefined permission sets)
- [ ] User Groups/Teams
- [ ] 2FA/MFA Support
- [ ] OAuth/SSO Integration
- [ ] Email Notifications (password reset, account creation)
- [ ] Password Expiration Policy
- [ ] Session Management (view active sessions, force logout)
- [ ] Advanced Audit Log Analytics (charts, trends)
- [ ] User Activity Dashboard

---

## References

- Phase 3 Implementation: Complete (JWT, Auth API, Login Page)
- ICAO Doc 9303: Security Standards
- OWASP Top 10: Security Best Practices
- Drogon Framework Docs: Filter/Middleware Usage
- React Best Practices: Component Architecture

---

**Document Version**: 1.0
**Last Updated**: 2026-01-22
**Author**: Claude (Phase 3 Continuation)
