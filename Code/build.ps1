param([ValidateSet('x86_64','arm64')][string]$Target = 'x86_64')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$llvmBin = 'C:\Program Files\LLVM\bin'
$outputDirectory = if ($Target -eq 'arm64') { 'build\arm64' } else { 'build' }
New-Item -ItemType Directory -Force $outputDirectory | Out-Null
if (-not (Test-Path 'assets\ui-fonts.bin') -or -not (Test-Path 'assets\wallpaper-modern.rgb') -or -not (Test-Path 'assets\font-licenses.txt')) {
  & "$PSScriptRoot\assets\generate_desktop_assets.ps1"
}
$compilerFlags = @('-std=c11','-O2','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-I.')
$sources = [ordered]@{}
foreach ($source in @('boot','kernel','console','shell','memory','runtime','setup','clock','desktop','gui','input_events','storage','settings','display_mode','files','file_app','volumes','power','editor','window_manager','apps','notes_app','calculator','calculator_app')) { $sources[$source] = "$source.c" }
$sources['platform_compat'] = 'platform/compat.c'
$sources['platform_store'] = 'platform/uefi_store.c'
$assembly = [ordered]@{ gui_assets = 'gui_assets.S' }
if ($Target -eq 'x86_64') {
  $compilerFlags += @('--target=x86_64-pc-windows-msvc','-mno-red-zone')
  foreach ($source in @('audio','exceptions','keyboard','installer','install_disk','ps2','mouse','devices','network')) { $sources[$source] = "$source.c" }
  $sources['platform_hal'] = 'platform/x86_64/hal.c'
  $sources['platform_clock'] = 'platform/x86_64/rtc.c'
  $sources['platform_disk'] = 'platform/x86_64/ata_disk.c'
  $sources['vbox_pointer'] = 'platform/x86_64/vbox_pointer.c'
  $assembly['chime'] = 'chime.S'
  $assembly['exception_stubs'] = 'exceptions.S'
  $efiName = 'BOOTX64.EFI'
  $triple = 'x86_64-pc-windows-msvc'
} else {
  $compilerFlags += @('--target=aarch64-pc-windows-msvc','-mgeneral-regs-only','-DYAAI_LIVE_BOOT')
  $sources['platform_hal'] = 'platform/aarch64/hal.c'
  $sources['virtio_input'] = 'platform/aarch64/virtio_input.c'
  $sources['platform_disk'] = 'platform/aarch64/virtio_disk.c'
  $assembly['platform_vectors'] = 'platform/aarch64/vectors.S'
  $efiName = 'BOOTAA64.EFI'
  $triple = 'aarch64-pc-windows-msvc'
}
$objects = @()
foreach ($entry in $sources.GetEnumerator()) {
  $object = "$outputDirectory\$($entry.Key).obj"
  & "$llvmBin\clang.exe" @compilerFlags -c $entry.Value -o $object
  if ($LASTEXITCODE -ne 0) { throw "$($entry.Value) compilation failed for $Target." }
  $objects += $object
}
foreach ($entry in $assembly.GetEnumerator()) {
  $object = "$outputDirectory\$($entry.Key).obj"
  & "$llvmBin\clang.exe" "--target=$triple" -c $entry.Value -o $object
  if ($LASTEXITCODE -ne 0) { throw "$($entry.Value) assembly failed for $Target." }
  $objects += $object
}
& "$llvmBin\lld-link.exe" /subsystem:efi_application /entry:efi_main /nodefaultlib "/out:$outputDirectory\$efiName" @objects
if ($LASTEXITCODE -ne 0) { throw "Linking failed for $Target." }
Write-Output "Built ${Target}: $PSScriptRoot\$outputDirectory\$efiName"
