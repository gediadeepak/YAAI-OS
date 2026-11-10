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

## UEFI ISO and Hyper-V

Create a bootable optical disc image with no extra ISO-authoring tools:

```powershell
.\export_iso.ps1
```

The exporter builds the current x86-64 EFI executable, embeds it in a 16 MiB
FAT16 EFI System Partition, and writes an ISO9660 image with an EFI El Torito boot
catalog. Font license notices are included inside the boot image and on the disc.
Generated ISOs are ignored by Git. Existing output files are never overwritten.

UEFI DVD boot and the live desktop have been verified in QEMU. This is UEFI-only
media; it does not add a legacy BIOS loader. The default installer/live menu is
the same as on the VDI media. Choose Try without installing to preview the OS.

For a Hyper-V compatibility test, use Generation 2, disable Secure Boot, allocate
at least 512 MiB fixed startup memory, attach the ISO to a virtual DVD drive, and
put DVD first in firmware boot order. Generation 1 cannot boot this UEFI image.

**Hyper-V is not yet a supported interactive runtime.** The current kernel has
PS/2/VirtualBox input and legacy ATA storage drivers, and lacks the synthetic
keyboard, mouse and VMBus/SCSI storage drivers needed by Generation 2. It may
reach the firmware installer, but the normal desktop cannot be used reliably
and a persistent VHDX installation is not supported. ISO packaging alone does
not solve device support. Continue using VirtualBox/QEMU for the working OS.
Hyper-V runtime testing also needs host administrator/Hyper-V permissions.

Microsoft generation comparison:
https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/plan/should-i-create-a-generation-1-or-2-virtual-machine-in-hyper-v

```powershell
.\tests\iso_export_test.ps1
.\tests\verify_iso.ps1 -IsoPath <iso> -ExpectedImagePath <matching-BOOTX64.EFI>
```

The exporter checks ISO metadata, the EFI boot catalog checksum, FAT chains,
both FAT copies and the exact EFI payload hash. The regression script rejects
corrupted catalogs/payloads, non-x86-64 EFI applications and overwriting an
existing output.


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

The Settings dashboard is on `codex/settings-dashboard`; the file-manager split is on `codex/file-manager`; Settings changes are on `codex/settings-app`; window-control changes are on `codex/window-controls`; the installer/app milestones are on `codex/single-disk-app-framework`.
Use Git revert on the feature commit to undo a change while keeping history.
Git does not back up ignored VDIs or documents stored inside them.

## Desktop applications

Files, Notes, Calculator and Settings use a shared application registry, lifecycle callbacks,
window manager and filesystem service table. Apps retain their state while
minimized. Unsaved notes veto close until saved or explicitly discarded.

Open Calculator from its dock icon, desktop menu, or **F11**. It supports basic
sequential arithmetic, sign change, backspace and six fractional decimal places.
Overflow and division by zero show an error.

These are cooperative built-in apps in the kernel address space. There is no
process isolation or external executable loader yet. The same app code builds for
x86-64 PC/UEFI and the ARM64 QEMU virt preview. The installed-disk updater currently
targets x86-64; this does not enable installation on arbitrary phones.

## Files and Notes

**F4** opens Files and **F12** opens Notes. Both have independent dock icons,
window controls and minimized state. The document filesystem format is unchanged.

Files lists folders first, then files by name, with icons and byte sizes.
Breadcrumb buttons and Up navigate folders without disturbing an unsaved note.
Double-click an item, or select it and press Enter, to open it. Plain ASCII text
opens in Notes; binary files are rejected. Notes is currently single-document.

Right-click in Files for Open, Rename, Move to folder, Move to Trash, New folder
and New note. Move accepts a folder path, with / meaning Home. Trash offers
Restore and Delete permanently; permanent removal requires typing DELETE.
Nonempty folders cannot be permanently removed until their contents are handled.

An open note and its parent folders cannot be renamed, moved or trashed from
Files until the note is closed. Notes keeps its close/save/discard protection,
undo/redo, selection and internal clipboard. Ctrl+S/F6 saves; F7 twice discards.
Changing the Notes filename saves a new copy and refuses to overwrite another
existing file. Files refreshes automatically after a Notes save; Refresh rereads
changes made elsewhere.

The footer distinguishes document payload capacity, free entry slots and total
data-volume size. YAAIFS v2 still has a combined 64-file/folder limit and a 64 KiB
per-file limit; free payload capacity is a filesystem quota, not all unused disk
space. New folders and text files use the existing disk and need no extra VDI.

Keyboard in Files: arrows select, Enter opens, N creates a folder, F creates a
note, R renames (or restores in Trash), M moves, T trashes, P permanently deletes
in Trash, U refreshes. Escape cancels a dialog or context menu before closing
the window. In a filename/path dialog, Ctrl+A selects the existing value.


## Settings

Open **Settings** from the dock, desktop menu, or **F2**. **F3** opens its Devices
section. Home opens by default; sections include Display, Personalization, Sound, Time & language, About and Devices.

The Settings app uses a light, card-based dashboard with an icon sidebar and a
Home page. Home links to real display, audio, device and personalization controls;
wallpaper thumbnails apply the existing saved backgrounds. Device cards show
reported availability, not simulated connectivity.

Click Find a setting (or press /), type a keyword and click a result or press
Enter to open its category. Search is case-insensitive; Ctrl+A selects the query
for replacement, and Escape clears search. The content pane clips its rendering
and scrolls with Page Up/Down, arrow keys, Home/End, or the draggable scrollbar.
The sidebar stays fixed. Narrow windows use a single column of Home cards.



Wallpaper, Ocean/Amber accents, volume, mute and country/time-zone changes apply
immediately. Resolution choices (Auto, 1024 x 768, 1280 x 720, 1920 x 1080) apply
on the next boot. Unsupported or rejected modes fall back to the firmware's best
usable mode. UI scaling remains fixed at 100%; runtime resolution changes and
manual clock editing are not implemented.

Preferences use checksummed, redundant UEFI-variable records. Keep the VM's
firmware/NVRAM file to retain preferences when moving the VM; they are not carried
by the system VDI alone. Existing wallpaper/volume/time-zone records remain
compatible. The status line reports when changes could not be saved.

Keyboard: Tab cycles Home/sections, 1-6 selects a section, 7 opens Home, and A-G chooses a section's displayed
options in row order. Click controls normally with the pointer. Sound playback
requires a supported output device. Devices reports actual availability rather
than enabling unsupported Wi-Fi, SIM or battery hardware.


## Window controls

- Click the square title-bar button to maximize or restore a window.
- **Alt+Tab** switches apps; hold Alt and press Tab repeatedly to cycle through all
  open apps. **Alt+Shift+Tab** cycles backward. Minimized apps are included.
- **Alt+F10** maximizes/restores, **Alt+F9** minimizes, and **Alt+Down** restores.
- **Alt+Left / Alt+Right** snaps the active window to the left or right half.
- Drag a title bar to the left/right screen edge and release to snap, or to the top
  edge to maximize. Dragging an arranged window restores its normal size.
- Dock indicators mark open apps. Click an app icon to reopen it after minimizing;
  its document or Calculator result is retained. About also has a dock icon.

Capture the VM keyboard before using shortcuts; otherwise the host may handle
Alt+Tab. Snapping is skipped when half the screen cannot fit the app's minimum
size. Snapped windows reserve clock and dock space. Maximized windows fill the entire screen and hide the desktop clock and dock until restored or minimized.

## Validation

```powershell
.\tests\run_tests.ps1
.\tests\run_arm64_tests.ps1
.\tests\verify_update.ps1 -DiskPath <updated-raw-disk> -OriginalDiskPath <original-raw-disk>
```

The VM suites cover memory, input, graphics, storage, documents, app lifecycle,
Calculator, independent Files/Notes workflows, saved Settings/resolution fallback and boot-update fault injection at 128 MiB and 512 MiB. The independent
raw-disk verifier compares the complete documents partition, setup settings, GPT,
boot records and retained original executable.
