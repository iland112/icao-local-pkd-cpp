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
    label: 'PKD Management',
    permissions: [
      { value: 'icao:read', label: 'ICAO 버전 상태', desc: 'ICAO PKD 버전 모니터링' },
      { value: 'upload:write', label: '파일/인증서 업로드', desc: 'LDIF, Master List, 인증서 업로드' },
      { value: 'cert:read', label: '인증서 조회', desc: '인증서 검색 및 상세 조회' },
      { value: 'cert:export', label: '인증서 내보내기', desc: '인증서 PEM/DER 파일 내보내기' },
      { value: 'report:read', label: '보고서', desc: 'Trust Chain, CRL, DSC_NC 보고서' },
      { value: 'ai:read', label: 'AI 분석', desc: 'AI 인증서 분석 결과 조회' },
      { value: 'upload:read', label: '업로드 이력/통계', desc: '업로드 이력 및 통계 대시보드' },
      { value: 'sync:read', label: '동기화 상태', desc: 'DB-LDAP 동기화 상태 조회' },
    ],
  },
  {
    label: 'Passive Auth',
    permissions: [
      { value: 'pa:verify', label: 'PA 검증 수행', desc: 'Passive Authentication 검증' },
      { value: 'pa:read', label: 'PA 이력/통계', desc: 'PA 검증 이력 및 통계 조회' },
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
