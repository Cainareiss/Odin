/*
 * odin::io for the Xbox DLL (M1): the host's functions when odin_init gave
 * them (OdinIo), else the Win32 functions of the app partition
 * (CreateFile2FromAppW and the other *FromApp calls: the console's broker
 * only accepts those). GPL-3.0-or-later, as Odin.
 *
 * A call that takes 40 ms or more is logged with its path, as the Savage Wind
 * host does (swanui_compat.c:105-125): on the console the broker is slow and
 * the log is how that is measured.
 */

#include "odin/common/io.hpp"

#include <windows.h>
#include <fcntl.h>
#include <io.h>

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/* The app partition's file functions: mingw's headers declare them only for
 * the desktop family. They are in libwindowsapp.a (checked: nm). The extended
 * parameters of CreateFile2 are passed as null, so they are void* here. */
extern "C" {
WINBASEAPI HANDLE WINAPI CreateFile2FromAppW(LPCWSTR file, DWORD access, DWORD share, DWORD disposition, void *extended);
WINBASEAPI BOOL WINAPI GetFileAttributesExFromAppW(LPCWSTR file, GET_FILEEX_INFO_LEVELS level, LPVOID data);
WINBASEAPI HANDLE WINAPI FindFirstFileExFromAppW(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data,
                                                 FINDEX_SEARCH_OPS search, LPVOID filter, DWORD flags);
WINBASEAPI BOOL WINAPI CreateDirectoryFromAppW(LPCWSTR path, LPSECURITY_ATTRIBUTES security);
WINBASEAPI BOOL WINAPI ReplaceFileFromAppW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags,
                                           LPVOID exclude, LPVOID reserved);
WINBASEAPI BOOL WINAPI MoveFileFromAppW(LPCWSTR from, LPCWSTR to);
WINBASEAPI BOOL WINAPI RemoveDirectoryFromAppW(LPCWSTR path);
WINBASEAPI BOOL WINAPI DeleteFileFromAppW(LPCWSTR path);
}

namespace odin::io {

namespace {

constexpr long long SLOW_MS = 40;

std::mutex g_mutex; /* the listing cache, and the state of the log */
Host g_host;
bool g_hosted = false;
void (*g_log)(const char *text) = nullptr;
int g_cache_depth = 0;
std::map<std::string, std::vector<Entry>> g_cache; /* by key_of(folder) */

/* The folder key of the cache: lower case, forward slashes, no trailing one */
std::string
key_of(const std::string &folder)
{
	std::string key;

	for (char c : folder) {
		if (c == '\\')
			c = '/';
		key += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	}
	while (key.size() > 1 && key.back() == '/')
		key.pop_back();
	return key;
}

/* folder and name of a path: the last separator splits them */
std::pair<std::string, std::string>
split(const std::string &path)
{
	const size_t at = path.find_last_of("/\\");

	if (at == std::string::npos)
		return { std::string(), path };
	return { path.substr(0, at), path.substr(at + 1) };
}

bool
same_name(const std::string &a, const std::string &b)
{
	return key_of(a) == key_of(b);
}

/* Times one call and logs the slow ones */
class Timer {
public:
	Timer(const char *what, const std::string &path) : m_what(what), m_path(path), m_start(std::chrono::steady_clock::now())
	{
	}
	~Timer()
	{
		const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		                         std::chrono::steady_clock::now() - m_start).count();
		void (*log)(const char *) = g_log;

		if (ms >= SLOW_MS && log) {
			char text[600];

			std::snprintf(text, sizeof(text), "io: %s '%s' took %lld ms", m_what, m_path.c_str(), ms);
			log(text);
		}
	}
	Timer(const Timer &) = delete;
	Timer &operator=(const Timer &) = delete;

private:
	const char *m_what;
	std::string m_path;
	std::chrono::steady_clock::time_point m_start;
};

/* UTF-8 to the wide form the *FromApp calls take: forward slashes become
 * backslashes, and a path of 240 characters or more gets the \\?\ long form */
std::wstring
wide_of(const std::string &utf8)
{
	const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring out(count > 0 ? static_cast<size_t>(count) : 0, L'\0');

	if (count > 0)
		MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), count);
	for (wchar_t &c : out)
		if (c == L'/')
			c = L'\\';
	if (out.size() >= 240 && out.compare(0, 4, L"\\\\?\\") != 0) {
		if (out.size() > 2 && out[1] == L':')
			out = L"\\\\?\\" + out;
		else if (out.compare(0, 2, L"\\\\") == 0)
			out = L"\\\\?\\UNC\\" + out.substr(2);
	}
	return out;
}

std::string
utf8_of(const std::wstring &wide)
{
	const int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
	                                      nullptr);
	std::string out(count > 0 ? static_cast<size_t>(count) : 0, '\0');

	if (count > 0)
		WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), count, nullptr,
		                    nullptr);
	return out;
}

/* ---- The Win32 functions of the app partition --------------------------- */

/* absent is not an error: 0 flags, 0 size */
int
win_stat(const std::string &path, unsigned &flags, uint64_t &size)
{
	WIN32_FILE_ATTRIBUTE_DATA data{};

	flags = 0;
	size = 0;
	if (!GetFileAttributesExFromAppW(wide_of(path).c_str(), GetFileExInfoStandard, &data))
		return 0;
	flags = EXISTS;
	if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		flags |= DIR;
	} else {
		size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	}
	return 0;
}

int
win_open(const std::string &path, unsigned access, void **handle)
{
	DWORD desired = 0, creation = 0;

	if (access & READ) {
		desired = GENERIC_READ;
		creation = OPEN_EXISTING;
	} else if (access & WRITE) {
		desired = GENERIC_WRITE;
		creation = CREATE_ALWAYS;
	} else if (access & APPEND) {
		desired = GENERIC_WRITE;
		creation = OPEN_ALWAYS;
	} else {
		return -1;
	}
	const HANDLE found = CreateFile2FromAppW(wide_of(path).c_str(), desired,
	                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, creation, nullptr);

	if (found == INVALID_HANDLE_VALUE)
		return -1;
	*handle = found;
	return 0;
}

int
win_list(const std::string &dir, void *sink,
         void (*entry)(void *sink, const char *name, unsigned is_dir, uint64_t size))
{
	WIN32_FIND_DATAW data{};
	const std::wstring pattern = wide_of(dir) + L"\\*";
	const HANDLE find = FindFirstFileExFromAppW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
	                                            FIND_FIRST_EX_LARGE_FETCH);

	if (find == INVALID_HANDLE_VALUE)
		return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
	do {
		const std::wstring name = data.cFileName;

		if (name == L"." || name == L"..")
			continue;
		const bool is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		const uint64_t size = is_dir ? 0 : (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
		const std::string utf8 = utf8_of(name);

		entry(sink, utf8.c_str(), is_dir ? 1u : 0u, size);
	} while (FindNextFileW(find, &data));
	const bool finished = GetLastError() == ERROR_NO_MORE_FILES;

	FindClose(find);
	return finished ? 0 : -1;
}

int
win_mkdir(const std::string &path)
{
	if (CreateDirectoryFromAppW(wide_of(path).c_str(), nullptr))
		return 0;
	return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}

int
win_rename_replace(const std::string &from, const std::string &to)
{
	unsigned flags = 0;
	uint64_t size = 0;

	win_stat(to, flags, size);
	if (flags & EXISTS)
		return ReplaceFileFromAppW(wide_of(to).c_str(), wide_of(from).c_str(), nullptr, 0, nullptr, nullptr) ? 0 : -1;
	return MoveFileFromAppW(wide_of(from).c_str(), wide_of(to).c_str()) ? 0 : -1;
}

int
win_remove(const std::string &path)
{
	unsigned flags = 0;
	uint64_t size = 0;

	win_stat(path, flags, size);
	if (flags & DIR)
		return RemoveDirectoryFromAppW(wide_of(path).c_str()) ? 0 : -1;
	return DeleteFileFromAppW(wide_of(path).c_str()) ? 0 : -1;
}

/* ---- The two backends: the host's, else the app partition --------------- */

int
raw_stat(const std::string &path, unsigned &flags, uint64_t &size)
{
	Timer timer("stat", path);

	flags = 0;
	size = 0;
	if (g_hosted) {
		unsigned host_flags = 0;
		uint64_t host_size = 0;

		if (g_host.stat(g_host.ctx, path.c_str(), &host_flags, &host_size) != 0)
			return 0; /* a broker refusal reads as absent */
		flags = host_flags;
		size = host_size;
		return 0;
	}
	return win_stat(path, flags, size);
}

void *
raw_open(const std::string &path, unsigned access)
{
	Timer timer("open", path);
	void *handle = nullptr;

	if (g_hosted) {
		if (g_host.open(g_host.ctx, path.c_str(), access, &handle) != 0)
			return nullptr;
		return handle;
	}
	return win_open(path, access, &handle) == 0 ? handle : nullptr;
}

struct Collect {
	std::vector<Entry> *out;
};

void
collect(void *sink, const char *name, unsigned is_dir, uint64_t size)
{
	static_cast<Collect *>(sink)->out->push_back(Entry{ name, is_dir != 0, is_dir ? 0 : size });
}

bool
raw_list(const std::string &dir, std::vector<Entry> &out)
{
	Timer timer("list", dir);
	Collect sink{ &out };

	if (g_hosted)
		return g_host.list(g_host.ctx, dir.c_str(), &sink, collect) == 0;
	return win_list(dir, &sink, collect) == 0;
}

bool
raw_mkdir(const std::string &path)
{
	Timer timer("mkdir", path);

	if (g_hosted)
		return g_host.mkdir(g_host.ctx, path.c_str()) == 0;
	return win_mkdir(path) == 0;
}

bool
raw_rename_replace(const std::string &from, const std::string &to)
{
	Timer timer("rename", from);

	if (g_hosted)
		return g_host.rename_replace(g_host.ctx, from.c_str(), to.c_str()) == 0;
	return win_rename_replace(from, to) == 0;
}

bool
raw_remove(const std::string &path)
{
	Timer timer("remove", path);

	if (g_hosted)
		return g_host.remove(g_host.ctx, path.c_str()) == 0;
	return win_remove(path) == 0;
}

/* A folder whose entries change is listed again: its cached listing goes */
void
forget(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_mutex);

	g_cache.erase(key_of(folder));
}

/* Stat of a path: from the listing of its folder while a cache is on */
bool
stat_of(const std::string &path, unsigned &flags, uint64_t &size)
{
	const auto [folder, name] = split(path);
	bool cached = false;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		cached = g_cache_depth > 0 && !folder.empty();
	}
	if (!cached)
		return raw_stat(path, flags, size) == 0;

	std::vector<Entry> entries;
	bool listed = false;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (const auto found = g_cache.find(key_of(folder)); found != g_cache.end()) {
			entries = found->second;
			listed = true;
		}
	}
	if (!listed) {
		std::vector<Entry> fresh;

		if (!raw_list(folder, fresh))
			return raw_stat(path, flags, size) == 0;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_cache[key_of(folder)] = fresh;
		}
		entries = std::move(fresh);
	}
	flags = 0;
	size = 0;
	for (const Entry &entry : entries)
		if (same_name(entry.name, name)) {
			flags = EXISTS | (entry.dir ? DIR : 0u);
			size = entry.dir ? 0 : entry.size;
			break;
		}
	return true;
}

} /* namespace */

/* ---- Public --------------------------------------------------------------- */

void
set_host(const Host &host)
{
	std::lock_guard<std::mutex> lock(g_mutex);

	g_host = host;
	g_hosted = host.open && host.stat && host.list && host.mkdir && host.rename_replace && host.remove;
	if (!g_hosted && (host.open || host.stat || host.list || host.mkdir || host.rename_replace || host.remove)) {
		/* a partial host is refused whole: the app partition is used */
		g_host = Host{};
	}
}

void
set_log(void (*log)(const char *text))
{
	std::lock_guard<std::mutex> lock(g_mutex);

	g_log = log;
}

std::FILE *
fopen(const std::string &path, const char *mode)
{
	if (!mode)
		return nullptr;
	const bool binary = std::strchr(mode, 'b') != nullptr;
	const bool plus = std::strchr(mode, '+') != nullptr;
	unsigned access = 0;
	const char *cmode = nullptr;

	if (plus)
		return nullptr;
	switch (mode[0]) {
	case 'r':
		access = READ;
		cmode = binary ? "rb" : "r";
		break;
	case 'w':
		access = WRITE;
		cmode = binary ? "wb" : "w";
		break;
	case 'a':
		access = APPEND;
		cmode = binary ? "ab" : "a";
		break;
	default:
		return nullptr;
	}
	if (!binary)
		access |= TEXT;

	void *const handle = raw_open(path, access);

	if (!handle)
		return nullptr;
	const int flags = ((access & READ) ? _O_RDONLY : _O_WRONLY) | ((access & TEXT) ? _O_TEXT : _O_BINARY);
	const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), flags);

	if (fd < 0) {
		CloseHandle(handle);
		return nullptr;
	}
	std::FILE *const file = _fdopen(fd, cmode);

	if (!file) {
		_close(fd); /* closes the handle */
		return nullptr;
	}
	if (access & APPEND)
		std::fseek(file, 0, SEEK_END);
	return file;
}

std::optional<uint64_t>
file_size(const std::string &path)
{
	unsigned flags = 0;
	uint64_t size = 0;

	if (!stat_of(path, flags, size) || !(flags & EXISTS) || (flags & DIR))
		return std::nullopt;
	return size;
}

bool
exists(const std::string &path)
{
	unsigned flags = 0;
	uint64_t size = 0;

	return stat_of(path, flags, size) && (flags & EXISTS);
}

bool
is_dir(const std::string &path)
{
	unsigned flags = 0;
	uint64_t size = 0;

	return stat_of(path, flags, size) && (flags & EXISTS) && (flags & DIR);
}

bool
list(const std::string &dir, std::vector<Entry> &out)
{
	const std::string key = key_of(dir);

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_cache_depth > 0)
			if (const auto found = g_cache.find(key); found != g_cache.end()) {
				out = found->second;
				return true;
			}
	}
	std::vector<Entry> fresh;

	if (!raw_list(dir, fresh))
		return false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_cache_depth > 0)
			g_cache[key] = fresh;
	}
	out = std::move(fresh);
	return true;
}

bool
mkdir_p(const std::string &dir)
{
	if (dir.empty())
		return false;
	if (is_dir(dir))
		return true;
	const std::string parent = split(dir).first;

	if (!parent.empty() && !is_dir(parent) && !mkdir_p(parent))
		return false;
	if (!parent.empty())
		forget(parent);
	return raw_mkdir(dir) || is_dir(dir);
}

bool
rename_replace(const std::string &from, const std::string &to)
{
	forget(split(from).first);
	forget(split(to).first);
	return raw_rename_replace(from, to);
}

bool
remove(const std::string &path)
{
	forget(split(path).first);
	return raw_remove(path);
}

std::optional<std::vector<uint8_t>>
read_file(const std::string &path)
{
	std::FILE *const file = fopen(path, "rb");
	std::vector<uint8_t> bytes;
	uint8_t chunk[65536];
	size_t got = 0;

	if (!file)
		return std::nullopt;
	while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
		bytes.insert(bytes.end(), chunk, chunk + got);
	const bool failed = std::ferror(file) != 0;

	std::fclose(file);
	if (failed)
		return std::nullopt;
	return bytes;
}

bool
write_kept(const std::string &path, const void *data, size_t size)
{
	const std::string fresh = path + ".new";
	const std::string folder = split(path).first;

	if (!folder.empty() && !mkdir_p(folder))
		return false;
	std::FILE *const file = fopen(fresh, "wb");
	if (!file)
		return false;
	bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;

	if (std::fclose(file) != 0)
		ok = false;
	if (ok)
		ok = rename_replace(fresh, path);
	if (!ok)
		raw_remove(fresh);
	return ok;
}

ListingCache::ListingCache()
{
	std::lock_guard<std::mutex> lock(g_mutex);

	++g_cache_depth;
}

ListingCache::~ListingCache()
{
	std::lock_guard<std::mutex> lock(g_mutex);

	if (--g_cache_depth == 0)
		g_cache.clear();
}

} /* namespace odin::io */
