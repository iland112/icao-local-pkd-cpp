export function Footer() {
  const currentYear = new Date().getFullYear();

  return (
    <footer className="py-4 px-6 border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800">
      <div className="flex flex-col sm:flex-row items-center justify-between gap-2 text-sm text-gray-500 dark:text-gray-400">
        <div className="flex items-center gap-4">
          <span>&copy; {currentYear} SmartCore Inc. All rights reserved.</span>
        </div>
        <div className="flex items-center gap-4">
          <span className="flex items-center gap-1">
            <span className="inline-block w-2 h-2 bg-green-500 rounded-full status-pulse"></span>
            System Operational
          </span>
          <span>|</span>
          <span>ICAO Doc 9303 Compliant</span>
        </div>
      </div>
    </footer>
  );
}

export default Footer;
