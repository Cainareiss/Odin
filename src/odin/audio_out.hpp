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

#ifndef ODIN_AUDIO_OUT_HPP
#define ODIN_AUDIO_OUT_HPP

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace odin {

/* The sound of the two boards to the speakers of the host, as the user
 * chooses: the main board, the optional one, both on two channels, or the
 * four channels of the cabinet. */
class AudioOut {
public:
	enum class Mode : uint8_t { MAIN, OPTION, MIX, QUAD };

	/* Left and right pairs of each board into the frames of the mode */
	static void interleave(Mode mode, std::span<const int16_t> main, std::span<const int16_t> option,
	                       std::vector<int16_t> &out)
	{
		const auto at = [](std::span<const int16_t> s, size_t i) { return i < s.size() ? s[i] : int16_t{ 0 }; };
		const auto sum = [](int16_t a, int16_t b) {
			return static_cast<int16_t>(std::clamp(a + b, -0x8000, 0x7FFF));
		};

		out.clear();
		for (size_t i = 0; i + 1 < main.size(); i += 2) {
			const int16_t ml = main[i], mr = main[i + 1], ol = at(option, i), orr = at(option, i + 1);

			switch (mode) {
			case Mode::MAIN:
				out.insert(out.end(), { ml, mr });
				break;
			case Mode::OPTION:
				out.insert(out.end(), { ol, orr });
				break;
			case Mode::MIX:
				out.insert(out.end(), { sum(ml, ol), sum(mr, orr) });
				break;
			case Mode::QUAD:
				out.insert(out.end(), { ml, mr, ol, orr });
				break;
			}
		}
	}

	/* nullptr when the host gives no sound device */
	static std::unique_ptr<AudioOut> make(Mode mode);
	~AudioOut();
	AudioOut(const AudioOut &) = delete;
	AudioOut &operator=(const AudioOut &) = delete;

	/* The samples of a frame of each board. The board makes its frames
	 * a little longer than the 1/60 s they are shown for: between 30 and
	 * 120 ms are kept ahead, a sample more or less a frame. */
	void play(std::span<const int16_t> main, std::span<const int16_t> option);

private:
	AudioOut(uint32_t device, Mode mode) : m_device(device), m_mode(mode) {}

	uint32_t m_device;
	Mode m_mode;
	std::vector<int16_t> m_frames;
};

} /* namespace odin */

#endif /* ODIN_AUDIO_OUT_HPP */
