/**
 * Monitoring Service API
 *
 * Handles communication with Monitoring Service (port 8084)
 * Responsibilities:
 * - System metrics (CPU, Memory, Disk, Network)
 * - Service health checks
 * - Metrics history
 *
 * @version 2.12.0
 */

import axios, { type AxiosError, type AxiosResponse } from 'axios';

// --- Types ---

export interface CpuMetrics {
  usagePercent: number;
  load1min: number;
  load5min: number;
  load15min: number;
}

export interface MemoryMetrics {
  totalMb: number;
  usedMb: number;
  freeMb: number;
  usagePercent: number;
}

export interface DiskMetrics {
  totalGb: number;
  usedGb: number;
  freeGb: number;
  usagePercent: number;
}

export interface NetworkMetrics {
  bytesSent: number;
  bytesRecv: number;
  packetsSent: number;
  packetsRecv: number;
}

export interface SystemMetrics {
  timestamp: string;
  cpu: CpuMetrics;
  memory: MemoryMetrics;
  disk: DiskMetrics;
  network: NetworkMetrics;
}

export interface ServiceHealth {
  serviceName: string;
  status: 'UP' | 'DEGRADED' | 'DOWN' | 'UNKNOWN';
  responseTimeMs: number;
  errorMessage?: string;
  checkedAt: string;
}

export interface SystemOverview {
  latestMetrics: SystemMetrics;
  services: ServiceHealth[];
}

export interface MetricsHistoryItem {
  timestamp: string;
  cpuUsagePercent: number;
  memoryUsagePercent: number;
  diskUsagePercent: number;
}

// --- Axios Instance ---

const monitoringClient = axios.create({
  baseURL: '/api/monitoring',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

monitoringClient.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (import.meta.env.DEV) console.error('[Monitoring API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// --- Monitoring Service APIs ---

export const monitoringServiceApi = {
  getHealth: () =>
    monitoringClient.get<{ status: string; uptime?: string }>('/health'),

  getSystemOverview: () =>
    monitoringClient.get<SystemOverview>('/system/overview'),

  getServicesHealth: () =>
    monitoringClient.get<ServiceHealth[]>('/services'),

  getMetricsHistory: (params: { hours?: number; limit?: number }) =>
    monitoringClient.get<MetricsHistoryItem[]>('/system/history', { params }),

  getSystemMetricsLatest: () =>
    monitoringClient.get<SystemMetrics>('/system/latest'),
};
