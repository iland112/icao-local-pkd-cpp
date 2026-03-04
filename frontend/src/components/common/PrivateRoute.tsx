import { Navigate } from 'react-router-dom';
import { authApi } from '@/services/api';

interface PrivateRouteProps {
  children: React.ReactNode;
}

/**
 * Check if a JWT token is expired by decoding its payload.
 * Returns true if the token is expired or cannot be decoded.
 */
function isTokenExpired(token: string): boolean {
  try {
    const parts = token.split('.');
    if (parts.length !== 3) return true;

    // Base64url decode the payload (second part)
    const payload = parts[1]
      .replace(/-/g, '+')
      .replace(/_/g, '/');
    const decoded = JSON.parse(atob(payload));

    if (typeof decoded.exp !== 'number') return false; // No exp claim — treat as valid
    // exp is in seconds, Date.now() is in milliseconds
    return decoded.exp * 1000 < Date.now();
  } catch {
    return true; // If decoding fails, treat as expired
  }
}

/**
 * PrivateRoute Component
 *
 * Protects routes that require authentication.
 * Checks both token presence and expiration.
 * Redirects to /login if user is not authenticated or token is expired.
 *
 * Usage:
 *   <Route path="/protected" element={<PrivateRoute><Component /></PrivateRoute>} />
 */
export function PrivateRoute({ children }: PrivateRouteProps) {
  const isAuthenticated = authApi.isAuthenticated();

  if (!isAuthenticated) {
    return <Navigate to="/login" replace />;
  }

  // Check JWT token expiration
  const token = localStorage.getItem('access_token');
  if (token && isTokenExpired(token)) {
    localStorage.removeItem('access_token');
    localStorage.removeItem('user');
    return <Navigate to="/login" replace />;
  }

  return <>{children}</>;
}
