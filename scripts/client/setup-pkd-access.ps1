<#
.SYNOPSIS
    ICAO Local PKD - Windows HTTPS Access Setup

.DESCRIPTION
    Setup HTTPS access to both Production and Dev Local PKD servers.
    1. Add pkd.smartcoreinc.com -> 10.0.0.220 to hosts file (Production)
    2. Add dev.pkd.smartcoreinc.com -> 127.0.0.1 to hosts file (Dev Local)
    3. Register both Private CA certificates to Trusted Root CA store

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File setup-pkd-access.ps1
#>

# ============================================
# Server Configuration
# ============================================
$Servers = @(
    @{
        Name = "Production"
        IP = "10.0.0.220"
        Domain = "pkd.smartcoreinc.com"
        # Production CA cert (DER base64) - Generated 2026-02-27, Expires 2036-02-25
        CACertBase64 = "MIIFpzCCA4+gAwIBAgIUS95taOjHyXUhRvDLhLmLbzzYY2EwDQYJKoZIhvcNAQELBQAwYzELMAkGA1UEBhMCS1IxFzAVBgNVBAoMDlNtYXJ0Q29yZSBJbmMuMRcwFQYDVQQLDA5QS0QgT3BlcmF0aW9uczEiMCAGA1UEAwwZSUNBTyBMb2NhbCBQS0QgUHJpdmF0ZSBDQTAeFw0yNjAyMjcwMDU4MzRaFw0zNjAyMjUwMDU4MzRaMGMxCzAJBgNVBAYTAktSMRcwFQYDVQQKDA5TbWFydENvcmUgSW5jLjEXMBUGA1UECwwOUEtEIE9wZXJhdGlvbnMxIjAgBgNVBAMMGUlDQU8gTG9jYWwgUEtEIFByaXZhdGUgQ0EwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQC++i2pYuZ+6pPdMCtrOM4K3csiQ4Hs12PAF1V/4XVwfPfxF24lz0FXDeTI5544t4tX70mdvptxLvGij5BdrmTclHE3iMxvx655TwlqJy6MPWXIYs6NFMFokP+YEG9KVYuyUWayGgsU4t95GA/Dl43pF46VozvNZ1Y4C56cpukNwz1Ai364f1IYDGgpsP3x6TpSLDxCaUZWeeEaiKrLe3xYY2GzIqyIy3lCQhiIUfTbA817IbSCzVmTQPFBvhDhNODdngmmnYQCi7W9D5R4woSYO/pL5r21FFoyRq6tAtpS7FSX/4tNugbTEUF5RLk0dDpT8Kh5uy3zNYw8dMWZj/av9zBcm/6F0mOY09pBirPLT0pVwylNmjJdkGvqKXEcFm/IX2LlB17Yjo/v3kT7PeGr7N8wh3uHTeb3tLnoOwC1vaAyKqnKwIY0kA4dFROefSya2iUHZ9MHsUKLsMAuQ4o9ruBXeBljyRvyuxNx0wUesXr6asgfi4zr+gSI27LDGRJgEepoKgEBI2QaAoeaH0GjhmBIcGd5XbLotmF5EhUNIdAd9tpBWJ8SDK20ZcRsRCjEnF1xr3NEW4injtqJQ5WFMm5jqPRL8dUsPZxE1FU4wrkk8nusvfk4NgicpimovHl+pg4Y74lsiKpZb5FSwZpt3IHDMsqnGkrFJlOO1PzsVQIDAQABo1MwUTAdBgNVHQ4EFgQUUPSTwHA8cm4e0GViyEw93RQY6t0wHwYDVR0jBBgwFoAUUPSTwHA8cm4e0GViyEw93RQY6t0wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAgEAG1fN+NJprhlFiKHRYOiBpqbf0PLs0ACZoEi/eVxaxDB7MWeDalsamEMA5qZsGpsWUEnTyY1Ud7QAvfndcmaBhwz/nJxX07VobKpg5wQDkHu8LYuYpOqFzwGqZUAKLNKpsipNYazWkwFXR32AIX03L5zcd+qc6gldgDPEN20bd2qGsy68cyXXA57YtO78vDKCa6M7st3sir8N8T2mblq/dx2pv37YQudfjOrLpRj6YdTn5KMfq6/tML5Sz/Om7TpOOM3zV17PGvJerXs8NMN0NlN0KYs8Gern03VsfclB3F4AGeoZAhBBfaavbO5dbnbRlb3ZmoPzOJFFsWgNQeWyPYUKw+W/OMGU3f/IcNinxr6ADAnPKb08JEVghwIK17uZAkPKQMyRLczj9Fd35KXlOs9RbbuptrNqb7qPtKTN1JrDO6UNf2/JWuYJyfoOLVNuXz5GhEKLV9+odzzspIcuPnfsEqUcDZ2aGkK3wOgup4e0ewDufBDeP8TUpoY86B37woSXfHqcMfbam1BdGJQmoIzbr0y+RRS6WzgntdBumyiCdPgnL7rqsk04zH10Mk5xmZNjGXJdPay6y9xlJyG5o6wNAofMh3ZyhGcG8CDoCUvoVNE64UF3ttDYjVNl7YTkfHW/wyxeuJv/p2RGjJLarQSutCYGADXAlqrBGIJgEtw="
    },
    @{
        Name = "Dev Local"
        IP = "127.0.0.1"
        Domain = "dev.pkd.smartcoreinc.com"
        # Dev Local CA cert (DER base64) - Generated 2026-03-04, Expires 2036-03-01
        CACertBase64 = "MIIFpzCCA4+gAwIBAgIUOZfX+50A24HtOEs6hQlIVEFn+TgwDQYJKoZIhvcNAQELBQAwYzELMAkGA1UEBhMCS1IxFzAVBgNVBAoMDlNtYXJ0Q29yZSBJbmMuMRcwFQYDVQQLDA5QS0QgT3BlcmF0aW9uczEiMCAGA1UEAwwZSUNBTyBMb2NhbCBQS0QgUHJpdmF0ZSBDQTAeFw0yNjAzMDQwNDI1MTdaFw0zNjAzMDEwNDI1MTdaMGMxCzAJBgNVBAYTAktSMRcwFQYDVQQKDA5TbWFydENvcmUgSW5jLjEXMBUGA1UECwwOUEtEIE9wZXJhdGlvbnMxIjAgBgNVBAMMGUlDQU8gTG9jYWwgUEtEIFByaXZhdGUgQ0EwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQChYCWDA6tJjbHSKLu0y4rYraoAk9GRHOwdno1W6csUJcQnkGAuLPK8p+QtVmnMkebN4NxY716TMMGj8aSXOAxgTf/G1NfI/CkG7rTJehY9jQPW7ZckMK2R443nW3sib1ZWQVFqZI1TtSyzFGiDrjqYBEvGhZm4luWoKC0NZMFLOTJ+r747ZLiB/u1LXGrvIKBe09PNWthaQ2/wslxcLaE76yALPzRj/XjWmt67EmHQzLjJ1yEaaCtZW1LpZklpbIZRvc4uzdhnye5zhRRH8Q+2447b2c1OdDEHCxjbeA9ygyOzpfZa9Yz8dFG3rLENNR/IkJkdrC7S/4jc/EmcYiMSO3qOO35Dz6AYp+JPsX0JEYrMaEdcyMZbGQG/ANHQS9zRYC2eFbSxx134N/TT+46yufCeMcSGqM7fAHfhAeV4OAdbx/N+UKUAFPDYFv2HeZNKS+MN/oG0yiU+Q+w+LPr0ZvH3+1VV47YM/DeqJ6eUmEZ6kpoM1NjeDBsWgI4OOn78UYhQx+o32xDE20msHmhdx4Xo5yBbB8WDYBZvLxMlQkPt9EdyMBYUbvFNgJKAxEGsbQR1CntGcQShvTnw6tAHAS97nU1ToHYlcnzOMLamJkN7LqZ68/NmUO8JLvjCVLfkKM1xNNfHjXl4Bk7h6hDm0xFp3OdwWVJloCdLfznzpwIDAQABo1MwUTAdBgNVHQ4EFgQUyApUSH7gEc+cetYfb9rusfDSud8wHwYDVR0jBBgwFoAUyApUSH7gEc+cetYfb9rusfDSud8wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAgEAerG5KB4jNoirlQqin7pkJYmh6f6gz4f/e8FoTnJi6S/69T9CGVYEL81k6+IFEFUPF8SwurmN9VrjCyaEys88rNyBB6mvmNrySWFjuwApnU/yUlJSWDV18J2695oTvm6H60VX2oRK4Jj/fhC7Qc0W4txJA7AMU/FgIwlKvuIP/0aglKrjnvGW5t6DrvQoSkceIXuRZeKP6orZisn7TeitSGm02JqFiNFsYAeTf0cNcXT7XsunKektMo+BE7ugEAgeRrGS3JZd46EgZDDj8yuKsXi40l5QdDc5aR5To3FKSZt8Wp0ClVR7kWPAIr/LigxgZAG5fNJL1in2xP91J8WpL6D0UUWEt7r91tmwQr3My5E2fokG/hyDnIprzBNsUpaMoVR33OXuHN5wTEUbusk8kowEjAEtFHH21hFNtEeOHfrpm4RLuj1Bth/A/3FfDJjGwqcdtlnTEywL4MM/xffqxrm01figH4YqBGAhgDKlvQJi1witGqKDQSW9eBTFJvb48DX9xuu0kPnk9UreVMFfc2wA5BUCW5WnYY0ld47ij3D3r3N3a6BM1lYokFyZ4De1Qfsjw9HK51ZjR0jOF5AApSjsjUMhwcOdTABODFChA5RyjOUYdfdAJLzo/z9reu7K7mybhh8L9EkAkBS6jDEs5COeSM6G1aRhridMlJXdA+Q="
    }
)

$logFile = Join-Path $env:TEMP "pkd-setup-log.txt"
Start-Transcript -Path $logFile -Force | Out-Null

try {
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "  ICAO Local PKD - HTTPS Access Setup" -ForegroundColor Cyan
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "  Servers: $($Servers.Count) (Production + Dev Local)"
    Write-Host ""

    # Admin check
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "[ERROR] Administrator privileges required!" -ForegroundColor Red
        throw "Not running as Administrator"
    }
    Write-Host "  Administrator: OK" -ForegroundColor Green
    Write-Host ""

    # ============================================
    # Step 1: hosts file (all servers)
    # ============================================
    Write-Host "[1/3] Configuring hosts file..." -ForegroundColor Yellow
    $hostsPath = "$env:SystemRoot\System32\drivers\etc\hosts"

    foreach ($srv in $Servers) {
        $hostsEntry = "$($srv.IP)`t$($srv.Domain)"
        $hostsWritten = $false
        for ($retry = 0; $retry -lt 3; $retry++) {
            try {
                $hostsContent = Get-Content $hostsPath -Raw -ErrorAction SilentlyContinue
                if ($hostsContent -and $hostsContent -match [regex]::Escape($srv.Domain)) {
                    $lines = Get-Content $hostsPath
                    $newLines = $lines | Where-Object { $_ -notmatch [regex]::Escape($srv.Domain) }
                    $newLines += $hostsEntry
                    $newLines | Set-Content $hostsPath -Encoding ASCII
                    Write-Host "      [$($srv.Name)] Updated: $hostsEntry" -ForegroundColor Green
                } else {
                    Add-Content $hostsPath -Value "`r`n$hostsEntry" -Encoding ASCII
                    Write-Host "      [$($srv.Name)] Added: $hostsEntry" -ForegroundColor Green
                }
                $hostsWritten = $true
                break
            } catch {
                if ($retry -lt 2) { Start-Sleep -Milliseconds 500 }
            }
        }
        if (-not $hostsWritten) {
            Write-Host "      [$($srv.Name)] FAILED to write hosts file" -ForegroundColor Red
        }
    }
    ipconfig /flushdns | Out-Null
    Write-Host "      DNS cache flushed" -ForegroundColor Green

    # ============================================
    # Step 2: Remove ALL old CA certs
    # ============================================
    Write-Host ""
    Write-Host "[2/3] Cleaning old CA certificates..." -ForegroundColor Yellow

    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store("Root", "LocalMachine")
    $store.Open("ReadWrite")

    $caSubjectCN = "ICAO Local PKD Private CA"
    $oldCerts = $store.Certificates | Where-Object { $_.Subject -match $caSubjectCN }
    $removedCount = 0
    foreach ($old in $oldCerts) {
        Write-Host "      Removing: $($old.Thumbprint.Substring(0,16))... (Expires: $($old.NotAfter.ToString('yyyy-MM-dd')))" -ForegroundColor Yellow
        $store.Remove($old)
        $removedCount++
    }
    if ($removedCount -gt 0) {
        Write-Host "      Removed $removedCount old certificate(s)" -ForegroundColor Yellow
    } else {
        Write-Host "      No old certificates found" -ForegroundColor Gray
    }

    # ============================================
    # Step 3: Install new CA certs
    # ============================================
    Write-Host ""
    Write-Host "[3/3] Registering CA certificates..." -ForegroundColor Yellow

    foreach ($srv in $Servers) {
        $certBytes = [Convert]::FromBase64String($srv.CACertBase64)
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(,$certBytes)

        $store.Add($cert)

        # Verify
        $check = Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Thumbprint -eq $cert.Thumbprint }
        if ($check) {
            Write-Host "      [$($srv.Name)] Installed: $($cert.Thumbprint.Substring(0,16))... (Expires: $($cert.NotAfter.ToString('yyyy-MM-dd')))" -ForegroundColor Green
        } else {
            Write-Host "      [$($srv.Name)] FAILED to install!" -ForegroundColor Red
        }
    }

    $store.Close()

    # ============================================
    # Done
    # ============================================
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Green
    Write-Host "  Setup Complete!" -ForegroundColor Green
    Write-Host "============================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Production:  https://pkd.smartcoreinc.com" -ForegroundColor Cyan
    Write-Host "  Dev Local:   https://dev.pkd.smartcoreinc.com" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  (Restart Chrome/Edge after setup)" -ForegroundColor Gray
    Write-Host "  (Firefox requires separate certificate import)" -ForegroundColor Gray
    Write-Host ""

    # Connection tests
    Write-Host "Testing connections..." -ForegroundColor Yellow
    foreach ($srv in $Servers) {
        try {
            $resp = Invoke-WebRequest -Uri "https://$($srv.Domain)/api/health" -UseBasicParsing -TimeoutSec 5
            Write-Host "  [$($srv.Name)] HTTPS OK (HTTP $($resp.StatusCode))" -ForegroundColor Green
        } catch {
            Write-Host "  [$($srv.Name)] Connection failed (check network to $($srv.IP))" -ForegroundColor Yellow
        }
    }

} catch {
    Write-Host ""
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "  Log: $logFile" -ForegroundColor Gray
}

Stop-Transcript | Out-Null
Write-Host ""
Write-Host "Log saved: $logFile"
Write-Host "Press any key to exit..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
