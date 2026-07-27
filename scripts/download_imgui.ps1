$destDir = "$PSScriptRoot\..\server\thirdparty\imgui"
if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
}

$baseUrl = "https://raw.githubusercontent.com/ocornut/imgui/master"
$files = @(
    "imconfig.h",
    "imgui.h",
    "imgui.cpp",
    "imgui_draw.cpp",
    "imgui_widgets.cpp",
    "imgui_tables.cpp",
    "imgui_internal.h",
    "imstb_textedit.h",
    "imstb_rectpack.h",
    "imstb_truetype.h",
    "backends/imgui_impl_win32.h",
    "backends/imgui_impl_win32.cpp",
    "backends/imgui_impl_dx11.h",
    "backends/imgui_impl_dx11.cpp",
    "backends/imgui_impl_glfw.h",
    "backends/imgui_impl_glfw.cpp",
    "backends/imgui_impl_opengl3.h",
    "backends/imgui_impl_opengl3.cpp",
    "backends/imgui_impl_opengl3_loader.h"
)

foreach ($f in $files) {
    $fileName = [System.IO.Path]::GetFileName($f)
    $url = "$baseUrl/$f"
    $targetPath = Join-Path $destDir $fileName
    Write-Host "Downloading $fileName..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $targetPath
}
Write-Host "Dear ImGui downloaded successfully to $destDir" -ForegroundColor Green
