param(
 [Parameter(Mandatory=$true)][string]$IsoPath,
 [Parameter(Mandatory=$true)][string]$ExpectedImagePath
)
$ErrorActionPreference='Stop'
$codeDirectory=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not ('YaaiIso' -as [type])){Add-Type -Path (Join-Path $codeDirectory 'tools\IsoImage.cs')}
[YaaiIso]::Verify((Resolve-Path -LiteralPath $IsoPath).Path,(Resolve-Path -LiteralPath $ExpectedImagePath).Path)
