=====================================
 ICAO Local PKD - HTTPS 접속 설정
=====================================

 1. 개요

   Windows PC에서 ICAO Local PKD 서버(https://pkd.smartcoreinc.com)에
   HTTPS로 접속하기 위한 설정 스크립트입니다.

   스크립트가 수행하는 작업:
     - hosts 파일에 pkd.smartcoreinc.com -> 10.0.0.220 등록
     - Private CA 인증서를 Windows 신뢰할 수 있는 루트 인증 기관에 설치


 2. 실행 방법

   [방법 1] BAT 파일 실행 (권장)

     1) setup-pkd-access.bat 파일을 우클릭
     2) "관리자 권한으로 실행" 선택
     3) 사용자 계정 컨트롤(UAC) 팝업에서 "예" 클릭
     4) 실행 결과 확인 (녹색 OK 메시지)

   [방법 2] PowerShell 직접 실행

     1) PowerShell을 "관리자 권한으로 실행"
     2) 아래 명령어 입력:

        powershell -ExecutionPolicy Bypass -File setup-pkd-access.ps1


 3. 실행 결과 확인

   정상 실행 시 아래와 같이 표시됩니다:

     [1/2] Configuring hosts file...
           Added entry: 10.0.0.220  pkd.smartcoreinc.com    [OK]
           DNS cache flushed                                 [OK]

     [2/2] Registering CA certificate...
           Registered to Trusted Root CA store!              [OK]
           Verified: certificate found in trust store        [OK]

     Testing connection...
           HTTPS connection OK! (HTTP 200)                   [OK]


 4. 실행 후 접속

   Chrome 또는 Edge 브라우저에서:

     https://pkd.smartcoreinc.com

   * 브라우저가 이미 열려 있었다면 완전히 종료 후 재시작하세요.


 5. 주의사항

   - 반드시 "관리자 권한"으로 실행해야 합니다.
     (일반 권한으로 실행하면 hosts 파일 수정 및 인증서 설치 불가)

   - Chrome, Edge 브라우저에서 정상 동작합니다.
     Firefox는 Windows 인증서 저장소를 사용하지 않으므로
     별도로 인증서를 가져와야 합니다.
     (Firefox: 설정 > 개인정보 및 보안 > 인증서 > 인증서 보기 > 가져오기)

   - 안티바이러스 프로그램이 hosts 파일 수정을 차단할 수 있습니다.
     차단 시 일시적으로 실시간 보호를 해제한 후 재실행하세요.

   - 서버 IP 또는 CA 인증서가 변경된 경우 업데이트된 스크립트를
     다시 실행하면 자동으로 기존 설정을 교체합니다.


 6. 문제 해결

   증상: "이 사이트는 안전하지 않습니다" 보안 경고
   원인: 관리자 권한 없이 실행하여 CA 인증서 미설치
   해결: BAT 파일을 우클릭 > "관리자 권한으로 실행"

   증상: "사이트에 연결할 수 없습니다" (ERR_NAME_NOT_RESOLVED)
   원인: hosts 파일 등록 실패
   해결: 관리자 권한으로 재실행, 또는 수동으로 hosts 파일 편집
         C:\Windows\System32\drivers\etc\hosts 에 아래 추가:
         10.0.0.220    pkd.smartcoreinc.com

   증상: 연결 시간 초과
   원인: 서버(10.0.0.220)에 네트워크 연결 불가
   해결: 서버와 같은 네트워크에 연결되어 있는지 확인

   증상: 스크립트 실행 시 빨간색 에러 메시지
   원인: PowerShell 실행 정책 차단
   해결: [방법 2]의 명령어로 직접 실행


 7. 서버 정보

   도메인:  pkd.smartcoreinc.com
   IP:      10.0.0.220
   포트:    443 (HTTPS)
   CA:      ICAO Local PKD Private CA (유효기간: 2026~2036)

=====================================
