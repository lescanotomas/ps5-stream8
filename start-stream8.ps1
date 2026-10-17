param(
    [string]$Ps5Ip = "192.168.1.50",
    [switch]$NoBrowser,
    [switch]$Tab
)

$arguments = @(
    (Join-Path $PSScriptRoot "tools\start_stream8.py"),
    "--ps5-host", $Ps5Ip
)

if ($NoBrowser) {
    $arguments += "--no-browser"
}

if ($Tab) { $arguments += "--tab" }
python @arguments
exit $LASTEXITCODE
