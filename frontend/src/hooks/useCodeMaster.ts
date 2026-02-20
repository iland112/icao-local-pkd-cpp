import { useQuery } from '@tanstack/react-query';
import { codeMasterApi, type CodeMasterItem } from '@/services/codeMasterApi';

export const codeMasterKeys = {
  all: ['codeMaster'] as const,
  category: (category: string) => [...codeMasterKeys.all, category] as const,
  categories: () => [...codeMasterKeys.all, 'categories'] as const,
};

/**
 * Fetch codes by category with 10-minute cache
 *
 * @example
 * const { codes, getLabel } = useCodeMaster('VALIDATION_STATUS');
 * // getLabel('VALID') → '유효'
 * // getLabel('UNKNOWN_CODE') → 'UNKNOWN_CODE' (fallback)
 */
export function useCodeMaster(category: string) {
  const query = useQuery({
    queryKey: codeMasterKeys.category(category),
    queryFn: async () => {
      const response = await codeMasterApi.getAll({ category, activeOnly: true, size: 500 });
      return response.data.items;
    },
    staleTime: 10 * 60 * 1000, // 10 minutes
    gcTime: 30 * 60 * 1000,    // 30 minutes
    enabled: !!category,
  });

  const codes: CodeMasterItem[] = query.data ?? [];

  const getLabel = (code: string): string => {
    const item = codes.find((c) => c.code === code);
    return item?.nameKo ?? code;
  };

  const getLabelEn = (code: string): string => {
    const item = codes.find((c) => c.code === code);
    return item?.nameEn ?? code;
  };

  const getItem = (code: string): CodeMasterItem | undefined => {
    return codes.find((c) => c.code === code);
  };

  return {
    codes,
    getLabel,
    getLabelEn,
    getItem,
    isLoading: query.isLoading,
    error: query.error,
  };
}

/**
 * Fetch all distinct categories
 */
export function useCodeMasterCategories() {
  return useQuery({
    queryKey: codeMasterKeys.categories(),
    queryFn: async () => {
      const response = await codeMasterApi.getCategories();
      return response.data.categories;
    },
    staleTime: 10 * 60 * 1000,
  });
}
