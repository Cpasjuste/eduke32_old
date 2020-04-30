//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2019 EDuke32 developers and contributors
Copyright (C) 2019 Nuke.YKT

This file is part of NBlood.

NBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

/**********************************************************************
   module: MIDI.C

   author: James R. Dose
   date:   May 25, 1994

   Midi song file playback routines.

   (c) Copyright 1994 James R. Dose.  All Rights Reserved.
**********************************************************************/

#include "midi.h"

#include "_midi.h"
#include "compat.h"
#include "drivers.h"
#include "music.h"
#include "pragmas.h"
#include "sndcards.h"

extern int MV_MixRate;
extern int ASS_MIDISoundDriver;

int MIDI_GetDevice()
{
    return ASS_MIDISoundDriver;
}

static const int _MIDI_CommandLengths[NUM_MIDI_CHANNELS] = { 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 1, 1, 2, 0 };

static track * _MIDI_TrackPtr;
static int _MIDI_TrackMemSize;
static int _MIDI_NumTracks;

static int _MIDI_SongActive;
static int _MIDI_SongLoaded;
static int _MIDI_Loop;

static int  _MIDI_Division;
static int  _MIDI_Tick;
static int  _MIDI_Beat    = 1;
static int  _MIDI_Measure = 1;
static uint32_t _MIDI_Time;
static int  _MIDI_BeatsPerMeasure;
static int  _MIDI_TicksPerBeat;
static int  _MIDI_TimeBase;
static int  _MIDI_FPSecondsPerTick;
static uint32_t _MIDI_TotalTime;
static int  _MIDI_TotalTicks;
static int  _MIDI_TotalBeats;
static int  _MIDI_TotalMeasures;

uint32_t _MIDI_PositionInTicks;
uint32_t _MIDI_GlobalPositionInTicks;

static int  _MIDI_Context;

static int _MIDI_ActiveTracks;
static int _MIDI_TotalVolume = MIDI_MaxVolume;

static int _MIDI_ChannelVolume[ NUM_MIDI_CHANNELS ];

static midifuncs *_MIDI_Funcs;

static int _MIDI_Reset;

int MV_MIDIRenderTempo = -1;
int MV_MIDIRenderTimer;

static char *_MIDI_SongPtr;

static void _MIDI_SetChannelVolume(int channel, int volume);

void MIDI_Restart(void)
{
    MIDI_PlaySong(_MIDI_SongPtr, _MIDI_Loop);
}

static int _MIDI_ReadNumber(void *from, size_t size)
{
    if (size > 4)
        size = 4;

    char *FromPtr = (char *)from;
    int value = 0;

    while (size--)
    {
        value <<= 8;
        value += *FromPtr++;
    }

    return value;
}

static int _MIDI_ReadDelta(track *ptr)
{
    int value;

    GET_NEXT_EVENT(ptr, value);

    if (value & 0x80)
    {
        value &= 0x7f;
        char c;

        do
        {
            GET_NEXT_EVENT(ptr, c);
            value = (value << 7) + (c & 0x7f);
        }
        while (c & 0x80);
    }

    return value;
}

static void _MIDI_ResetTracks(void)
{
    _MIDI_Tick = 0;
    _MIDI_Beat = 1;
    _MIDI_Measure = 1;
    _MIDI_Time = 0;
    _MIDI_BeatsPerMeasure = 4;
    _MIDI_TicksPerBeat = _MIDI_Division;
    _MIDI_TimeBase = 4;
    _MIDI_PositionInTicks = 0;
    _MIDI_ActiveTracks    = 0;
    _MIDI_Context         = 0;

    track *ptr = _MIDI_TrackPtr;
    for (bssize_t i = 0; i < _MIDI_NumTracks; ++i)
    {
        ptr->pos                    = ptr->start;
        ptr->delay                  = _MIDI_ReadDelta(ptr);
        ptr->active                 = ptr->EMIDI_IncludeTrack;
        ptr->RunningStatus          = 0;
        ptr->currentcontext         = 0;
        ptr->context[ 0 ].loopstart = ptr->start;
        ptr->context[ 0 ].loopcount = 0;

        if (ptr->active)
            _MIDI_ActiveTracks++;

        ptr++;
    }
}

static void _MIDI_AdvanceTick(void)
{
    _MIDI_PositionInTicks++;
    _MIDI_Time += _MIDI_FPSecondsPerTick;

    _MIDI_Tick++;
    while (_MIDI_Tick > _MIDI_TicksPerBeat)
    {
        _MIDI_Tick -= _MIDI_TicksPerBeat;
        _MIDI_Beat++;
    }
    while (_MIDI_Beat > _MIDI_BeatsPerMeasure)
    {
        _MIDI_Beat -= _MIDI_BeatsPerMeasure;
        _MIDI_Measure++;
    }
}

static void _MIDI_SysEx(track *Track)
{
    int length = _MIDI_ReadDelta(Track);
    Track->pos += length;
}


static void _MIDI_MetaEvent(track *Track)
{
    int   command;
    int   length;

    GET_NEXT_EVENT(Track, command);
    GET_NEXT_EVENT(Track, length);

    switch (command)
    {
        case MIDI_END_OF_TRACK:
            Track->active = FALSE;

            _MIDI_ActiveTracks--;
            break;

        case MIDI_TEMPO_CHANGE:
        {
            int tempo = tabledivide32_noinline(60000000L, _MIDI_ReadNumber(Track->pos, 3));
            MIDI_SetTempo(tempo);
            break;
        }

        case MIDI_TIME_SIGNATURE:
        {
            if ((_MIDI_Tick > 0) || (_MIDI_Beat > 1))
                _MIDI_Measure++;

            _MIDI_Tick = 0;
            _MIDI_Beat = 1;
            _MIDI_TimeBase = 1;
            _MIDI_BeatsPerMeasure = (int)*Track->pos;
            int denominator = (int) * (Track->pos + 1);

            while (denominator > 0)
            {
                _MIDI_TimeBase += _MIDI_TimeBase;
                denominator--;
            }

            _MIDI_TicksPerBeat = tabledivide32_noinline(_MIDI_Division * 4, _MIDI_TimeBase);
            break;
        }
    }

    Track->pos += length;
}

static int _MIDI_InterpretControllerInfo(track *Track, int TimeSet, int channel, int c1, int c2)
{
    track *trackptr;
    int tracknum;
    int loopcount;

    switch (c1)
    {
        case MIDI_MONO_MODE_ON :
            Track->pos++;
            break;

        case MIDI_VOLUME :
            if (!Track->EMIDI_VolumeChange)
                _MIDI_SetChannelVolume(channel, c2);
            break;

        case EMIDI_INCLUDE_TRACK :
        case EMIDI_EXCLUDE_TRACK :
            break;

        case EMIDI_PROGRAM_CHANGE :
            if (Track->EMIDI_ProgramChange)
                _MIDI_Funcs->ProgramChange(channel, c2 & 0x7f);
            break;

        case EMIDI_VOLUME_CHANGE :
            if (Track->EMIDI_VolumeChange)
                _MIDI_SetChannelVolume(channel, c2);
            break;

        case EMIDI_CONTEXT_START :
            break;

        case EMIDI_CONTEXT_END :
            if ((Track->currentcontext == _MIDI_Context) || (_MIDI_Context < 0) ||
                (Track->context[_MIDI_Context].pos == nullptr))
                break;

            Track->currentcontext = _MIDI_Context;
            Track->context[ 0 ].loopstart = Track->context[ _MIDI_Context ].loopstart;
            Track->context[ 0 ].loopcount = Track->context[ _MIDI_Context ].loopcount;
            Track->pos           = Track->context[ _MIDI_Context ].pos;
            Track->RunningStatus = Track->context[ _MIDI_Context ].RunningStatus;

            if (TimeSet)
            {
                break;
            }

            _MIDI_Time             = Track->context[ _MIDI_Context ].time;
            _MIDI_FPSecondsPerTick = Track->context[ _MIDI_Context ].FPSecondsPerTick;
            _MIDI_Tick             = Track->context[ _MIDI_Context ].tick;
            _MIDI_Beat             = Track->context[ _MIDI_Context ].beat;
            _MIDI_Measure          = Track->context[ _MIDI_Context ].measure;
            _MIDI_BeatsPerMeasure  = Track->context[ _MIDI_Context ].BeatsPerMeasure;
            _MIDI_TicksPerBeat     = Track->context[ _MIDI_Context ].TicksPerBeat;
            _MIDI_TimeBase         = Track->context[ _MIDI_Context ].TimeBase;
            TimeSet = TRUE;
            break;

        case EMIDI_LOOP_START :
        case EMIDI_SONG_LOOP_START :
            loopcount = (c2 == 0) ? EMIDI_INFINITE : c2;

            if (c1 == EMIDI_SONG_LOOP_START)
            {
                trackptr = _MIDI_TrackPtr;
                tracknum = _MIDI_NumTracks;
            }
            else
            {
                trackptr = Track;
                tracknum = 1;
            }

            while (tracknum > 0)
            {
                trackptr->context[ 0 ].loopcount        = loopcount;
                trackptr->context[ 0 ].pos              = trackptr->pos;
                trackptr->context[ 0 ].loopstart        = trackptr->pos;
                trackptr->context[ 0 ].RunningStatus    = trackptr->RunningStatus;
                trackptr->context[ 0 ].active           = trackptr->active;
                trackptr->context[ 0 ].delay            = trackptr->delay;
                trackptr->context[ 0 ].time             = _MIDI_Time;
                trackptr->context[ 0 ].FPSecondsPerTick = _MIDI_FPSecondsPerTick;
                trackptr->context[ 0 ].tick             = _MIDI_Tick;
                trackptr->context[ 0 ].beat             = _MIDI_Beat;
                trackptr->context[ 0 ].measure          = _MIDI_Measure;
                trackptr->context[ 0 ].BeatsPerMeasure  = _MIDI_BeatsPerMeasure;
                trackptr->context[ 0 ].TicksPerBeat     = _MIDI_TicksPerBeat;
                trackptr->context[ 0 ].TimeBase         = _MIDI_TimeBase;
                trackptr++;
                tracknum--;
            }
            break;

        case EMIDI_LOOP_END :
        case EMIDI_SONG_LOOP_END :
            if ((c2 != EMIDI_END_LOOP_VALUE) || (Track->context[0].loopstart == nullptr) || (Track->context[0].loopcount == 0))
                break;

            if (c1 == EMIDI_SONG_LOOP_END)
            {
                trackptr = _MIDI_TrackPtr;
                tracknum = _MIDI_NumTracks;
                _MIDI_ActiveTracks = 0;
            }
            else
            {
                trackptr = Track;
                tracknum = 1;
                _MIDI_ActiveTracks--;
            }

            while (tracknum > 0)
            {
                if (trackptr->context[ 0 ].loopcount != EMIDI_INFINITE)
                {
                    trackptr->context[ 0 ].loopcount--;
                }

                trackptr->pos           = trackptr->context[ 0 ].loopstart;
                trackptr->RunningStatus = trackptr->context[ 0 ].RunningStatus;
                trackptr->delay         = trackptr->context[ 0 ].delay;
                trackptr->active        = trackptr->context[ 0 ].active;
                if (trackptr->active)
                {
                    _MIDI_ActiveTracks++;
                }

                if (!TimeSet)
                {
                    _MIDI_Time             = trackptr->context[ 0 ].time;
                    _MIDI_FPSecondsPerTick = trackptr->context[ 0 ].FPSecondsPerTick;
                    _MIDI_Tick             = trackptr->context[ 0 ].tick;
                    _MIDI_Beat             = trackptr->context[ 0 ].beat;
                    _MIDI_Measure          = trackptr->context[ 0 ].measure;
                    _MIDI_BeatsPerMeasure  = trackptr->context[ 0 ].BeatsPerMeasure;
                    _MIDI_TicksPerBeat     = trackptr->context[ 0 ].TicksPerBeat;
                    _MIDI_TimeBase         = trackptr->context[ 0 ].TimeBase;
                    TimeSet = TRUE;
                }

                trackptr++;
                tracknum--;
            }
            break;

        default :
            if (_MIDI_Funcs->ControlChange)
                _MIDI_Funcs->ControlChange(channel, c1, c2);
    }

    return TimeSet;
}

void MIDI_ServiceRoutine(void)
{
    if (!_MIDI_SongActive)
        return;

    track *Track = _MIDI_TrackPtr;
    int tracknum = 0;
    int TimeSet = FALSE;
    int c1 = 0;
    int c2 = 0;

    while (tracknum < _MIDI_NumTracks)
    {
        while ((Track->active) && (Track->delay == 0))
        {
            int event;
            GET_NEXT_EVENT(Track, event);

            if (GET_MIDI_COMMAND(event) == MIDI_SPECIAL)
            {
                switch (event)
                {
                    case MIDI_SYSEX:
                    case MIDI_SYSEX_CONTINUE: _MIDI_SysEx(Track); break;
                    case MIDI_META_EVENT: _MIDI_MetaEvent(Track); break;
                }

                if (Track->active)
                    Track->delay = _MIDI_ReadDelta(Track);
                continue;
            }

            if (event & MIDI_RUNNING_STATUS)
                Track->RunningStatus = event;
            else
            {
                event = Track->RunningStatus;
                Track->pos--;
            }

            int const channel = GET_MIDI_CHANNEL(event);
            int const command = GET_MIDI_COMMAND(event);

            if (_MIDI_CommandLengths[ command ] > 0)
            {
                GET_NEXT_EVENT(Track, c1);
                if (_MIDI_CommandLengths[ command ] > 1)
                    GET_NEXT_EVENT(Track, c2);
            }

            switch (command)
            {
                case MIDI_NOTE_OFF:
                    if (_MIDI_Funcs->NoteOff)
                        _MIDI_Funcs->NoteOff(channel, c1, c2);
                    break;

                case MIDI_NOTE_ON:
                    if (_MIDI_Funcs->NoteOn)
                        _MIDI_Funcs->NoteOn(channel, c1, c2);
                    break;

                case MIDI_POLY_AFTER_TCH:
                    if (_MIDI_Funcs->PolyAftertouch)
                        _MIDI_Funcs->PolyAftertouch(channel, c1, c2);
                    break;

                case MIDI_CONTROL_CHANGE:
                    TimeSet = _MIDI_InterpretControllerInfo(Track, TimeSet, channel, c1, c2);
                    break;

                case MIDI_PROGRAM_CHANGE:
                    if ((_MIDI_Funcs->ProgramChange) && (!Track->EMIDI_ProgramChange))
                        _MIDI_Funcs->ProgramChange(channel, c1 & 0x7f);
                    break;

                case MIDI_AFTER_TOUCH:
                    if (_MIDI_Funcs->ChannelAftertouch)
                        _MIDI_Funcs->ChannelAftertouch(channel, c1);
                    break;

                case MIDI_PITCH_BEND:
                    if (_MIDI_Funcs->PitchBend)
                        _MIDI_Funcs->PitchBend(channel, c1, c2);
                    break;

                default: break;
            }

            Track->delay = _MIDI_ReadDelta(Track);
        }

        Track->delay--;
        Track++;
        tracknum++;

        if (_MIDI_ActiveTracks == 0)
        {
            _MIDI_ResetTracks();
            if (_MIDI_Loop)
            {
                tracknum = 0;
                Track = _MIDI_TrackPtr;
            }
            else
            {
                _MIDI_SongActive = FALSE;
                break;
            }
        }
    }

    _MIDI_AdvanceTick();
    _MIDI_GlobalPositionInTicks++;
}

static int _MIDI_SendControlChange(int channel, int c1, int c2)
{
    if (_MIDI_Funcs == nullptr || _MIDI_Funcs->ControlChange == nullptr)
        return MIDI_Error;

    _MIDI_Funcs->ControlChange(channel, c1, c2);

    return MIDI_Ok;
}

static int _MIDI_SendProgramChange(int channel, int c1)
{
    if (_MIDI_Funcs == nullptr || _MIDI_Funcs->ProgramChange == nullptr)
        return MIDI_Error;

    _MIDI_Funcs->ProgramChange(channel, c1);

    return MIDI_Ok;
}

int MIDI_AllNotesOff(void)
{
    SoundDriver_MIDI_Lock();

    for (bssize_t channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
    {
        _MIDI_SendControlChange(channel, MIDI_HOLD1, 0);
        _MIDI_SendControlChange(channel, MIDI_SOSTENUTO, 0);
        _MIDI_SendControlChange(channel, MIDI_ALL_NOTES_OFF, 0);
        _MIDI_SendControlChange(channel, MIDI_ALL_SOUNDS_OFF, 0);
    }

    SoundDriver_MIDI_Unlock();

    return MIDI_Ok;
}

static void _MIDI_SetChannelVolume(int channel, int volume)
{
    _MIDI_ChannelVolume[channel] = volume;

    if (_MIDI_Funcs == nullptr || _MIDI_Funcs->ControlChange == nullptr)
        return;

    if (_MIDI_Funcs->SetVolume == nullptr)
    {
        volume *= _MIDI_TotalVolume;
        volume = tabledivide32_noinline(volume, MIDI_MaxVolume);
    }

    _MIDI_Funcs->ControlChange(channel, MIDI_VOLUME, volume);
}

static void _MIDI_SendChannelVolumes(void)
{
    for (bssize_t channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
        _MIDI_SetChannelVolume(channel, _MIDI_ChannelVolume[channel]);
}

int MIDI_Reset(void)
{
    MIDI_AllNotesOff();

    SoundDriver_MIDI_Lock();

    for (bssize_t channel = 0; channel < NUM_MIDI_CHANNELS; channel++)
    {
        _MIDI_SendControlChange(channel, MIDI_RESET_ALL_CONTROLLERS, 0);
        _MIDI_SendControlChange(channel, MIDI_RPN_MSB, MIDI_PITCHBEND_MSB);
        _MIDI_SendControlChange(channel, MIDI_RPN_LSB, MIDI_PITCHBEND_LSB);
        _MIDI_SendControlChange(channel, MIDI_DATAENTRY_MSB, 2); /* Pitch Bend Sensitivity MSB */
        _MIDI_SendControlChange(channel, MIDI_DATAENTRY_LSB, 0); /* Pitch Bend Sensitivity LSB */
        _MIDI_ChannelVolume[channel] = GENMIDI_DefaultVolume;
        _MIDI_SendControlChange(channel, MIDI_PAN, 64);  // begin TURRICAN's recommendation
        _MIDI_SendControlChange(channel, MIDI_REVERB, 40);
        _MIDI_SendControlChange(channel, MIDI_CHORUS, 0);
        _MIDI_SendControlChange(channel, MIDI_BANK_SELECT_MSB, 0);
        _MIDI_SendControlChange(channel, MIDI_BANK_SELECT_LSB, 0);
        _MIDI_SendProgramChange(channel, 0);  // end TURRICAN's recommendation
    }

    _MIDI_SendChannelVolumes();

    SoundDriver_MIDI_Unlock();

    _MIDI_Reset = TRUE;

    return MIDI_Ok;
}


int MIDI_SetVolume(int volume)
{
    if (_MIDI_Funcs == nullptr)
        return MIDI_NullMidiModule;

    volume = min(MIDI_MaxVolume, volume);
    volume = max(0, volume);

    _MIDI_TotalVolume = volume;

    SoundDriver_MIDI_Lock();

    if (_MIDI_Funcs->SetVolume)
        _MIDI_Funcs->SetVolume(volume);
    else
        _MIDI_SendChannelVolumes();

    SoundDriver_MIDI_Unlock();

    return MIDI_Ok;
}


int MIDI_GetVolume(void)
{
    if (_MIDI_Funcs == nullptr)
        return MIDI_NullMidiModule;

    SoundDriver_MIDI_Lock();
    int volume = (_MIDI_Funcs->GetVolume) ? _MIDI_Funcs->GetVolume() : _MIDI_TotalVolume;
    SoundDriver_MIDI_Unlock();

    return volume;
}

void MIDI_SetLoopFlag(int loopflag) { _MIDI_Loop = loopflag; }

void MIDI_ContinueSong(void)
{
    if (!_MIDI_SongLoaded)
        return;

    _MIDI_SongActive = TRUE;
}

void MIDI_PauseSong(void)
{
    if (!_MIDI_SongLoaded)
        return;

    _MIDI_SongActive = FALSE;
    MIDI_AllNotesOff();
}

void MIDI_SetMidiFuncs(midifuncs *funcs) { _MIDI_Funcs = funcs; }

void MIDI_StopSong(void)
{
    if (!_MIDI_SongLoaded)
        return;

    SoundDriver_MIDI_HaltPlayback();

    _MIDI_SongActive = FALSE;
    _MIDI_SongLoaded = FALSE;

    MIDI_Reset();
    _MIDI_ResetTracks();

    DO_FREE_AND_NULL(_MIDI_TrackPtr);

    _MIDI_NumTracks    = 0;
    _MIDI_TrackMemSize = 0;

    _MIDI_TotalTime     = 0;
    _MIDI_TotalTicks    = 0;
    _MIDI_TotalBeats    = 0;
    _MIDI_TotalMeasures = 0;
}

static void _MIDI_InitEMIDI(void);

int MIDI_PlaySong(char *song, int loopflag)
{
    if (_MIDI_Funcs == nullptr)
        return MIDI_NullMidiModule;

    if (B_UNBUF32(song) != MIDI_HEADER_SIGNATURE)
        return MIDI_InvalidMidiFile;

    _MIDI_SongPtr = song;

    song += 4;
    int const headersize = _MIDI_ReadNumber(song, 4);
    song += 4;
    int const format = _MIDI_ReadNumber(song, 2);

    int My_MIDI_NumTracks = _MIDI_ReadNumber(song + 2, 2);
    int My_MIDI_Division  = _MIDI_ReadNumber(song + 4, 2);

    if (My_MIDI_Division < 0)
    {
        // If a SMPTE time division is given, just set to 96 so no errors occur
        My_MIDI_Division = 96;
    }

    if (format > MAX_FORMAT)
        return MIDI_UnknownMidiFormat;

    char *ptr = song + headersize;

    if (My_MIDI_NumTracks == 0)
        return MIDI_NoTracks;

    int My_MIDI_TrackMemSize = My_MIDI_NumTracks  * sizeof(track);
    track * My_MIDI_TrackPtr = (track *)Xmalloc(My_MIDI_TrackMemSize);

    auto CurrentTrack = My_MIDI_TrackPtr;
    int numtracks    = My_MIDI_NumTracks;

    while (numtracks--)
    {
        if (B_UNBUF32(ptr) != MIDI_TRACK_SIGNATURE)
        {
            DO_FREE_AND_NULL(My_MIDI_TrackPtr);

            My_MIDI_TrackMemSize = 0;

            return MIDI_InvalidTrack;
        }

        int tracklength = _MIDI_ReadNumber(ptr + 4, 4);
        ptr += 8;
        CurrentTrack->start = ptr;
        CurrentTrack->EMIDI_IncludeTrack = FALSE;
        ptr += tracklength;
        CurrentTrack++;
    }

    // at this point we know song load is successful

    if (_MIDI_SongLoaded)
        MIDI_StopSong();

    _MIDI_Loop = loopflag;
    _MIDI_NumTracks = My_MIDI_NumTracks;
    _MIDI_Division = My_MIDI_Division;
    _MIDI_TrackMemSize = My_MIDI_TrackMemSize;
    _MIDI_TrackPtr = My_MIDI_TrackPtr;

    _MIDI_InitEMIDI();
    _MIDI_ResetTracks();

    if (!_MIDI_Reset)
        MIDI_Reset();

    _MIDI_Reset = FALSE;

    // this can either stay like this, or I can add another field to the MIDI driver spec that holds the service callback
    if (SoundDriver_MIDI_StartPlayback() != MIDI_Ok)
        return MIDI_DriverError;

    MIDI_SetTempo(120);

    _MIDI_SongLoaded = TRUE;
    _MIDI_SongActive = TRUE;

    return MIDI_Ok;
}

void MIDI_SetTempo(int tempo)
{
    SoundDriver_MIDI_SetTempo(tempo, _MIDI_Division);
    int const tickspersecond = tempo * _MIDI_Division / 60;
    _MIDI_FPSecondsPerTick = tabledivide32_noinline(1 << TIME_PRECISION, tickspersecond);
}

static void _MIDI_InitEMIDI(void)
{
    int const type = (ASS_EMIDICard != -1) ? ASS_EMIDICard : SoundDriver_MIDI_GetCardType();

    _MIDI_ResetTracks();

    _MIDI_TotalTime     = 0;
    _MIDI_TotalTicks    = 0;
    _MIDI_TotalBeats    = 0;
    _MIDI_TotalMeasures = 0;

    track *Track = _MIDI_TrackPtr;
    int tracknum = 0;

    while ((tracknum < _MIDI_NumTracks) && (Track != nullptr))
    {
        _MIDI_Tick = 0;
        _MIDI_Beat = 1;
        _MIDI_Measure = 1;
        _MIDI_Time = 0;
        _MIDI_BeatsPerMeasure = 4;
        _MIDI_TicksPerBeat = _MIDI_Division;
        _MIDI_TimeBase = 4;

        _MIDI_PositionInTicks = 0;
        _MIDI_ActiveTracks    = 0;
        _MIDI_Context         = -1;

        Track->RunningStatus = 0;
        Track->active        = TRUE;

        Track->EMIDI_ProgramChange = FALSE;
        Track->EMIDI_VolumeChange  = FALSE;
        Track->EMIDI_IncludeTrack  = TRUE;

        memset(Track->context, 0, sizeof(Track->context));

        while (Track->delay > 0)
        {
            _MIDI_AdvanceTick();
            Track->delay--;
        }

        int IncludeFound = FALSE;

        while (Track->active)
        {
            int event;

            GET_NEXT_EVENT(Track, event);

            if (GET_MIDI_COMMAND(event) == MIDI_SPECIAL)
            {
                switch (event)
                {
                    case MIDI_SYSEX:
                    case MIDI_SYSEX_CONTINUE: _MIDI_SysEx(Track); break;
                    case MIDI_META_EVENT: _MIDI_MetaEvent(Track); break;
                }

                if (Track->active)
                {
                    Track->delay = _MIDI_ReadDelta(Track);
                    while (Track->delay > 0)
                    {
                        _MIDI_AdvanceTick();
                        Track->delay--;
                    }
                }

                continue;
            }

            if (event & MIDI_RUNNING_STATUS)
                Track->RunningStatus = event;
            else
            {
                event = Track->RunningStatus;
                Track->pos--;
            }

//            channel = GET_MIDI_CHANNEL(event);
            int const command = GET_MIDI_COMMAND(event);
            int length = _MIDI_CommandLengths[ command ];

            if (command == MIDI_CONTROL_CHANGE)
            {
                if (*Track->pos == MIDI_MONO_MODE_ON)
                    length++;

                int c1, c2;
                GET_NEXT_EVENT(Track, c1);
                GET_NEXT_EVENT(Track, c2);
                length -= 2;

                switch (c1)
                {
                case EMIDI_LOOP_START :
                case EMIDI_SONG_LOOP_START :
                    Track->context[ 0 ].loopcount        = (c2 == 0) ? EMIDI_INFINITE : c2;
                    Track->context[ 0 ].pos              = Track->pos;
                    Track->context[ 0 ].loopstart        = Track->pos;
                    Track->context[ 0 ].RunningStatus    = Track->RunningStatus;
                    Track->context[ 0 ].time             = _MIDI_Time;
                    Track->context[ 0 ].FPSecondsPerTick = _MIDI_FPSecondsPerTick;
                    Track->context[ 0 ].tick             = _MIDI_Tick;
                    Track->context[ 0 ].beat             = _MIDI_Beat;
                    Track->context[ 0 ].measure          = _MIDI_Measure;
                    Track->context[ 0 ].BeatsPerMeasure  = _MIDI_BeatsPerMeasure;
                    Track->context[ 0 ].TicksPerBeat     = _MIDI_TicksPerBeat;
                    Track->context[ 0 ].TimeBase         = _MIDI_TimeBase;
                    break;

                case EMIDI_LOOP_END :
                case EMIDI_SONG_LOOP_END :
                    if (c2 == EMIDI_END_LOOP_VALUE)
                    {
                        Track->context[ 0 ].loopstart = nullptr;
                        Track->context[ 0 ].loopcount = 0;
                    }
                    break;

                case EMIDI_INCLUDE_TRACK :
                    if (EMIDI_AffectsCurrentCard(c2, type))
                    {
                        //printf( "Include track %d on card %d\n", tracknum, c2 );
                        IncludeFound = TRUE;
                        Track->EMIDI_IncludeTrack = TRUE;
                    }
                    else if (!IncludeFound)
                    {
                        //printf( "Track excluded %d on card %d\n", tracknum, c2 );
                        IncludeFound = TRUE;
                        Track->EMIDI_IncludeTrack = FALSE;
                    }
                    break;

                case EMIDI_EXCLUDE_TRACK :
                    if (EMIDI_AffectsCurrentCard(c2, type))
                    {
                        //printf( "Exclude track %d on card %d\n", tracknum, c2 );
                        Track->EMIDI_IncludeTrack = FALSE;
                    }
                    break;

                case EMIDI_PROGRAM_CHANGE :
                    if (!Track->EMIDI_ProgramChange)
                        //printf( "Program change on track %d\n", tracknum );
                        Track->EMIDI_ProgramChange = TRUE;
                    break;

                case EMIDI_VOLUME_CHANGE :
                    if (!Track->EMIDI_VolumeChange)
                        //printf( "Volume change on track %d\n", tracknum );
                        Track->EMIDI_VolumeChange = TRUE;
                    break;

                case EMIDI_CONTEXT_START :
                    if ((c2 > 0) && (c2 < EMIDI_NUM_CONTEXTS))
                    {
                        Track->context[ c2 ].pos              = Track->pos;
                        Track->context[ c2 ].loopstart        = Track->context[ 0 ].loopstart;
                        Track->context[ c2 ].loopcount        = Track->context[ 0 ].loopcount;
                        Track->context[ c2 ].RunningStatus    = Track->RunningStatus;
                        Track->context[ c2 ].time             = _MIDI_Time;
                        Track->context[ c2 ].FPSecondsPerTick = _MIDI_FPSecondsPerTick;
                        Track->context[ c2 ].tick             = _MIDI_Tick;
                        Track->context[ c2 ].beat             = _MIDI_Beat;
                        Track->context[ c2 ].measure          = _MIDI_Measure;
                        Track->context[ c2 ].BeatsPerMeasure  = _MIDI_BeatsPerMeasure;
                        Track->context[ c2 ].TicksPerBeat     = _MIDI_TicksPerBeat;
                        Track->context[ c2 ].TimeBase         = _MIDI_TimeBase;
                    }
                    break;

                case EMIDI_CONTEXT_END :
                    break;
                }
            }

            Track->pos += length;
            Track->delay = _MIDI_ReadDelta(Track);

            while (Track->delay > 0)
            {
                _MIDI_AdvanceTick();
                Track->delay--;
            }
        }

        _MIDI_TotalTime = max(_MIDI_TotalTime, _MIDI_Time);
        if (RELATIVE_BEAT(_MIDI_Measure, _MIDI_Beat, _MIDI_Tick) >
            RELATIVE_BEAT(_MIDI_TotalMeasures, _MIDI_TotalBeats, _MIDI_TotalTicks))
        {
            _MIDI_TotalTicks = _MIDI_Tick;
            _MIDI_TotalBeats = _MIDI_Beat;
            _MIDI_TotalMeasures = _MIDI_Measure;
        }

        Track++;
        tracknum++;
    }

    _MIDI_ResetTracks();
}

