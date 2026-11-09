$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& "$PSScriptRoot\build.ps1"
$qemuShare = 'C:\Program Files\qemu\share'
$systemDisk = Join-Path $PSScriptRoot 'build\YAAI-system.qcow2'
$legacySystemDisk = Join-Path $PSScriptRoot 'build\SHR.I-system.qcow2'
if (-not (Test-Path -LiteralPath $systemDisk) -and (Test-Path -LiteralPath $legacySystemDisk)) {
  $systemDisk = $legacySystemDisk
}
if (-not (Test-Path -LiteralPath $systemDisk)) {
  & 'C:\Program Files\qemu\qemu-img.exe' create -f qcow2 $systemDisk 1G
  if ($LASTEXITCODE -ne 0) { throw 'Cannot create the installation disk.' }
}
$documents = Join-Path $PSScriptRoot 'build\YAAI-documents.img'
$documentsArguments = @()
# Preserve old documents disks, but new installations need only the system disk.
if (Test-Path -LiteralPath $documents) {
  $documentsArguments = @('-drive',"if=none,id=documents,format=raw,file=$documents",'-device','ide-hd,drive=documents,bus=storage.1')
}
$runDirectory = Join-Path $PSScriptRoot ('build\run-' + [guid]::NewGuid().ToString('N'))
$bootDirectory = Join-Path $runDirectory 'esp\EFI\BOOT'
New-Item -ItemType Directory -Path $bootDirectory -Force | Out-Null

try {
  Copy-Item "$PSScriptRoot\build\BOOTX64.EFI" "$bootDirectory\BOOTX64.EFI"
  Copy-Item "$PSScriptRoot\assets\font-licenses.txt" "$runDirectory\esp\FONTS.TXT"
  $variables = Join-Path $PSScriptRoot 'build\YAAI-vars.fd'
  if (-not (Test-Path -LiteralPath $variables)) { Copy-Item "$qemuShare\edk2-i386-vars.fd" $variables }

  & 'C:\Program Files\qemu\qemu-system-x86_64.exe' -machine q35 -m 512M -rtc base=utc `
    -device piix3-ide,id=storage `
    @documentsArguments `
    -drive "if=pflash,format=raw,readonly=on,file=$qemuShare\edk2-x86_64-code.fd" `
    -drive "if=pflash,format=raw,file=$variables" `
    -drive "if=none,id=osdisk,format=raw,snapshot=on,file=fat:$runDirectory\esp" `
    -device ide-hd,drive=osdisk,bus=ide.0,bootindex=1 `
    -drive "if=none,id=systemdisk,format=qcow2,file=$systemDisk" `
    -device ide-hd,drive=systemdisk,bus=storage.0,bootindex=2 `
    -audiodev dsound,id=bootaudio `
    -device AC97,audiodev=bootaudio
  if ($LASTEXITCODE -ne 0) { throw 'QEMU failed.' }
} finally {
  # Delete only the temporary files and directories created for this launch.
  Remove-Item -LiteralPath "$bootDirectory\BOOTX64.EFI", "$runDirectory\esp\FONTS.TXT" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $bootDirectory, "$runDirectory\esp\EFI", "$runDirectory\esp", $runDirectory -ErrorAction SilentlyContinue
}
