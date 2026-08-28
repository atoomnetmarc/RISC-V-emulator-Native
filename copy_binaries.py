Import("env")
import os
from shutil import copy2

def copybinary(*args, **kwargs):
    os.makedirs('binaries', exist_ok=True)
    # copy2 preserves the executable bit of the built program. Copy to a
    # temporary name and rename, so readers never see a half-written binary.
    tmp = 'binaries/'+env['PIOENV']+'.tmp'
    copy2(str(kwargs['target'][0]), tmp)
    os.replace(tmp, 'binaries/'+env['PIOENV'])

env.AddPostAction("$BUILD_DIR/${PROGNAME}", copybinary)