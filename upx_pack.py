"""Post-build UPX packing for the Linux envs.

The ``custom_upx`` project option (defined in ``[common]``) switches packing
on/off.  Only envs whose name starts with ``RV32I-linux-`` are packed, so the
ISA-extension-combination envs are left untouched.

The post-action is registered between ``lss.py`` and ``copy_binaries.py`` in
``[common].extra_scripts``: the disassembly is generated from the unpacked
binary, then the binary is packed in place, and finally the packed binary is
copied to ``binaries/gcc``.
"""

Import("env")

import shutil
import subprocess

_TRUTHY = ("1", "true", "yes", "on")


def _upx_enabled(env):
    value = env.GetProjectOption("custom_upx", "true")
    return str(value).strip().lower() in _TRUTHY


def upx_pack(target, source, env):
    if not _upx_enabled(env):
        return
    if not env["PIOENV"].startswith("RV32I-linux-"):
        return

    program = str(target[0])
    upx = shutil.which("upx")
    if not upx:
        print("upx_pack: 'upx' not found, leaving %s unpacked" % program)
        return

    # A post-action can run again without a relink; don't pack twice.
    if subprocess.call(
        [upx, "-t", program],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ) == 0:
        print("upx_pack: %s is already packed" % program)
        return

    try:
        subprocess.check_call([upx, "--best", "--no-progress", program])
    except subprocess.CalledProcessError as exc:
        print("upx_pack: failed to pack %s (exit %d), leaving it unpacked"
              % (program, exc.returncode))


env.AddPostAction("$BUILD_DIR/${PROGNAME}", upx_pack)
