/**
 * Runtime application configuration.
 * Values are injected by docker-entrypoint.sh via /config.js (window.__APP_CONFIG__).
 * Defaults apply when running outside Docker (e.g., local `npm run dev`).
 */

interface AppConfig {
  enableEacMenu: boolean;
}

declare global {
  interface Window {
    __APP_CONFIG__?: AppConfig;
  }
}

const defaults: AppConfig = {
  enableEacMenu: true, // show EAC menu in local dev by default
};

export const appConfig: AppConfig = {
  ...defaults,
  ...window.__APP_CONFIG__,
};
