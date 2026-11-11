$ErrorActionPreference = 'Stop'
$projectDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $projectDirectory
& "$projectDirectory\build.ps1" -Target arm64
$llvmBin = 'C:\Program Files\LLVM\bin'
$testDirectory = Join-Path $projectDirectory 'build\arm64-tests'
New-Item -ItemType Directory -Force "$testDirectory\esp\EFI\BOOT" | Out-Null
$flags = @('--target=aarch64-pc-windows-msvc','-std=c11','-Wall','-Wextra','-Werror','-ffreestanding','-fno-stack-protector','-mno-stack-arg-probe','-mgeneral-regs-only','-I.')
& "$llvmBin\clang.exe" @flags -DYAAI_LIVE_BOOT -Dkernel_main=arm_probe_main -c boot.c -o "$testDirectory\boot_probe.obj"
if ($LASTEXITCODE -ne 0) { throw 'ARM64 test loader compilation failed.' }
$objects = @("$testDirectory\boot_probe.obj")
foreach ($source in @('memory_test','clock_test','gui_test','input_events_test','storage_test','settings_test','settings_ui_test','launcher_test','quick_settings_test','display_mode_test','files_test','volumes_test','editor_test','folders_test','apps_test','browser_test','boot_update_test','arm_boot_test')) {
  & "$llvmBin\clang.exe" @flags -c "tests\$source.c" -o "$testDirectory\$source.obj"
  if ($LASTEXITCODE -ne 0) { throw "$source compilation failed." }
  $objects += "$testDirectory\$source.obj"
}
$objects += @('kernel','console','shell','memory','runtime','setup','clock','desktop','shell_ui','quick_settings','notifications','gui','gui_assets','platform_compat','platform_hal','platform_vectors','input_events','virtio_input','storage','settings','platform_store','display_mode','files','platform_disk','file_app','file_browser','volumes','power','editor','window_manager','apps','notes_app','calculator','calculator_app','settings_app','boot_update') | ForEach-Object { "build\arm64\$_.obj" }
& "$llvmBin\lld-link.exe" /subsystem:efi_application /entry:efi_main /nodefaultlib "/out:$testDirectory\esp\EFI\BOOT\BOOTAA64.EFI" @objects
if ($LASTEXITCODE -ne 0) { throw 'ARM64 test linking failed.' }
foreach ($ram in @('128M','512M')) {
  $vars = Join-Path $testDirectory "vars-$ram.fd"
  $log = Join-Path $testDirectory "serial-$ram.log"
  Copy-Item 'C:\Program Files\qemu\share\edk2-arm-vars.fd' $vars
  $arguments = @('-machine','virt,highmem=off','-cpu','cortex-a72','-m',$ram,'-rtc','base=utc','-device','ramfb',
    '-global','virtio-mmio.force-legacy=false','-device','virtio-keyboard-device','-device','virtio-tablet-device',
    '-drive','if=pflash,format=raw,readonly=on,file=C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    '-drive',"if=pflash,format=raw,file=$vars",'-drive',"if=none,id=boot,format=raw,snapshot=on,file=fat:$testDirectory\esp",
    '-device','virtio-blk-pci,drive=boot,bootindex=1','-display','none','-serial',"file:$log",'-monitor','none','-net','none',
    '-semihosting-config','enable=on,target=native','-no-reboot') | ForEach-Object { '"' + $_ + '"' }
  $process = Start-Process -FilePath 'C:\Program Files\qemu\qemu-system-aarch64.exe' -ArgumentList $arguments -WorkingDirectory $projectDirectory -WindowStyle Hidden -PassThru
  try {
    if (-not $process.WaitForExit(45000)) { $process.Kill(); $process.WaitForExit(); throw "ARM64 tests timed out with $ram. See $log" }
    $process.Refresh()
    $output = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'ARM64 TESTS PASSED' -or $output -notmatch 'INPUT EVENT TESTS PASSED' -or $output -notmatch 'VIRTIO INPUT INITIALIZATION PASSED' -or $output -notmatch 'STORAGE TESTS PASSED' -or $output -notmatch 'DISPLAY MODE TESTS PASSED' -or $output -notmatch 'FILESYSTEM TESTS PASSED') { throw "ARM64 tests failed with $ram. See $log" }
    Write-Output "ARM64 memory, clock, GUI, input, Virtio and storage tests passed with $ram."
  } finally {
    if ($process.HasExited) { Remove-Item -LiteralPath $vars -ErrorAction SilentlyContinue }
    $process.Dispose()
  }
}
