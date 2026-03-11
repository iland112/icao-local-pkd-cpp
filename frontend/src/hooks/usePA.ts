import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useTranslation } from 'react-i18next';
import { paApi } from '@/services/api';
import type { PageRequest, PAVerificationRequest } from '@/types';
import { toast } from '@/stores/toastStore';

// Query keys
export const paKeys = {
  all: ['pa'] as const,
  lists: () => [...paKeys.all, 'list'] as const,
  list: (params: PageRequest) => [...paKeys.lists(), params] as const,
  details: () => [...paKeys.all, 'detail'] as const,
  detail: (id: string) => [...paKeys.details(), id] as const,
  statistics: () => [...paKeys.all, 'statistics'] as const,
};

// Fetch PA history
export function usePAHistory(params: PageRequest) {
  return useQuery({
    queryKey: paKeys.list(params),
    queryFn: async () => {
      const response = await paApi.getHistory(params);
      return response.data;
    },
  });
}

// Fetch PA detail
export function usePADetail(paId: string | undefined) {
  return useQuery({
    queryKey: paKeys.detail(paId ?? ''),
    queryFn: async () => {
      if (!paId) throw new Error('PA ID is required');
      const response = await paApi.getDetail(paId);
      return response.data;
    },
    enabled: !!paId,
  });
}

// Fetch PA statistics
export function usePAStatistics() {
  return useQuery({
    queryKey: paKeys.statistics(),
    queryFn: async () => {
      const response = await paApi.getStatistics();
      return response.data;
    },
  });
}

// Perform PA verification mutation
export function useVerifyPA() {
  const queryClient = useQueryClient();
  const { t } = useTranslation(['pa', 'common']);

  return useMutation({
    mutationFn: async (request: PAVerificationRequest) => {
      const response = await paApi.verify(request);
      return response.data;
    },
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: paKeys.lists() });
      queryClient.invalidateQueries({ queryKey: paKeys.statistics() });
      if (data.success && data.data) {
        if (data.data.status === 'VALID') {
          toast.success(t('pa:hook.verifySuccess'), t('pa:hook.verifySuccessDesc'));
        } else {
          toast.warning(t('pa:hook.verifyFailed'), t('pa:hook.verifyFailedDesc'));
        }
      }
    },
    onError: (error) => {
      toast.error(t('pa:hook.verifyError'), error instanceof Error ? error.message : t('common:error.unknownError_short'));
    },
  });
}

// Parse DG1 mutation
export function useParseDG1() {
  const { t } = useTranslation(['pa', 'common']);

  return useMutation({
    mutationFn: async (data: string) => {
      const response = await paApi.parseDG1(data);
      return response.data;
    },
    onError: (error) => {
      toast.error(t('pa:hook.dg1ParseFailed'), error instanceof Error ? error.message : t('common:error.unknownError_short'));
    },
  });
}

// Parse DG2 mutation
export function useParseDG2() {
  const { t } = useTranslation(['pa', 'common']);

  return useMutation({
    mutationFn: async (data: string) => {
      const response = await paApi.parseDG2(data);
      return response.data;
    },
    onError: (error) => {
      toast.error(t('pa:hook.dg2ParseFailed'), error instanceof Error ? error.message : t('common:error.unknownError_short'));
    },
  });
}
