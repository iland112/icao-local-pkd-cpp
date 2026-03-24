import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { Stepper, type Step } from '../Stepper';

const makeSteps = (overrides: Partial<Step>[] = []): Step[] => {
  const defaults: Step[] = [
    { id: 'upload', label: 'Upload', status: 'idle' },
    { id: 'parse', label: 'Parse', status: 'idle' },
    { id: 'validate', label: 'Validate', status: 'idle' },
    { id: 'database', label: 'Save', status: 'idle' },
  ];
  return defaults.map((step, i) => ({ ...step, ...overrides[i] }));
};

describe('Stepper', () => {
  describe('vertical layout', () => {
    it('should render all step labels', () => {
      render(<Stepper steps={makeSteps()} orientation="vertical" />);

      expect(screen.getByText('Upload')).toBeInTheDocument();
      expect(screen.getByText('Parse')).toBeInTheDocument();
      expect(screen.getByText('Validate')).toBeInTheDocument();
      expect(screen.getByText('Save')).toBeInTheDocument();
    });

    it('should render description when provided', () => {
      const steps = makeSteps([{ description: 'Uploading file...' }]);
      render(<Stepper steps={steps} orientation="vertical" />);

      expect(screen.getByText('Uploading file...')).toBeInTheDocument();
    });

    it('should render progress bar for active step', () => {
      const steps = makeSteps([
        { status: 'completed' },
        { status: 'active', progress: 50, details: '50% done' },
      ]);
      const { container } = render(<Stepper steps={steps} orientation="vertical" />);

      expect(screen.getByText('50%')).toBeInTheDocument();
      expect(screen.getByText('50% done')).toBeInTheDocument();
      const bar = container.querySelector('[style*="width: 50%"]');
      expect(bar).toBeInTheDocument();
    });

    it('should show error details for error step', () => {
      const steps = makeSteps([
        { status: 'completed' },
        { status: 'error', details: 'Connection failed' },
      ]);
      render(<Stepper steps={steps} orientation="vertical" />);

      expect(screen.getByText('Connection failed')).toBeInTheDocument();
    });
  });

  describe('horizontal layout', () => {
    it('should render all step labels', () => {
      render(<Stepper steps={makeSteps()} orientation="horizontal" />);

      expect(screen.getByText('Upload')).toBeInTheDocument();
      expect(screen.getByText('Parse')).toBeInTheDocument();
      expect(screen.getByText('Validate')).toBeInTheDocument();
      expect(screen.getByText('Save')).toBeInTheDocument();
    });

    it('should show active step detail panel', () => {
      const steps = makeSteps([
        { status: 'completed' },
        { status: 'active', progress: 75, details: 'Processing...' },
      ]);
      render(<Stepper steps={steps} orientation="horizontal" />);

      expect(screen.getByText('75%')).toBeInTheDocument();
      expect(screen.getByText('Processing...')).toBeInTheDocument();
    });

    it('should show error step details in horizontal', () => {
      const steps = makeSteps([
        { status: 'completed' },
        { status: 'error', details: 'Upload error' },
      ]);
      render(<Stepper steps={steps} orientation="horizontal" />);

      expect(screen.getByText('Upload error')).toBeInTheDocument();
    });
  });

  describe('size variants', () => {
    it('should render with sm size', () => {
      const { container } = render(<Stepper steps={makeSteps()} size="sm" />);
      expect(container.querySelector('.size-6')).toBeInTheDocument();
    });

    it('should render with md size (default)', () => {
      const { container } = render(<Stepper steps={makeSteps()} />);
      expect(container.querySelector('.size-8')).toBeInTheDocument();
    });

    it('should render with lg size', () => {
      const { container } = render(<Stepper steps={makeSteps()} size="lg" />);
      expect(container.querySelector('.size-10')).toBeInTheDocument();
    });
  });

  describe('step statuses', () => {
    it('should render completed step with check icon class', () => {
      const steps = makeSteps([{ status: 'completed' }]);
      const { container } = render(<Stepper steps={steps} />);

      // Completed steps have teal background
      expect(container.querySelector('.bg-teal-500')).toBeInTheDocument();
    });

    it('should render active step with blue background', () => {
      const steps = makeSteps([{ status: 'active' }]);
      const { container } = render(<Stepper steps={steps} />);

      expect(container.querySelector('.bg-blue-600')).toBeInTheDocument();
    });

    it('should render error step with red background', () => {
      const steps = makeSteps([{ status: 'error' }]);
      const { container } = render(<Stepper steps={steps} />);

      expect(container.querySelector('.bg-red-500')).toBeInTheDocument();
    });

    it('should render idle step with gray background', () => {
      const steps = makeSteps();
      const { container } = render(<Stepper steps={steps} />);

      expect(container.querySelector('.bg-gray-100')).toBeInTheDocument();
    });
  });

  describe('showProgress', () => {
    it('should not show progress when showProgress=false', () => {
      const steps = makeSteps([{ status: 'active', progress: 50 }]);
      render(<Stepper steps={steps} showProgress={false} />);

      expect(screen.queryByText('50%')).not.toBeInTheDocument();
    });
  });
});
