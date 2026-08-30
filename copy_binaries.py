Import("env")
import os
from shutil import copy2

def copybinary(*args, **kwargs):
    # Same layout as the CMake driver (run-matrix.py): binaries/<compiler-tag>.
    # The native platform always uses the default gcc toolchain here.
    outdir = 'binaries/gcc'
    os.makedirs(outdir, exist_ok=True)
    # copy2 preserves the executable bit of the built program. Copy to a
    # temporary name and rename, so readers never see a half-written binary.
    tmp = outdir+'/'+env['PIOENV']+'.tmp'
    copy2(str(kwargs['target'][0]), tmp)
    os.replace(tmp, outdir+'/'+env['PIOENV'])

env.AddPostAction("$BUILD_DIR/${PROGNAME}", copybinary)