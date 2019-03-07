/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file libtimidity.cpp Playing music via the timidity library. */

#include "../stdafx.h"
#include "../openttd.h"
#include "../sound_type.h"
#include "../debug.h"
#include "libtimidity.h"
#include "midifile.hpp"
#include "../base_media_base.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <timidity.h>
#if defined(__vita__)
#include "../mixer.h"
#endif

#include "../safeguards.h"

/** The state of playing. */
enum MidiState {
	MIDI_STOPPED = 0,
	MIDI_PLAYING = 1,
};

static struct {
	MidIStream *stream;
	MidSongOptions options;
	MidSong *song;

	MidiState status;
	uint32 song_length;
	uint32 song_position;
	int gain;
} _midi; ///< Metadata about the midi we're playing.

#if defined(__vita__)
static void RenderMusicStream(int16 *buffer, size_t samples)
{
	if (_midi.status != MIDI_PLAYING || !_midi.song) return;
	size_t size = mid_song_read_wave(_midi.song, (int8*)buffer, samples*4);
}

#endif


/** Factory for the libtimidity driver. */
static FMusicDriver_LibTimidity iFMusicDriver_LibTimidity;

const char *MusicDriver_LibTimidity::Start(const char * const *param)
{
	_midi.status = MIDI_STOPPED;
	_midi.song = NULL;
	_midi.gain = 40;
#if defined(__vita__)
	if (mid_init("ux0:/data/openttd/timidity/timidity.cfg") < 0) {
#else
	if (mid_init(param == NULL ? NULL : const_cast<char *>(param[0])) < 0) {
#endif
		/* If init fails, it can be because no configuration was found.
		 *  If it was not forced via param, try to load it without a
		 *  configuration. Who knows that works. */
		if (param != NULL || mid_init_no_config() < 0) {
			return "error initializing timidity";
		}
	}
	DEBUG(driver, 1, "successfully initialised timidity");

#if defined(__vita__)
	uint32 samplerate = MxSetMusicSource(RenderMusicStream);
	_midi.options.rate = samplerate;
#else
	_midi.options.rate = 44100;
#endif
	_midi.options.format = MID_AUDIO_S16LSB;
	_midi.options.channels = 2;
	_midi.options.buffer_size = _midi.options.rate;

	return NULL;
}

void MusicDriver_LibTimidity::Stop()
{
#if defined(__vita__)
	MxSetMusicSource(NULL);
#endif
	if (_midi.status == MIDI_PLAYING) this->StopSong();
	mid_exit();
}

void MusicDriver_LibTimidity::PlaySong(const MusicSongInfo &song)
{
	std::string filename = MidiFile::GetSMFFile(song);

	this->StopSong();
	if (filename.empty()) return;

	_midi.stream = mid_istream_open_file(filename.c_str());
	if (_midi.stream == NULL) {
		DEBUG(driver, 0, "Could not open music file");
		return;
	}

	_midi.song = mid_song_load(_midi.stream, &_midi.options);
	mid_istream_close(_midi.stream);
	_midi.song_length = mid_song_get_total_time(_midi.song);

	if (_midi.song == NULL) {
		DEBUG(driver, 1, "Invalid MIDI file");
		return;
	}

	mid_song_start(_midi.song);
	mid_song_set_volume(_midi.song, _midi.gain);
	_midi.status = MIDI_PLAYING;
}

void MusicDriver_LibTimidity::StopSong()
{
	_midi.status = MIDI_STOPPED;
	/* mid_song_free cannot handle NULL! */
	if (_midi.song != NULL) mid_song_free(_midi.song);
	_midi.song = NULL;
}

bool MusicDriver_LibTimidity::IsSongPlaying()
{
	if (_midi.status == MIDI_PLAYING) {
		_midi.song_position = mid_song_get_time(_midi.song);
		if (_midi.song_position >= _midi.song_length) {
			_midi.status = MIDI_STOPPED;
			_midi.song_position = 0;
		}
	}

	return (_midi.status == MIDI_PLAYING);
}

void MusicDriver_LibTimidity::SetVolume(byte vol)
{
	/* libtimidity's default volume is 70, but that is much too loud.
	 * Set gain using OpenTTD's volume, as a number between 0
	 * and 20. */
	_midi.gain = (20.0 * vol) / (128.0);
	if (_midi.song != NULL) mid_song_set_volume(_midi.song, _midi.gain);
}
