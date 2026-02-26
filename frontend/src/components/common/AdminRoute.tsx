import { Navigate } from 'react-router-dom';
import { authApi } from '@/services/api';

interface AdminRouteProps {
  children: React.ReactNode;
}

/**
 * AdminRoute Component
 *
 * Protects routes that require admin privileges.
 * Redirects to dashboard if user is not admin.
 * Must be used inside PrivateRoute (authentication already checked).
 */
export function AdminRoute({ children }: AdminRouteProps) {
  if (!authApi.isAdmin()) {
    return <Navigate to="/" replace />;
  }

  return <>{children}</>;
}
