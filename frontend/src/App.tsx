import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { Layout } from '@/components/layout';
import { ToastContainer } from '@/components/common';
import { Dashboard, FileUpload, UploadHistory, UploadDetail, UploadDashboard, PAVerify, PAHistory, PADetail, PADashboard, SyncDashboard, APIDocs } from '@/pages';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: 1,
      staleTime: 5 * 60 * 1000, // 5 minutes
    },
  },
});

function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <Routes>
          <Route path="/" element={<Layout />}>
            <Route index element={<Dashboard />} />
            <Route path="upload" element={<FileUpload />} />
            <Route path="upload-history" element={<UploadHistory />} />
            <Route path="upload/:uploadId" element={<UploadDetail />} />
            <Route path="upload-dashboard" element={<UploadDashboard />} />
            <Route path="pa/verify" element={<PAVerify />} />
            <Route path="pa/history" element={<PAHistory />} />
            <Route path="pa/:paId" element={<PADetail />} />
            <Route path="pa/dashboard" element={<PADashboard />} />
            <Route path="sync" element={<SyncDashboard />} />
            <Route path="api-docs/:service" element={<APIDocs />} />
          </Route>
        </Routes>
        <ToastContainer />
      </BrowserRouter>
    </QueryClientProvider>
  );
}

export default App;
