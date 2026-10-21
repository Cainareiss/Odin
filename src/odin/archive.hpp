/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_ARCHIVE_HPP
#define ODIN_ARCHIVE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace odin {

/* An archive of files, zip (miniz) or 7z (the LZMA SDK), told by its first
 * bytes whatever its name says: both kinds are found under the other's
 * name. It lists the files it holds, with their size and CRC32, and reads
 * one whole, its CRC checked. */
class Archive {
public:
	struct Entry {
		std::string name;
		uint64_t size;
		uint32_t crc;
		bool crc_known;
	};

	/* Nothing when there is no file there, or it is no archive */
	static std::unique_ptr<Archive> open(const std::string &path);
	virtual ~Archive() = default;

	const std::vector<Entry> &entries() const { return m_entries; }
	/* Nothing when it cannot be read whole or its CRC is not the one the
	 * archive tells */
	virtual std::optional<std::vector<uint8_t>> read(size_t entry) = 0;

protected:
	Archive() = default;

	std::vector<Entry> m_entries;
};

} /* namespace odin */

#endif /* ODIN_ARCHIVE_HPP */
