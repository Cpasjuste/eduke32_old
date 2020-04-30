/*
 Copyright (C) 2009 Jonathon Fowler <jf@jonof.id.au>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */

/**
 * OggVorbis source support for MultiVoc
 */

#include "compat.h"
#include "cache1d.h"
#include "pragmas.h"

#ifdef HAVE_VORBIS

#include "pitch.h"
#include "multivoc.h"
#include "_multivc.h"

#define OV_EXCLUDE_STATIC_CALLBACKS

#if defined __APPLE__
# include <vorbis/vorbisfile.h>
#elif defined GEKKO
# define USING_TREMOR
# include <tremor/ivorbisfile.h>
#else
# include "vorbis/vorbisfile.h"
#endif

#define BLOCKSIZE MV_MIXBUFFERSIZE


typedef struct {
   void * ptr;
   size_t length;
   size_t pos;

   OggVorbis_File vf;

   char block[BLOCKSIZE];
   int lastbitstream;
} vorbis_data;

// designed with multiple calls in mind
static void MV_GetVorbisCommentLoops(VoiceNode *voice, vorbis_comment *vc)
{
    const char *vc_loopstart = nullptr;
    const char *vc_loopend = nullptr;
    const char *vc_looplength = nullptr;

    for (int comment = 0; comment < vc->comments; ++comment)
    {
        auto entry = (const char *)vc->user_comments[comment];
        if (entry != nullptr && entry[0] != '\0')
        {
            const char *value = Bstrchr(entry, '=');

            if (!value)
                continue;

            const size_t field = value - entry;
            value += 1;

            for (int t = 0; t < loopStartTagCount && vc_loopstart == nullptr; ++t)
            {
                auto tag = loopStartTags[t];
                if (field == Bstrlen(tag) && Bstrncasecmp(entry, tag, field) == 0)
                    vc_loopstart = value;
            }

            for (int t = 0; t < loopEndTagCount && vc_loopend == nullptr; ++t)
            {
                auto tag = loopEndTags[t];
                if (field == Bstrlen(tag) && Bstrncasecmp(entry, tag, field) == 0)
                    vc_loopend = value;
            }

            for (int t = 0; t < loopLengthTagCount && vc_looplength == nullptr; ++t)
            {
                auto tag = loopLengthTags[t];
                if (field == Bstrlen(tag) && Bstrncasecmp(entry, tag, field) == 0)
                    vc_looplength = value;
            }
        }
    }

    if (vc_loopstart != nullptr)
    {
        const ogg_int64_t ov_loopstart = Batol(vc_loopstart);
        if (ov_loopstart >= 0) // a loop starting at 0 is valid
        {
            voice->LoopStart = (const char *) (intptr_t) ov_loopstart;
            voice->LoopSize = 1;
        }
    }
    if (vc_loopend != nullptr)
    {
        if (voice->LoopSize > 0)
        {
            const ogg_int64_t ov_loopend = Batol(vc_loopend);
            if (ov_loopend > 0) // a loop ending at 0 is invalid
                voice->LoopEnd = (const char *) (intptr_t) ov_loopend;
        }
    }
    if (vc_looplength != nullptr)
    {
        if (voice->LoopSize > 0 && voice->LoopEnd == 0)
        {
            const ogg_int64_t ov_looplength = Batol(vc_looplength);
            if (ov_looplength > 0) // a loop of length 0 is invalid
                voice->LoopEnd = (const char *) ((intptr_t) ov_looplength + (intptr_t) voice->LoopStart);
        }
    }
}

// callbacks

static size_t read_vorbis(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    auto vorb = (vorbis_data *)datasource;

    errno = 0;

    if (vorb->length == vorb->pos)
        return 0;

    int nread = 0;

    for (; nmemb > 0; nmemb--, nread++)
    {
        int bytes = vorb->length - vorb->pos;

        if ((signed)size < bytes)
            bytes = (int)size;

        memcpy(ptr, (uint8_t *)vorb->ptr + vorb->pos, bytes);
        vorb->pos += bytes;
        ptr = (uint8_t *)ptr + bytes;

        if (vorb->length == vorb->pos)
        {
            nread++;
            break;
        }
    }

    return nread;
}


static int seek_vorbis(void *datasource, ogg_int64_t offset, int whence)
{
    auto vorb = (vorbis_data *)datasource;

    switch (whence)
    {
        case SEEK_SET: vorb->pos = 0; break;
        case SEEK_CUR: break;
        case SEEK_END: vorb->pos = vorb->length; break;
    }

    vorb->pos += offset;

    if (vorb->pos > vorb->length)
        vorb->pos = vorb->length;

    return vorb->pos;
}

static int close_vorbis(void *datasource)
{
    UNREFERENCED_PARAMETER(datasource);
    return 0;
}

static long tell_vorbis(void *datasource)
{
    auto vorb = (vorbis_data *)datasource;

    return vorb->pos;
}

static ov_callbacks vorbis_callbacks = { read_vorbis, seek_vorbis, close_vorbis, tell_vorbis };


int MV_GetVorbisPosition(VoiceNode *voice)
{
    auto vd = (vorbis_data *) voice->rawdataptr;

    return ov_pcm_tell(&vd->vf);
}

void MV_SetVorbisPosition(VoiceNode *voice, int position)
{
    auto vd = (vorbis_data *) voice->rawdataptr;

    ov_pcm_seek(&vd->vf, position);
}

/*---------------------------------------------------------------------
Function: MV_GetNextVorbisBlock

Controls playback of OggVorbis data
---------------------------------------------------------------------*/

static playbackstatus MV_GetNextVorbisBlock(VoiceNode *voice)
{
    int bitstream;

    int bytesread = 0;
    auto vd = (vorbis_data *)voice->rawdataptr;
    do
    {
#ifdef USING_TREMOR
        int bytes = ov_read(&vd->vf, vd->block + bytesread, BLOCKSIZE - bytesread, &bitstream);
#else
        int bytes = ov_read(&vd->vf, vd->block + bytesread, BLOCKSIZE - bytesread, 0, 2, 1, &bitstream);
#endif
        // fprintf(stderr, "ov_read = %d\n", bytes);
        if (bytes > 0)
        {
            ogg_int64_t currentPosition;
            bytesread += bytes;
            if ((ogg_int64_t)(intptr_t)voice->LoopEnd > 0 &&
                (currentPosition = ov_pcm_tell(&vd->vf)) >= (ogg_int64_t)(intptr_t)voice->LoopEnd)
            {
                bytesread -=
                (currentPosition - (ogg_int64_t)(intptr_t)voice->LoopEnd) * voice->channels * 2;  // (voice->bits>>3)

                int const err = ov_pcm_seek(&vd->vf, (ogg_int64_t)(intptr_t)voice->LoopStart);

                if (err != 0)
                {
                    MV_Printf("MV_GetNextVorbisBlock ov_pcm_seek: LOOP_START %l, LOOP_END %l, err %d\n",
                              (ogg_int64_t)(intptr_t)voice->LoopStart, (ogg_int64_t)(intptr_t)voice->LoopEnd, err);
                }
            }
            continue;
        }
        else if (bytes == OV_HOLE)
            continue;
        else if (bytes == 0)
        {
            if (voice->LoopSize > 0)
            {
                int const err = ov_pcm_seek(&vd->vf, (ogg_int64_t)(intptr_t)voice->LoopStart);

                if (err != 0)
                {
                    MV_Printf("MV_GetNextVorbisBlock ov_pcm_seek: LOOP_START %l, err %d\n",
                              (ogg_int64_t)(intptr_t)voice->LoopStart, err);
                }
                else
                    continue;
            }
            else
            {
                break;
            }
        }
        else if (bytes < 0)
        {
            MV_Printf("MV_GetNextVorbisBlock ov_read: err %d\n", bytes);
            return NoMoreData;
        }
    } while (bytesread < BLOCKSIZE);

    if (bytesread == 0)
        return NoMoreData;

    if (bitstream != vd->lastbitstream)
    {
        vorbis_info *vi = ov_info(&vd->vf, -1);
        if (!vi || (vi->channels != 1 && vi->channels != 2))
            return NoMoreData;

        voice->channels     = vi->channels;
        voice->SamplingRate = vi->rate;
        voice->RateScale    = divideu32(voice->SamplingRate * voice->PitchScale, MV_MixRate);

        voice->FixedPointBufferSize = (voice->RateScale * MV_MIXBUFFERSIZE) - voice->RateScale;
        vd->lastbitstream = bitstream;
        MV_SetVoiceMixMode(voice);
    }

    uint32_t const samples = divideu32(bytesread, ((voice->bits>>3) * voice->channels));

    voice->position = 0;
    voice->sound = vd->block;
    voice->BlockLength = 0;
    voice->length = samples << 16;

#ifdef GEKKO
    // If libtremor had the three additional ov_read() parameters that libvorbis has,
    // this would be better handled using the endianness parameter.
    int16_t *data = (int16_t *)(vd->block);  // assumes signed 16-bit
    for (bytesread = 0; bytesread < BLOCKSIZE / 2; ++bytesread)
        data[bytesread] = (data[bytesread] & 0xff) << 8 | ((data[bytesread] & 0xff00) >> 8);
#endif

    return KeepPlaying;
}


/*---------------------------------------------------------------------
Function: MV_PlayVorbis3D

Begin playback of sound data at specified angle and distance
from listener.
---------------------------------------------------------------------*/

int MV_PlayVorbis3D(char *ptr, uint32_t length, int loophow, int pitchoffset, int angle, int distance, int priority, fix16_t volume, intptr_t callbackval)
{
    if (!MV_Installed)
        return MV_SetErrorCode(MV_NotInstalled);

    if (distance < 0)
    {
        distance = -distance;
        angle += MV_NUMPANPOSITIONS / 2;
    }

    int const vol = MIX_VOLUME(distance);

    // Ensure angle is within 0 - 127
    angle &= MV_MAXPANPOSITION;

    return MV_PlayVorbis(ptr, length, loophow, -1, pitchoffset, max(0, 255 - distance),
                         MV_PanTable[angle][vol].left, MV_PanTable[angle][vol].right, priority, volume, callbackval);
}


/*---------------------------------------------------------------------
Function: MV_PlayVorbis

Begin playback of sound data with the given sound levels and
priority.
---------------------------------------------------------------------*/

int MV_PlayVorbis(char *ptr, uint32_t length, int loopstart, int loopend, int pitchoffset, int vol, int left, int right, int priority, fix16_t volume, intptr_t callbackval)
{
    UNREFERENCED_PARAMETER(loopend);

    if (!MV_Installed)
        return MV_SetErrorCode(MV_NotInstalled);

    VoiceNode *voice = MV_AllocVoice(priority);

    if (voice == nullptr)
        return MV_SetErrorCode(MV_NoVoices);

    voice->ptrlock = CACHE1D_PERMANENT;

    if (voice->rawdataptr == nullptr || voice->wavetype != FMT_VORBIS)
        g_cache.allocateBlock((intptr_t *)&voice->rawdataptr, sizeof(vorbis_data), &voice->ptrlock);

    vorbis_data *vd = (vorbis_data *)voice->rawdataptr;

    vd->ptr    = ptr;
    vd->pos    = 0;
    vd->length = length;

    vd->lastbitstream = -1;

    int status = ov_open_callbacks((void *)vd, &vd->vf, 0, 0, vorbis_callbacks);
    vorbis_info *vi;

    if (status < 0 || ((vi = ov_info(&vd->vf, 0)) == nullptr) || vi->channels < 1 || vi->channels > 2)
    {
        if (status == 0)
            ov_clear(&vd->vf);
        else
            MV_Printf("MV_PlayVorbis: err %d\n", status);

        voice->ptrlock = CACHE1D_FREE;
        return MV_SetErrorCode(MV_InvalidFile);
    }

    voice->wavetype    = FMT_VORBIS;
    voice->bits        = 16;
    voice->channels    = vi->channels;
    voice->rawdataptr  = (void *)vd;
    voice->GetSound    = MV_GetNextVorbisBlock;
    voice->NextBlock   = vd->block;
    voice->LoopCount   = 0;
    voice->BlockLength = 0;
    voice->length      = 0;
    voice->next        = nullptr;
    voice->prev        = nullptr;
    voice->priority    = priority;
    voice->callbackval = callbackval;

    voice->LoopStart = nullptr;
    voice->LoopEnd   = nullptr;
    voice->LoopSize  = (loopstart >= 0 ? 1 : 0);

    // load loop tags from metadata
    if (auto comment = ov_comment(&vd->vf, 0))
        MV_GetVorbisCommentLoops(voice, comment);

    voice->Paused = FALSE;

    MV_SetVoicePitch(voice, vi->rate, pitchoffset);
    MV_SetVoiceMixMode(voice);

    MV_SetVoiceVolume(voice, vol, left, right, volume);
    MV_PlayVoice(voice);

    return voice->handle;
}

void MV_ReleaseVorbisVoice( VoiceNode * voice )
{
    if (voice->wavetype != FMT_VORBIS)
        return;

    auto vd = (vorbis_data *)voice->rawdataptr;

    voice->length = 0;
    voice->sound = nullptr;
    voice->ptrlock = CACHE1D_UNLOCKED;

    ov_clear(&vd->vf);
}
#else
#include "_multivc.h"

int MV_PlayVorbis(char *ptr, uint32_t ptrlength, int loopstart, int loopend, int pitchoffset,
    int vol, int left, int right, int priority, fix16_t volume, intptr_t callbackval)
{
    UNREFERENCED_PARAMETER(ptr);
    UNREFERENCED_PARAMETER(ptrlength);
    UNREFERENCED_PARAMETER(loopstart);
    UNREFERENCED_PARAMETER(loopend);
    UNREFERENCED_PARAMETER(pitchoffset);
    UNREFERENCED_PARAMETER(vol);
    UNREFERENCED_PARAMETER(left);
    UNREFERENCED_PARAMETER(right);
    UNREFERENCED_PARAMETER(priority);
    UNREFERENCED_PARAMETER(volume);
    UNREFERENCED_PARAMETER(callbackval);

    MV_Printf("MV_PlayVorbis: OggVorbis support not included in this binary.\n");
    return -1;
}

int MV_PlayVorbis3D(char *ptr, uint32_t ptrlength, int loophow, int pitchoffset, int angle,
    int distance, int priority, fix16_t volume, intptr_t callbackval)
{
    UNREFERENCED_PARAMETER(ptr);
    UNREFERENCED_PARAMETER(ptrlength);
    UNREFERENCED_PARAMETER(loophow);
    UNREFERENCED_PARAMETER(pitchoffset);
    UNREFERENCED_PARAMETER(angle);
    UNREFERENCED_PARAMETER(distance);
    UNREFERENCED_PARAMETER(priority);
    UNREFERENCED_PARAMETER(volume);
    UNREFERENCED_PARAMETER(callbackval);

    MV_Printf("MV_PlayVorbis: OggVorbis support not included in this binary.\n");
    return -1;
}
#endif //HAVE_VORBIS
