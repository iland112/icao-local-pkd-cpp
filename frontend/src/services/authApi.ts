/**
 * Authentication API Module
 *
 * Handles JWT authentication operations:
 * - Login
 * - Logout
 * - Token refresh
 * - Current user info
 *
 * @version 2.0.0 (Phase 3 - Authentication)
 */

import axios, { type AxiosError, type AxiosResponse, type InternalAxiosRequestConfig } from 'axios';

// =============================================================================
// Types
// =============================================================================

export interface LoginRequest {
  username: string;
  password: string;
}

export interface LoginResponse {
  success: boolean;
  access_token?: string;
  token_type?: string;
  expires_in?: number;
  user?: UserInfo;
  error?: string;
  message?: string;
}

export interface UserInfo {
  id: string;
  username: string;
  email?: string;
  full_name?: string;
  permissions: string[];
  is_admin: boolean;
}

export interface RefreshTokenRequest {
  token: string;
}

export interface RefreshTokenResponse {
  success: boolean;
  access_token?: string;
  token_type?: string;
  expires_in?: number;
  error?: string;
  message?: string;
}

export interface CurrentUserResponse {
  success: boolean;
  user?: UserInfo;
  error?: string;
  message?: string;
}

export interface LogoutResponse {
  success: boolean;
  message?: string;
}

// =============================================================================
// Axios Instance with Token Injection
// =============================================================================

const authApiClient = axios.create({
  baseURL: '/api/auth',
  timeout: 10000,
  headers: {
    'Content-Type': 'application/json',
  },
});

/**
 * Request Interceptor: Inject JWT token from localStorage
 */
authApiClient.interceptors.request.use(
  (config: InternalAxiosRequestConfig) => {
    const token = localStorage.getItem('access_token');

    if (token && config.headers) {
      config.headers.Authorization = `Bearer ${token}`;
    }

    return config;
  },
  (error) => {
    return Promise.reject(error);
  }
);

/**
 * Response Interceptor: Handle 401 Unauthorized errors
 */
authApiClient.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (error.response?.status === 401) {
      // Token expired or invalid - redirect to login
      const currentPath = window.location.pathname;

      // Only redirect if not already on login page
      if (currentPath !== '/login') {
        // Clear stored auth data
        localStorage.removeItem('access_token');
        localStorage.removeItem('user');

        // Redirect to login
        window.location.href = '/login';
      }
    }

    console.error('[Auth API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// =============================================================================
// API Client for All Services (with Token Injection)
// =============================================================================

/**
 * Create authenticated axios client for non-auth APIs
 * Automatically injects JWT token from localStorage
 */
export const createAuthenticatedClient = (baseURL: string = '/api') => {
  const client = axios.create({
    baseURL,
    timeout: 30000,
    headers: {
      'Content-Type': 'application/json',
    },
  });

  // Request interceptor: Inject token
  client.interceptors.request.use(
    (config: InternalAxiosRequestConfig) => {
      const token = localStorage.getItem('access_token');

      if (token && config.headers) {
        config.headers.Authorization = `Bearer ${token}`;
      }

      return config;
    },
    (error) => Promise.reject(error)
  );

  // Response interceptor: Handle 401 errors
  client.interceptors.response.use(
    (response: AxiosResponse) => response,
    (error: AxiosError) => {
      if (error.response?.status === 401) {
        const currentPath = window.location.pathname;
        if (currentPath !== '/login') {
          localStorage.removeItem('access_token');
          localStorage.removeItem('user');
          window.location.href = '/login';
        }
      }
      return Promise.reject(error);
    }
  );

  return client;
};

// =============================================================================
// Authentication API Methods
// =============================================================================

export const authApi = {
  /**
   * Login with username and password
   */
  login: async (username: string, password: string): Promise<LoginResponse> => {
    const response = await authApiClient.post<LoginResponse>('/login', {
      username,
      password,
    });
    return response.data;
  },

  /**
   * Logout current user
   */
  logout: async (): Promise<LogoutResponse> => {
    const response = await authApiClient.post<LogoutResponse>('/logout');

    // Clear local storage
    localStorage.removeItem('access_token');
    localStorage.removeItem('user');

    return response.data;
  },

  /**
   * Refresh JWT token
   */
  refreshToken: async (): Promise<RefreshTokenResponse> => {
    const token = localStorage.getItem('access_token');

    if (!token) {
      throw new Error('No token found');
    }

    const response = await authApiClient.post<RefreshTokenResponse>('/refresh', {
      token,
    });

    // Update token in localStorage
    if (response.data.success && response.data.access_token) {
      localStorage.setItem('access_token', response.data.access_token);
    }

    return response.data;
  },

  /**
   * Get current user info
   */
  getCurrentUser: async (): Promise<CurrentUserResponse> => {
    const response = await authApiClient.get<CurrentUserResponse>('/me');
    return response.data;
  },

  /**
   * Check if user is authenticated
   */
  isAuthenticated: (): boolean => {
    const token = localStorage.getItem('access_token');
    return !!token;
  },

  /**
   * Get stored user info
   */
  getStoredUser: (): UserInfo | null => {
    const userStr = localStorage.getItem('user');
    if (!userStr) return null;

    try {
      return JSON.parse(userStr) as UserInfo;
    } catch {
      return null;
    }
  },

  /**
   * Check if user has specific permission
   */
  hasPermission: (permission: string): boolean => {
    const user = authApi.getStoredUser();
    if (!user) return false;
    if (user.is_admin) return true;

    return user.permissions.includes(permission) || user.permissions.includes('admin');
  },

  /**
   * Check if user is admin
   */
  isAdmin: (): boolean => {
    const user = authApi.getStoredUser();
    return user?.is_admin || false;
  },
};

export default authApi;
