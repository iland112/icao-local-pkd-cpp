import { cn } from '@/utils/cn';
import { Check, X, Loader2, Upload, FileSearch, ShieldCheck, Database, Server } from 'lucide-react';

export type StepStatus = 'idle' | 'active' | 'completed' | 'error';

export interface Step {
  id: string;
  label: string;
  description?: string;
  status: StepStatus;
  progress?: number;
  details?: string;
  icon?: React.ReactNode;
}

interface StepperProps {
  steps: Step[];
  orientation?: 'horizontal' | 'vertical';
  size?: 'sm' | 'md' | 'lg';
  showProgress?: boolean;
  className?: string;
}

const defaultIcons: Record<string, React.ReactNode> = {
  upload: <Upload className="w-3.5 h-3.5" />,
  parse: <FileSearch className="w-3.5 h-3.5" />,
  validate: <ShieldCheck className="w-3.5 h-3.5" />,
  database: <Database className="w-3.5 h-3.5" />,
  ldap: <Server className="w-3.5 h-3.5" />,
};

export function Stepper({
  steps,
  orientation = 'vertical',
  size = 'md',
  showProgress = true,
  className,
}: StepperProps) {
  const isVertical = orientation === 'vertical';

  const sizeConfig = {
    sm: {
      indicator: 'size-6',
      iconSize: 'size-3',
      text: 'text-xs',
      spacing: 'gap-x-2',
      lineWidth: 'w-px',
      lineHeight: 'h-full',
    },
    md: {
      indicator: 'size-8',
      iconSize: 'size-4',
      text: 'text-sm',
      spacing: 'gap-x-3',
      lineWidth: 'w-0.5',
      lineHeight: 'h-full',
    },
    lg: {
      indicator: 'size-10',
      iconSize: 'size-5',
      text: 'text-base',
      spacing: 'gap-x-4',
      lineWidth: 'w-1',
      lineHeight: 'h-full',
    },
  };

  const config = sizeConfig[size];

  return (
    <ul
      className={cn(
        'relative flex',
        isVertical ? 'flex-col gap-0' : 'flex-row gap-2',
        className
      )}
    >
      {steps.map((step, index) => {
        const isLast = index === steps.length - 1;
        const icon = step.icon || defaultIcons[step.id];

        return (
          <li
            key={step.id}
            className={cn(
              'group flex',
              isVertical
                ? `${config.spacing} relative`
                : 'items-center shrink basis-0 flex-1'
            )}
          >
            {/* Step Indicator Column (Vertical) */}
            {isVertical ? (
              <>
                <div className="flex flex-col items-center">
                  {/* Circle Indicator */}
                  <StepIndicator
                    step={step}
                    index={index}
                    icon={icon}
                    config={config}
                  />

                  {/* Vertical Line */}
                  {!isLast && (
                    <div
                      className={cn(
                        'mt-1 flex-1 min-h-8',
                        config.lineWidth,
                        step.status === 'completed'
                          ? 'bg-gradient-to-b from-teal-500 to-teal-400'
                          : step.status === 'active'
                          ? 'bg-gradient-to-b from-blue-500 to-blue-300'
                          : step.status === 'error'
                          ? 'bg-red-300'
                          : 'bg-gray-200 dark:bg-neutral-700'
                      )}
                    />
                  )}
                </div>

                {/* Content */}
                <div className={cn('grow pb-6 pt-0.5', isLast && 'pb-0')}>
                  <StepContent step={step} config={config} showProgress={showProgress} />
                </div>
              </>
            ) : (
              /* Horizontal Layout */
              <>
                <span className="min-w-7 min-h-7 inline-flex items-center">
                  <StepIndicator
                    step={step}
                    index={index}
                    icon={icon}
                    config={config}
                  />
                  <span className={cn('ms-2 font-medium text-gray-800 dark:text-white', config.text)}>
                    {step.label}
                  </span>
                </span>

                {/* Horizontal Line */}
                {!isLast && (
                  <div
                    className={cn(
                      'w-full h-0.5 flex-1 mx-2 rounded-full transition-colors duration-300',
                      step.status === 'completed'
                        ? 'bg-teal-500'
                        : step.status === 'active'
                        ? 'bg-blue-500'
                        : step.status === 'error'
                        ? 'bg-red-400'
                        : 'bg-gray-200 dark:bg-neutral-700'
                    )}
                  />
                )}
              </>
            )}
          </li>
        );
      })}
    </ul>
  );
}

interface StepIndicatorProps {
  step: Step;
  index: number;
  icon?: React.ReactNode;
  config: {
    indicator: string;
    iconSize: string;
    text: string;
  };
}

function StepIndicator({ step, index, icon, config }: StepIndicatorProps) {
  const getIndicatorClasses = () => {
    const baseClasses = cn(
      config.indicator,
      'flex justify-center items-center shrink-0 rounded-full font-semibold transition-all duration-300'
    );

    switch (step.status) {
      case 'active':
        return cn(
          baseClasses,
          'bg-blue-600 text-white shadow-lg shadow-blue-500/30 ring-4 ring-blue-100 dark:ring-blue-900/50'
        );
      case 'completed':
        return cn(
          baseClasses,
          'bg-teal-500 text-white shadow-md shadow-teal-500/20'
        );
      case 'error':
        return cn(
          baseClasses,
          'bg-red-500 text-white shadow-lg shadow-red-500/30 ring-4 ring-red-100 dark:ring-red-900/50'
        );
      default:
        return cn(
          baseClasses,
          'bg-gray-100 text-gray-500 dark:bg-neutral-700 dark:text-neutral-400'
        );
    }
  };

  const renderContent = () => {
    if (step.status === 'active') {
      return (
        <Loader2 className={cn(config.iconSize, 'animate-spin')} />
      );
    }
    if (step.status === 'completed') {
      return <Check className={config.iconSize} strokeWidth={3} />;
    }
    if (step.status === 'error') {
      return <X className={config.iconSize} strokeWidth={3} />;
    }
    // Idle - show icon or number
    return icon || <span className="text-xs font-bold">{index + 1}</span>;
  };

  return (
    <span className={getIndicatorClasses()}>
      {renderContent()}
    </span>
  );
}

interface StepContentProps {
  step: Step;
  config: {
    text: string;
  };
  showProgress: boolean;
}

function StepContent({ step, config, showProgress }: StepContentProps) {
  return (
    <div className="space-y-1">
      {/* Label */}
      <span
        className={cn(
          'block font-semibold transition-colors',
          config.text,
          step.status === 'active'
            ? 'text-blue-600 dark:text-blue-400'
            : step.status === 'completed'
            ? 'text-teal-600 dark:text-teal-400'
            : step.status === 'error'
            ? 'text-red-600 dark:text-red-400'
            : 'text-gray-600 dark:text-neutral-400'
        )}
      >
        {step.label}
      </span>

      {/* Description */}
      {step.description && (
        <p
          className={cn(
            'text-xs text-gray-500 dark:text-neutral-500 transition-opacity',
            step.status === 'idle' && 'opacity-60'
          )}
        >
          {step.description}
        </p>
      )}

      {/* Progress Bar */}
      {showProgress && step.status === 'active' && step.progress !== undefined && step.progress > 0 && (
        <div className="mt-2">
          <div className="flex items-center justify-between mb-1">
            <span className="text-xs font-medium text-blue-600 dark:text-blue-400">
              {step.progress}%
            </span>
          </div>
          <div className="h-1.5 w-full bg-gray-200 dark:bg-neutral-700 rounded-full overflow-hidden">
            <div
              className="h-full bg-gradient-to-r from-blue-500 to-blue-400 rounded-full transition-all duration-300 ease-out"
              style={{ width: `${step.progress}%` }}
            />
          </div>
          {/* Details below progress bar - always visible when present */}
          {step.details && (
            <p className="text-xs text-blue-600 dark:text-blue-400 font-medium mt-1.5">
              {step.details}
            </p>
          )}
        </div>
      )}

      {/* Details for active status without progress bar (when progress is 0 or undefined but details exist) */}
      {step.status === 'active' && step.details && !(step.progress !== undefined && step.progress > 0) && (
        <p className="text-xs text-blue-600 dark:text-blue-400 font-medium mt-1">
          {step.details}
        </p>
      )}

      {/* Error/Success Details */}
      {step.status === 'completed' && step.details && (
        <p className="text-xs text-teal-600 dark:text-teal-400 font-medium">
          {step.details}
        </p>
      )}
      {step.status === 'error' && step.details && (
        <p className="text-xs text-red-600 dark:text-red-400">
          {step.details}
        </p>
      )}
    </div>
  );
}

export default Stepper;
