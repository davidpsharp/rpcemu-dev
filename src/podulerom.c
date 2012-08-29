#include <stdio.h>
#include <allegro.h>
#include "rpcemu.h"
#include "podules.h"
#include "podulerom.h"

#define MAXROMS 16
static char romfns[MAXROMS+1][256];

static uint8_t *podulerom = NULL;
static uint32_t poduleromsize = 0;
static uint32_t chunkbase;
static uint32_t filebase;

static const char description[] = "RPCEmu additional ROM";

static void
makechunk(uint8_t type, uint32_t filebase, uint32_t size)
{
	podulerom[chunkbase++] = type;
	podulerom[chunkbase++] = (uint8_t) size;
	podulerom[chunkbase++] = (uint8_t) (size >> 8);
	podulerom[chunkbase++] = (uint8_t) (size >> 16);

	podulerom[chunkbase++] = (uint8_t) filebase;
	podulerom[chunkbase++] = (uint8_t) (filebase >> 8);
	podulerom[chunkbase++] = (uint8_t) (filebase >> 16);
	podulerom[chunkbase++] = (uint8_t) (filebase >> 24);
}

static uint8_t readpodulerom(podule *p, int easi, uint32_t addr)
{
        if (easi && (poduleromsize>0))
        {
                addr=(addr&0x00FFFFFF)>>2;
                if (addr<poduleromsize) return podulerom[addr];
                return 0x00;
        }
        return 0xFF;
}

void
podulerom_reset(void)
{
	addpodule(NULL, NULL, NULL, NULL, NULL, readpodulerom, NULL, NULL, 0);
}

void initpodulerom(void)
{
        int finished=0;
        int file=0;
        struct al_ffblk ff;
        char olddir[512];
        char fn[512];
        int i;

        if (podulerom) free(podulerom);
        poduleromsize = 0;

	if (getcwd(olddir, sizeof(olddir)) == NULL) {
		fatal("initpodulerom: Failed to read working directory: %s",
		      strerror(errno));
	}
        append_filename(fn, rpcemu_get_datadir(), "poduleroms", sizeof(fn));
        if (chdir(fn) == 0)
        {
                finished=al_findfirst("*.*",&ff,FA_ALL&~FA_DIREC);
                while (!finished && file<MAXROMS)
                {
                        const char *ext = get_extension(ff.name);
                        /* Skip files with a .txt extension or starting with '.' */
                        if (stricmp(ext, "txt") && ff.name[0] != '.') {
                                strcpy(romfns[file++], ff.name);
                        }
                        finished = al_findnext(&ff);
                }
                al_findclose(&ff);
        }

        chunkbase = 0x10;
        filebase = chunkbase + 8 * file + 8;
        poduleromsize = filebase + ((sizeof(description)+3) &~3);
        podulerom = malloc(poduleromsize);
        if (podulerom == NULL) fatal("Out of Memory");

        memset(podulerom, 0, poduleromsize);
        podulerom[1] = 3; // Interrupt and chunk directories present, byte access

        memcpy(podulerom + filebase, description, sizeof(description));
        makechunk(0xF5, filebase, sizeof(description));
        filebase+=(sizeof(description)+3)&~3;

        for (i=0;i<file;i++)
        {
                FILE *f=fopen(romfns[i],"rb");
                int len;
                if (f==NULL) fatal("Can't open podulerom file\n");
                fseek(f,-1,SEEK_END);
                len = ftell(f) + 1;
                poduleromsize += (len+3)&~3;
                if (poduleromsize > 4096*1024) fatal("Cannot have more than 4MB of podule ROM");
                podulerom = realloc(podulerom, poduleromsize);
                if (podulerom == NULL) fatal("Out of Memory");

                fseek(f,0,SEEK_SET);
		if (fread(podulerom + filebase, 1, len, f) != len) {
			fatal("initpodulerom: Failed to read file '%s': %s",
			      romfns[i], strerror(errno));
		}
                fclose(f);
		rpclog("initpodulerom: Successfully loaded '%s' into podulerom\n",
		       romfns[i]);
                makechunk(0x81, filebase, len);
                filebase+=(len+3)&~3;
        }
	if (chdir(olddir) == -1) {
		fatal("initpodulerom: Failed to return to previous directory '%s': '%s'",
		      olddir, strerror(errno));
	}
        addpodule(NULL,NULL,NULL,NULL,NULL,readpodulerom,NULL,NULL,0);
}
