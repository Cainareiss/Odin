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

#include "odin/archive.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>

/* As they come (src/miniz, src/lzma): system headers for our warnings */
#include <miniz.h>
#include <7z.h>
#include <7zAlloc.h>
#include <7zCrc.h>
#include <7zFile.h>

namespace odin {

namespace {

class Zip final : public Archive {
public:
	explicit Zip(const std::string &path) : m_zip{}
	{
		m_open = mz_zip_reader_init_file(&m_zip, path.c_str(), 0);
		if (!m_open)
			return;
		for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&m_zip); i++) {
			mz_zip_archive_file_stat stat;

			if (!mz_zip_reader_file_stat(&m_zip, i, &stat) || stat.m_is_directory)
				continue;
			m_entries.push_back({ stat.m_filename, stat.m_uncomp_size, stat.m_crc32, true });
			m_index.push_back(i);
		}
	}

	~Zip() override
	{
		if (m_open)
			mz_zip_reader_end(&m_zip);
	}

	bool opened() const { return m_open; }

	/* miniz checks the CRC itself */
	std::optional<std::vector<uint8_t>> read(size_t entry) override
	{
		std::vector<uint8_t> bytes(static_cast<size_t>(m_entries[entry].size));

		if (!mz_zip_reader_extract_to_mem(&m_zip, m_index[entry], bytes.data(), bytes.size(), 0))
			return std::nullopt;
		return bytes;
	}

private:
	mz_zip_archive m_zip;
	bool m_open;
	std::vector<mz_uint> m_index;
};

const ISzAlloc ALLOC = { SzAlloc, SzFree };
const ISzAlloc ALLOC_TEMP = { SzAllocTemp, SzFreeTemp };
/* What the LZMA SDK reads ahead of the archive at a time, as its example */
constexpr size_t LOOK_AHEAD = size_t{1} << 18;

class SevenZip final : public Archive {
public:
	explicit SevenZip(const std::string &path) : m_look_buffer(LOOK_AHEAD)
	{
		static std::once_flag crc_table;

		std::call_once(crc_table, [] { CrcGenerateTable(); });
		SzArEx_Init(&m_db);
		m_file_open = InFile_Open(&m_file.file, path.c_str()) == 0;
		if (!m_file_open)
			return;
		FileInStream_CreateVTable(&m_file);
		LookToRead2_CreateVTable(&m_look, False);
		m_look.realStream = &m_file.vt;
		m_look.buf = m_look_buffer.data();
		m_look.bufSize = m_look_buffer.size();
		LookToRead2_INIT(&m_look);
		m_open = SzArEx_Open(&m_db, &m_look.vt, &ALLOC, &ALLOC_TEMP) == SZ_OK;
		if (!m_open)
			return;
		for (UInt32 i = 0; i < m_db.NumFiles; i++) {
			if (SzArEx_IsDir(&m_db, i))
				continue;

			/* The names are UTF-16; those of the chips are plain ASCII */
			std::vector<UInt16> utf16(SzArEx_GetFileNameUtf16(&m_db, i, nullptr));
			std::string name;

			SzArEx_GetFileNameUtf16(&m_db, i, utf16.data());
			for (const UInt16 c : utf16)
				if (c)
					name += c < 0x80 ? static_cast<char>(c) : '?';
			const bool crc_known = SzBitWithVals_Check(&m_db.CRCs, i);
			m_entries.push_back({ name, SzArEx_GetFileSize(&m_db, i), crc_known ? m_db.CRCs.Vals[i] : 0, crc_known });
			m_index.push_back(i);
		}
	}

	~SevenZip() override
	{
		if (m_block)
			ISzAlloc_Free(&ALLOC, m_block);
		SzArEx_Free(&m_db, &ALLOC);
		if (m_file_open)
			File_Close(&m_file.file);
	}

	bool opened() const { return m_open; }

	/* The block a file is in is unpacked whole, and kept for the next
	 * file of the same block; the LZMA SDK checks the CRC */
	std::optional<std::vector<uint8_t>> read(size_t entry) override
	{
		size_t offset = 0, size = 0;

		if (SzArEx_Extract(&m_db, &m_look.vt, m_index[entry], &m_block_index, &m_block, &m_block_size, &offset, &size,
		                   &ALLOC, &ALLOC_TEMP) != SZ_OK)
			return std::nullopt;
		return std::vector<uint8_t>(m_block + offset, m_block + offset + size);
	}

private:
	CSzArEx m_db;
	CFileInStream m_file{};
	CLookToRead2 m_look{};
	std::vector<Byte> m_look_buffer;
	bool m_file_open = false, m_open = false;
	std::vector<UInt32> m_index;
	UInt32 m_block_index = 0xFFFFFFFF;
	Byte *m_block = nullptr;
	size_t m_block_size = 0;
};

} /* namespace */

std::unique_ptr<Archive>
Archive::open(const std::string &path)
{
	static constexpr std::array<uint8_t, 4> ZIP = { 'P', 'K', 3, 4 }, EMPTY_ZIP = { 'P', 'K', 5, 6 };
	static constexpr std::array<uint8_t, 6> SEVEN_ZIP = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
	std::array<uint8_t, 6> head{};
	std::FILE *const file = std::fopen(path.c_str(), "rb");

	if (!file)
		return nullptr;
	const size_t got = std::fread(head.data(), 1, head.size(), file);
	std::fclose(file);

	if (got >= ZIP.size() && (!std::memcmp(head.data(), ZIP.data(), ZIP.size()) ||
	                          !std::memcmp(head.data(), EMPTY_ZIP.data(), EMPTY_ZIP.size()))) {
		auto zip = std::make_unique<Zip>(path);
		return zip->opened() ? std::move(zip) : nullptr;
	}
	if (got == SEVEN_ZIP.size() && !std::memcmp(head.data(), SEVEN_ZIP.data(), SEVEN_ZIP.size())) {
		auto seven = std::make_unique<SevenZip>(path);
		return seven->opened() ? std::move(seven) : nullptr;
	}
	return nullptr;
}

} /* namespace odin */
