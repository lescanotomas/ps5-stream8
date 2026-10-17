$ErrorActionPreference = 'Stop'
$app = Join-Path $PSScriptRoot 'stream8_setup.pyw'
$pythonw = Get-Command pythonw -ErrorAction SilentlyContinue

if ($pythonw) {
    Start-Process -FilePath $pythonw.Source -ArgumentList @($app) -WindowStyle Hidden
} else {
    Start-Process -FilePath python -ArgumentList @($app) -WindowStyle Hidden
}
