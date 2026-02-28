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
    mutationFn: async ({ file }: { file: File }) => {
      const response = await uploadApi.uploadLdif(file);
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
    mutationFn: async ({ file }: { file: File }) => {
      const response = await uploadApi.uploadMasterList(file);
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
