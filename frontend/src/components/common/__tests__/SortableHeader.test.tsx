import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { SortableHeader } from '../SortableHeader';

describe('SortableHeader', () => {
  it('should render label text', () => {
    render(
      <table>
        <thead>
          <tr>
            <SortableHeader label="Name" sortKey="name" sortConfig={null} onSort={vi.fn()} />
          </tr>
        </thead>
      </table>
    );

    expect(screen.getByText('Name')).toBeInTheDocument();
  });

  it('should render children instead of label when provided', () => {
    render(
      <table>
        <thead>
          <tr>
            <SortableHeader label="Name" sortKey="name" sortConfig={null} onSort={vi.fn()}>
              <span>Custom Label</span>
            </SortableHeader>
          </tr>
        </thead>
      </table>
    );

    expect(screen.getByText('Custom Label')).toBeInTheDocument();
  });

  it('should call onSort with sortKey when clicked', () => {
    const onSort = vi.fn();
    render(
      <table>
        <thead>
          <tr>
            <SortableHeader label="Name" sortKey="name" sortConfig={null} onSort={onSort} />
          </tr>
        </thead>
      </table>
    );

    fireEvent.click(screen.getByText('Name'));
    expect(onSort).toHaveBeenCalledWith('name');
  });

  it('should show active styling when sortConfig matches sortKey (asc)', () => {
    const { container } = render(
      <table>
        <thead>
          <tr>
            <SortableHeader
              label="Name"
              sortKey="name"
              sortConfig={{ key: 'name', direction: 'asc' }}
              onSort={vi.fn()}
            />
          </tr>
        </thead>
      </table>
    );

    const sortIndicator = container.querySelector('.text-blue-500');
    expect(sortIndicator).toBeInTheDocument();
  });

  it('should show inactive styling when sortConfig does not match', () => {
    const { container } = render(
      <table>
        <thead>
          <tr>
            <SortableHeader
              label="Name"
              sortKey="name"
              sortConfig={{ key: 'age', direction: 'asc' }}
              onSort={vi.fn()}
            />
          </tr>
        </thead>
      </table>
    );

    const activeIndicator = container.querySelector('.text-blue-500');
    expect(activeIndicator).not.toBeInTheDocument();
  });

  it('should render as div when as="div"', () => {
    const { container } = render(
      <SortableHeader label="Name" sortKey="name" sortConfig={null} onSort={vi.fn()} as="div" />
    );

    expect(container.querySelector('div.cursor-pointer')).toBeInTheDocument();
    expect(container.querySelector('th')).not.toBeInTheDocument();
  });

  it('should apply custom className', () => {
    const { container } = render(
      <table>
        <thead>
          <tr>
            <SortableHeader
              label="Name"
              sortKey="name"
              sortConfig={null}
              onSort={vi.fn()}
              className="custom-class"
            />
          </tr>
        </thead>
      </table>
    );

    expect(container.querySelector('.custom-class')).toBeInTheDocument();
  });
});
