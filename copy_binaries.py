Import("env")
import os
from shutil import copy2

def copybinary(*args, **kwargs):
    os.makedirs('binaries', exist_ok=True)
    # copy2 preserves the executable bit of the built program.
    copy2(str(kwargs['target'][0]), 'binaries/'+env['PIOENV'])

env.AddPostAction("$BUILD_DIR/${PROGNAME}", copybinary)