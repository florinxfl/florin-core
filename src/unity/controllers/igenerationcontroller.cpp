// Copyright (c) 2020 The Novo developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the NOVO software license, see the accompanying
// file COPYING

//Workaround braindamaged 'hack' in libtool.m4 that defines DLL_EXPORT when building a dll via libtool (this in turn imports unwanted symbols from e.g. pthread that breaks static pthread linkage)
#ifdef DLL_EXPORT
#undef DLL_EXPORT
#endif

#include "appname.h"
#include "net.h"
#include "net_processing.h"
#include "validation/validation.h"
#include "ui_interface.h"
#include "utilstrencodings.h"

#include "util.h"
#include "generation/miner.h"


#include <wallet/wallet.h>
#include <wallet/extwallet.h>
#include <wallet/account.h>
#include <wallet/witness_operations.h>

// Unity specific includes
#include "i_generation_controller.hpp"
#include "i_generation_listener.hpp"

std::shared_ptr<IGenerationListener> generationListener;
std::list<boost::signals2::connection> coreGenerationSignalConnections;

void IGenerationController::setListener(const std::shared_ptr<IGenerationListener>& generationListener_)
{
    generationListener = generationListener_;
        
    // Disconnect all already connected signals
    for (auto& connection: coreGenerationSignalConnections)
    {
        connection.disconnect();
    }
    coreGenerationSignalConnections.clear();
        
    if (pactiveWallet && generationListener)
    {
        coreGenerationSignalConnections.push_back(pactiveWallet->NotifyGenerationStarted.connect([]()
        {
            generationListener->onGenerationStarted();
        }));
        coreGenerationSignalConnections.push_back(pactiveWallet->NotifyGenerationStopped.connect([]()
        {
            generationListener->onGenerationStopped();
        }));
        coreGenerationSignalConnections.push_back(pactiveWallet->NotifyGenerationStatisticsUpdate.connect([]()
        {
            //fixme: (DEDUP) - Share this code with RPC if possible
            double dHashPerSecLog = dHashesPerSec;
            std::string sHashPerSecLogLabel = " h";
            selectLargesHashUnit(dHashPerSecLog, sHashPerSecLogLabel);
            double dRollingHashPerSecLog = dRollingHashesPerSec;
            std::string sRollingHashPerSecLogLabel = " h";
            selectLargesHashUnit(dRollingHashPerSecLog, sRollingHashPerSecLogLabel);
            double dBestHashPerSecLog = dBestHashesPerSec;
            std::string sBestHashPerSecLogLabel = " h";
            selectLargesHashUnit(dBestHashPerSecLog, sBestHashPerSecLogLabel);
            generationListener->onStatsUpdated(dHashPerSecLog, sHashPerSecLogLabel, dRollingHashPerSecLog, sRollingHashPerSecLogLabel, dBestHashPerSecLog, sBestHashPerSecLogLabel, nArenaSetupTime/1000.0);
        }));
    }
}

CAccount* findMiningAccount(CWallet* pWallet)
{
    for (const auto& [accountUUID, account] : pWallet->mapAccounts)
    {
        (unused) accountUUID;
        if (account->IsMiningAccount() && account->m_State == AccountState::Normal)
        {
            return account;
        }
    }
    return nullptr;
}

bool IGenerationController::startGeneration(int32_t numThreads, const std::string& memoryLimit)
{
    if (!pactiveWallet)
    {
        return false;
    }
    
    DS_LOCK2(cs_main, pactiveWallet->cs_wallet);
    CAccount* forAccount = findMiningAccount(pactiveWallet);        
    if (!forAccount)
    {
        return false;
    }
    
    std::string overrideAccountAddress;
    CWalletDB(*pactiveWallet->dbw).ReadMiningAddressString(overrideAccountAddress);

    // Allow user to override default memory selection.
    uint64_t nGenMemoryLimitBytes = GetMemLimitInBytesFromFormattedStringSpecifier(memoryLimit);
    
    // Normalise for SIGMA arena expectations (arena size must be a multiple of 16mb)
    normaliseBufferSize(nGenMemoryLimitBytes);
    
    std::thread([=]
    {
        try
        {
            PoWGenerateBlocks(true, numThreads, nGenMemoryLimitBytes/1024, Params(), forAccount, overrideAccountAddress);
        }
        catch(...)
        {
        }
    }).detach();
    
    if (overrideAccountAddress.length() > 0)
    {
        LogPrintf("Block generation enabled into account [%s] using target address [%s], thread limit: [%d threads], memory: [%d Mb].", pactiveWallet->mapAccountLabels[forAccount->getUUID()], overrideAccountAddress ,numThreads, nGenMemoryLimitBytes/1024/1024);
    }
    else
    {
        LogPrintf("Block generation enabled into account [%s], thread limit: [%d threads], memory: [%d Mb].", pactiveWallet->mapAccountLabels[forAccount->getUUID()] ,numThreads, nGenMemoryLimitBytes/1024/1024);
    }
    return true;
}

bool IGenerationController::stopGeneration()
{
    std::thread([=]
    {
        PoWStopGeneration();
    }).detach();
    return true;
}

std::string IGenerationController::getGenerationAddress()
{
    CAccount* forAccount = findMiningAccount(pactiveWallet);
    if (forAccount)
    {
        CReserveKeyOrScript* receiveAddress = new CReserveKeyOrScript(pactiveWallet, forAccount, KEYCHAIN_EXTERNAL);
        CPubKey pubKey;
        if (receiveAddress->GetReservedKey(pubKey))
        {
            CKeyID keyID = pubKey.GetID();
            return CNativeAddress(keyID).ToString();
        }
    }
    return "";
}

std::string IGenerationController::getGenerationOverrideAddress()
{
    std::string overrideAccountAddress;
    if (CWalletDB(*pactiveWallet->dbw).ReadMiningAddressString(overrideAccountAddress))
    {
        return overrideAccountAddress;
    }
    else
    {
        return "";
    }
}

bool IGenerationController::setGenerationOverrideAddress(const std::string& overrideAddress)
{
    if (!pactiveWallet)
        return false;

    DS_LOCK2(cs_main, pactiveWallet->cs_wallet);

    if (!findMiningAccount(pactiveWallet))
        return false;
    
    if (!overrideAddress.empty())
    {
        CNativeAddress address(overrideAddress);
        if (!address.IsValid())
        {
            return false;
        }
    }
    return CWalletDB(*pactiveWallet->dbw).WriteMiningAddressString(overrideAddress);
}

