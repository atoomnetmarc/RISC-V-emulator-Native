Import("env")
import os
from shutil import copyfile

def copybinary(*args, **kwargs):
    os.makedirs('binaries', exist_ok=True)
    copyfile(str(kwargs['target'][0]), 'binaries/'+env['PIOENV'])

env.AddPostAction("$BUILD_DIR/${PROGNAME}", copybinary)