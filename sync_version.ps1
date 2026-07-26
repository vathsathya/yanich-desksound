$versionPath = "$PSScriptRoot\version.txt"
$headerPath = "$PSScriptRoot\version.h"

if (Test-Path $versionPath) {
    $v = (Get-Content $versionPath -Raw).Trim()
    $parts = $v.Split('.')
    $major = if ($parts.Length -gt 0) { $parts[0] } else { "1" }
    $minor = if ($parts.Length -gt 1) { $parts[1] } else { "0" }
    $patch = if ($parts.Length -gt 2) { $parts[2] } else { "0" }

    $hContent = @"
#ifndef VERSION_H
#define VERSION_H

#define APP_VERSION_MAJOR $major
#define APP_VERSION_MINOR $minor
#define APP_VERSION_PATCH $patch
#define APP_VERSION_BUILD 0

#define APP_VERSION_STRING "$v"
#define APP_VERSION_TAG "v$v"

#endif // VERSION_H

"@
    [System.IO.File]::WriteAllText($headerPath, $hContent.Replace("`r`n", "`n").Replace("`n", "`r`n"))
    Write-Host "Synced version.h to $v" -ForegroundColor Green
}
