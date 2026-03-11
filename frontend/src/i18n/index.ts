import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import LanguageDetector from 'i18next-browser-languagedetector';

// Korean translations
import koCommon from './locales/ko/common.json';
import koNav from './locales/ko/nav.json';
import koDashboard from './locales/ko/dashboard.json';
import koUpload from './locales/ko/upload.json';
import koCertificate from './locales/ko/certificate.json';
import koPa from './locales/ko/pa.json';
import koSync from './locales/ko/sync.json';
import koReport from './locales/ko/report.json';
import koAi from './locales/ko/ai.json';
import koAdmin from './locales/ko/admin.json';
import koAuth from './locales/ko/auth.json';
import koMonitoring from './locales/ko/monitoring.json';
import koIcao from './locales/ko/icao.json';

// English translations
import enCommon from './locales/en/common.json';
import enNav from './locales/en/nav.json';
import enDashboard from './locales/en/dashboard.json';
import enUpload from './locales/en/upload.json';
import enCertificate from './locales/en/certificate.json';
import enPa from './locales/en/pa.json';
import enSync from './locales/en/sync.json';
import enReport from './locales/en/report.json';
import enAi from './locales/en/ai.json';
import enAdmin from './locales/en/admin.json';
import enAuth from './locales/en/auth.json';
import enMonitoring from './locales/en/monitoring.json';
import enIcao from './locales/en/icao.json';

const resources = {
  ko: {
    common: koCommon,
    nav: koNav,
    dashboard: koDashboard,
    upload: koUpload,
    certificate: koCertificate,
    pa: koPa,
    sync: koSync,
    report: koReport,
    ai: koAi,
    admin: koAdmin,
    auth: koAuth,
    monitoring: koMonitoring,
    icao: koIcao,
  },
  en: {
    common: enCommon,
    nav: enNav,
    dashboard: enDashboard,
    upload: enUpload,
    certificate: enCertificate,
    pa: enPa,
    sync: enSync,
    report: enReport,
    ai: enAi,
    admin: enAdmin,
    auth: enAuth,
    monitoring: enMonitoring,
    icao: enIcao,
  },
};

i18n
  .use(LanguageDetector)
  .use(initReactI18next)
  .init({
    resources,
    supportedLngs: ['ko', 'en'],
    nonExplicitSupportedLngs: true,
    fallbackLng: 'ko',
    defaultNS: 'common',
    ns: [
      'common', 'nav', 'dashboard', 'upload', 'certificate',
      'pa', 'sync', 'report', 'ai', 'admin', 'auth',
      'monitoring', 'icao',
    ],
    interpolation: {
      escapeValue: false, // React already escapes
    },
    detection: {
      order: ['localStorage', 'navigator'],
      caches: ['localStorage'],
      lookupLocalStorage: 'i18nextLng',
    },
  });

export default i18n;
