/* odin-games.json (Odin's game list: names, sizes, CRCs, cabinets, the keys
 * and the default EEPROM images) embedded in odin_core.dll with .incbin, so
 * that no list or CRC table ships beside the DLL. The file is found through
 * -Wa,-I,<folder of odin-games.json> (CMakeLists.txt). GPL-3.0-or-later. */

	.section .rdata, "dr"
	.global odin_games_json
	.balign 16
odin_games_json:
	.incbin "odin-games.json"
	.global odin_games_json_end
odin_games_json_end:
	.byte 0
