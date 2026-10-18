# win98-mousefix

Windows 98 SE / ME Mouse Driver Patch to remove mouse acceleration in Windows **and** DOS

This is a patch that intends to remove all traces of mouse acceleration whilst keeping full compatibility and an all-around good feel for DOS applications and the Windows GUI.

Older versions of this patch were DOS-focused, the latest version also patches the UI part to allow fully linear, un-accelerated sensitivity adjustments even in the control panel.

***Note: This patch is for Windows 98 Second Edition and Windows Millenium Edition. It MAY work on Windows 98 Gold or even Windows 95, but this has not been tested.***

![](image.png)

## How to run the patch

The patch is a 32-bit windows application that runs on anything from 95 to 11.

There are 2 ways to run the patch:

* On the target system running Windows 98SE/ME, simply run it via `mousefix` or double clicking it

* You can run it by specifying a windows system directory anywhere to patch the files therein

    Example: `mousefix F:\Temp\Windows`

***Important: After installing any kind of system updates, please reboot before using this patch.***

***Please also reboot AFTER installing this patch :P***

This is because updates may replace files that are in use (e.g. USER.EXE) **only** during the next boot. The patch might then conflict with the update.

If the USER.EXE in a running system is patched, a reboot is also needed.

### Previously patched files

The patch leaves a marker in the target files. If a patched file is detected the user is prevented from modifying it again - however, previous patch versions are *not* specifically checked for.

However, if a backup (`.BAK`, see below) is still there, the patcher restores it before patching it anew.

## Pointer speed slider (USER.EXE)

✅ Windows 9x
❌ Windows ME

In Windows 9x, the pointer speed slider in the Mouse control panel doesn't really set a speed at all. It just changes the mouse acceleration thresholds. It is common knowledge that the leftmost setting is actual linear mouse movement without any acceleration.

This patch changes `USER.EXE`'s behavior so that the slider changes the absolute linear speed instead, with NO acceleration regardless of position.

The speed is then a factor multiplied with the actual mouse movement:

| Slider position | 1 (slow) | 2 | 3 | 4 | 5 | 6 | 7 (fast) |
|---|---|---|---|---|---|---|---|
| Speed | 0.5x | 0.75x | 1.0x | 1.25x | 1.5x | 2.0x | 3.0x |

IIRC the default Windows setting is 3, so you'll have 1:1 speed by default

**NOTE:** faster positions can cause pixel skips. this is a flaw inherent to the mouse (driver) design.

There is a hidden `MouseSensitivity` registry setting, which still works, even with this patch.

Hidden, as in, there is no control panel setting, but software written for Win2000 can set it (`SPI_SETMOUSESPEED`). Just for that faint possibility code has been added to handle this setting, else I probably would have ditched it...

* The original file is backed up as `SYSTEM\USER.BAK`.  
  To undo the patch, copy it back over `USER.EXE` from DOS.
* `USER.EXE` is in use while Windows is running. The patcher can detect when the patch target is the actual running copy. In this case, `ADVPACK.DLL` is used (like official patches would), which replaces `USER.EXE` during the next boot.
* Run `mousefix /skipuser` to skip this patch

Windows ME has mouse acceleration toggles already built-in, so the USER.EXE patch is not needed there...

## DOS (MSMOUSE.VXD)

✅ Windows 9x
✅ Windows ME

In DOS VMs, `MSMOUSE.VXD` has its own acceleration mechanism. Every DOS box starts with the "Moderate" acceleration profile.

The patch overrides this default with "Unaccelerated".

## DOS raw input (VMOUSE.VXD)

✅ Windows 9x
✅ Windows ME

A typical DOS game like DOOM does not use the mouse cursor, obvoulsy. Instead it asks the mouse driver for a movement delta in (usually fixed) intervals (int 33h function 0Bh).

Without acceleration this becomes *super* slow , even at the games' highest sensitivity setting.

The patch makes `VMOUSE.VXD` multiply the delta with a fixed factor (2x)

* Without updates, `VMOUSE.VXD` only exists inside `VMM32.VXD`. The patch then extracts it to `SYSTEM\VMM32\VMOUSE.VXD`, which Windows loads instead.
* The original file is backed up as `SYSTEM\VMM32\VMOUSE.BAK`. To undo the patch, copy it back over `VMOUSE.VXD`, or delete `SYSTEM\VMM32\VMOUSE.VXD` if it was extracted from `VMM32.VXD`.
* MouseFix 0.1 / 0.2 put their backups into the current directory instead. To update a file patched by one of them, copy its original to `SYSTEM\VMM32\VMOUSE.BAK` (or `SYSTEM\MSMOUSE.BAK`) first.

## Download

Check **Releases** on this page (usually somewhere to the right).

## Building

In a standard OpenWatcom 2.x environment, type `wmake` and a `mousefix.exe` should magically appear.

## Future plans

* ~~Add option to force PS/2 rate to 200Hz. The padding in PCOD is not enough to allow for this in Windows ME though :(~~ this is scrapped because SweetLow already has a really good patch for this
* Add some kind of GUI app to configure all this stuff. Especially now with AI this is piss-easy but I'm not sure how I'd like it to be implemented....


# Notes about reverse engineering & patch workings

Reverse engineered by E. Voirin (oerg866) in his pre-AI arc :)

The code comments have grown over time so they are probably of more value a this point...

## Speed values

* Default DOS mouse speed values reside at offset 
	* 0x7170 in unpacked VMOUSE.VXD from VMM32 (Windows 98 SE)
		* Default 0x32 X / 0x32 Y
	* 0x6160 in unpacked VMOUSE.VXD from VMM32 (Windows ME)
		* Default 0x32 X / 0x32 Y

## Mouse acceleration 

* Int 33h supports four acceleration profiles: Slow (0), Moderate (1), Fast (2) and Unaccelerated (3).
* In `MSMOUSE.VXD`, they are stored in the Int 33h/func 2Bh format: 4 entry counts, then 4 x 32 bytes of thresholds and 4 x 32 bytes of factors (in 1/16), at 0x388C (98SE) / 0x308C (ME).
* Every VM gets its own copy, and its profile number is set by the VM_Init handler: 3 (Unaccelerated) for the system VM, 1 (Moderate) for DOS boxes.
* The patch changes the one for DOS boxes to 3 as well.

## Motion counters

* In `VMOUSE.VXD`, every VM gets a pair of 16 bit motion counters at `[edx+28h]` - vertical (low) and horizontal (high)
* These already INCLUDE the sensitivity setting (int 33h function 1Ah).
* The INT 33h function 0Bh handler reads and clears them.
* The patch replaces its first instruction with a jump to a copy that multiplies it by a factor of 2 before storing them `imul ax, ax, 2` (`66 6B C0 02`).
* The patch goes into the zero padding after the end of the PCOD object, increasing its size by 32 bytes to do so
* Function 27h has its own pair of counters at `[edx+24h]` and is left alone.

# License

CC-BY-NC-SA 4.0

