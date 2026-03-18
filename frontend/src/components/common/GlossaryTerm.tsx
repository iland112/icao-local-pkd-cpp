import { useState, useRef } from 'react';
import { HelpCircle } from 'lucide-react';

/** Glossary definitions for technical terms used in the system */
const glossary: Record<string, { ko: string; en: string }> = {
  CSCA: {
    ko: 'Country Signing Certificate Authority — 국가 서명 인증 기관. 여권 전자 칩 서명용 DSC를 발급하는 국가 최상위 루트 인증서',
    en: 'Country Signing Certificate Authority — The national root certificate that issues DSCs for signing ePassport chips',
  },
  DSC: {
    ko: 'Document Signer Certificate — 문서 서명 인증서. 여권 전자 칩의 데이터 그룹(DG)에 디지털 서명하는 인증서',
    en: 'Document Signer Certificate — Signs the data groups (DG) stored in ePassport chips',
  },
  DSC_NC: {
    ko: 'Non-Conformant DSC — ICAO 표준 미준수 DSC. ICAO PKD의 nc-data 영역에 별도 저장',
    en: 'Non-Conformant DSC — DSC that does not fully comply with ICAO standards, stored separately in nc-data',
  },
  MLSC: {
    ko: 'Master List Signer Certificate — Master List에 서명하는 Self-signed 인증서',
    en: 'Master List Signer Certificate — Self-signed certificate used to sign ICAO Master Lists',
  },
  CRL: {
    ko: 'Certificate Revocation List — 인증서 폐기 목록. 폐기된 인증서 일련번호를 담은 X.509 데이터 구조',
    en: 'Certificate Revocation List — A list of revoked certificate serial numbers published by the CA',
  },
  SOD: {
    ko: 'Security Object Document — 보안 객체 문서. 여권 칩 내 DG 해시값에 DSC로 서명한 CMS SignedData 구조',
    en: 'Security Object Document — CMS SignedData structure containing DG hashes signed by DSC',
  },
  DG1: {
    ko: 'Data Group 1 — MRZ(기계 판독 영역) 데이터. 여권 정보(이름, 국적, 생년월일 등)를 포함',
    en: 'Data Group 1 — Contains MRZ (Machine Readable Zone) data: name, nationality, date of birth, etc.',
  },
  DG2: {
    ko: 'Data Group 2 — 얼굴 이미지 데이터. JPEG 또는 JPEG2000 형식의 여권 사진',
    en: 'Data Group 2 — Facial image data stored as JPEG or JPEG2000 format',
  },
  MRZ: {
    ko: 'Machine Readable Zone — 기계 판독 영역. 여권 하단의 OCR 판독 가능한 2~3줄의 코드 영역',
    en: 'Machine Readable Zone — The 2-3 line OCR-readable code area at the bottom of a passport',
  },
  PA: {
    ko: 'Passive Authentication — 수동 인증. ICAO 9303 Part 11에 정의된 여권 칩 데이터의 위·변조 검증 프로토콜',
    en: 'Passive Authentication — ICAO 9303 Part 11 protocol for verifying ePassport chip data integrity',
  },
  BAC: {
    ko: 'Basic Access Control — 기본 접근 제어. MRZ 정보로 여권 칩에 대한 접근을 제어하는 프로토콜',
    en: 'Basic Access Control — Protocol that controls access to passport chip using MRZ information',
  },
  PKD: {
    ko: 'Public Key Directory — 공개키 디렉토리. ICAO가 운영하는 전자여권 인증서의 중앙 저장소',
    en: 'Public Key Directory — Central repository of ePassport certificates operated by ICAO',
  },
  LDIF: {
    ko: 'LDAP Data Interchange Format — LDAP 데이터 교환 형식. 디렉토리 엔트리를 텍스트로 표현하는 표준 파일 형식',
    en: 'LDAP Data Interchange Format — Standard text format for representing directory entries',
  },
  LDAP: {
    ko: 'Lightweight Directory Access Protocol — 경량 디렉토리 접근 프로토콜. 인증서 저장 및 검색용 디렉토리 서비스',
    en: 'Lightweight Directory Access Protocol — Directory service for storing and retrieving certificates',
  },
  'Trust Chain': {
    ko: '신뢰 체인 — DSC → CSCA 간의 인증서 서명 검증 경로. 여권 인증서의 신뢰성을 보장',
    en: 'Trust Chain — Certificate signature verification path from DSC to CSCA, ensuring certificate authenticity',
  },
  'Link Certificate': {
    ko: 'Link Certificate (LC) — 이전 CSCA와 새 CSCA 사이의 신뢰 체인을 연결하는 인증서. CSCA 키 교체/갱신 시 발급',
    en: 'Link Certificate (LC) — Bridges trust between old and new CSCA during key rollover or algorithm migration',
  },
  'Self-signed': {
    ko: 'Self-signed — 자체 서명 인증서. 발급자(Issuer)와 주체(Subject)가 동일한 루트 인증서',
    en: 'Self-signed — Root certificate where the issuer and subject are the same entity',
  },
  'Master List': {
    ko: 'Master List (ML) — ICAO PKD에서 배포하는 국가별 CSCA 인증서 목록. CMS SignedData 구조로 MLSC가 서명',
    en: 'Master List (ML) — Country-level CSCA certificate list distributed by ICAO PKD, signed as CMS SignedData by MLSC',
  },
  ML: {
    ko: 'Master List — ICAO PKD에서 배포하는 국가별 CSCA 인증서 목록. CMS SignedData 구조로 MLSC가 서명',
    en: 'Master List — Country-level CSCA certificate list distributed by ICAO PKD, signed as CMS SignedData by MLSC',
  },
  CSR: {
    ko: 'Certificate Signing Request — 인증서 서명 요청. RSA/ECDSA 키 쌍을 생성하고 CA에 인증서 발급을 요청하는 PKCS#10 구조',
    en: 'Certificate Signing Request — PKCS#10 structure requesting a CA to issue a certificate for a generated key pair',
  },
};

interface GlossaryTermProps {
  /** The technical term to explain (e.g., "CSCA", "DSC", "Trust Chain") */
  term: string;
  /** Override the displayed label (defaults to the term itself) */
  label?: string;
  /** Additional className for the term text */
  className?: string;
  /** Icon size */
  iconSize?: 'xs' | 'sm';
}

/**
 * Displays a technical term with a help icon.
 * On hover, shows a fixed-position tooltip that escapes any overflow-hidden parent.
 */
export function GlossaryTerm({ term, label, className, iconSize = 'xs' }: GlossaryTermProps) {
  const [show, setShow] = useState(false);
  const [coords, setCoords] = useState({ top: 0, left: 0, placement: 'bottom' as 'top' | 'bottom' });
  const ref = useRef<HTMLSpanElement>(null);

  const entry = glossary[term];
  if (!entry) return <span className={className}>{label ?? term}</span>;

  const lang = typeof document !== 'undefined' && document.documentElement.lang === 'en' ? 'en' : 'ko';
  const tooltip = entry[lang] || entry.ko;
  const iconCls = iconSize === 'xs' ? 'w-3 h-3' : 'w-3.5 h-3.5';

  const handleEnter = () => {
    if (ref.current) {
      const rect = ref.current.getBoundingClientRect();
      const spaceBelow = window.innerHeight - rect.bottom;
      const placement = spaceBelow < 140 ? 'top' : 'bottom';
      const left = Math.min(rect.left, window.innerWidth - 296); // 288px width + 8px margin
      setCoords({
        top: placement === 'bottom' ? rect.bottom + 4 : rect.top - 4,
        left: Math.max(4, left),
        placement,
      });
    }
    setShow(true);
  };

  return (
    <span
      ref={ref}
      className={`inline-flex items-center gap-0.5 ${className ?? ''}`}
      onMouseEnter={handleEnter}
      onMouseLeave={() => setShow(false)}
    >
      {label ?? term}
      <HelpCircle className={`${iconCls} text-gray-400 dark:text-gray-500 cursor-help flex-shrink-0`} />

      {show && (
        <span
          className="fixed z-[9999] w-72 px-3 py-2 rounded-lg shadow-lg border text-xs leading-relaxed pointer-events-none
            bg-gray-900 dark:bg-gray-700 text-gray-100 dark:text-gray-200 border-gray-700 dark:border-gray-600"
          style={{
            top: coords.placement === 'bottom' ? coords.top : undefined,
            bottom: coords.placement === 'top' ? (window.innerHeight - coords.top) : undefined,
            left: coords.left,
          }}
        >
          <span className="font-semibold text-white">{term}</span>
          <br />
          {tooltip}
        </span>
      )}
    </span>
  );
}

/** Get glossary tooltip text for a term (for use in title attributes) */
export function getGlossaryTooltip(term: string): string {
  const entry = glossary[term];
  if (!entry) return term;
  const lang = typeof document !== 'undefined' && document.documentElement.lang === 'en' ? 'en' : 'ko';
  return `${term}: ${entry[lang] || entry.ko}`;
}
