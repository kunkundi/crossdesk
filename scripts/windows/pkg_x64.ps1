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

function Convert-To-WindowsVersion {
  param(
    [Parameter(Mandatory = $true)]
    [string]$InputVersion
  )

  $body = $InputVersion -replace '^v', ''
  $baseMatch = [regex]::Match($body, '^[0-9]+(?:\.[0-9]+){0,3}')
  if (!$baseMatch.Success) {
    return "0.0.0.0"
  }

  $parts = [System.Collections.Generic.List[int]]::new()
  foreach ($part in $baseMatch.Value.Split('.')) {
    $value = [int]$part
    if ($value -gt 65535) {
      $value = 0
    }
    $parts.Add($value)
  }
  while ($parts.Count -lt 4) {
    $parts.Add(0)
  }

  return ($parts -join '.')
}

$normalizedVersion = Normalize-AppVersion -InputVersion $Version
$windowsVersion = Convert-To-WindowsVersion -InputVersion $normalizedVersion
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Push-Location $scriptDir
try {
  & makensis "/DVERSION=$normalizedVersion" "/DVERSION_NUMERIC=$windowsVersion" "nsis_script.nsi"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
}
