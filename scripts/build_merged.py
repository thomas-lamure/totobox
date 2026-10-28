# SPDX-License-Identifier: GPL-3.0-or-later
# Cible PlatformIO `merged` (pio run -t merged, après pio run et pio run -t buildfs): une seule image à flasher en
# 0x0 pour une première installation sans PlatformIO (esptool, ou un outil de flash dans le navigateur).
# Elle regroupe ce que `upload` et `uploadfs` écrivent: bootloader, table de partitions, boot_app0 (offsets
# donnés par PlatformIO), firmware et image LittleFS (offset de la partition spiffs de partitions.csv).
# Les trous sont remplis de 0xFF: la NVS (réglages, cartes) est effacée, pas la partition d'usine mfg qui est
# au-delà de l'image.

import subprocess
from pathlib import Path

Import("env")  # noqa: F821 (fourni par PlatformIO)


def filesystem_offset(partitions: Path) -> str:
    for line in partitions.read_text().splitlines():
        fields = [field.strip() for field in line.split(",")]
        if len(fields) >= 4 and fields[1] == "data" and fields[2] in ("spiffs", "littlefs"):
            return fields[3]
    raise RuntimeError("partition du système de fichiers introuvable dans partitions.csv")


def build_merged(target, source, env):
    build = Path(env.subst("$BUILD_DIR"))
    project = Path(env.subst("$PROJECT_DIR"))
    images = [(offset, Path(path)) for offset, path in env["FLASH_EXTRA_IMAGES"]]
    images.append((env.subst("$ESP32_APP_OFFSET"), build / "firmware.bin"))
    images.append((filesystem_offset(project / env.subst("$PARTITIONS_TABLE_CSV")), build / "littlefs.bin"))
    missing = [str(path) for _, path in images if not path.exists()]
    if missing:
        raise RuntimeError("à construire d'abord (pio run, pio run -t buildfs): " + ", ".join(missing))

    output = build / "merged.bin"
    command = [
        env.subst("$PYTHONEXE"), env.subst("$UPLOADER"), "--chip", env.subst("$BOARD_MCU"), "merge_bin",
        "-o", str(output),
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", env.BoardConfig().get("upload.flash_size", "8MB"),
    ]
    for offset, path in images:
        command += [offset, str(path)]
    subprocess.run(command, check=True)
    print(f"merged: {output} ({output.stat().st_size} octets)")


env.AddCustomTarget(  # noqa: F821
    name="merged",
    dependencies=None,
    actions=build_merged,
    title="Image complète",
    description="Image unique à flasher en 0x0 (bootloader, partitions, firmware, page web)",
)
