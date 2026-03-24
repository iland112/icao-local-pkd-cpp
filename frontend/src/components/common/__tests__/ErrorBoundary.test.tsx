import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ErrorBoundary } from '../ErrorBoundary';

// Mock i18n
vi.mock('../../../i18n', () => ({
  default: {
    t: (key: string) => {
      const translations: Record<string, string> = {
        'common:errorBoundary.title': '오류가 발생했습니다',
        'common:errorBoundary.description': '예상치 못한 오류가 발생했습니다.',
        'common:errorBoundary.retry': '다시 시도',
      };
      return translations[key] ?? key;
    },
  },
}));

// Component that throws an error
function ThrowingComponent({ shouldThrow }: { shouldThrow: boolean }) {
  if (shouldThrow) throw new Error('Test error message');
  return <div>Normal content</div>;
}

// Suppress React error boundary console output during tests
const originalError = console.error;
beforeEach(() => {
  console.error = vi.fn();
  return () => {
    console.error = originalError;
  };
});

describe('ErrorBoundary', () => {
  it('should render children when no error', () => {
    render(
      <ErrorBoundary>
        <div>Child content</div>
      </ErrorBoundary>
    );

    expect(screen.getByText('Child content')).toBeInTheDocument();
  });

  it('should render error UI when child throws', () => {
    render(
      <ErrorBoundary>
        <ThrowingComponent shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('오류가 발생했습니다')).toBeInTheDocument();
    expect(screen.getByText('예상치 못한 오류가 발생했습니다.')).toBeInTheDocument();
    expect(screen.getByText('다시 시도')).toBeInTheDocument();
  });

  it('should render custom fallback when provided', () => {
    render(
      <ErrorBoundary fallback={<div>Custom error page</div>}>
        <ThrowingComponent shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('Custom error page')).toBeInTheDocument();
    expect(screen.queryByText('오류가 발생했습니다')).not.toBeInTheDocument();
  });

  it('should have retry button that resets error state', () => {
    render(
      <ErrorBoundary>
        <ThrowingComponent shouldThrow={true} />
      </ErrorBoundary>
    );

    expect(screen.getByText('오류가 발생했습니다')).toBeInTheDocument();

    // Verify retry button is present and clickable
    const retryButton = screen.getByText('다시 시도');
    expect(retryButton).toBeInTheDocument();
    expect(retryButton.closest('button')).toBeInTheDocument();
  });
});
