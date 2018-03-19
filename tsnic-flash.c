/*
 * Copyright (c) 2018, Kontron Europe GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <pciaccess.h>
#include <unistd.h>
#include <asm/byteorder.h>

#define PCI_BAR 5
#define CSR_OFFSET 0x02000000
#define MEM_OFFSET 0x00000000

enum {
	WR_ENABLE                = CSR_OFFSET + 0x0000,
	WR_DISABLE               = CSR_OFFSET + 0x0004,
	WR_STATUS                = CSR_OFFSET + 0x0008,
	RD_STATUS                = CSR_OFFSET + 0x000c,
	SECTOR_ERASE             = CSR_OFFSET + 0x0010,
	SUBSECTOR_ERASE          = CSR_OFFSET + 0x0014,
	CONTROL                  = CSR_OFFSET + 0x0020,
	WR_NON_VOLATILE_CONF_REG = CSR_OFFSET + 0x0034,
	RD_NON_VOLATILE_CONF_REG = CSR_OFFSET + 0x0038,
	RD_FLAG_STATUS_REG       = CSR_OFFSET + 0x003c,
	CLR_FLAG_STATUS_REG      = CSR_OFFSET + 0x0040,
	BULK_ERASE               = CSR_OFFSET + 0x0044,
	DIE_ERASE                = CSR_OFFSET + 0x0048,
	FOURBYTES_ADDR_EN        = CSR_OFFSET + 0x004c,
	FOURBYTES_ADDR_EX        = CSR_OFFSET + 0x0050,
	SECTOR_PROTECT           = CSR_OFFSET + 0x0054,
	RD_MEMORY_CAPACITY_ID    = CSR_OFFSET + 0x0058,
};

struct flash_info {
	uint32_t id;
	size_t size;
	const char *name;
};

static bool quiet = false;
static bool batch_mode = false;
static size_t flash_offset = 0;
static volatile void *virt_addr;
static struct flash_info *flash_info = NULL;

#define MB (1024*1024)
static struct flash_info flash_infos[256] = {
	{ .id = 0x15, .size =  2 * MB, .name = "EPCQ16" },
	{ .id = 0x16, .size =  4 * MB, .name = "EPCQ32" },
	{ .id = 0x17, .size =  8 * MB, .name = "EPCQ64" },
	{ .id = 0x18, .size = 16 * MB, .name = "EPCQ128" },
	{ .id = 0x19, .size = 32 * MB, .name = "EPCQ256" },
	{ .id = 0x20, .size = 64 * MB, .name = "EPCQ512/A" },
	{ 0, 0, NULL }
};

static void __attribute__((noreturn)) error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static inline uint32_t pci_read(int offset)
{
	return *(uint32_t*)(virt_addr + offset);
}

static inline void pci_write(int offset, uint32_t value)
{
	*(uint32_t*)(virt_addr + offset) = value;
}

static inline uint64_t pci_read64(int offset)
{
	return *(uint64_t*)(virt_addr + offset);
}

static inline void pci_write64(int offset, uint64_t value)
{
	*(uint64_t*)(virt_addr + offset) = value;
}

static inline uint8_t mirror_byte(uint8_t val)
{
	static const uint8_t lut[256] = {
		0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
		0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
		0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
		0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
		0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
		0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
		0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
		0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
		0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
		0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
		0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
		0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
		0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
		0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
		0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
		0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
		0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
		0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
		0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
		0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
		0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
		0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
		0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
		0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
		0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
		0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
		0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
		0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
		0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
		0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
		0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
		0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
	};

	return lut[val];
}

static int spi_write_enable(void)
{
	pci_write(WR_ENABLE, 1);
	return 0;
}

static int spi_write_disable(void)
{
	pci_write(WR_DISABLE, 1);
	return 0;
}

static int spi_erase_sector(int sector)
{
	spi_write_enable();
	usleep(100);
	pci_write(SECTOR_ERASE, sector * 64 * 1024);
	usleep(100);

	while (pci_read(RD_STATUS) & 1) {
		usleep(100000);
	}

	return 0;
}

static int spi_read_buf(void *buf, int len, int offset)
{
	int i;

	for (i = 0; i < len; i += 8) {
		*((uint64_t *)(buf + i)) = __le64_to_cpu(pci_read64(MEM_OFFSET + offset + i));
	}

	return 0;
}

static int spi_write_buf(void *buf, int len, int offset)
{
	int i;

	for (i = 0; i < len; i += 8) {
		pci_write64(MEM_OFFSET + offset + i, __cpu_to_le64p(buf + i));
	}

	return 0;
}

static struct flash_info *spi_flash_detect()
{
	struct flash_info *info = flash_infos;
	uint32_t id = pci_read(RD_MEMORY_CAPACITY_ID);

	while (info->id && info->id != id && info++);
	return (info->id) ? info : NULL;
}

static int spi_flash(char *flashfile)
{
	FILE *fp;
	long size;
	char buf[1024];
	int sector;
	unsigned int offset, i;

	fp = fopen(flashfile, "r");
	if (!fp) {
		error("Could not open file %s.\n", flashfile);
	}

	/* get filesize */
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	printf("Filesize is %ld bytes.\n", size);
	printf("Internal offset is %zXh.\n", flash_offset);
	if (size + flash_offset > flash_info->size) {
		printf("File too big (max %zd bytes).\n", flash_info->size - flash_offset);
		fclose(fp);
		return 1;
	}

	for (sector = 0; sector * 64 * 1024 < size; sector++) {
		printf("\rErasing sector %03d", sector);
		fflush(stdout);
		spi_erase_sector(sector);
	}
	printf("\rErasing done.      \n");

	spi_write_enable();
	for (offset = flash_offset; offset < flash_offset + size; offset += sizeof(buf)) {
		memset(buf, 0xff, sizeof(buf));
		fread(buf, sizeof(buf), 1, fp);
		/* mirror bits */
		for (i = 0; i < sizeof(buf); i++)
			buf[i] = mirror_byte(buf[i]);
		printf("\rWriting at address %08Xh", offset);
		fflush(stdout);
		spi_write_buf(buf, sizeof(buf), offset);
	}
	spi_write_disable();
	printf("\rWriting done.                \n");

	fclose(fp);

	return 0;
}

static int spi_verify(char *flashfile)
{
	FILE *fp;
	long size;
	char buf[1024];
	char buf2[1024];
	unsigned int offset, i;

	fp = fopen(flashfile, "r");
	if (!fp) {
		error("Could not open file %s.\n", flashfile);
	}

	/* get filesize */
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (size + flash_offset > flash_info->size) {
		printf("File too big (max %zd bytes).\n", flash_info->size - flash_offset);
		fclose(fp);
		return 1;
	}

	for (offset = flash_offset; offset < flash_offset + size; offset += sizeof(buf)) {
		memset(buf, 0xff, sizeof(buf));
		memset(buf2, 0xff, sizeof(buf));
		fread(buf, sizeof(buf), 1, fp);
		/* mirror bits */
		for (i = 0; i < sizeof(buf); i++)
			buf[i] = mirror_byte(buf[i]);
		printf("\rVerifying address %08Xh", offset);
		spi_read_buf(buf2, sizeof(buf2), offset);
		if (memcmp(buf, buf2, sizeof(buf)) != 0) {
			printf("\rVerifying failed at %08Xh\n", offset);
			fflush(stdout);
			return 1;
		}
	}
	printf("\rVerifying done.              \n");

	fclose(fp);

	return 0;
}

static int spi_dump(char *dumpfile)
{
	FILE *fp;
	long size = flash_info->size;
	char buf[1024];
	int offset;
	unsigned int i;

	fp = fopen(dumpfile, "w");
	if (!fp) {
		error("Could not write file %s.\n", dumpfile);
	}

	for (offset = 0; offset < size; offset += sizeof(buf)) {
		spi_read_buf(buf, sizeof(buf), offset);
		/* mirror bits */
		for (i = 0; i < sizeof(buf); i++)
			buf[i] = mirror_byte(buf[i]);
		fwrite(buf, sizeof(buf), 1, fp);
	}

	fclose(fp);

	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-d <devnum>] [-q] <rpdfile>\n", prog);
}

int main(int argc, char **argv)
{
	int rc;
	int devnum = 1;
	bool dump = false;
	bool probe_only = false;
	int opt;
	struct pci_device *dev = NULL;
	struct pci_device_iterator *iter;
	struct pci_id_match match = {
		.vendor_id = 0x1059,
		.device_id = 0xa100,
		.subvendor_id = PCI_MATCH_ANY,
		.subdevice_id = PCI_MATCH_ANY,
	};
	long pg_size;

	while ((opt = getopt(argc, argv, "qd:O:DPb")) != -1) {
		switch (opt) {
		case 'q':
			quiet = true;
			break;
		case 'd':
			devnum = atoi(optarg);
			devnum = (devnum < 1) ? 1 : devnum;
			break;
		case 'b':
			batch_mode = true;
			break;
		case 'P':
			probe_only = true;
			break;
		case 'D':
			dump = true;
			break;
		case 'O':
			flash_offset = atoi(optarg);
			flash_offset &= ~0xf;
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	pci_system_init();

	iter = pci_id_match_iterator_create(&match);
	while (devnum--) {
		dev = pci_device_next(iter);
	}
	pci_iterator_destroy(iter);

	if (!dev) {
		error("PCI device not found.\n");
	}

	rc = pci_device_probe(dev);
	if (rc) {
		error("Could not probe PCI device: %s (%d).\n", strerror(rc), rc);
	}

	if (!dev->regions[PCI_BAR].base_addr) {
		error("PCI BAR%d not available.\n", PCI_BAR);
	}

	rc = pci_device_map_range(dev, dev->regions[PCI_BAR].base_addr,
			dev->regions[PCI_BAR].size, PCI_DEV_MAP_FLAG_WRITABLE, (void**)&virt_addr);
	if (rc) {
		error("Could not map PCI device: %s (%d).\n", strerror(rc), rc);
	}

	/*
	 * linux only maps whole pages, so if the PCI BAR is not page aligned, we have to add
	 * the missing offset ourselfs.
	 */
	pg_size = sysconf(_SC_PAGESIZE);
	virt_addr += dev->regions[PCI_BAR].base_addr & (pg_size - 1);



	flash_info = spi_flash_detect();
	if (flash_info) {
		printf("Found flash chip %s, size %zd kB.\n",
				flash_info->name, flash_info->size / 1024);
	} else {
		error("No supported flash chip found (%x)\n",
			pci_read(RD_MEMORY_CAPACITY_ID));
	}

	if (!batch_mode) {
		printf("\n\nWARNING! FLASHING STARTS IN 5 SECONDS.\n"
				"DO NOT TURN OFF POWER WHILE FLASHING!\n\n");
		sleep(5);
	}

	if (!probe_only) {
		if (argc - optind < 1) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}

		if (dump) {
			rc = spi_dump(argv[optind]);
			if (rc) goto out;
		} else {
			rc = spi_flash(argv[optind]);
			if (rc) goto out;
			rc = spi_verify(argv[optind]);
			if (rc) goto out;
		}
	}

out:
	pci_device_unmap_range(dev, (void*)virt_addr, dev->regions[PCI_BAR].size);
	pci_system_cleanup();

	return (rc) ? EXIT_FAILURE : EXIT_SUCCESS;
}
