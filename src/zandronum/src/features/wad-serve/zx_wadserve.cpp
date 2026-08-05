// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_wadserve.h for the design and why this is a TCP listener rather than the game socket.
//
// networkheaders.h comes FIRST, before any engine header, and is the engine's own answer to a problem
// worth not rediscovering: on Windows it defines USE_WINDOWS_DWORD before pulling in windows.h, so
// basictypes.h leaves DWORD alone. Including winsock2.h directly instead produces
// "error C2371: 'DWORD': redefinition", which reads like a header ordering mistake and is really a
// missing define. It also supplies SOCKET / INVALID_SOCKET / closesocket on POSIX, so the socket
// calls below have one spelling on every platform.
#include "networkheaders.h"

#ifdef _WIN32
  #define zx_fseek64        _fseeki64
  #define zx_ftell64        _ftelli64
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/time.h>
  #define zx_fseek64        fseeko
  #define zx_ftell64        ftello
#endif

typedef SOCKET zx_socket_t;
#define ZX_INVALID_SOCKET INVALID_SOCKET
#define zx_close_socket   closesocket

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "network.h"
#include "v_text.h"
#include "w_wad.h"

#include "features/wad-serve/zx_wadserve.h"
#include "features/wad-serve/computation/httpreq_compute.h"
#include "features/wad-serve/computation/ratebucket_compute.h"
#include "features/wad-serve/computation/servepolicy_compute.h"

//*****************************************************************************
//	CONSOLE VARIABLES

// [rc4l] fua_ per the naming rule. Odamex, Source and Quake all spell their equivalent
// sv_allowdownload, but theirs mostly means "advertise a redirect somewhere else" -- ours actually
// sends the bytes, so borrowing the name would promise an operator the wrong thing.
CVAR( Bool, sv_fua_download, true, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] 0 means "the same number as the game port", which is what a firewall rule is easiest to
// write for and what the client assumes when the server advertises no port of its own.
CVAR( Int, sv_fua_download_port, 0, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] The global budget, in KB/s, shared by every transfer at once. THIS is the knob that matters:
// see computation/ratebucket_compute.h for why a per-connection cap alone silently multiplies by
// client count. 512 KB/s is about 4 Mbit -- roughly a quarter of a modest home uplink, so the tic
// stream still has room. Operators with real bandwidth raise this one number and nothing else.
CVAR( Int, sv_fua_download_maxrate, 512, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] Ceiling for any single transfer, in KB/s, so one downloader cannot take the entire global
// budget while three others crawl. Above the global cap divided by the slot count on purpose: when
// only one person is downloading they should get the whole pipe.
CVAR( Int, sv_fua_download_rate, 256, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] Concurrent transfers. Beyond this a client is told 503 with a Retry-After and comes back --
// slicing the same budget into twenty streams helps nobody and costs twenty file handles.
CVAR( Int, sv_fua_download_slots, 4, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] Slots one address may hold. Without it a single peer opens `slots` connections and the
// server serves nobody else -- free for them, expensive for everyone else.
CVAR( Int, sv_fua_download_peraddress, 2, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] Ceiling on one file, in MB; 0 means no ceiling. Unlike the client's version of this setting
// it is not a safety bound -- we know exactly what we are sending -- it is for an operator who does
// not want a 2 GB resource pack leaving their uplink one copy at a time.
CVAR( Int, sv_fua_download_maxsize, 0, CVAR_ARCHIVE | CVAR_NOSETBYACS )

// [rc4l] For operators who host their WADs somewhere with real bandwidth and would rather clients
// went there first. Advertised over the launcher protocol; the client orders its mirror list by it.
// Default off, because the case this feature exists for -- a WAD that is on no mirror because it was
// built ten minutes ago -- is exactly the case where the server must be tried first.
CVAR( Bool, sv_fua_download_prefermirrors, false, CVAR_ARCHIVE | CVAR_NOSETBYACS )

namespace
{

//*****************************************************************************
//	TUNING

// Bytes handed to send() at a time. Small enough that the rate limiter stays fine-grained, large
// enough that a 200 MB file is not ten million syscalls.
const long long kChunkBytes = 32 * 1024;

// Header block ceiling and how long a peer may take to produce one. Together with the slot cap these
// are what bound slowloris: a connection that has not asked for anything within the timeout is not
// holding a slot for long.
const size_t kMaxRequestBytes = 8 * 1024;
const int kRequestTimeoutMs = 5000;

// A send that makes no progress for this long is a dead peer holding a slot.
const int kSendTimeoutMs = 30000;

// Rough seconds for one transfer, used only to estimate Retry-After for a queued client.
const int kTypicalTransferSeconds = 60;

//*****************************************************************************
//	SHARED STATE
//
// Everything a worker reads lives here in plain types, snapshotted on the main thread. Workers never
// touch a CVAR, an FString, the wad tables or Printf -- see features/updater/zx_updater.h for the
// crash that rule came from.

struct ServableEntry
{
	zx::ServableFile info;
	std::string path;
};

struct ServeConfig
{
	bool enabled;
	long long globalRateBytes;
	long long connRateBytes;
	int slots;
	int perAddress;
	long long maxFileBytes;

	ServeConfig()
		: enabled(false), globalRateBytes(0), connRateBytes(0), slots(0), perAddress(0),
		  maxFileBytes(0) {}
};

std::mutex g_mutex;
std::vector<std::string> g_log;					// lines for Tick() to Printf
std::vector<ServableEntry> g_servable;
std::vector<std::string> g_servableNames;		// what the table was built from
ServeConfig g_config;

zx::RateBucket g_globalBucket;
int g_activeTotal = 0;
std::map<unsigned long, int> g_activeByAddress;

long long g_bytesServed = 0;
int g_transfersServed = 0;
int g_transfersRefused = 0;

zx_socket_t g_listen = ZX_INVALID_SOCKET;
int g_boundPort = 0;
bool g_stopping = false;
bool g_listenerRunning = false;
bool g_winsockReady = false;

void Say(const std::string &line)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_log.push_back(line);
}

//*****************************************************************************
//	SOCKET HELPERS

long long NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void SetTimeout(zx_socket_t sock, int optname, int ms)
{
#ifdef _WIN32
	DWORD value = static_cast<DWORD>(ms);
	setsockopt(sock, SOL_SOCKET, optname, reinterpret_cast<const char *>(&value), sizeof(value));
#else
	struct timeval value;
	value.tv_sec = ms / 1000;
	value.tv_usec = (ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, optname, reinterpret_cast<const char *>(&value), sizeof(value));
#endif
}

// Send everything or report failure. A short send is normal on a socket under pressure and is not an
// error; treating it as one would truncate files at random under exactly the load this feature adds.
bool SendAll(zx_socket_t sock, const char *data, size_t length)
{
	size_t sent = 0;
	while (sent < length)
	{
		const int n = static_cast<int>(send(sock, data + sent, static_cast<int>(length - sent), 0));
		if (n <= 0)
			return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

bool SendAll(zx_socket_t sock, const std::string &text)
{
	return SendAll(sock, text.c_str(), text.size());
}

// Close without throwing away the response we just wrote.
//
// A plain close() on a socket that still has unread received data is defined to send RST, and the
// peer's TCP then discards whatever is sitting in ITS receive buffer -- including our reply. Every
// short response here is written without having consumed the request that prompted it, so this is
// not a rare race: refusing a client with 503 and closing gave curl a connection reset instead of
// the 503 roughly one time in six, which reads like the listener falling over and is really the
// refusal working perfectly and then being thrown away.
//
// The fix is the orderly shutdown: signal we are done sending, drain whatever the peer still had in
// flight, and only then close.
// `drainMs` bounds how long that drain may take. The accept loop passes a short one -- a refusal is
// answered on that thread, so a silent peer must not be able to hold up everyone else's connect.
void CloseGracefully(zx_socket_t sock, int drainMs)
{
#ifdef _WIN32
	shutdown(sock, SD_SEND);
#else
	shutdown(sock, SHUT_WR);
#endif

	SetTimeout(sock, SO_RCVTIMEO, drainMs);

	// Bounded: a peer that keeps talking after we have answered does not get to hold the slot.
	char scratch[512];
	for (int i = 0; i < 16; ++i)
	{
		if (recv(sock, scratch, sizeof scratch, 0) <= 0)
			break;
	}

	zx_close_socket(sock);
}

std::string StatusText(int status)
{
	switch (status)
	{
	case 200: return "OK";
	case 206: return "Partial Content";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 408: return "Request Timeout";
	case 416: return "Range Not Satisfiable";
	case 431: return "Request Header Fields Too Large";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	default:  return "Error";
	}
}

// A complete short response. `extraHeaders` may be empty; it is inserted verbatim and every caller
// builds it from numbers, never from anything a peer sent.
void SendShortResponse(zx_socket_t sock, int status, const std::string &extraHeaders,
	const std::string &body)
{
	char buffer[512];
	std::snprintf(buffer, sizeof buffer,
		"HTTP/1.1 %d %s\r\n"
		"Content-Length: %u\r\n"
		"Content-Type: text/plain\r\n"
		"Connection: close\r\n",
		status, StatusText(status).c_str(), static_cast<unsigned>(body.size()));

	std::string response = buffer;
	response += extraHeaders;
	response += "\r\n";
	response += body;
	SendAll(sock, response);
}

std::string DescribeAddress(unsigned long address)
{
	char buffer[32];
	const unsigned long host = ntohl(address);
	std::snprintf(buffer, sizeof buffer, "%lu.%lu.%lu.%lu",
		(host >> 24) & 0xff, (host >> 16) & 0xff, (host >> 8) & 0xff, host & 0xff);
	return buffer;
}

//*****************************************************************************
//	THE TRANSFER

// Read until the header block is complete, the peer gives up, or the timeout fires.
zx::HttpParse ReadRequest(zx_socket_t sock, zx::HttpRequest &request)
{
	std::string buffer;
	const long long deadline = NowMs() + kRequestTimeoutMs;

	for (;;)
	{
		const zx::HttpParse state = zx::ParseHttpRequest(buffer, kMaxRequestBytes, request);
		if (state != zx::HttpParse::NeedMore)
			return state;

		if (NowMs() >= deadline)
			return zx::HttpParse::NeedMore;			// caller turns this into a 408

		char chunk[1024];
		const int n = static_cast<int>(recv(sock, chunk, sizeof chunk, 0));
		if (n <= 0)
			return zx::HttpParse::NeedMore;
		buffer.append(chunk, static_cast<size_t>(n));
	}
}

// Stream `length` bytes from `offset` of `path`, paced by the global budget and this connection's
// own cap. Returns bytes actually sent.
long long SendFileBody(zx_socket_t sock, const std::string &path, long long offset, long long length,
	const ServeConfig &config)
{
	FILE *file = std::fopen(path.c_str(), "rb");
	if (file == NULL)
		return 0;

	if (zx_fseek64(file, offset, SEEK_SET) != 0)
	{
		std::fclose(file);
		return 0;
	}

	const zx::RateLimit globalLimit(config.globalRateBytes, 0);
	const zx::RateLimit connLimit(config.connRateBytes, 0);
	zx::RateBucket connBucket;

	std::vector<char> chunk(static_cast<size_t>(kChunkBytes));
	long long remaining = length;
	long long sent = 0;

	while (remaining > 0)
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_stopping)
				break;
		}

		const long long want = (remaining < kChunkBytes) ? remaining : kChunkBytes;

		long long grant = 0;
		int waitMs = 0;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			grant = zx::BucketTakePair(g_globalBucket, globalLimit, connBucket, connLimit, want,
				NowMs());
			if (grant <= 0)
				waitMs = zx::BucketWaitMsPair(g_globalBucket, globalLimit, connBucket, connLimit,
					want);
		}

		if (grant <= 0)
		{
			// Sleeping rather than spinning is the whole point of the budget: the uplink stays free
			// for tic traffic while this thread does nothing at all.
			std::this_thread::sleep_for(std::chrono::milliseconds(waitMs > 0 ? waitMs : 1));
			continue;
		}

		const size_t got = std::fread(&chunk[0], 1, static_cast<size_t>(grant), file);
		if (got == 0)
			break;

		if (!SendAll(sock, &chunk[0], got))
			break;

		sent += static_cast<long long>(got);
		remaining -= static_cast<long long>(got);
	}

	std::fclose(file);
	return sent;
}

void ServeConnection(zx_socket_t sock, unsigned long address)
{
	SetTimeout(sock, SO_RCVTIMEO, kRequestTimeoutMs);
	SetTimeout(sock, SO_SNDTIMEO, kSendTimeoutMs);

	zx::HttpRequest request;
	const zx::HttpParse state = ReadRequest(sock, request);

	if (state != zx::HttpParse::Ok)
	{
		int status = 400;
		if (state == zx::HttpParse::NeedMore)
			status = 408;
		else if (state == zx::HttpParse::TooLarge)
			status = 431;
		else if (state == zx::HttpParse::Unsupported)
			status = 501;

		SendShortResponse(sock, status, "", StatusText(status) + "\n");
		return;
	}

	// Snapshot everything the decision needs, then let go of the lock: classification and the whole
	// transfer run without holding up the main thread or another connection.
	std::vector<ServableEntry> servable;
	ServeConfig config;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		servable = g_servable;
		config = g_config;
	}

	std::vector<zx::ServableFile> catalogue;
	catalogue.reserve(servable.size());
	for (size_t i = 0; i < servable.size(); ++i)
		catalogue.push_back(servable[i].info);

	int index = -1;
	const zx::ServeVerdict verdict = zx::ClassifyServeRequest(catalogue, request.filename,
		config.enabled, config.maxFileBytes, index);

	if (verdict != zx::ServeVerdict::Allowed)
	{
		const int status = zx::ServeVerdictStatus(verdict);
		Say(std::string("wadserve: refused ") + request.filename + " for " + DescribeAddress(address)
			+ " -- " + zx::ServeVerdictReason(verdict));
		SendShortResponse(sock, status, "", std::string(zx::ServeVerdictReason(verdict)) + "\n");
		return;
	}

	const ServableEntry &entry = servable[static_cast<size_t>(index)];

	long long offset = 0;
	long long length = 0;
	if (!zx::ResolveRange(request.range, entry.info.size, offset, length))
	{
		char rangeHeader[96];
		std::snprintf(rangeHeader, sizeof rangeHeader, "Content-Range: bytes */%lld\r\n",
			entry.info.size);
		SendShortResponse(sock, 416, rangeHeader, "range not satisfiable\n");
		return;
	}

	const bool partial = request.range.present && (length != entry.info.size);

	char header[512];
	if (partial)
	{
		std::snprintf(header, sizeof header,
			"HTTP/1.1 206 Partial Content\r\n"
			"Content-Length: %lld\r\n"
			"Content-Range: bytes %lld-%lld/%lld\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Accept-Ranges: bytes\r\n"
			"Connection: close\r\n\r\n",
			length, offset, (offset + length) - 1, entry.info.size);
	}
	else
	{
		std::snprintf(header, sizeof header,
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: %lld\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Accept-Ranges: bytes\r\n"
			"Connection: close\r\n\r\n",
			length);
	}

	if (!SendAll(sock, header, std::strlen(header)))
		return;

	if (request.headOnly)
		return;

	const long long sent = SendFileBody(sock, entry.path, offset, length, config);

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_bytesServed += sent;
		if (sent == length)
			g_transfersServed++;
	}

	if (sent != length)
	{
		Say(std::string("wadserve: ") + DescribeAddress(address) + " disconnected during "
			+ entry.info.name);
	}
}

// One connection, start to finish, on its own thread. Releases its slot however it exits.
void TransferThread(zx_socket_t sock, unsigned long address)
{
	ServeConnection(sock, address);
	CloseGracefully(sock, 500);

	std::lock_guard<std::mutex> lock(g_mutex);
	g_activeTotal--;
	std::map<unsigned long, int>::iterator it = g_activeByAddress.find(address);
	if (it != g_activeByAddress.end())
	{
		it->second--;
		if (it->second <= 0)
			g_activeByAddress.erase(it);
	}
}

void ListenThread()
{
	for (;;)
	{
		sockaddr_in peer;
		std::memset(&peer, 0, sizeof peer);
#ifdef _WIN32
		int peerLength = static_cast<int>(sizeof peer);
#else
		socklen_t peerLength = static_cast<socklen_t>(sizeof peer);
#endif

		const zx_socket_t sock = accept(g_listen, reinterpret_cast<sockaddr *>(&peer), &peerLength);

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_stopping)
			{
				if (sock != ZX_INVALID_SOCKET)
					zx_close_socket(sock);
				g_listenerRunning = false;
				return;
			}
		}

		if (sock == ZX_INVALID_SOCKET)
		{
			// A transient accept failure should not become a spin. Shutdown closes the listener,
			// which lands here too and is caught by the flag check above.
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		const unsigned long address = static_cast<unsigned long>(peer.sin_addr.s_addr);

		bool admitted = false;
		int retryAfter = 0;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			const int fromThisAddress = g_activeByAddress.count(address)
				? g_activeByAddress[address] : 0;

			admitted = zx::ComputeAdmitTransfer(g_activeTotal, g_config.slots, fromThisAddress,
				g_config.perAddress);

			if (admitted)
			{
				g_activeTotal++;
				g_activeByAddress[address]++;
			}
			else
			{
				g_transfersRefused++;
				retryAfter = zx::ComputeRetryAfterSeconds(g_activeTotal, g_config.slots,
					kTypicalTransferSeconds);
			}
		}

		if (!admitted)
		{
			// Answered inline rather than on a thread: spawning one to say "come back later" would
			// undo the cap that produced the answer. The short send timeout bounds the delay this
			// can add to the accept loop.
			SetTimeout(sock, SO_SNDTIMEO, 2000);
			char retryHeader[64];
			std::snprintf(retryHeader, sizeof retryHeader, "Retry-After: %d\r\n", retryAfter);
			SendShortResponse(sock, 503, retryHeader, "all download slots are busy\n");
			CloseGracefully(sock, 200);
			continue;
		}

		std::thread(TransferThread, sock, address).detach();
	}
}

//*****************************************************************************
//	MAIN-THREAD SIDE

long long FileSizeOf(const char *path)
{
	FILE *file = std::fopen(path, "rb");
	if (file == NULL)
		return -1;

	long long size = -1;
	if (zx_fseek64(file, 0, SEEK_END) == 0)
		size = static_cast<long long>(zx_ftell64(file));
	std::fclose(file);
	return size;
}

// The engine's own definition of which loaded file is the IWAD, matching network_InitPWADList: skip
// everything loaded automatically, and the entry at IWAD_FILENUM is the one.
int FindIwadWadnum()
{
	ULONG seen = 0;
	for (ULONG idx = 0; Wads.GetWadName(idx) != NULL; ++idx)
	{
		if (Wads.GetLoadedAutomatically(idx) == false)
		{
			if (seen == static_cast<ULONG>(FWadCollection::IWAD_FILENUM))
				return static_cast<int>(idx);
			seen++;
		}
	}
	return -1;
}

// Has the loaded WAD set changed since the table was built?
//
// Called every frame on the server, so it ALLOCATES NOTHING on the common path -- the answer is
// almost always "no", and building a signature string to discover that would be a heap allocation
// per tic for the lifetime of the process. Compare the names in place instead: a count check, then a
// strcmp each, against the names recorded when the table was last built.
bool WadSetChanged(const std::vector<std::string> &known)
{
	const char *iwad = NETWORK_GetIWAD();
	const TArray<NetworkPWAD> &pwads = NETWORK_GetPWADList();

	if (known.size() != (pwads.Size() + 1))
		return true;

	if (known[0].compare((iwad != NULL) ? iwad : "") != 0)
		return true;

	for (unsigned i = 0; i < pwads.Size(); ++i)
	{
		if (known[i + 1].compare(pwads[i].name.GetChars()) != 0)
			return true;
	}

	return false;
}

// The names the current table was built from, recorded so the check above has something to compare
// against. Built once per wad-set change, not once per frame.
std::vector<std::string> CurrentWadNames()
{
	const char *iwad = NETWORK_GetIWAD();
	const TArray<NetworkPWAD> &pwads = NETWORK_GetPWADList();

	std::vector<std::string> names;
	names.reserve(pwads.Size() + 1);
	names.push_back((iwad != NULL) ? iwad : "");
	for (unsigned i = 0; i < pwads.Size(); ++i)
		names.push_back(pwads[i].name.GetChars());

	return names;
}

void RebuildServableTable()
{
	std::vector<ServableEntry> table;

	const int iwadNum = FindIwadWadnum();
	if (iwadNum >= 0)
	{
		const char *name = Wads.GetWadName(iwadNum);
		const char *path = Wads.GetWadFullName(iwadNum);
		if (name != NULL && path != NULL)
		{
			const long long size = FileSizeOf(path);
			if (size >= 0)
			{
				ServableEntry entry;
				entry.info = zx::ServableFile(name, true, size);
				entry.path = path;
				table.push_back(entry);
			}
		}
	}

	const TArray<NetworkPWAD> &pwads = NETWORK_GetPWADList();
	for (unsigned i = 0; i < pwads.Size(); ++i)
	{
		const char *path = Wads.GetWadFullName(pwads[i].wadnum);
		if (path == NULL)
			continue;

		const long long size = FileSizeOf(path);
		if (size < 0)
			continue;

		ServableEntry entry;
		entry.info = zx::ServableFile(pwads[i].name.GetChars(), false, size);
		entry.path = path;
		table.push_back(entry);
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	g_servable.swap(table);
}

ServeConfig ReadConfig()
{
	ServeConfig config;
	config.enabled = !!sv_fua_download;
	config.globalRateBytes = static_cast<long long>(sv_fua_download_maxrate) * 1024;
	config.connRateBytes = static_cast<long long>(sv_fua_download_rate) * 1024;
	config.slots = sv_fua_download_slots;
	config.perAddress = sv_fua_download_peraddress;
	config.maxFileBytes = static_cast<long long>(sv_fua_download_maxsize) * 1024 * 1024;
	return config;
}

int WantedPort()
{
	const int configured = sv_fua_download_port;
	if (configured > 0 && configured < 65536)
		return configured;
	return static_cast<int>(NETWORK_GetLocalPort());
}

bool StartListener(int port)
{
#ifdef _WIN32
	if (!g_winsockReady)
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			return false;
		g_winsockReady = true;
	}
#endif

	const zx_socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == ZX_INVALID_SOCKET)
		return false;

	int yes = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof yes);

	sockaddr_in address;
	std::memset(&address, 0, sizeof address);
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(port));
	address.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0 ||
		listen(sock, 16) < 0)
	{
		zx_close_socket(sock);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_listen = sock;
		g_boundPort = port;
		g_stopping = false;
		g_listenerRunning = true;
	}

	std::thread(ListenThread).detach();
	return true;
}

void StopListener()
{
	zx_socket_t sock = ZX_INVALID_SOCKET;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_listen == ZX_INVALID_SOCKET)
			return;
		g_stopping = true;
		sock = g_listen;
		g_listen = ZX_INVALID_SOCKET;
		g_boundPort = 0;
	}

	// Closing the listener is what unblocks accept(); the thread then sees g_stopping and returns.
	zx_close_socket(sock);
}

} // namespace

namespace zx { namespace wadserve {

bool IsActive()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_listen != ZX_INVALID_SOCKET;
}

int Port()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_boundPort;
}

bool PrefersMirrors()
{
	return !!sv_fua_download_prefermirrors;
}

void Shutdown()
{
	StopListener();
}

FString StatusLine()
{
	std::lock_guard<std::mutex> lock(g_mutex);

	FString line;
	if (g_listen == ZX_INVALID_SOCKET)
	{
		line = "wad serving is off";
		return line;
	}

	line.Format("serving on TCP port %d -- %d/%d transfers active, %d done, %d refused, %.1f MB sent",
		g_boundPort, g_activeTotal, g_config.slots, g_transfersServed, g_transfersRefused,
		double(g_bytesServed) / (1024.0 * 1024.0));
	return line;
}

void Tick()
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;

	// Configuration first: a worker must never read a CVAR, so this is the only place they change.
	const ServeConfig config = ReadConfig();
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_config = config;
	}

	const bool active = IsActive();

	if (config.enabled && !active)
	{
		const int port = WantedPort();
		if (port > 0)
		{
			if (StartListener(port))
			{
				RebuildServableTable();
				Printf(TEXTCOLOR_GREEN "WAD serving enabled on TCP port %d.\n" TEXTCOLOR_NORMAL
					"Clients download missing files directly from this server. Forward TCP %d as "
					"well as UDP, or downloads will fail while the server itself works.\n",
					port, port);
			}
			else
			{
				Printf(TEXTCOLOR_RED "Could not listen on TCP port %d for WAD serving.\n"
					TEXTCOLOR_NORMAL "Something else is using it, or the port is not available. "
					"Set sv_fua_download_port, or sv_fua_download 0 to stop trying.\n", port);
				sv_fua_download = false;
			}
		}
	}
	else if (!config.enabled && active)
	{
		StopListener();
		Printf("WAD serving disabled.\n");
	}

	if (active)
	{
		bool changed = false;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			changed = WadSetChanged(g_servableNames);
		}

		if (changed)
		{
			std::vector<std::string> names = CurrentWadNames();
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_servableNames.swap(names);
			}
			RebuildServableTable();
		}
	}

	// Drain whatever the workers had to say. Copied out under the lock and printed outside it, so a
	// slow console never holds up a transfer.
	std::vector<std::string> lines;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		lines.swap(g_log);
	}
	for (size_t i = 0; i < lines.size(); ++i)
		Printf("%s\n", lines[i].c_str());
}

}} // namespace zx::wadserve

//*****************************************************************************
//	CONSOLE COMMANDS

// [rc4l] fua_ per the naming rule. Mostly exists so "downloads do not work" is diagnosable without
// guessing: it says whether the listener is up, on what port, and whether anyone is being refused.
CCMD( fua_downloadserver_status )
{
	Printf("%s\n", zx::wadserve::StatusLine().GetChars());
}
