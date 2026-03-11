import i18n from '../i18n';

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
    label: 'admin:userMgmt.permGroup.certManagement',
    permissions: [
      { value: 'icao:read', label: 'admin:userMgmt.perm.icaoRead', desc: 'admin:userMgmt.permDesc.icaoRead' },
      { value: 'upload:write', label: 'admin:userMgmt.perm.uploadWrite', desc: 'admin:userMgmt.permDesc.uploadWrite' },
      { value: 'cert:read', label: 'admin:userMgmt.perm.certRead', desc: 'admin:userMgmt.permDesc.certRead' },
      { value: 'cert:export', label: 'admin:userMgmt.perm.certExport', desc: 'admin:userMgmt.permDesc.certExport' },
      { value: 'upload:read', label: 'admin:userMgmt.perm.uploadRead', desc: 'admin:userMgmt.permDesc.uploadRead' },
      { value: 'sync:read', label: 'admin:userMgmt.perm.syncRead', desc: 'admin:userMgmt.permDesc.syncRead' },
    ],
  },
  {
    label: 'admin:userMgmt.permGroup.forgeryDetection',
    permissions: [
      { value: 'pa:verify', label: 'admin:userMgmt.perm.paVerify', desc: 'admin:userMgmt.permDesc.paVerify' },
      { value: 'pa:read', label: 'admin:userMgmt.perm.paRead', desc: 'admin:userMgmt.permDesc.paRead' },
    ],
  },
  {
    label: 'admin:userMgmt.permGroup.reportAnalysis',
    permissions: [
      { value: 'report:read', label: 'admin:userMgmt.perm.reportRead', desc: 'admin:userMgmt.permDesc.reportRead' },
      { value: 'ai:read', label: 'admin:userMgmt.perm.aiRead', desc: 'admin:userMgmt.permDesc.aiRead' },
    ],
  },
  {
    label: 'admin:userMgmt.permGroup.systemAdmin',
    permissions: [
      { value: 'api-client:manage', label: 'admin:userMgmt.perm.apiClientManage', desc: 'admin:userMgmt.permDesc.apiClientManage' },
    ],
  },
];

/** Flat list of all permissions */
export const AVAILABLE_PERMISSIONS = PERMISSION_GROUPS.flatMap(g => g.permissions);

/** Look up translated label for a permission code */
export const getPermissionLabel = (code: string): string => {
  const found = AVAILABLE_PERMISSIONS.find(p => p.value === code);
  return found ? i18n.t(found.label) : code;
};
