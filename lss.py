Import("env", "projenv")

env.AddPostAction(
	"$BUILD_DIR/${PROGNAME}.exe",
	env.VerboseAction("objdump -h -S \"$BUILD_DIR/${PROGNAME}.exe\" > \"$BUILD_DIR/${PROGNAME}.lss\"",
	"Creating $BUILD_DIR/${PROGNAME}.lss")
)

env.AddPostAction(
	"$BUILD_DIR/${PROGNAME}",
	env.VerboseAction("objdump -h -S \"$BUILD_DIR/${PROGNAME}\" > \"$BUILD_DIR/${PROGNAME}.lss\"",
	"Creating $BUILD_DIR/${PROGNAME}.lss")
)
