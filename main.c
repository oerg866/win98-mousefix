/*
    Windows 9x mouse acceleration fix
    (C) 2026 oerg866
    CC-BY-NC-SA 4.0    
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <ctype.h>
#include <windows.h>

#include "decompress/unpacker.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t i16;
typedef int32_t i32;

static void write32(u8 *data, u32 offset, u32 value)    { memcpy (&data[offset], (u8*) &value, sizeof(value)); }

static u16  read16 (u8 *data, u32 offset)               { return *((u16*) (&data[offset])); }
static u32  read32 (u8 *data, u32 offset)               { return *((u32*) (&data[offset])); }

static long filesize(FILE *f) {
    long cur = ftell(f);
    long ret = 0;
    fseek(f, 0, SEEK_END);
    ret = ftell(f);
    fseek(f, cur, SEEK_SET);
    return ret;
}

static u8 *openAndReadWholeFile(const char *fname, long *size) {
    FILE *in = NULL;
    u8 *data = NULL;

    assert(fname != NULL);
    assert(size != NULL);

    in = fopen(fname, "rb");

    if (in == NULL) {
        goto error;
    }

    *size = filesize(in);

    if (*size <= 0)
        goto error;
    
    data = calloc(*size, 1);

    if (data == NULL)
        goto error;

    if (fread(data, 1, *size, in) != *size)
        goto error;

    fclose(in);
    return data;

error:
    perror("Error reading file");
    free(data);
    fclose(in);
    *size = -1;
    return NULL;
}

static bool openAndWriteWholeFile(const char *fname, const u8 *data, long size) {
    FILE *file = NULL;

    assert(fname != NULL);
    assert(data != NULL);
    assert(size > 0);

    file = fopen(fname, "wb");

    if (file == NULL) {
        goto error;
    }

    if (fwrite(data, 1, size, file) != size)
        goto error;

    fclose(file);
    return true;

error:
    perror("Error writing file");
    
    if (file != NULL)
        fclose(file);
    return false;
}

/* Finds a byte pattern in some data, returns NULL if not found */
u8 *findBytes(const u8 *haystack, const u8 *needle, size_t haystackSize, size_t needleSize) {
    const u8 *ceiling = haystack + haystackSize;
    
    assert(haystack != NULL);
    assert(needle != NULL); 
    assert(needleSize <= haystackSize);
    assert(needleSize != 0);
    
    while (haystack + needleSize <= ceiling) {
        if (0 == memcmp(haystack, needle, needleSize)) {
            return (u8*) haystack;
        }
        haystack++;
    }

    return NULL;
}

/*  VMOUSE.VXD implements the int 33h mouse interface for DOS VMs. 
    Games like doom don't use the cursor but take raw movements with int 33h function 0bh,
    but if we are unaccelerated that is *very* slow compared to the cursor and compared to windows itself,
    so we want to patch this function to apply a multiplier (VMOUSE_GAIN) to the actual data.

    Extracts VMOUSE.VXD from VMM32.VXD if it doesn't exist yet */
static bool patchVmouseVxd(const char *windowsDir) {
    char vmouseVxd[PATH_MAX];
    char vmm32Vxd[PATH_MAX];
    char vmm32Dir[PATH_MAX];
    char temp[PATH_MAX];
    char backup[PATH_MAX];
    bool extracted = false;
    bool success = false;
    u8 *data = NULL;
    long dataSize = 0;
    u32 le = 0;
    u32 objectTable = 0;
    u32 objectCount = 0;
    u32 pageSize = 0;
    u32 pageDataStart = 0;
    u32 pcod = 0;
    u32 pcodSize = 0;
    u32 pcodPaddedSize = 0;
    u32 pcodPageCount = 0;
    u32 pcodFirstPageIndex = 0;
    u32 pcodOffset = 0;
    u32 pcodPatchOffset = 0;
    u32 handler = 0;
    u8 *found = NULL;

#define VMOUSE_GAIN                 (2)
#define VXD_OBJ_TABLE_ENTRY_SIZE    (0x18)

    static const u8 vmouseOrig0B[] = {
        0x33, 0xC0,                                 /* xor  eax, eax */
        0x87, 0x42, 0x28,                           /* xchg eax, [edx+28h]          ; motion, vertical | horizontal << 16 */
        0x66, 0x89, 0x45, 0x14,                     /* mov  [ebp+14h], ax           ; client DX */
        0xC1, 0xE8, 0x10,                           /* shr  eax, 10h */
        0x66, 0x89, 0x45, 0x18,                     /* mov  [ebp+18h], ax           ; client CX */
        0xC3                                        /* retn */
    };

    static const u8 vmousePatch0B[] = {
        0x33, 0xC0,                                 /* xor  eax, eax */
        0x87, 0x42, 0x28,                           /* xchg eax, [edx+28h] */
        0x66, 0x6B, 0xC0, VMOUSE_GAIN,              /* imul ax, ax, VMOUSE_GAIN */
        0x66, 0x89, 0x45, 0x14,                     /* mov  [ebp+14h], ax */
        0xC1, 0xE8, 0x10,                           /* shr  eax, 10h */
        0x66, 0x6B, 0xC0, VMOUSE_GAIN,              /* imul ax, ax, VMOUSE_GAIN */
        0x66, 0x89, 0x45, 0x18,                     /* mov  [ebp+18h], ax */
        0xC3,                                       /* retn */
        'O', 'e', 'r', 'g', '8', '6', '6'           /* ASCII 'Oerg866' to detect if file is already patched */
    };

    /* Extract VMOUSE from VMM32.VXD first so we can patch it (if it doesn't exist yet )*/

    snprintf(vmouseVxd, sizeof(vmouseVxd), "%s\\SYSTEM\\VMM32\\VMOUSE.VXD", windowsDir);
    snprintf(vmm32Vxd, sizeof(vmm32Vxd), "%s\\SYSTEM\\VMM32.VXD", windowsDir);
    snprintf(vmm32Dir, sizeof(vmm32Dir), "%s\\SYSTEM\\VMM32", windowsDir);
    snprintf(temp, sizeof(temp), "%s\\SYSTEM\\VMM32MFX.TMP", windowsDir);
    snprintf(backup, sizeof(backup), "%s\\SYSTEM\\VMM32\\VMOUSE.BAK", windowsDir);

    if (GetFileAttributesA(vmouseVxd) == INVALID_FILE_ATTRIBUTES) {
        printf("Extracting VMOUSE.VXD from %s\n", vmm32Vxd);
        CreateDirectoryA(vmm32Dir, NULL);
        remove(temp);                           /* wx_unpack would read an existing one instead of VMM32.VXD */
        wx_unpack(vmm32Vxd, "VMOUSE.VXD", vmouseVxd, temp);
        remove(temp);
        extracted = GetFileAttributesA(vmouseVxd) != INVALID_FILE_ATTRIBUTES;

        if (!extracted) {
            printf("ERROR: Could not extract VMOUSE.VXD from %s\n", vmm32Vxd);
            return false;
        }
    }

    /* Open and read VMOUSE VXD file */

    printf("Patching %s\n", vmouseVxd);

    data = openAndReadWholeFile(vmouseVxd, &dataSize);

    if (data == NULL)
        goto cleanup;

    /* File is read, check if it is already patched. If so, start over from the backup, if there is one */

    if (NULL != findBytes(data, (const u8 *) "Oerg866", dataSize, 7)) {
        free(data);
        data = NULL;

        if (GetFileAttributesA(backup) == INVALID_FILE_ATTRIBUTES) {
            printf("%s is already patched.\n", vmouseVxd);
            success = true;
            goto cleanup;
        }

        printf("%s is already patched, restoring the original from %s\n", vmouseVxd, backup);
        data = openAndReadWholeFile(backup, &dataSize);

        if (data == NULL)
            goto cleanup;
    }

    /* Find the int 33h function 0Bh handler. If it's there, this is a VMOUSE.VXD, so its header can be trusted */

    found = findBytes(data, vmouseOrig0B, dataSize, sizeof(vmouseOrig0B));

    if (found == NULL) {
        printf("ERROR: %s is not a supported version.\n", vmouseVxd);
        goto cleanup;
    }

    handler = found - data;

    /* Open file header, get object table */

    le = read32(data, 0x3C);
    objectTable = le + read32(data, le + 0x40);
    objectCount = read32(data, le + 0x44);

    /* Size of 1 data / code page and offset of first page */

    pageSize = read32(data, le + 0x28);
    pageDataStart = read32(data, le + 0x80);

    /* Find PCOD object */

    for (u32 i = 0; i < objectCount; i++) {
        if (0 == memcmp(&data[objectTable + i * VXD_OBJ_TABLE_ENTRY_SIZE + 0x14], "PCOD", 4))
            pcod = objectTable + i * VXD_OBJ_TABLE_ENTRY_SIZE;
    }

    /* PCOD object found, let's find it in the file */

    pcodSize = read32(data, pcod + 0);
    pcodPageCount = read32(data, pcod + 0x10);
    pcodPaddedSize = pageSize * pcodPageCount;
    pcodFirstPageIndex = read32(data, pcod + 0x0C);

    /* Find the page in the file, page index is 1-based */

    pcodOffset = pageDataStart + pageSize * (pcodFirstPageIndex - 1);
    pcodPatchOffset = pcodOffset + pcodSize;

    /* We need enough space after the end of PCOD for our patch */

    if (pcodSize + sizeof(vmousePatch0B) > pcodPaddedSize) {
        printf("ERROR: Not enough padding in PCOD to patch %s\n", vmouseVxd);
        goto cleanup;
    }

    /* Back up the file before doing anything */

    if (!openAndWriteWholeFile(backup, data, dataSize)) {
        printf("ERROR: Could not back up VMOUSE.VXD to %s\n", backup);
        goto cleanup;
    }

    printf("Backed up to %s\n", backup);

    /* Copy the patch code into the patch offset in the pcod object */

    memcpy(data + pcodPatchOffset, vmousePatch0B, sizeof(vmousePatch0B));

    /* increase size of pcod segment so we can put our patch in */

    write32(data, pcod, pcodSize + sizeof(vmousePatch0B));

    /* Replace the first instruction of the original handler with a jump to our patch code */

    data[handler] = 0xE9;                       /* jmp rel32 */
    write32(data, handler + 1, pcodPatchOffset - (handler + 5));

    if (!openAndWriteWholeFile(vmouseVxd, data, dataSize)) {
        printf("Error writing the patched data to the file!\n");
        goto cleanup;
    }

    printf("Patched %s\n", vmouseVxd);
    extracted = false;                          /* keep it */
    success = true;

cleanup:
    if (extracted)
        remove(vmouseVxd);
    free(data);
    return success;
}

/* Case insensitive string compare, paths can be in any case */
static bool strEqualNoCase(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char) *a) != toupper((unsigned char) *b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

static void stripTrailingBackslashes(char *path) {
    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/'))
        path[--len] = '\0';
}

/* Converts a path to its absolute 8.3 form, falls back to the full path */
static void getShortPath(const char *path, char *out, size_t outSize) {
    char full[PATH_MAX];
    DWORD len = GetFullPathNameA(path, sizeof(full), full, NULL);

    if (len == 0 || len >= sizeof(full))
        snprintf(full, sizeof(full), "%s", path);

    len = GetShortPathNameA(full, out, (DWORD) outSize);

    if (len == 0 || len >= outSize)
        snprintf(out, outSize, "%s", full);
}

/* Checks whether windowsDir is the Windows installation we are running on, i.e. its system files are in use */
static bool isRunningWindowsDir(const char *windowsDir) {
    char running[PATH_MAX];
    char a[PATH_MAX];
    char b[PATH_MAX];

    if (0 == GetWindowsDirectoryA(running, sizeof(running)))
        return false;

    getShortPath(running, a, sizeof(a));
    getShortPath(windowsDir, b, sizeof(b));
    stripTrailingBackslashes(a);
    stripTrailingBackslashes(b);
    return strEqualNoCase(a, b);
}

/*  ADVPACK's AdvInstallFile function installs a file through SETUPAPI.
    It copies it to a temp file next to the target and if it's in use, it uses the WININIT.INI
    mechanism to replace it during the next reboot
    
    (this is how QFEs replace critical system files btw) */

/* AdvInstallFile flags: */
#define AIF_NOVERSIONCHECK      0x00000004  /* skip version check for this file */
#define AIF_FORCE_FILE_IN_USE   0x00000008  /* always install during the next boot */
#define AIF_NOLANGUAGECHECK     0x10000000  /* skip language check for this file */

typedef HRESULT (WINAPI *AdvInstallFileFunc)(HWND hwnd, LPCSTR srcDir, LPCSTR srcFile, LPCSTR destDir, LPCSTR destFile, DWORD flags, DWORD reserved);

#define USER_PATCH_A_OFFSET         (0x08AC)
#define USER_PATCH_B_OFFSET         (0x0A93)

static const u8 userOrigA[] = {
    0x83, 0x3E, 0x6E, 0x08, 0x00,               /* cmp  word [MouseSpeed], 0 */
    0x74, 0x0E,                                 /* jz   NoAccel */
    0x8B, 0xC3,                                 /* mov  ax, bx */
    0xE8, 0xDB, 0x01,                           /* call Accelerate */
    0x8B, 0xD8,                                 /* mov  bx, ax */
    0x8B, 0xC1,                                 /* mov  ax, cx */
    0xE8, 0xD4, 0x01,                           /* call Accelerate */
    0x8B, 0xC8,                                 /* mov  cx, ax */
    /* NoAccel: */
    0x83, 0x3E, 0x70, 0x08, 0x0A,               /* cmp  word [MouseSensitivity], 10 */
    0x74, 0x34,                                 /* jz   Done */
    0x85, 0xDB, 0x74, 0x16,                     /* test bx, bx / jz SkipX */
    0xA1, 0x72, 0x08,                           /* mov  ax, [factor] */
    0xF7, 0xEB,                                 /* imul bx */
    0x03, 0x06, 0x74, 0x08,                     /* add  ax, [X remainder] */
    0x99,                                       /* cwd                      ; drops the high word */
    0x8B, 0xD8, 0xC1, 0xEB, 0x08, 0x8A, 0xFA,   /* mov  bx, ax / shr bx, 8 / mov bh, dl */
    0x32, 0xE4, 0xA3, 0x74, 0x08,               /* xor  ah, ah / mov [X remainder], ax */
    /* SkipX: */
    0x85, 0xC9, 0x74, 0x16,                     /* test cx, cx / jz Done */
    0xA1, 0x72, 0x08,                           /* mov  ax, [factor] */
    0xF7, 0xE9,                                 /* imul cx */
    0x03, 0x06, 0x76, 0x08,                     /* add  ax, [Y remainder] */
    0x99,                                       /* cwd */
    0x8B, 0xC8, 0xC1, 0xE9, 0x08, 0x8A, 0xEA,   /* mov  cx, ax / shr cx, 8 / mov ch, dl */
    0x32, 0xE4, 0xA3, 0x76, 0x08                /* xor  ah, ah / mov [Y remainder], ax */
};

static const u8 userOrigB[] = {
    /* Accelerate: */
    0x53,                                       /* push bx */
    0x8B, 0xD8,                                 /* mov  bx, ax */
    0x85, 0xC0,                                 /* test ax, ax */
    0x7D, 0x02,                                 /* jge  Positive */
    0xF7, 0xD8,                                 /* neg  ax                      ; abs(movement) */
    /* Positive: */
    0x3B, 0x06, 0x6A, 0x08,                     /* cmp  ax, [MouseThreshold1] */
    0x76, 0x11,                                 /* jbe  Done */
    0xD1, 0xE3,                                 /* shl  bx, 1                   ; double */
    0x3B, 0x06, 0x6C, 0x08,                     /* cmp  ax, [MouseThreshold2] */
    0x76, 0x09,                                 /* jbe  Done */
    0x83, 0x3E, 0x6E, 0x08, 0x02,               /* cmp  word [MouseSpeed], 2 */
    0x75, 0x02,                                 /* jnz  Done */
    0xD1, 0xE3,                                 /* shl  bx, 1                   ; double again */
    /* Done: */
    0x8B, 0xC3,                                 /* mov  ax, bx */
    0x5B,                                       /* pop  bx */
    0xC3                                        /* ret */
};

static const u8 userPatchA[] = {
    0x60,                                       /* pusha                        ; frame holds BX = dx, CX = dy */
    0x33, 0xC0,                                 /* xor  ax, ax                  ; position 0 */
    0x83, 0x3E, 0x6E, 0x08, 0x01,               /* cmp  word [MouseSpeed], 1 */
    0x72, 0x1A,                                 /* jb   Lookup                  ; 0: position 0 */
    0x8B, 0x16, 0x6C, 0x08,                     /* mov  dx, [MouseThreshold2] */
    0xB0, 0x04,                                 /* mov  al, 4 */
    0x77, 0x08,                                 /* ja   Compare                 ; >= 2: position 4..6 from threshold 2 */
    0x8B, 0x16, 0x6A, 0x08,                     /* mov  dx, [MouseThreshold1] */
    0x42, 0x42,                                 /* inc  dx / inc dx             ; threshold 1 + 2 lines up with the same cut-offs */
    0xB0, 0x01,                                 /* mov  al, 1                   ; 1: position 1..3 from threshold 1 */
    /* Compare: */
    0x83, 0xFA, 0x0B, 0x14, 0x00,               /* cmp  dx, 11 / adc al, 0 */
    0x83, 0xFA, 0x08, 0x14, 0x00,               /* cmp  dx, 8  / adc al, 0 */
    /* Lookup: */
    0xBB, 0xA7, 0x0A,                           /* mov  bx, offset GainTable */
    0x2E, 0xD7,                                 /* cs xlat                      ; gain in 1/16 */
    0xC1, 0xE0, 0x04,                           /* shl  ax, 4                   ; -> 8.8 */
    0x83, 0x3E, 0x70, 0x08, 0x0A,               /* cmp  word [MouseSensitivity], 10 */
    0x74, 0x08,                                 /* jz   HaveGain */
    0xF7, 0x26, 0x72, 0x08,                     /* mul  word [factor] */
    0x8A, 0xC4, 0x8A, 0xE2,                     /* mov  al, ah / mov ah, dl     ; (gain * factor) >> 8 */
    /* HaveGain: */
    0x97,                                       /* xchg ax, di */
    0x8B, 0xDC,                                 /* mov  bx, sp */
    0x83, 0xC3, 0x08,                           /* add  bx, 8                   ; saved BX */
    0xBE, 0x74, 0x08,                           /* mov  si, offset X remainder */
    0xE8, 0xA0, 0x01,                           /* call Scale */
    0x83, 0xC3, 0x04,                           /* add  bx, 4                   ; saved CX */
    0x46, 0x46,                                 /* inc  si / inc si             ; Y remainder */
    0xE8, 0x98, 0x01,                           /* call Scale */
    0x61                                        /* popa                         ; falls through to 0x08FC */
};

static const u8 userPatchB[] = {
    /* Scale: */
    0x36, 0x8B, 0x07,                           /* mov  ax, ss:[bx]             ; movement */
    0xF7, 0xEF,                                 /* imul di                      ; dx:ax = movement * gain */
    0x03, 0x04, 0x83, 0xD2, 0x00,               /* add  ax, [si] / adc dx, 0    ; + carried fraction (high byte is 0) */
    0x88, 0x04,                                 /* mov  [si], al                ; new fraction */
    0x8A, 0xC4, 0x8A, 0xE2,                     /* mov  al, ah / mov ah, dl     ; >> 8 */
    0x36, 0x89, 0x07,                           /* mov  ss:[bx], ax */
    0xC3,                                       /* ret */
    /* GainTable (0x0AA7):
       Gain per slider position (0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0).
       Thus, the slider should be set to the third position for 1:1 movement */
    0x08, 0x0C, 0x10, 0x14, 0x18, 0x20, 0x30,
    'O', 'e', 'r', 'g', '8', '6', '6',          /* ASCII 'Oerg866' to detect if file is already patched */
    0xCC, 0xCC
};

/* Checks if the given USER.EXE belongs to Windows ME */
static bool userExeIsWinMe(const u8 *data, long dataSize) {
    /* VS_FIXEDFILEINFO starts with this signature 0xFEEF04BD */
    const u8 versionInfoSignature[] = { 0xBD, 0x04, 0xEF, 0xFE };
    u8 *info = findBytes(data, versionInfoSignature, dataSize, sizeof(versionInfoSignature));
    u16 major = 0;
    u16 minor = 0;

    if (info == NULL || info + 12 > data + dataSize)
        return false;

    /* VS_FIXEDFILEINFO: DWORD dwSignature, DWORD dwStrucVersion, DWORD dwFileVersionMS (major << 16 | minor) */
    minor = read16(info, 8);
    major = read16(info, 10);

    return major == 4 && minor == 90;
}

/*  Patch USER.EXE so the mouse speed slider sets a linear speed, making the mouse in windows completely unaccelerated.
    The code for this is identical between all Windows 98 Gold/SE versions.
    
    Windows ME has a different setup and its own controls for mouse acceleration, so it needn't be patched.

    In the mouse control panel the slider writes the MouseThreshold1 / MouseThreshold2 / MouseSpeed registry values.
    MOUSE_EVENT uses them to double or quadruple fast movements.
    Ever yslider position except the first one is accelerated.
    
    The hidden linear MouseSensitivity setting is limited to 16 bits complete with overflow bugs...

    All of this is replaced without changing the UI on this:
    The 3 registry values are interpreted to figure out the slider position it came from (since it's not stored
    anywhere q.q)

    Based on this index, it selects a gain from a table that is applied to every movement.
    The calculation ends up a 32 bit fixed point value and carries over the fractional part
    to the next movement per axis, usin the remainder words the previous code used when handling
    the MouseSensitivity setting.

    The MouseSensitivity still works in this modification. 
    Windows 98 has no control for it, but i don't know if any softare for win2k+ modifies it
    (using SPI_SETMOUSESPEED).

    There are two regions to modify in segment 1:
    A (0x08AC-0x08FB) is the main code path
    B (0x0A93-0x0AB6) the old acceleration function

    The parameters are in the following data segment offsets
    0x086A MouseThreshold1
    0x086C MouseThreshold2
    0x086E MouseSpeed
    0x0870 MouseSensitivity
    0x0872 MouseSensitivity 8.8 factor,
    0x0874 / 0876 Mousesensitivity X / Y remainder.
    
    After applying the patch, the slider behaves as such:

    .5x   .75x  1x    1.25x 1.5x  2x    3x
    |-----|-----|-----|-----|-----|-----|

    If windowsDir belongs to the running Windows then USER.EXE is in use and cannot be overwritten
    So the patched file is written to SYSTEM\USERMFX.TMP instead.
    It then uses ADVPACK.DLL akin to how QFEs work, which installs it during the next boot 
    (it uses the WININIT.INI renaming mechanism to do that) */
static bool patchUserExe(const char *windowsDir, bool live) {
    char userExe[PATH_MAX];     /* USER.EXE path */
    char backup[PATH_MAX];      /* USER.BAK path */
    char systemDir[PATH_MAX];   /* SYSTEM directory path */
    char temp[PATH_MAX];        /* temp file path */
    u8 *data = NULL;
    long dataSize = 0;
    u8 *a = NULL;
    u8 *b = NULL;
    u32 ne = 0;
    u32 seg1 = 0;
    HMODULE advpack = NULL;
    AdvInstallFileFunc advInstallFile = NULL;
    HRESULT result = E_FAIL;

    snprintf(userExe, sizeof(userExe), "%s\\SYSTEM\\USER.EXE", windowsDir);
    snprintf(backup, sizeof(backup), "%s\\SYSTEM\\USER.BAK", windowsDir);

    data = openAndReadWholeFile(userExe, &dataSize);

    if (data == NULL)
        return false;

    /* ME already lets you disable acceleration */

    if (userExeIsWinMe(data, dataSize)) {
        printf("USER.EXE patch not applicable to Windows ME, skipping.\n");
        free(data);
        return true;
    }

    printf("Patching %s\n", userExe);

    /* Already patched? Then start over from the backup, if there is one */

    if (NULL != findBytes(data, (const u8 *) "Oerg866", dataSize, 7)) {
        free(data);

        printf("%s is already patched.\n", userExe);

        if (GetFileAttributesA(backup) == INVALID_FILE_ATTRIBUTES) {
            return true;
        }

        printf("Will try to restore original from %s\n", userExe, backup);
        data = openAndReadWholeFile(backup, &dataSize);

        if (data == NULL) {
            printf("ERROR: Failed ot read %s, giving up\n", backup);
            return false;
        }
    }

    /*  Find the original code to replace
        PROBLEM: calls in our patch are relative, but the multiplier table is at an absolute offset (0x0AA7)
        Therefore region A has to be at 0x08AC in the first segment, else we better stop before we break anything

        If both regions are found, it is USER.EXE and we can skip the NE parsing and whatnot */

    a = findBytes(data, userOrigA, dataSize, sizeof(userOrigA));
    b = findBytes(data, userOrigB, dataSize, sizeof(userOrigB));

    if (a != NULL && b != NULL) {
        ne = read32(data, 0x3C);
        seg1 = (u32) read16(data, ne + read16(data, ne + 0x22)) << read16(data, ne + 0x32);
    }

    /* need to verify not only that the patterns are found but also that they are the right distance */
    if (a == NULL || b == NULL || b - a != USER_PATCH_B_OFFSET - USER_PATCH_A_OFFSET || (a - data) != (seg1 + USER_PATCH_A_OFFSET)) {
        printf("ERROR: %s does not look like a supported USER.EXE file.\n", userExe);
        free(data);
        return false;
    }

    /* Back up the file before doing anything, this patch can't go ahead without a backup */

    if (!openAndWriteWholeFile(backup, data, dataSize)) {
        printf("ERROR: Could not back up USER.EXE to %s\n", backup);
        free(data);
        return false;
    }

    printf("Backed up to %s\n", backup);

    /* Copy the patch code over regions A and B */

    memcpy(a, userPatchA, sizeof(userPatchA));
    memcpy(b, userPatchB, sizeof(userPatchB));

    if (!live) {

        /* An installation that isn't running, nothing has the file open */

        if (!openAndWriteWholeFile(userExe, data, dataSize)) {
            printf("Error writing the patched data to the file!\n");
            free(data);
            return false;
        }

        printf("Patched %s\n", userExe);
    } else {

        /* USER.EXE is in use, let ADVPACK install the patched copy during the next boot. */

        GetSystemDirectoryA(systemDir, sizeof(systemDir));
        snprintf(temp, sizeof(temp), "%s\\USERMFX.TMP", systemDir);

        advpack = LoadLibraryA("ADVPACK.DLL");

        if (advpack != NULL)
            advInstallFile = (AdvInstallFileFunc) GetProcAddress(advpack, "AdvInstallFile");

        if (advInstallFile != NULL && openAndWriteWholeFile(temp, data, dataSize)) {
            /* same mechanism as QFEs use */
            result = advInstallFile(NULL, systemDir, "USERMFX.TMP", systemDir, "USER.EXE",
                                    AIF_NOVERSIONCHECK | AIF_FORCE_FILE_IN_USE | AIF_NOLANGUAGECHECK, 0);
            remove(temp); /* SETUPAPI makes its own temp file so we can delete this one*/
        }

        if (advpack != NULL)
            FreeLibrary(advpack);

        if (FAILED(result)) {
            printf("ERROR: Could not install the patched USER.EXE (error %08lX)\n", (unsigned long) result);
            free(data);
            return false;
        }

        printf("The patched USER.EXE will be installed during the next boot\n");
    }

    free(data);
    return true;
}

/*  Patch MSMOUSE.VXD go give DOS boxes the "Unaccelerated" profile instead of "moderate acceleration" */
static bool patchMsmouseVxd(const char *windowsDir) {
    /* Replaces the FileDescription "MSMINI Virtual Device  (Version 4.0)" to detect if file is already patched */
    const char marker[] = "MSMINI Unaccelerated by Oerg866";
    /*  End of MSMOUSE's VM_Init handler:
        rep movsb
        mov byte ptr [edx+0Ch], 1 ; <-- profile value
        clc
        retn
    */
    u8 pattern[] = { 0xF3, 0xA4, 0xC6, 0x42, 0x0C, 0x01, 0xF8, 0xC3 };
    u8 patched[] = { 0xF3, 0xA4, 0xC6, 0x42, 0x0C, 0x03, 0xF8, 0xC3 }; // 3 = unaccelerated
    char msmouseVxd[PATH_MAX];
    char backup[PATH_MAX];
    u8 *data = NULL;
    u8 *toPatch = NULL;
    char *description = NULL;
    long dataSize = 0;

    snprintf(msmouseVxd, sizeof(msmouseVxd), "%s\\SYSTEM\\MSMOUSE.VXD", windowsDir);
    snprintf(backup, sizeof(backup), "%s\\SYSTEM\\MSMOUSE.BAK", windowsDir);

    /* Open and read MSMOUSE VXD file */

    printf("Patching %s\n", msmouseVxd);

    data = openAndReadWholeFile(msmouseVxd, &dataSize);

    if (data == NULL) {
        printf("Failed to read %s\n", msmouseVxd);
        return false;
    }

    /* Check for patch marker. If the file is already patched, start over from the backup, if there is one */

    if (NULL != findBytes(data, (const u8 *) marker, dataSize, strlen(marker))) {
        free(data);

        if (GetFileAttributesA(backup) == INVALID_FILE_ATTRIBUTES) {
            printf("%s is already patched.\n", msmouseVxd);
            return true;
        }

        printf("%s is already patched, restoring the original from %s\n", msmouseVxd, backup);
        data = openAndReadWholeFile(backup, &dataSize);

        if (data == NULL)
            return false;
    }

    /* Find version information, the value comes right after the key */

    description = (char *) findBytes(data, (const u8 *) "FileDescription", dataSize, sizeof("FileDescription"));

    if (description != NULL)
        description += sizeof("FileDescription");

    /* Find the profile value to patch */

    toPatch = findBytes(data, pattern, dataSize, sizeof(pattern));

    if (toPatch == NULL || description == NULL) {
        printf("ERROR: %s is not a supported MSMOUSE version.\n", msmouseVxd);
        free(data);
        return false;
    }

    /* Back up the file before doing anything */
    if (!openAndWriteWholeFile(backup, data, dataSize)) {
        printf("ERROR: Could not back up MSMOUSE.VXD to %s\n", backup);
        free(data);
        return false;
    }

    memcpy(toPatch, patched, sizeof(pattern));

    /* Add marker so we know the file is patched */

    strcpy(description, marker);                /* shorter than the original */

    /* Write out patched file */

    if (!openAndWriteWholeFile(msmouseVxd, data, dataSize)) {
        printf("Error writing the patched data to the file!\n");
        free(data);
        return false;
    }

    printf("Patched %s\n", msmouseVxd);
    free(data);
    return true;
}

int main(int argc, char *argv[]) {
    char msmouseVxd[PATH_MAX] = { 0, };
    char windowsDir[PATH_MAX] =  { 0, };

    bool skipUser = false;
    bool live = false;
    bool anyPatched = false;

    printf("MouseFix - Windows 98 SE / ME Mouse Acceleration Patcher - V0.3\n");
    printf("(C) 2026 E. Voirin (oerg866)\n");
    printf("VMM32 Unpacker code (c) 2022 Jaroslav Hensl\n");
    printf("---------------------------------------------------------\n");
    printf("\n");

    /* Rudimentary usage :-) */

    if (argc == 2 && 0 == strcmp("/?", argv[1])) {
        printf("mousefix [windows_dir]\n");
        printf("mousefix /skipuser   <-- This skips USER.EXE patching\n");
        printf("\n");
        return -1;
    }

    if (argc == 2 && 0 == strcmp(argv[1], "/skipuser")) {
        printf("Skipping USER.EXE patching\n");
        skipUser = true;
    } else if (argc == 2) {
        strncpy(windowsDir, argv[1], PATH_MAX);
    }

    if (0 == strlen(windowsDir)) {
        if (getenv("WINDIR") != NULL) {
            strncpy(windowsDir, getenv("WINDIR"), PATH_MAX);
        }
    }

    printf("Using Windows directory: %s\n", windowsDir);

    snprintf(msmouseVxd, PATH_MAX, "%s\\SYSTEM\\MSMOUSE.VXD", windowsDir);

    if (GetFileAttributesA(msmouseVxd) == INVALID_FILE_ATTRIBUTES) {
        printf("Error: MSMOUSE not found in expected path (%s)\n", msmouseVxd);
        return -1;
    }

    live = isRunningWindowsDir(windowsDir);

    /* Do the actual patching */

    printf("\n");

    if (patchVmouseVxd(windowsDir))
        anyPatched = true;
    else
        printf("VMOUSE.VXD patching failed!\n");

    printf("\n");

    if (patchMsmouseVxd(windowsDir))
        anyPatched = true;
    else
        printf("MSMOUSE.VXD patching failed!\n");

    if (!skipUser) {
        printf("\n");

        if (patchUserExe(windowsDir, live))
            anyPatched = true;
        else
            printf("USER.EXE patching failed!\n");
    }

    if (live && anyPatched)
        printf("\nPlease restart Windows for the changes to take effect.\n");

    return 0;
}
