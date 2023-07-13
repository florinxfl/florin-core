// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#if defined(HAVE_CONFIG_H)
#include "config/build-config.h"
#endif

#include "util.h"

#include "chainparamsbase.h"
#include "fs.h"
#include "random.h"
#include "serialize.h"
#include "util/strencodings.h"
#include "util/time.h"
#include "appname.h"

#include <stdarg.h>

#if (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
#include <pthread.h>
#include <pthread_np.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>
#endif

#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif // __linux__

#include <algorithm>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#else

#ifdef _MSC_VER
#pragma warning(disable:4786)
#pragma warning(disable:4804)
#pragma warning(disable:4805)
#pragma warning(disable:4717)
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501

#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <boost/algorithm/string/replace.hpp>
#ifdef HAVE_MALLOPT_ARENA_MAX
#include <malloc.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/predicate.hpp> // for starts_with() and ends_with()
#include <boost/program_options/detail/config_file.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/conf.h>


const char * const DEFAULT_CONF_FILENAME = "florin.conf";
const char * const DEFAULT_PID_FILENAME = "florin.pid";

ArgsManager gArgs;
bool fPrintToConsole = false;
bool fPrintToDebugLog = true;
bool fNoUI = false;
bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS;
bool fLogIPs = DEFAULT_LOGIPS;
bool gbMinimalLogging = false;
std::atomic<bool> fReopenDebugLog(false);
CTranslationInterface translationInterface;

/** Log categories bitfield. */
std::atomic<uint32_t> logCategories(0);

/** Init OpenSSL library multithreading support */
static std::unique_ptr<RecursiveMutex[]> ppmutexOpenSSL;

#pragma message("Check thread safety of OpenSSL usage!")
/* In recent versions of OpenSSL the CRYPTO_set_locking_callback taking the locking_callback below as an argument is
 * an empty define. So the locking_callback is reported as unused. It could be fine as it is, but we should check
 * that our usage of OpenSSL doesn't have concurrency issues because we should update to newer OpenSSL APIs or something.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void locking_callback(int mode, int i, [[maybe_unused]] const char* file, [[maybe_unused]] int line) NO_THREAD_SAFETY_ANALYSIS
{
    if (mode & CRYPTO_LOCK) {
        ENTER_CRITICAL_SECTION(ppmutexOpenSSL[i]);
    } else {
        LEAVE_CRITICAL_SECTION(ppmutexOpenSSL[i]);
    }
}
#pragma GCC diagnostic pop


// Singleton for wrapping OpenSSL setup/teardown.
class CInit
{
public:
    CInit()
    {
        // Init OpenSSL library multithreading support
        ppmutexOpenSSL.reset(new RecursiveMutex[CRYPTO_num_locks()]);
        CRYPTO_set_locking_callback(locking_callback);

        // OpenSSL can optionally load a config file which lists optional loadable modules and engines.
        // We don't use them so we don't require the config. However some of our libs may call functions
        // which attempt to load the config file, possibly resulting in an exit() or crash if it is missing
        // or corrupt. Explicitly tell OpenSSL not to try to load the file. The result for our libs will be
        // that the config appears to have been loaded and there are no modules/engines available.
        OPENSSL_no_config();

#ifdef WIN32
        // Seed OpenSSL PRNG with current contents of the screen
        RAND_screen();
#endif

        // Seed OpenSSL PRNG with performance counter
        RandAddSeed();
    }
    ~CInit()
    {
        // Securely erase the memory used by the PRNG
        RAND_cleanup();
        // Shutdown OpenSSL library multithreading support
        CRYPTO_set_locking_callback(NULL);
        // Clear the set of locks now to maintain symmetry with the constructor.
        ppmutexOpenSSL.reset();
    }
}
instance_of_cinit;

struct CLogCategoryDesc
{
    uint32_t flag;
    std::string category;
};

const CLogCategoryDesc LogCategories[] =
{
    {BCLog::NONE, "0"},
    {BCLog::ALERT, "alert"},
    {BCLog::NET, "net"},
    {BCLog::TOR, "tor"},
    {BCLog::MEMPOOL, "mempool"},
    {BCLog::HTTP, "http"},
    {BCLog::BENCH, "bench"},
    {BCLog::IO, "io"},
    {BCLog::ZMQ, "zmq"},
    {BCLog::DB, "db"},
    {BCLog::RPC, "rpc"},
    {BCLog::ESTIMATEFEE, "estimatefee"},
    {BCLog::ADDRMAN, "addrman"},
    {BCLog::SELECTCOINS, "selectcoins"},
    {BCLog::REINDEX, "reindex"},
    {BCLog::CMPCTBLOCK, "cmpctblock"},
    {BCLog::RAND, "rand"},
    {BCLog::PRUNE, "prune"},
    {BCLog::PROXY, "proxy"},
    {BCLog::MEMPOOLREJ, "mempoolrej"},
    {BCLog::LIBEVENT, "libevent"},
    {BCLog::COINDB, "coindb"},
    {BCLog::DELTA, "delta"},
    {BCLog::WITNESS, "witness"},
    {BCLog::QT, "qt"},
    {BCLog::LEVELDB, "leveldb"},
    {BCLog::WALLET, "wallet"},
    {BCLog::ALL, "1"},
    {BCLog::ALL, "all"},
};

bool GetLogCategory(uint32_t *f, const std::string *str)
{
    if (f && str) {
        if (*str == "") {
            *f = BCLog::ALL;
            return true;
        }
        for (unsigned int i = 0; i < ARRAYLEN(LogCategories); i++) {
            if (LogCategories[i].category == *str) {
                *f = LogCategories[i].flag;
                return true;
            }
        }
    }
    return false;
}

std::string ListLogCategories()
{
    std::string ret;
    int outcount = 0;
    for (unsigned int i = 0; i < ARRAYLEN(LogCategories); i++) {
        // Omit the special cases.
        if (LogCategories[i].flag != BCLog::NONE && LogCategories[i].flag != BCLog::ALL) {
            if (outcount != 0) ret += ", ";
            ret += LogCategories[i].category;
            outcount++;
        }
    }
    return ret;
}

std::vector<CLogCategoryActive> ListActiveLogCategories()
{
    std::vector<CLogCategoryActive> ret;
    for (unsigned int i = 0; i < ARRAYLEN(LogCategories); i++) {
        // Omit the special cases.
        if (LogCategories[i].flag != BCLog::NONE && LogCategories[i].flag != BCLog::ALL) {
            CLogCategoryActive catActive;
            catActive.category = LogCategories[i].category;
            catActive.active = LogAcceptCategory(LogCategories[i].flag);
            ret.push_back(catActive);
        }
    }
    return ret;
}

/** Interpret string as boolean, for argument parsing */
static bool InterpretBool(const std::string& strValue)
{
    if (strValue.empty())
        return true;
    if (strValue == "true")
        return true;
    if (strValue == "yes")
        return true;
    if (strValue == "1")
        return true;
    if (strValue == "false")
        return false;
    if (strValue == "no")
        return false;
    if (strValue == "0")
        return false;
    return (atoi(strValue) != 0);
}

/** Turn -noX into -X=0 */
static void InterpretNegativeSetting(std::string& strKey, std::string& strValue)
{
    if (strKey.length()>3 && strKey[0]=='-' && strKey[1]=='n' && strKey[2]=='o')
    {
        strKey = "-" + strKey.substr(3);
        strValue = InterpretBool(strValue) ? "0" : "1";
    }
}

void ArgsManager::ParseParameters(int argc, const char* const argv[])
{
    LOCK(cs_args);

    mapArgs.clear();
    mapMultiArgs.clear();

    ParseExtraParameters(argc - 1, argv + 1);
}

void ArgsManager::ParseExtraParameters(int argc, const char*const argv[])
{
    LOCK(cs_args);
    for (int i = 0; i < argc; i++)
    {
        std::string str(argv[i]);
        std::string strValue;
        size_t is_index = str.find('=');
        if (is_index != std::string::npos)
        {
            strValue = str.substr(is_index+1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        boost::to_lower(str);
        if (boost::algorithm::starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif

        if (str[0] != '-')
            break;

        // Interpret --foo as -foo.
        // If both --foo and -foo are set, the last takes effect.
        if (str.length() > 1 && str[1] == '-')
            str = str.substr(1);
        InterpretNegativeSetting(str, strValue);

        mapArgs[str] = strValue;
        mapMultiArgs[str].push_back(strValue);
    }
}

std::vector<std::string> ArgsManager::GetArgs(const std::string& strArg)
{
    LOCK(cs_args);
    return mapMultiArgs.at(strArg);
}

bool ArgsManager::IsArgSet(const std::string& strArg)
{
    LOCK(cs_args);
    return mapArgs.count(strArg);
}

std::string ArgsManager::GetArg(const std::string& strArg, const std::string& strDefault)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return mapArgs[strArg];
    return strDefault;
}

int64_t ArgsManager::GetArg(const std::string& strArg, int64_t nDefault)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return atoi64(mapArgs[strArg]);
    return nDefault;
}

bool ArgsManager::GetBoolArg(const std::string& strArg, bool fDefault)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return InterpretBool(mapArgs[strArg]);
    return fDefault;
}

bool ArgsManager::SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return false;
    ForceSetArg(strArg, strValue);
    return true;
}

bool ArgsManager::SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

void ArgsManager::ForceSetArg(const std::string& strArg, const std::string& strValue)
{
    LOCK(cs_args);
    mapArgs[strArg] = strValue;
    mapMultiArgs[strArg].clear();
    mapMultiArgs[strArg].push_back(strValue);
}



static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

std::string HelpMessageGroup(const std::string &message) {
    return std::string(message) + std::string("\n\n");
}

std::string HelpMessageOpt(const std::string &option, const std::string &message) {
    return std::string(optIndent,' ') + std::string(option) +
           std::string("\n") + std::string(msgIndent,' ') +
           FormatParagraph(message, screenWidth - msgIndent, msgIndent) +
           std::string("\n\n");
}

static std::string FormatException(const std::exception* pex, const char* pszThread)
{
#ifdef WIN32
    char pszModule[MAX_PATH] = "";
    GetModuleFileNameA(NULL, pszModule, sizeof(pszModule));
#else
    const char* pszModule = GLOBAL_APPNAME;
#endif
    if (pex)
        return strprintf(
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        return strprintf(
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintExceptionContinue(const std::exception* pex, const char* pszThread)
{
    std::string message = FormatException(pex, pszThread);
    LogPrintf("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
}

std::string defaultDataDirOverride="";
fs::path GetDefaultDataDir()
{
    if (!defaultDataDirOverride.empty())
        return defaultDataDirOverride;

    // Windows < Vista: C:\Documents and Settings\Username\Application Data\florin
    // Windows >= Vista: C:\Users\Username\AppData\Roaming\florin
    // Mac: ~/Library/Application Support/florin
    // Unix: ~/.florin
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "florin";
#else
    fs::path pathRet;
    char* pszHome = getenv("HOME");
    if (pszHome == NULL || strlen(pszHome) == 0)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    return pathRet / "Library/Application Support/florin";
#else
    // Unix
    return pathRet / ".florin";
#endif
#endif
}

static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static RecursiveMutex csPathCached;

const fs::path &GetDataDir(bool fNetSpecific)
{
    LOCK(csPathCached);

    fs::path &path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (IsArgSet("-datadir")) {
        path = fs::system_complete(GetArg("-datadir", ""));
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific)
        path /= BaseParams().DataDir();

    fs::create_directories(path);

    return path;
}

void ClearDatadirCache()
{
    LOCK(csPathCached);

    pathCached = fs::path();
    pathCachedNetSpecific = fs::path();
}

fs::path GetConfigFile(const std::string& confPath)
{
    fs::path pathConfigFile(confPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

void ArgsManager::ReadConfigFile(const std::string& confPath)
{
    static bool fTestnet = IsArgSet("-testnet");
    static bool fRegTest = IsArgSet("-regtest");
    static bool fRegTestLegacy = IsArgSet("-regtestlegacy");
    fs::ifstream streamConfig(GetConfigFile(confPath));
    if (!streamConfig.good())
        return; // No florin.conf file is OK

    {
        LOCK(cs_args);
        std::set<std::string> setOptions;
        setOptions.insert("*");

        for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it)
        {
            // Don't overwrite existing settings so command line settings override florin.conf
            std::string strKey = std::string("-") + it->string_key;
            std::string strValue = it->value[0];
            if (fRegTest)
                boost::replace_all(strKey, "regtest.", "");
            if (fRegTestLegacy)
                boost::replace_all(strKey, "regtestlegacy.", "");
            else if (fTestnet)
                boost::replace_all(strKey, "testnet.", "");
            else
                boost::replace_all(strKey, "mainnet.", "");
            InterpretNegativeSetting(strKey, strValue);
            if (mapArgs.count(strKey) == 0)
                mapArgs[strKey] = strValue;
            mapMultiArgs[strKey].push_back(strValue);
            
            if (strKey == "-testnet")
                fTestnet=true;
            if (strKey == "-regtest")
                fRegTest=true;
            if (strKey == "-regtestlegacy")
                fRegTestLegacy=true;
        }
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

#ifndef WIN32
fs::path GetPidFile()
{
    fs::path pathPidFile(GetArg("-pid", DEFAULT_PID_FILENAME));
    if (!pathPidFile.is_complete()) pathPidFile = GetDataDir() / pathPidFile;
    return pathPidFile;
}

void CreatePidFile(const fs::path &path, pid_t pid)
{
    FILE* file = fsbridge::fopen(path, "w");
    if (file)
    {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

bool RenameOver(fs::path src, fs::path dest)
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    int rc = std::rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif /* WIN32 */
}

/**
 * Ignores exceptions thrown by Boost's create_directory if the requested directory exists.
 * Specifically handles case where path p exists, but it wasn't possible for the user to
 * write to the parent directory.
 */
bool TryCreateDirectory(const fs::path& p)
{
    try
    {
        return fs::create_directory(p);
    } catch (const fs::filesystem_error&) {
        if (!fs::exists(p) || !fs::is_directory(p))
            throw;
    }

    // create_directory didn't create the directory, it had to have existed already
    return false;
}

void FileCommit(FILE *file)
{
    fflush(file); // harmless if redundantly called
#ifdef WIN32
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    FlushFileBuffers(hFile);
#else
    #if defined(__linux__) || defined(__NetBSD__)
    fdatasync(fileno(file));
    #elif defined(__APPLE__) && defined(F_FULLFSYNC)
    fcntl(fileno(file), F_FULLFSYNC, 0);
    #else
    fsync(fileno(file));
    #endif
#endif
}

bool TruncateFile(FILE *file, unsigned int length) {
#if defined(WIN32)
    return _chsize(_fileno(file), length) == 0;
#else
    return ftruncate(fileno(file), length) == 0;
#endif
}

/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
int RaiseFileDescriptorLimit(int nMinFD) {
#if defined(WIN32)
    return 2048;
#else
    struct rlimit limitFD;
    if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1) {
        if (limitFD.rlim_cur < (rlim_t)nMinFD) {
            limitFD.rlim_cur = nMinFD;
            if (limitFD.rlim_cur > limitFD.rlim_max)
                limitFD.rlim_cur = limitFD.rlim_max;
            setrlimit(RLIMIT_NOFILE, &limitFD);
            getrlimit(RLIMIT_NOFILE, &limitFD);
        }
        return limitFD.rlim_cur;
    }
    return nMinFD; // getrlimit failed, assume it's fine
#endif
}

/**
 * this function tries to make a particular range of a file allocated (corresponding to disk space)
 * it is advisory, and the range specified in the arguments will never contain live data
 */
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length) {
#if defined(WIN32)
    // Windows-specific version
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    LARGE_INTEGER nFileSize;
    int64_t nEndPos = (int64_t)offset + length;
    nFileSize.u.LowPart = nEndPos & 0xFFFFFFFF;
    nFileSize.u.HighPart = nEndPos >> 32;
    SetFilePointerEx(hFile, nFileSize, 0, FILE_BEGIN);
    SetEndOfFile(hFile);
#elif defined(MAC_OSX)
    // OSX specific version
    fstore_t fst;
    fst.fst_flags = F_ALLOCATECONTIG;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = (off_t)offset + length;
    fst.fst_bytesalloc = 0;
    if (fcntl(fileno(file), F_PREALLOCATE, &fst) == -1) {
        fst.fst_flags = F_ALLOCATEALL;
        fcntl(fileno(file), F_PREALLOCATE, &fst);
    }
    ftruncate(fileno(file), fst.fst_length);
#elif defined(__linux__)
    // Version using posix_fallocate
    off_t nEndPos = (off_t)offset + length;
    posix_fallocate(fileno(file), 0, nEndPos);
#else
    // Fallback version
    // TODO: just write one byte per block
    static const char buf[65536] = {};
    fseek(file, offset, SEEK_SET);
    while (length > 0) {
        unsigned int now = 65536;
        if (length < now)
            now = length;
        fwrite(buf, 1, now, file); // allowed to fail; this function is advisory anyway
        length -= now;
    }
#endif
}

void ShrinkDebugFile()
{
    // Amount of debug.log to save at end when shrinking (must fit in memory)
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;
    // Scroll debug.log if it's getting too big
    fs::path pathLog = GetDataDir() / "debug.log";
    FILE* file = fsbridge::fopen(pathLog, "r");
    // If debug.log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes
    if (file && fs::file_size(pathLog) > 11 * (RECENT_DEBUG_HISTORY_SIZE / 10))
    {
        // Restart the file with some of the end
        std::vector<char> vch(RECENT_DEBUG_HISTORY_SIZE, 0);
        fseek(file, -((long)vch.size()), SEEK_END);
        int nBytes = fread(vch.data(), 1, vch.size(), file);
        fclose(file);

        file = fsbridge::fopen(pathLog, "w");
        if (file)
        {
            fwrite(vch.data(), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file != NULL)
        fclose(file);
}

#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    char pszPath[MAX_PATH] = "";

    if(SHGetSpecialFolderPathA(NULL, pszPath, nFolder, fCreate))
    {
        return fs::path(pszPath);
    }

    LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif

void runCommand(const std::string& strCommand)
{
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE!=0 // it's defined, but as 0 on macOS
        LogPrintf("runCommand not supported on iOS!\n");
#else
    int nErr = ::system(strCommand.c_str());
    if (nErr)
        LogPrintf("runCommand error: system(%s) returned %d\n", strCommand, nErr);
#endif
}

// Application startup time (used for uptime calculation)
const int64_t nStartupTime = GetTime();

// Obtain the application startup time (used for uptime calculation)
int64_t GetStartupTime()
{
    return nStartupTime;
}

void SetupEnvironment()
{
#ifdef HAVE_MALLOPT_ARENA_MAX
    // glibc-specific: On 32-bit systems set the number of arenas to 1.
    // By default, since glibc 2.10, the C library will create up to two heap
    // arenas per core. This is known to cause excessive virtual address space
    // usage in our usage. Work around it by setting the maximum number of
    // arenas to 1.
    if (sizeof(void*) == 4) {
        mallopt(M_ARENA_MAX, 1);
    }
#endif
    // On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
    // may be invalid, in which case the "C" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    try {
        std::locale(""); // Raises a runtime error if current locale is invalid
    } catch (const std::runtime_error&) {
        setenv("LC_ALL", "C", 1);
    }
#endif
    // The path locale is lazy initialized and to avoid deinitialization errors
    // in multithreading environments, it is set explicitly by the main thread.
    // A dummy locale is used to extract the internal default locale, used by
    // fs::path, which is then used to explicitly imbue the path.
    std::locale loc = fs::path::imbue(std::locale::classic());
    fs::path::imbue(loc);
}

bool SetupNetworking()
{
#ifdef WIN32
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion ) != 2 || HIBYTE(wsadata.wVersion) != 2)
        return false;
#endif
    return true;
}

int GetNumCores()
{
#if BOOST_VERSION >= 105600
    return boost::thread::physical_concurrency();
#else // Must fall back to hardware_concurrency, which unfortunately counts virtual cores
    return boost::thread::hardware_concurrency();
#endif
}

// Base-10 variation on Fletcher's checksum algorithm to create a position dependent checksum
int Base10ChecksumEncode(int data)
{
    int number = data;

    int sum1 = 0;
    int sum2 = 0;
    while (data>0)
    {
        int v = data % 10;
        sum1 = (sum1 + v) % 10;
        sum2 = (sum2 + sum1) % 10;
        data = data / 10;
    }

    int f0 = sum1;
    int f1 = sum2;
    int c0 = 10 - (( f0 + f1) % 10);
    int c1 = 10 - (( f0 + c0 ) % 10);

    // here we have c0/c1 in [1..10], map onto [0..9] to fit into single digit
    c0 = c0 - 1;
    c1 = c1 - 1;

    return 100 * number + 10 * c1 + c0;
}

bool Base10ChecksumDecode(int number, int* decoded)
{
    int c0 = number % 10;
    int c1 = (number / 10) % 10;

    // remap c0/1 from [0..9] onto their original [1..10] range
    c0 += 1;
    c1 += 1;

    int data = number / 100;

    int sum1 = 0;
    int sum2 = 0;
    while (data>0)
    {
        int v = data % 10;
        sum1 = (sum1 + v) % 10;
        sum2 = (sum2 + sum1) % 10;
        data = data / 10;
    }

    sum1 = (sum1 + c0) % 10;
    sum2 = (sum2 + sum1) % 10;

    sum1 = (sum1 + c1) % 10;
    sum2 = (sum2 + sum1) % 10;

    bool succes = sum1 == 0 && sum2 == 0;

    if (succes && decoded)
        *decoded = number / 100;

    return succes;
}
