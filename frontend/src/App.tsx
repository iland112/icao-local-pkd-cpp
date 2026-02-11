import { BrowserRouter, Routes, Route, useLocation } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { useEffect } from 'react';
import { Layout } from '@/components/layout';
import { ToastContainer, PrivateRoute } from '@/components/common';
import { Dashboard, FileUpload, UploadHistory, UploadDetail, UploadDashboard, PAVerify, PAHistory, PADetail, PADashboard, SyncDashboard, Login, Profile, AuditLog, UserManagement, CertificateUpload } from '@/pages';
import MonitoringDashboard from '@/pages/MonitoringDashboard';
import CertificateSearch from '@/pages/CertificateSearch';
import IcaoStatus from '@/pages/IcaoStatus';
import { OperationAuditLog } from '@/pages/OperationAuditLog';
import ValidationDemo from '@/pages/ValidationDemo';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: 1,
      staleTime: 5 * 60 * 1000, // 5 minutes
    },
  },
});

// Preline UI initializer component
function PrelineInitializer() {
  const location = useLocation();

  useEffect(() => {
    // Initialize Preline components on route change
    if (typeof window !== 'undefined' && (window as any).HSStaticMethods) {
      setTimeout(() => {
        (window as any).HSStaticMethods.autoInit();
      }, 100);
    }
  }, [location.pathname]);

  return null;
}

function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <PrelineInitializer />
        <Routes>
          {/* Public Route - Login */}
          <Route path="/login" element={<Login />} />

          {/* Protected Routes - Require Authentication */}
          <Route path="/" element={<PrivateRoute><Layout /></PrivateRoute>}>
            <Route index element={<Dashboard />} />
            <Route path="upload" element={<FileUpload />} />
            <Route path="upload/certificate" element={<CertificateUpload />} />
            <Route path="pkd/certificates" element={<CertificateSearch />} />
            <Route path="upload-history" element={<UploadHistory />} />
            <Route path="upload/:uploadId" element={<UploadDetail />} />
            <Route path="upload-dashboard" element={<UploadDashboard />} />
            <Route path="pa/verify" element={<PAVerify />} />
            <Route path="pa/history" element={<PAHistory />} />
            <Route path="pa/:paId" element={<PADetail />} />
            <Route path="pa/dashboard" element={<PADashboard />} />
            <Route path="sync" element={<SyncDashboard />} />
            <Route path="icao" element={<IcaoStatus />} />
            <Route path="monitoring" element={<MonitoringDashboard />} />
            <Route path="validation-demo" element={<ValidationDemo />} />
            <Route path="profile" element={<Profile />} />
            <Route path="admin/users" element={<UserManagement />} />
            <Route path="admin/operation-audit" element={<OperationAuditLog />} />
            <Route path="admin/audit-log" element={<AuditLog />} />
          </Route>
        </Routes>
        <ToastContainer />
      </BrowserRouter>
    </QueryClientProvider>
  );
}

export default App;
