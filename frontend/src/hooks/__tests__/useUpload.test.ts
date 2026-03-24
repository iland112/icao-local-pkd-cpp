import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook, waitFor, act } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { createElement } from 'react';
import {
  useUploadHistory,
  useUploadDetail,
  useUploadStatistics,
  useUploadLdif,
  useUploadMasterList,
  uploadKeys,
} from '../useUpload';

// --- Mocks ---

vi.mock('@/services/api', () => ({
  uploadApi: {
    getHistory: vi.fn(),
    getDetail: vi.fn(),
    getStatistics: vi.fn(),
    uploadLdif: vi.fn(),
    uploadMasterList: vi.fn(),
  },
}));

vi.mock('@/stores/toastStore', () => ({
  toast: {
    success: vi.fn(),
    error: vi.fn(),
    warning: vi.fn(),
    info: vi.fn(),
  },
}));

// react-i18next: return the key as-is so we can assert on the key string
vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: 'ko' },
  }),
}));

import { uploadApi } from '@/services/api';
import { toast } from '@/stores/toastStore';

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

function makeFile(name = 'test.ldif', type = 'text/plain'): File {
  return new File(['dummy content'], name, { type });
}

// --- Fixtures ---

const mockUploadedFile = {
  id: 'upload-uuid-001',
  fileName: 'collection-001.ldif',
  fileFormat: 'LDIF' as const,
  fileSize: 1024,
  status: 'COMPLETED' as const,
  processingMode: 'AUTO' as const,
  createdAt: '2026-01-01T00:00:00Z',
  cscaCount: 5,
  dscCount: 100,
};

const mockPageResponse = {
  success: true,
  total: 1,
  page: 1,
  size: 10,
  items: [mockUploadedFile],
};

const mockStatistics = {
  success: true,
  totalFiles: 10,
  totalCertificates: 31212,
  cscaCount: 845,
  dscCount: 29838,
  crlCount: 69,
};

// --- Tests ---

describe('uploadKeys', () => {
  it('should generate stable base key', () => {
    expect(uploadKeys.all).toEqual(['uploads']);
  });

  it('should generate lists key', () => {
    expect(uploadKeys.lists()).toEqual(['uploads', 'list']);
  });

  it('should generate paginated list key', () => {
    const params = { page: 1, size: 10 };
    expect(uploadKeys.list(params)).toEqual(['uploads', 'list', params]);
  });

  it('should generate details key', () => {
    expect(uploadKeys.details()).toEqual(['uploads', 'detail']);
  });

  it('should generate detail key for specific id', () => {
    expect(uploadKeys.detail('abc-123')).toEqual(['uploads', 'detail', 'abc-123']);
  });

  it('should generate statistics key', () => {
    expect(uploadKeys.statistics()).toEqual(['uploads', 'statistics']);
  });

  it('should produce distinct keys for different upload ids', () => {
    const key1 = uploadKeys.detail('id-1');
    const key2 = uploadKeys.detail('id-2');
    expect(key1).not.toEqual(key2);
  });
});

describe('useUploadHistory', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be in loading state initially', () => {
    vi.mocked(uploadApi.getHistory).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(
      () => useUploadHistory({ page: 1, size: 10 }),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(true);
    expect(result.current.data).toBeUndefined();
  });

  it('should return upload history on success', async () => {
    vi.mocked(uploadApi.getHistory).mockResolvedValue({
      data: mockPageResponse,
    } as any);

    const { result } = renderHook(
      () => useUploadHistory({ page: 1, size: 10 }),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.data).toEqual(mockPageResponse);
    expect(result.current.error).toBeNull();
  });

  it('should call getHistory with provided params', async () => {
    vi.mocked(uploadApi.getHistory).mockResolvedValue({ data: mockPageResponse } as any);

    const params = { page: 2, size: 20 };
    const { result } = renderHook(
      () => useUploadHistory(params),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(uploadApi.getHistory).toHaveBeenCalledWith(params);
  });

  it('should expose error when getHistory fails', async () => {
    const err = new Error('Fetch history failed');
    vi.mocked(uploadApi.getHistory).mockRejectedValue(err);

    const { result } = renderHook(
      () => useUploadHistory({ page: 1, size: 10 }),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
    expect(result.current.isError).toBe(true);
  });

  it('should use different query keys for different params', async () => {
    vi.mocked(uploadApi.getHistory).mockResolvedValue({ data: mockPageResponse } as any);

    const wrapper = createWrapper();

    const { result: r1 } = renderHook(
      () => useUploadHistory({ page: 1, size: 10 }),
      { wrapper }
    );
    const { result: r2 } = renderHook(
      () => useUploadHistory({ page: 2, size: 10 }),
      { wrapper }
    );

    await waitFor(() => expect(r1.current.isLoading).toBe(false));
    await waitFor(() => expect(r2.current.isLoading).toBe(false));

    // Both called separately (different query keys)
    expect(uploadApi.getHistory).toHaveBeenCalledTimes(2);
  });
});

describe('useUploadDetail', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be disabled when uploadId is undefined', () => {
    const { result } = renderHook(
      () => useUploadDetail(undefined),
      { wrapper: createWrapper() }
    );

    // enabled: !!uploadId → false
    expect(result.current.isLoading).toBe(false);
    expect(result.current.data).toBeUndefined();
    expect(uploadApi.getDetail).not.toHaveBeenCalled();
  });

  it('should be disabled when uploadId is empty string', () => {
    const { result } = renderHook(
      () => useUploadDetail(''),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(false);
    expect(uploadApi.getDetail).not.toHaveBeenCalled();
  });

  it('should fetch detail when uploadId is provided', async () => {
    vi.mocked(uploadApi.getDetail).mockResolvedValue({
      data: { success: true, data: mockUploadedFile },
    } as any);

    const { result } = renderHook(
      () => useUploadDetail('upload-uuid-001'),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(uploadApi.getDetail).toHaveBeenCalledWith('upload-uuid-001');
    expect(result.current.data).toEqual({ success: true, data: mockUploadedFile });
  });

  it('should expose error when detail fetch fails', async () => {
    const err = new Error('Upload not found');
    vi.mocked(uploadApi.getDetail).mockRejectedValue(err);

    const { result } = renderHook(
      () => useUploadDetail('bad-id'),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
    expect(result.current.isError).toBe(true);
  });

  it('should be in loading state when uploadId is provided and fetch is pending', () => {
    vi.mocked(uploadApi.getDetail).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(
      () => useUploadDetail('upload-uuid-001'),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(true);
  });
});

describe('useUploadStatistics', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be in loading state initially', () => {
    vi.mocked(uploadApi.getStatistics).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(() => useUploadStatistics(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isLoading).toBe(true);
  });

  it('should return statistics on success', async () => {
    vi.mocked(uploadApi.getStatistics).mockResolvedValue({
      data: mockStatistics,
    } as any);

    const { result } = renderHook(() => useUploadStatistics(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.data).toEqual(mockStatistics);
    expect(result.current.error).toBeNull();
  });

  it('should call getStatistics once on mount', async () => {
    vi.mocked(uploadApi.getStatistics).mockResolvedValue({ data: mockStatistics } as any);

    const { result } = renderHook(() => useUploadStatistics(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(uploadApi.getStatistics).toHaveBeenCalledTimes(1);
  });

  it('should expose error when statistics fetch fails', async () => {
    const err = new Error('Statistics unavailable');
    vi.mocked(uploadApi.getStatistics).mockRejectedValue(err);

    const { result } = renderHook(() => useUploadStatistics(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
  });
});

describe('useUploadLdif', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be idle initially', () => {
    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isPending).toBe(false);
    expect(result.current.isSuccess).toBe(false);
    expect(result.current.isError).toBe(false);
  });

  it('should call uploadLdif with the provided file', async () => {
    const file = makeFile('collection-001.ldif');
    const uploadResponse = { success: true, data: mockUploadedFile };

    vi.mocked(uploadApi.uploadLdif).mockResolvedValue({ data: uploadResponse } as any);

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    expect(uploadApi.uploadLdif).toHaveBeenCalledWith(file);
  });

  it('should show success toast when upload succeeds with data.success=true', async () => {
    const file = makeFile('test.ldif');
    vi.mocked(uploadApi.uploadLdif).mockResolvedValue({
      data: { success: true, data: mockUploadedFile },
    } as any);

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    expect(toast.success).toHaveBeenCalledWith(
      'upload:hook.uploadSuccess',
      'upload:hook.ldifUploadSuccessDesc'
    );
  });

  it('should NOT show success toast when data.success=false', async () => {
    const file = makeFile('test.ldif');
    vi.mocked(uploadApi.uploadLdif).mockResolvedValue({
      data: { success: false },
    } as any);

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    expect(toast.success).not.toHaveBeenCalled();
  });

  it('should show error toast when upload throws an Error', async () => {
    const file = makeFile('bad.ldif');
    const err = new Error('File too large');
    vi.mocked(uploadApi.uploadLdif).mockRejectedValue(err);

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync({ file });
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'upload:hook.uploadFailed',
      'File too large'
    );
  });

  it('should show generic error message for non-Error rejection', async () => {
    const file = makeFile('bad.ldif');
    vi.mocked(uploadApi.uploadLdif).mockRejectedValue('string error');

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync({ file });
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'upload:hook.uploadFailed',
      'common:error.unknownError_short'
    );
  });

  it('should set isSuccess after successful mutation', async () => {
    const file = makeFile('test.ldif');
    vi.mocked(uploadApi.uploadLdif).mockResolvedValue({
      data: { success: true, data: mockUploadedFile },
    } as any);

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    await waitFor(() => expect(result.current.isSuccess).toBe(true));
    expect(result.current.isError).toBe(false);
  });

  it('should set isError after failed mutation', async () => {
    const file = makeFile('test.ldif');
    vi.mocked(uploadApi.uploadLdif).mockRejectedValue(new Error('Rejected'));

    const { result } = renderHook(() => useUploadLdif(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync({ file });
      } catch {
        // expected
      }
    });

    await waitFor(() => expect(result.current.isError).toBe(true));
    expect(result.current.isSuccess).toBe(false);
  });
});

describe('useUploadMasterList', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be idle initially', () => {
    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isPending).toBe(false);
    expect(result.current.isSuccess).toBe(false);
    expect(result.current.isError).toBe(false);
  });

  it('should call uploadMasterList with the provided file', async () => {
    const file = makeFile('masterlist.ml', 'application/octet-stream');
    vi.mocked(uploadApi.uploadMasterList).mockResolvedValue({
      data: { success: true, data: mockUploadedFile },
    } as any);

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    expect(uploadApi.uploadMasterList).toHaveBeenCalledWith(file);
  });

  it('should show success toast when master list upload succeeds', async () => {
    const file = makeFile('ml.ml');
    vi.mocked(uploadApi.uploadMasterList).mockResolvedValue({
      data: { success: true, data: mockUploadedFile },
    } as any);

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    expect(toast.success).toHaveBeenCalledWith(
      'upload:hook.uploadSuccess',
      'upload:hook.mlUploadSuccessDesc'
    );
  });

  it('should NOT show success toast when data.success=false', async () => {
    const file = makeFile('ml.ml');
    vi.mocked(uploadApi.uploadMasterList).mockResolvedValue({
      data: { success: false },
    } as any);

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    expect(toast.success).not.toHaveBeenCalled();
  });

  it('should show error toast when upload throws an Error', async () => {
    const file = makeFile('ml.ml');
    const err = new Error('Processing failed');
    vi.mocked(uploadApi.uploadMasterList).mockRejectedValue(err);

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync({ file });
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'upload:hook.uploadFailed',
      'Processing failed'
    );
  });

  it('should show generic error when rejection is not an Error instance', async () => {
    const file = makeFile('ml.ml');
    vi.mocked(uploadApi.uploadMasterList).mockRejectedValue({ code: 413 });

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync({ file });
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'upload:hook.uploadFailed',
      'common:error.unknownError_short'
    );
  });

  it('should set isError after failed mutation', async () => {
    const file = makeFile('ml.ml');
    vi.mocked(uploadApi.uploadMasterList).mockRejectedValue(new Error('Fail'));

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync({ file });
      } catch {
        // expected
      }
    });

    await waitFor(() => expect(result.current.isError).toBe(true));
  });

  it('should use different toast description from useUploadLdif', async () => {
    // Verify ML uses mlUploadSuccessDesc, not ldifUploadSuccessDesc
    const file = makeFile('ml.ml');
    vi.mocked(uploadApi.uploadMasterList).mockResolvedValue({
      data: { success: true, data: mockUploadedFile },
    } as any);

    const { result } = renderHook(() => useUploadMasterList(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync({ file });
    });

    const [, descArg] = vi.mocked(toast.success).mock.calls[0];
    expect(descArg).toBe('upload:hook.mlUploadSuccessDesc');
    expect(descArg).not.toBe('upload:hook.ldifUploadSuccessDesc');
  });
});
