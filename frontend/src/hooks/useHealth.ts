import { useQuery } from '@tanstack/react-query';
import { healthApi } from '@/services/api';

// Query keys
export const healthKeys = {
  all: ['health'] as const,
  status: () => [...healthKeys.all, 'status'] as const,
  database: () => [...healthKeys.all, 'database'] as const,
  ldap: () => [...healthKeys.all, 'ldap'] as const,
};

// Fetch overall health status
export function useHealthStatus() {
  return useQuery({
    queryKey: healthKeys.status(),
    queryFn: async () => {
      const response = await healthApi.check();
      return response.data;
    },
    refetchInterval: 30000, // Refetch every 30 seconds
    staleTime: 10000, // Consider fresh for 10 seconds
  });
}

// Fetch database health
export function useDatabaseHealth() {
  return useQuery({
    queryKey: healthKeys.database(),
    queryFn: async () => {
      const response = await healthApi.checkDatabase();
      return response.data;
    },
    refetchInterval: 30000,
    staleTime: 10000,
  });
}

// Fetch LDAP health
export function useLdapHealth() {
  return useQuery({
    queryKey: healthKeys.ldap(),
    queryFn: async () => {
      const response = await healthApi.checkLdap();
      return response.data;
    },
    refetchInterval: 30000,
    staleTime: 10000,
  });
}
