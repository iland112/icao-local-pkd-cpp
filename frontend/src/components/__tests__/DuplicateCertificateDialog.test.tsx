import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { DuplicateCertificateDialog } from '../DuplicateCertificateDialog';
import type { UploadDuplicate } from '@/types';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => {
      const map: Record<string, string> = {
        'upload:duplicateDialog.title': '중복 인증서',
        'upload:duplicateDialog.subtitle': '이미 등록된 인증서입니다',
        'upload:duplicateDialog.certInfo': '인증서 정보',
        'upload:duplicateDialog.type': '유형',
        'upload:duplicateDialog.originalUpload': '원본 업로드',
        'upload:duplicateDialog.uploadTime': '업로드 시간',
        'upload:duplicateDialog.detectionInfo': '탐지 정보',
        'upload:duplicateDialog.sourceType': '소스 유형',
        'upload:duplicateDialog.sourceCountry': '소스 국가',
        'upload:duplicateDialog.sourceFile': '소스 파일',
        'upload:duplicateDialog.detectedAt': '탐지 시간',
        'upload:duplicateDialog.databaseInfo': 'DB 정보',
        'common:label.country': '국가',
        'common:label.fileName': '파일명',
        'common:button.close': '닫기',
        'common:button.copy': '복사',
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

vi.mock('@/utils/dateFormat', () => ({
  formatDateTime: (d: string) => d || 'N/A',
}));

// DuplicateCertificateDialog imports from '../utils/countryCode' (relative, not @/)
vi.mock('@/utils/countryCode', () => ({
  getFlagSvgPath: () => null,
}));

vi.mock('@/utils/countryNames', () => ({
  getCountryName: (code: string) => code,
}));

const makeDuplicate = (overrides: Partial<UploadDuplicate> = {}): UploadDuplicate => ({
  id: 42,
  certificateId: 'cert-uuid-abc',
  fingerprint: 'abcdef1234567890abcdef1234567890abcdef12',
  subjectDn: 'CN=Test DSC,C=KR',
  certificateType: 'DSC',
  country: 'KR',
  sourceType: 'LDIF_PARSED',
  detectedAt: '2026-01-15T10:30:00Z',
  firstUploadId: 'upload-uuid-xyz',
  firstUploadFileName: 'collection-001.ldif',
  firstUploadTimestamp: '2026-01-10T08:00:00Z',
  ...overrides,
});

describe('DuplicateCertificateDialog', () => {
  it('should render nothing when isOpen=false', () => {
    const { container } = render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={false} onClose={vi.fn()} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render nothing when duplicate is null', () => {
    const { container } = render(
      <DuplicateCertificateDialog duplicate={null} isOpen={true} onClose={vi.fn()} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('should render dialog when isOpen=true and duplicate is provided', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('중복 인증서')).toBeInTheDocument();
    expect(screen.getByText('이미 등록된 인증서입니다')).toBeInTheDocument();
  });

  it('should render certificate type badge', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('DSC')).toBeInTheDocument();
  });

  it('should render CSCA certificate type badge', () => {
    render(
      <DuplicateCertificateDialog
        duplicate={makeDuplicate({ certificateType: 'CSCA' })}
        isOpen={true}
        onClose={vi.fn()}
      />
    );
    expect(screen.getByText('CSCA')).toBeInTheDocument();
  });

  it('should render subject DN', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('CN=Test DSC,C=KR')).toBeInTheDocument();
  });

  it('should render fingerprint', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('abcdef1234567890abcdef1234567890abcdef12')).toBeInTheDocument();
  });

  it('should render country code', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('KR')).toBeInTheDocument();
  });

  it('should render original upload filename', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('collection-001.ldif')).toBeInTheDocument();
  });

  it('should render "N/A" when firstUploadFileName is not provided', () => {
    render(
      <DuplicateCertificateDialog
        duplicate={makeDuplicate({ firstUploadFileName: undefined })}
        isOpen={true}
        onClose={vi.fn()}
      />
    );
    expect(screen.getByText('N/A')).toBeInTheDocument();
  });

  it('should render source type', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('LDIF_PARSED')).toBeInTheDocument();
  });

  it('should render source country when provided', () => {
    render(
      <DuplicateCertificateDialog
        duplicate={makeDuplicate({ sourceCountry: 'DE' })}
        isOpen={true}
        onClose={vi.fn()}
      />
    );
    expect(screen.getByText('DE')).toBeInTheDocument();
  });

  it('should render LDAP DN when sourceEntryDn is provided', () => {
    render(
      <DuplicateCertificateDialog
        duplicate={makeDuplicate({ sourceEntryDn: 'cn=abc,o=dsc,c=KR' })}
        isOpen={true}
        onClose={vi.fn()}
      />
    );
    expect(screen.getByText('cn=abc,o=dsc,c=KR')).toBeInTheDocument();
  });

  it('should render certificate ID in database section', () => {
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={vi.fn()} />
    );
    expect(screen.getByText('cert-uuid-abc')).toBeInTheDocument();
  });

  it('should call onClose when close button in header is clicked', () => {
    const onClose = vi.fn();
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={onClose} />
    );

    // The X button in header
    const closeButtons = screen.getAllByRole('button');
    fireEvent.click(closeButtons[0]);
    expect(onClose).toHaveBeenCalled();
  });

  it('should call onClose when footer close button is clicked', () => {
    const onClose = vi.fn();
    render(
      <DuplicateCertificateDialog duplicate={makeDuplicate()} isOpen={true} onClose={onClose} />
    );

    fireEvent.click(screen.getByText('닫기'));
    expect(onClose).toHaveBeenCalledOnce();
  });
});
