/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <string.h>
#include <wrc.h>
#include "shell.h"
#include "storage.h"
#include "syscon.h"
#include "i2c.h"

static int cmd_init(const char *args[])
{
	if (!mi2c_devprobe(WRPC_FMC_I2C, FMC_EEPROM_ADR)) {
		mprintf("EEPROM not found..\n");
		return -1;
	}

	if (args[0] && !strcasecmp(args[0], "erase")) {
		if (storage_init_erase() < 0)
			mprintf("Could not erase init script\n");
	} else if (args[1] && !strcasecmp(args[0], "add")) {
		if (storage_init_add(args) < 0)
			mprintf("Could not add the command\n");
		else
			mprintf("OK.\n");
	} else if (args[0] && !strcasecmp(args[0], "show")) {
		storage_init_show();
	} else if (args[0] && !strcasecmp(args[0], "boot")) {
		shell_boot_script();
	}

	return 0;
}

DEFINE_WRC_COMMAND(init) = {
	.name = "init",
	.exec = cmd_init,
};
