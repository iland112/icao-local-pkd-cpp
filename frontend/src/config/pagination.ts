/**
 * Pagination configuration
 *
 * Page size can be configured via environment variable:
 *   VITE_DEFAULT_PAGE_SIZE=25  (in .env or docker-compose)
 *
 * Default: 25
 */
const envPageSize = Number(import.meta.env.VITE_DEFAULT_PAGE_SIZE);
export const DEFAULT_PAGE_SIZE = envPageSize > 0 ? envPageSize : 25;
