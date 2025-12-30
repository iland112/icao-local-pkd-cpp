import { useEffect } from 'react';
import { useLocation } from 'react-router-dom';

// Hook to initialize Preline components after navigation
export const usePreline = () => {
  const location = useLocation();

  useEffect(() => {
    // Dynamically import and initialize Preline
    const initPreline = async () => {
      try {
        const { HSStaticMethods } = await import('preline/preline');
        // Initialize all Preline components
        HSStaticMethods.autoInit();
      } catch (error) {
        console.warn('Preline initialization failed:', error);
      }
    };

    // Small delay to ensure DOM is ready
    const timer = setTimeout(initPreline, 100);

    return () => clearTimeout(timer);
  }, [location.pathname]);
};

export default usePreline;
