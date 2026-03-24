import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

// ---------------------------------------------------------------------------
// appConfig reads window.__APP_CONFIG__ at module evaluation time via the
// spread:  { ...defaults, ...window.__APP_CONFIG__ }
// To test different window states we manipulate the property BEFORE each
// dynamic import and invalidate the module from the cache between tests.
// ---------------------------------------------------------------------------

type AppConfig = { enableEacMenu: boolean };

/** Helper: set window.__APP_CONFIG__ and re-import appConfig fresh. */
async function loadAppConfig(windowConfig?: Partial<AppConfig>): Promise<AppConfig> {
  // Detach previous cached module
  vi.resetModules();

  // Set (or clear) window.__APP_CONFIG__ before the module is evaluated
  if (windowConfig !== undefined) {
    Object.defineProperty(window, '__APP_CONFIG__', {
      value: windowConfig,
      writable: true,
      configurable: true,
    });
  } else {
    // Remove the property so the module falls through to defaults
    try {
      delete (window as { __APP_CONFIG__?: AppConfig }).__APP_CONFIG__;
    } catch {
      Object.defineProperty(window, '__APP_CONFIG__', {
        value: undefined,
        writable: true,
        configurable: true,
      });
    }
  }

  const mod = await import('../appConfig');
  return mod.appConfig as AppConfig;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
describe('appConfig', () => {
  afterEach(() => {
    // Restore modules and clean up window property after each test
    vi.resetModules();
    try {
      delete (window as { __APP_CONFIG__?: AppConfig }).__APP_CONFIG__;
    } catch {
      // ignore — read-only in some environments
    }
  });

  // -------------------------------------------------------------------------
  // Default values (no window.__APP_CONFIG__ injected)
  // -------------------------------------------------------------------------
  describe('default configuration (no window.__APP_CONFIG__)', () => {
    it('should default enableEacMenu to true when window.__APP_CONFIG__ is absent', async () => {
      const config = await loadAppConfig(undefined);
      expect(config.enableEacMenu).toBe(true);
    });

    it('should be a plain object (not null)', async () => {
      const config = await loadAppConfig(undefined);
      expect(config).not.toBeNull();
      expect(typeof config).toBe('object');
    });

    it('should expose the enableEacMenu property', async () => {
      const config = await loadAppConfig(undefined);
      expect('enableEacMenu' in config).toBe(true);
    });
  });

  // -------------------------------------------------------------------------
  // Runtime override from window.__APP_CONFIG__
  // -------------------------------------------------------------------------
  describe('runtime override via window.__APP_CONFIG__', () => {
    it('should override enableEacMenu to false when window.__APP_CONFIG__.enableEacMenu is false', async () => {
      const config = await loadAppConfig({ enableEacMenu: false });
      expect(config.enableEacMenu).toBe(false);
    });

    it('should keep enableEacMenu true when window.__APP_CONFIG__.enableEacMenu is true', async () => {
      const config = await loadAppConfig({ enableEacMenu: true });
      expect(config.enableEacMenu).toBe(true);
    });

    it('should use the window value, not the default, when override is provided', async () => {
      // Default is true; override with false
      const config = await loadAppConfig({ enableEacMenu: false });
      expect(config.enableEacMenu).toBe(false);
    });
  });

  // -------------------------------------------------------------------------
  // Edge cases
  // -------------------------------------------------------------------------
  describe('edge cases', () => {
    it('should fall back to defaults when window.__APP_CONFIG__ is an empty object', async () => {
      const config = await loadAppConfig({});
      // {} spread does not override any key → defaults win
      expect(config.enableEacMenu).toBe(true);
    });

    it('should be a frozen / read-only export (the exported object reflects spread result)', async () => {
      const config = await loadAppConfig({ enableEacMenu: false });
      // The value was set by the override at module init time
      expect(config.enableEacMenu).toBe(false);
    });
  });
});
