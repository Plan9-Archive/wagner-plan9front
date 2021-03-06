#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include "../boot/boot.h"

void
boot(int argc, char *argv[])
{
	char cputype[64];
	char buf[32];

	fmtinstall('r', errfmt);

	bind("#c", "/dev", MBEFORE);
	open("/dev/cons", OREAD);
	open("/dev/cons", OWRITE);
	open("/dev/cons", OWRITE);
	/*
	 * init will reinitialize its namespace.
	 * #ec gets us plan9.ini settings (*var variables).
	 */
	bind("#ec", "/env", MREPL);
	bind("#e", "/env", MBEFORE|MCREATE);
	bind("#s", "/srv", MREPL|MCREATE);

	if(Debug){
		int i;

		print("argc=%d\n", argc);
		for(i = 0; i < argc; i++)
			print("%lux %s ", (ulong)argv[i], argv[i]);
		print("\n");
	}

	readfile("#e/cputype", cputype, sizeof(cputype));
	setenv("bootdisk", bootdisk, 0);

	/* setup the boot namespace */
	bind("/boot", "/bin", MAFTER);
	run("/bin/paqfs", "-q", "-c", "8", "-m" "/root", "/boot/bootfs.paq", nil);
	bind("/root", "/", MAFTER);
	snprint(buf, sizeof(buf), "/%s/bin", cputype);
	bind(buf, "/bin", MAFTER);
	bind("/rc/bin", "/bin", MAFTER);
	exec("/bin/bootrc", argv);
}
