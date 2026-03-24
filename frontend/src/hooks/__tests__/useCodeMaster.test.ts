import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook, waitFor } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { createElement } from 'react';
import { useCodeMaster, useCodeMasterCategories, codeMasterKeys } from '../useCodeMaster';

// --- Mocks ---

vi.mock('@/services/codeMasterApi', () => ({
  codeMasterApi: {
    getAll: vi.fn(),
    getCategories: vi.fn(),
  },
}));

import { codeMasterApi } from '@/services/codeMasterApi';
import type { CodeMasterItem } from '@/services/codeMasterApi';

// --- Helpers ---

function createWrapper() {
  const queryClient = new QueryClient({
    defaultOptions: {
      queries: { retry: false, gcTime: 0 },
      mutations: { retry: false },
    },
  });
  return ({ children }: { children: React.ReactNode }) =>
    createElement(QueryClientProvider, { client: queryClient }, children);
}

const mockValidationStatusItems: CodeMasterItem[] = [
  {
    id: '1',
    category: 'VALIDATION_STATUS',
    code: 'VALID',
    nameKo: '유효',
    nameEn: 'Valid',
    description: 'Certificate is valid',
    severity: null,
    sortOrder: 1,
    isActive: true,
    metadata: null,
    createdAt: '2026-01-01T00:00:00Z',
    updatedAt: '2026-01-01T00:00:00Z',
  },
  {
    id: '2',
    category: 'VALIDATION_STATUS',
    code: 'INVALID',
    nameKo: '무효',
    nameEn: 'Invalid',
    description: 'Certificate is invalid',
    severity: 'HIGH',
    sortOrder: 2,
    isActive: true,
    metadata: null,
    createdAt: '2026-01-01T00:00:00Z',
    updatedAt: '2026-01-01T00:00:00Z',
  },
  {
    id: '3',
    category: 'VALIDATION_STATUS',
    code: 'PENDING',
    nameKo: '검증 대기',
    nameEn: 'Pending',
    description: null,
    severity: null,
    sortOrder: 3,
    isActive: true,
    metadata: { color: 'yellow' },
    createdAt: '2026-01-01T00:00:00Z',
    updatedAt: '2026-01-01T00:00:00Z',
  },
];

// --- Tests ---

describe('useCodeMaster', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  describe('initial state (loading)', () => {
    it('should return empty codes array while loading', () => {
      vi.mocked(codeMasterApi.getAll).mockReturnValue(new Promise(() => {})); // never resolves

      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      expect(result.current.codes).toEqual([]);
      expect(result.current.isLoading).toBe(true);
      expect(result.current.error).toBeNull();
    });

    it('should have working getLabel fallback while loading', () => {
      vi.mocked(codeMasterApi.getAll).mockReturnValue(new Promise(() => {}));

      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      // fallback: return the code as-is when not yet loaded
      expect(result.current.getLabel('VALID')).toBe('VALID');
      expect(result.current.getLabelEn('VALID')).toBe('VALID');
      expect(result.current.getItem('VALID')).toBeUndefined();
    });
  });

  describe('successful data fetch', () => {
    beforeEach(() => {
      vi.mocked(codeMasterApi.getAll).mockResolvedValue({
        data: {
          success: true,
          total: 3,
          page: 1,
          size: 500,
          items: mockValidationStatusItems,
        },
      } as any);
    });

    it('should return loaded codes after successful fetch', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.codes).toHaveLength(3);
      expect(result.current.error).toBeNull();
    });

    it('should call getAll with correct parameters', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(codeMasterApi.getAll).toHaveBeenCalledWith({
        category: 'VALIDATION_STATUS',
        activeOnly: true,
        size: 500,
      });
    });

    it('should return Korean label for known code via getLabel', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.getLabel('VALID')).toBe('유효');
      expect(result.current.getLabel('INVALID')).toBe('무효');
      expect(result.current.getLabel('PENDING')).toBe('검증 대기');
    });

    it('should return the code itself as fallback for unknown code in getLabel', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.getLabel('UNKNOWN_CODE')).toBe('UNKNOWN_CODE');
      expect(result.current.getLabel('')).toBe('');
    });

    it('should return English label for known code via getLabelEn', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.getLabelEn('VALID')).toBe('Valid');
      expect(result.current.getLabelEn('INVALID')).toBe('Invalid');
    });

    it('should fall back to the code when nameEn is null in getLabelEn', async () => {
      // PENDING has nameEn: 'Pending', but test with a hypothetical null nameEn item
      const itemWithNullNameEn: CodeMasterItem = {
        ...mockValidationStatusItems[0],
        id: '99',
        code: 'NULL_EN',
        nameKo: '한국어만',
        nameEn: null,
      };
      vi.mocked(codeMasterApi.getAll).mockResolvedValue({
        data: {
          success: true,
          total: 1,
          page: 1,
          size: 500,
          items: [itemWithNullNameEn],
        },
      } as any);

      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      // nameEn is null → falls back to the code
      expect(result.current.getLabelEn('NULL_EN')).toBe('NULL_EN');
    });

    it('should return the full item via getItem for a known code', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      const item = result.current.getItem('VALID');
      expect(item).toBeDefined();
      expect(item?.id).toBe('1');
      expect(item?.nameKo).toBe('유효');
      expect(item?.severity).toBeNull();
    });

    it('should return undefined via getItem for an unknown code', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.getItem('DOES_NOT_EXIST')).toBeUndefined();
    });

    it('should return item with metadata via getItem', async () => {
      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      const item = result.current.getItem('PENDING');
      expect(item?.metadata).toEqual({ color: 'yellow' });
    });
  });

  describe('disabled state (empty category)', () => {
    it('should not fetch when category is empty string', () => {
      const { result } = renderHook(
        () => useCodeMaster(''),
        { wrapper: createWrapper() }
      );

      // enabled: !!category → false for empty string
      expect(result.current.isLoading).toBe(false);
      expect(result.current.codes).toEqual([]);
      expect(codeMasterApi.getAll).not.toHaveBeenCalled();
    });
  });

  describe('error handling', () => {
    it('should expose error when fetch fails', async () => {
      const apiError = new Error('Network error');
      vi.mocked(codeMasterApi.getAll).mockRejectedValue(apiError);

      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.error).toBe(apiError);
      expect(result.current.codes).toEqual([]);
    });

    it('should return fallback labels when fetch fails', async () => {
      vi.mocked(codeMasterApi.getAll).mockRejectedValue(new Error('500'));

      const { result } = renderHook(
        () => useCodeMaster('VALIDATION_STATUS'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.getLabel('VALID')).toBe('VALID');
      expect(result.current.getLabelEn('VALID')).toBe('VALID');
      expect(result.current.getItem('VALID')).toBeUndefined();
    });
  });

  describe('query key structure (codeMasterKeys)', () => {
    it('should generate stable base key', () => {
      expect(codeMasterKeys.all).toEqual(['codeMaster']);
    });

    it('should generate category-specific key', () => {
      expect(codeMasterKeys.category('VALIDATION_STATUS')).toEqual([
        'codeMaster',
        'VALIDATION_STATUS',
      ]);
    });

    it('should generate categories key', () => {
      expect(codeMasterKeys.categories()).toEqual(['codeMaster', 'categories']);
    });

    it('should produce different keys for different categories', () => {
      const key1 = codeMasterKeys.category('CERT_TYPE');
      const key2 = codeMasterKeys.category('UPLOAD_STATUS');
      expect(key1).not.toEqual(key2);
    });
  });

  describe('different categories', () => {
    it('should use the correct category in the API call', async () => {
      vi.mocked(codeMasterApi.getAll).mockResolvedValue({
        data: { success: true, total: 0, page: 1, size: 500, items: [] },
      } as any);

      const { result } = renderHook(
        () => useCodeMaster('CERTIFICATE_TYPE'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(codeMasterApi.getAll).toHaveBeenCalledWith({
        category: 'CERTIFICATE_TYPE',
        activeOnly: true,
        size: 500,
      });
    });

    it('should return empty codes when API returns empty items', async () => {
      vi.mocked(codeMasterApi.getAll).mockResolvedValue({
        data: { success: true, total: 0, page: 1, size: 500, items: [] },
      } as any);

      const { result } = renderHook(
        () => useCodeMaster('EMPTY_CATEGORY'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.codes).toHaveLength(0);
      expect(result.current.getLabel('ANYTHING')).toBe('ANYTHING');
    });
  });

  describe('Korean text support', () => {
    it('should return Korean label correctly including multi-byte characters', async () => {
      const koreanItem: CodeMasterItem = {
        id: 'k1',
        category: 'TEST',
        code: 'KOR',
        nameKo: '한국어 레이블 테스트',
        nameEn: 'Korean label test',
        description: '테스트용 코드',
        severity: null,
        sortOrder: 1,
        isActive: true,
        metadata: null,
        createdAt: '2026-01-01T00:00:00Z',
        updatedAt: '2026-01-01T00:00:00Z',
      };

      vi.mocked(codeMasterApi.getAll).mockResolvedValue({
        data: { success: true, total: 1, page: 1, size: 500, items: [koreanItem] },
      } as any);

      const { result } = renderHook(
        () => useCodeMaster('TEST'),
        { wrapper: createWrapper() }
      );

      await waitFor(() => expect(result.current.isLoading).toBe(false));

      expect(result.current.getLabel('KOR')).toBe('한국어 레이블 테스트');
    });
  });
});

describe('useCodeMasterCategories', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should fetch categories successfully', async () => {
    vi.mocked(codeMasterApi.getCategories).mockResolvedValue({
      data: {
        success: true,
        count: 3,
        categories: ['VALIDATION_STATUS', 'CERTIFICATE_TYPE', 'UPLOAD_STATUS'],
      },
    } as any);

    const { result } = renderHook(() => useCodeMasterCategories(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.data).toEqual([
      'VALIDATION_STATUS',
      'CERTIFICATE_TYPE',
      'UPLOAD_STATUS',
    ]);
    expect(result.current.error).toBeNull();
  });

  it('should be in loading state initially', () => {
    vi.mocked(codeMasterApi.getCategories).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(() => useCodeMasterCategories(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isLoading).toBe(true);
    expect(result.current.data).toBeUndefined();
  });

  it('should expose error when categories fetch fails', async () => {
    const err = new Error('Categories fetch failed');
    vi.mocked(codeMasterApi.getCategories).mockRejectedValue(err);

    const { result } = renderHook(() => useCodeMasterCategories(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
    expect(result.current.data).toBeUndefined();
  });

  it('should return empty array when API returns no categories', async () => {
    vi.mocked(codeMasterApi.getCategories).mockResolvedValue({
      data: { success: true, count: 0, categories: [] },
    } as any);

    const { result } = renderHook(() => useCodeMasterCategories(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.data).toEqual([]);
  });

  it('should call getCategories exactly once per mount', async () => {
    vi.mocked(codeMasterApi.getCategories).mockResolvedValue({
      data: { success: true, count: 0, categories: [] },
    } as any);

    const { result } = renderHook(() => useCodeMasterCategories(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(codeMasterApi.getCategories).toHaveBeenCalledTimes(1);
  });
});
