<#
  Выкладка Глеботрона на сервер с Windows: тесты, сборка интерфейса, архив, scp и install.sh.

  Из папки Glebotron:
    powershell -ExecutionPolicy Bypass -File deploy\deploy.ps1
  Параметры:
    -Key       путь к ключу SSH (по умолчанию Desktop\cosmos1562-key.pem)
    -Server    пользователь@сервер (по умолчанию ubuntu@cosmos1562.space)
    -SkipBuild не пересобирать интерфейс (static/ уже собран)
    -SkipTests не запускать pytest
    -DryRun    только собрать архив и показать его содержимое, на сервер ничего не отправлять
#>
param(
  [string]$Key = (Join-Path $HOME "Desktop\cosmos1562-key.pem"),
  [string]$Server = "ubuntu@cosmos1562.space",
  [switch]$SkipBuild,
  [switch]$SkipTests,
  [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what завершился с ошибкой ($LASTEXITCODE)" } }

if (-not $SkipTests) {
  Step "Тесты"
  python -m pytest -q -p no:cacheprovider
  Check "pytest"
}

if (-not $SkipBuild) {
  Step "Сборка интерфейса"
  Push-Location frontend
  try {
    if (-not (Test-Path node_modules)) { npm install --no-audit --no-fund; Check "npm install" }
    npm run build
    Check "npm run build"
  } finally {
    Pop-Location
  }
}
if (-not (Test-Path static\index.html)) { throw "Нет static\index.html: соберите интерфейс (без -SkipBuild)" }

Step "Архив"
$archive = Join-Path $env:TEMP "glebotron.tgz"
$items = @(Get-ChildItem -Name -File *.py) + @("requirements.txt", "static", "tools", "deploy")
& "$env:SystemRoot\System32\tar.exe" -czf $archive --exclude=__pycache__ --exclude=*.pyc @items
Check "tar"
$size = [math]::Round((Get-Item $archive).Length / 1KB)
Write-Host "    $archive, $size КБ"

if ($DryRun) {
  & "$env:SystemRoot\System32\tar.exe" -tzf $archive
  Write-Host "DryRun: на сервер ничего не отправлено"
  return
}

if (-not (Test-Path $Key)) { throw "Нет ключа $Key (укажите -Key)" }

Step "Копирование на $Server"
scp -i $Key $archive "${Server}:/tmp/glebotron.tgz"
Check "scp"

Step "Установка на сервере"
$remote = 'rm -rf /tmp/glb && mkdir -p /tmp/glb && tar -xzf /tmp/glebotron.tgz -C /tmp/glb && ' +
          'sudo bash /tmp/glb/deploy/install.sh; rc=$?; rm -rf /tmp/glb /tmp/glebotron.tgz; exit $rc'
ssh -i $Key $Server $remote
Check "install.sh"
Remove-Item $archive -ErrorAction SilentlyContinue
