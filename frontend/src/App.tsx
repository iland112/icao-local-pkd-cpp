import { lazy, Suspense, useEffect } from 'react';
import { BrowserRouter, Routes, Route, useNavigate } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { Layout } from '@/components/layout';
import { ToastContainer, PrivateRoute, AdminRoute, ErrorBoundary } from '@/components/common';
import { Login } from '@/pages';
import { Dashboard } from '@/pages';

// Lazy-loaded page components (code splitting)
const FileUpload = lazy(() => import('./pages/FileUpload'));
const CertificateUpload = lazy(() => import('./pages/CertificateUpload'));
const CertificateSearch = lazy(() => import('./pages/CertificateSearch'));
const UploadHistory = lazy(() => import('./pages/UploadHistory'));
const UploadDetail = lazy(() => import('./pages/UploadDetail'));
const UploadDashboard = lazy(() => import('./pages/UploadDashboard'));
const PAVerify = lazy(() => import('./pages/PAVerify'));
const PAHistory = lazy(() => import('./pages/PAHistory'));
const PADetail = lazy(() => import('./pages/PADetail'));
const PADashboard = lazy(() => import('./pages/PADashboard'));
const SyncDashboard = lazy(() => import('./pages/SyncDashboard'));
const IcaoStatus = lazy(() => import('./pages/IcaoStatus'));
const MonitoringDashboard = lazy(() => import('./pages/MonitoringDashboard'));
const AiAnalysisDashboard = lazy(() => import('./pages/AiAnalysisDashboard'));
const DscNcReport = lazy(() => import('./pages/DscNcReport'));
const CrlReport = lazy(() => import('./pages/CrlReport'));
const TrustChainValidationReport = lazy(() => import('./pages/TrustChainValidationReport'));
const Profile = lazy(() => import('./pages/Profile'));
const UserManagement = lazy(() => import('./pages/UserManagement'));
const ApiClientManagement = lazy(() => import('./pages/ApiClientManagement'));
const OperationAuditLog = lazy(() => import('./pages/OperationAuditLog'));
const AuditLog = lazy(() => import('./pages/AuditLog'));
const PendingDscApproval = lazy(() => import('./pages/PendingDscApproval'));
const ApiClientRequest = lazy(() => import('./pages/ApiClientRequest'));
const CertificateQualityReport = lazy(() => import('./pages/CertificateQualityReport'));

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: 1,
      staleTime: 5 * 60 * 1000, // 5 minutes
    },
  },
});

// Preline UI initializer removed — no data-hs-* components used in this app

/**
 * Listens for 'auth:expired' custom events dispatched by API interceptors
 * and navigates to /login using React Router (no full page reload).
 */
function AuthExpiredHandler() {
  const navigate = useNavigate();

  useEffect(() => {
    const handleAuthExpired = () => {
      navigate('/login', { replace: true });
    };
    window.addEventListener('auth:expired', handleAuthExpired);
    return () => {
      window.removeEventListener('auth:expired', handleAuthExpired);
    };
  }, [navigate]);

  return null;
}

function PageLoader() {
  return (
    <div className="flex items-center justify-center h-64">
      <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-blue-600" />
    </div>
  );
}

function App() {
  return (
    <ErrorBoundary>
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <AuthExpiredHandler />
        <Routes>
          {/* Public Routes */}
          <Route path="/login" element={<Login />} />
          <Route path="/api-client-request" element={<Suspense fallback={<PageLoader />}><ApiClientRequest /></Suspense>} />

          {/* Protected Routes - Require Authentication */}
          <Route path="/" element={<PrivateRoute><Layout /></PrivateRoute>}>
            <Route index element={<Dashboard />} />
            <Route path="upload" element={<Suspense fallback={<PageLoader />}><FileUpload /></Suspense>} />
            <Route path="upload/certificate" element={<Suspense fallback={<PageLoader />}><CertificateUpload /></Suspense>} />
            <Route path="pkd/certificates" element={<Suspense fallback={<PageLoader />}><CertificateSearch /></Suspense>} />
            <Route path="pkd/dsc-nc" element={<Suspense fallback={<PageLoader />}><DscNcReport /></Suspense>} />
            <Route path="pkd/quality" element={<Suspense fallback={<PageLoader />}><CertificateQualityReport /></Suspense>} />
            <Route path="pkd/crl" element={<Suspense fallback={<PageLoader />}><CrlReport /></Suspense>} />
            <Route path="upload-history" element={<Suspense fallback={<PageLoader />}><UploadHistory /></Suspense>} />
            <Route path="upload/:uploadId" element={<Suspense fallback={<PageLoader />}><UploadDetail /></Suspense>} />
            <Route path="upload-dashboard" element={<Suspense fallback={<PageLoader />}><UploadDashboard /></Suspense>} />
            <Route path="pkd/trust-chain" element={<Suspense fallback={<PageLoader />}><TrustChainValidationReport /></Suspense>} />
            <Route path="pa/verify" element={<Suspense fallback={<PageLoader />}><PAVerify /></Suspense>} />
            <Route path="pa/history" element={<Suspense fallback={<PageLoader />}><PAHistory /></Suspense>} />
            <Route path="pa/:paId" element={<Suspense fallback={<PageLoader />}><PADetail /></Suspense>} />
            <Route path="pa/dashboard" element={<Suspense fallback={<PageLoader />}><PADashboard /></Suspense>} />
            <Route path="sync" element={<Suspense fallback={<PageLoader />}><SyncDashboard /></Suspense>} />
            <Route path="icao" element={<Suspense fallback={<PageLoader />}><IcaoStatus /></Suspense>} />
            <Route path="monitoring" element={<Suspense fallback={<PageLoader />}><MonitoringDashboard /></Suspense>} />
            <Route path="ai/analysis" element={<Suspense fallback={<PageLoader />}><AiAnalysisDashboard /></Suspense>} />

            <Route path="profile" element={<Suspense fallback={<PageLoader />}><Profile /></Suspense>} />
            <Route path="admin/users" element={<AdminRoute><Suspense fallback={<PageLoader />}><UserManagement /></Suspense></AdminRoute>} />
            <Route path="admin/api-clients" element={<AdminRoute><Suspense fallback={<PageLoader />}><ApiClientManagement /></Suspense></AdminRoute>} />
            <Route path="admin/operation-audit" element={<AdminRoute><Suspense fallback={<PageLoader />}><OperationAuditLog /></Suspense></AdminRoute>} />
            <Route path="admin/audit-log" element={<AdminRoute><Suspense fallback={<PageLoader />}><AuditLog /></Suspense></AdminRoute>} />
            <Route path="admin/pending-dsc" element={<AdminRoute><Suspense fallback={<PageLoader />}><PendingDscApproval /></Suspense></AdminRoute>} />
          </Route>
        </Routes>
        <ToastContainer />
      </BrowserRouter>
    </QueryClientProvider>
    </ErrorBoundary>
  );
}

export default App;
