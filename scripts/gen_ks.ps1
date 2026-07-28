$cert = New-SelfSignedCertificate -CertStoreLocation 'cert:\CurrentUser\My' -Subject 'CN=Yanich DeskSound' -NotAfter (Get-Date).AddYears(30)
$bytes = $cert.Export('Pkcs12', 'desksound123')
$outPath = Resolve-Path "$PSScriptRoot\..\client-android\app"
$ksFile = "$outPath\desksound.keystore"
[System.IO.File]::WriteAllBytes($ksFile, $bytes)
Write-Host "[+] Generated PKCS12 keystore at $ksFile ($($bytes.Length) bytes)" -ForegroundColor Green
