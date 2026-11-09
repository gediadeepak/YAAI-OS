param([string]$DiskPath = "$(Split-Path $PSScriptRoot -Parent)\VDI\YAAI-documents.vdi")
$ErrorActionPreference = 'Stop'
$targetPath = [IO.Path]::GetFullPath($DiskPath)
if (Test-Path -LiteralPath $targetPath) { Write-Output "Existing documents disk preserved: $targetPath"; return }
$exportId = [guid]::NewGuid().ToString('N')
$rawPath = Join-Path $PSScriptRoot "build\documents-export-$exportId.img"
$temporaryVdi = Join-Path $PSScriptRoot "build\documents-export-$exportId.vdi"
try {
  & "$PSScriptRoot\create_documents.ps1" -DiskPath $rawPath
  & 'C:\Program Files\qemu\qemu-img.exe' convert -f raw -O vdi $rawPath $temporaryVdi
  if ($LASTEXITCODE -ne 0) { throw 'Documents disk conversion failed.' }
  New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($targetPath)) | Out-Null
  [IO.File]::Move($temporaryVdi,$targetPath) # Fails rather than overwriting an existing disk.
  Write-Output "VirtualBox documents disk: $targetPath"
} finally {
  Remove-Item -LiteralPath $rawPath,$temporaryVdi -ErrorAction SilentlyContinue
}
