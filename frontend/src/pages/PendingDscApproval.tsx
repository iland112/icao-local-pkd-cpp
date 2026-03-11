import { useTranslation } from 'react-i18next';
/**
 * Pending DSC Registration Approval Page
 * Admin reviews and approves/rejects DSC certificates extracted from PA verification
 */
import { useState, useEffect, useCallback } from 'react';
import { ShieldCheck, ShieldX, Clock, CheckCircle, XCircle, ChevronLeft, ChevronRight, RefreshCw } from 'lucide-react';
import { pendingDscApi, type PendingDsc } from '@/api/pendingDscApi';
import { toast } from '@/stores/toastStore';
import { formatDate } from '@/utils/dateFormat';
// Custom inline dialogs (ConfirmDialog doesn't support children/loading)

type StatusFilter = '' | 'PENDING' | 'APPROVED' | 'REJECTED';

export default function PendingDscApproval() {
  const { t } = useTranslation(['admin', 'common']);
  const [items, setItems] = useState<PendingDsc[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [size] = useState(10);
  const [statusFilter, setStatusFilter] = useState<StatusFilter>('PENDING');
  const [countryFilter, setCountryFilter] = useState('');
  const [loading, setLoading] = useState(true);

  const [stats, setStats] = useState({ pending: 0, approved: 0, rejected: 0, total: 0 });

  // Dialog state
  const [approveTarget, setApproveTarget] = useState<PendingDsc | null>(null);
  const [rejectTarget, setRejectTarget] = useState<PendingDsc | null>(null);
  const [detailTarget, setDetailTarget] = useState<PendingDsc | null>(null);
  const [comment, setComment] = useState('');
  const [actionLoading, setActionLoading] = useState(false);

  const fetchList = useCallback(async () => {
    setLoading(true);
    try {
      const res = await pendingDscApi.getList({
        status: statusFilter || undefined,
        country: countryFilter || undefined,
        page,
        size,
      });
      setItems(res.data?.data || []);
      setTotal(res.data?.total || 0);
    } catch {
      toast.error('조회 실패', 'DSC 대기 목록을 불러올 수 없습니다.');
    } finally {
      setLoading(false);
    }
  }, [statusFilter, countryFilter, page, size]);

  const fetchStats = useCallback(async () => {
    try {
      const res = await pendingDscApi.getStats();
      const d = res.data?.data;
      if (d) setStats({ pending: d.pendingCount, approved: d.approvedCount, rejected: d.rejectedCount, total: d.totalCount });
    } catch {
      /* non-critical */
    }
  }, []);

  useEffect(() => { fetchList(); }, [fetchList]);
  useEffect(() => { fetchStats(); }, [fetchStats]);

  const handleApprove = async () => {
    if (!approveTarget) return;
    setActionLoading(true);
    try {
      const res = await pendingDscApi.approve(approveTarget.id, comment || undefined);
      if (res.data?.success) {
        toast.success('승인 완료', `DSC 인증서가 등록되었습니다.${res.data.ldapStored ? t('admin.pendingDsc.ldapSaved') : ''}`);
        setApproveTarget(null);
        setComment('');
        fetchList();
        fetchStats();
      } else {
        toast.error('승인 실패', res.data?.message || t('common.error.unknownError_short'));
      }
    } catch (e) {
      const msg = (e as { response?: { data?: { error?: string } } })?.response?.data?.error || t('admin.apiClient.approveError');
      toast.error('승인 실패', msg);
    } finally {
      setActionLoading(false);
    }
  };

  const handleReject = async () => {
    if (!rejectTarget) return;
    setActionLoading(true);
    try {
      const res = await pendingDscApi.reject(rejectTarget.id, comment || undefined);
      if (res.data?.success) {
        toast.success('거부 완료', 'DSC 등록이 거부되었습니다.');
        setRejectTarget(null);
        setComment('');
        fetchList();
        fetchStats();
      } else {
        toast.error('거부 실패', res.data?.message || t('common.error.unknownError_short'));
      }
    } catch {
      toast.error('거부 실패', '거부 처리 중 오류가 발생했습니다.');
    } finally {
      setActionLoading(false);
    }
  };

  const totalPages = Math.ceil(total / size);

  const statusBadge = (s: string) => {
    switch (s) {
      case 'PENDING': return <span className="px-2 py-0.5 text-xs rounded-full bg-amber-100 text-amber-700 font-medium">{ t('monitoring:pool.idle') }</span>;
      case 'APPROVED': return <span className="px-2 py-0.5 text-xs rounded-full bg-green-100 text-green-700 font-medium">{t('common:button.approve')}</span>;
      case 'REJECTED': return <span className="px-2 py-0.5 text-xs rounded-full bg-red-100 text-red-700 font-medium">{t('common:button.reject')}</span>;
      default: return <span className="px-2 py-0.5 text-xs rounded-full bg-gray-100 text-gray-600">{s}</span>;
    }
  };

  const verificationBadge = (s: string) => {
    if (s === 'VALID') return <span className="px-1.5 py-0.5 text-xs rounded bg-green-50 text-green-600 font-medium">VALID</span>;
    return <span className="px-1.5 py-0.5 text-xs rounded bg-red-50 text-red-600 font-medium">{s}</span>;
  };

  return (
    <div className="w-full px-4 lg:px-6 py-4 space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-4">
          <div className="p-3 rounded-xl bg-gradient-to-br from-amber-500 to-orange-600 shadow-lg">
            <ShieldCheck className="w-7 h-7 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-gray-900">{t('pendingDsc.title')}</h1>
            <p className="text-sm text-gray-500">PA 검증에서 추출된 DSC 인증서의 등록을 관리합니다.</p>
          </div>
        </div>
        <button onClick={() => { fetchList(); fetchStats(); }} className="flex items-center gap-1.5 px-3 py-2 text-sm text-gray-600 hover:text-gray-800 bg-white border border-gray-200 rounded-lg hover:bg-gray-50 transition-colors">
          <RefreshCw className="w-4 h-4" /> {t('common.button.refresh')}
        </button>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
        <StatCard icon={<Clock className="w-5 h-5 text-amber-600" />} label={t('monitoring:pool.idle')} value={stats.pending} color="amber" />
        <StatCard icon={<CheckCircle className="w-5 h-5 text-green-600" />} label={t('common:button.approve')} value={stats.approved} color="green" />
        <StatCard icon={<XCircle className="w-5 h-5 text-red-600" />} label={t('common:button.reject')} value={stats.rejected} color="red" />
        <StatCard icon={<ShieldCheck className="w-5 h-5 text-blue-600" />} label={t('common:label.all')} value={stats.total} color="blue" />
      </div>

      {/* Filters */}
      <div className="flex flex-wrap items-center gap-3">
        <select
          id="pendingDscStatusFilter"
          name="status"
          value={statusFilter}
          onChange={(e) => { setStatusFilter(e.target.value as StatusFilter); setPage(1); }}
          className="px-3 py-2 text-sm border border-gray-200 rounded-lg bg-white focus:ring-2 focus:ring-blue-500 focus:border-blue-500"
        >
          <option value="">{ t('report:crl.allStatuses') }</option>
          <option value="PENDING">{ t('monitoring:pool.idle') }</option>
          <option value="APPROVED">{t('common:button.approve')}</option>
          <option value="REJECTED">{t('common:button.reject')}</option>
        </select>
        <input
          id="pendingDscCountryFilter"
          name="country"
          type="text"
          placeholder="국가 코드 (예: KR)"
          value={countryFilter}
          onChange={(e) => { setCountryFilter(e.target.value.toUpperCase()); setPage(1); }}
          className="px-3 py-2 text-sm border border-gray-200 rounded-lg bg-white focus:ring-2 focus:ring-blue-500 focus:border-blue-500 w-40"
          maxLength={3}
        />
        <span className="text-sm text-gray-500">총 {total}건</span>
      </div>

      {/* Table */}
      <div className="bg-white rounded-xl border border-gray-200 overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="bg-gray-50 border-b border-gray-200">
                <th className="text-left px-4 py-3 font-medium text-gray-600">{ t('ai:dashboard.filterCountry') }</th>
                <th className="text-left px-4 py-3 font-medium text-gray-600">Subject DN</th>
                <th className="text-left px-4 py-3 font-medium text-gray-600">{ t('ai:forensic.categories.algorithm') }</th>
                <th className="text-left px-4 py-3 font-medium text-gray-600">{ t('common:label.validPeriod') }</th>
                <th className="text-left px-4 py-3 font-medium text-gray-600">PA 결과</th>
                <th className="text-left px-4 py-3 font-medium text-gray-600">{ t('admin:apiClient.status') }</th>
                <th className="text-left px-4 py-3 font-medium text-gray-600">등록일</th>
                <th className="text-center px-4 py-3 font-medium text-gray-600">{ t('certificate:search.action') }</th>
              </tr>
            </thead>
            <tbody>
              {loading ? (
                <tr><td colSpan={8} className="px-4 py-12 text-center text-gray-400">불러오는 중...</td></tr>
              ) : items.length === 0 ? (
                <tr><td colSpan={8} className="px-4 py-12 text-center text-gray-400">{ t('common:table.noData') }</td></tr>
              ) : items.map((item) => (
                <tr key={item.id} className="border-b border-gray-100 hover:bg-gray-50/50 transition-colors">
                  <td className="px-4 py-3 font-medium">{item.country_code}</td>
                  <td className="px-4 py-3">
                    <button
                      onClick={() => setDetailTarget(item)}
                      className="text-left text-blue-600 hover:text-blue-800 hover:underline truncate max-w-[280px] block"
                      title={item.subject_dn}
                    >
                      {extractCN(item.subject_dn)}
                    </button>
                  </td>
                  <td className="px-4 py-3 text-gray-600">
                    <span className="text-xs">{item.signature_algorithm}</span>
                    <br />
                    <span className="text-xs text-gray-400">{item.public_key_algorithm} {item.public_key_size}bit</span>
                  </td>
                  <td className="px-4 py-3 text-xs text-gray-500">
                    {formatDate(item.not_before)} ~ {formatDate(item.not_after)}
                  </td>
                  <td className="px-4 py-3">{verificationBadge(item.verification_status)}</td>
                  <td className="px-4 py-3">{statusBadge(item.status)}</td>
                  <td className="px-4 py-3 text-xs text-gray-500">{formatDate(item.created_at)}</td>
                  <td className="px-4 py-3 text-center">
                    {item.status === 'PENDING' ? (
                      <div className="flex items-center justify-center gap-1">
                        <button
                          onClick={() => { setApproveTarget(item); setComment(''); }}
                          className="px-2.5 py-1 text-xs font-medium text-white bg-green-600 hover:bg-green-700 rounded-md transition-colors"
                        >
                          {t('common.button.approve')}
                        </button>
                        <button
                          onClick={() => { setRejectTarget(item); setComment(''); }}
                          className="px-2.5 py-1 text-xs font-medium text-white bg-red-500 hover:bg-red-600 rounded-md transition-colors"
                        >
                          {t('common.button.reject')}
                        </button>
                      </div>
                    ) : (
                      <span className="text-xs text-gray-400">
                        {item.reviewed_by && `${item.reviewed_by}`}
                      </span>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="flex items-center justify-between px-4 py-3 border-t border-gray-200 bg-gray-50">
            <span className="text-sm text-gray-500">
              {(page - 1) * size + 1}~{Math.min(page * size, total)} / {total}건
            </span>
            <div className="flex items-center gap-1">
              <button
                onClick={() => setPage(p => Math.max(1, p - 1))}
                disabled={page <= 1}
                className="p-1.5 rounded hover:bg-gray-200 disabled:opacity-40 disabled:cursor-not-allowed"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>
              <span className="px-3 py-1 text-sm">{page} / {totalPages}</span>
              <button
                onClick={() => setPage(p => Math.min(totalPages, p + 1))}
                disabled={page >= totalPages}
                className="p-1.5 rounded hover:bg-gray-200 disabled:opacity-40 disabled:cursor-not-allowed"
              >
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Approve Dialog */}
      {approveTarget && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setApproveTarget(null)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-md bg-white rounded-xl shadow-2xl" onClick={(e) => e.stopPropagation()}>
              <div className="px-6 py-4 border-b border-gray-200">
                <h3 className="text-base font-semibold text-gray-900">DSC 인증서 등록 승인</h3>
              </div>
              <div className="px-6 py-4 space-y-3">
                <p className="text-sm text-gray-600">이 DSC 인증서를 Local PKD에 등록하시겠습니까?</p>
                <div className="p-3 bg-gray-50 rounded-lg text-xs space-y-1">
                  <InfoRow label={t('common:label.country')} value={approveTarget.country_code} />
                  <InfoRow label="Subject" value={approveTarget.subject_dn} />
                  <InfoRow label="Fingerprint" value={approveTarget.fingerprint_sha256} mono />
                  <InfoRow label={t('ai:forensic.categories.algorithm')} value={`${approveTarget.signature_algorithm} / ${approveTarget.public_key_algorithm} ${approveTarget.public_key_size}bit`} />
                  <InfoRow label={t('common:label.validPeriod')} value={`${formatDate(approveTarget.not_before)} ~ ${formatDate(approveTarget.not_after)}`} />
                  <InfoRow label="PA 결과" value={approveTarget.verification_status} />
                </div>
                <div>
                  <label htmlFor="approveComment" className="block text-xs text-gray-500 mb-1">코멘트 (선택)</label>
                  <input id="approveComment" name="comment" type="text" value={comment} onChange={(e) => setComment(e.target.value)} placeholder="승인 사유를 입력하세요" className="w-full px-3 py-2 text-sm border border-gray-200 rounded-lg focus:ring-2 focus:ring-green-500 focus:border-green-500" />
                </div>
              </div>
              <div className="flex justify-end gap-2 px-6 py-4 border-t border-gray-200">
                <button onClick={() => setApproveTarget(null)} disabled={actionLoading} className="px-4 py-2 text-sm font-medium text-gray-700 bg-white border border-gray-300 rounded-lg hover:bg-gray-50 transition-colors">{t('common:button.cancel')}</button>
                <button onClick={handleApprove} disabled={actionLoading} className="px-4 py-2 text-sm font-medium text-white bg-green-600 hover:bg-green-700 rounded-lg transition-colors disabled:opacity-50">{actionLoading ? t('upload.fileUpload.processing') : t('common:button.approve')}</button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Reject Dialog */}
      {rejectTarget && (
        <div className="fixed inset-0 z-[80] bg-black/50 backdrop-blur-sm" onClick={() => setRejectTarget(null)}>
          <div className="fixed inset-0 z-[81] flex items-center justify-center p-4">
            <div className="w-full max-w-md bg-white rounded-xl shadow-2xl" onClick={(e) => e.stopPropagation()}>
              <div className="px-6 py-4 border-b border-gray-200">
                <h3 className="text-base font-semibold text-gray-900">DSC 인증서 등록 거부</h3>
              </div>
              <div className="px-6 py-4 space-y-3">
                <p className="text-sm text-gray-600">이 DSC 인증서 등록을 거부하시겠습니까?</p>
                <div className="p-3 bg-gray-50 rounded-lg text-xs space-y-1">
                  <InfoRow label={t('common:label.country')} value={rejectTarget.country_code} />
                  <InfoRow label="Subject" value={rejectTarget.subject_dn} />
                  <InfoRow label="Fingerprint" value={rejectTarget.fingerprint_sha256} mono />
                </div>
                <div>
                  <label htmlFor="rejectComment" className="block text-xs text-gray-500 mb-1">코멘트 (선택)</label>
                  <input id="rejectComment" name="comment" type="text" value={comment} onChange={(e) => setComment(e.target.value)} placeholder="거부 사유를 입력하세요" className="w-full px-3 py-2 text-sm border border-gray-200 rounded-lg focus:ring-2 focus:ring-red-500 focus:border-red-500" />
                </div>
              </div>
              <div className="flex justify-end gap-2 px-6 py-4 border-t border-gray-200">
                <button onClick={() => setRejectTarget(null)} disabled={actionLoading} className="px-4 py-2 text-sm font-medium text-gray-700 bg-white border border-gray-300 rounded-lg hover:bg-gray-50 transition-colors">{t('common:button.cancel')}</button>
                <button onClick={handleReject} disabled={actionLoading} className="px-4 py-2 text-sm font-medium text-white bg-red-600 hover:bg-red-700 rounded-lg transition-colors disabled:opacity-50">{actionLoading ? t('upload.fileUpload.processing') : t('common:button.reject')}</button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Detail Dialog */}
      {detailTarget && (
        <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/40" onClick={() => setDetailTarget(null)}>
          <div className="bg-white rounded-xl shadow-2xl w-full max-w-lg mx-4" onClick={(e) => e.stopPropagation()}>
            <div className="flex items-center justify-between px-6 py-4 border-b border-gray-200">
              <h3 className="text-lg font-semibold text-gray-900">DSC 인증서 상세</h3>
              <button onClick={() => setDetailTarget(null)} className="text-gray-400 hover:text-gray-600">
                <ShieldX className="w-5 h-5" />
              </button>
            </div>
            <div className="px-6 py-4 space-y-2 text-sm max-h-[60vh] overflow-y-auto">
              <InfoRow label="ID" value={detailTarget.id} mono />
              <InfoRow label={t('common:label.status')} value={detailTarget.status} />
              <InfoRow label={t('common:label.country')} value={detailTarget.country_code} />
              <InfoRow label="Subject DN" value={detailTarget.subject_dn} />
              <InfoRow label="Issuer DN" value={detailTarget.issuer_dn} />
              <InfoRow label="Serial Number" value={detailTarget.serial_number} mono />
              <InfoRow label="Fingerprint" value={detailTarget.fingerprint_sha256} mono />
              <InfoRow label={t('certificate:detail.signatureAlgorithm')} value={detailTarget.signature_algorithm} />
              <InfoRow label={t('certificate:detail.publicKeyAlgorithm')} value={`${detailTarget.public_key_algorithm} ${detailTarget.public_key_size}bit`} />
              <InfoRow label={t('certificate:metadata.notBefore')} value={formatDate(detailTarget.not_before)} />
              <InfoRow label={t('certificate:metadata.notAfter')} value={formatDate(detailTarget.not_after)} />
              <InfoRow label="인증서 상태" value={detailTarget.validation_status} />
              <InfoRow label="PA 검증 결과" value={detailTarget.verification_status} />
              <InfoRow label="PA 검증 ID" value={detailTarget.pa_verification_id} mono />
              <InfoRow label="Self-Signed" value={detailTarget.is_self_signed ? t('common.label.yes') : t('common.label.no')} />
              <InfoRow label="등록 요청일" value={formatDate(detailTarget.created_at)} />
              {detailTarget.reviewed_by && <InfoRow label={t('admin:pendingDsc.reviewedBy')} value={detailTarget.reviewed_by} />}
              {detailTarget.reviewed_at && <InfoRow label={t('admin:pendingDsc.reviewedAt')} value={formatDate(detailTarget.reviewed_at)} />}
              {detailTarget.review_comment && <InfoRow label={t('admin.apiClient.comment')} value={detailTarget.review_comment} />}
            </div>
            <div className="flex justify-end px-6 py-3 border-t border-gray-200 gap-2">
              {detailTarget.status === 'PENDING' && (
                <>
                  <button
                    onClick={() => { setDetailTarget(null); setApproveTarget(detailTarget); setComment(''); }}
                    className="px-4 py-2 text-sm font-medium text-white bg-green-600 hover:bg-green-700 rounded-lg transition-colors"
                  >
                    {t('common.button.approve')}
                  </button>
                  <button
                    onClick={() => { setDetailTarget(null); setRejectTarget(detailTarget); setComment(''); }}
                    className="px-4 py-2 text-sm font-medium text-white bg-red-500 hover:bg-red-600 rounded-lg transition-colors"
                  >
                    {t('common.button.reject')}
                  </button>
                </>
              )}
              <button
                onClick={() => setDetailTarget(null)}
                className="px-4 py-2 text-sm text-gray-600 hover:text-gray-800 bg-gray-100 hover:bg-gray-200 rounded-lg transition-colors"
              >
                {t('icao.banner.dismiss')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

// --- Helper Components ---

function StatCard({ icon, label, value, color }: { icon: React.ReactNode; label: string; value: number; color: string }) {
  const bgMap: Record<string, string> = {
    amber: 'bg-amber-50 border-amber-200',
    green: 'bg-green-50 border-green-200',
    red: 'bg-red-50 border-red-200',
    blue: 'bg-blue-50 border-blue-200',
  };
  return (
    <div className={`flex items-center gap-3 p-4 rounded-xl border ${bgMap[color] || 'bg-gray-50 border-gray-200'}`}>
      {icon}
      <div>
        <div className="text-2xl font-bold text-gray-900">{value.toLocaleString()}</div>
        <div className="text-xs text-gray-500">{label}</div>
      </div>
    </div>
  );
}

function InfoRow({ label, value, mono }: { label: string; value: string; mono?: boolean }) {
  return (
    <div className="flex gap-2">
      <span className="text-gray-500 shrink-0 w-24">{label}</span>
      <span className={`text-gray-800 break-all ${mono ? 'font-mono text-xs' : ''}`}>{value}</span>
    </div>
  );
}

function extractCN(dn: string): string {
  // Extract CN from DN: "/C=KR/O=Gov/CN=Some Name" or "CN=Some Name,O=Gov,C=KR"
  const cnMatch = dn.match(/CN=([^/,]+)/i);
  return cnMatch ? cnMatch[1].trim() : dn;
}
