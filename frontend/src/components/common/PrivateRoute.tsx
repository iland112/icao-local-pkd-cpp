import { Navigate } from 'react-router-dom';
import { authApi } from '@/services/api';

interface PrivateRouteProps {
  children: React.ReactNode;
}

/**
 * PrivateRoute Component
 *
 * Protects routes that require authentication.
 * Redirects to /login if user is not authenticated.
 *
 * Usage:
 *   <Route path="/protected" element={<PrivateRoute><Component /></PrivateRoute>} />
 */
export function PrivateRoute({ children }: PrivateRouteProps) {
  const isAuthenticated = authApi.isAuthenticated();

  if (!isAuthenticated) {
    // Redirect to login page
    return <Navigate to="/login" replace />;
  }

  return <>{children}</>;
}
