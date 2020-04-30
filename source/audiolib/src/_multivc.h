/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

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
/**********************************************************************
   file:   _MULTIVC.H

   author: James R. Dose
   date:   December 20, 1993

   Private header for MULTIVOC.C

   (c) Copyright 1993 James R. Dose.  All Rights Reserved.
**********************************************************************/

#ifndef MULTIVC_H_
#define MULTIVC_H_

#include "multivoc.h"

#define VOC_8BIT            0x0
#define VOC_16BIT           0x4

#define T_SIXTEENBIT_STEREO 0
#define T_MONO         1
#define T_16BITSOURCE  2
#define T_STEREOSOURCE 4
#define T_DEFAULT      T_SIXTEENBIT_STEREO

#define MV_MAXPANPOSITION  127  /* formerly 31 */
#define MV_NUMPANPOSITIONS ( MV_MAXPANPOSITION + 1 )
#define MV_MAXTOTALVOLUME  255
#define MV_MAXVOLUME       127  /* formerly 63 */

// mirrors FX_MUSIC_PRIORITY from fx_man.h
#define MV_MUSIC_PRIORITY INT_MAX

#define MIX_VOLUME(volume) ((max(0, min((volume), 255)) * (MV_MAXVOLUME + 1)) >> 8)

extern struct VoiceNode *MV_Voices;
extern struct VoiceNode  VoiceList;
extern struct VoiceNode  VoicePool;

extern fix16_t MV_GlobalVolume;
extern fix16_t MV_VolumeSmooth;

static FORCE_INLINE fix16_t SMOOTH_VOLUME(fix16_t const volume, fix16_t const dest)
{
    return volume + fix16_fast_trunc_mul(dest - volume, MV_VolumeSmooth);
}

template <typename T>
static inline conditional_t< is_signed<T>::value, make_unsigned_t<T>, make_signed_t<T> > FLIP_SIGN(T src)
{
    static constexpr make_unsigned_t<T> msb = ((make_unsigned_t<T>)1) << (sizeof(T) * CHAR_BIT - 1u);
    return src ^ msb;
}

template <typename T>
static inline enable_if_t<is_signed<T>::value, T> SCALE_SAMPLE(T src, fix16_t volume)
{
    return (T)fix16_fast_trunc_mul_int_by_fix16(src, volume);
}

template <typename T>
static inline T CONVERT_SAMPLE_FROM_SIGNED(int src);

template<>
inline int16_t CONVERT_SAMPLE_FROM_SIGNED<int16_t>(int src)
{
    return src;
}

template <typename T>
static inline int CONVERT_SAMPLE_TO_SIGNED(T src);

template<>
inline int CONVERT_SAMPLE_TO_SIGNED<int16_t>(int16_t src)
{
    return src;
}

template <typename S, typename D>
static inline int CONVERT_LE_SAMPLE_TO_SIGNED(S src);

template<>
inline int CONVERT_LE_SAMPLE_TO_SIGNED<uint8_t, int16_t>(uint8_t src)
{
    return FLIP_SIGN(src) << 8;
}

template<>
inline int CONVERT_LE_SAMPLE_TO_SIGNED<int16_t, int16_t>(int16_t src)
{
    return B_LITTLE16(src);
}

template <typename T>
static int CLAMP_SAMPLE(int src);

template<>
inline int CLAMP_SAMPLE<int16_t>(int src)
{
    return clamp(src, INT16_MIN, INT16_MAX);
}

template <typename T>
static T MIX_SAMPLES(int signed_sample, T untouched_sample)
{
    return CONVERT_SAMPLE_FROM_SIGNED<T>(CLAMP_SAMPLE<T>(signed_sample + CONVERT_SAMPLE_TO_SIGNED<T>(untouched_sample)));
}

struct split16_t
{
    explicit split16_t(uint16_t x) : v{x} {}

    uint8_t l() const
    {
        return (v & 0x00FFu);
    }
    uint8_t h() const
    {
        return (v & 0xFF00u) >> CHAR_BIT;
    }

private:
    uint16_t v;
};

#define MV_MIXBUFFERSIZE     256
#define MV_NUMBEROFBUFFERS   32
#define MV_TOTALBUFFERSIZE   ( MV_MIXBUFFERSIZE * MV_NUMBEROFBUFFERS )

typedef enum : bool
{
    NoMoreData,
    KeepPlaying
} playbackstatus;


typedef struct VoiceNode
{
    struct VoiceNode *next;
    struct VoiceNode *prev;

    playbackstatus (*GetSound)(struct VoiceNode *);

    uint32_t (*mix)(struct VoiceNode *, uint32_t);

    const char *sound;

    fix16_t LeftVolume, LeftVolumeDest;
    fix16_t RightVolume, RightVolumeDest;

    union
    {
        void *rawdataptr;
        void (*DemandFeed)(const char** ptr, uint32_t* length, void* userdata);
    };

    const char *NextBlock;
    const char *LoopStart;
    const char *LoopEnd;

    wavefmt_t wavetype;
    char bits;
    char channels;
    char ptrlock;

    fix16_t volume;

    int      LoopCount;
    uint32_t LoopSize;
    uint32_t BlockLength;

    int ptrlength;  // ptrlength-1 is the max permissible index for rawdataptr

    uint32_t PitchScale;
    uint32_t FixedPointBufferSize;

    uint32_t length;
    uint32_t SamplingRate;
    uint32_t RateScale;
    uint32_t position;
    int Paused;

    int handle;
    int priority;

    intptr_t callbackval;
    void *userdata;
} VoiceNode;

typedef struct
{
    uint8_t left;
    uint8_t right;
} Pan;

typedef struct
{
    char RIFF[4];
    uint32_t file_size;
    char WAVE[4];
    char fmt[4];
    uint32_t format_size;
} riff_header;

typedef struct
{
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t nBitsPerSample;
} format_header;

typedef struct
{
    uint8_t DATA[4];
    uint32_t size;
} data_header;

extern Pan MV_PanTable[ MV_NUMPANPOSITIONS ][ MV_MAXVOLUME + 1 ];
extern int MV_ErrorCode;
extern int MV_Installed;
extern int MV_MixRate;
extern char *MV_MusicBuffer;
extern int MV_BufferSize;

extern int MV_MaxVoices;
extern int MV_Channels;
extern int MV_MixRate;
extern void *MV_InitDataPtr;

extern int MV_MIDIRenderTempo;
extern int MV_MIDIRenderTimer;

static FORCE_INLINE int MV_SetErrorCode(int status)
{
    MV_ErrorCode = status;
    return MV_Error;
}

void MV_PlayVoice(VoiceNode *voice);

VoiceNode *MV_AllocVoice(int priority);

void MV_SetVoiceMixMode(VoiceNode *voice);
void MV_SetVoiceVolume(VoiceNode *voice, int vol, int left, int right, fix16_t volume);
void MV_SetVoicePitch(VoiceNode *voice, uint32_t rate, int pitchoffset);

int  MV_GetVorbisPosition(VoiceNode *voice);
void MV_SetVorbisPosition(VoiceNode *voice, int position);
int  MV_GetFLACPosition(VoiceNode *voice);
void MV_SetFLACPosition(VoiceNode *voice, int position);
int  MV_GetXAPosition(VoiceNode *voice);
void MV_SetXAPosition(VoiceNode *voice, int position);
int  MV_GetXMPPosition(VoiceNode *voice);
void MV_SetXMPPosition(VoiceNode *voice, int position);

void MV_ReleaseVorbisVoice(VoiceNode *voice);
void MV_ReleaseFLACVoice(VoiceNode *voice);
void MV_ReleaseXAVoice(VoiceNode *voice);
void MV_ReleaseXMPVoice(VoiceNode *voice);

#ifdef HAVE_XMP
extern int MV_XMPInterpolation;
#endif

// implemented in mix.c
template <typename S, typename D> uint32_t MV_MixMono(struct VoiceNode * const voice, uint32_t length);
template <typename S, typename D> uint32_t MV_MixStereo(struct VoiceNode * const voice, uint32_t length);
template <typename T> void MV_Reverb(char const *src, char * const dest, const fix16_t volume, int count);

// implemented in mixst.c
template <typename S, typename D> uint32_t MV_MixMonoStereo(struct VoiceNode * const voice, uint32_t length);
template <typename S, typename D> uint32_t MV_MixStereoStereo(struct VoiceNode * const voice, uint32_t length);

extern char *MV_MixDestination;  // pointer to the next output sample
extern int MV_SampleSize;
extern int MV_RightChannelOffset;

#define loopStartTagCount 3
extern const char *loopStartTags[loopStartTagCount];
#define loopEndTagCount 2
extern const char *loopEndTags[loopEndTagCount];
#define loopLengthTagCount 2
extern const char *loopLengthTags[loopLengthTagCount];

#if defined __POWERPC__ || defined GEKKO
# define BIGENDIAN
#endif

#endif
