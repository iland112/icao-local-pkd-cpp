export function Footer() {
  return (
    <footer className="py-2 px-6 border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800">
      <div className="flex items-center justify-end gap-4 text-xs text-gray-400 dark:text-gray-500">
        <span className="flex items-center gap-1">
          <span className="inline-block w-1.5 h-1.5 bg-green-500 rounded-full status-pulse"></span>
          System Operational
        </span>
        <span>|</span>
        <span>ICAO Doc 9303 Compliant</span>
      </div>
    </footer>
  );
}

export default Footer;
