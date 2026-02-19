import {
  Award,
  XCircle,
  AlertTriangle,
  Clock,
  Globe,
  IdCard,
  ExternalLink,
} from 'lucide-react';
import type { PAVerificationResponse } from '@/types';
import { cn } from '@/utils/cn';
import { Link } from 'react-router-dom';
import { getFlagSvgPath } from '@/utils/countryCode';

// DG1 MRZ 파싱 결과 타입
export interface DG1ParseResult {
  success: boolean;
  documentType?: string;
  issuingCountry?: string;
  surname?: string;
  givenNames?: string;
  fullName?: string;
  documentNumber?: string;
  nationality?: string;
  dateOfBirth?: string;
  sex?: string;
  dateOfExpiry?: string;
  mrzLine1?: string;
  mrzLine2?: string;
  mrzFull?: string;
  error?: string;
}

// DG2 Face 파싱 결과 타입
export interface DG2ParseResult {
  success: boolean;
  faceCount?: number;
  faceImages?: Array<{
    index: number;
    imageFormat: string;
    imageSize: number;
    width?: number;
    height?: number;
    imageDataUrl?: string;
  }>;
  hasFacContainer?: boolean;
  error?: string;
}

interface VerificationResultCardProps {
  result: PAVerificationResponse;
  dg1ParseResult: DG1ParseResult | null;
  dg2ParseResult: DG2ParseResult | null;
}

export function VerificationResultCard({
  result,
  dg1ParseResult,
  dg2ParseResult,
}: VerificationResultCardProps) {
  return (
    <div className={cn(
      'rounded-2xl p-4',
      result.status === 'VALID'
        ? 'bg-gradient-to-r from-emerald-600/80 to-teal-600/80 text-white'
        : result.status === 'INVALID'
        ? 'bg-gradient-to-r from-rose-600/80 to-red-700/80 text-white'
        : 'bg-gradient-to-r from-amber-600/80 to-orange-600/80 text-white'
    )}>
      <div className="flex items-center gap-3">
        {result.status === 'VALID' ? (
          <Award className="w-10 h-10" />
        ) : result.status === 'INVALID' ? (
          <XCircle className="w-10 h-10" />
        ) : (
          <AlertTriangle className="w-10 h-10" />
        )}
        <div className="flex-grow">
          <h2 className="text-lg font-bold">
            {result.status === 'VALID'
              ? '검증 성공'
              : result.status === 'INVALID'
              ? '검증 실패'
              : '검증 오류'}
          </h2>
          <div className="flex items-center gap-4 mt-0.5 text-sm opacity-90">
            <span className="flex items-center gap-1">
              <Clock className="w-3.5 h-3.5" />
              {result.processingDurationMs}ms
            </span>
            {result.issuingCountry && (
              <span className="flex items-center gap-1">
                {getFlagSvgPath(result.issuingCountry) ? (
                  <img
                    src={getFlagSvgPath(result.issuingCountry)}
                    alt={result.issuingCountry}
                    className="w-5 h-3.5 object-cover rounded-sm border border-white/30"
                    onError={(e) => {
                      const img = e.target as HTMLImageElement;
                      img.style.display = 'none';
                      img.nextElementSibling?.classList.remove('hidden');
                    }}
                  />
                ) : null}
                <Globe className={`w-3.5 h-3.5 ${getFlagSvgPath(result.issuingCountry) ? 'hidden' : ''}`} />
                {result.issuingCountry}
              </span>
            )}
            {result.documentNumber && (
              <span className="flex items-center gap-1">
                <IdCard className="w-3.5 h-3.5" />
                {result.documentNumber}
              </span>
            )}
          </div>
        </div>
        <Link
          to={`/pa/history?id=${result.verificationId}`}
          className="flex items-center gap-1 text-xs opacity-80 hover:opacity-100 underline shrink-0"
        >
          <ExternalLink className="w-3.5 h-3.5" />
          상세
        </Link>
      </div>

      {/* Failure reasons */}
      {result.status === 'INVALID' && (
        <div className="mt-3 pt-3 border-t border-white/20 space-y-1.5">
          <div className="text-xs font-semibold opacity-90">실패 원인:</div>
          {!result.certificateChainValidation?.valid && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>Trust Chain 검증 실패{result.certificateChainValidation?.validationErrors ? ` \u2014 ${result.certificateChainValidation.validationErrors}` : ''}</span>
            </div>
          )}
          {!result.sodSignatureValidation?.valid && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>SOD 서명 검증 실패{result.sodSignatureValidation?.validationErrors ? ` \u2014 ${result.sodSignatureValidation.validationErrors}` : ''}</span>
            </div>
          )}
          {result.dataGroupValidation && result.dataGroupValidation.invalidGroups > 0 && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>Data Group 해시 불일치 ({result.dataGroupValidation.invalidGroups}/{result.dataGroupValidation.totalGroups})</span>
            </div>
          )}
          {result.certificateChainValidation?.revoked && (
            <div className="flex items-center gap-2 text-sm opacity-90">
              <XCircle className="w-3.5 h-3.5 shrink-0" />
              <span>인증서 폐기됨 (CRL)</span>
            </div>
          )}
        </div>
      )}

      {/* Non-Conformant DSC warning (shown regardless of VALID/INVALID) */}
      {result.certificateChainValidation?.dscNonConformant && (
        <div className="mt-3 pt-3 border-t border-white/20">
          <div className="flex items-center gap-2 text-sm">
            <AlertTriangle className="w-4 h-4 text-amber-300 shrink-0" />
            <span className="font-semibold text-amber-200">Non-Conformant DSC</span>
          </div>
          <p className="mt-1 text-xs opacity-80">
            {result.certificateChainValidation.pkdConformanceCode && (
              <span className="font-mono">{result.certificateChainValidation.pkdConformanceCode}: </span>
            )}
            {result.certificateChainValidation.pkdConformanceText || 'ICAO PKD 비준수 인증서'}
          </p>
        </div>
      )}

      {/* DG Parsing Results (shown when verification succeeds) */}
      {result.status === 'VALID' && (dg1ParseResult || dg2ParseResult) && (
        <div className="mt-3 pt-3 border-t border-white/20">
          <div className="flex gap-3">
            {/* DG2 Face Image */}
            {dg2ParseResult?.success && dg2ParseResult.faceImages?.[0] && (
              <div className="shrink-0">
                <img
                  src={dg2ParseResult.faceImages[0].imageDataUrl}
                  alt="Passport Face"
                  className="w-20 h-26 object-cover rounded-lg border-2 border-white/40 shadow-md"
                />
              </div>
            )}
            {/* DG1 MRZ Data */}
            {dg1ParseResult?.success && (
              <div className="flex-grow grid grid-cols-3 gap-x-4 gap-y-1 text-xs">
                <div>
                  <span className="opacity-70">성명</span>
                  <div className="font-semibold">{dg1ParseResult.fullName}</div>
                </div>
                <div>
                  <span className="opacity-70">여권번호</span>
                  <div className="font-mono font-semibold">{dg1ParseResult.documentNumber}</div>
                </div>
                <div>
                  <span className="opacity-70">국적</span>
                  <div className="font-semibold">{dg1ParseResult.nationality}</div>
                </div>
                <div>
                  <span className="opacity-70">생년월일</span>
                  <div className="font-mono font-semibold">{dg1ParseResult.dateOfBirth}</div>
                </div>
                <div>
                  <span className="opacity-70">만료일</span>
                  <div className="font-mono font-semibold">{dg1ParseResult.dateOfExpiry}</div>
                </div>
                <div>
                  <span className="opacity-70">성별</span>
                  <div className="font-semibold">
                    {dg1ParseResult.sex === 'M' ? '남성' : dg1ParseResult.sex === 'F' ? '여성' : dg1ParseResult.sex}
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Verification ID & Timestamp */}
      <div className="mt-3 pt-2 border-t border-white/20 text-xs flex flex-wrap gap-4 opacity-75">
        <div>
          <span>검증 ID: </span>
          <span className="font-mono">{result.verificationId}</span>
        </div>
        <div>
          <span>검증 시각: </span>
          <span>{new Date(result.verificationTimestamp).toLocaleString('ko-KR')}</span>
        </div>
      </div>
    </div>
  );
}

export default VerificationResultCard;
