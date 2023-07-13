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

#include "wallet/wallet.h"
#include "wallet/wallettx.h"

#include "alert.h"

#include "validation/validation.h"
#include "net.h"
#include "scheduler.h"
#include "timedata.h"
#include "util/moneystr.h"
#include "init.h"
#include "net_processing.h"
#include "spvscanner.h"
#include <unity/appmanager.h>
#include <wallet/mnemonic.h>
#include <future>

CWallet::CWallet()
    : CExtWallet()
{
    SetNull();
}

CWallet::CWallet(std::unique_ptr<CWalletDBWrapper> dbw_in)
    : CExtWallet(std::move(dbw_in))
{
    SetNull();
}

CWallet::~CWallet()
{
    delete pwalletdbEncryption;
    pwalletdbEncryption = NULL;
    for (const auto& [accountUUID, forAccount] : mapAccounts)
    {
        (unused) accountUUID;
        delete forAccount;
    }
    for (const auto& [seedUUID, forSeed] : mapSeeds)
    {
        (unused) seedUUID;
        delete forSeed;
    }
}

DBErrors CWallet::LoadWallet(WalletLoadState& nExtraLoadState)
{
    nExtraLoadState = NEW_WALLET;
    DBErrors nLoadWalletRet = CWalletDB(*dbw,"cr+").LoadWallet(this, nExtraLoadState);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (dbw->Rewrite("\x04pool"))
        {
            LOCK(cs_wallet);
            //fixme: (FUT) (ACCOUNTS)
            for (const auto& [accountUUID, forAccount] : mapAccounts)
            {
                (unused) accountUUID;
                forAccount->setKeyPoolInternal.clear();
                forAccount->setKeyPoolExternal.clear();
            }
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;

    uiInterface.LoadWallet(this);

    return DB_LOAD_OK;
}

//If we want to translate help messages in future we can replace helptr with _ and everything will just work.
#define helptr(x) std::string(x)
//If we want to translate error messages in future we can replace helptr with _ and everything will just work.
#define errortr(x) std::string(x)
//If we want to translate warning messages in future we can replace helptr with _ and everything will just work.
#define warningtr(x) std::string(x)

std::string CWallet::GetWalletHelpString(bool showDebug)
{
    std::string strUsage = HelpMessageGroup(helptr("Wallet options:"));
    strUsage += HelpMessageOpt("-disablewallet", helptr("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-disableui", helptr("Load the wallet in a special console only mode"));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(helptr("Set key pool size to <n> (default: %u)"), DEFAULT_ACCOUNT_KEYPOOL_SIZE));
    strUsage += HelpMessageOpt("-accountpool=<n>", strprintf(helptr("Set account pool size to <n> (default: %u)"), 10));
    strUsage += HelpMessageOpt("-fallbackfee=<amt>", strprintf(helptr("A fee rate (in %s/kB) that will be used when fee estimation has insufficient data (default: %s)"),
                                                               CURRENCY_UNIT, FormatMoney(DEFAULT_FALLBACK_FEE)));
    strUsage += HelpMessageOpt("-mintxfee=<amt>", strprintf(helptr("Fees (in %s/kB) smaller than this are considered zero fee for transaction creation (default: %s)"),
                                                            CURRENCY_UNIT, FormatMoney(DEFAULT_TRANSACTION_MINFEE)));
    strUsage += HelpMessageOpt("-paytxfee=<amt>", strprintf(helptr("Fee (in %s/kB) to add to transactions you send (default: %s)"),
                                                            CURRENCY_UNIT, FormatMoney(payTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-rescan", helptr("Rescan the block chain for missing wallet transactions on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", helptr("Attempt to recover private keys from a corrupt wallet on startup"));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(helptr("Spend unconfirmed change when sending transactions (default: %u)"), DEFAULT_SPEND_ZEROCONF_CHANGE));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(helptr("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), DEFAULT_TX_CONFIRM_TARGET));
    strUsage += HelpMessageOpt("-usehd", helptr("Use hierarchical deterministic key generation (HD) after BIP32. Only has effect during wallet creation/first start") + " " + strprintf(helptr("(default: %u)"), DEFAULT_USE_HD_WALLET));
    strUsage += HelpMessageOpt("-walletrbf", strprintf(helptr("Send transactions with full-RBF opt-in enabled (default: %u)"), DEFAULT_WALLET_RBF));
    strUsage += HelpMessageOpt("-upgradewallet", helptr("Upgrade wallet to latest format on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", helptr("Specify wallet file (within data directory)") + " " + strprintf(helptr("(default: %s)"), DEFAULT_WALLET_DAT));
    strUsage += HelpMessageOpt("-walletbroadcast", helptr("Make the wallet broadcast transactions") + " " + strprintf(helptr("(default: %u)"), DEFAULT_WALLETBROADCAST));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", helptr("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    strUsage += HelpMessageOpt("-zapwallettxes=<mode>", helptr("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
                               " " + helptr("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));

    if (showDebug)
    {
        strUsage += HelpMessageGroup(helptr("Wallet debugging/testing options:"));

        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf("Flush wallet database activity from memory to disk log every <n> megabytes (default: %u)", DEFAULT_WALLET_DBLOGSIZE));
        strUsage += HelpMessageOpt("-flushwallet", strprintf("Run a thread to flush wallet periodically (default: %u)", DEFAULT_FLUSHWALLET));
        strUsage += HelpMessageOpt("-privdb", strprintf("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)", DEFAULT_WALLET_PRIVDB));
        strUsage += HelpMessageOpt("-walletrejectlongchains", strprintf(helptr("Wallet will not create transactions that violate mempool chain limits (default: %u)"), DEFAULT_WALLET_REJECT_LONG_CHAINS));
    }

    return strUsage;
}

//Assign the bare minimum keys here, let the rest take place in the background thread
void PerformInitialMinimalKeyAllocation(CWallet* walletInstance)
{
    walletInstance->TopUpKeyPool(1);
}

void CWallet::CreateSeedAndAccountFromLink(CWallet *walletInstance)
{
    walletInstance->nTimeFirstKey = AppLifecycleManager::gApp->getLinkedBirthTime();

    LogPrintf("%s: Creating new linked primary account, birth time [%d]\n", __func__, walletInstance->nTimeFirstKey);

    walletInstance->activeAccount = walletInstance->CreateSeedlessHDAccount("My account", AppLifecycleManager::gApp->getLinkedKey(), AccountState::Normal, AccountType::Mobi, false);

    SecureString encryptUsingPassword = AppLifecycleManager::gApp->getRecoveryPassword();
    if (encryptUsingPassword.length() > 0)
    {
        LogPrintf("Encrypting wallet using passphrase\n");
        if (!walletInstance->EncryptWallet(encryptUsingPassword))
            throw std::runtime_error("Failed to encrypt wallet");
        if (!walletInstance->Unlock(encryptUsingPassword))
            throw std::runtime_error("Failed to unlock freshly encrypted wallet");
    }
    // Write the primary account into wallet file
    {
        CWalletDB walletdb(*walletInstance->dbw);
        if (!walletdb.WriteAccount(getUUIDAsString(walletInstance->activeAccount->getUUID()), walletInstance->activeAccount))
        {
            throw std::runtime_error("Writing legacy account failed");
        }
        walletdb.WritePrimaryAccount(walletInstance->activeAccount);
    }

    // Assign initial keys to the wallet
    PerformInitialMinimalKeyAllocation(walletInstance);

    //SPV special case - we need to allocate all the addresses now already for better filtering.
    if (GetBoolArg("-spv", DEFAULT_SPV))
    {
        walletInstance->TopUpKeyPool(10);
    }

    AppLifecycleManager::gApp->SecureWipeRecoveryDetails();
}

CWallet* CWallet::CreateWalletFromFile(const std::string walletFile)
{
    // needed to restore wallet transaction meta data after -zapwallettxes
    std::vector<CWalletTx> vWtx;

    if (GetBoolArg("-zapwallettxes", false)) {
        uiInterface.InitMessage(_("Zapping all transactions from wallet..."));

        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, walletFile));
        CWallet *tempWallet = new CWallet(std::move(dbw));
        DBErrors nZapWalletRet = tempWallet->ZapWalletTx(vWtx);
        if (nZapWalletRet != DB_LOAD_OK) {
            InitError(strprintf(errortr("Error loading %s: Wallet corrupted"), walletFile));
            return NULL;
        }

        delete tempWallet;
        tempWallet = NULL;
    }

    uiInterface.InitMessage(_("Loading wallet..."));

    int64_t nStart = GetTimeMillis();
    WalletLoadState loadState = NEW_WALLET;
    std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, walletFile));
    CWallet *walletInstance = new CWallet(std::move(dbw));
    DBErrors nLoadWalletRet = walletInstance->LoadWallet(loadState);
    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
        {
            InitError(strprintf(errortr("Error loading %s: Wallet corrupted"), walletFile));
            return NULL;
        }
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            uiInterface.InitMessage(_("Zapping all transactions from wallet..."));
            if (GetBoolArg("-zapwallettxes", false))
            {
                LOCK(walletInstance->cs_wallet);
                CWalletDB db(*walletInstance->dbw);
                db.ZapAllTx();
                walletInstance->mapWallet.clear();
                walletInstance->mapWalletHash.clear();
                walletInstance->mapTxSpends.clear();
            }
            InitWarning(strprintf(warningtr("Error reading %s! All keys read correctly, but transaction data or address book entries might be missing or incorrect."), walletFile));
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
        {
            InitError(strprintf(errortr("Error loading %s: Wallet requires newer version of %s"), walletFile, _(PACKAGE_NAME)));
            return NULL;
        }
        else if (nLoadWalletRet == DB_NEED_REWRITE)
        {
            InitError(strprintf(errortr("Wallet needed to be rewritten: restart %s to complete"), _(PACKAGE_NAME)));
            return NULL;
        }
        else {
            InitError(strprintf(errortr("Error loading %s"), walletFile));
            return NULL;
        }
    }

    if (GetBoolArg("-upgradewallet", loadState == NEW_WALLET))
    {
        int nMaxVersion = GetArg("-upgradewallet", 0);
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            walletInstance->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < walletInstance->GetVersion())
        {
            InitError(errortr("Cannot downgrade wallet"));
            return NULL;
        }
        walletInstance->SetMaxVersion(nMaxVersion);
    }

    if (loadState == NEW_WALLET)
    {
        // Create new keyUser and set as default key
        if (GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET))
        {
            if (fNoUI)
            {
                //fixme: (UNITY) (SPV) decide if we want to keep this option
                if (IsArgSet("-phrase")) {
                    SecureString phrase(GetArg("-phrase", ""));
                    AppLifecycleManager::gApp->setCombinedRecoveryPhrase(phrase);
                    LogPrintf("Using phrase argument for new wallet seed\n");
                }
                else if (AppLifecycleManager::gApp->getRecoveryPhrase().size() == 0)
                {
                    std::vector<unsigned char> entropy(16);
                    GetStrongRandBytes(&entropy[0], 16);
                    AppLifecycleManager::gApp->setRecoveryPhrase(mnemonicFromEntropy(entropy, entropy.size()*8));
                    AppLifecycleManager::gApp->setRecoveryBirthTime(GetAdjustedTime());
                }
            }

            if (AppLifecycleManager::gApp->getRecoveryPhrase().size() == 0)
            {
                //Work around an issue with "non HD" wallets from older versions where active account may not be set in the wallet.
                if (!walletInstance->mapAccounts.empty()) {
                    CWalletDB walletdb(*walletInstance->dbw);
                    walletInstance->setActiveAccount(walletdb, walletInstance->mapAccounts.begin()->second);
                }
                throw std::runtime_error("Invalid seed mnemonic");
            }

            if (AppLifecycleManager::gApp->isLink)
            {
                CreateSeedAndAccountFromLink(walletInstance);
            }
            else
            {
                CreateSeedAndAccountFromPhrase(walletInstance);
            }
        }
        else
        {
            //fixme: (FUT) (MED) - encrypt at creation option here.
            walletInstance->activeAccount = new CAccount();
            walletInstance->activeAccount->m_State = AccountState::Normal;
            walletInstance->activeAccount->m_Type = AccountType::Desktop;

            // Write the primary account into wallet file
            {
                CWalletDB walletdb(*walletInstance->dbw);
                if (!walletdb.WriteAccount(getUUIDAsString(walletInstance->activeAccount->getUUID()), walletInstance->activeAccount))
                {
                    throw std::runtime_error("Writing legacy account failed");
                }
                walletdb.WritePrimaryAccount(walletInstance->activeAccount);
            }

            // Assign initial keys to the wallet
            PerformInitialMinimalKeyAllocation(walletInstance);
        }

        pactiveWallet = walletInstance;
        walletInstance->SetBestChain(chainActive.GetLocatorPoW2());
    }
    else if (loadState == EXISTING_WALLET_OLDACCOUNTSYSTEM)
    {
        // HD upgrade.
        // Only perform upgrade if usehd is present (default for UI)
        // For daemon we force users to choose (for exchanges etc.)
        if (fNoUI)
        {
            if (!walletInstance->activeAccount->IsHD() && !walletInstance->activeSeed)
            {
                if (IsArgSet("-usehd"))
                {
                    throw std::runtime_error("Must specify -usehd=1 or -usehd=0, in order to allow or refuse HD upgrade.");
                }
            }
        }

        if (GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET))
        {
            if (!walletInstance->activeAccount->IsHD() && !walletInstance->activeSeed)
            {
                std::promise<void> promiseToUnlock;
                walletInstance->BeginUnlocked(_("Wallet unlock required for wallet upgrade"), [&]() {
                    promiseToUnlock.set_value();
                });
                promiseToUnlock.get_future().wait();

                bool walletWasCrypted = walletInstance->activeAccount->externalKeyStore.IsCrypted();
                {
                    LOCK(walletInstance->cs_wallet);

                    // transfer unlock session ownership
                    walletInstance->nUnlockedSessionsOwnedByShadow++;

                    //Force old legacy account to resave
                    {
                        CWalletDB walletdb(*walletInstance->dbw);
                        walletInstance->changeAccountName(walletInstance->activeAccount, _("Legacy account"), true);
                        if (!walletdb.WriteAccount(getUUIDAsString(walletInstance->activeAccount->getUUID()), walletInstance->activeAccount))
                        {
                            throw std::runtime_error("Writing legacy account failed");
                        }
                        if (walletWasCrypted && !walletInstance->activeAccount->internalKeyStore.IsCrypted())
                        {
                            walletInstance->activeAccount->internalKeyStore.SetCrypted();
                            bool needsWriteToDisk = false;
                            walletInstance->activeAccount->internalKeyStore.Unlock(walletInstance->activeAccount->vMasterKey, needsWriteToDisk);
                            if (needsWriteToDisk)
                            {
                                CWalletDB db(*dbw);
                                if (!db.WriteAccount(getUUIDAsString(walletInstance->activeAccount->getUUID()), walletInstance->activeAccount))
                                {
                                    throw std::runtime_error("Writing account failed");
                                }
                            }
                        }
                        walletInstance->ForceRewriteKeys(*walletInstance->activeAccount);
                    }

                    // Generate a new primary seed and account (BIP44)
                    std::vector<unsigned char> entropy(16);
                    GetStrongRandBytes(&entropy[0], 16);
                    walletInstance->activeSeed = new CHDSeed(mnemonicFromEntropy(entropy, entropy.size()*8).c_str(), CHDSeed::CHDSeed::BIP44);
                    if (!CWalletDB(*walletInstance->dbw).WriteHDSeed(*walletInstance->activeSeed))
                    {
                        throw std::runtime_error("Writing seed failed");
                    }
                    if (walletWasCrypted)
                    {
                        if (!walletInstance->activeSeed->Encrypt(walletInstance->activeAccount->vMasterKey))
                        {
                            throw std::runtime_error("Encrypting seed failed");
                        }
                    }
                    walletInstance->mapSeeds[walletInstance->activeSeed->getUUID()] = walletInstance->activeSeed;
                    {
                        CWalletDB walletdb(*walletInstance->dbw);
                        walletdb.WritePrimarySeed(*walletInstance->activeSeed);
                    }
                    walletInstance->activeAccount = walletInstance->GenerateNewAccount(_("My account"), AccountState::Normal, AccountType::Desktop);
                    {
                        CWalletDB walletdb(*walletInstance->dbw);
                        walletdb.WritePrimaryAccount(walletInstance->activeAccount);
                    }
                }
            }
        }
        else
        {
            std::promise<void> promiseToUnlock;
            walletInstance->BeginUnlocked(_("Wallet unlock required for wallet upgrade"), [&]() {
                promiseToUnlock.set_value();
            });
            promiseToUnlock.get_future().wait();

            bool walletWasCrypted = walletInstance->activeAccount->externalKeyStore.IsCrypted();
            {
                LOCK(walletInstance->cs_wallet);

                // transfer unlock session ownership
                walletInstance->nUnlockedSessionsOwnedByShadow++;

                //Force old legacy account to resave
                {
                    CWalletDB walletdb(*walletInstance->dbw);
                    walletInstance->changeAccountName(walletInstance->activeAccount, _("Legacy account"), true);
                    if (!walletdb.WriteAccount(getUUIDAsString(walletInstance->activeAccount->getUUID()), walletInstance->activeAccount))
                    {
                        throw std::runtime_error("Writing legacy account failed");
                    }
                    if (walletWasCrypted && !walletInstance->activeAccount->internalKeyStore.IsCrypted())
                    {
                        walletInstance->activeAccount->internalKeyStore.SetCrypted();
                        bool needsWriteToDisk = false;
                        walletInstance->activeAccount->internalKeyStore.Unlock(walletInstance->activeAccount->vMasterKey, needsWriteToDisk);
                        if (needsWriteToDisk)
                        {
                            CWalletDB db(*dbw);
                            if (!db.WriteAccount(getUUIDAsString(walletInstance->activeAccount->getUUID()), walletInstance->activeAccount))
                            {
                                throw std::runtime_error("Writing account failed");
                            }
                        }
                    }
                    walletInstance->ForceRewriteKeys(*walletInstance->activeAccount);
                }
            }
        }
    }
    else if (loadState == EXISTING_WALLET)
    {
        CAccount* firstAccount = nullptr;

        //Detect any HD account seed gaps.
        bool indexError=false;
        for (const auto& accountType : {AccountType::Desktop, AccountType::Mobi, AccountType::PoW2Witness})
        {
            for (const auto& [seedUUID, seed] : walletInstance->mapSeeds)
            {
                (unused)seed;
                std::set<uint64_t> allIndexes;
                for(const auto& [accountUUID, account] : walletInstance->mapAccounts)
                {
                    if (account->m_Type != accountType)
                        continue;

                    (unused)accountUUID;
                    if (account->IsHD() && dynamic_cast<CAccountHD*>(account)->getSeedUUID() == seedUUID)
                    {
                        uint64_t nIndex = dynamic_cast<CAccountHD*>(account)->getIndex();
                        if (nIndex >= BIP32_HARDENED_KEY_LIMIT)
                        {
                            nIndex = nIndex & ~BIP32_HARDENED_KEY_LIMIT;
                        }
                        if (!firstAccount && nIndex == 0)
                            firstAccount = account;
                        allIndexes.insert(nIndex);
                    }
                }
                if (allIndexes.size() > 0)
                {
                    uint64_t nStart = *allIndexes.begin();
                    uint64_t nStartComp = 0;
                    switch (accountType)
                    {
                        case AccountType::Desktop: nStartComp=0; break;
                        case AccountType::Mobi: nStartComp=HDMobileStartIndex; break;
                        case AccountType::PoW2Witness: nStartComp=HDWitnessStartIndex; break;
                        default:
                            //Do nothing.
                            break;
                    }
                    if (nStart != nStartComp && nStart != nStartComp+1)
                        indexError = true;
                    if (!indexError)
                    {
                        std::set<uint64_t>::iterator setIter = allIndexes.begin();
                        ++setIter;
                        uint64_t index=1;
                        for (;setIter != allIndexes.end();)
                        {
                            if (*setIter != nStart + index)
                            {
                                indexError = true;
                            }
                            ++index;
                            ++setIter;
                        }
                    }
                }
            }
        }
        if (indexError)
        {
            std::string strErrorMessage = strprintf("Wallet contains an HD index error, this is not immediately dangerous but should be rectified as soon as possible, please contact a developer for assistance");
            CAlert::Notify(strErrorMessage, true, true);
            LogPrintf("%s", strErrorMessage.c_str());
            uiInterface.ThreadSafeMessageBox(strErrorMessage,"", CClientUIInterface::MSG_ERROR);
        }

        //Clean up a slight issue in 1.6.0 -> 1.6.3 wallets where a "usehd=0" account was created but no active account set.
        if (!walletInstance->activeAccount)
        {
            if (!walletInstance->mapAccounts.empty())
            {
                CWalletDB walletdb(*walletInstance->dbw);
                walletInstance->setActiveAccount(walletdb, firstAccount ? firstAccount : walletInstance->mapAccounts.begin()->second);
            }
            else
                throw std::runtime_error("Wallet contains no accounts, but is marked as upgraded.");
        }
        //fixme: (FUT) (SPV) (LOW) - Remove this eventually
        //Work around issue in early android SPV wallets where if there are multiple accounts we end up with the wrong one active somehow.
        //We should remove this in the future, but only once we are sure it is fully solved.
        else if(firstAccount && (GetBoolArg("-spv", DEFAULT_SPV)) && walletInstance->activeAccount != firstAccount)
        {
            CWalletDB walletdb(*walletInstance->dbw);
            walletInstance->setActiveAccount(walletdb, firstAccount);
        }

        //fixme: (FUT) Remove this in future
        //Clean up an issue with some wallets that accidentally changed mining address, in 2.2.0.0 release.
        {
            LOCK(walletInstance->cs_wallet);
            for (const auto& [accountUUID, forAccount] : walletInstance->mapAccounts)
            {
                if (forAccount->IsMiningAccount())
                {
                    LOCK(walletInstance->cs_wallet);
                    CExtPubKey pubkey;
                    if (static_cast<CAccountHD*>(forAccount)->GetPubKeyManual(0, KEYCHAIN_EXTERNAL, pubkey))
                    {
                        CWalletDB walletdb(*walletInstance->dbw);
                        if (!walletdb.HasPool(walletInstance, pubkey.GetKey().GetID()))
                        {
                            LogPrintf("Restoring mining address for account [%s] to first address\n", forAccount->getLabel());
                            
                            // Find a unique *gap* in the current pool indexes
                            // Needs to be a gap because we need it to be the lowest id key in the mining account
                            // Note this isn't as bad as it looks, because most wallets will have a gap at quite low numbers if they've ever transacted at all
                            // And small keypools if they have not
                            int64_t nIndex = 1;
                            bool foundGap=false;
                            while (!foundGap)
                            {
                                foundGap = true;
                                for (const auto& [loopAccountUUID, loopForAccount] : walletInstance->mapAccounts)
                                {
                                    (unused) loopAccountUUID;
                                    for (auto keyChain : { KEYCHAIN_EXTERNAL, KEYCHAIN_CHANGE })
                                    {
                                        auto& keyPool = ( keyChain == KEYCHAIN_EXTERNAL ? loopForAccount->setKeyPoolExternal : loopForAccount->setKeyPoolInternal );
                                        for (const auto& item : keyPool)
                                        {
                                            if (item == nIndex)
                                            {
                                                ++nIndex;
                                                foundGap = false;
                                                break;
                                            }
                                        }
                                        if (!foundGap)
                                            break;
                                    }
                                    if (!foundGap)
                                            break;
                                }
                            }
                            auto& keyPool = forAccount->setKeyPoolExternal;
                            walletdb.WritePool(++nIndex, CKeyPool(pubkey.GetKey(), getUUIDAsString(accountUUID), KEYCHAIN_EXTERNAL ));
                            keyPool.insert(nIndex);
                        }
                    }    
                }
            }
        }
        
        //Clean up an issue with some wallets that didn't delete one of the seeds correctly
        if (walletInstance->IsCrypted() && walletInstance->mapSeeds.size() > 1)
        {
            bool anyFixed = true;
            while (anyFixed)
            {
                anyFixed = false;
                for (const auto& [seedUUID, seed] : walletInstance->mapSeeds)
                {
                    if (!seed->IsCrypted())
                    {
                        if (seed->m_type == CHDSeed::CHDSeed::BIP32 || seed->m_type == CHDSeed::CHDSeed::BIP32Legacy)
                        {
                            // Erase
                            walletInstance->mapSeeds.erase(walletInstance->mapSeeds.find(seedUUID));
                            if (!CWalletDB(*walletInstance->dbw).DeleteHDSeed(*seed))
                            {
                                throw std::runtime_error("Deleting seed failed");
                            }
                            anyFixed = true;
                            break;
                        }
                        else
                        {
                            std::string strErrorMessage = strprintf("Wallet contains a seed encryption error, this is not immediately dangerous but should be rectified as soon as possible, please contact a developer for assistance");
                            CAlert::Notify(strErrorMessage, true, true);
                            LogPrintf("%s", strErrorMessage.c_str());
                            uiInterface.ThreadSafeMessageBox(strErrorMessage,"", CClientUIInterface::MSG_ERROR);
                            anyFixed = false;
                            break;
                        }
                    }
                }
            }
        }
    }
    else
    {
        throw std::runtime_error("Unknown wallet load state.");
    }

    if (AppLifecycleManager::gApp->isRecovery && AppLifecycleManager::gApp->getRecoveryBirthTime() == 0)
    {
        walletInstance->nTimeFirstKey = Params().GenesisBlock().nTime;
    }

    LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

    RegisterValidationInterface(walletInstance);

    CBlockIndex *pindexRescan = chainActive.Tip();
    if (GetBoolArg("-rescan", false) || AppLifecycleManager::gApp->isRecovery || AppLifecycleManager::gApp->isLink)
    {
        //fixme: (FUT) (SPV) rescan from latest checkpoint before nTimeFirstKey
        pindexRescan = chainActive.Genesis();
    }
    else
    {
        CWalletDB walletdb(*walletInstance->dbw);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = FindForkInGlobalIndex(chainActive, locator);
    }
    if (chainActive.Tip() && chainActive.Tip() != pindexRescan)
    {
        //We can't rescan beyond non-pruned blocks, stop and throw an error
        //this might happen if a user uses a old wallet within a pruned node
        // or if he ran -disablewallet for a longer time, then decided to re-enable
        if (fPruneMode)
        {
            CBlockIndex *block = chainActive.Tip();
            while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA) && block->pprev->nTx > 0 && pindexRescan != block)
                block = block->pprev;

            if (pindexRescan != block) {
                InitError(errortr("Prune: last wallet synchronisation goes beyond pruned data. You need to -reindex (download the whole blockchain again in case of pruned node)"));
                return NULL;
            }
        }

        uiInterface.InitMessage(_("Rescanning..."));
        LogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        //fixme: (FUT) We have to set the active wallet early here otherwise we get a crash inside AddToWalletIfInvolvingMe via ScanForWalletTransactions as pActiveWallet is NULL.
        //Normally active wallet would only be set after this function returns.
        //Look into a re-architecture here.
        pactiveWallet = walletInstance;
        walletInstance->ScanForWalletTransactions(pindexRescan, true);
        LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
        walletInstance->SetBestChain(chainActive.GetLocatorPoW2());
        walletInstance->dbw->IncrementUpdateCounter();

        // Restore wallet transaction metadata after -zapwallettxes=1
        if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2")
        {
            CWalletDB walletdb(*walletInstance->dbw);

            for(const CWalletTx& wtxOld : vWtx)
            {
                uint256 hash = wtxOld.GetHash();
                std::map<uint256, CWalletTx>::iterator mi = walletInstance->mapWallet.find(hash);
                if (mi != walletInstance->mapWallet.end())
                {
                    const CWalletTx* copyFrom = &wtxOld;
                    CWalletTx* copyTo = &mi->second;
                    copyTo->mapValue = copyFrom->mapValue;
                    copyTo->vOrderForm = copyFrom->vOrderForm;
                    copyTo->nTimeReceived = copyFrom->nTimeReceived;
                    copyTo->nTimeSmart = copyFrom->nTimeSmart;
                    copyTo->fFromMe = copyFrom->fFromMe;
                    copyTo->strFromAccount = copyFrom->strFromAccount;
                    copyTo->nOrderPos = copyFrom->nOrderPos;
                    walletdb.WriteTx(*copyTo);
                }
            }
        }
    }
    walletInstance->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", DEFAULT_WALLETBROADCAST));

    {
        LOCK(walletInstance->cs_wallet);
        //fixme: (FUT) (ACCOUNTS) - 'key pool size' concept for wallet doesn't really make sense anymore.
        LogPrintf("setKeyPool.size() = %u\n",      walletInstance->GetKeyPoolSize());
        LogPrintf("mapWallet.size() = %u\n",       walletInstance->mapWallet.size());
        LogPrintf("mapAddressBook.size() = %u\n",  walletInstance->mapAddressBook.size());
    }

    // Ensure we leave the wallet in a locked state
    if (!walletInstance->IsLocked())
    {
        // However don't lock immediately let the shadow pool thread generate accounts still if it needs too
        walletInstance->fAutoLock = true;
        walletInstance->nUnlockSessions++;
        walletInstance->nUnlockedSessionsOwnedByShadow++;
    }

    return walletInstance;
}

void CWallet::CreateSeedAndAccountFromPhrase(CWallet* walletInstance)
{
    // Generate a new primary seed and account (BIP44)
    walletInstance->activeSeed = new CHDSeed(AppLifecycleManager::gApp->getRecoveryPhrase().c_str(), CHDSeed::CHDSeed::BIP44);
    walletInstance->nTimeFirstKey = AppLifecycleManager::gApp->getRecoveryBirthTime();

    SecureString encryptUsingPassword = AppLifecycleManager::gApp->getRecoveryPassword();
    walletInstance->mapSeeds[walletInstance->activeSeed->getUUID()] = walletInstance->activeSeed;
    if (encryptUsingPassword.length() > 0)
    {
        LogPrintf("Encrypting wallet using passphrase\n");
        if (!walletInstance->EncryptWallet(encryptUsingPassword))
            throw std::runtime_error("Failed to encrypt wallet");
        if (!walletInstance->Unlock(encryptUsingPassword))
            throw std::runtime_error("Failed to unlock freshly encrypted wallet");
    }
    walletInstance->activeAccount = walletInstance->GenerateNewAccount("My account", AccountState::Normal, AccountType::Desktop);

    // Now generate children shadow accounts to handle legacy transactions
    // Only for recovery wallets though, new ones don't need them
    #ifdef DJINNI_NODEJS
    //fixme: (UNITY) We seem to unwittingly create these unnecessarily when creating new wallets (because we treat them as a "recovery")
    if (!fSPV && AppLifecycleManager::gApp->isRecovery)
    #else
    if (AppLifecycleManager::gApp->isRecovery)
    #endif
    {
        //fixme: (UNITY) (SPV) extract firstkeytime from recovery
        //Temporary seeds for shadow children
        CHDSeed* seedBip32 = new CHDSeed(AppLifecycleManager::gApp->getRecoveryPhrase().c_str(), CHDSeed::CHDSeed::BIP32);
        CHDSeed* seedBip32Legacy = new CHDSeed(AppLifecycleManager::gApp->getRecoveryPhrase().c_str(), CHDSeed::CHDSeed::BIP32Legacy);

        // If wallet is encrypted we need to encrypt the seeds (even though they are temporary) to ensure that they produce properly encrypted accounts.
        if (walletInstance->IsCrypted())
        {
            if (!seedBip32->Encrypt(walletInstance->activeAccount->vMasterKey)) { throw std::runtime_error("Encrypting bip32 seed failed"); }
            if (!seedBip32Legacy->Encrypt(walletInstance->activeAccount->vMasterKey)) { throw std::runtime_error("Encrypting bip32-legacy seed failed"); }
        }

        // Write new accounts
        CAccountHD* newAccountBip32 = seedBip32->GenerateAccount(AccountType::Desktop, NULL);
        newAccountBip32->m_State = AccountState::ShadowChild;
        walletInstance->activeAccount->AddChild(newAccountBip32);
        walletInstance->addAccount(newAccountBip32, "BIP32 child account");

        // Write new accounts
        CAccountHD* newAccountBip32Legacy = seedBip32Legacy->GenerateAccount(AccountType::Desktop, NULL);
        newAccountBip32Legacy->m_State = AccountState::ShadowChild;
        walletInstance->activeAccount->AddChild(newAccountBip32Legacy);
        walletInstance->addAccount(newAccountBip32Legacy, "BIP32 legacy child account");

        //NB! We intentionally delete these seeds we only want the one account from them (for backwards compatibility with old phones).
        delete seedBip32Legacy;
        delete seedBip32;
    }

    // Write the seed last so that account index changes are reflected
    {
        CWalletDB walletdb(*walletInstance->dbw);
        if (!walletdb.WriteHDSeed(*walletInstance->activeSeed)) { throw std::runtime_error("Writing seed failed"); }
        if (!walletdb.WritePrimarySeed(*walletInstance->activeSeed)) { throw std::runtime_error("Writing primary seed failed"); }
        if (!walletdb.WritePrimaryAccount(walletInstance->activeAccount)) { throw std::runtime_error("Writing active account failed"); }
    }

    AppLifecycleManager::gApp->SecureWipeRecoveryDetails();

    // Assign initial keys to the wallet
    PerformInitialMinimalKeyAllocation(walletInstance);

    //SPV special case - we need to allocate all the addresses now already for better filtering.
    if (GetBoolArg("-spv", DEFAULT_SPV))
    {
        walletInstance->TopUpKeyPool(10);
    }
}

bool CWallet::InitLoadWallet()
{
    LOCK(cs_main);

    if (GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        LogPrintf("Wallet disabled!\n");
        return true;
    }

    for (const std::string& walletFile : gArgs.GetArgs("-wallet")) {
        CWallet * const pwallet = CreateWalletFromFile(walletFile);
        if (!pwallet) {
            return false;
        }
        vpwallets.push_back(pwallet);
        pactiveWallet = pwallet;
    }

    if (fSPV) {
        for (CWallet* pwallet : vpwallets)
            pwallet->StartSPV();
    }

    return true;
}

void CWallet::StartSPV()
{
    LOCK(cs_wallet);
    auto scanner = std::make_unique<CSPVScanner>(*this);
    if (scanner->StartScan())
        pSPVScanner.swap(scanner);
}

void CWallet::ResetSPV()
{
    LOCK(cs_wallet);
    if (pSPVScanner) {
        pSPVScanner->ResetScan();
        pSPVScanner->StartScan();
    }
}

void CWallet::EraseWalletSeedsAndAccounts()
{
    LOCK2(cs_main, cs_wallet);

    if (pSPVScanner)
        pSPVScanner->ResetScan();

    CWalletDB walletdb(*dbw);

    // Purge all current accounts/seeds from the system
    LogPrintf("EraseWalletSeedsAndAccounts: Begin purge seeds [%d]", mapSeeds.size());
    while (!mapSeeds.empty())
    {
        LogPrintf("EraseWalletSeedsAndAccounts: purge seed");
        DeleteSeed(walletdb, pactiveWallet->mapSeeds.begin()->second, true);
    }
    LogPrintf("EraseWalletSeedsAndAccounts: End purge seeds");

    LogPrintf("EraseWalletSeedsAndAccounts: Begin purge standalone accounts [%d]", mapAccounts.size());
    while (!mapAccounts.empty())
    {
        LogPrintf("EraseWalletSeedsAndAccounts: purge account");
        deleteAccount(walletdb, pactiveWallet->mapAccounts.begin()->second, true);
    }
    LogPrintf("EraseWalletSeedsAndAccounts: End purge standalone accounts");

    LogPrintf("EraseWalletSeedsAndAccounts: Begin purge masterkeys [%d]", mapMasterKeys.size());
    while (!mapMasterKeys.empty())
    {
        LogPrintf("EraseWalletSeedsAndAccounts: purge masterkey");
        walletdb.EraseMasterKey(mapMasterKeys.begin()->first);
        mapMasterKeys.erase(mapMasterKeys.begin());
    }
    nMasterKeyMaxID = 0;
    LogPrintf("EraseWalletSeedsAndAccounts: End purge masterkeys");

    walletdb.ErasePrimaryAccount();
    walletdb.EraseLastSPVBlockProcessed();
}

void CWallet::ResetUnifiedSPVProgressNotification()
{
    // TODO refactor spv scanning out of wallet and into validation/net at some point
    // this simplifies architecture a bit and avoids the weird responsibility split
    // which for example leads to this funny bit in here where we need to iterate the wallets
    // to get to the spvscannerS!

    LOCK(cs_main);

    for (CWallet* pwallet : vpwallets)
    {
        LOCK(pwallet->cs_wallet);
        if (pwallet->pSPVScanner)
            pwallet->pSPVScanner->ResetUnifiedProgressNotification();
    }
}

std::atomic<bool> CWallet::fFlushScheduled(false);

void CWallet::postInitProcess(CScheduler& scheduler)
{
    schedulerForLock = &scheduler;

    // Add wallet transactions that aren't already in a block to mempool
    // Do this here as mempool requires genesis block to be loaded
    ReacceptWalletTransactions();

    // Run a thread to flush wallet periodically
    if (!CWallet::fFlushScheduled.exchange(true)) {
        scheduler.scheduleEvery(MaybeCompactWalletDB, std::chrono::milliseconds(500));
    }
}

bool CWallet::ParameterInteraction()
{
    SoftSetArg("-wallet", DEFAULT_WALLET_DAT);
    const bool is_multiwallet = gArgs.GetArgs("-wallet").size() > 1;

    if (GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return true;

    if (GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY) && SoftSetBoolArg("-walletbroadcast", false)) {
        LogPrintf("%s: parameter interaction: -blocksonly=1 -> setting -walletbroadcast=0\n", __func__);
    }

    if (GetBoolArg("-salvagewallet", false) && SoftSetBoolArg("-rescan", true)) {
        if (is_multiwallet) {
            return InitError(strprintf("%s is only allowed with a single wallet file", "-salvagewallet"));
        }
        // Rewrite just private keys: rescan to find transactions
        LogPrintf("%s: parameter interaction: -salvagewallet=1 -> setting -rescan=1\n", __func__);
    }

    // -zapwallettx implies a rescan
    if (GetBoolArg("-zapwallettxes", false) && SoftSetBoolArg("-rescan", true)) {
        if (is_multiwallet) {
            return InitError(strprintf("%s is only allowed with a single wallet file", "-zapwallettxes"));
        }
        LogPrintf("%s: parameter interaction: -zapwallettxes=<mode> -> setting -rescan=1\n", __func__);
    }

    if (is_multiwallet) {
        if (GetBoolArg("-upgradewallet", false)) {
            return InitError(strprintf("%s is only allowed with a single wallet file", "-upgradewallet"));
        }
    }

    if (GetBoolArg("-sysperms", false))
        return InitError("-sysperms is not allowed in combination with enabled wallet functionality");
    if (GetArg("-prune", 0) && GetBoolArg("-rescan", false))
        return InitError(errortr("Rescans are not possible in pruned mode. You will need to use -reindex which will download the whole blockchain again."));

    if (::minRelayTxFee.GetFeePerK() > HIGH_TX_FEE_PER_KB)
        InitWarning(AmountHighWarn("-minrelaytxfee") + " " + warningtr("The wallet will avoid paying less than the minimum relay fee."));

    if (IsArgSet("-mintxfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(GetArg("-mintxfee", ""), n) || 0 == n)
            return InitError(AmountErrMsg("mintxfee", GetArg("-mintxfee", "")));
        if (n > HIGH_TX_FEE_PER_KB)
            InitWarning(AmountHighWarn("-mintxfee") + " " + warningtr("This is the minimum transaction fee you pay on every transaction."));
        CWallet::minTxFee = CFeeRate(n);
    }
    if (IsArgSet("-fallbackfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(GetArg("-fallbackfee", ""), nFeePerK))
            return InitError(strprintf(warningtr("Invalid amount for -fallbackfee=<amount>: '%s'"), GetArg("-fallbackfee", "")));
        if (nFeePerK > HIGH_TX_FEE_PER_KB)
            InitWarning(AmountHighWarn("-fallbackfee") + " " + warningtr("This is the transaction fee you may pay when fee estimates are not available."));
        CWallet::fallbackFee = CFeeRate(nFeePerK);
    }
    if (IsArgSet("-paytxfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(GetArg("-paytxfee", ""), nFeePerK))
            return InitError(AmountErrMsg("paytxfee", GetArg("-paytxfee", "")));
        if (nFeePerK > HIGH_TX_FEE_PER_KB)
            InitWarning(AmountHighWarn("-paytxfee") + " " + warningtr("This is the transaction fee you will pay if you send a transaction."));

        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < ::minRelayTxFee)
        {
            return InitError(strprintf(errortr("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                                       GetArg("-paytxfee", ""), ::minRelayTxFee.ToString()));
        }
    }
    if (IsArgSet("-maxtxfee"))
    {
        CAmount nMaxFee = 0;
        if (!ParseMoney(GetArg("-maxtxfee", ""), nMaxFee))
            return InitError(AmountErrMsg("maxtxfee", GetArg("-maxtxfee", "")));
        if (nMaxFee > HIGH_MAX_TX_FEE)
            InitWarning(warningtr("-maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee = nMaxFee;
        if (CFeeRate(maxTxFee, 1000) < ::minRelayTxFee)
        {
            return InitError(strprintf(errortr("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                                       GetArg("-maxtxfee", ""), ::minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET);
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", DEFAULT_SPEND_ZEROCONF_CHANGE);
    fWalletRbf = GetBoolArg("-walletrbf", DEFAULT_WALLET_RBF);

    if (GetBoolArg("-spv", DEFAULT_SPV)) {
        fSPV = true;
        // When using SPV we don't want normal block download to happen when syncing headers, it could delay SPV scanning
        PreventBlockDownloadDuringHeaderSync(true);
    }

    return true;
}

bool CWallet::BackupWallet(const std::string& strDest)
{
    return dbw->Backup(strDest);
}
