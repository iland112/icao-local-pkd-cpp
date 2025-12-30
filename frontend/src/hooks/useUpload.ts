import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { uploadApi } from '@/services/api';
import type { PageRequest } from '@/types';
import { toast } from '@/stores/toastStore';

// Query keys
export const uploadKeys = {
  all: ['uploads'] as const,
  lists: () => [...uploadKeys.all, 'list'] as const,
  list: (params: PageRequest) => [...uploadKeys.lists(), params] as const,
  details: () => [...uploadKeys.all, 'detail'] as const,
  detail: (id: string) => [...uploadKeys.details(), id] as const,
  statistics: () => [...uploadKeys.all, 'statistics'] as const,
};

// Fetch upload history
export function useUploadHistory(params: PageRequest) {
  return useQuery({
    queryKey: uploadKeys.list(params),
    queryFn: async () => {
      const response = await uploadApi.getHistory(params);
      return response.data;
    },
  });
}

// Fetch upload detail
export function useUploadDetail(uploadId: string | undefined) {
  return useQuery({
    queryKey: uploadKeys.detail(uploadId ?? ''),
    queryFn: async () => {
      if (!uploadId) throw new Error('Upload ID is required');
      const response = await uploadApi.getDetail(uploadId);
      return response.data;
    },
    enabled: !!uploadId,
  });
}

// Fetch upload statistics
export function useUploadStatistics() {
  return useQuery({
    queryKey: uploadKeys.statistics(),
    queryFn: async () => {
      const response = await uploadApi.getStatistics();
      return response.data;
    },
  });
}

// Upload LDIF file mutation
export function useUploadLdif() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: async ({ file, processingMode }: { file: File; processingMode: string }) => {
      const response = await uploadApi.uploadLdif(file, processingMode);
      return response.data;
    },
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: uploadKeys.lists() });
      queryClient.invalidateQueries({ queryKey: uploadKeys.statistics() });
      if (data.success) {
        toast.success('파일 업로드 성공', 'LDIF 파일이 성공적으로 업로드되었습니다.');
      }
    },
    onError: (error) => {
      toast.error('파일 업로드 실패', error instanceof Error ? error.message : '알 수 없는 오류');
    },
  });
}

// Upload Master List file mutation
export function useUploadMasterList() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: async ({ file, processingMode }: { file: File; processingMode: string }) => {
      const response = await uploadApi.uploadMasterList(file, processingMode);
      return response.data;
    },
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: uploadKeys.lists() });
      queryClient.invalidateQueries({ queryKey: uploadKeys.statistics() });
      if (data.success) {
        toast.success('파일 업로드 성공', 'Master List 파일이 성공적으로 업로드되었습니다.');
      }
    },
    onError: (error) => {
      toast.error('파일 업로드 실패', error instanceof Error ? error.message : '알 수 없는 오류');
    },
  });
}

// Trigger parse mutation
export function useTriggerParse() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: async (uploadId: string) => {
      const response = await uploadApi.triggerParse(uploadId);
      return response.data;
    },
    onSuccess: (_, uploadId) => {
      queryClient.invalidateQueries({ queryKey: uploadKeys.detail(uploadId) });
      toast.info('파싱 시작', '파일 파싱이 시작되었습니다.');
    },
    onError: (error) => {
      toast.error('파싱 실패', error instanceof Error ? error.message : '알 수 없는 오류');
    },
  });
}

// Trigger validate mutation
export function useTriggerValidate() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: async (uploadId: string) => {
      const response = await uploadApi.triggerValidate(uploadId);
      return response.data;
    },
    onSuccess: (_, uploadId) => {
      queryClient.invalidateQueries({ queryKey: uploadKeys.detail(uploadId) });
      toast.info('검증 시작', '인증서 검증이 시작되었습니다.');
    },
    onError: (error) => {
      toast.error('검증 실패', error instanceof Error ? error.message : '알 수 없는 오류');
    },
  });
}

// Trigger LDAP upload mutation
export function useTriggerLdapUpload() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: async (uploadId: string) => {
      const response = await uploadApi.triggerLdapUpload(uploadId);
      return response.data;
    },
    onSuccess: (_, uploadId) => {
      queryClient.invalidateQueries({ queryKey: uploadKeys.detail(uploadId) });
      toast.info('LDAP 저장 시작', 'LDAP 서버로 데이터 저장이 시작되었습니다.');
    },
    onError: (error) => {
      toast.error('LDAP 저장 실패', error instanceof Error ? error.message : '알 수 없는 오류');
    },
  });
}
