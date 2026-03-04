import { describe, it, expect, beforeEach } from 'vitest';
import { useThemeStore } from '../themeStore';

beforeEach(() => {
  // Reset store state
  useThemeStore.setState({ darkMode: false });
  document.documentElement.classList.remove('dark');
});

describe('useThemeStore', () => {
  it('should start with darkMode false', () => {
    expect(useThemeStore.getState().darkMode).toBe(false);
  });

  it('should toggle dark mode on', () => {
    useThemeStore.getState().toggleTheme();

    expect(useThemeStore.getState().darkMode).toBe(true);
    expect(document.documentElement.classList.contains('dark')).toBe(true);
  });

  it('should toggle dark mode off', () => {
    useThemeStore.setState({ darkMode: true });
    document.documentElement.classList.add('dark');

    useThemeStore.getState().toggleTheme();

    expect(useThemeStore.getState().darkMode).toBe(false);
    expect(document.documentElement.classList.contains('dark')).toBe(false);
  });

  it('should toggle theme back and forth', () => {
    const { toggleTheme } = useThemeStore.getState();

    toggleTheme(); // → dark
    expect(useThemeStore.getState().darkMode).toBe(true);

    toggleTheme(); // → light
    expect(useThemeStore.getState().darkMode).toBe(false);

    toggleTheme(); // → dark again
    expect(useThemeStore.getState().darkMode).toBe(true);
  });

  it('should set dark mode explicitly via setDarkMode', () => {
    useThemeStore.getState().setDarkMode(true);
    expect(useThemeStore.getState().darkMode).toBe(true);
    expect(document.documentElement.classList.contains('dark')).toBe(true);

    useThemeStore.getState().setDarkMode(false);
    expect(useThemeStore.getState().darkMode).toBe(false);
    expect(document.documentElement.classList.contains('dark')).toBe(false);
  });
});
