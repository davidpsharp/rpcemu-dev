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

/* Sound emulation */
#include <assert.h>
#include <stdint.h>

#include "rpcemu.h"
#include "mem.h"
#include "iomd.h"

#include "sound.h"

uint32_t soundaddr[4];
static int samplefreq = 44100;
int soundinited, soundlatch, soundcount;
static int16_t bigsoundbuffer[8][44100 * 2]; /**< Temp store, used to buffer
                                                    data between the emulated sound
                                                    and Allegro, * 2 is stereo */
static int bigsoundpos = 0;
static int bigsoundbufferhead = 0; // sound buffer being written to
static int bigsoundbuffertail = 0; // sound buffer being read from

#define BUFFERLEN (4410>>1)

/**
 * Called on program startup to initialise the sound system
 */
void
sound_init(void)
{
	/* Call the platform code to create a thread for handing sound updates */
	sound_thread_start();

	/* As we currently only support '16-bit' sound this is the only sample rate we support */
	samplefreq = 44100;

	/* Call the platform specific code to start the audio playing */
	plt_sound_init(BUFFERLEN << 2);
}

/**
 * Called when the user turns the sound on via the GUI
 */
void
sound_restart(void)
{
	assert(config.soundenabled);

	/* Pass the call on to the platform specific code */
	plt_sound_restart();
}

/**
 * Called when the user turns the sound off via the GUI
 */
void
sound_pause(void)
{
	assert(!config.soundenabled);

	/* Pass the call on to the platform specific code */
	plt_sound_pause();
}

/**
 * Called when the VIDC registers controlling
 * sample frequency have changed
 *
 * @param newsamplefreq Sample frequency in Hz (e.g. 44100)
 */
void
sound_samplefreq_change(int newsamplefreq)
{
	/* This is the VIDC interface for changing sample rate for the
	  8-bit audio system, as we only support 16-bit at the moment, ignore */

	NOT_USED(newsamplefreq);
}

/**
 * Copy data from the emulated sound data into a temp store.
 * Also generates sound interrupts.
 *
 * Called from gentimerirq (iomd.c)
 * @thread emulator
 */
void
sound_irq_update(void)
{
        uint32_t page,start,end,temp;
        int offset = (iomd.sndstat & IOMD_DMA_STATUS_BUFFER) << 1;
        int len;
        unsigned int c;

        if (!config.soundenabled) {
                return;
        }

        // If bigsoundbufferhead is 1 less than bigsoundbuffertail, then
        // the buffer list is full.
        if (((bigsoundbufferhead + 1) & 7) == bigsoundbuffertail)
        {
                soundcount += 4000;
                // kick the sound thread to clear the list
                sound_thread_wakeup();
                return;
        }
        page  = soundaddr[offset] & 0xFFFFF000;
        start = soundaddr[offset] & 0xFF0;
        end   = (soundaddr[offset + 1] & 0xFF0) + 16;
        len   = (end - start) >> 2;
        soundlatch = (int) (((float) len / (float) samplefreq) * 2000000.0f);

        iomd.irqdma.status |= IOMD_IRQDMA_SOUND_0;
        updateirqs();

        iomd.sndstat |= (IOMD_DMA_STATUS_INTERRUPT | IOMD_DMA_STATUS_OVERRUN);
        iomd.sndstat ^= IOMD_DMA_STATUS_BUFFER; /* Swap between buffer A and B */

        for (c = start; c < end; c += 4)
        {
                temp = ram00[((c + page) & mem_rammask) >> 2];
                bigsoundbuffer[bigsoundbufferhead][bigsoundpos++] = (temp & 0xFFFF); //^0x8000;
                bigsoundbuffer[bigsoundbufferhead][bigsoundpos++] = (temp >> 16); //&0x8000;
                if (bigsoundpos >= (BUFFERLEN << 1))
                {
                        bigsoundbufferhead++;
                        bigsoundbufferhead &= 7; /* if (bigsoundbufferhead > 7) { bigsoundbufferhead = 0; } */
                        bigsoundpos = 0;
                        sound_thread_wakeup();
                }
        }
}

/**
 * Copy data from the temp store into the platform specific output sound buffer.
 *
 * Called from host platform-specific sound thread function.
 * @thread sound 
 */
void
sound_buffer_update(void)
{
	if (!config.soundenabled) {
		return;
	}

	while (bigsoundbuffertail != bigsoundbufferhead) {
		if(plt_sound_buffer_free() > (BUFFERLEN << 1)) {
			plt_sound_buffer_play((const char *) bigsoundbuffer[bigsoundbuffertail], BUFFERLEN << 2);  // write one buffer

			bigsoundbuffertail++;
			bigsoundbuffertail &= 7; /* if (bigsoundbuffertail > 7) { bigsoundbuffertail = 0; } */
		} else {
			/* Still playing previous block of data, no need to fill it up yet */
			break;
		}
	}
}

