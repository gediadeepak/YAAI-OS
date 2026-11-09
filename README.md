# YAAI OS

Yet Another AI Operating System: a custom C operating system with a graphical desktop.
Development workspace: `G:\Project\YAAI-OS`.

## Run on a PC virtual machine

Install LLVM/LLD, QEMU, and optionally VirtualBox. In PowerShell:

```powershell
cd 'G:\Project\YAAI-OS\Code'
.\launch_qemu.ps1
```

The first run creates a blank system disk and starts setup. Install, finish setup, then
shut down. Later runs boot the installed system disk directly, without installer media.
If you cancelled setup, or want to update an existing installation, use:

```powershell
.\launch_qemu.ps1 -Installer
```

On an existing installation, the installer offers Update, Start installed system,
Roll back, and Try this build. Updating replaces boot files and preserves setup and
the documents partition. Avoid powering off while the updater writes boot files.

## VirtualBox

```powershell
.\export_virtualbox.ps1
```

Attach the exported installer VDI first and the blank system VDI second as IDE hard
disks. Enable UEFI and disable Secure Boot. Complete setup, shut down, detach the
installer, and keep the system VDI. The installed OS, setup settings and documents
all live on that one system VDI. The installer is removable installation/update media.

For future updates, export only new installer media:

```powershell
.\export_virtualbox.ps1 -InstallerOnly
```

Back up the powered-off system VDI, attach the new installer alongside it, select
Update installed YAAI OS, then shut down and detach the installer again. Do not
replace the existing system disk with a new blank one.

## Rollback

After an update, press **R** during the two-second YAAI boot prompt to boot the
previous verified version. You can also attach installer media and choose Roll back.
Rollback changes the OS boot selection; it does not undo your document edits.

The updater writes and reads back an inactive EFI image before publishing a
checksummed boot record. Two boot records and two image slots retain the current
and previous version. The original pre-update executable is saved as RECOVER.EFI.
The stable BOOTX64.EFI dispatcher is not rewritten on ordinary subsequent updates.

The first migration from an older installation also has to replace BOOTX64.EFI.
A power loss during that one-time replacement may require booting installer media
again; retain the powered-off VDI backup. This is a prototype recovery mechanism,
not a guarantee against every firmware, filesystem or physical-storage failure.

For code rollback, changes are on `codex/single-disk-app-framework`.
Use Git revert on the feature commit to undo a change while keeping history.
Git does not back up ignored VDIs or documents stored inside them.

## Desktop applications

Files & Notes and Calculator use a shared application registry, lifecycle callbacks,
window manager and filesystem service table. Apps retain their state while
minimized. Unsaved notes veto close until saved or explicitly discarded.

Open Calculator from its dock icon, desktop menu, or **F11**. It supports basic
sequential arithmetic, sign change, backspace and six fractional decimal places.
Overflow and division by zero show an error.

These are cooperative built-in apps in the kernel address space. There is no
process isolation or external executable loader yet. The same app code builds for
x86-64 PC/UEFI and the ARM64 QEMU virt preview. The installed-disk updater currently
targets x86-64; this does not enable installation on arbitrary phones.

## Validation

```powershell
.\tests\run_tests.ps1
.\tests\run_arm64_tests.ps1
.\tests\verify_update.ps1 -DiskPath <updated-raw-disk> -OriginalDiskPath <original-raw-disk>
```

The VM suites cover memory, input, graphics, storage, documents, app lifecycle,
Calculator and boot-update fault injection at 128 MiB and 512 MiB. The independent
raw-disk verifier compares the complete documents partition, setup settings, GPT,
boot records and retained original executable.
