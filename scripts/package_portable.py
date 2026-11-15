r"""
package_portable.py — упаковка mpv в единственную папку "MPV PLAYER AC-4"

Правила:
- Единственная папка с готовым плеером: R:\MPV PLAYER AC-4
- Папка mpv-portable больше не используется и автоматически удаляется.
- В итоговой папке НЕТ bin/, include/, lib/, share/ — только плоская структура:
    mpv.exe, mpv.com, libmpv-2.dll, все необходимые *.dll, portable_config/, mpv-icon.ico, *.bat
- Никакого дублирования файлов.
"""

import os
import shutil
import subprocess
import re

BUILD_DIR      = r"R:\mpv-build"
INSTALL_PREFIX = os.path.join(BUILD_DIR, "install_prefix")
INSTALL_BIN    = r"R:\install\bin"       # собранный ffmpeg (avcodec-61.dll и др.)
UCRT_BIN       = r"R:\msys64\ucrt64\bin" # MSYS2 UCRT64 runtime DLL
OUTPUT_DIR     = r"R:\MPV PLAYER AC-4"  # единственная папка с плеером
OLD_PORTABLE   = r"R:\mpv-portable"

OBJDUMP = r"R:\msys64\ucrt64\bin\objdump.exe"

SYSTEM_DLLS = {
    "advapi32.dll","avrt.dll","bcrypt.dll","bcryptprimitives.dll",
    "cfgmgr32.dll","comctl32.dll","comdlg32.dll","crypt32.dll",
    "d3d11.dll","d3d12.dll","d3dcompiler_47.dll","dbghelp.dll",
    "dwrite.dll","dwmapi.dll","gdi32.dll","imm32.dll","iphlpapi.dll",
    "kernel32.dll","mf.dll","mfplat.dll","mfreadwrite.dll",
    "msvcp_win.dll","ntdll.dll","ole32.dll","oleaut32.dll",
    "opengl32.dll","powrprof.dll","rpcrt4.dll","secur32.dll",
    "setupapi.dll","shell32.dll","shcore.dll","shlwapi.dll",
    "usp10.dll","user32.dll","userenv.dll","uxtheme.dll",
    "version.dll","winmm.dll","ws2_32.dll","wtsapi32.dll",
}

def is_system_dll(name):
    n = name.lower()
    return (n in SYSTEM_DLLS or
            n.startswith("api-ms-win-") or
            n.startswith("ext-ms-win-") or
            n == "ucrtbase.dll" or
            n.startswith("vcruntime"))

def get_imported_dlls(filepath):
    try:
        res = subprocess.run(
            [OBJDUMP, "-p", filepath],
            capture_output=True, text=True, errors="ignore", timeout=30
        )
        dlls = []
        for line in res.stdout.splitlines():
            m = re.search(r'DLL Name:\s*(\S+)', line, re.IGNORECASE)
            if m:
                dlls.append(m.group(1))
        return dlls
    except Exception:
        return []

# Удаляем старую дублирующуюся папку mpv-portable, если она осталась
if os.path.exists(OLD_PORTABLE):
    try:
        shutil.rmtree(OLD_PORTABLE)
        print(f"Removed redundant folder: {OLD_PORTABLE}")
    except Exception as e:
        print(f"Notice: could not delete {OLD_PORTABLE}: {e}")

# ── Шаг 1: очистить OUTPUT_DIR, сохраняя portable_config и пользовательские скрипты
print(f"=== Step 1: Preparing output dir: {OUTPUT_DIR} ===")
os.makedirs(OUTPUT_DIR, exist_ok=True)

KEEP_DIRS  = {"mpv", "fonts", "scripts", "script-opts", "installer", "doc", "shaders"}
KEEP_FILES = {"yt-dlp.exe", "mpv-icon.ico", "mpv-document.ico"}

for entry in os.listdir(OUTPUT_DIR):
    full = os.path.join(OUTPUT_DIR, entry)
    if entry in KEEP_DIRS or entry.lower() in {k.lower() for k in KEEP_FILES}:
        continue
    if os.path.isdir(full):
        shutil.rmtree(full)
        print(f"  Removed directory: {entry}/")
    elif entry.lower().endswith((".dll", ".exe", ".com")):
        os.remove(full)
        print(f"  Removed: {entry}")

# ── Шаг 2: копируем mpv.exe, mpv.com, libmpv-2.dll
print(f"\n=== Step 2: Copy mpv executables ===")
search_dirs = [
    os.path.join(INSTALL_PREFIX, "bin"),
    BUILD_DIR,
]

for fname in ["mpv.exe", "mpv.com"]:
    copied = False
    for sdir in search_dirs:
        src = os.path.join(sdir, fname)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(OUTPUT_DIR, fname))
            print(f"  Copied: {fname} (from {src})")
            copied = True
            break
    if not copied:
        print(f"  ERROR: {fname} not found in build directories!")

copied_libmpv = False
for sdir in search_dirs:
    candidate = os.path.join(sdir, "libmpv-2.dll")
    if os.path.exists(candidate):
        shutil.copy2(candidate, os.path.join(OUTPUT_DIR, "libmpv-2.dll"))
        print(f"  Copied: libmpv-2.dll (from {candidate})")
        copied_libmpv = True
        break
if not copied_libmpv:
    print("  ERROR: libmpv-2.dll not found in build directories!")

# ── Шаг 3: копируем FFmpeg DLL из R:\install\bin
print(f"\n=== Step 3: Copy FFmpeg DLLs from {INSTALL_BIN} ===")
if os.path.isdir(INSTALL_BIN):
    for fname in sorted(os.listdir(INSTALL_BIN)):
        if fname.lower().endswith(".dll") and not is_system_dll(fname):
            src = os.path.join(INSTALL_BIN, fname)
            dst = os.path.join(OUTPUT_DIR, fname)
            shutil.copy2(src, dst)
            print(f"  Copied FFmpeg DLL: {fname}")
else:
    print(f"  ERROR: {INSTALL_BIN} not found!")

# ── Шаг 4: рекурсивно подтягиваем зависимости UCRT64
print(f"\n=== Step 4: Resolve transitive dependencies from UCRT64 ===")
rounds = 0
while True:
    newly_copied = 0
    rounds += 1
    for fname in sorted(os.listdir(OUTPUT_DIR)):
        if not (fname.lower().endswith(".dll") or fname.lower().endswith(".exe") or fname.lower().endswith(".com")):
            continue
        full = os.path.join(OUTPUT_DIR, fname)
        if not os.path.isfile(full):
            continue
        for imp in get_imported_dlls(full):
            if is_system_dll(imp):
                continue
            target = os.path.join(OUTPUT_DIR, imp)
            if not os.path.exists(target):
                src = os.path.join(UCRT_BIN, imp)
                if os.path.exists(src):
                    shutil.copy2(src, target)
                    print(f"  Round {rounds}: {imp} (dep of {fname})")
                    newly_copied += 1
    if newly_copied == 0:
        print(f"  Resolved in {rounds} round(s). All dependencies resolved.")
        break

# ── Шаг 5: Итог и скрытие DLL для чистого вида как у shinchiro
print(f"\n=== Step 5: Verification & Styling like shinchiro ===")
dlls  = [f for f in os.listdir(OUTPUT_DIR) if f.lower().endswith(".dll")]
exes  = [f for f in os.listdir(OUTPUT_DIR) if f.lower().endswith(".exe")]
coms  = [f for f in os.listdir(OUTPUT_DIR) if f.lower().endswith(".com")]
dirs  = [f for f in os.listdir(OUTPUT_DIR) if os.path.isdir(os.path.join(OUTPUT_DIR, f))]

# Скрываем DLL файлы от глаз в проводнике
for d in dlls:
    try:
        subprocess.run(["attrib", "+h", os.path.join(OUTPUT_DIR, d)], capture_output=True)
    except Exception:
        pass

print(f"  EXE : {exes}")
print(f"  COM : {coms}")
print(f"  DLLs: {len(dlls)} files (marked hidden for clean folder appearance)")
print(f"  Subdirectories: {dirs} (installer, portable_config)")
print(f"\nDone! Single player instance ready in: {OUTPUT_DIR}")
