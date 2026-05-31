param(
  [Parameter(Mandatory = $true)]
  [string]$Version
)

$ErrorActionPreference = "Stop"

function Normalize-AppVersion {
  param(
    [Parameter(Mandatory = $true)]
    [string]$InputVersion
  )

  $prefix = ""
  $body = $InputVersion

  if ($body.StartsWith("v")) {
    $prefix = "v"
    $body = $body.Substring(1)
  }

  if ($body -match '^([0-9]+(\.[0-9]+){1,3})-([0-9]{8})-([0-9]+)$') {
    return "${prefix}$($Matches[1])-$($Matches[4])-$($Matches[3])"
  }

  return $InputVersion
}

$normalizedVersion = Normalize-AppVersion -InputVersion $Version
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Push-Location $scriptDir
try {
  & makensis "/DVERSION=$normalizedVersion" "nsis_script.nsi"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
}
