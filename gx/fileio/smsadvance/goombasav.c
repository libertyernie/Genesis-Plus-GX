/* goombasav.c - functions to handle Goomba / Goomba Color SRAM

Copyright (C) 2014-2017 libertyernie

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.

https://github.com/libertyernie/goombasav

When compiling in Visual Studio, set the project to compile
as C++ code (Properties -> C/C++ -> Advanced -> Compile As.)
*/

#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "goombasav.h"
#include "minilzo/minilzo.h"

#define goomba_error(...) { sprintf(last_error, __VA_ARGS__); }

#define F16 little_endian_conv_16
#define F32 little_endian_conv_32

static const char* const sleeptxt[] = { "5min", "10min", "30min", "OFF" };
static const char* const brightxt[] = { "I", "II", "III", "IIII", "IIIII" };

static char last_error[256] = "No error has occured yet.";
static char goomba_strbuf[256];

const char* goomba_last_error() {
	return (const char*)last_error;
}

size_t goomba_set_last_error(const char* msg) {
	size_t len = strlen(msg);
	if (len > sizeof(last_error)-1) {
		len = sizeof(last_error)-1;
	}
	memcpy(last_error, msg, len);
	last_error[sizeof(last_error)-1] = '\0';
	return len;
}

// For making a checksum of the compressed data.
// output_bytes is limited to 8 at maximum
uint64_t checksum_slow(const void* ptr, size_t length, int output_bytes) {
	const unsigned char* p = (const unsigned char*)ptr;
	uint64_t sum=0;
	char* sumptr = (char*)&sum;
	size_t j;
	for (j=0;j<length;j++) {
		int index = j%output_bytes;
		sumptr[index] += *p;
		p++;
	}
	return sum;
}

uint16_t little_endian_conv_16(uint16_t value) {
	if (*(uint16_t *)"\0\xff" < 0x100) {
		uint16_t buffer;
		((char*)&buffer)[0] = ((char*)&value)[1];
		((char*)&buffer)[1] = ((char*)&value)[0];
		return buffer;
	} else {
		return value;
	}
}

uint32_t little_endian_conv_32(uint32_t value) {
	if (*(uint16_t *)"\0\xff" < 0x100) {
		uint32_t buffer;
		((char*)&buffer)[0] = ((char*)&value)[3];
		((char*)&buffer)[1] = ((char*)&value)[2];
		((char*)&buffer)[2] = ((char*)&value)[1];
		((char*)&buffer)[3] = ((char*)&value)[0];
		return buffer;
	} else {
		return value;
	}
}

configdata_misc_strings configdata_get_misc(char misc) {
	configdata_misc_strings s;
	s.sleep = sleeptxt[misc & 0x3];
	s.autoload_state = ((misc & 0x10) >> 4) ? "ON" : "OFF";
	s.gamma = brightxt[(misc & 0xE0) >> 5];
	return s;
}

const char* stateheader_typestr(uint16_t type) {
	switch (type) {
	case GOOMBA_STATESAVE:
		return "Savestate";
	case GOOMBA_SRAMSAVE:
		return "SRAM";
	case GOOMBA_CONFIGSAVE:
		return "Configuration";
	case GOOMBA_PALETTE: // Used by Goomba Paletted
		return "Palette";
	default:
		return "Unknown"; // Stateheaders with these types are rejected by stateheader_plausible
	}
}

const char* stateheader_str(const stateheader* sh) {
	int j = 0;
	j += sprintf(goomba_strbuf + j, "size: %u\n", F16(sh->size));
	j += sprintf(goomba_strbuf + j, "type: %s (%u)\n", stateheader_typestr(F16(sh->type)), F16(sh->type));
	if (F16(sh->type) == GOOMBA_CONFIGSAVE) {
		configdata* cd = (configdata*)sh;
		if (cd->size == sizeof(goomba_configdata)) {
			const goomba_configdata* gcd = (const goomba_configdata*)cd;
			j += sprintf(goomba_strbuf + j, "(goomba) bordercolor: %u\n", gcd->bordercolor);
			j += sprintf(goomba_strbuf + j, "(goomba) palettebank: %u\n", gcd->palettebank);
			configdata_misc_strings strs = configdata_get_misc(gcd->misc);
			j += sprintf(goomba_strbuf + j, "(goomba) sleep: %s\n", strs.sleep);
			j += sprintf(goomba_strbuf + j, "(goomba) autoload state: %s\n", strs.autoload_state);
			j += sprintf(goomba_strbuf + j, "(goomba) gamma: %s\n", strs.gamma);
			const pocketnes_configdata* pcd = (const pocketnes_configdata*)cd;
			j += sprintf(goomba_strbuf + j, "(pocketnes) displaytype: %u\n", pcd->displaytype);
			j += sprintf(goomba_strbuf + j, "(pocketnes) misc: %u\n", pcd->misc);
			j += sprintf(goomba_strbuf + j, "rom checksum: %8X (0xE000-0xFFFF %s)\n", F32(gcd->sram_checksum),
				gcd->sram_checksum != 0 ? "occupied" : "free");
			j += sprintf(goomba_strbuf + j, "title: %s", gcd->reserved4);
		} else if (cd->size == sizeof(smsadvance_configdata)) {
			const smsadvance_configdata* scd = (const smsadvance_configdata*)cd;
			j += sprintf(goomba_strbuf + j, "displaytype: %u\n", scd->displaytype);
			j += sprintf(goomba_strbuf + j, "gammavalue: %u\n", scd->gammavalue);
			j += sprintf(goomba_strbuf + j, "region: %u\n", scd->region);
			j += sprintf(goomba_strbuf + j, "sleepflick: %u\n", scd->sleepflick);
			j += sprintf(goomba_strbuf + j, "config: %u\n", scd->config);
			j += sprintf(goomba_strbuf + j, "bcolor: %u\n", scd->bcolor);
			j += sprintf(goomba_strbuf + j, "rom checksum: %8X (0xE000-0xFFFF %s)\n", F32(scd->sram_checksum),
				scd->sram_checksum != 0 ? "occupied" : "free");
			j += sprintf(goomba_strbuf + j, "title: %s", scd->reserved3);
		}
	} else {
		j += sprintf(goomba_strbuf + j, "%scompressed_size: %u\n",
			(F32(sh->uncompressed_size) < F16(sh->size) ? "" : "un"),
			F32(sh->uncompressed_size));
		j += sprintf(goomba_strbuf + j, "framecount: %u\n", F32(sh->framecount));
		j += sprintf(goomba_strbuf + j, "rom checksum: %8X\n", F32(sh->checksum));
		j += sprintf(goomba_strbuf + j, "title: %s", sh->title);
	}
	return goomba_strbuf;
}

const char* stateheader_summary_str(const stateheader* sh) {
	sprintf(goomba_strbuf, "%s: %s (%u b / %u uncomp)", stateheader_typestr(
		F16(sh->type)), sh->title, F16(sh->size), F32(sh->uncompressed_size));
	return goomba_strbuf;
}

int stateheader_plausible(const void* ptr) {
	const stateheader* sh = (const stateheader*)ptr;
	uint16_t type = F16(sh->type);
	if (type < 0 || type == 3 || type == 4 || type > 5) return 0;
	return F16(sh->size) >= sizeof(stateheader) && // check size (at least 48)
		(F16(sh->type) == GOOMBA_CONFIGSAVE || sh->uncompressed_size != 0); // check uncompressed_size, but not for configsave
	// when checking for whether something equals 0, endian conversion is not necessary
}

const stateheader* stateheader_advance(const stateheader* sh) {
	if (!stateheader_plausible(sh)) return NULL;

	uint16_t s = F16(sh->size);
	char* c = (char*)sh;
	c += s;
	return (stateheader*)c;
}

const stateheader* stateheader_first(const void* gba_data) {
	uint32_t* check = (uint32_t*)gba_data;
	uint32_t check_le = F32(*check);
	if (check_le == GOOMBA_STATEID) check++;
	else if (check_le == POCKETNES_STATEID) check++;
	else if (check_le == POCKETNES_STATEID2) check++;
	else if (check_le == SMSADVANCE_STATEID) check++;

	if (stateheader_plausible(check)) {
		return (stateheader*)check;
	} else {
		goomba_error("sh at %p not plausible - value: %08X", gba_data, *(uint32_t*)gba_data);
		return NULL;
	}
}

const stateheader** stateheader_scan(const void* gba_data) {
	const goomba_size_t psize = sizeof(stateheader*);
	const stateheader** headers = (const stateheader**)malloc(psize * 64);
	memset(headers, 0, psize * 64);

	const stateheader* sh = stateheader_first(gba_data);
	if (sh == NULL) {
		free(headers);
		return NULL;
	}
	int i = 0;
	while (sh && stateheader_plausible(sh) && i < 63) {
		headers[i] = sh;
		i++;
		sh = stateheader_advance(sh);
	}
	return headers;
}

const stateheader* stateheader_for(const void* gba_data, const char* gbc_title) {
	char title[0x10];
	memcpy(title, gbc_title, 0x0F);
	title[0x0F] = '\0';
	const stateheader* sh = stateheader_first(gba_data);
	while (sh && stateheader_plausible(sh)) {
		if (strcmp(sh->title, title) == 0 && sh->type == GOOMBA_SRAMSAVE) {
			return sh;
		}
		sh = stateheader_advance(sh);
	}
	goomba_error(last_error, "Could not find SRAM data for %s", title);
	return NULL;
}

const stateheader* stateheader_for_checksum(const void* gba_data, uint32_t checksum) {
	const stateheader* sh = stateheader_first(gba_data);
	while (sh && stateheader_plausible(sh)) {
		if (sh->checksum == checksum && sh->type == GOOMBA_SRAMSAVE) {
			return sh;
		}
		sh = stateheader_advance(sh);
	}
	goomba_error(last_error, "Could not find SRAM data for %04X", checksum);
	return NULL;
}

// Uses checksum_slow, and looks at the compressed data (not the header).
// output_bytes is limited to 8 at maximum
uint64_t goomba_compressed_data_checksum(const stateheader* sh, int output_bytes) {
	return checksum_slow(sh+1, F16(sh->size) - sizeof(stateheader), output_bytes);
}

int goomba_is_sram(const void* data) {
	uint32_t stateid_le = F32(*(uint32_t*)data);
	return stateid_le == GOOMBA_STATEID
		|| stateid_le == POCKETNES_STATEID
		|| stateid_le == POCKETNES_STATEID2
		|| stateid_le == SMSADVANCE_STATEID;
}

/**
* Returns the 32-bit checksum (unsigned) in the configdata header, or -1 if
* an error occurred.
*/
int64_t goomba_get_configdata_checksum_field(const void* gba_data) {
	const stateheader* sh = stateheader_first(gba_data);

	while (sh && stateheader_plausible(sh)) {
		if (F16(sh->type) == GOOMBA_CONFIGSAVE) {
			// found configdata
			const configdata* cd = (configdata*)sh;
			if (F16(cd->size) == sizeof(goomba_configdata)) {
				const goomba_configdata* gcd = (const goomba_configdata*)cd;
				return F32(gcd->sram_checksum); // 0 = clean, postitive = unclean
			} else if (F16(cd->size) == sizeof(smsadvance_configdata)) {
				const smsadvance_configdata* scd = (const smsadvance_configdata*)cd;
				return F32(scd->sram_checksum); // 0 = clean, postitive = unclean
			} else {
				goomba_error("This is not a recognized type of configheader (by cd->size).");
				return -1;
			}
		}
		sh = stateheader_advance(sh);
	}

	goomba_error("Unknown error (no configdata?)");
	return -1;
}

char* goomba_cleanup(const void* gba_data_param) {
	char gba_data[GOOMBA_COLOR_SRAM_SIZE]; // on stack - do not need to free
	memcpy(gba_data, gba_data_param, GOOMBA_COLOR_SRAM_SIZE);

	const stateheader* first = stateheader_first(gba_data);
	if (first == NULL) return NULL;
	
	for (const stateheader* sh1 = first; sh1 && stateheader_plausible(sh1); sh1 = stateheader_advance(sh1)) {
		if (F16(sh1->type) == GOOMBA_CONFIGSAVE) {
			// found configdata
			configdata* cd = (configdata*)sh1;
			uint32_t checksum = 0;

			goomba_configdata* gcd = NULL;
			smsadvance_configdata* scd = NULL;

			if (F16(cd->size) == sizeof(goomba_configdata)) {
				gcd = (goomba_configdata*)cd;
				checksum = F32(gcd->sram_checksum); // 0 = clean, postitive = unclean
			} else if (F16(cd->size) == sizeof(smsadvance_configdata)) {
				scd = (smsadvance_configdata*)cd;
				checksum = F32(scd->sram_checksum); // 0 = clean, postitive = unclean
			} else {
				goomba_error("Unrecognized size of configdata, cannot clean");
				return NULL;
			}

			for (const stateheader* sh2 = first; sh2 && stateheader_plausible(sh2); sh2 = stateheader_advance(sh2)) {
				if (F16(sh2->type) == GOOMBA_SRAMSAVE && F32(sh2->checksum) == checksum) {
					// found stateheader

					if (gcd) gcd->sram_checksum = 0; // because we do this here, goomba_new_sav should not complain about an unclean file
					if (scd) scd->sram_checksum = 0;

					char gbc_data[GOOMBA_COLOR_SRAM_SIZE - GOOMBA_COLOR_AVAILABLE_SIZE];
					memcpy(gbc_data,
						gba_data + GOOMBA_COLOR_AVAILABLE_SIZE,
						sizeof(gbc_data)); // Extract GBC data at 0xe000 to an array

					char* new_gba_data = goomba_new_sav(gba_data, sh2, gbc_data, sizeof(gbc_data));
					if (new_gba_data != NULL) memset(new_gba_data + GOOMBA_COLOR_AVAILABLE_SIZE, 0, sizeof(gbc_data));
					return new_gba_data;
				}
			}
		}
	}

	return (char*)gba_data_param;
}

void* goomba_extract(const void* gba_data, const stateheader* header_ptr, goomba_size_t* size_output) {
	const stateheader* sh = (const stateheader*)header_ptr;

	if (F16(sh->type) != GOOMBA_SRAMSAVE) {
		goomba_error("Error: this program can only extract SRAM data.\n");
		return NULL;
	}

	const int64_t ck = goomba_get_configdata_checksum_field(gba_data);
	if (ck < 0) {
		return NULL;
	} else if (ck == F32(sh->checksum)) {
		goomba_error("File is unclean - run goomba_cleanup before trying to extract SRAM, or you might get old data\n");
		return NULL;
	} else if (ck != 0) {
		fprintf(stderr, "File is unclean, but it shouldn't affect retrieval of the data you asked for\n");
	}
	
	lzo_uint compressed_size = F16(sh->size) - sizeof(stateheader);
	lzo_uint output_size = 32768;
	const unsigned char* compressed_data = (unsigned char*)header_ptr + sizeof(stateheader);
	unsigned char* uncompressed_data = (unsigned char*)malloc(output_size);
	int r = lzo1x_decompress_safe(compressed_data, compressed_size,
		uncompressed_data, &output_size,
		(void*)NULL);
	//fprintf(stderr, "Actual uncompressed size: %lu\n", output_size);
	if (r == LZO_E_INPUT_NOT_CONSUMED) {
		//goomba_error("Warning: input not fully used. Double-check the result to make sure it works.\n");
	} else if (r < 0) {
		goomba_error("Cannot decompress data (lzoconf.h error code %d).\n", r);
		free(uncompressed_data);
		return NULL;
	}
	*size_output = output_size;
	return uncompressed_data;
}

goomba_size_t copy_until_invalid_header(void* dest, const stateheader* src_param) {
	const void* src = src_param;
	goomba_size_t bytes_copied = 0;
	while (1) {
		const stateheader* sh = (const stateheader*)src;
		if (!stateheader_plausible(sh)) break;

		memcpy(dest, src, F16(sh->size));

		src = (char*)src + F16(sh->size);
		dest = (char*)dest + F16(sh->size);
		bytes_copied += F16(sh->size);
	}
	memcpy(dest, src, sizeof(stateheader)); // copy "footer"
	return bytes_copied + sizeof(stateheader);
}

char* goomba_new_sav(const void* gba_data, const void* gba_header, const void* gbc_sram, goomba_size_t gbc_length) {
	unsigned char* gba_header_ptr = (unsigned char*)gba_header;
	stateheader* sh = (stateheader*)gba_header_ptr;

	int64_t ck = goomba_get_configdata_checksum_field(gba_data);
	if (ck < 0) {
		return NULL;
	} else if (ck == F32(sh->checksum)) {
		// have to clean file
		goomba_error("File is unclean - run goomba_cleanup before trying to replace SRAM, or your new data might get overwritten");
		return NULL;
	} else if (ck != 0) {
		fprintf(stderr, "File is unclean, but it shouldn't affect replacement of the data you asked for\n");
	}

	if (F16(sh->type) != GOOMBA_SRAMSAVE) {
		goomba_error("Error - This program cannot replace non-SRAM data.\n");
		return NULL;
	}

	// sh->uncompressed_size is valid for Goomba Color.
	// For Goomba, it's actually compressed size (and will be less than sh->size).
	goomba_size_t uncompressed_size;
	if (F16(sh->size) > F32(sh->uncompressed_size)) {
		// Uncompress to a temporary location, just so we can see how big it is
		goomba_size_t output;
		void* dump = goomba_extract(gba_data, sh, &output);
		if (dump == NULL) {
			return NULL;
		}
		free(dump);
		uncompressed_size = output;
	} else {
		// Goomba Color header - use size from there
		uncompressed_size = F32(sh->uncompressed_size);
	}
	
	if (gbc_length == 0) {
		// Remove data instead of replacing it
		uncompressed_size = 0;
	} else if (gbc_length < uncompressed_size) {
		goomba_error("Error: the length of the GBC data (%u) is too short - expected %u bytes.\n",
			gbc_length, uncompressed_size);
		return NULL;
	} else if (gbc_length - 4 == uncompressed_size) {
		goomba_error("Note: RTC data (TGB_Dual format) will not be copied\n");
	} else if (gbc_length - 44 == uncompressed_size) {
		goomba_error("Note: RTC data (old VBA format) will not be copied\n");
	} else if (gbc_length - 48 == uncompressed_size) {
		goomba_error("Note: RTC data (new VBA format) will not be copied\n");
	} else if (gbc_length > uncompressed_size) {
		goomba_error("Warning: unknown data at end of GBC save file - only first %u bytes will be used\n", uncompressed_size);
	}

	if (F16(sh->type) != GOOMBA_SRAMSAVE) {
		goomba_error("The data at gba_header is not SRAM data.\n");
		return NULL;
	}

	char* const goomba_new_sav = (char*)malloc(GOOMBA_COLOR_SRAM_SIZE);
	memset(goomba_new_sav, 0, GOOMBA_COLOR_SRAM_SIZE);
	char* working = goomba_new_sav; // will be incremented throughout

	goomba_size_t before_header = (char*)gba_header - (char*)gba_data;
	// copy anything before stateheader
	memcpy(goomba_new_sav, gba_data, before_header);
	working += before_header;
	// copy stateheader
	memcpy(working, sh, sizeof(stateheader));
	stateheader* new_sh = (stateheader*)working;
	working += sizeof(stateheader);

	// backup data that comes after this header
	unsigned char* backup = (unsigned char*)malloc(GOOMBA_COLOR_SRAM_SIZE);
	goomba_size_t backup_len = copy_until_invalid_header(backup, (stateheader*)(gba_header_ptr + F16(sh->size)));

	// compress gbc sram
	if (uncompressed_size == 0) {
		// never mind about that header
		working -= sizeof(stateheader);
		memset(working, 0, sizeof(stateheader));
	} else {
		lzo_uint compressed_size;
		unsigned char* dest = (unsigned char*)working;
		void* wrkmem = malloc(LZO1X_1_MEM_COMPRESS);
		lzo1x_1_compress((const unsigned char*)gbc_sram, uncompressed_size,
			dest, &compressed_size,
			wrkmem);
		free(wrkmem);
		working += compressed_size;
		//fprintf(stderr, "Compressed %u bytes (compressed size: %lu)\n", uncompressed_size, compressed_size);

		if (F16(sh->size) > F32(sh->uncompressed_size)) {
			// Goomba header (not Goomba Color)
			new_sh->uncompressed_size = F32(compressed_size);
		}

		new_sh->size = F16((uint16_t)(compressed_size + sizeof(stateheader)));
		// pad to 4 bytes!
		// if I don't do this, goomba color might not load the palette settings, or seemingly 'forget' them later
		// btw, the settings are stored in the configdata struct defined in goombasav.h
		uint16_t s = F16(new_sh->size);
		while (s % 4 != 0) {
			*working = 0;
			working++;
			s++;
		}
		new_sh->size = F16(s);
	}

	goomba_size_t used = working - goomba_new_sav;
	if (used + backup_len > GOOMBA_COLOR_AVAILABLE_SIZE) {
		goomba_error("Not enough room in file for the new save data (0xe000-0xffff must be kept free, I think)\n");
		free(backup);
		free(goomba_new_sav);
		return NULL;
	}
	// restore the backup - just assume we have enough space
	memcpy(working, backup, backup_len);
	free(backup);

	// restore data from 0xe000 to 0xffff
	memcpy(goomba_new_sav + GOOMBA_COLOR_AVAILABLE_SIZE,
		(char*)gba_data + GOOMBA_COLOR_AVAILABLE_SIZE,
		GOOMBA_COLOR_SRAM_SIZE - GOOMBA_COLOR_AVAILABLE_SIZE);

	return goomba_new_sav;
}
