#include "Audio.h"
#include "Logger.h"
#include "Platform.h"
#include "Event.h"
#include "Block.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Game.h"
#include "GameStructs.h"
#include "Errors.h"
#include "Vorbis.h"
#include "Chat.h"
#include "Stream.h"

int Audio_SoundsVolume, Audio_MusicVolume;
static StringsBuffer g_files;

static void Volume_Mix16(int16_t* samples, int count, int volume) {
	int i;

	for (i = 0; i < (count & ~0x07); i += 8, samples += 8) {
		samples[0] = (int16_t) (samples[0] * volume / 100);
		samples[1] = (int16_t) (samples[1] * volume / 100);
		samples[2] = (int16_t) (samples[2] * volume / 100);
		samples[3] = (int16_t) (samples[3] * volume / 100);
		samples[4] = (int16_t) (samples[4] * volume / 100);
		samples[5] = (int16_t) (samples[5] * volume / 100);
		samples[6] = (int16_t) (samples[6] * volume / 100);
		samples[7] = (int16_t) (samples[7] * volume / 100);
	}

	for (; i < count; i++, samples++) {
		samples[0] = (int16_t) (samples[0] * volume / 100);
	}
}

static void Volume_Mix8(uint8_t* samples, int count, int volume) {
	int i;
	for (i = 0; i < count; i++, samples++) {
		samples[0] = (uint8_t) (127 + (samples[0] - 127) * volume / 100);
	}
}

/*########################################################################################################################*
*------------------------------------------------------Soundboard---------------------------------------------------------*
*#########################################################################################################################*/
struct Sound {
	struct AudioFormat Format;
	uint8_t* Data; uint32_t DataSize;
};

#define AUDIO_MAX_SOUNDS 10
struct SoundGroup {
	String Name; int Count;
	struct Sound Sounds[AUDIO_MAX_SOUNDS];
};

struct Soundboard {
	RNGState Rnd; int Count;
	struct SoundGroup Groups[AUDIO_MAX_SOUNDS];
};

#define WAV_FourCC(a, b, c, d) (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d)
#define WAV_FMT_SIZE 16

static ReturnCode Sound_ReadWaveData(struct Stream* stream, struct Sound* snd) {
	uint32_t fourCC, size;
	uint8_t tmp[WAV_FMT_SIZE];
	ReturnCode res;

	if ((res = Stream_Read(stream, tmp, 12))) return res;
	fourCC = Stream_GetU32_BE(&tmp[0]);
	if (fourCC != WAV_FourCC('R','I','F','F')) return WAV_ERR_STREAM_HDR;
	/* tmp[4] (4) file size */
	fourCC = Stream_GetU32_BE(&tmp[8]);
	if (fourCC != WAV_FourCC('W','A','V','E')) return WAV_ERR_STREAM_TYPE;

	for (;;) {
		if ((res = Stream_Read(stream, tmp, 8))) return res;
		fourCC = Stream_GetU32_BE(&tmp[0]);
		size   = Stream_GetU32_LE(&tmp[4]);

		if (fourCC == WAV_FourCC('f','m','t',' ')) {
			if ((res = Stream_Read(stream, tmp, sizeof(tmp)))) return res;
			if (Stream_GetU16_LE(&tmp[0]) != 1) return WAV_ERR_DATA_TYPE;

			snd->Format.Channels      = Stream_GetU16_LE(&tmp[2]);
			snd->Format.SampleRate    = (int) Stream_GetU32_LE(&tmp[4]);
			/* tmp[8] (6) alignment data and stuff */
			snd->Format.BitsPerSample = Stream_GetU16_LE(&tmp[14]);
			size -= WAV_FMT_SIZE;
		} else if (fourCC == WAV_FourCC('d','a','t','a')) {
			snd->Data = Mem_Alloc(size, 1, "WAV sound data");
			snd->DataSize = size;
			return Stream_Read(stream, snd->Data, size);
		}

		/* Skip over unhandled data */
		if (size && (res = stream->Skip(stream, size))) return res;
	}
}

static ReturnCode Sound_ReadWave(const String* filename, struct Sound* snd) {
	String path; char pathBuffer[FILENAME_SIZE];
	struct Stream stream;
	ReturnCode res;

	String_InitArray(path, pathBuffer);
	String_Format1(&path, "audio/%s", filename);

	res = Stream_OpenFile(&stream, &path);
	if (res) return res;

	res = Sound_ReadWaveData(&stream, snd);
	if (res) { stream.Close(&stream); return res; }

	return stream.Close(&stream);
}

static struct SoundGroup* Soundboard_Find(struct Soundboard* board, const String* name) {
	struct SoundGroup* groups = board->Groups;
	int i;

	for (i = 0; i < board->Count; i++) {
		if (String_CaselessEquals(&groups[i].Name, name)) return &groups[i];
	}
	return NULL;
}

static void Soundboard_Init(struct Soundboard* board, const String* boardName, StringsBuffer* l_files) {
	String file, name;
	struct SoundGroup* group;
	struct Sound* snd;
	ReturnCode res;
	int i, dotIndex;

	for (i = 0; i < l_files->Count; i++) {
		file = StringsBuffer_UNSAFE_Get(l_files, i);
		name = file;

		/* dig_grass1.wav -> dig_grass1 */
		dotIndex = String_LastIndexOf(&name, '.');
		if (dotIndex >= 0) { name = String_UNSAFE_Substring(&name, 0, dotIndex); }
		if (!String_CaselessStarts(&name, boardName)) continue;

		/* Convert dig_grass1 to grass */
		name = String_UNSAFE_SubstringAt(&name, boardName->length);
		name = String_UNSAFE_Substring(&name, 0, name.length - 1);

		group = Soundboard_Find(board, &name);
		if (!group) {
			if (board->Count == Array_Elems(board->Groups)) {
				Chat_AddRaw("&cCannot have more than 10 sound groups"); return;
			}

			group = &board->Groups[board->Count++];
			/* NOTE: This keeps a reference to inside buffer of files */
			group->Name = name;
		}

		if (group->Count == Array_Elems(group->Sounds)) {
			Chat_AddRaw("&cCannot have more than 10 sounds in a group"); return;
		}

		snd = &group->Sounds[group->Count];
		res = Sound_ReadWave(&file, snd);

		if (res) {
			Logger_Warn2(res, "decoding", &file);
			Mem_Free(snd->Data);
			snd->Data     = NULL;
			snd->DataSize = 0;
		} else { group->Count++; }
	}
}

static struct Sound* Soundboard_PickRandom(struct Soundboard* board, uint8_t type) {
	String name;
	struct SoundGroup* group;
	int idx;

	if (type == SOUND_NONE || type >= SOUND_COUNT) return NULL;
	if (type == SOUND_METAL) type = SOUND_STONE;

	name  = String_FromReadonly(Sound_Names[type]);
	group = Soundboard_Find(board, &name);
	if (!group) return NULL;

	idx = Random_Range(&board->Rnd, 0, group->Count);
	return &group->Sounds[idx];
}


/*########################################################################################################################*
*--------------------------------------------------------Sounds-----------------------------------------------------------*
*#########################################################################################################################*/
struct SoundOutput { AudioHandle Handle; void* Buffer; uint32_t BufferSize; };
#define AUDIO_MAX_HANDLES 6
#define AUDIO_DEF_ELEMS 0
#define HANDLE_INV -1
#define SOUND_INV { HANDLE_INV, NULL, 0 }

static struct Soundboard digBoard, stepBoard;
static struct SoundOutput monoOutputs[AUDIO_MAX_HANDLES]   = { SOUND_INV, SOUND_INV, SOUND_INV, SOUND_INV, SOUND_INV, SOUND_INV };
static struct SoundOutput stereoOutputs[AUDIO_MAX_HANDLES] = { SOUND_INV, SOUND_INV, SOUND_INV, SOUND_INV, SOUND_INV, SOUND_INV };

CC_NOINLINE static void Sounds_Fail(ReturnCode res) {
	Logger_OldWarn(res, "playing sounds");
	Chat_AddRaw("&cDisabling sounds");
	Audio_SetSounds(0);
}

static void Sounds_PlayRaw(struct SoundOutput* output, struct Sound* snd, struct AudioFormat* fmt, int volume) {
	uint32_t expandBy;
	void* data = snd->Data;
	ReturnCode res;

	if ((res = Audio_SetFormat(output->Handle, fmt))) { Sounds_Fail(res); return; }

	/* copy to temp buffer to apply volume */
	if (volume < 100) {
		if (output->BufferSize < snd->DataSize) {
			expandBy       = snd->DataSize - output->BufferSize;
			output->Buffer = Utils_Resize(output->Buffer, &output->BufferSize,
											1, AUDIO_DEF_ELEMS, expandBy);
		}
		data = output->Buffer;

		Mem_Copy(data, snd->Data, snd->DataSize);
		if (fmt->BitsPerSample == 8) {
			Volume_Mix8(data,  (int) snd->DataSize,     volume);
		} else {
			Volume_Mix16(data, (int) snd->DataSize / 2, volume);
		}
	}

	if ((res = Audio_BufferData(output->Handle, 0, data, snd->DataSize))) { Sounds_Fail(res); return; }
	if ((res = Audio_Play(output->Handle)))                               { Sounds_Fail(res); return; }
}

static void Sounds_Play(uint8_t type, struct Soundboard* board) {
	struct Sound* snd;
	struct AudioFormat  fmt;
	struct SoundOutput* outputs;
	struct SoundOutput* output;
	struct AudioFormat* l;

	bool finished;
	int i, volume;
	ReturnCode res;

	if (type == SOUND_NONE || !Audio_SoundsVolume) return;
	snd = Soundboard_PickRandom(board, type);
	if (!snd) return;

	fmt     = snd->Format;
	volume  = Audio_SoundsVolume;
	outputs = fmt.Channels == 1 ? monoOutputs : stereoOutputs;

	if (board == &digBoard) {
		if (type == SOUND_METAL) fmt.SampleRate = (fmt.SampleRate * 6) / 5;
		else fmt.SampleRate = (fmt.SampleRate * 4) / 5;
	} else {
		volume /= 2;
		if (type == SOUND_METAL) fmt.SampleRate = (fmt.SampleRate * 7) / 5;
	}

	/* Try to play on fresh device, or device with same data format */
	for (i = 0; i < AUDIO_MAX_HANDLES; i++) {
		output = &outputs[i];
		if (output->Handle == HANDLE_INV) {
			Audio_Init(&output->Handle, 1);
		} else {
			res = Audio_IsFinished(output->Handle, &finished);

			if (res) { Sounds_Fail(res); return; }
			if (!finished) continue;
		}

		l = Audio_GetFormat(output->Handle);
		if (!l->Channels || AudioFormat_Eq(l, &fmt)) {
			Sounds_PlayRaw(output, snd, &fmt, volume); return;
		}
	}

	/* Try again with all devices, even if need to recreate one (expensive) */
	for (i = 0; i < AUDIO_MAX_HANDLES; i++) {
		output = &outputs[i];
		res = Audio_IsFinished(output->Handle, &finished);

		if (res) { Sounds_Fail(res); return; }
		if (!finished) continue;

		Sounds_PlayRaw(output, snd, &fmt, volume); return;
	}
}

static void Audio_PlayBlockSound(void* obj, Vector3I coords, BlockID old, BlockID now) {
	(void) obj; // why are these unused?
	(void) coords;
	if (now == BLOCK_AIR) {
		Audio_PlayDigSound(Blocks.DigSounds[old]);
	} else if (!Game_ClassicMode) {
		Audio_PlayDigSound(Blocks.StepSounds[now]);
	}
}

static void Sounds_FreeOutputs(struct SoundOutput* outputs) {
	int i;
	for (i = 0; i < AUDIO_MAX_HANDLES; i++) {
		if (outputs[i].Handle == HANDLE_INV) continue;

		Audio_StopAndFree(outputs[i].Handle);
		outputs[i].Handle = HANDLE_INV;

		Mem_Free(outputs[i].Buffer);
		outputs[i].Buffer     = NULL;
		outputs[i].BufferSize = 0;
	}
}

static void Sounds_Init(void) {
	static const String dig  = String_FromConst("dig_");
	static const String step = String_FromConst("step_");

	if (digBoard.Count || stepBoard.Count) return;
	Soundboard_Init(&digBoard,  &dig,  &g_files);
	Soundboard_Init(&stepBoard, &step, &g_files);
}

static void Sounds_Free(void) {
	Sounds_FreeOutputs(monoOutputs);
	Sounds_FreeOutputs(stereoOutputs);
}

void Audio_SetSounds(int volume) {
	if (volume) Sounds_Init();
	else        Sounds_Free();
	Audio_SoundsVolume = volume;
}

void Audio_PlayDigSound(uint8_t type)  { Sounds_Play(type, &digBoard); }
void Audio_PlayStepSound(uint8_t type) { Sounds_Play(type, &stepBoard); }


/*########################################################################################################################*
*--------------------------------------------------------Music------------------------------------------------------------*
*#########################################################################################################################*/
static AudioHandle music_out;
static void* music_thread;
static void* music_waitable;
static volatile bool music_pendingStop, music_joining;

static ReturnCode Music_Buffer(int i, int16_t* data, int maxSamples, struct VorbisState* ctx) {
	int samples = 0;
	int16_t* cur;
	ReturnCode res = 0, res2;

	while (samples < maxSamples) {
		if ((res = Vorbis_DecodeFrame(ctx))) break;

		cur = &data[samples];
		samples += Vorbis_OutputFrame(ctx, cur);
	}
	if (Audio_MusicVolume < 100) { Volume_Mix16(data, samples, Audio_MusicVolume); }

	res2 = Audio_BufferData(music_out, i, data, (uint32_t) samples * 2);
	if (res2) { music_pendingStop = true; return res2; }
	return res;
}

static ReturnCode Music_PlayOgg(struct Stream* source) {
	uint8_t buffer[OGG_BUFFER_SIZE];
	struct Stream stream;
	struct VorbisState vorbis = { 0 };
	struct AudioFormat fmt;

	int chunkSize, samplesPerSecond;
	int16_t* data = NULL;
	bool completed;
	int i, next;
	ReturnCode res;

	Ogg_MakeStream(&stream, buffer, source);
	vorbis.Source = &stream;
	if ((res = Vorbis_DecodeHeaders(&vorbis))) goto cleanup;

	fmt.Channels      = vorbis.Channels;
	fmt.SampleRate    = vorbis.SampleRate;
	fmt.BitsPerSample = 16;
	if ((res = Audio_SetFormat(music_out, &fmt))) goto cleanup;

	/* largest possible vorbis frame decodes to blocksize1 * channels samples */
	/* so we may end up decoding slightly over a second of audio */
	chunkSize        = (int) fmt.Channels * (fmt.SampleRate + vorbis.BlockSizes[1]);
	samplesPerSecond = (int) fmt.Channels * fmt.SampleRate;
	data = Mem_Alloc((uint32_t) (chunkSize * AUDIO_MAX_BUFFERS), 2, "Ogg - final PCM output");

	/* fill up with some samples before playing */
	for (i = 0; i < AUDIO_MAX_BUFFERS && !res; i++) {
		res = Music_Buffer(i, &data[chunkSize * i], samplesPerSecond, &vorbis);
	}
	if (music_pendingStop) goto cleanup;

	res = Audio_Play(music_out);
	if (res) goto cleanup;

	for (;;) {
		next = -1;

		for (i = 0; i < AUDIO_MAX_BUFFERS; i++) {
			res = Audio_IsCompleted(music_out, i, &completed);
			if (res)       { music_pendingStop = true; break; }
			if (completed) { next = i; break; }
		}

		if (next == -1) { Thread_Sleep(10); continue; }
		if (music_pendingStop) break;

		res = Music_Buffer(next, &data[chunkSize * next], samplesPerSecond, &vorbis);
		/* need to specially handle last bit of audio */
		if (res) break;
	}

	if (music_pendingStop) Audio_Stop(music_out);
	/* Wait until the buffers finished playing */
	for (;;) {
		if (Audio_IsFinished(music_out, &completed) || completed) break;
		Thread_Sleep(10);
	}

cleanup:
	Mem_Free(data);
	Vorbis_Free(&vorbis);
	return res == ERR_END_OF_STREAM ? 0 : res;
}

#define MUSIC_MAX_FILES 512
static void Music_RunLoop(void) {
	static const String ogg = String_FromConst(".ogg");
	char pathBuffer[FILENAME_SIZE];
	String path;

	uint16_t musicFiles[MUSIC_MAX_FILES];
	String file;

	RNGState rnd;
	struct Stream stream;
	int i, count = 0, idx, delay;
	ReturnCode res = 0;

	for (i = 0; i < g_files.Count && count < MUSIC_MAX_FILES; i++) {
		file = StringsBuffer_UNSAFE_Get(&g_files, i);
		if (!String_CaselessEnds(&file, &ogg)) continue;
		musicFiles[count++] = (uint16_t) i;
	}

	Random_SeedFromCurrentTime(&rnd);
	Audio_Init(&music_out, AUDIO_MAX_BUFFERS);

	while (!music_pendingStop && count) {
		idx  = Random_Range(&rnd, 0, count);
		file = StringsBuffer_UNSAFE_Get(&g_files, musicFiles[idx]);

		String_InitArray(path, pathBuffer);
		String_Format1(&path, "audio/%s", &file);
		Platform_Log1("playing music file: %s", &file);

		res = Stream_OpenFile(&stream, &path);
		if (res) { Logger_Warn2(res, "opening", &path); break; }

		res = Music_PlayOgg(&stream);
		if (res) {
			Logger_OldWarn2(res, "playing", &path);
			stream.Close(&stream); break;
		}

		res = stream.Close(&stream);
		if (res) { Logger_Warn2(res, "closing", &path); break; }

		if (music_pendingStop) break;
		delay = 1000 * 120 + Random_Range(&rnd, 0, 1000 * 300);
		Waitable_WaitFor(music_waitable, (uint32_t) delay);
	}

	if (res) {
		Chat_AddRaw("&cDisabling music");
		Audio_MusicVolume = 0;
	}
	Audio_StopAndFree(music_out);

	if (music_joining) return;
	Thread_Detach(music_thread);
	music_thread = NULL;
}

static void Music_Init(void) {
	if (music_thread) return;
	music_joining     = false;
	music_pendingStop = false;

	music_thread = Thread_Start(Music_RunLoop, false);
}

static void Music_Free(void) {
	music_joining     = true;
	music_pendingStop = true;
	Waitable_Signal(music_waitable);

	if (music_thread) Thread_Join(music_thread);
	music_thread = NULL;
}

void Audio_SetMusic(int volume) {
	if (volume) Music_Init();
	else        Music_Free();
	Audio_MusicVolume = volume;
}


/*########################################################################################################################*
*--------------------------------------------------------General----------------------------------------------------------*
*#########################################################################################################################*/
static int AudioManager_GetVolume(const char* volKey, const char* boolKey) {
	int volume = Options_GetInt(volKey, 0, 100, 0);
	if (volume) return volume;

	volume = Options_GetBool(boolKey, false) ? 100 : 0;
	Options_Set(boolKey, NULL);
	return volume;
}

static void AudioManager_FilesCallback(const String* path, void* obj) {
	(void) obj;
	String file = *path; Utils_UNSAFE_GetFilename(&file);
	StringsBuffer_Add(&g_files, &file);
}

static void AudioManager_Init(void) {
	static const String path = String_FromConst("audio");
	int volume;

	if (Directory_Exists(&path)) {
		Directory_Enum(&path, NULL, AudioManager_FilesCallback);
	}
	music_waitable = Waitable_Create();

	volume = AudioManager_GetVolume(OPT_MUSIC_VOLUME, OPT_USE_MUSIC);
	Audio_SetMusic(volume);
	volume = AudioManager_GetVolume(OPT_SOUND_VOLUME, OPT_USE_SOUND);
	Audio_SetSounds(volume);
	Event_RegisterBlock(&UserEvents.BlockChanged, NULL, Audio_PlayBlockSound);
}

static void AudioManager_Free(void) {
	Music_Free();
	Sounds_Free();
	Waitable_Free(music_waitable);
	Event_UnregisterBlock(&UserEvents.BlockChanged, NULL, Audio_PlayBlockSound);
}

static void AudioManager_Reset(void) {
	AudioManager_Free();
	AudioManager_Init();
}

static void AudioManager_OnNewMap(void) { }
static void AudioManager_OnNewMapLoaded(void) { }

struct IGameComponent Audio_Component = {
	AudioManager_Init,  /* Init */
	AudioManager_Free,  /* Free */
	AudioManager_Reset, /* Reset */
	AudioManager_OnNewMap, /* OnNewMap */
	AudioManager_OnNewMapLoaded, /* OnNewMapLoaded */
	NULL /* Next */
};
