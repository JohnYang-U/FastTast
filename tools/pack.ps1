# ---------------------------------------------------------------------------
# tools/pack.ps1 -- 打包 FastTest 给另一台电脑
#
#   slim : 源码 + 头文件 + 工具 + 文档        (~6 MB)  目标机已有 META 运行库
#   full : slim + sdk/META/bin + build/bin    (~210 MB) 目标机直接能跑
#
# 用法:  powershell -ExecutionPolicy Bypass -File tools\pack.ps1 -Mode slim
# 或直接用根目录的 pack.bat
# ---------------------------------------------------------------------------
param(
    [ValidateSet('slim','full')]
    [string]$Mode = 'slim',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot          # D:\FastTest
if ([string]::IsNullOrWhiteSpace($OutDir)) { $OutDir = Join-Path $root 'dist' }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$stamp = Get-Date -Format 'yyyyMMdd_HHmm'
$name  = "FastTest_${Mode}_$stamp"
$tmpRoot = $env:TEMP
if ([string]::IsNullOrWhiteSpace($tmpRoot)) { $tmpRoot = $env:TMP }
if ([string]::IsNullOrWhiteSpace($tmpRoot)) { $tmpRoot = [System.IO.Path]::GetTempPath() }
$stage = Join-Path $tmpRoot $name
$zip   = Join-Path $OutDir "$name.zip"

Write-Host "=== FastTest packaging ==="
Write-Host "  mode  : $Mode"
Write-Host "  source: $root"
Write-Host "  output: $zip"
Write-Host ""

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# ---- 总是打进去的 ----
$files = @('README.md','build.bat','pack.bat','CMakeLists.txt','.gitignore')
# 根目录其它 .md（本机文档）也一并带上，不在这里点名
$files += (Get-ChildItem $root -Filter '*.md' -File |
           Where-Object { $files -notcontains $_.Name } |
           ForEach-Object { $_.Name })
foreach ($f in $files) {
    $p = Join-Path $root $f
    if (Test-Path $p) { Copy-Item $p $stage -Force; Write-Host "  + $f" }
}
$dirs = @('src','tools','config','sdk\META\include')
foreach ($d in $dirs) {
    $p = Join-Path $root $d
    if (Test-Path $p) {
        $dst = Join-Path $stage $d
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item $p $dst -Recurse -Force
        $sz = (Get-ChildItem $p -Recurse -File | Measure-Object Length -Sum).Sum
        Write-Host ("  + {0}  ({1:N1} MB)" -f $d, ($sz/1MB))
    }
}

# ---- full 额外打进去 ----
if ($Mode -eq 'full') {
    foreach ($d in @('sdk\META\bin','build\bin')) {
        $p = Join-Path $root $d
        if (Test-Path $p) {
            $dst = Join-Path $stage $d
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
            Copy-Item $p $dst -Recurse -Force
            $sz = (Get-ChildItem $p -Recurse -File | Measure-Object Length -Sum).Sum
            Write-Host ("  + {0}  ({1:N1} MB)" -f $d, ($sz/1MB))
        } else {
            Write-Host "  ! $d not found, skipped"
        }
    }
}

# 不要在包里留下本机的运行痕迹
Get-ChildItem $stage -Recurse -File -Include '*.ini','*.log','*_selftest.txt' -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'METAQuickCheck.ini' -or $_.Name -like '*_selftest.txt' } |
    ForEach-Object { Remove-Item $_.FullName -Force; Write-Host "  - removed local artifact: $($_.Name)" }

Write-Host ""
Write-Host "compressing ..."

# 注意: 不用 Compress-Archive —— Windows PowerShell 5.1 的它会把 zip 内的路径写成
# 反斜杠(违反 ZIP 规范), 部分解压工具会解不出来; 而 .NET 的 ZipFile.CreateFromDirectory
# 同样是反斜杠。
# 改用 Windows 自带的 tar.exe (bsdtar), 它是唯一能产出规范正斜杠 zip 的方式。
# 注意 Get-Command tar.exe 在子进程里可能取不到(路径/PATH 被裁剪), 这里直接查 system32。
$sysRoot = $env:SystemRoot
if ([string]::IsNullOrWhiteSpace($sysRoot)) { $sysRoot = $env:windir }
if ([string]::IsNullOrWhiteSpace($sysRoot)) { $sysRoot = 'C:\Windows' }
$tarPath = Join-Path $sysRoot 'System32\tar.exe'
if (-not (Test-Path -LiteralPath $tarPath)) {
    $c = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($c) { $tarPath = $c.Source } else { $tarPath = '' }
}

if (Test-Path $zip) { Remove-Item $zip -Force }

if ($tarPath -ne '') {
    Write-Host "  using: $tarPath"
    $top = Get-ChildItem $stage -Force | Select-Object -ExpandProperty Name
    & $tarPath -a -c -f $zip -C $stage @top
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
} else {
    Write-Host "  ! tar.exe not found, falling back to Compress-Archive (entry separators may be backslashes)"
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal -Force
}

Remove-Item $stage -Recurse -Force

$z = Get-Item $zip
Add-Type -AssemblyName System.IO.Compression.FileSystem
$entryCount = 0
$ar = [System.IO.Compression.ZipFile]::OpenRead($z.FullName)
try { $entryCount = ($ar.Entries | Where-Object { $_.Name -ne '' }).Count } finally { $ar.Dispose() }

Write-Host ""
Write-Host "=== DONE ==="
Write-Host ("  {0}" -f $z.FullName)
Write-Host ("  {0:N1} MB, {1} files" -f ($z.Length/1MB), $entryCount)
Write-Host ""
if ($Mode -eq 'slim') {
    Write-Host "Slim package: target machine must already have sdk\META\bin (META DLLs)"
    Write-Host "(METACalibrationLibrary.dll / MetaCore.dll / MetaApp.dll / METAInstrumentLibrary.dll ...)"
} else {
    Write-Host "Full package: unzip and run build\bin\METAQuickCheck.exe directly"
}
