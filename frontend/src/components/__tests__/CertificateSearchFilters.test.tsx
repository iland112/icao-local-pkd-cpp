import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import CertificateSearchFilters from '../CertificateSearchFilters';
import type { SearchCriteria } from '../CertificateSearchFilters';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => {
      const map: Record<string, string> = {
        'certificate:search.filterLabel': '필터',
        'certificate:filters.certType': '인증서 유형',
        'certificate:filters.validity': '유효성',
        'certificate:detail.sourceType': '출처',
        'certificate:filters.displayCount': '표시 수',
        'certificate:filters.keywordSearch': '키워드 검색',
        'certificate:filters.search': '검색',
        'certificate:filters.exportAllPem': '전체 내보내기 PEM',
        'certificate:filters.exportAllDer': '전체 내보내기 DER',
        'certificate:search.cnSearch': 'CN 검색',
        'certificate:search.validity.expiredValid': '만료(서명유효)',
        'certificate:search.validity.notYetValid': '유효하지 않음',
        'pa:history.country': '국가',
        'report:crl.allCountries': '전체 국가',
        'monitoring:pool.total': '전체',
        'nav:menu.fileUpload': '파일 업로드',
        'common:status.valid': '유효',
        'common:status.expired': '만료',
        'common:source.LDIF_UPLOAD': 'LDIF 업로드',
        'common:source.PA_EXTRACTED_short': 'PA 추출',
        'common:source.DL_PARSED_short': 'DL 파싱',
      };
      return map[key] ?? key;
    },
    i18n: { language: 'ko' },
  }),
}));

vi.mock('@/utils/countryCode', () => ({
  getFlagSvgPath: () => null,
}));

vi.mock('@/utils/countryNames', () => ({
  getCountryDisplayName: (code: string) => code,
  getCountryName: (code: string) => code,
}));

const defaultCriteria: SearchCriteria = {
  country: '',
  certType: '',
  validity: 'all',
  source: '',
  searchTerm: '',
  limit: 25,
  offset: 0,
};

const defaultProps = {
  criteria: defaultCriteria,
  setCriteria: vi.fn(),
  countries: ['KR', 'DE', 'JP'],
  countriesLoading: false,
  loading: false,
  showFilters: true,
  setShowFilters: vi.fn(),
  handleSearch: vi.fn(),
  exportCountry: vi.fn(),
  exportAll: vi.fn(),
  exportAllLoading: false,
};

describe('CertificateSearchFilters', () => {
  it('should render filter label', () => {
    render(<CertificateSearchFilters {...defaultProps} />);
    expect(screen.getByText('필터')).toBeInTheDocument();
  });

  it('should show filter fields when showFilters=true', () => {
    render(<CertificateSearchFilters {...defaultProps} showFilters={true} />);
    expect(screen.getByLabelText('국가')).toBeInTheDocument();
    expect(screen.getByLabelText('인증서 유형')).toBeInTheDocument();
  });

  it('should hide filter fields when showFilters=false', () => {
    render(<CertificateSearchFilters {...defaultProps} showFilters={false} />);
    expect(screen.queryByLabelText('국가')).not.toBeInTheDocument();
  });

  it('should call setShowFilters when toggle button is clicked', () => {
    const setShowFilters = vi.fn();
    const { container } = render(
      <CertificateSearchFilters
        {...defaultProps}
        showFilters={true}
        setShowFilters={setShowFilters}
      />
    );

    // The toggle button is the only button in the header area (icon-only, no text)
    // It sits right after the filter label, detected by its position in the DOM
    const buttons = container.querySelectorAll('button');
    // First button in the component is the toggle chevron
    fireEvent.click(buttons[0]);
    expect(setShowFilters).toHaveBeenCalledWith(false);
  });

  it('should render country options from countries prop', () => {
    render(<CertificateSearchFilters {...defaultProps} />);
    const countrySelect = screen.getByLabelText('국가') as HTMLSelectElement;
    const options = Array.from(countrySelect.options).map((o) => o.value);
    expect(options).toContain('KR');
    expect(options).toContain('DE');
    expect(options).toContain('JP');
  });

  it('should show "Loading..." in country select when countriesLoading=true', () => {
    render(<CertificateSearchFilters {...defaultProps} countriesLoading={true} />);
    expect(screen.getByText('Loading...')).toBeInTheDocument();
  });

  it('should show "No countries" when countries array is empty and not loading', () => {
    render(<CertificateSearchFilters {...defaultProps} countries={[]} />);
    expect(screen.getByText('No countries')).toBeInTheDocument();
  });

  it('should call setCriteria when country select changes', () => {
    const setCriteria = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} setCriteria={setCriteria} />);

    fireEvent.change(screen.getByLabelText('국가'), { target: { value: 'KR' } });
    expect(setCriteria).toHaveBeenCalledWith({ ...defaultCriteria, country: 'KR' });
  });

  it('should call setCriteria when certType changes', () => {
    const setCriteria = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} setCriteria={setCriteria} />);

    fireEvent.change(screen.getByLabelText('인증서 유형'), { target: { value: 'DSC' } });
    expect(setCriteria).toHaveBeenCalledWith({ ...defaultCriteria, certType: 'DSC' });
  });

  it('should call handleSearch when search button is clicked', () => {
    const handleSearch = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} handleSearch={handleSearch} />);

    fireEvent.click(screen.getByText('검색'));
    expect(handleSearch).toHaveBeenCalledOnce();
  });

  it('should disable search button when loading=true', () => {
    render(<CertificateSearchFilters {...defaultProps} loading={true} />);
    const searchBtn = screen.getByText('검색').closest('button');
    expect(searchBtn).toBeDisabled();
  });

  it('should call handleSearch on Enter key in search input', () => {
    const handleSearch = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} handleSearch={handleSearch} />);

    const searchInput = screen.getByPlaceholderText('CN 검색');
    fireEvent.keyDown(searchInput, { key: 'Enter' });
    expect(handleSearch).toHaveBeenCalledOnce();
  });

  it('should show country export buttons when country is selected', () => {
    render(
      <CertificateSearchFilters
        {...defaultProps}
        criteria={{ ...defaultCriteria, country: 'KR' }}
      />
    );

    expect(screen.getByText('KR PEM ZIP')).toBeInTheDocument();
    expect(screen.getByText('KR DER ZIP')).toBeInTheDocument();
  });

  it('should not show country export buttons when no country is selected', () => {
    render(<CertificateSearchFilters {...defaultProps} />);
    expect(screen.queryByText(/PEM ZIP/)).not.toBeInTheDocument();
  });

  it('should call exportCountry with pem when PEM ZIP button clicked', () => {
    const exportCountry = vi.fn();
    render(
      <CertificateSearchFilters
        {...defaultProps}
        criteria={{ ...defaultCriteria, country: 'DE' }}
        exportCountry={exportCountry}
      />
    );

    fireEvent.click(screen.getByText('DE PEM ZIP'));
    expect(exportCountry).toHaveBeenCalledWith('DE', 'pem');
  });

  it('should call exportCountry with der when DER ZIP button clicked', () => {
    const exportCountry = vi.fn();
    render(
      <CertificateSearchFilters
        {...defaultProps}
        criteria={{ ...defaultCriteria, country: 'DE' }}
        exportCountry={exportCountry}
      />
    );

    fireEvent.click(screen.getByText('DE DER ZIP'));
    expect(exportCountry).toHaveBeenCalledWith('DE', 'der');
  });

  it('should call exportAll with pem when "전체 내보내기 PEM" button clicked', () => {
    const exportAll = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} exportAll={exportAll} />);

    fireEvent.click(screen.getByText('전체 내보내기 PEM'));
    expect(exportAll).toHaveBeenCalledWith('pem');
  });

  it('should call exportAll with der when "전체 내보내기 DER" button clicked', () => {
    const exportAll = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} exportAll={exportAll} />);

    fireEvent.click(screen.getByText('전체 내보내기 DER'));
    expect(exportAll).toHaveBeenCalledWith('der');
  });

  it('should disable export all buttons when exportAllLoading=true', () => {
    render(<CertificateSearchFilters {...defaultProps} exportAllLoading={true} />);

    const pemBtn = screen.getByText('전체 내보내기 PEM').closest('button');
    const derBtn = screen.getByText('전체 내보내기 DER').closest('button');
    expect(pemBtn).toBeDisabled();
    expect(derBtn).toBeDisabled();
  });

  it('should update searchTerm when typing in search input', () => {
    const setCriteria = vi.fn();
    render(<CertificateSearchFilters {...defaultProps} setCriteria={setCriteria} />);

    const searchInput = screen.getByPlaceholderText('CN 검색');
    fireEvent.change(searchInput, { target: { value: 'Korea' } });

    expect(setCriteria).toHaveBeenCalledWith({
      ...defaultCriteria,
      searchTerm: 'Korea',
    });
  });

  it('should reset offset to 0 when limit changes', () => {
    const setCriteria = vi.fn();
    render(
      <CertificateSearchFilters
        {...defaultProps}
        criteria={{ ...defaultCriteria, offset: 50 }}
        setCriteria={setCriteria}
      />
    );

    fireEvent.change(screen.getByLabelText('표시 수'), { target: { value: '100' } });
    const callArg = setCriteria.mock.calls[0][0];
    expect(callArg.offset).toBe(0);
    expect(callArg.limit).toBe(100);
  });
});
