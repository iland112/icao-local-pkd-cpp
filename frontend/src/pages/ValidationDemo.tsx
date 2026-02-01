/**
 * Sprint 3 Task 3.6: Trust Chain Visualization Demo Page
 *
 * Demo page showing trust chain visualization with sample data
 */

import { useState } from 'react';
import { Shield, FileText, AlertCircle } from 'lucide-react';
import { TrustChainVisualization } from '@/components/TrustChainVisualization';

// Sample trust chain paths for demonstration
const SAMPLE_CHAINS = [
  {
    id: 1,
    name: 'Single Level (Self-Signed CSCA)',
    country: 'KR',
    certType: 'CSCA',
    trustChainPath: 'CN=CSCA-KOREA-2025,O=Ministry of Foreign Affairs,C=KR',
    trustChainValid: true,
    description: 'Self-signed root CSCA certificate'
  },
  {
    id: 2,
    name: '2-Level Chain (DSC → CSCA)',
    country: 'KR',
    certType: 'DSC',
    trustChainPath: 'DSC → CN=CSCA-KOREA-2025,O=Ministry of Foreign Affairs,C=KR',
    trustChainValid: true,
    description: 'Standard DSC signed by CSCA'
  },
  {
    id: 3,
    name: '3-Level Chain (Latvia Key Rotation)',
    country: 'LV',
    certType: 'DSC',
    trustChainPath: 'DSC → serialNumber=003,CN=CSCA Latvia,O=Latvian State,C=LV → serialNumber=002,CN=CSCA Latvia,O=Latvian State,C=LV',
    trustChainValid: true,
    description: 'DSC validated through link certificate (key rotation)'
  },
  {
    id: 4,
    name: '4-Level Chain (Multiple Key Rotations)',
    country: 'LV',
    certType: 'DSC',
    trustChainPath: 'DSC → serialNumber=003,CN=CSCA Latvia,O=Latvian State,C=LV → serialNumber=002,CN=CSCA Latvia,O=Latvian State,C=LV → serialNumber=001,CN=CSCA Latvia,O=Latvian State,C=LV',
    trustChainValid: true,
    description: 'DSC validated through multiple link certificates'
  },
  {
    id: 5,
    name: 'Invalid Chain (CSCA Not Found)',
    country: 'XX',
    certType: 'DSC',
    trustChainPath: '',
    trustChainValid: false,
    description: 'DSC with no matching CSCA'
  },
  {
    id: 6,
    name: 'Luxembourg Organizational Change',
    country: 'LU',
    certType: 'DSC',
    trustChainPath: 'DSC → CN=INCERT,O=INCERT public agency,C=LU → CN=CSCA-LUXEMBOURG,O=Ministry of Foreign Affairs,C=LU',
    trustChainValid: true,
    description: 'DSC validated through organizational change link certificate'
  },
  {
    id: 7,
    name: 'Philippines Key Rotation',
    country: 'PH',
    certType: 'DSC',
    trustChainPath: 'DSC → CN=CSCA01008,O=DFA,C=PH → CN=CSCA01007,O=DFA,C=PH',
    trustChainValid: true,
    description: 'DSC validated through Philippines CSCA key rotation'
  }
];

export function ValidationDemo() {
  const [selectedChain, setSelectedChain] = useState(SAMPLE_CHAINS[3]); // Default: 4-level chain

  return (
    <div className="min-h-screen bg-gradient-to-br from-gray-50 to-gray-100 dark:from-gray-900 dark:to-gray-800 p-6">
      <div className="max-w-7xl mx-auto">
        {/* Header */}
        <div className="mb-8">
          <div className="flex items-center gap-3 mb-2">
            <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-blue-500 to-cyan-500 flex items-center justify-center shadow-lg">
              <Shield className="w-7 h-7 text-white" />
            </div>
            <div>
              <h1 className="text-3xl font-bold text-gray-900 dark:text-white">
                Trust Chain Visualization Demo
              </h1>
              <p className="text-sm text-gray-600 dark:text-gray-400">
                Sprint 3 Task 3.6 - Trust Chain Path Display
              </p>
            </div>
          </div>

          {/* Info Banner */}
          <div className="mt-4 p-4 bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-800 rounded-lg">
            <div className="flex items-start gap-3">
              <AlertCircle className="w-5 h-5 text-blue-600 dark:text-blue-400 flex-shrink-0 mt-0.5" />
              <div className="text-sm text-blue-900 dark:text-blue-100">
                <p className="font-semibold mb-1">About Trust Chain Visualization</p>
                <p>
                  This component displays the certificate trust chain path, showing how a DSC (Document Signer Certificate)
                  is validated through one or more CSCA (Country Signing CA) certificates, including link certificates used
                  during key rotations or organizational changes.
                </p>
              </div>
            </div>
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          {/* Left Panel: Chain Selection */}
          <div className="lg:col-span-1">
            <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6 sticky top-6">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
                <FileText className="w-5 h-5" />
                Sample Chains
              </h2>

              <div className="space-y-2">
                {SAMPLE_CHAINS.map((chain) => (
                  <button
                    key={chain.id}
                    onClick={() => setSelectedChain(chain)}
                    className={`w-full text-left p-3 rounded-lg border transition-all ${
                      selectedChain.id === chain.id
                        ? 'bg-blue-50 dark:bg-blue-900/30 border-blue-300 dark:border-blue-700'
                        : 'border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700/50'
                    }`}
                  >
                    <div className="flex items-start justify-between mb-1">
                      <span className="text-sm font-medium text-gray-900 dark:text-white">
                        {chain.name}
                      </span>
                      {chain.trustChainValid ? (
                        <Shield className="w-4 h-4 text-green-500 flex-shrink-0" />
                      ) : (
                        <Shield className="w-4 h-4 text-red-500 flex-shrink-0" />
                      )}
                    </div>
                    <div className="flex items-center gap-2 text-xs text-gray-600 dark:text-gray-400">
                      <span
                        className={`px-2 py-0.5 rounded ${
                          chain.certType === 'CSCA'
                            ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-300'
                            : 'bg-blue-100 dark:bg-blue-900/30 text-blue-700 dark:text-blue-300'
                        }`}
                      >
                        {chain.certType}
                      </span>
                      <span className="px-2 py-0.5 rounded bg-gray-100 dark:bg-gray-700">
                        {chain.country}
                      </span>
                    </div>
                    <p className="text-xs text-gray-500 dark:text-gray-400 mt-2">
                      {chain.description}
                    </p>
                  </button>
                ))}
              </div>

              {/* Implementation Notes */}
              <div className="mt-6 pt-6 border-t border-gray-200 dark:border-gray-700">
                <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-2">
                  Implementation Notes
                </h3>
                <ul className="text-xs text-gray-600 dark:text-gray-400 space-y-1">
                  <li>• Compact mode for table cells</li>
                  <li>• Full mode for detail dialogs</li>
                  <li>• Automatic DN parsing (CN, serialNumber)</li>
                  <li>• Link certificate detection</li>
                  <li>• Responsive design (mobile-friendly)</li>
                </ul>
              </div>
            </div>
          </div>

          {/* Right Panel: Visualization */}
          <div className="lg:col-span-2 space-y-6">
            {/* Selected Chain Info */}
            <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">
                Selected Chain Details
              </h2>

              <div className="grid grid-cols-2 gap-4 mb-4">
                <div>
                  <span className="text-sm text-gray-600 dark:text-gray-400">Name:</span>
                  <p className="font-medium text-gray-900 dark:text-white">{selectedChain.name}</p>
                </div>
                <div>
                  <span className="text-sm text-gray-600 dark:text-gray-400">Country:</span>
                  <p className="font-medium text-gray-900 dark:text-white">{selectedChain.country}</p>
                </div>
                <div>
                  <span className="text-sm text-gray-600 dark:text-gray-400">Certificate Type:</span>
                  <p className="font-medium text-gray-900 dark:text-white">{selectedChain.certType}</p>
                </div>
                <div>
                  <span className="text-sm text-gray-600 dark:text-gray-400">Validation Status:</span>
                  <p
                    className={`font-medium ${
                      selectedChain.trustChainValid
                        ? 'text-green-600 dark:text-green-400'
                        : 'text-red-600 dark:text-red-400'
                    }`}
                  >
                    {selectedChain.trustChainValid ? '✓ Valid' : '✗ Invalid'}
                  </p>
                </div>
              </div>

              <div>
                <span className="text-sm text-gray-600 dark:text-gray-400">Description:</span>
                <p className="text-sm text-gray-700 dark:text-gray-300 mt-1">
                  {selectedChain.description}
                </p>
              </div>

              {/* Raw Trust Chain Path */}
              {selectedChain.trustChainPath && (
                <div className="mt-4 p-3 bg-gray-50 dark:bg-gray-900/50 rounded-lg border border-gray-200 dark:border-gray-700">
                  <span className="text-xs text-gray-600 dark:text-gray-400 font-mono">
                    trust_chain_path:
                  </span>
                  <p className="text-xs font-mono text-gray-900 dark:text-white mt-1 break-all">
                    "{selectedChain.trustChainPath}"
                  </p>
                </div>
              )}
            </div>

            {/* Full Visualization */}
            <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">
                Full Visualization (Detail Dialog Mode)
              </h2>

              {selectedChain.trustChainPath ? (
                <TrustChainVisualization
                  trustChainPath={selectedChain.trustChainPath}
                  trustChainValid={selectedChain.trustChainValid}
                  compact={false}
                />
              ) : (
                <div className="py-8 text-center text-gray-500 dark:text-gray-400">
                  <Shield className="w-12 h-12 mx-auto mb-3 opacity-50" />
                  <p className="text-sm">No trust chain path available</p>
                  <p className="text-xs mt-1">
                    This certificate could not be validated (CSCA not found)
                  </p>
                </div>
              )}
            </div>

            {/* Compact Visualization */}
            <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
              <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">
                Compact Visualization (Table Cell Mode)
              </h2>

              {selectedChain.trustChainPath ? (
                <div className="p-3 bg-gray-50 dark:bg-gray-900/50 rounded-lg border border-gray-200 dark:border-gray-700">
                  <TrustChainVisualization
                    trustChainPath={selectedChain.trustChainPath}
                    trustChainValid={selectedChain.trustChainValid}
                    compact={true}
                  />
                </div>
              ) : (
                <div className="p-3 bg-gray-50 dark:bg-gray-900/50 rounded-lg border border-gray-200 dark:border-gray-700">
                  <span className="text-xs text-gray-500 dark:text-gray-400">
                    No trust chain available
                  </span>
                </div>
              )}

              <p className="text-xs text-gray-600 dark:text-gray-400 mt-3">
                This compact mode is designed for table cells in Certificate Search and Upload Dashboard pages.
                It shows a single-line view with truncated DNs and icons.
              </p>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default ValidationDemo;
