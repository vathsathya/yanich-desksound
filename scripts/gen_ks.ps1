$scriptDir = $PSScriptRoot
$rootDir = (Resolve-Path "$scriptDir\..").Path
$ksFile = "$rootDir\client-android\app\desksound.keystore"

$javaExe = Get-ChildItem -Path "$rootDir\jdk" -Recurse -Filter "java.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $javaExe) {
    throw "Java binary not found in $rootDir\jdk"
}

Write-Host "[+] Generating Java PKCS12 keystore at $ksFile..." -ForegroundColor Cyan
& $javaExe.FullName sun.security.tools.keytool.Main -genkeypair -v -keystore $ksFile -alias desksound -keyalg RSA -keysize 2048 -validity 10000 -storepass desksound123 -keypass desksound123 -dname "CN=Yanich DeskSound, OU=Audio, O=Yanich, L=PhnomPenh, C=KH" -storetype PKCS12
Write-Host "[+] Successfully generated desksound.keystore with keyAlias 'desksound'!" -ForegroundColor Green
