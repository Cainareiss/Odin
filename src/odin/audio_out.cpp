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

#include <SDL.h>

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
	if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		std::fprintf(stderr, "ERROR: sound: %s\n", SDL_GetError());
		return nullptr;
	}

	SDL_AudioSpec want = {};
	want.freq = RATE;
	want.format = AUDIO_S16SYS;
	want.channels = static_cast<Uint8>(channels(mode));
	want.samples = 1024;

	const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
	if (!device) {
		std::fprintf(stderr, "ERROR: sound: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return nullptr;
	}
	SDL_PauseAudioDevice(device, 0);
	return std::unique_ptr<AudioOut>(new AudioOut(device, mode, gain_of(option_db)));
}

AudioOut::~AudioOut()
{
	SDL_CloseAudioDevice(m_device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void
AudioOut::play(std::span<const int16_t> main, std::span<const int16_t> option)
{
	const uint32_t n = channels(m_mode);

	interleave(m_mode, main, option, m_option_gain, m_frames);
	if (m_frames.size() < n)
		return;

	const uint32_t queued = SDL_GetQueuedAudioSize(m_device) / (n * 2);
	const uint32_t low = RATE * 30 / 1000, high = RATE * 120 / 1000;

	/* After a pause or a stall, start again from what is due now */
	if (queued > 4 * high)
		SDL_ClearQueuedAudio(m_device);
	if (queued > high)
		m_frames.resize(m_frames.size() - n);
	else if (queued < low) {
		const std::vector<int16_t> last(m_frames.end() - n, m_frames.end());
		m_frames.insert(m_frames.end(), last.begin(), last.end());
	}
	SDL_QueueAudio(m_device, m_frames.data(), static_cast<Uint32>(m_frames.size() * 2));
}

} /* namespace odin */
