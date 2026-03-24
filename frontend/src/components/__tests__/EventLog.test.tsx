import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { EventLog } from '../EventLog';
import type { EventLogEntry } from '../EventLog';

const makeEntry = (overrides: Partial<EventLogEntry> = {}): EventLogEntry => ({
  id: 1,
  timestamp: '12:34:56.789',
  eventName: 'TEST_EVENT',
  detail: 'Some detail',
  status: 'info',
  ...overrides,
});

describe('EventLog', () => {
  it('should render "No events yet" when events array is empty', () => {
    render(<EventLog events={[]} onClear={vi.fn()} />);
    expect(screen.getByText('No events yet')).toBeInTheDocument();
  });

  it('should render event entries when events are provided', () => {
    const events = [
      makeEntry({ id: 1, eventName: 'PARSING_IN_PROGRESS', detail: 'DSC 1/100' }),
      makeEntry({ id: 2, eventName: 'DB_SAVING_COMPLETED', detail: 'COMPLETED', status: 'success' }),
    ];
    render(<EventLog events={events} onClear={vi.fn()} />);

    expect(screen.getByText('PARSING_IN_PROGRESS:')).toBeInTheDocument();
    expect(screen.getByText('DSC 1/100')).toBeInTheDocument();
    expect(screen.getByText('DB_SAVING_COMPLETED:')).toBeInTheDocument();
    expect(screen.getByText('COMPLETED')).toBeInTheDocument();
  });

  it('should render timestamps in brackets', () => {
    const events = [makeEntry({ timestamp: '10:00:00.000' })];
    render(<EventLog events={events} onClear={vi.fn()} />);

    expect(screen.getByText('[10:00:00.000]')).toBeInTheDocument();
  });

  it('should show total event count in footer', () => {
    const events = [makeEntry({ id: 1 }), makeEntry({ id: 2 }), makeEntry({ id: 3 })];
    render(<EventLog events={events} onClear={vi.fn()} />);

    expect(screen.getByText('Total events: 3')).toBeInTheDocument();
  });

  it('should show last event timestamp in footer', () => {
    const events = [
      makeEntry({ id: 1, timestamp: '10:00:00.000' }),
      makeEntry({ id: 2, timestamp: '11:22:33.456' }),
    ];
    render(<EventLog events={events} onClear={vi.fn()} />);

    expect(screen.getByText('Last: 11:22:33.456')).toBeInTheDocument();
  });

  it('should show "-" for last timestamp when no events', () => {
    render(<EventLog events={[]} onClear={vi.fn()} />);
    expect(screen.getByText('Last: -')).toBeInTheDocument();
  });

  it('should call onClear when Clear button is clicked', () => {
    const onClear = vi.fn();
    render(<EventLog events={[makeEntry()]} onClear={onClear} />);

    fireEvent.click(screen.getByText('Clear'));
    expect(onClear).toHaveBeenCalledOnce();
  });

  it('should toggle auto-scroll when Auto-scroll button is clicked', () => {
    render(<EventLog events={[]} onClear={vi.fn()} />);

    expect(screen.getByText('Auto-scroll: ON')).toBeInTheDocument();

    fireEvent.click(screen.getByText('Auto-scroll: ON'));
    expect(screen.getByText('Auto-scroll: OFF')).toBeInTheDocument();

    fireEvent.click(screen.getByText('Auto-scroll: OFF'));
    expect(screen.getByText('Auto-scroll: ON')).toBeInTheDocument();
  });

  it('should render Event Log title in header', () => {
    render(<EventLog events={[]} onClear={vi.fn()} />);
    expect(screen.getByText('Event Log')).toBeInTheDocument();
  });

  it('should apply custom className to the container', () => {
    const { container } = render(
      <EventLog events={[]} onClear={vi.fn()} className="custom-class" />
    );
    expect(container.firstChild).toHaveClass('custom-class');
  });

  it('should render all four status dot colors', () => {
    const events: EventLogEntry[] = [
      makeEntry({ id: 1, status: 'info' }),
      makeEntry({ id: 2, status: 'success' }),
      makeEntry({ id: 3, status: 'fail' }),
      makeEntry({ id: 4, status: 'warning' }),
    ];
    const { container } = render(<EventLog events={events} onClear={vi.fn()} />);

    expect(container.querySelector('.bg-blue-500')).toBeInTheDocument();
    expect(container.querySelector('.bg-emerald-500')).toBeInTheDocument();
    expect(container.querySelector('.bg-red-500')).toBeInTheDocument();
    expect(container.querySelector('.bg-amber-500')).toBeInTheDocument();
  });
});
