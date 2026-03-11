#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h> // tolower()
#include <sys/stat.h>
#include "pt2_structs.h"
#include "pt2_textout.h"
#include "pt2_mouse.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_diskop.h"
#include "pt2_askbox.h"

#define PLAYBACK_FREQ 16574 /* C-3, period 214 */

static void removeSampleFileExt(char *text) // for sample saver
{
	if (text == NULL || text[0] == '\0')
		return;
	
	uint32_t filenameLength = (uint32_t)strlen(text);
	if (filenameLength < 5)
		return;

	// remove .wav/.iff/from end of sample name (if present)
	uint32_t fileExtPos = filenameLength - 4;
	if (fileExtPos > 0 && (!strncmp(&text[fileExtPos], ".wav", 4) || !strncmp(&text[fileExtPos], ".iff", 4)))
		text[fileExtPos] = '\0';
}

static void iffWriteChunkHeader(FILE *f, char *chunkName, uint32_t chunkLen)
{
	fwrite(chunkName, sizeof (int32_t), 1, f);
	chunkLen = SWAP32(chunkLen);
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	chunkLen = SDL_Swap32(chunkLen);
#endif
	fwrite(&chunkLen, sizeof (int32_t), 1, f);
}


static void iffWriteUint32(FILE *f, uint32_t value)
{
	value = SWAP32(value);
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	value = SDL_Swap32(value);
#endif
	fwrite(&value, sizeof (int32_t), 1, f);
}

static void iffWriteUint16(FILE *f, uint16_t value)
{
	value = SWAP16(value);
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	value = SDL_Swap16(value);
#endif
	fwrite(&value, sizeof (int16_t), 1, f);
}

static void iffWriteUint8(FILE *f, const uint8_t value)
{
	fwrite(&value, sizeof (int8_t), 1, f);
}

static void iffWriteChunkData(FILE *f, const void *data, size_t length)
{
	fwrite(data, sizeof (int8_t), length, f);
	if (length & 1) fputc(0, f); // write pad byte if chunk size is uneven
}

bool saveSample(bool checkIfFileExist, bool giveNewFreeFilename)
{
	char fileName[128], tmpBuffer[64];
	struct stat statBuffer;
	wavHeader_t wavHeader;
	samplerChunk_t samplerChunk;
	mptExtraChunk_t mptExtraChunk;

	memset(tmpBuffer, 0, sizeof (tmpBuffer));
	memset(fileName, 0, sizeof (fileName));

	const moduleSample_t *s = &song->samples[editor.currSample];

	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return false;
	}

	// get sample filename
	if (s->text[0] == '\0')
	{
		strcpy(fileName, "untitled");
	}
	else
	{
		for (int32_t i = 0; i < 22; i++)
		{
			tmpBuffer[i] = (char)tolower(song->samples[editor.currSample].text[i]);
			if (tmpBuffer[i] == '\0') break;
			sanitizeFilenameChar(&tmpBuffer[i]);
		}

		strcpy(fileName, tmpBuffer);
	}
	
	removeSampleFileExt(fileName);
	addSampleFileExt(fileName);

	// if the user picked "no" to overwriting the file, generate a new filename
	if (giveNewFreeFilename && stat(fileName, &statBuffer) == 0)
	{
		for (int32_t j = 1; j <= 999; j++)
		{
			if (s->text[0] == '\0')
			{
				sprintf(fileName, "untitled-%d", j);
			}
			else
			{
				for (int32_t i = 0; i < 22; i++)
				{
					tmpBuffer[i] = (char)tolower(song->samples[editor.currSample].text[i]);
					if (tmpBuffer[i] == '\0') break;
					sanitizeFilenameChar(&tmpBuffer[i]);
				}

				removeSampleFileExt(tmpBuffer);
				sprintf(fileName, "%s-%d", tmpBuffer, j);
			}

			addSampleFileExt(fileName);

			if (stat(fileName, &statBuffer) != 0)
				break; // this filename can be used
		}
	}

	if (checkIfFileExist && stat(fileName, &statBuffer) == 0)
	{
		if (!askBox(ASKBOX_YES_NO, "OVERWRITE FILE ?"))
			return false;
	}

	FILE *f = fopen(fileName, "wb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	const int8_t *sampleData = &song->sampleData[s->offset];
	uint32_t sampleLength = s->length;
	uint32_t loopStart = s->loopStart & ~1;
	uint32_t loopLength = s->loopLength & ~1;

	switch (diskop.smpSaveType)
	{
		default:
		case DISKOP_SMP_WAV:
		{
			wavHeader.format = SDL_Swap32(0x45564157); // "WAVE"
			wavHeader.chunkID = SDL_Swap32(0x46464952); // "RIFF"
			wavHeader.subchunk1ID = SDL_Swap32(0x20746D66); // "fmt "
			wavHeader.subchunk2ID = SDL_Swap32(0x61746164); // "data"
			wavHeader.subchunk1Size = SDL_Swap32(16);
			wavHeader.subchunk2Size = SDL_Swap32(sampleLength);
			wavHeader.chunkSize = 36 + wavHeader.subchunk2Size;
			wavHeader.audioFormat = SDL_Swap16(1);
			wavHeader.numChannels = SDL_Swap16(1);
			wavHeader.bitsPerSample = SDL_Swap16(8);
			wavHeader.sampleRate = SDL_Swap32(PLAYBACK_FREQ);
			wavHeader.byteRate = SDL_Swap32((SDL_Swap32(wavHeader.sampleRate) * SDL_Swap16(wavHeader.numChannels) * SDL_Swap16(wavHeader.bitsPerSample)) / 8);
			wavHeader.blockAlign = SDL_Swap16(SDL_Swap16(wavHeader.numChannels) * SDL_Swap16(wavHeader.bitsPerSample) / 8);

			// set "sampler" chunk if loop is enabled
			if (loopStart+loopLength > 2) // loop enabled?
			{
				wavHeader.chunkSize += sizeof (samplerChunk_t);
				memset(&samplerChunk, 0, sizeof (samplerChunk_t));
				samplerChunk.chunkID = SDL_Swap32(0x6C706D73); // "smpl"
				samplerChunk.chunkSize = SDL_Swap32(60);
				samplerChunk.dwSamplePeriod = SDL_Swap32(1000000000 / PLAYBACK_FREQ);
				samplerChunk.dwMIDIUnityNote = SDL_Swap32(60); // 60 = MIDI middle-C
				samplerChunk.cSampleLoops = SDL_Swap32(1);
				samplerChunk.loop.dwStart = SDL_Swap32(loopStart);
				samplerChunk.loop.dwEnd = SDL_Swap32(loopStart) + SDL_Swap32(loopLength) - 1;
			}

			// set ModPlug Tracker chunk (used for sample volume only in this case)
			wavHeader.chunkSize += sizeof (mptExtraChunk);
			memset(&mptExtraChunk, 0, sizeof (mptExtraChunk));
			mptExtraChunk.chunkID = SDL_Swap32(0x61727478); // "xtra"
			mptExtraChunk.chunkSize = sizeof (mptExtraChunk) - 4 - 4;
			mptExtraChunk.defaultPan = SDL_Swap16(128); // 0..255
			mptExtraChunk.defaultVolume = SDL_Swap16(s->volume * 4); // 0..256
			mptExtraChunk.globalVolume = SDL_Swap16(64); // 0..64

			fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);

			for (uint32_t i = 0; i < sampleLength; i++)
				fputc((uint8_t)(sampleData[i] + 128), f);

			if (sampleLength & 1)
				fputc(0, f); // pad byte needed for 8-bit samples

			if (loopStart+loopLength > 2) // loop enabled?
				fwrite(&samplerChunk, sizeof (samplerChunk), 1, f);

			fwrite(&mptExtraChunk, sizeof (mptExtraChunk), 1, f);
		}
		break;

		case DISKOP_SMP_IFF:
		{
			// "FORM" chunk
			iffWriteChunkHeader(f, "FORM", 0); // "FORM" chunk size is overwritten later
			iffWriteUint32(f, 0x38535658); // "8SVX"

			// "VHDR" chunk
			iffWriteChunkHeader(f, "VHDR", 20);

			if (loopStart+loopLength > 2) // loop enabled?
			{
				iffWriteUint32(f, loopStart); // oneShotHiSamples (loop start when loop is enabled)
				iffWriteUint32(f, loopLength); // repeatHiSamples
			}
			else
			{
				iffWriteUint32(f, sampleLength); // oneShotHiSamples (length of sample when no loop)
				iffWriteUint32(f, 0); // repeatHiSamples
			}

			iffWriteUint32(f, 0); // samplesPerHiCycle
			iffWriteUint16(f, PLAYBACK_FREQ); // samplesPerSec
			iffWriteUint8(f, 1); // ctOctave (number of samples)
			iffWriteUint8(f, 0); // sCompression
			iffWriteUint32(f, s->volume * 1024); // volume (max: 65536/0x10000)

			// "NAME" chunk
			uint32_t chunkLen = (uint32_t)strlen(s->text);
			if (chunkLen > 0)
			{
				iffWriteChunkHeader(f, "NAME", chunkLen);
				iffWriteChunkData(f, s->text, chunkLen);
			}

			// "ANNO" chunk (we put the program name here)
			const char annoStr[] = "ProTracker 2 clone";
			chunkLen = sizeof (annoStr) - 1;
			iffWriteChunkHeader(f, "ANNO", chunkLen);
			iffWriteChunkData(f, annoStr, chunkLen);

			// "BODY" chunk
			chunkLen = sampleLength;
			iffWriteChunkHeader(f, "BODY", chunkLen);
			iffWriteChunkData(f, sampleData, chunkLen);

			// go back and fill in "FORM" chunk size
			chunkLen = ftell(f) - 8;
			fseek(f, 4, SEEK_SET);
			iffWriteUint32(f, chunkLen);
		}
		break;

		case DISKOP_SMP_RAW:
			fwrite(sampleData, 1, sampleLength, f);
		break;
	}

	fclose(f);

	displayMsg("SAMPLE SAVED !");
	setMsgPointer();

	diskop.cached = false;
	if (ui.diskOpScreenShown)
		ui.updateDiskOpFileList = true;

	return true;
}
