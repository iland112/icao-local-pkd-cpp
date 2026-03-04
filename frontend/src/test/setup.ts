/// <reference types="vitest/globals" />
import '@testing-library/jest-dom';

// Mock __APP_VERSION__ global (defined in vite.config.ts)
// @ts-expect-error - global define from vite
globalThis.__APP_VERSION__ = '2.27.1';

// Mock import.meta.env
// Vitest already provides import.meta.env, but ensure DEV is set
if (typeof import.meta.env === 'undefined') {
  // @ts-expect-error - polyfill for test environment
  import.meta.env = {};
}

// Mock window.matchMedia (used by Tailwind/responsive components)
Object.defineProperty(window, 'matchMedia', {
  writable: true,
  value: vi.fn().mockImplementation((query: string) => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: vi.fn(),
    removeListener: vi.fn(),
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
    dispatchEvent: vi.fn(),
  })),
});

// Mock IntersectionObserver (used by lazy-loaded components)
class MockIntersectionObserver {
  observe = vi.fn();
  unobserve = vi.fn();
  disconnect = vi.fn();
  root = null;
  rootMargin = '';
  thresholds = [0];
  takeRecords = vi.fn().mockReturnValue([]);
}

Object.defineProperty(window, 'IntersectionObserver', {
  writable: true,
  value: MockIntersectionObserver,
});

// Mock ResizeObserver (used by Recharts and some layout components)
class MockResizeObserver {
  observe = vi.fn();
  unobserve = vi.fn();
  disconnect = vi.fn();
}

Object.defineProperty(window, 'ResizeObserver', {
  writable: true,
  value: MockResizeObserver,
});

// Mock URL.createObjectURL / revokeObjectURL (used by CSV export)
if (typeof URL.createObjectURL === 'undefined') {
  URL.createObjectURL = vi.fn(() => 'blob:mock-url');
}
if (typeof URL.revokeObjectURL === 'undefined') {
  URL.revokeObjectURL = vi.fn();
}

// Clean up localStorage between tests
afterEach(() => {
  localStorage.clear();
  vi.restoreAllMocks();
});
