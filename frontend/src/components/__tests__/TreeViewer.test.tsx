import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { TreeViewer } from '../TreeViewer';
import type { TreeNode } from '../TreeViewer';

// Mock react-arborist — it requires DOM measurements unavailable in jsdom
vi.mock('react-arborist', () => ({
  Tree: ({ children, data }: { children: (args: any) => any; data: TreeNode[] }) => {
    // Render a flat list of all nodes for testability
    const renderNodes = (nodes: TreeNode[]): React.ReactNode[] => {
      return nodes.flatMap((node) => {
        const style = {};
        const fakeNode = {
          data: node,
          isOpen: false,
          toggle: vi.fn(),
        };
        return [
          children({ node: fakeNode, style }),
          ...(node.children ? renderNodes(node.children) : []),
        ];
      });
    };
    return <div data-testid="tree-container">{renderNodes(data)}</div>;
  },
}));

// Mock navigator.clipboard
Object.defineProperty(navigator, 'clipboard', {
  value: { writeText: vi.fn().mockResolvedValue(undefined) },
  writable: true,
});

const makeNode = (overrides: Partial<TreeNode> = {}): TreeNode => ({
  id: 'node-1',
  name: 'Test Node',
  ...overrides,
});

describe('TreeViewer', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('should render without crashing with empty data', () => {
    const { container } = render(<TreeViewer data={[]} />);
    expect(container.querySelector('[data-testid="tree-container"]')).toBeInTheDocument();
  });

  it('should render a node name with colon suffix', () => {
    render(<TreeViewer data={[makeNode({ name: 'Subject DN' })]} />);
    expect(screen.getByText('Subject DN:')).toBeInTheDocument();
  });

  it('should render node value when provided', () => {
    render(<TreeViewer data={[makeNode({ name: 'Key', value: 'some-value' })]} />);
    expect(screen.getByText('some-value')).toBeInTheDocument();
  });

  it('should not render value when value is absent', () => {
    const { container } = render(<TreeViewer data={[makeNode({ name: 'Key', value: undefined })]} />);
    const spans = container.querySelectorAll('span');
    // Only the name span should be present
    const texts = Array.from(spans).map((s) => s.textContent);
    expect(texts.some((t) => t === '')).toBe(false);
  });

  it('should render copy button when node is copyable and has a value', () => {
    render(
      <TreeViewer
        data={[makeNode({ name: 'Fingerprint', value: 'abc123', copyable: true })]}
      />
    );
    const copyBtn = screen.getByTitle('Copy to clipboard');
    expect(copyBtn).toBeInTheDocument();
  });

  it('should not render copy button when copyable is false', () => {
    render(
      <TreeViewer
        data={[makeNode({ name: 'Fingerprint', value: 'abc123', copyable: false })]}
      />
    );
    expect(screen.queryByTitle('Copy to clipboard')).not.toBeInTheDocument();
  });

  it('should call clipboard.writeText on copy button click', () => {
    render(
      <TreeViewer
        data={[makeNode({ name: 'Hash', value: 'deadbeef', copyable: true })]}
      />
    );
    fireEvent.click(screen.getByTitle('Copy to clipboard'));
    expect(navigator.clipboard.writeText).toHaveBeenCalledWith('deadbeef');
  });

  it('should render external link when linkUrl is provided', () => {
    render(
      <TreeViewer
        data={[makeNode({ name: 'CRL', value: 'http://crl.example.com', linkUrl: 'http://crl.example.com' })]}
      />
    );
    const link = screen.getByTitle('Open link');
    expect(link).toBeInTheDocument();
    expect(link).toHaveAttribute('href', 'http://crl.example.com');
  });

  it('should call onNodeClick when a leaf node is clicked', () => {
    const onNodeClick = vi.fn();
    const node = makeNode({ name: 'Leaf', value: 'leaf-value' });
    render(<TreeViewer data={[node]} onNodeClick={onNodeClick} />);

    // Click the node container (first div with hover style)
    const nodeDiv = screen.getByText('Leaf:').closest('div[class*="hover"]');
    if (nodeDiv) fireEvent.click(nodeDiv);

    expect(onNodeClick).toHaveBeenCalledWith(expect.objectContaining({ id: 'node-1' }));
  });

  it('should apply custom className to container', () => {
    const { container } = render(<TreeViewer data={[]} className="custom-class" />);
    expect(container.querySelector('.custom-class')).toBeInTheDocument();
  });

  it('should render compact mode (font classes differ)', () => {
    const { container } = render(
      <TreeViewer data={[makeNode({ name: 'Test', value: 'val' })]} compact />
    );
    // In compact mode the name uses text-[11px]
    const nameSpan = container.querySelector('.text-\\[11px\\]');
    expect(nameSpan).toBeInTheDocument();
  });

  it('should render flag icon for flag- prefixed icon name', () => {
    render(
      <TreeViewer
        data={[makeNode({ name: 'Country', icon: 'flag-kr' })]}
      />
    );
    const img = screen.getByAltText('KR');
    expect(img).toBeInTheDocument();
    expect(img).toHaveAttribute('src', '/svg/kr.svg');
  });

  it('should render known icon types without crashing', () => {
    const icons = ['file-text', 'shield', 'user', 'calendar', 'key', 'lock', 'settings', 'hash', 'link-2', 'alert-circle'];
    icons.forEach((icon) => {
      const { container } = render(
        <TreeViewer data={[makeNode({ name: `Icon ${icon}`, icon })]} />
      );
      expect(container.querySelector('[data-testid="tree-container"]')).toBeInTheDocument();
    });
  });

  it('should render nested children nodes', () => {
    const data: TreeNode[] = [
      {
        id: 'parent',
        name: 'Parent',
        children: [
          { id: 'child-1', name: 'Child One', value: 'c1' },
          { id: 'child-2', name: 'Child Two', value: 'c2' },
        ],
      },
    ];
    render(<TreeViewer data={data} />);
    expect(screen.getByText('Parent:')).toBeInTheDocument();
    expect(screen.getByText('Child One:')).toBeInTheDocument();
    expect(screen.getByText('Child Two:')).toBeInTheDocument();
  });
});
