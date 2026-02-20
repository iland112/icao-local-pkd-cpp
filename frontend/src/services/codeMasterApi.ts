/**
 * Code Master API
 *
 * Centralized code/status/enum management
 * Endpoints: /api/code-master/*
 */

import pkdApi from '@/services/pkdApi';

export interface CodeMasterItem {
  id: string;
  category: string;
  code: string;
  nameKo: string;
  nameEn?: string | null;
  description?: string | null;
  severity?: string | null;
  sortOrder: number;
  isActive: boolean;
  metadata?: Record<string, unknown> | null;
  createdAt: string;
  updatedAt: string;
}

export interface CodeMasterListResponse {
  success: boolean;
  total: number;
  page: number;
  size: number;
  items: CodeMasterItem[];
}

export interface CodeMasterCategoriesResponse {
  success: boolean;
  count: number;
  categories: string[];
}

export const codeMasterApi = {
  /**
   * Get codes by category (or all codes)
   */
  getAll: (params?: { category?: string; activeOnly?: boolean; page?: number; size?: number }) =>
    pkdApi.get<CodeMasterListResponse>('/code-master', { params }),

  /**
   * Get all distinct categories
   */
  getCategories: () =>
    pkdApi.get<CodeMasterCategoriesResponse>('/code-master/categories'),

  /**
   * Get single code by ID
   */
  getById: (id: string) =>
    pkdApi.get<{ success: boolean; item: CodeMasterItem }>(`/code-master/${id}`),
};

export default codeMasterApi;
