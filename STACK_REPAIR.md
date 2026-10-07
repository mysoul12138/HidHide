# StackRepair — filter driver attachment repair (layer-1 fix)

This branch adds a user-mode fix for the well known ordering problem of the HidHide filter driver attachment:

> Hiding silently does nothing for devices whose driver stack was created before the HidHide filter driver was registered
> (or that were enumerated while the filter couldn't attach). Typical symptoms: "it only works after I unplug/re-plug the
> controller", "it only takes effect when I start the game first and connect the controller afterwards".

## Root cause (documented upstream)

HidHide's filter driver can only attach to device stacks that are created *after* the filter was registered (official FAQ:
"Stale device stack after install"). Devices that were already enumerated keep running on a stack without the filter, so
neither hiding nor whitelisting has any effect for them until the stack is rebuilt (unplug/re-plug, disable/enable in
Device Manager, or a reboot). A related failure mode is an application that latched a device handle *before* the cloak
was engaged and keeps its access until the handle is released (again: replug).

## Fix

Two new commands, implemented entirely in new files (`HidHideCLI/src/StackRepair.h` / `.cpp`), plus additive wiring
in existing files:

- `--dev-check` — read-only diagnostic. For every blacklisted device that is present, evaluates the driver stack
  (`DEVPKEY_Device_Stack`) and prints `--dev-repair "<device instance path>"` for each device missing the filter
  driver attachment. Silent when everything is healthy.
- `--dev-repair ["<device instance path>"]` — rebuilds the driver stack of the selected device(s), the programmatic
  equivalent of unplug/re-plug:
  - no argument: repairs all present blacklisted devices that are missing the filter attachment;
  - explicit path: unconditionally rebuilds that device (also useful to force a running application to release a
    latched handle and re-evaluate the cloak state on its next open);
  - the device is disabled with `CM_Disable_DevNode(CM_DISABLE_ABSOLUTE | CM_DISABLE_UI_NOT_OK)` (non-persistent,
    so an interrupted repair never survives a reboot), waited on, and re-enabled with `CM_Enable_DevNode`;
  - after re-enumeration the filter attachment is re-evaluated; if the device itself doesn't regain the filter the
    USB/BTHENUM parent is rebuilt as a fallback;
  - needs no driver changes and no re-signing. The PnP operations may require an elevated shell.

## Usage examples

```
HidHideCLI --dev-check
HidHideCLI --cloak-on --dev-hide "HID\VID_054C&PID_0CE6&MI_03\7&387ff904&0&0000" --dev-repair
HidHideCLI --dev-repair "HID\VID_054C&PID_0CE6&MI_03\7&387ff904&0&0000"
```

If a device still cannot be repaired (e.g. access denied, or the class filter registration is missing entirely),
fall back to a manual unplug/re-plug or a reboot; `--dev-check` will show the remaining offenders.

## Upstream merge notes (minimize conflicts)

Only additive changes are made to existing files. Touch points:

1. `HidHideCLI/src/StackRepair.h` / `.cpp` — new files.
2. `HidHideCLI/src/CommandInterpreter.h` — two method declarations + one validator declaration.
3. `HidHideCLI/src/Commands.cpp` — `#include "StackRepair.h"`, `ValZeroOrOneDeviceInstancePath`, `DevCheck`, `DevRepair`.
4. `HidHideCLI/src/CommandInterpreter.cpp` — two command registrations (`dev-check`, `dev-repair`).
5. `HidHideCLI/resource.h` / `HidHideCLI.rc` — new string table IDs appended.
6. `HidHideCLI/HidHideCLI.vcxproj` / `.filters` — two new file entries.

To rebase on a newer upstream: merge `master`, and in the six existing files keep both sides (the additions above are
independent of upstream code paths).

## Status

Empirical validation is performed on a Windows 11 test machine (see the session logs). The module touches neither the
driver nor its signing.
