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

#include "_multivc.h"

template uint32_t MV_MixMonoStereo<uint8_t, int16_t>(struct VoiceNode * const voice, uint32_t length);
template uint32_t MV_MixStereoStereo<uint8_t, int16_t>(struct VoiceNode * const voice, uint32_t length);
template uint32_t MV_MixMonoStereo<int16_t, int16_t>(struct VoiceNode * const voice, uint32_t length);
template uint32_t MV_MixStereoStereo<int16_t, int16_t>(struct VoiceNode * const voice, uint32_t length);

/*
 length = count of samples to mix
 position = offset of starting sample in source
 rate = resampling increment
 volume = direct volume adjustment, 1.0 = no change
 */

// stereo source, mono output
template <typename S, typename D>
uint32_t MV_MixMonoStereo(struct VoiceNode * const voice, uint32_t length)
{
    auto const * __restrict source = (S const *)voice->sound;
    auto       * __restrict dest   = (D *)MV_MixDestination;

    uint32_t       position = voice->position;
    uint32_t const rate     = voice->RateScale;
    fix16_t const  volume   = fix16_fast_trunc_mul(voice->volume, MV_GlobalVolume);

    do
    {
        auto const isample0 = CONVERT_LE_SAMPLE_TO_SIGNED<S, D>(source[(position >> 16) << 1]);
        auto const isample1 = CONVERT_LE_SAMPLE_TO_SIGNED<S, D>(source[((position >> 16) << 1) + 1]);

        position += rate;

        *dest = MIX_SAMPLES<D>((SCALE_SAMPLE((isample0 + isample1) >> 1, fix16_fast_trunc_mul(volume, voice->LeftVolume))), *dest);
        dest++;

        voice->LeftVolume = SMOOTH_VOLUME(voice->LeftVolume, voice->LeftVolumeDest);
    }
    while (--length);

    MV_MixDestination = (char *)dest;

    return position;
}

// stereo source, stereo output
template <typename S, typename D>
uint32_t MV_MixStereoStereo(struct VoiceNode * const voice, uint32_t length)
{
    auto const * __restrict source = (S const *)voice->sound;
    auto       * __restrict dest   = (D *)MV_MixDestination;

    uint32_t       position = voice->position;
    uint32_t const rate     = voice->RateScale;
    fix16_t const  volume   = fix16_fast_trunc_mul(voice->volume, MV_GlobalVolume);

    do
    {
        auto const isample0 = CONVERT_LE_SAMPLE_TO_SIGNED<S, D>(source[(position >> 16) << 1]);
        auto const isample1 = CONVERT_LE_SAMPLE_TO_SIGNED<S, D>(source[((position >> 16) << 1) + 1]);

        position += rate;

        *dest = MIX_SAMPLES<D>(SCALE_SAMPLE(isample0, fix16_fast_trunc_mul(volume, voice->LeftVolume)), *dest);
        *(dest + (MV_RightChannelOffset / sizeof(*dest)))
            = MIX_SAMPLES<D>(SCALE_SAMPLE(isample1, fix16_fast_trunc_mul(volume, voice->RightVolume)), *(dest + (MV_RightChannelOffset / sizeof(*dest))));
        dest += 2;

        voice->LeftVolume = SMOOTH_VOLUME(voice->LeftVolume, voice->LeftVolumeDest);
        voice->RightVolume = SMOOTH_VOLUME(voice->RightVolume, voice->RightVolumeDest);
    }
    while (--length);

    MV_MixDestination = (char *)dest;

    return position;
}
