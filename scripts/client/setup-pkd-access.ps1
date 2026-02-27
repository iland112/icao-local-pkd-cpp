<#
.SYNOPSIS
    ICAO Local PKD - Windows HTTPS Access Setup

.DESCRIPTION
    Setup HTTPS access to pkd.smartcoreinc.com (10.0.0.220)
    1. Add pkd.smartcoreinc.com -> 10.0.0.220 to hosts file
    2. Register Private CA certificate to Trusted Root CA store

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File setup-pkd-access.ps1
#>

$ServerIP = "10.0.0.220"
$ServerDomain = "pkd.smartcoreinc.com"
$CACertBase64 = "MIIFpzCCA4+gAwIBAgIUS95taOjHyXUhRvDLhLmLbzzYY2EwDQYJKoZIhvcNAQELBQAwYzELMAkGA1UEBhMCS1IxFzAVBgNVBAoMDlNtYXJ0Q29yZSBJbmMuMRcwFQYDVQQLDA5QS0QgT3BlcmF0aW9uczEiMCAGA1UEAwwZSUNBTyBMb2NhbCBQS0QgUHJpdmF0ZSBDQTAeFw0yNjAyMjcwMDU4MzRaFw0zNjAyMjUwMDU4MzRaMGMxCzAJBgNVBAYTAktSMRcwFQYDVQQKDA5TbWFydENvcmUgSW5jLjEXMBUGA1UECwwOUEtEIE9wZXJhdGlvbnMxIjAgBgNVBAMMGUlDQU8gTG9jYWwgUEtEIFByaXZhdGUgQ0EwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQC++i2pYuZ+6pPdMCtrOM4K3csiQ4Hs12PAF1V/4XVwfPfxF24lz0FXDeTI5544t4tX70mdvptxLvGij5BdrmTclHE3iMxvx655TwlqJy6MPWXIYs6NFMFokP+YEG9KVYuyUWayGgsU4t95GA/Dl43pF46VozvNZ1Y4C56cpukNwz1Ai364f1IYDGgpsP3x6TpSLDxCaUZWeeEaiKrLe3xYY2GzIqyIy3lCQhiIUfTbA817IbSCzVmTQPFBvhDhNODdngmmnYQCi7W9D5R4woSYO/pL5r21FFoyRq6tAtpS7FSX/4tNugbTEUF5RLk0dDpT8Kh5uy3zNYw8dMWZj/av9zBcm/6F0mOY09pBirPLT0pVwylNmjJdkGvqKXEcFm/IX2LlB17Yjo/v3kT7PeGr7N8wh3uHTeb3tLnoOwC1vaAyKqnKwIY0kA4dFROefSya2iUHZ9MHsUKLsMAuQ4o9ruBXeBljyRvyuxNx0wUesXr6asgfi4zr+gSI27LDGRJgEepoKgEBI2QaAoeaH0GjhmBIcGd5XbLotmF5EhUNIdAd9tpBWJ8SDK20ZcRsRCjEnF1xr3NEW4injtqJQ5WFMm5jqPRL8dUsPZxE1FU4wrkk8nusvfk4NgicpimovHl+pg4Y74lsiKpZb5FSwZpt3IHDMsqnGkrFJlOO1PzsVQIDAQABo1MwUTAdBgNVHQ4EFgQUUPSTwHA8cm4e0GViyEw93RQY6t0wHwYDVR0jBBgwFoAUUPSTwHA8cm4e0GViyEw93RQY6t0wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAgEAG1fN+NJprhlFiKHRYOiBpqbf0PLs0ACZoEi/eVxaxDB7MWeDalsamEMA5qZsGpsWUEnTyY1Ud7QAvfndcmaBhwz/nJxX07VobKpg5wQDkHu8LYuYpOqFzwGqZUAKLNKpsipNYazWkwFXR32AIX03L5zcd+qc6gldgDPEN20bd2qGsy68cyXXA57YtO78vDKCa6M7st3sir8N8T2mblq/dx2pv37YQudfjOrLpRj6YdTn5KMfq6/tML5Sz/Om7TpOOM3zV17PGvJerXs8NMN0NlN0KYs8Gern03VsfclB3F4AGeoZAhBBfaavbO5dbnbRlb3ZmoPzOJFFsWgNQeWyPYUKw+W/OMGU3f/IcNinxr6ADAnPKb08JEVghwIK17uZAkPKQMyRLczj9Fd35KXlOs9RbbuptrNqb7qPtKTN1JrDO6UNf2/JWuYJyfoOLVNuXz5GhEKLV9+odzzspIcuPnfsEqUcDZ2aGkK3wOgup4e0ewDufBDeP8TUpoY86B37woSXfHqcMfbam1BdGJQmoIzbr0y+RRS6WzgntdBumyiCdPgnL7rqsk04zH10Mk5xmZNjGXJdPay6y9xlJyG5o6wNAofMh3ZyhGcG8CDoCUvoVNE64UF3ttDYjVNl7YTkfHW/wyxeuJv/p2RGjJLarQSutCYGADXAlqrBGIJgEtw="

$logFile = Join-Path $env:TEMP "pkd-setup-log.txt"
Start-Transcript -Path $logFile -Force | Out-Null

try {
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "  ICAO Local PKD - HTTPS Access Setup" -ForegroundColor Cyan
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "  Server: $ServerDomain ($ServerIP)"
    Write-Host ""

    # Admin check
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "[ERROR] Administrator privileges required!" -ForegroundColor Red
        throw "Not running as Administrator"
    }
    Write-Host "  Administrator: OK" -ForegroundColor Green
    Write-Host ""

    # Step 1: hosts file
    Write-Host "[1/2] Configuring hosts file..." -ForegroundColor Yellow
    $hostsPath = "$env:SystemRoot\System32\drivers\etc\hosts"
    $hostsEntry = "$ServerIP`t$ServerDomain"

    $hostsWritten = $false
    for ($retry = 0; $retry -lt 3; $retry++) {
        try {
            $hostsContent = Get-Content $hostsPath -Raw -ErrorAction SilentlyContinue
            if ($hostsContent -and $hostsContent -match [regex]::Escape($ServerDomain)) {
                $lines = Get-Content $hostsPath
                $newLines = $lines | Where-Object { $_ -notmatch [regex]::Escape($ServerDomain) }
                $newLines += $hostsEntry
                $newLines | Set-Content $hostsPath -Encoding ASCII
                Write-Host "      Updated existing entry: $hostsEntry" -ForegroundColor Green
            } else {
                Add-Content $hostsPath -Value "`r`n$hostsEntry" -Encoding ASCII
                Write-Host "      Added entry: $hostsEntry" -ForegroundColor Green
            }
            $hostsWritten = $true
            break
        } catch {
            if ($retry -lt 2) { Start-Sleep -Milliseconds 500 }
        }
    }
    if (-not $hostsWritten) {
        Write-Host "      FAILED to write hosts file (file locked)" -ForegroundColor Red
    }
    ipconfig /flushdns | Out-Null
    Write-Host "      DNS cache flushed" -ForegroundColor Green

    # Step 2: CA certificate
    Write-Host ""
    Write-Host "[2/2] Registering CA certificate..." -ForegroundColor Yellow

    $certBytes = [Convert]::FromBase64String($CACertBase64)
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(,$certBytes)
    Write-Host "      Subject: $($cert.Subject)" -ForegroundColor Gray
    Write-Host "      Thumbprint: $($cert.Thumbprint)" -ForegroundColor Gray

    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store("Root", "LocalMachine")
    $store.Open("ReadWrite")

    # Remove ALL existing "ICAO Local PKD Private CA" certs (handles CA regeneration)
    $caSubjectCN = "ICAO Local PKD Private CA"
    $oldCerts = $store.Certificates | Where-Object { $_.Subject -match $caSubjectCN }
    $removedCount = 0
    foreach ($old in $oldCerts) {
        Write-Host "      Removing old cert: $($old.Thumbprint) (Expires: $($old.NotAfter))" -ForegroundColor Yellow
        $store.Remove($old)
        $removedCount++
    }
    if ($removedCount -gt 0) {
        Write-Host "      Removed $removedCount old certificate(s)" -ForegroundColor Yellow
    }

    # Install new certificate
    $store.Add($cert)
    Write-Host "      Registered to Trusted Root CA store!" -ForegroundColor Green
    $store.Close()

    # Verify
    $check = Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Thumbprint -eq $cert.Thumbprint }
    if ($check) {
        Write-Host "      Verified: certificate found in trust store" -ForegroundColor Green
    } else {
        Write-Host "      FAILED: certificate NOT found in trust store!" -ForegroundColor Red
    }

    # Done
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Green
    Write-Host "  Setup Complete!" -ForegroundColor Green
    Write-Host "============================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Open in Chrome/Edge: https://$ServerDomain" -ForegroundColor Cyan
    Write-Host "  (Firefox requires separate certificate import)" -ForegroundColor Gray
    Write-Host ""

    # Connection test
    Write-Host "Testing connection..." -ForegroundColor Yellow
    try {
        $resp = Invoke-WebRequest -Uri "https://${ServerDomain}/health" -UseBasicParsing -TimeoutSec 5
        Write-Host "  HTTPS connection OK! (HTTP $($resp.StatusCode))" -ForegroundColor Green
    } catch {
        Write-Host "  Connection failed (check network to $ServerIP)" -ForegroundColor Yellow
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
