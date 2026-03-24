import { describe, it, expect, beforeEach } from 'vitest';
import { useSidebarStore } from '../sidebarStore';

describe('sidebarStore', () => {
  beforeEach(() => {
    useSidebarStore.setState({ expanded: true, mobileOpen: false });
  });

  describe('expanded state', () => {
    it('should default to expanded=true', () => {
      expect(useSidebarStore.getState().expanded).toBe(true);
    });

    it('should toggle expanded', () => {
      useSidebarStore.getState().toggleExpanded();
      expect(useSidebarStore.getState().expanded).toBe(false);

      useSidebarStore.getState().toggleExpanded();
      expect(useSidebarStore.getState().expanded).toBe(true);
    });

    it('should set expanded directly', () => {
      useSidebarStore.getState().setExpanded(false);
      expect(useSidebarStore.getState().expanded).toBe(false);

      useSidebarStore.getState().setExpanded(true);
      expect(useSidebarStore.getState().expanded).toBe(true);
    });
  });

  describe('mobileOpen state', () => {
    it('should default to mobileOpen=false', () => {
      expect(useSidebarStore.getState().mobileOpen).toBe(false);
    });

    it('should toggle mobileOpen', () => {
      useSidebarStore.getState().toggleMobile();
      expect(useSidebarStore.getState().mobileOpen).toBe(true);

      useSidebarStore.getState().toggleMobile();
      expect(useSidebarStore.getState().mobileOpen).toBe(false);
    });

    it('should set mobileOpen directly', () => {
      useSidebarStore.getState().setMobileOpen(true);
      expect(useSidebarStore.getState().mobileOpen).toBe(true);

      useSidebarStore.getState().setMobileOpen(false);
      expect(useSidebarStore.getState().mobileOpen).toBe(false);
    });
  });

  describe('state independence', () => {
    it('toggling expanded should not affect mobileOpen', () => {
      useSidebarStore.getState().setMobileOpen(true);
      useSidebarStore.getState().toggleExpanded();

      expect(useSidebarStore.getState().expanded).toBe(false);
      expect(useSidebarStore.getState().mobileOpen).toBe(true);
    });

    it('toggling mobileOpen should not affect expanded', () => {
      useSidebarStore.getState().setExpanded(false);
      useSidebarStore.getState().toggleMobile();

      expect(useSidebarStore.getState().expanded).toBe(false);
      expect(useSidebarStore.getState().mobileOpen).toBe(true);
    });
  });
});
