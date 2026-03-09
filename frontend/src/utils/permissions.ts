/** Permission definition */
export interface PermissionDef {
  value: string;
  label: string;
  desc: string;
}

/** Permission group matching sidebar menu structure */
export interface PermissionGroup {
  label: string;
  permissions: PermissionDef[];
}

export const PERMISSION_GROUPS: PermissionGroup[] = [
  {
    label: '인증서 관리',
    permissions: [
      { value: 'icao:read', label: 'ICAO 버전 상태', desc: 'ICAO PKD 버전 모니터링' },
      { value: 'upload:write', label: '파일/인증서 업로드', desc: 'LDIF, Master List, 인증서 업로드' },
      { value: 'cert:read', label: '인증서 조회', desc: '인증서 검색 및 상세 조회' },
      { value: 'cert:export', label: '인증서 내보내기', desc: '인증서 PEM/DER 파일 내보내기' },
      { value: 'upload:read', label: '업로드 이력', desc: '업로드 이력 조회' },
      { value: 'sync:read', label: '동기화 상태', desc: 'DB-LDAP 동기화 상태 조회' },
    ],
  },
  {
    label: '위·변조 검사',
    permissions: [
      { value: 'pa:verify', label: 'PA 검증 수행', desc: 'Passive Authentication 검증' },
      { value: 'pa:read', label: 'PA 검증 이력', desc: 'PA 검증 이력 조회' },
    ],
  },
  {
    label: '보고서 & 분석',
    permissions: [
      { value: 'report:read', label: '인증서 보고서', desc: '인증서 통계, Trust Chain, CRL, DSC_NC 보고서' },
      { value: 'ai:read', label: 'AI 분석', desc: 'AI 인증서 분석 결과 조회' },
    ],
  },
  {
    label: '시스템 관리',
    permissions: [
      { value: 'api-client:manage', label: 'API 클라이언트 관리', desc: 'API 클라이언트 생성, 수정, 삭제, 키 재발급' },
    ],
  },
];

/** Flat list of all permissions */
export const AVAILABLE_PERMISSIONS = PERMISSION_GROUPS.flatMap(g => g.permissions);

/** Look up label for a permission code */
export const getPermissionLabel = (code: string): string => {
  const found = AVAILABLE_PERMISSIONS.find(p => p.value === code);
  return found ? found.label : code;
};
