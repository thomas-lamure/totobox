# SPDX-License-Identifier: GPL-3.0-or-later
# Écrit build_info.h (TOTOBOX_BUILD: commit compilé, "-dirty" si des fichiers suivis sont modifiés) dans le
# dossier de build, inclus par include/version.h. Le fichier n'est réécrit que s'il change: seuls les fichiers
# qui l'incluent sont recompilés.

import subprocess
from pathlib import Path

Import("env")  # noqa: F821 (fourni par PlatformIO)


def git_build() -> str:
    try:
        return subprocess.check_output(
            ["git", "describe", "--always", "--dirty", "--exclude", "*"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "inconnu"  # archive sans dépôt git


generated = Path(env.subst("$BUILD_DIR")) / "generated"  # noqa: F821
generated.mkdir(parents=True, exist_ok=True)
header = generated / "build_info.h"
content = f'#pragma once\n#define TOTOBOX_BUILD "{git_build()}"\n'
if not header.exists() or header.read_text() != content:
    header.write_text(content)
env.Append(CPPPATH=[str(generated)])  # noqa: F821
