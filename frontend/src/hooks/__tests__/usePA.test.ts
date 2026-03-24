import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook, waitFor, act } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { createElement } from 'react';
import {
  usePAHistory,
  usePADetail,
  usePAStatistics,
  useVerifyPA,
  useParseDG1,
  useParseDG2,
  paKeys,
} from '../usePA';

// --- Mocks ---

vi.mock('@/services/api', () => ({
  paApi: {
    getHistory: vi.fn(),
    getDetail: vi.fn(),
    getStatistics: vi.fn(),
    verify: vi.fn(),
    parseDG1: vi.fn(),
    parseDG2: vi.fn(),
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

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { language: 'ko' },
  }),
}));

import { paApi } from '@/services/api';
import { toast } from '@/stores/toastStore';
import type { PAVerificationRequest } from '@/types';

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

// --- Fixtures ---

const mockHistoryResponse = {
  success: true,
  total: 2,
  page: 1,
  size: 10,
  items: [
    {
      id: 'pa-001',
      status: 'VALID',
      issuingCountry: 'KR',
      verificationTimestamp: '2026-01-01T00:00:00Z',
    },
    {
      id: 'pa-002',
      status: 'INVALID',
      issuingCountry: 'US',
      verificationTimestamp: '2026-01-02T00:00:00Z',
    },
  ],
};

const mockDetailResponse = {
  status: 'VALID' as const,
  verificationId: 'pa-001',
  verificationTimestamp: '2026-01-01T00:00:00Z',
  issuingCountry: 'KR',
  documentNumber: 'M12345678',
  certificateChainValidation: {
    valid: true,
    dscSubject: 'CN=KR DSC',
    dscSerialNumber: '01',
    cscaSubject: 'CN=KR CSCA',
    cscaSerialNumber: '02',
    notBefore: '2024-01-01T00:00:00Z',
    notAfter: '2029-01-01T00:00:00Z',
    crlChecked: true,
    revoked: false,
    crlStatus: 'NOT_REVOKED',
    crlStatusDescription: '',
    crlStatusDetailedDescription: '',
    crlStatusSeverity: 'INFO',
    crlMessage: '',
  },
  sodSignatureValidation: {
    valid: true,
    signatureAlgorithm: 'SHA256withRSA',
    hashAlgorithm: 'SHA-256',
  },
  dataGroupValidation: {
    totalGroups: 2,
    validGroups: 2,
    invalidGroups: 0,
    details: {},
  },
  processingDurationMs: 123,
  errors: [],
};

const mockStatisticsResponse = {
  success: true,
  totalVerifications: 50,
  validCount: 40,
  invalidCount: 10,
  averageProcessingTimeMs: 150,
};

const mockVerifyRequest: PAVerificationRequest = {
  sod: 'base64encodedSOD==',
  dataGroups: [
    { number: 1, data: 'base64DG1==' },
    { number: 2, data: 'base64DG2==' },
  ],
  mrzData: {
    line1: 'P<KORLDOE<<JOHN<<<<<<<<<<<<<<<<<<<<<<<<<<<<',
    line2: 'M123456789KOR8001015M3101015<<<<<<<<<<<<<<<6',
    documentNumber: 'M12345678',
  },
  requestedBy: 'admin',
};

// --- Tests ---

describe('paKeys', () => {
  it('should generate stable base key', () => {
    expect(paKeys.all).toEqual(['pa']);
  });

  it('should generate lists key', () => {
    expect(paKeys.lists()).toEqual(['pa', 'list']);
  });

  it('should generate paginated list key embedding params', () => {
    const params = { page: 1, size: 10 };
    expect(paKeys.list(params)).toEqual(['pa', 'list', params]);
  });

  it('should generate details key', () => {
    expect(paKeys.details()).toEqual(['pa', 'detail']);
  });

  it('should generate per-ID detail key', () => {
    expect(paKeys.detail('pa-001')).toEqual(['pa', 'detail', 'pa-001']);
  });

  it('should generate statistics key', () => {
    expect(paKeys.statistics()).toEqual(['pa', 'statistics']);
  });

  it('should produce distinct keys for different PA ids', () => {
    const k1 = paKeys.detail('pa-001');
    const k2 = paKeys.detail('pa-002');
    expect(k1).not.toEqual(k2);
  });
});

describe('usePAHistory', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be in loading state initially', () => {
    vi.mocked(paApi.getHistory).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(
      () => usePAHistory({ page: 1, size: 10 }),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(true);
    expect(result.current.data).toBeUndefined();
  });

  it('should return PA history on success', async () => {
    vi.mocked(paApi.getHistory).mockResolvedValue({
      data: mockHistoryResponse,
    } as any);

    const { result } = renderHook(
      () => usePAHistory({ page: 1, size: 10 }),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.data).toEqual(mockHistoryResponse);
    expect(result.current.error).toBeNull();
  });

  it('should pass pagination params to getHistory', async () => {
    vi.mocked(paApi.getHistory).mockResolvedValue({ data: mockHistoryResponse } as any);

    const params = { page: 3, size: 5 };
    const { result } = renderHook(
      () => usePAHistory(params),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(paApi.getHistory).toHaveBeenCalledWith(params);
  });

  it('should expose error when getHistory fails', async () => {
    const err = new Error('PA history fetch failed');
    vi.mocked(paApi.getHistory).mockRejectedValue(err);

    const { result } = renderHook(
      () => usePAHistory({ page: 1, size: 10 }),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
    expect(result.current.isError).toBe(true);
  });
});

describe('usePADetail', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be disabled when paId is undefined', () => {
    const { result } = renderHook(
      () => usePADetail(undefined),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(false);
    expect(result.current.data).toBeUndefined();
    expect(paApi.getDetail).not.toHaveBeenCalled();
  });

  it('should be disabled when paId is empty string', () => {
    const { result } = renderHook(
      () => usePADetail(''),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(false);
    expect(paApi.getDetail).not.toHaveBeenCalled();
  });

  it('should fetch detail when paId is provided', async () => {
    vi.mocked(paApi.getDetail).mockResolvedValue({
      data: mockDetailResponse,
    } as any);

    const { result } = renderHook(
      () => usePADetail('pa-001'),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(paApi.getDetail).toHaveBeenCalledWith('pa-001');
    expect(result.current.data).toEqual(mockDetailResponse);
  });

  it('should be in loading state while fetch is pending', () => {
    vi.mocked(paApi.getDetail).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(
      () => usePADetail('pa-001'),
      { wrapper: createWrapper() }
    );

    expect(result.current.isLoading).toBe(true);
  });

  it('should expose error when detail fetch fails', async () => {
    const err = new Error('PA record not found');
    vi.mocked(paApi.getDetail).mockRejectedValue(err);

    const { result } = renderHook(
      () => usePADetail('pa-999'),
      { wrapper: createWrapper() }
    );

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
  });
});

describe('usePAStatistics', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be in loading state initially', () => {
    vi.mocked(paApi.getStatistics).mockReturnValue(new Promise(() => {}));

    const { result } = renderHook(() => usePAStatistics(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isLoading).toBe(true);
  });

  it('should return PA statistics on success', async () => {
    vi.mocked(paApi.getStatistics).mockResolvedValue({
      data: mockStatisticsResponse,
    } as any);

    const { result } = renderHook(() => usePAStatistics(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.data).toEqual(mockStatisticsResponse);
    expect(result.current.error).toBeNull();
  });

  it('should call getStatistics exactly once on mount', async () => {
    vi.mocked(paApi.getStatistics).mockResolvedValue({
      data: mockStatisticsResponse,
    } as any);

    const { result } = renderHook(() => usePAStatistics(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(paApi.getStatistics).toHaveBeenCalledTimes(1);
  });

  it('should expose error when statistics fetch fails', async () => {
    const err = new Error('Statistics unavailable');
    vi.mocked(paApi.getStatistics).mockRejectedValue(err);

    const { result } = renderHook(() => usePAStatistics(), {
      wrapper: createWrapper(),
    });

    await waitFor(() => expect(result.current.isLoading).toBe(false));

    expect(result.current.error).toBe(err);
    expect(result.current.isError).toBe(true);
  });
});

describe('useVerifyPA', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be idle initially', () => {
    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isPending).toBe(false);
    expect(result.current.isSuccess).toBe(false);
    expect(result.current.isError).toBe(false);
  });

  it('should call paApi.verify with the request payload', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: { success: true, data: { ...mockDetailResponse, status: 'VALID' } },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    expect(paApi.verify).toHaveBeenCalledWith(mockVerifyRequest);
  });

  it('should show success toast when verification result is VALID', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: { success: true, data: { ...mockDetailResponse, status: 'VALID' } },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    expect(toast.success).toHaveBeenCalledWith(
      'pa:hook.verifySuccess',
      'pa:hook.verifySuccessDesc'
    );
    expect(toast.warning).not.toHaveBeenCalled();
  });

  it('should show warning toast when verification result is INVALID', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: {
        success: true,
        data: { ...mockDetailResponse, status: 'INVALID' },
      },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    expect(toast.warning).toHaveBeenCalledWith(
      'pa:hook.verifyFailed',
      'pa:hook.verifyFailedDesc'
    );
    expect(toast.success).not.toHaveBeenCalled();
  });

  it('should NOT show toast when data.success is false', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: { success: false, data: null },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    expect(toast.success).not.toHaveBeenCalled();
    expect(toast.warning).not.toHaveBeenCalled();
  });

  it('should NOT show toast when data.data is null/undefined', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: { success: true, data: undefined },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    expect(toast.success).not.toHaveBeenCalled();
    expect(toast.warning).not.toHaveBeenCalled();
  });

  it('should show error toast when paApi.verify throws an Error', async () => {
    const err = new Error('SOD parsing failed');
    vi.mocked(paApi.verify).mockRejectedValue(err);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync(mockVerifyRequest);
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'pa:hook.verifyError',
      'SOD parsing failed'
    );
  });

  it('should show generic error toast for non-Error rejections', async () => {
    vi.mocked(paApi.verify).mockRejectedValue({ status: 500 });

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync(mockVerifyRequest);
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'pa:hook.verifyError',
      'common:error.unknownError_short'
    );
  });

  it('should set isSuccess after successful mutation', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: { success: true, data: { ...mockDetailResponse, status: 'VALID' } },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    await waitFor(() => expect(result.current.isSuccess).toBe(true));
    expect(result.current.isError).toBe(false);
  });

  it('should set isError after failed mutation', async () => {
    vi.mocked(paApi.verify).mockRejectedValue(new Error('Network error'));

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync(mockVerifyRequest);
      } catch {
        // expected
      }
    });

    await waitFor(() => expect(result.current.isError).toBe(true));
    expect(result.current.isSuccess).toBe(false);
  });

  it('should handle UNKNOWN status (not VALID) with warning toast', async () => {
    vi.mocked(paApi.verify).mockResolvedValue({
      data: {
        success: true,
        data: { ...mockDetailResponse, status: 'UNKNOWN' },
      },
    } as any);

    const { result } = renderHook(() => useVerifyPA(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      await result.current.mutateAsync(mockVerifyRequest);
    });

    // status !== 'VALID' → warning
    expect(toast.warning).toHaveBeenCalled();
    expect(toast.success).not.toHaveBeenCalled();
  });
});

describe('useParseDG1', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be idle initially', () => {
    const { result } = renderHook(() => useParseDG1(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isPending).toBe(false);
    expect(result.current.isSuccess).toBe(false);
  });

  it('should call parseDG1 with the provided base64 data', async () => {
    const mockDG1Response = {
      success: true,
      data: {
        documentNumber: 'M12345678',
        nationality: 'KOR',
        dateOfBirth: '800101',
      },
    };
    vi.mocked(paApi.parseDG1).mockResolvedValue({ data: mockDG1Response } as any);

    const { result } = renderHook(() => useParseDG1(), {
      wrapper: createWrapper(),
    });

    const dg1Data = 'base64EncodedDG1==';
    await act(async () => {
      await result.current.mutateAsync(dg1Data);
    });

    expect(paApi.parseDG1).toHaveBeenCalledWith(dg1Data);
    await waitFor(() => expect(result.current.isSuccess).toBe(true));
  });

  it('should show error toast when DG1 parsing fails', async () => {
    const err = new Error('Invalid DG1 structure');
    vi.mocked(paApi.parseDG1).mockRejectedValue(err);

    const { result } = renderHook(() => useParseDG1(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('bad-data');
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'pa:hook.dg1ParseFailed',
      'Invalid DG1 structure'
    );
  });

  it('should show generic error for non-Error DG1 rejection', async () => {
    vi.mocked(paApi.parseDG1).mockRejectedValue(null);

    const { result } = renderHook(() => useParseDG1(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('bad-data');
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'pa:hook.dg1ParseFailed',
      'common:error.unknownError_short'
    );
  });

  it('should set isError after failed mutation', async () => {
    vi.mocked(paApi.parseDG1).mockRejectedValue(new Error('fail'));

    const { result } = renderHook(() => useParseDG1(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('data');
      } catch {
        // expected
      }
    });

    await waitFor(() => expect(result.current.isError).toBe(true));
  });
});

describe('useParseDG2', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should be idle initially', () => {
    const { result } = renderHook(() => useParseDG2(), {
      wrapper: createWrapper(),
    });

    expect(result.current.isPending).toBe(false);
    expect(result.current.isSuccess).toBe(false);
  });

  it('should call parseDG2 with the provided base64 data', async () => {
    const mockDG2Response = {
      success: true,
      data: {
        faceImageBase64: 'jpeg-base64-data==',
        imageFormat: 'JPEG',
      },
    };
    vi.mocked(paApi.parseDG2).mockResolvedValue({ data: mockDG2Response } as any);

    const { result } = renderHook(() => useParseDG2(), {
      wrapper: createWrapper(),
    });

    const dg2Data = 'base64EncodedDG2==';
    await act(async () => {
      await result.current.mutateAsync(dg2Data);
    });

    expect(paApi.parseDG2).toHaveBeenCalledWith(dg2Data);
    await waitFor(() => expect(result.current.isSuccess).toBe(true));
  });

  it('should show error toast when DG2 parsing fails', async () => {
    const err = new Error('JPEG2000 decode error');
    vi.mocked(paApi.parseDG2).mockRejectedValue(err);

    const { result } = renderHook(() => useParseDG2(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('corrupt-data');
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'pa:hook.dg2ParseFailed',
      'JPEG2000 decode error'
    );
  });

  it('should show generic error for non-Error DG2 rejection', async () => {
    vi.mocked(paApi.parseDG2).mockRejectedValue(undefined);

    const { result } = renderHook(() => useParseDG2(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('data');
      } catch {
        // expected
      }
    });

    expect(toast.error).toHaveBeenCalledWith(
      'pa:hook.dg2ParseFailed',
      'common:error.unknownError_short'
    );
  });

  it('should use dg2ParseFailed key, not dg1ParseFailed key', async () => {
    vi.mocked(paApi.parseDG2).mockRejectedValue(new Error('DG2 fail'));

    const { result } = renderHook(() => useParseDG2(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('data');
      } catch {
        // expected
      }
    });

    const [titleArg] = vi.mocked(toast.error).mock.calls[0];
    expect(titleArg).toBe('pa:hook.dg2ParseFailed');
    expect(titleArg).not.toBe('pa:hook.dg1ParseFailed');
  });

  it('should set isError after failed mutation', async () => {
    vi.mocked(paApi.parseDG2).mockRejectedValue(new Error('fail'));

    const { result } = renderHook(() => useParseDG2(), {
      wrapper: createWrapper(),
    });

    await act(async () => {
      try {
        await result.current.mutateAsync('data');
      } catch {
        // expected
      }
    });

    await waitFor(() => expect(result.current.isError).toBe(true));
  });
});
