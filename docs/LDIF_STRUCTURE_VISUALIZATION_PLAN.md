# LDIF Structure Visualization - Implementation Plan

**Version**: v2.2.2 (Planned)
**Created**: 2026-01-31
**Status**: 📋 Planning Phase
**Implementation**: Next Session

---

## Executive Summary

Implement LDIF file structure visualization in the upload detail dialog, providing immigration officers with detailed insight into LDAP dump file contents. The feature will dynamically change the tab name based on file format ("LDIF 구조" for LDIF, "Master List 구조" for ML) and display LDIF entries in a hierarchical tree structure similar to DTI format.

---

## Background

### Current State
- Upload detail dialog has "Master List 구조" tab (only for ML files)
- Shows ASN.1/TLV structure for Master List files
- LDIF files have no structure visualization

### Problem Statement
- LDIF files (collection-001, 002, 003) lack structure visibility
- Users cannot inspect LDIF entry hierarchy
- Binary attributes (Master List CMS in collection-002) are hidden
- Tab name "Master List 구조" is misleading for LDIF files

### Requirements
1. **Dynamic Tab Name**: Show "LDIF 구조" for LDIF files, "Master List 구조" for ML files
2. **LDIF Tree Structure**: Display LDIF entries in hierarchical tree format
3. **DN Hierarchy**: Show DN components and entry structure
4. **Attribute Display**: Show all entry attributes with values
5. **Binary Data Handling**: Indicate binary attributes (e.g., Master List CMS)
6. **Collection-002 Support**: Handle Master List binary entries correctly

---

## Technical Design

### 1. Backend API Development

#### 1.1 LDIF Structure Parser

**New Files**:
- `services/pkd-management/src/common/ldif_structure_parser.h`
- `services/pkd-management/src/common/ldif_structure_parser.cpp`

**Functionality**:
```cpp
class LdifStructureParser {
public:
    struct LdifAttribute {
        std::string name;
        std::string value;
        bool isBinary;
        size_t binarySize;
    };

    struct LdifEntry {
        std::string dn;
        std::vector<LdifAttribute> attributes;
        std::string objectClass;
        int lineNumber;
    };

    struct LdifStructure {
        std::vector<LdifEntry> entries;
        int totalEntries;
        int totalAttributes;
        std::map<std::string, int> objectClassCounts;
    };

    // Parse LDIF file and extract structure
    static LdifStructure parse(const std::string& filePath, int maxEntries = 100);

    // Parse binary attribute (base64 encoded)
    static std::pair<bool, size_t> parseBinaryAttribute(const std::string& value);

    // Extract DN components
    static std::vector<std::string> extractDnComponents(const std::string& dn);
};
```

**Key Features**:
- Parse LDIF entries up to configurable limit (default: 100 entries)
- Detect binary attributes (base64 encoded, `::` syntax)
- Extract DN components for hierarchy display
- Count entries by objectClass
- Handle collection-002 Master List binary data

#### 1.2 API Endpoint

**New Endpoint**: `GET /api/upload/{uploadId}/ldif-structure`

**Response Format**:
```json
{
  "success": true,
  "data": {
    "entries": [
      {
        "dn": "cn=CSCA-FRANCE,o=csca,c=FR,dc=data,dc=download,dc=pkd,...",
        "objectClass": "pkdCertificate",
        "attributes": [
          {
            "name": "cn",
            "value": "CSCA-FRANCE",
            "isBinary": false
          },
          {
            "name": "userCertificate;binary",
            "value": "[Binary Data: 1234 bytes]",
            "isBinary": true,
            "binarySize": 1234
          }
        ],
        "lineNumber": 15
      },
      {
        "dn": "cn=ML-2024-12,o=ml,c=KR,dc=data,dc=download,dc=pkd,...",
        "objectClass": "pkdMasterList",
        "attributes": [
          {
            "name": "cn",
            "value": "ML-2024-12",
            "isBinary": false
          },
          {
            "name": "pkdMasterListContent;binary",
            "value": "[Binary CMS Data: 45678 bytes]",
            "isBinary": true,
            "binarySize": 45678
          }
        ],
        "lineNumber": 150
      }
    ],
    "totalEntries": 5017,
    "displayedEntries": 100,
    "totalAttributes": 15051,
    "objectClassCounts": {
      "pkdCertificate": 4991,
      "pkdMasterList": 26
    },
    "truncated": true
  }
}
```

**Implementation Location**: `services/pkd-management/src/main.cpp`

```cpp
// GET /api/upload/:id/ldif-structure
app().registerHandler(
    "/api/upload/{uploadId}/ldif-structure",
    [](const HttpRequestPtr& req,
       std::function<void(const HttpResponsePtr&)>&& callback,
       const std::string& uploadId) {
        // 1. Get upload record to find file path
        // 2. Parse LDIF file using LdifStructureParser
        // 3. Return JSON response with structure
    },
    {Get}
);
```

#### 1.3 Environment Configuration

**File**: `docker/docker-compose.yaml`

```yaml
environment:
  - LDIF_MAX_ENTRIES=100  # Max entries to display in structure view
```

---

### 2. Frontend UI Development

#### 2.1 Dynamic Tab Name

**File**: `frontend/src/pages/UploadHistory.tsx`

**Current**:
```tsx
<button>Master List 구조</button>
```

**New**:
```tsx
{/* Show structure tab for ML or LDIF files */}
{(selectedUpload.fileFormat === 'ML' ||
  selectedUpload.fileFormat === 'MASTER_LIST' ||
  selectedUpload.fileFormat === 'LDIF') && (
  <button onClick={() => setActiveTab('structure')}>
    {selectedUpload.fileFormat === 'LDIF' ? 'LDIF 구조' : 'Master List 구조'}
  </button>
)}
```

#### 2.2 LdifStructure Component

**New File**: `frontend/src/components/LdifStructure.tsx`

**Component Structure**:
```tsx
interface LdifAttribute {
  name: string;
  value: string;
  isBinary: boolean;
  binarySize?: number;
}

interface LdifEntry {
  dn: string;
  objectClass: string;
  attributes: LdifAttribute[];
  lineNumber: number;
}

interface LdifStructureData {
  entries: LdifEntry[];
  totalEntries: number;
  displayedEntries: number;
  totalAttributes: number;
  objectClassCounts: Record<string, number>;
  truncated: boolean;
}

export const LdifStructure: React.FC<{ uploadId: string }> = ({ uploadId }) => {
  // State
  const [data, setData] = useState<LdifStructureData | null>(null);
  const [loading, setLoading] = useState(true);
  const [expandedEntries, setExpandedEntries] = useState<Set<number>>(new Set());
  const [maxEntries, setMaxEntries] = useState(100);

  // Fetch LDIF structure
  useEffect(() => {
    fetchLdifStructure();
  }, [uploadId, maxEntries]);

  // Render tree structure
  return (
    <div className="space-y-4">
      {/* Summary section */}
      <div className="grid grid-cols-3 gap-4">
        <div>총 엔트리: {data.totalEntries}개</div>
        <div>표시 중: {data.displayedEntries}개</div>
        <div>총 속성: {data.totalAttributes}개</div>
      </div>

      {/* ObjectClass counts */}
      <div>
        {Object.entries(data.objectClassCounts).map(([cls, count]) => (
          <span key={cls}>{cls}: {count}개</span>
        ))}
      </div>

      {/* Entry limit selector */}
      <select onChange={(e) => setMaxEntries(Number(e.target.value))}>
        <option value="50">50 엔트리</option>
        <option value="100">100 엔트리</option>
        <option value="500">500 엔트리</option>
        <option value="1000">1000 엔트리</option>
      </select>

      {/* Entry tree */}
      <div className="font-mono text-sm space-y-2">
        {data.entries.map((entry, idx) => (
          <LdifEntryNode
            key={idx}
            entry={entry}
            index={idx}
            expanded={expandedEntries.has(idx)}
            onToggle={() => toggleEntry(idx)}
          />
        ))}
      </div>

      {data.truncated && (
        <div className="text-yellow-600">
          ⚠️ 표시 제한: {data.totalEntries}개 중 {data.displayedEntries}개만 표시됨
        </div>
      )}
    </div>
  );
};
```

#### 2.3 LdifEntryNode Component

**Functionality**:
```tsx
const LdifEntryNode: React.FC<{
  entry: LdifEntry;
  index: number;
  expanded: boolean;
  onToggle: () => void;
}> = ({ entry, index, expanded, onToggle }) => {
  return (
    <div className="border border-gray-300 rounded-lg">
      {/* Entry header - DN */}
      <div
        className="flex items-center gap-2 p-2 cursor-pointer hover:bg-gray-50"
        onClick={onToggle}
      >
        {expanded ? <ChevronDown /> : <ChevronRight />}
        <span className="text-blue-600">{entry.dn}</span>
        <span className="text-xs text-gray-500">
          ({entry.objectClass})
        </span>
      </div>

      {/* Entry attributes */}
      {expanded && (
        <div className="pl-6 pr-2 pb-2 space-y-1">
          {entry.attributes.map((attr, attrIdx) => (
            <div key={attrIdx} className="flex gap-2">
              <span className="text-green-600">{attr.name}:</span>
              {attr.isBinary ? (
                <span className="text-orange-600">
                  [Binary Data: {attr.binarySize} bytes]
                </span>
              ) : (
                <span className="text-gray-700 break-all">{attr.value}</span>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
};
```

**Visual Design**:
```
┌─ Entry #1 ──────────────────────────────────────────┐
│ > cn=CSCA-FRANCE,o=csca,c=FR,dc=data,... (pkdCert) │
└─────────────────────────────────────────────────────┘

┌─ Entry #2 (Expanded) ───────────────────────────────┐
│ ∨ cn=ML-2024-12,o=ml,c=KR,dc=data,... (pkdML)      │
│   cn: ML-2024-12                                    │
│   objectClass: pkdMasterList                        │
│   pkdMasterListContent;binary: [Binary: 45678 B]   │
│   createTimestamp: 20241215120000Z                  │
└─────────────────────────────────────────────────────┘
```

#### 2.4 UploadHistory Tab Content

**File**: `frontend/src/pages/UploadHistory.tsx`

```tsx
{/* Structure Tab - Dynamic content based on file format */}
{activeTab === 'structure' && (
  <div className="max-h-[600px] overflow-y-auto">
    {selectedUpload.fileFormat === 'LDIF' ? (
      <LdifStructure uploadId={selectedUpload.id} />
    ) : (
      <MasterListStructure uploadId={selectedUpload.id} />
    )}
  </div>
)}
```

---

### 3. API Service Layer

**File**: `frontend/src/services/pkdApi.ts`

```typescript
// LDIF Structure API
export const uploadHistoryApi = {
  // ... existing methods ...

  getLdifStructure: async (
    uploadId: string,
    maxEntries: number = 100
  ): Promise<ApiResponse<LdifStructureData>> => {
    const response = await fetch(
      `${API_BASE_URL}/api/upload/${uploadId}/ldif-structure?maxEntries=${maxEntries}`
    );
    return response.json();
  },
};
```

**Type Definitions**: `frontend/src/types/index.ts`

```typescript
export interface LdifAttribute {
  name: string;
  value: string;
  isBinary: boolean;
  binarySize?: number;
}

export interface LdifEntry {
  dn: string;
  objectClass: string;
  attributes: LdifAttribute[];
  lineNumber: number;
}

export interface LdifStructureData {
  entries: LdifEntry[];
  totalEntries: number;
  displayedEntries: number;
  totalAttributes: number;
  objectClassCounts: Record<string, number>;
  truncated: boolean;
}
```

---

## Implementation Phases

### Phase 1: Backend Development (2-3 hours)

**Tasks**:
1. Create `LdifStructureParser` class
   - [ ] Parse LDIF entries
   - [ ] Detect binary attributes (base64, `::` syntax)
   - [ ] Extract DN components
   - [ ] Count objectClass entries
   - [ ] Handle entry limit

2. Add API endpoint
   - [ ] `GET /api/upload/{uploadId}/ldif-structure`
   - [ ] File path resolution
   - [ ] Error handling (file not found, parse errors)
   - [ ] JSON response formatting

3. Environment configuration
   - [ ] Add `LDIF_MAX_ENTRIES` to docker-compose
   - [ ] Default: 100 entries

**Files to Create**:
- `services/pkd-management/src/common/ldif_structure_parser.h`
- `services/pkd-management/src/common/ldif_structure_parser.cpp`

**Files to Modify**:
- `services/pkd-management/src/main.cpp` (add endpoint)
- `services/pkd-management/CMakeLists.txt` (add parser to build)
- `docker/docker-compose.yaml` (add env var)

### Phase 2: Frontend Development (2-3 hours)

**Tasks**:
1. Create `LdifStructure` component
   - [ ] Fetch LDIF structure API
   - [ ] Summary section (totals, objectClass counts)
   - [ ] Entry limit selector
   - [ ] Loading/error states
   - [ ] Truncation warning

2. Create `LdifEntryNode` component
   - [ ] Expandable entry card
   - [ ] DN display with objectClass
   - [ ] Attribute list with binary indicators
   - [ ] Color-coded attribute names/values

3. Update `UploadHistory` tab logic
   - [ ] Dynamic tab name (LDIF 구조 / Master List 구조)
   - [ ] Conditional rendering (LdifStructure vs MasterListStructure)
   - [ ] Show structure tab for LDIF files

4. Add API service method
   - [ ] `uploadHistoryApi.getLdifStructure()`
   - [ ] TypeScript interfaces

**Files to Create**:
- `frontend/src/components/LdifStructure.tsx`
- `frontend/src/components/LdifEntryNode.tsx` (optional, can be inline)

**Files to Modify**:
- `frontend/src/pages/UploadHistory.tsx`
- `frontend/src/services/pkdApi.ts`
- `frontend/src/types/index.ts`

### Phase 3: Testing & Validation (1-2 hours)

**Test Cases**:

1. **Collection-001 (DSC Certificates)**
   - [ ] Upload LDIF file
   - [ ] Open upload detail dialog
   - [ ] Verify "LDIF 구조" tab appears
   - [ ] Click tab and view entries
   - [ ] Verify DN hierarchy display
   - [ ] Verify certificate attributes shown
   - [ ] Test entry limit selector (50/100/500/1000)

2. **Collection-002 (Master Lists with Binary CMS)**
   - [ ] Upload LDIF file
   - [ ] Verify tab appears
   - [ ] Expand entry with Master List
   - [ ] Verify binary CMS indicator: `[Binary CMS Data: XXXXX bytes]`
   - [ ] Verify objectClass counts (26 Master Lists)
   - [ ] Test with different entry limits

3. **Collection-003 (DSC_NC Certificates)**
   - [ ] Upload LDIF file
   - [ ] Verify structure display
   - [ ] Verify DSC_NC specific attributes

4. **Master List File (Existing)**
   - [ ] Upload ML file
   - [ ] Verify "Master List 구조" tab (not "LDIF 구조")
   - [ ] Verify ASN.1 structure still works

**Performance Testing**:
- [ ] Large LDIF files (5000+ entries)
- [ ] Entry limit performance (1000 entries)
- [ ] Binary data handling (large CMS)

---

## Expected Results

### Before
- LDIF files have no structure visualization
- Tab always says "Master List 구조" (even for LDIF)
- Binary Master List data in collection-002 is invisible

### After
- ✅ Dynamic tab name based on file format
- ✅ LDIF entries displayed in tree structure
- ✅ DN hierarchy visible
- ✅ All attributes shown with values
- ✅ Binary data clearly indicated with size
- ✅ ObjectClass statistics
- ✅ Configurable entry limit (50/100/500/1000)
- ✅ Collection-002 Master List CMS visible

---

## Special Considerations

### Collection-002 Binary Master Lists

**LDIF Format**:
```ldif
dn: cn=ML-KR-2024-12,o=ml,c=KR,dc=data,dc=download,dc=pkd,...
objectClass: pkdMasterList
cn: ML-KR-2024-12
pkdMasterListContent;binary:: MIIBogYJKoZIhvcNAQcCoIIBkzCCAY8CAQExDz...
  ANBgkqhkiG9w0BAQsFADB7MQswCQYDVQQGEwJLUjENMAsGA1UEChMES0RQRDEUMBIG...
  [thousands of base64 characters]
```

**Parsing Strategy**:
1. Detect `pkdMasterListContent;binary::` attribute
2. Identify base64 encoded data (double colon `::`)
3. Calculate decoded size (base64 length * 3/4)
4. Display as: `[Binary CMS Data: 45678 bytes]`
5. Do NOT decode or process the CMS data (too large for display)

**Frontend Display**:
```tsx
{attr.name === 'pkdMasterListContent;binary' && (
  <div className="flex items-center gap-2">
    <span className="text-green-600">{attr.name}:</span>
    <span className="text-orange-600 font-semibold">
      [Binary CMS Data: {attr.binarySize.toLocaleString()} bytes]
    </span>
    <span className="text-xs text-gray-500">
      (Master List Signed CMS)
    </span>
  </div>
)}
```

---

## Files Summary

### Backend
**New Files**:
- `services/pkd-management/src/common/ldif_structure_parser.h`
- `services/pkd-management/src/common/ldif_structure_parser.cpp`

**Modified Files**:
- `services/pkd-management/src/main.cpp` (new endpoint)
- `services/pkd-management/CMakeLists.txt` (add parser)
- `docker/docker-compose.yaml` (LDIF_MAX_ENTRIES)

### Frontend
**New Files**:
- `frontend/src/components/LdifStructure.tsx`

**Modified Files**:
- `frontend/src/pages/UploadHistory.tsx` (dynamic tab, conditional rendering)
- `frontend/src/services/pkdApi.ts` (new API method)
- `frontend/src/types/index.ts` (new interfaces)

---

## Documentation

**New Documents**:
- `docs/LDIF_STRUCTURE_VISUALIZATION_IMPLEMENTATION.md` (completion report)

**Update Documents**:
- `CLAUDE.md` (add v2.2.2 section)
- `docs/DEVELOPMENT_GUIDE.md` (LDIF structure feature)

---

## Deployment

```bash
# Backend rebuild
cd docker
docker-compose build --no-cache pkd-management

# Frontend rebuild
docker-compose build frontend

# Restart containers
docker-compose up -d --force-recreate pkd-management frontend
docker-compose restart api-gateway

# Verify
docker-compose ps
```

---

## Success Criteria

- ✅ LDIF files show "LDIF 구조" tab
- ✅ Master List files show "Master List 구조" tab
- ✅ LDIF entries displayed in tree format
- ✅ DN and attributes visible
- ✅ Binary data indicated with size
- ✅ Collection-002 Master List CMS detected and displayed
- ✅ Entry limit selector works (50/100/500/1000)
- ✅ ObjectClass statistics accurate
- ✅ All three collections upload and display correctly

---

## Next Session TODO

1. Start Phase 1: Backend Development
2. Implement LdifStructureParser
3. Add API endpoint
4. Test with collection-002 LDIF
5. Move to Phase 2: Frontend Development

---

## Related Issues

- [MASTER_LIST_PROCESSING_GUIDE.md](MASTER_LIST_PROCESSING_GUIDE.md) - Master List handling reference
- [ASN1_PARSER_IMPLEMENTATION.md](ASN1_PARSER_IMPLEMENTATION.md) - Similar structure visualization
- [DUPLICATE_CERTIFICATE_TAB_UI.md](DUPLICATE_CERTIFICATE_TAB_UI.md) - Tab-based UI pattern
