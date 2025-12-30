import { create } from 'zustand';
import { persist } from 'zustand/middleware';

interface SidebarState {
  expanded: boolean;
  mobileOpen: boolean;
  toggleExpanded: () => void;
  setExpanded: (expanded: boolean) => void;
  toggleMobile: () => void;
  setMobileOpen: (open: boolean) => void;
}

export const useSidebarStore = create<SidebarState>()(
  persist(
    (set) => ({
      expanded: true,
      mobileOpen: false,
      toggleExpanded: () => set((state) => ({ expanded: !state.expanded })),
      setExpanded: (expanded) => set({ expanded }),
      toggleMobile: () => set((state) => ({ mobileOpen: !state.mobileOpen })),
      setMobileOpen: (mobileOpen) => set({ mobileOpen }),
    }),
    {
      name: 'sidebar-storage',
      partialize: (state) => ({ expanded: state.expanded }),
    }
  )
);
