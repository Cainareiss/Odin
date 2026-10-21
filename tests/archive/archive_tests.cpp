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

/* Unit tests for the archives: a zip and a 7z, each under its own name and
 * the other's, list their files with the size and CRC32 they hold and read
 * them back whole; a damaged one is refused, as is a file that is no
 * archive or none at all.
 *
 * The objects of miniz and the LZMA SDK are those make builds.
 * Build: g++ -std=c++20 -O2 -flto=auto -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src -isystem src/miniz -isystem src/lzma tests/archive/archive_tests.cpp src/odin/archive.cpp \
 *            src/miniz/miniz.o src/lzma/7zAlloc.o src/lzma/7zArcIn.o src/lzma/7zBuf.o \
 *            src/lzma/7zBuf2.o src/lzma/7zCrc.o src/lzma/7zCrcOpt.o src/lzma/7zDec.o src/lzma/7zFile.o \
 *            src/lzma/7zStream.o src/lzma/Bcj2.o src/lzma/Bra.o src/lzma/Bra86.o src/lzma/BraIA64.o \
 *            src/lzma/CpuArch.o src/lzma/Delta.o src/lzma/Lzma2Dec.o src/lzma/LzmaDec.o \
 *            src/lzma/Ppmd7.o src/lzma/Ppmd7Dec.o -o archive_tests
 * It writes its files in the folder given as its argument, or the current
 * one. Exit status is 0 when every check passes. */

#include "odin/archive.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <miniz.h>

namespace {

using odin::Archive;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void write_file(const std::string &path, const std::vector<uint8_t> &bytes)
{
	std::FILE *const file = std::fopen(path.c_str(), "wb");

	if (file) {
		std::fwrite(bytes.data(), 1, bytes.size(), file);
		std::fclose(file);
	}
}

std::vector<uint8_t> read_file(const std::string &path)
{
	std::error_code error;
	std::vector<uint8_t> bytes(static_cast<size_t>(std::filesystem::file_size(path, error)));
	std::FILE *const file = std::fopen(path.c_str(), "rb");

	if (file) {
		if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
			bytes.clear();
		std::fclose(file);
	}
	return bytes;
}

/* The two files of both archives */
std::vector<uint8_t> chip()
{
	std::vector<uint8_t> bytes(5000);

	for (size_t i = 0; i < bytes.size(); i++)
		bytes[i] = static_cast<uint8_t>(i * 7 + 3);
	return bytes;
}

const std::vector<uint8_t> NOTE = { 'H', 'i', 'k', 'a', 'r', 'u', '\n' };
constexpr uint32_t CHIP_CRC = 0x1ABD04D4, NOTE_CRC = 0x4DE3B45B;

/* chip.bin and note.txt as `7z a -t7z -mx=9` packs them */
const std::vector<uint8_t> SEVEN_ZIP = {
	0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c, 0x00, 0x04, 0x8b, 0xfd, 0x16, 0xe3,
	0x87, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x04, 0xcc, 0x37, 0xbd, 0xe0, 0x13, 0x8e, 0x01,
	0x14, 0x5d, 0x00, 0x01, 0x82, 0xe0, 0x56, 0x89, 0xc2, 0xf1, 0xf0, 0xe5,
	0xdf, 0xf5, 0xe1, 0xd0, 0x31, 0xae, 0x11, 0x00, 0x5a, 0x08, 0x8a, 0x0e,
	0x25, 0x1b, 0xf1, 0x5e, 0x0e, 0x99, 0x56, 0x73, 0x92, 0x16, 0xe5, 0x5d,
	0xc5, 0x02, 0x56, 0xa1, 0xa0, 0x01, 0x75, 0xbf, 0x0a, 0xf7, 0x39, 0xca,
	0x31, 0xcf, 0xee, 0xb4, 0x90, 0xdd, 0x60, 0xd7, 0xd6, 0x99, 0x78, 0xe0,
	0x0e, 0x01, 0xc6, 0xae, 0x95, 0xd4, 0x6f, 0xeb, 0x3c, 0x70, 0x99, 0x8f,
	0xe6, 0x7c, 0xf8, 0x04, 0x3c, 0xf7, 0x01, 0x32, 0xf5, 0xd7, 0x4c, 0xe8,
	0x62, 0x21, 0x64, 0x3c, 0x9b, 0x63, 0xa9, 0x15, 0xca, 0xeb, 0x12, 0x33,
	0x35, 0x8e, 0x65, 0x45, 0xad, 0xd6, 0xef, 0x02, 0x17, 0x75, 0xac, 0x52,
	0x8f, 0xcd, 0x17, 0x57, 0xd1, 0x8a, 0x38, 0x71, 0x59, 0xaa, 0xd1, 0x95,
	0x74, 0x63, 0x4c, 0x72, 0x7f, 0x8c, 0x52, 0xbc, 0x00, 0x45, 0x00, 0xb2,
	0xaf, 0xdd, 0xaa, 0x4f, 0xc0, 0x7f, 0x95, 0xff, 0x78, 0xcb, 0xf9, 0xb1,
	0x0f, 0x95, 0x27, 0x3c, 0x4f, 0x21, 0x8b, 0xc6, 0xbd, 0x95, 0x41, 0x4e,
	0x97, 0x45, 0x34, 0xde, 0xdd, 0x4a, 0x08, 0xe8, 0xc7, 0x0f, 0x84, 0x7c,
	0x33, 0xf0, 0x13, 0x69, 0xa4, 0x80, 0x64, 0xb3, 0xfb, 0x73, 0x41, 0x6b,
	0x11, 0xff, 0xdc, 0x97, 0x95, 0x3f, 0xc2, 0x34, 0x98, 0x79, 0xa7, 0xa1,
	0xae, 0xab, 0xfc, 0x34, 0xcb, 0xcd, 0xdf, 0xee, 0x48, 0x13, 0xb8, 0x2e,
	0xae, 0xae, 0x39, 0xed, 0xff, 0xc5, 0xc0, 0x8f, 0x08, 0xc8, 0x90, 0x77,
	0x30, 0x83, 0x27, 0xeb, 0x0a, 0x34, 0xc7, 0x5b, 0x5a, 0x2a, 0xf4, 0x3b,
	0x2b, 0xeb, 0x87, 0x57, 0x73, 0x33, 0xff, 0xec, 0xbe, 0xd4, 0x62, 0xae,
	0xfc, 0xc5, 0xf9, 0xfc, 0xbc, 0x87, 0x6a, 0xd3, 0xab, 0x4e, 0x33, 0x8c,
	0x22, 0xe4, 0xe2, 0x4f, 0x11, 0xa7, 0xf4, 0xa6, 0xdc, 0xeb, 0xbd, 0xeb,
	0xcb, 0x1d, 0xe5, 0xfa, 0xa8, 0x6a, 0x60, 0x53, 0xca, 0xf7, 0x17, 0x81,
	0x66, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x81, 0x33, 0x07, 0xae, 0x0f, 0xd5,
	0x38, 0x70, 0xa8, 0x39, 0xc6, 0x66, 0xda, 0x8d, 0xf3, 0xed, 0xd0, 0x68,
	0x97, 0x10, 0xf1, 0x70, 0x97, 0xb3, 0x22, 0xf9, 0x57, 0x47, 0x2e, 0x49,
	0x3a, 0xb2, 0xe0, 0x29, 0xc1, 0x31, 0x73, 0x54, 0x38, 0xfe, 0x01, 0xaa,
	0x6c, 0x15, 0x6f, 0xb8, 0x2e, 0x5c, 0x9e, 0xf5, 0x13, 0xa8, 0x0e, 0x85,
	0xc0, 0x12, 0x00, 0xe5, 0xda, 0x96, 0x6e, 0xf1, 0x9f, 0xef, 0x64, 0xc9,
	0x6b, 0xd5, 0x15, 0xc8, 0xfb, 0xb0, 0x15, 0x29, 0xe4, 0x34, 0xd2, 0x28,
	0xf8, 0x80, 0xf1, 0xf9, 0xce, 0x93, 0x83, 0x90, 0x2b, 0x1a, 0xa1, 0xc2,
	0xa1, 0x97, 0xe4, 0xcf, 0x53, 0xc7, 0x03, 0x8b, 0x52, 0x86, 0xc1, 0xe5,
	0x06, 0x00, 0x00, 0x17, 0x06, 0x81, 0x1c, 0x01, 0x09, 0x6b, 0x00, 0x07,
	0x0b, 0x01, 0x00, 0x01, 0x23, 0x03, 0x01, 0x01, 0x05, 0x5d, 0x00, 0x10,
	0x00, 0x00, 0x0c, 0x76, 0x0a, 0x01, 0xd9, 0x2a, 0xab, 0x45, 0x00, 0x00
};

void test_listing(Archive &archive, const char *what)
{
	const auto &entries = archive.entries();
	std::string label = std::string(what) + ": ";

	check(entries.size() == 2, (label + "two files").c_str());
	for (size_t i = 0; i < entries.size(); i++) {
		const bool is_chip = entries[i].name == "chip.bin", is_note = entries[i].name == "note.txt";

		check(is_chip || is_note, (label + "the names").c_str());
		check(entries[i].crc_known, (label + "the CRC is told").c_str());
		check(entries[i].size == (is_chip ? 5000 : NOTE.size()), (label + "the sizes").c_str());
		check(entries[i].crc == (is_chip ? CHIP_CRC : NOTE_CRC), (label + "the CRCs").c_str());

		const auto bytes = archive.read(i);
		check(bytes && *bytes == (is_chip ? chip() : NOTE), (label + "read back whole").c_str());
	}
}

void test_kinds(const std::string &dir)
{
	const std::string zip = dir + "/archive-test.zip", seven = dir + "/archive-test.7z";
	const std::vector<uint8_t> bytes = chip();
	std::error_code error;

	std::filesystem::remove(zip, error);
	check(mz_zip_add_mem_to_archive_file_in_place(zip.c_str(), "chip.bin", bytes.data(), bytes.size(), nullptr, 0,
	                                              MZ_BEST_COMPRESSION),
	      "a zip written");
	check(mz_zip_add_mem_to_archive_file_in_place(zip.c_str(), "note.txt", NOTE.data(), NOTE.size(), nullptr, 0,
	                                              MZ_BEST_COMPRESSION),
	      "a second file in it");
	check(mz_crc32(MZ_CRC32_INIT, bytes.data(), bytes.size()) == CHIP_CRC, "the CRC of the chip is the one of 7z");
	write_file(seven, SEVEN_ZIP);

	if (auto archive = Archive::open(zip))
		test_listing(*archive, "zip");
	else
		check(false, "the zip opens");
	if (auto archive = Archive::open(seven))
		test_listing(*archive, "7z");
	else
		check(false, "the 7z opens");

	/* Told by what they start with */
	const std::string zip_as_7z = dir + "/archive-test-zip.7z", seven_as_zip = dir + "/archive-test-7z.zip";

	write_file(zip_as_7z, read_file(zip));
	write_file(seven_as_zip, SEVEN_ZIP);
	if (auto archive = Archive::open(zip_as_7z))
		test_listing(*archive, "a zip named .7z");
	else
		check(false, "a zip named .7z opens");
	if (auto archive = Archive::open(seven_as_zip))
		test_listing(*archive, "a 7z named .zip");
	else
		check(false, "a 7z named .zip opens");

	for (const std::string &path : { zip, seven, zip_as_7z, seven_as_zip })
		std::filesystem::remove(path, error);
}

void test_refused(const std::string &dir)
{
	const std::string text = dir + "/archive-test.txt", zip = dir + "/archive-test-bad.zip";
	const std::string seven = dir + "/archive-test-bad.7z";
	const std::vector<uint8_t> bytes = chip();
	std::error_code error;

	check(!Archive::open(dir + "/archive-test-none.zip"), "no file, no archive");
	write_file(text, NOTE);
	check(!Archive::open(text), "a text is no archive");

	/* The data of the chip changed under its header: its CRC is wrong */
	std::filesystem::remove(zip, error);
	mz_zip_add_mem_to_archive_file_in_place(zip.c_str(), "chip.bin", bytes.data(), bytes.size(), nullptr, 0,
	                                        MZ_NO_COMPRESSION);
	std::vector<uint8_t> damaged = read_file(zip);
	for (size_t i = 0; i + 4 < damaged.size(); i++)
		if (damaged[i] == bytes[100] && damaged[i + 1] == bytes[101] && damaged[i + 2] == bytes[102]) {
			damaged[i] ^= 0xFF;
			break;
		}
	write_file(zip, damaged);
	if (auto archive = Archive::open(zip))
		check(archive->entries().size() == 1 && !archive->read(0), "a damaged zip: listed, not read");
	else
		check(false, "a damaged zip still opens");

	/* A byte of the packed data of the 7z changed */
	std::vector<uint8_t> packed = SEVEN_ZIP;
	packed[40] ^= 0xFF;
	write_file(seven, packed);
	if (auto archive = Archive::open(seven))
		for (size_t i = 0; i < archive->entries().size(); i++)
			check(!archive->read(i), "a damaged 7z: not read");

	for (const std::string &path : { text, zip, seven })
		std::filesystem::remove(path, error);
}

} /* namespace */

int main(int argc, char **argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";

	test_kinds(dir);
	test_refused(dir);

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
