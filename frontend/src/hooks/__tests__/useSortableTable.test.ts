import { describe, it, expect } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { useSortableTable } from '../useSortableTable';

interface TestItem {
  name: string;
  age: number;
  country?: string | null;
}

const testData: TestItem[] = [
  { name: 'Charlie', age: 30, country: 'KR' },
  { name: 'Alice', age: 25, country: 'US' },
  { name: 'Bob', age: 35, country: null },
];

describe('useSortableTable', () => {
  describe('initial state', () => {
    it('should return unsorted data when no default sort', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      expect(result.current.sortedData).toEqual(testData);
      expect(result.current.sortConfig).toBeNull();
    });

    it('should apply default sort config', () => {
      const { result } = renderHook(() =>
        useSortableTable(testData, { key: 'name', direction: 'asc' })
      );

      expect(result.current.sortConfig).toEqual({ key: 'name', direction: 'asc' });
      expect(result.current.sortedData[0].name).toBe('Alice');
      expect(result.current.sortedData[2].name).toBe('Charlie');
    });
  });

  describe('requestSort', () => {
    it('should sort ascending on first click', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('name'));

      expect(result.current.sortConfig).toEqual({ key: 'name', direction: 'asc' });
      expect(result.current.sortedData.map((d) => d.name)).toEqual(['Alice', 'Bob', 'Charlie']);
    });

    it('should sort descending on second click (same key)', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('name'));
      act(() => result.current.requestSort('name'));

      expect(result.current.sortConfig).toEqual({ key: 'name', direction: 'desc' });
      expect(result.current.sortedData.map((d) => d.name)).toEqual(['Charlie', 'Bob', 'Alice']);
    });

    it('should clear sort on third click (same key)', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('name'));
      act(() => result.current.requestSort('name'));
      act(() => result.current.requestSort('name'));

      expect(result.current.sortConfig).toBeNull();
      expect(result.current.sortedData).toEqual(testData);
    });

    it('should reset to asc when switching to a different key', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('name'));
      act(() => result.current.requestSort('age'));

      expect(result.current.sortConfig).toEqual({ key: 'age', direction: 'asc' });
      expect(result.current.sortedData.map((d) => d.age)).toEqual([25, 30, 35]);
    });
  });

  describe('sorting behavior', () => {
    it('should sort numbers correctly', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('age'));
      expect(result.current.sortedData.map((d) => d.age)).toEqual([25, 30, 35]);

      act(() => result.current.requestSort('age'));
      expect(result.current.sortedData.map((d) => d.age)).toEqual([35, 30, 25]);
    });

    it('should handle null values (pushed to end in asc)', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('country'));

      const countries = result.current.sortedData.map((d) => d.country);
      // null should be at the end
      expect(countries[countries.length - 1]).toBeNull();
    });

    it('should handle null values (pushed to end in desc)', () => {
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('country'));
      act(() => result.current.requestSort('country'));

      const countries = result.current.sortedData.map((d) => d.country);
      // null still at end in desc
      expect(countries[countries.length - 1]).toBeNull();
    });

    it('should not mutate original data', () => {
      const original = [...testData];
      const { result } = renderHook(() => useSortableTable(testData));

      act(() => result.current.requestSort('name'));

      expect(testData).toEqual(original);
    });

    it('should handle empty data', () => {
      const { result } = renderHook(() => useSortableTable<TestItem>([]));

      act(() => result.current.requestSort('name'));

      expect(result.current.sortedData).toEqual([]);
    });

    it('should handle single item', () => {
      const { result } = renderHook(() => useSortableTable([testData[0]]));

      act(() => result.current.requestSort('name'));

      expect(result.current.sortedData).toHaveLength(1);
    });
  });

  describe('data reactivity', () => {
    it('should re-sort when data changes', () => {
      const { result, rerender } = renderHook(
        ({ data }) => useSortableTable(data, { key: 'name', direction: 'asc' }),
        { initialProps: { data: testData } }
      );

      expect(result.current.sortedData[0].name).toBe('Alice');

      const newData = [...testData, { name: 'Aaron', age: 20, country: 'JP' }];
      rerender({ data: newData });

      expect(result.current.sortedData[0].name).toBe('Aaron');
    });
  });
});
