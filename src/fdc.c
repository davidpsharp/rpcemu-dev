/*
  RPCEmu - An Acorn system emulator

  Copyright (C) 2005-2010 Sarah Walker

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* NEC 765/Intel 82077 Floppy drive emulation, on RPC/A7000 a part of the
   SMC 37C665GT PC style Super IO chip */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "rpcemu.h"
#include "fdc.h"
#include "vidc20.h"
#include "iomd.h"
#include "ide.h"
#include "arm.h"

/* FDC commands */
enum {
	FD_CMD_SPECIFY			= 0x03,
	FD_CMD_SENSE_DRIVE_STATUS	= 0x04,
	FD_CMD_RECALIBRATE		= 0x07,
	FD_CMD_SENSE_INTERRUPT_STATUS	= 0x08,
	FD_CMD_READ_ID_FM		= 0x0a,
	FD_CMD_DUMPREG			= 0x0e,
	FD_CMD_SEEK			= 0x0f,
	FD_CMD_CONFIGURE		= 0x13,
	FD_CMD_WRITE_DATA_MFM		= 0x45,
	FD_CMD_READ_DATA_MFM		= 0x46,
	FD_CMD_READ_ID_MFM		= 0x4a,
	FD_CMD_FORMAT_TRACK_MFM		= 0x4d,
	FD_CMD_VERIFY_DATA_MFM          = 0x56,
};

static void fdcsend(uint8_t val);

static struct
{
        uint8_t dor;
        int reset;
        uint8_t status;
        int incommand;
        uint8_t command;
        uint8_t st0,st1,st2,st3;
        int commandpos;
        int track,sector,side;
        uint8_t data;
        int params,curparam;
        uint8_t parameters[10];
        uint8_t dmadat;
        int rate;
        int oldpos;
} fdc;

/* This enumeration must be kept in sync with the formats[] array */
typedef enum {
	DISC_FORMAT_ADFS_DE_800K,
	DISC_FORMAT_ADFS_F_1600K,
	DISC_FORMAT_ADFS_L_640K,
/*	DISC_FORMAT_DOS_720K,
	DISC_FORMAT_DOS_1440K, */
} DiscFormat;

typedef struct {
	const char *name;
	const char *extension;
	int sides;
	int tracks;
	int sectors;
	int sectorsize;
	int density;
} Format;

/* This array must be kept in sync with the disc_format enumeration */
static const Format formats[] = {
	{ "ADFS D/E 800KB",   "adf", 2, 80,  5, 1024, 2 },
	{ "ADFS F 1600KB",    "adf", 2, 80, 10, 1024, 0 },
	{ "ADFS L 640KB",     "adl", 2, 80, 16,  256, 2 },
/*	{ "DOS 720KB",        "img", 2, 80,  9,  512, 2 },
	{ "DOS 1440KB",       "img", 2, 80, 18,  512, 0 }, */
};

/**
 * Structure to hold information about a specific floppy drive and the disc
 * image inside it
 */
typedef struct {
	uint8_t disc[2][80][18][1024]; /**< side, Track, Sector, Bytes, large enough to hold all format variants */
	const Format *format;
	int discchanged;
} Drive;

/** Floppy controller has two disc drives attached */
static Drive drives[2];

int fdccallback = 0;
int motoron = 0;

/**
 * Calculate the size code for a given length of sector
 *
 * @param sectorsize size of one sector on disc (in bytes)
 * @return code used to represent that sectorsize
 */
static uint8_t
fdc_size_code_from_sector_size(int sectorsize)
{
	switch (sectorsize) {
	case  128: return 0;
	case  256: return 1;
	case  512: return 2;
	case 1024: return 3;
	}

	UNIMPLEMENTED("fdc", "Unsupported sectorsize %d", sectorsize);
	return 0;
}

void
fdc_reset(void)
{
	int d;

	fdccallback = 0;
	motoron = 0;

	for (d = 0; d < 2; d++) {
		// Configure a default format
		drives[d].format = &formats[DISC_FORMAT_ADFS_DE_800K];

		// Clear disc contents
		memset(&drives[d].disc, 0, sizeof(drives[d].disc));

		drives[d].discchanged = 0;
	}
}

/**
 * Load a disc image into one of the two virtual floppy
 * disc drives.
 *
 * @param fn    Filename of disc image to load (including .adf .adl extension)
 * @param drive Which drive to load image into 0 or 1
 */
void
fdc_image_load(const char *fn, int drive)
{
	FILE *f;
	int h, t, s, b;
	const char *extension;

	assert(drive == 0 || drive == 1); // Only support two drives
	assert(fn != NULL); // Must have filename

	// must be at least a.ext
	if (strlen(fn) < 5 || fn[strlen(fn) - 4] != '.') {
		error("Disc image filename must include a file extension (.adf,.adl)");
		return;
	}

	f = fopen(fn, "rb");
	if (f == NULL) {
//		error("Unable to open disc image '%s'", fn);
		return;
	}
	fseek(f, 0, SEEK_END);

	extension = fn + strlen(fn) - 4;

	if (strcasecmp(extension, ".adf") == 0) {
		if (ftell(f) > 1000000) {
			drives[drive].format = &formats[DISC_FORMAT_ADFS_F_1600K];
		} else {
			drives[drive].format = &formats[DISC_FORMAT_ADFS_DE_800K];
		}
	} else if (strcasecmp(extension, ".adl") == 0) {
		drives[drive].format = &formats[DISC_FORMAT_ADFS_L_640K];
	} else {
		error("Unknown disc image file extension '%s', must be .adf or .adl", extension);
		return;
	}

	rpclog("fdc_image_load: %s (%ld) loaded as '%s'\n", fn, ftell(f), drives[drive].format->name);

	drives[drive].discchanged = 0;
	rewind(f);

	for (t = 0; t < drives[drive].format->tracks ; t++) {
		for (h = 0; h < drives[drive].format->sides; h++) {
			for (s = 0; s < drives[drive].format->sectors; s++) {
				for (b = 0; b < drives[drive].format->sectorsize; b++) {
					drives[drive].disc[h][t][s][b] = fgetc(f);
				}
			}
		}
	}

	fclose(f);
}

/**
 * Save a disc image from one of the two virtual floppy
 * disc drives to host disc.
 *
 * @param fn    Filename of disc image to load (including .adf .adl extension)
 * @param drive Which drive to save image from 0 or 1
 */
void
fdc_image_save(const char *fn, int drive)
{
	FILE *f;
	int h, t, s, b;

	assert(drive == 0 || drive == 1); // Only support two drives
	assert(fn != NULL); // Must have filename

	if (!drives[drive].discchanged) {
		return;
	}

	// must be at least a.ext
	if (strlen(fn) < 5 || fn[strlen(fn) - 4] != '.') {
		error("Disc image filename must include a file extension (.adf,.adl)");
		return;
	}

	f = fopen(fn, "wb");
	if (f == NULL) {
//		error("Unable to open disc image '%s'", fn);
		return;
	}
	drives[drive].discchanged = 0;

	for (t = 0; t < drives[drive].format->tracks; t++) {
		for (h = 0; h < drives[drive].format->sides; h++) {
			for (s = 0; s < drives[drive].format->sectors; s++) {
				for (b = 0; b < drives[drive].format->sectorsize; b++) {
					putc(drives[drive].disc[h][t][s][b], f);
				}
			}
		}
	}

	fclose(f);
}


void
fdc_write(uint32_t addr, uint32_t val)
{
        //printf("FDC write %03X %08X %08X\n", addr, val, PC);
        switch (addr)
        {
        case 0x3f2: /* Digital Output Register (DOR) */
                if ((val&4) && !(fdc.dor&4)) /*Reset*/
                {
                        fdc.reset=1;
                        fdccallback=500;
                }
                if (!(val&4)) fdc.status=0x80;
                motoron = val & 0x30;
                break;

        case 0x3f4: /* Data Rate Select Register (DSR) */
                // printf("3f4 write %02X %07X\n", val, PC);
                break;

        case 0x3f5: /* Data (FIFO) - Command */
                if (fdc.params)
                {
                        fdc.parameters[fdc.curparam++]=val;
//                        printf("Param : %i %i %02X\n",fdc.curparam,fdc.params,val);
                        if (fdc.curparam==fdc.params)
                        {
                                fdc.status&=~0x80;
                                switch (fdc.command)
                                {
                                case FD_CMD_SPECIFY:
                                        fdccallback=100;
                                        break;

                                case FD_CMD_SENSE_DRIVE_STATUS:
                                        fdccallback=100;
                                        break;

                                case FD_CMD_RECALIBRATE:
//                                        printf("Recalibrate starting\n");
                                case FD_CMD_SEEK:
                                        fdccallback=500;
                                        fdc.status|=1;
                                        break;

                                case FD_CMD_CONFIGURE:
                                        fdccallback=100;
                                        break;

                                case FD_CMD_WRITE_DATA_MFM:
                                        fdc.commandpos=0;
                                        fdccallback=1000;
                                        fdc.st0=fdc.parameters[0]&7;
                                        fdc.st1=fdc.st2=0;
                                        fdc.track=fdc.parameters[1];
                                        fdc.side=fdc.parameters[2];
                                        fdc.sector=fdc.parameters[3];
//                                        rpclog("Write data %i %i %i\n",fdc.side,fdc.track,fdc.sector);
                                        drives[fdc.st0 & 1].discchanged = 1;
                                        break;

                                case FD_CMD_READ_DATA_MFM:
                                case FD_CMD_VERIFY_DATA_MFM:
                                        fdc.commandpos=0;
                                        fdccallback=1000;
                                        fdc.st0=fdc.parameters[0]&7;
                                        fdc.st1=fdc.st2=0;
                                        fdc.track=fdc.parameters[1];
                                        fdc.side=fdc.parameters[2];
                                        fdc.sector=fdc.parameters[3];
//                                        printf("Read data %i %i %i\n",fdc.side,fdc.track,fdc.sector);
                                        break;

                                case FD_CMD_READ_ID_MFM:
                                        fdc.commandpos=0;
                                        fdccallback=4000;
                                        fdc.st0=fdc.parameters[0]&7;
                                        fdc.st1=fdc.st2=0;
                                        if (fdc.rate != drives[fdc.st0 & 1].format->density) {
                                                fdc.command = 0xA;
                                        }
//                                        printf("Density : %i %i\n",fdc.rate, drives[fdc.st0 & 1].discdensity);
                                        break;

                                case FD_CMD_READ_ID_FM:
                                        fdc.commandpos=0;
                                        fdccallback=4000;
                                        fdc.st0=fdc.parameters[0]&7;
                                        fdc.st1=fdc.st2=0;
                                        break;

				case FD_CMD_FORMAT_TRACK_MFM:
					fdc.commandpos = 0;
					fdc.st0        = fdc.parameters[0] & 7;
					fdc.st1        = 0;
					fdc.st2        = 0;
					fdccallback    = 1000;
					break;

                                default:
                                        UNIMPLEMENTED("FDC command",
                                                      "Unknown command 0x%02x",
                                                      fdc.command);
                                }
                        }
                        return;
                }
                if (fdc.incommand)
                {
                        fatal("FDC already in command\n");
                }
                fdc.incommand=1;
                fdc.commandpos=0;
                fdc.command=val;
//                printf("Rate %i %i\n",discdensity[0],fdc.rate);
                switch (fdc.command)
                {
                case FD_CMD_SPECIFY:
                        fdc.params=2;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

                case FD_CMD_SENSE_DRIVE_STATUS:
                        fdc.params=1;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

                case FD_CMD_RECALIBRATE:
                        fdc.params=1;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

                case FD_CMD_SENSE_INTERRUPT_STATUS:
                        fdccallback=100;
                        fdc.status=0x10;
                        break;

                case FD_CMD_DUMPREG: /* Used by Linux to identify FDC type. */
                        fdc.st0=0x80;
                        fdcsend(fdc.st0);
                        fdc.incommand=0;
                        fdccallback=0;
                        fdc.status=0x80;
                        break;
//                        fdccallback=50;
//                        fdc.status=0x10;
//                        break;

                case FD_CMD_SEEK:
                        fdc.params=2;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

                case FD_CMD_CONFIGURE:
                        fdc.params=3;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

                case FD_CMD_WRITE_DATA_MFM:
                case FD_CMD_READ_DATA_MFM:
                case FD_CMD_VERIFY_DATA_MFM:
                        fdc.params=8;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

                case FD_CMD_READ_ID_FM:
                case FD_CMD_READ_ID_MFM:
                        fdc.params=1;
                        fdc.curparam=0;
                        fdc.status=0x90;
                        break;

		case FD_CMD_FORMAT_TRACK_MFM:
			fdc.params   = 5;
			fdc.curparam = 0;
			fdc.status   = 0x80;
			break;

                default:
                        UNIMPLEMENTED("FDC command 2",
                                      "Unknown command 0x%02x", fdc.command);
                        fatal("Bad FDC command %02X\n", val);
                }
                break;

        case 0x3f7: /* Configuration Control Register (CCR) */
                // printf("3f7 write %02X %07X\n", val, PC);
                fdc.rate=val&3;
                break;

        default:
                UNIMPLEMENTED("FDC write",
                              "Unknown register 0x%03x", addr);
        }
}


uint8_t
fdc_read(uint32_t addr)
{
        //printf("FDC read %03X %08X\n", addr, PC);
        switch (addr)
        {
        case 0x3f4: /* Main Status Register (MSR) */
                iomd.irqb.status &= ~IOMD_IRQB_FLOPPY;
                updateirqs();
                // printf("Status : %02X %07X\n", fdc.status, PC);
                return fdc.status;

        case 0x3f5: /* Data (FIFO) */
                fdc.status&=0x7F;
                if (!fdc.incommand) fdc.status=0x80;
                else                fdccallback=100;
                // printf("Read FDC data %02X\n",fdc.data);
                return fdc.data;

        // case 0x3f7: /* Digital Input Register (DIR) */
		// TODO Disc changed flag
                // return 0x80;

        default:
                UNIMPLEMENTED("FDC read",
                              "Unknown register 0x%03x", addr);
        }
        return 0;
}

static void fdcsend(uint8_t val)
{
//        printf("New FDC data %02X %02X %i %i\n",val,fdc.command,fdc.incommand,fdc.commandpos);
        fdc.data=val;
        fdc.status=0xD0;
        iomd.irqb.status |= IOMD_IRQB_FLOPPY;
        updateirqs();
}

static void fdcsend2(uint8_t val)
{
//        printf("NO INT - New FDC data %02X %02X %i %i\n",val,fdc.command,fdc.incommand,fdc.commandpos);
        fdc.data=val;
        fdc.status=0xD0;
}

static void fdcsenddata(uint8_t val)
{
//        printf("New FDC DMA data %02X %02X %i %i  %i %i %i\n",val,fdc.command,fdc.incommand,fdc.commandpos,fdc.side,fdc.track,fdc.sector);
        fdc.dmadat=val;
        iomd.fiq.status |= IOMD_FIQ_FLOPPY_DMA_REQUEST;
        updateirqs();
}


void
fdc_callback(void)
{
  //        int maxsector=5;
        if (fdc.reset)
        {
                iomd.irqb.status |= IOMD_IRQB_FLOPPY;
                updateirqs();
                fdc.reset=0;
                fdc.status=0x80;
                fdc.incommand=0;
                fdc.st0=0xC0;
                fdc.track=0;
                fdc.curparam=fdc.params=0;
                fdc.rate=2;
                return;
        }
        switch (fdc.command)
        {
        case FD_CMD_SPECIFY:
//                printf("Specify : %02X %02X\n",fdc.parameters[0],fdc.parameters[1]);
                fdc.incommand=0;
                fdc.status=0x80;
                fdc.params=fdc.curparam=0;
                break;

        case FD_CMD_SENSE_DRIVE_STATUS:
		fdc.st3 = (fdc.parameters[0] & 7) | 0x28; // Drive number, 'two side' and 'ready'
		if (fdc.track == 0) {
			fdc.st3 |= 0x10; // Track is zero
		}
		// TODO Mix in Head number?
		// TODO Mix in read only?

                fdc.incommand=0;
//                printf("Send ST3\n");
                fdcsend(fdc.st3);
                fdc.params=fdc.curparam=0;
                break;

        case FD_CMD_RECALIBRATE:
                fdc.track=0;
                fdc.incommand=0;
                fdc.status=0x80;
                fdc.params=fdc.curparam=0;
		// TODO set drive back to track 0
                iomd.irqb.status |= IOMD_IRQB_FLOPPY;
                updateirqs();
		fdc.st0 = 0x20; // Seek End
//                printf("Recalibrate complete\n");
                break;

        case FD_CMD_SENSE_INTERRUPT_STATUS:
                fdc.commandpos++;
                if (fdc.commandpos==1)
                {
//                        printf("Send ST0\n");
                        fdcsend(fdc.st0);
                        fdccallback=100;
                }
                else
                {
//                        printf("Send track\n");
                        fdc.incommand=0;
                        fdcsend(fdc.track);
                }
                break;

//        case FD_CMD_DUMPREG: /*Dump registers - act as invalid command*/
//                fdc.st0=0x80;
//                fdcsend(fdc.st0);
//                fdc.incommand=0;
//                break;

        case FD_CMD_SEEK:
//                printf("Seek to %i\n",fdc.parameters[1]);
                fdc.track=fdc.parameters[1];
                fdc.incommand=0;
                fdc.status=0x80;
                fdc.params=fdc.curparam=0;
                iomd.irqb.status |= IOMD_IRQB_FLOPPY;
                updateirqs();
                fdc.st0=0x20;
                break;

        case FD_CMD_CONFIGURE:
//                printf("Configure : %02X %02X %02X\n",fdc.parameters[0],fdc.parameters[1],fdc.parameters[2]);
                fdc.incommand=0;
                fdc.status=0x80;
                fdc.params=fdc.curparam=0;
                break;

        case FD_CMD_WRITE_DATA_MFM:
                if (fdc.commandpos==2048) // DMA Write
                {
                        drives[fdc.st0 & 1].disc[fdc.side][fdc.track][fdc.sector][fdc.oldpos - 1] = fdc.dmadat;
//                        rpclog("Write %i %02i %i %03X %02X\n",fdc.side,fdc.track,fdc.sector,fdc.oldpos-1,fdc.dmadat);
//                        rpclog("Operation terminated\n");
			fdc.commandpos = drives[fdc.st0 & 1].format->sectorsize + 1;
                        fdccallback=500;
                        fdc.sector++;
                }
                else if (fdc.commandpos >= (drives[fdc.st0 & 1].format->sectorsize + 1))
                {
//                        printf("Sending result\n");
//                        fdccallback=50;
                        switch (fdc.commandpos - (drives[fdc.st0 & 1].format->sectorsize + 1))
                        {
                                case 0: fdcsend(fdc.st0); break;
                                case 1: fdcsend2(fdc.st1); break;
                                case 2: fdcsend2(fdc.st2); break;
                                case 3: fdcsend2(fdc.track); break;
                                case 4: fdcsend2((fdc.parameters[0]&4)?1:0); break;
                                case 5: fdcsend2(fdc.sector); break;
                                case 6:
				fdcsend2(fdc_size_code_from_sector_size(drives[fdc.st0 & 1].format->sectorsize));
                                fdc.incommand=0;
                                fdc.params=fdc.curparam=0;
                                fdccallback=0;
                                break;
                        }
                        fdc.commandpos++;
                }
                else
                {
                        if (fdc.commandpos)
                        {
                                drives[fdc.st0 & 1].disc[fdc.side][fdc.track][fdc.sector][fdc.commandpos - 1] = fdc.dmadat;
//                                rpclog("Write %i %02i %i %03X %02X\n",fdc.side,fdc.track,fdc.sector,fdc.commandpos-1,fdc.dmadat);
                        }
                        fdc.commandpos++;
                        if (fdc.commandpos == drives[fdc.st0 & 1].format->sectorsize + 1)
                        {
//                                rpclog("End of sector\n");
                                fdc.sector++;
                                if (fdc.sector<=fdc.parameters[5])
                                {
//                                        printf("Continuing to next sector\n");
                                        fdc.commandpos=0;
                                        fdccallback=100;
                                        return;
                                }
                                else
                                {
                                        fdccallback=100;
                                        return;
                                }
                        }
                        else
                        {
//                                printf("FIQ\n");
                                iomd.fiq.status |= IOMD_FIQ_FLOPPY_DMA_REQUEST;
                                updateirqs();
                        }
                        fdccallback=0;
                }
                break;

        case FD_CMD_READ_DATA_MFM:
//                printf("Read data callback %i\n",fdc.commandpos);
                if (fdc.commandpos >= drives[fdc.st0 & 1].format->sectorsize)
                {
//                        printf("sending result %i\n",fdc.commandpos-1024);
//                        fdccallback=20;
                        switch (fdc.commandpos - drives[fdc.st0 & 1].format->sectorsize)
                        {
                                case 0: fdcsend(fdc.st0); break;
                                case 1: fdcsend2(fdc.st1); break;
                                case 2: fdcsend2(fdc.st2); break;
                                case 3: fdcsend2(fdc.track); break;
                                case 4: fdcsend2((fdc.parameters[0] & 4) ? 1 : 0); break;
                                case 5: fdcsend2(fdc.sector); break;
                                case 6:
				fdcsend2(fdc_size_code_from_sector_size(drives[fdc.st0 & 1].format->sectorsize));
                                fdc.incommand=0;
                                fdc.params=fdc.curparam=0;
                                fdccallback=0;
                                break;
                        }
                        fdc.commandpos++;
                }
                else
                {
//                        printf("sending data\n");
                        fdcsenddata(drives[fdc.st0 & 1].disc[fdc.side][fdc.track][fdc.sector][fdc.commandpos]);
                        fdc.commandpos++;
                        if (fdc.commandpos == drives[fdc.st0 & 1].format->sectorsize)
                        {
//                                printf("Finished sector %i - target %i\n",fdc.sector,fdc.parameters[5]);
                                fdc.sector++;
                                if (fdc.sector <= fdc.parameters[5]) {
                                   fdc.commandpos=0;
				}
/*                                else
                                {
//                                        printf("End of read op\n");
                                        fdc.sector=1;
                                }*/
                        }
                        fdccallback=0;
                }
                break;

        case FD_CMD_READ_ID_MFM:
                if (fdc.sector >= drives[fdc.st0 & 1].format->sectors) {
                        fdc.sector = 0;
                }
                switch (fdc.commandpos)
                {
                        case 0: fdcsend(fdc.st0); break;
                        case 1: fdcsend2(fdc.st1); break;
                        case 2: fdcsend2(fdc.st2); break;
                        case 3: fdcsend2(fdc.track); break;
                        case 4: fdcsend2((fdc.parameters[0]&4)?1:0); break;
                        case 5: fdcsend2(fdc.sector); break;
                        case 6: fdcsend2(fdc_size_code_from_sector_size(drives[fdc.st0 & 1].format->sectorsize)); break;
                        default:
                        printf("Bad ReadID command pos %i\n",fdc.commandpos);
                        exit(-1);
                }
                fdc.commandpos++;
                if (fdc.commandpos==7)
                {
//                        printf("Sector %i : maxsector %i density %i\n",fdc.sector,discsectors[0],discdensity[0]);
                        fdc.incommand=0;
//                        printf("Read ID for track %i sector %i\n",fdc.track,fdc.sector);
                        fdc.sector++;
                        if (fdc.sector >= drives[fdc.st0 & 1].format->sectors) {
                                fdc.sector = 0;
                        }
                        fdc.params=fdc.curparam=0;
                }
//                else
//                   fdccallback=50;
                break;

        case FD_CMD_READ_ID_FM:
                iomd.irqb.status |= IOMD_IRQB_FLOPPY;
                updateirqs();
                fdc.st0=0x40|(fdc.parameters[0]&7);
                fdc.st1=1;
                fdc.st2=1;
                fdc.incommand=0;
                fdc.params=fdc.curparam=0;
                break;

	case FD_CMD_FORMAT_TRACK_MFM:
		switch (fdc.commandpos) {
		case 0: fdcsend(fdc.st0);  break;
		case 1: fdcsend2(fdc.st1); break;
		case 2: fdcsend2(fdc.st2); break;
		case 3: fdcsend2(0); break;
		case 4: fdcsend2(0); break;
		case 5: fdcsend2(0); break;
		case 6:
			fdcsend2(0);
			fdc.incommand = 0;
			fdc.params    = 0;
			fdc.curparam  = 0;
			break;
		default:
			fatal("Bad FormatTrack command pos %i", fdc.commandpos);
		}
		fdc.commandpos++;
		break;

	case FD_CMD_VERIFY_DATA_MFM:
		/* Verified OK (amazing!), jump straight to the end */
		if (fdc.parameters[0] & 0x80 /* EC */) {
			fdc.sector = fdc.parameters[7];
		} else {
			fdc.sector = fdc.parameters[5];
		}
		switch (fdc.commandpos) {
		case 0: fdcsend(fdc.st0); break;
		case 1: fdcsend2(fdc.st1); break;
		case 2: fdcsend2(fdc.st2); break;
		case 3: fdcsend2(fdc.track); break;
		case 4: fdcsend2((fdc.parameters[0] & 4) ? 1 : 0); break;
		case 5: fdcsend2(fdc.sector); break;
		case 6:
			fdcsend2(fdc_size_code_from_sector_size(drives[fdc.st0 & 1].format->sectorsize));
			fdc.incommand = 0;
			fdc.params    = 0;
			fdc.curparam  = 0;
			break;
		default:
			fatal("Bad VerifyData command pos %i", fdc.commandpos);
		}
		fdc.commandpos++;
		break;
        }
}

uint8_t
fdc_dma_read(uint32_t addr)
{
//        printf("Read FDC DMA %08X\n",addr);
        iomd.fiq.status &= ~IOMD_FIQ_FLOPPY_DMA_REQUEST;
        updateirqs();
        fdccallback=100;
        if (!fdc.commandpos) fdccallback=2000;
        if (addr==0x302A000)
        {
//                if (!fdc.commandpos)
//                   fdc.sector--;
                fdc.commandpos = drives[fdc.st0 & 1].format->sectorsize;
//                printf("End of DMA\n");
                fdccallback=2000;
                fdc.st0=0;
        }
        return fdc.dmadat;
}

void
fdc_dma_write(uint32_t addr, uint8_t val)
{
        iomd.fiq.status &= ~IOMD_FIQ_FLOPPY_DMA_REQUEST;
        updateirqs();
        fdccallback=200;
        if (!fdc.commandpos) fdccallback=2000;
        if (addr==0x302A000)
        {
//                printf("DMA terminated\n");
                fdc.oldpos=fdc.commandpos;
                fdc.commandpos=2048;
                fdccallback=2000;
                fdc.st0=0;
        }
        fdc.dmadat=val;
//        printf("Write DMA dat %02X %08X\n",val,addr);
}
