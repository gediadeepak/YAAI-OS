param([switch]$InstallerOnly)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& "$PSScriptRoot\build.ps1"

# A new disk UUID and filename let an existing VM keep its previous build.
$exportId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$stagingDirectory = Join-Path $PSScriptRoot "build\export-$exportId"
$bootDirectory = Join-Path $stagingDirectory 'EFI\BOOT'
$diskPath = Join-Path $PSScriptRoot "build\YAAI-installer-$exportId.vdi"
$targetDiskPath = Join-Path $PSScriptRoot "build\YAAI-system-$exportId.vdi"
New-Item -ItemType Directory -Path $bootDirectory -Force | Out-Null

try {
  Copy-Item "$PSScriptRoot\build\BOOTX64.EFI" "$bootDirectory\BOOTX64.EFI"
  Copy-Item "$PSScriptRoot\assets\font-licenses.txt" "$stagingDirectory\FONTS.TXT"
  # QEMU creates the FAT filesystem and partition table, then converts it to VDI.
  & 'C:\Program Files\qemu\qemu-img.exe' convert -f raw -O vdi "fat:$stagingDirectory" $diskPath
  if ($LASTEXITCODE -ne 0) { throw 'VirtualBox disk export failed.' }
  Write-Output "VirtualBox installer disk: $diskPath"
  if (-not $InstallerOnly) {
    & 'C:\Program Files\qemu\qemu-img.exe' create -f vdi $targetDiskPath 1G
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox target disk creation failed.' }
    Write-Output "Blank installation disk: $targetDiskPath"
  }
} finally {
  Remove-Item -LiteralPath "$bootDirectory\BOOTX64.EFI" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath "$stagingDirectory\FONTS.TXT" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $bootDirectory, "$stagingDirectory\EFI", $stagingDirectory -ErrorAction SilentlyContinue
}
