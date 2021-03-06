/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Author: Theodor Stana <t.stana@cern.ch>
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <unistd.h>

#include "flash.h"

#define SPEC_SYSCON_OFFSET 0x20400 /* from "sdb" on the shell, current gateware */

#define MAX_DEVICES		8		/* no alloc, me lazy */

static struct spec_pci_id {
	unsigned pci_v;
	unsigned pci_d;
} spec_devices[] = {
	{ 0x10dc /* CERN   */, 0x018d /* SPEC   */ },
	{ 0x1a39 /* Gennum */, 0x0004 /* GN4124 */ },
	{ 0, },
};

struct spec_device {
	void *mapaddr;
	int mapsize;
	int busid;
};

static struct spec_device devs[MAX_DEVICES];

char *prgname;
int verbose;

extern void *BASE_SYSCON;

/*
 * Read the flash chip
 */
static int spec_read_flash(struct spec_device *spec, int addr, int len)
{
	int i;
	int startlen = len;
	int startaddr = addr;

	/* Initializations */
	uint8_t *buf = malloc(len);

	if (buf == NULL) {
		fprintf(stderr, "Memory not available for read buffer!");
		return -1;
	}

	BASE_SYSCON = spec->mapaddr + SPEC_SYSCON_OFFSET;

	flash_init();

	if (verbose) {
		fprintf(stderr, "Reading device on bus %i: "
			"offset %i (0x%X), len %i\n", spec->busid,
			addr, addr, len);
	}

	/* Read data and put it in buffer */
	while (len) {
		i = len;
		if (i > 256) i = 256;

		flash_read(addr, buf, i);

		len  -= i;
		addr += i;
		buf  += i;
	}

	/* Pull buffer pointer back to start of buffer */
	buf -= startlen;

	/* Print read values */
	if (verbose) {
		for (i = 0; i < startlen; i++) {
			fprintf(stderr, "offset %4i (0x%03x): %3i (0x%02x)\n",
			       startaddr + i, startaddr + i, buf[i], buf[i]);
		}
	} else {
		fwrite(buf, 1, startlen, stdout);
	}

	/* Free buffer and exit */
	free(buf);

	return 0;
}


/*
 * What follows is mostly generic, should be librarized in a way
 */

/* Access a PCI device, mmap and so on */
static void *spec_access_pci(char *name, int index)
{
	struct spec_device *dev = devs + index;
	char path[PATH_MAX];
	struct stat stbuf;
	int fd;

	memset(dev, 0, sizeof(*dev));
	sprintf(path, "/sys/bus/pci/devices/%s/resource0", name);
	if ((fd = open(path, O_RDWR | O_SYNC)) < 0) {
		fprintf(stderr, "%s: %s: %s\n", prgname, path,
			strerror(errno));
		return NULL;
	}
	fstat(fd, &stbuf);
	dev->mapsize = stbuf.st_size;
	dev->mapaddr = mmap(0, stbuf.st_size,
			    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (dev->mapaddr == MAP_FAILED) {
		fprintf(stderr, "%s: mmap(%s): %s\n", prgname, path,
			strerror(errno));
		return NULL;
	}
	if (sscanf(name, "%*x:%x", &dev->busid) != 1)
		return NULL;
	return dev->mapaddr;
}

/* Scan PCI space for vendor and device; return number of successes */
static int spec_scan_pci(struct spec_pci_id *id, struct spec_device *arr,
			int alen)
{
	char path[PATH_MAX];
	FILE *f;
	struct dirent **namelist;
	int i, j, n, ndevs;
	unsigned v, d;

	n = scandir("/sys/bus/pci/devices", &namelist, 0, 0);

	if (n < 0) {
		fprintf(stderr, "%s: /sys/bus/pci/devices: %s\n", prgname,
			strerror(errno));
		return -1;
	}

	for (i = ndevs = 0; i < n; i++) {
		if (namelist[i]->d_name[0] == '.')
			continue;
		/* check vendor */
		sprintf(path, "/sys/bus/pci/devices/%s/vendor",
			namelist[i]->d_name);
		f = fopen(path, "r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", prgname, path,
				strerror(errno));
			continue;
		}
		if (fscanf(f, "%i", &v) != 1)
			continue;
		fclose(f);

		/* check device */
		sprintf(path, "/sys/bus/pci/devices/%s/device",
			namelist[i]->d_name);
		f = fopen(path, "r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", prgname, path,
				strerror(errno));
			continue;
		}
		if (fscanf(f, "%i", &d) != 1)
			continue;
		fclose(f);

		for (j = 0; id[j].pci_v; j++)
			if (id[j].pci_v == v && id[j].pci_d == d)
				break;
		if (!spec_devices[j].pci_v)
			continue; /* not found in whole array */

		/* Ok, so this is ours. Celebrate, and open it */
		if (verbose)
			fprintf(stderr, "%s: found device %04x:%04x: %s\n",
				prgname, v, d, namelist[i]->d_name);

		if (ndevs == alen) {
			fprintf(stderr, "%s: array overflow, ignoring card\n",
				prgname);
			continue;
		}
		if (spec_access_pci(namelist[i]->d_name, ndevs) == NULL)
			continue;
		ndevs++;
	}
	return ndevs;
}

static int help(void)
{
	fprintf(stderr, "%s: Use: \"%s [-v] [-b <bus>] <addr> <len>\n",
		prgname, prgname);
	return 1;
}

int main(int argc, char **argv)
{
	int ndev, i, c, bus = -1;
	struct spec_device *spec = NULL;
	prgname = argv[0];

	while ((c = getopt(argc, argv, "b:v")) != -1) {
		switch(c) {
		case 'b':
			sscanf(optarg, "%i", &bus);
			break;
		case 'v':
			verbose++;
			break;
		default:
			exit(help());
		}
	}
	if (optind != argc - 2)
		exit(help());

	/* find which one to use */
	ndev = spec_scan_pci(spec_devices, devs, MAX_DEVICES);
	if (ndev < 1) {
		fprintf(stderr, "%s: no suitable PCI devices\n", prgname);
		exit(1);
	}
	if (bus == -1 && ndev == 1) {
		spec = devs;
	} else if (bus == -1) {
		fprintf(stderr, "%s: several devices found, please choose:\n",
			prgname);
		for (i = 0; i < ndev; i++) {
			fprintf(stderr, "   -b %i\n", devs[i].busid);
		}
		exit(1);
	} else {
		for (i = 0; i < ndev; i++)
			if (bus == devs[i].busid)
				break;
		if (i == ndev) {
			fprintf(stderr, "%s: no device on bus %i\n", prgname,
				bus);
			exit(1);
		}
		spec = devs + i;
	}

	i = spec_read_flash(spec, atoi(argv[optind]), atoi(argv[optind + 1]));
	return i;
}
