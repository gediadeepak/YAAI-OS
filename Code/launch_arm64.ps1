param([switch]$Headless)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& "$PSScriptRoot\build.ps1" -Target arm64
$runDirectory = Join-Path $PSScriptRoot ('build\arm64\run-' + [guid]::NewGuid().ToString('N'))
$bootDirectory = Join-Path $runDirectory 'esp\EFI\BOOT'
New-Item -ItemType Directory -Force $bootDirectory | Out-Null
Copy-Item 'build\arm64\BOOTAA64.EFI' "$bootDirectory\BOOTAA64.EFI"
Copy-Item 'assets\font-licenses.txt' "$runDirectory\esp\FONTS.TXT"
$variables = Join-Path $PSScriptRoot 'build\arm64\YAAI-vars.fd'
if (-not (Test-Path -LiteralPath $variables)) { Copy-Item 'C:\Program Files\qemu\share\edk2-arm-vars.fd' $variables }
$documents = Join-Path $PSScriptRoot 'build\arm64\YAAI-documents.img'
& "$PSScriptRoot\create_documents.ps1" -DiskPath $documents
Write-Output 'ARM64 QEMU virt preview. Click the QEMU display and use its keyboard and pointer.'
Write-Output 'F1 Terminal, F2 System, F3 Devices; right-click to change wallpaper. Serial fallback:'
Write-Output 'Ctrl-T Terminal, Ctrl-Y System, Ctrl-D Devices. Ctrl-A then C switches to the QEMU monitor; type quit to exit.'
$displayArguments = if ($Headless) { @('-display','none') } else { @() }
try {
  & 'C:\Program Files\qemu\qemu-system-aarch64.exe' -machine virt,highmem=off -cpu cortex-a72 -m 512M -rtc base=utc -device ramfb `
    -global virtio-mmio.force-legacy=false -device virtio-keyboard-device -device virtio-tablet-device `
    -drive "if=none,id=documents,format=raw,file=$documents" -device virtio-blk-device,drive=documents `
    -drive 'if=pflash,format=raw,readonly=on,file=C:\Program Files\qemu\share\edk2-aarch64-code.fd' `
    -drive "if=pflash,format=raw,file=$variables" `
    -drive "if=none,id=boot,format=raw,snapshot=on,file=fat:$runDirectory\esp" -device virtio-blk-pci,drive=boot,bootindex=1 `
    -chardev "stdio,id=uart,mux=on,logfile=$runDirectory\serial.log" -serial chardev:uart -mon chardev=uart,mode=readline -net none @displayArguments
  if ($LASTEXITCODE -ne 0) { throw "ARM64 QEMU failed. See $runDirectory\serial.log" }
} finally {
  Remove-Item -LiteralPath "$bootDirectory\BOOTAA64.EFI", "$runDirectory\esp\FONTS.TXT" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $bootDirectory, "$runDirectory\esp\EFI", "$runDirectory\esp" -ErrorAction SilentlyContinue
}
