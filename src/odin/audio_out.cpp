/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "odin/audio_out.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

namespace odin {

namespace {

constexpr int RATE = 44100;

uint32_t
channels(AudioOut::Mode mode)
{
	return mode == AudioOut::Mode::QUAD ? 4 : 2;
}

} /* namespace */

std::unique_ptr<AudioOut>
AudioOut::make(Mode mode, double option_db)
{
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		std::fprintf(stderr, "ERROR: sound: %s\n", SDL_GetError());
		return nullptr;
	}

	/* The buffer of the device as before: 1024 frames */
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");

	const SDL_AudioSpec spec = { SDL_AUDIO_S16, static_cast<int>(channels(mode)), RATE };
	SDL_AudioStream *const stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (!stream) {
		std::fprintf(stderr, "ERROR: sound: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return nullptr;
	}
	SDL_ResumeAudioStreamDevice(stream);
	return std::unique_ptr<AudioOut>(new AudioOut(stream, mode, gain_of(option_db)));
}

AudioOut::~AudioOut()
{
	SDL_DestroyAudioStream(m_stream);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void
AudioOut::play(std::span<const int16_t> main, std::span<const int16_t> option)
{
	const uint32_t n = channels(m_mode);

	interleave(m_mode, main, option, m_option_gain, m_frames);
	if (m_frames.size() < n)
		return;

	const uint32_t queued = static_cast<uint32_t>(std::max(SDL_GetAudioStreamQueued(m_stream), 0)) / (n * 2);
	const uint32_t low = RATE * 30 / 1000, high = RATE * 120 / 1000;

	/* After a pause or a stall, start again from what is due now */
	if (queued > 4 * high)
		SDL_ClearAudioStream(m_stream);
	if (queued > high)
		m_frames.resize(m_frames.size() - n);
	else if (queued < low) {
		const std::vector<int16_t> last(m_frames.end() - n, m_frames.end());
		m_frames.insert(m_frames.end(), last.begin(), last.end());
	}
	SDL_PutAudioStreamData(m_stream, m_frames.data(), static_cast<int>(m_frames.size() * 2));
}

} /* namespace odin */
