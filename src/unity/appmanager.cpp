// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "appname.h"
#include "appmanager.h"
#include "chainparams.h"
#include "util.h"
#include "util/thread.h"
#include "util/threadnames.h"
#include "witnessutil.h"
#include "init.h"
#include "warnings.h"

// Below  includes is for the socketpair that controls shutdown.
#ifdef WIN32
#include <sys/types.h>
#endif

#if HAVE_DECL_FORK
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#endif

AppLifecycleManager* AppLifecycleManager::gApp = nullptr;

AppLifecycleManager::AppLifecycleManager()
: fShutDownHasBeenInitiated(false)
, fShutDownFromSigterm(false)
, shutdownDidFinish(false)
, recoveryBirthNumber(0)
{
    if (gApp)
        assert(0);

    gApp = this;

    // Create the shutdown handler thread.
    shutdownThread();
}

AppLifecycleManager::~AppLifecycleManager()
{
    // Refuse to close if initialize() or shutdown() is still busy.
    std::lock_guard<std::mutex> lock(appManagerInitShutDownMutex);
}

void AppLifecycleManager::handleRunawayException(const std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    signalRunawayException((GetWarnings("gui")));
}

void AppLifecycleManager::initialize()
{
    std::thread([=]
    {
        //RenameThread(GLOBAL_APPNAME"-initialise");
        std::lock_guard<std::mutex> lock(appManagerInitShutDownMutex);
        try
        {
            LogPrintf("AppLifecycleManager::initialize: Running initialization in thread\n");
            if (fShutDownHasBeenInitiated)
                return;
            if (!AppInitBasicSetup())
            {
                signalAppInitializeResult(false);
                return;
            }
            if (fShutDownHasBeenInitiated)
                return;
            if (!AppInitParameterInteraction())
            {
                signalAppInitializeResult(false);
                return;
            }
            if (fShutDownHasBeenInitiated)
                return;
            if (!AppInitSanityChecks())
            {
                signalAppInitializeResult(false);
                return;
            }
            if (fShutDownHasBeenInitiated)
                return;

            //fixme: (UNITY) - We handle only the last slot return here - this is fine for now as there -is- only one.
            //However we should just use a custom combiner and boolean && the results to be future safe for other ports.
            if (!signalAboutToInitMain())
            {
                LogPrintf("shutdown: AppLifecycleManager, signalAboutToInitMain returned false, terminating app");
                //Start shutdown process.
                shutdown();
                return;
            }

            if (fShutDownHasBeenInitiated)
                return;

            bool rv = AppInitMain(threadGroup, nodeContext);
            signalAppInitializeResult(rv);
        }
        catch (const std::exception& e)
        {
            handleRunawayException(&e);
        }
        catch (...)
        {
            handleRunawayException(NULL);
        }
    }).detach();
}

// We use a socket here to signal shutdown to the main app.
// As we (may) have been called from sigterm it is not safe to do anything else here.
// See http://doc.qt.io/qt-5/unix-signals.html for more information.
void AppLifecycleManager::shutdown(bool fromSigTerm)
{
    // Let the core know that we are in the early process of shutting down.
    // Do this before the mutex so that if we are still in init we can abandon the init.
    fShutDownHasBeenInitiated = true;
    
    fShutDownFromSigterm = fromSigTerm;

    #ifdef WIN32
    sigtermCv.notify_one();
    #else
    char signalClose = 1;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-result"
    ::write(sigtermFd[0], &signalClose, sizeof(signalClose));
    #pragma GCC diagnostic pop

    #endif
}

void AppLifecycleManager::waitForShutDown()
{
    std::unique_lock<std::mutex> lock(shutdownFinishMutex);
    shutdownFinishCondition.wait(lock, [&]{ return shutdownDidFinish; });
}

#if HAVE_DECL_FORK
bool daemoniseUsingFork() {
    // deamonize using double fork as daemonise() is not standarized and available on all platforms
    // instead use POSIX compliant fork() using double forking as described (for example)
    // at http://www.microhowto.info/howto/cause_a_process_to_become_a_daemon_in_c.html

    pid_t pid = fork();
    if (pid == -1) {
        return false;
    } else if (pid != 0) {
        // I'm the parent of the first fork, exit
        _exit(0);
    }

    // Start a new session for the daemon.
    if (setsid()==-1) {
        return false;
    }

    // ignore closing of controlliing terminal
    signal(SIGHUP,SIG_IGN);

    pid=fork();
    if (pid == -1) {
        return false;
    } else if (pid != 0) {
        // I'm the parent of the 2nd fork, exit
        _exit(0);
    }

    // close standard file descriptors.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // re-open
    const char* DEV_NULL = "/dev/null";
    if (open(DEV_NULL, O_RDONLY) == -1) {
        return false;
    }
    if (open(DEV_NULL, O_WRONLY) == -1) {
        return false;
    }
    if (open(DEV_NULL, O_RDWR) == -1) {
        return false;
    }

    return true;
}
#endif

bool AppLifecycleManager::daemonise()
{
    #if HAVE_DECL_FORK
    {
        bool managedToDeamonise = daemoniseUsingFork();

        if (!managedToDeamonise)
        {
            fprintf(stderr, "Error: daemon() failed: %s\n", strerror(errno));
        }

        // Create a new shutdownThread
        shutdownThread();
        return managedToDeamonise;
    }
    #else
    fprintf(stderr, "Error: -daemon is not supported on this operating system\n");
    return false;
    #endif
}

void AppLifecycleManager::shutdownThread()
{
    #ifndef WIN32
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigtermFd) == -1)
    {
        LogPrintf("shutdown thread: Failed to create socket pair\n");
        assert(0);
    }
    #endif

    std::thread([=]
    {
        util::ThreadRename(GLOBAL_APPNAME"-shutdown");

        // Block until we are signalled to commence
        #ifdef WIN32
        std::unique_lock<std::mutex> lk(appManagerInitShutDownMutex);
        sigtermCv.wait(lk, [this]{ return fShutDownHasBeenInitiated == true; });
        #else
        char signalClose = 0;
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wunused-result"
        ::read(sigtermFd[1], &signalClose, sizeof(signalClose));
        #pragma GCC diagnostic pop
        LogPrintf("shutdown thread: App shutdown requested\n");
        if (fShutDownFromSigterm)
            LogPrintf("shutdown thread: App shutdown requested from SIGTERM\n");
        std::lock_guard<std::mutex> lock(appManagerInitShutDownMutex);
        #endif

        LogPrintf("shutdown thread: Commence app shutdown\n");

        try
        {
            // Allow UI to visually alert start of shutdown progress.
            LogPrintf("shutdown thread: Signal start of shutdown to UI\n");
            signalAppShutdownStarted();
            MilliSleep(200);

            LogPrintf("shutdown thread: Signal UI to alert user of shutdown\n");
            signalAppShutdownAlertUser();
            MilliSleep(50);

            // Notify all core and network threads to start "wrapping up".
            LogPrintf("shutdown thread: Interrupt core\n");
            CoreInterrupt(threadGroup);
            MilliSleep(50);

            // Notify UI that core shutdown has begun and that it should start disconnecting the various models/signals.
            LogPrintf("shutdown thread: Signal core interrupt to UI\n");
            signalAppShutdownCoreInterrupted();
            MilliSleep(50);

            // Terminate all core threads.
            LogPrintf("shutdown thread: Shut down core\n");
            CoreShutdown(threadGroup, nodeContext);
            MilliSleep(50);

            LogPrintf("shutdown thread: Core shutdown finished, signaling UI to shut itself down\n");
            signalAppShutdownFinished();
            MilliSleep(50);

            // signal threads blocked on waitForShutDown()
            {
                std::lock_guard<std::mutex> lock(shutdownFinishMutex);
                shutdownDidFinish = true;
            }
            shutdownFinishCondition.notify_all();

            LogPrintf("shutdown thread: Exiting shutdown thread\n");
        }
        catch (const std::exception& e)
        {
            LogPrintf("AppLifecycleManager::shutdownThread: App shutdown exception [%s]\n", e.what());
            handleRunawayException(&e);
        }
        catch (...)
        {
            LogPrintf("AppLifecycleManager::shutdownThread: App shutdown exception\n");
            handleRunawayException(NULL);
        }
    }).detach();
}

void AppLifecycleManager::setRecoveryPhrase(const SecureString& recoveryPhrase_)
{
    recoveryPhrase = recoveryPhrase_;
}

SecureString AppLifecycleManager::getRecoveryPhrase()
{
    return recoveryPhrase;
}

void AppLifecycleManager::BurnRecoveryPhrase()
{
    // The below is a 'SecureString' - so no memory burn necessary, it should burn itself.
    recoveryPhrase = "";
}

// if no birth number given or birth number is invalid the result will be zero
void AppLifecycleManager::splitRecoveryPhraseAndBirth(const SecureString& input, SecureString& phrase, int& birthNumber)
{
    phrase = input;

    auto lastSpace = phrase.find_last_of(" ");
    if (lastSpace != SecureString::npos) {
        std::string birthString(phrase.substr(lastSpace));
        try {
            birthNumber = std::stoi(birthString);
        }
        catch (const std::exception&) {
            birthNumber = 0;
        }
        if (birthNumber != 0 || birthString == "0") {
            // succesfull numeric conversion, strip birth number from phrase
            phrase.erase(lastSpace);
        }
    }
}

int AppLifecycleManager::getRecoveryBirth() const
{
    return recoveryBirthNumber;
}

void AppLifecycleManager::setRecoveryBirthNumber(int _recoveryBirth)
{
    recoveryBirthNumber = _recoveryBirth;
}

int64_t AppLifecycleManager::getRecoveryBirthTime() const
{
    return birthNumberToTime(recoveryBirthNumber);
}

void AppLifecycleManager::setRecoveryBirthTime(int64_t birthTime)
{
    if (birthTime >= Params().GenesisBlock().nTime) {
        recoveryBirthNumber = timeToBirthNumber(birthTime);
    }
    else
        recoveryBirthNumber = 0;
}

SecureString AppLifecycleManager::getCombinedRecoveryPhrase() const
{
    if (recoveryBirthNumber != 0)
        return recoveryPhrase + SecureString(" ") + SecureString(i64tostr(recoveryBirthNumber));
    else
        return recoveryPhrase;
}

void AppLifecycleManager::setCombinedRecoveryPhrase(const SecureString& combinedPhrase)
{
    SecureString phrase;
    int birth;
    splitRecoveryPhraseAndBirth(combinedPhrase, phrase, birth);
    setRecoveryPhrase(phrase);
    setRecoveryBirthNumber(birth);
}

std::pair<SecureString, int> AppLifecycleManager::composeRecoveryPhrase(const SecureString& phrase, int64_t birthTime)
{
    if (birthTime != 0)
    {
        int birthNumber = timeToBirthNumber(birthTime);
        return std::pair(phrase + SecureString(" ") + SecureString(i64tostr(birthNumber)), birthNumber);
    }
    else
        return std::pair(phrase, 0);
}

void AppLifecycleManager::setLinkKey(CEncodedSecretKeyExt<CExtKey> _linkKey)
{
    linkKey = _linkKey;
}

int64_t AppLifecycleManager::getLinkedBirthTime() const
{
    return linkKey.getCreationTime();
}

CEncodedSecretKeyExt<CExtKey> AppLifecycleManager::getLinkedKey() const
{
    return linkKey;
}

void AppLifecycleManager::setRecoveryPassword(const SecureString& password_)
{
    recoveryPassword = password_;
}

SecureString AppLifecycleManager::getRecoveryPassword()
{
    return recoveryPassword;
}

void AppLifecycleManager::SecureWipeRecoveryDetails()
{
    //fixme: (UNITY) Burn all seeds/passwords etc. here.
}

bool ShutdownRequested()
{
    return AppLifecycleManager::gApp ? (bool)AppLifecycleManager::gApp->fShutDownHasBeenInitiated : false;
}
