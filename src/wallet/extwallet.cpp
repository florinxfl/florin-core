// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "wallet/wallet.h"
#include "policy/fees.h"
#include "alert.h"
#include "appname.h"
#include "account.h"
#include "script/ismine.h"

#define BOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX
#include <boost/uuid/nil_generator.hpp>
#include <boost/algorithm/string/predicate.hpp> // for starts_with() and end_swith()
#include <boost/algorithm/string.hpp> // for split()
#include <wallet/mnemonic.h>
#include "util.h"
#include "util/thread.h"
#include <validation/validation.h>

bool fShowChildAccountsSeperately = false;

// TODO: consider moving shadow thread functionality into wallet class to reduce usage of global pactiveWallet
// and possibly make some members private.

static void AllocateShadowAccountsIfNeeded(int nAccountPoolTargetSize, int nAccountPoolTargetSizeWitness, int nAccountPoolTargetSizeMobi, int nAccountPoolTargetSizeMining, int& nNumNewAccountsAllocated, bool& tryLockWallet)
{
    // Special SPV optimisation
    // Prevent extra accounts from generating until after we have found the first transaction
    // This keeps our filter ranges smaller and speeds up sync
    if (fSPV && IsInitialBlockDownload() && pactiveWallet->mapAccounts.size() == 1 && pactiveWallet->wtxOrdered.size() == 0)
    {
        tryLockWallet = false;
        return;
    }
    
    for (const auto& seedIter : pactiveWallet->mapSeeds)
    {
        //fixme: (FUT) (ACCOUNTS) (Support other seed types here)
        if (seedIter.second->m_type != CHDSeed::CHDSeed::BIP44 && seedIter.second->m_type != CHDSeed::CHDSeed::BIP44External && seedIter.second->m_type != CHDSeed::CHDSeed::BIP44NoHardening)
            continue;

        for (const auto shadowSubType : { AccountType::Desktop, AccountType::Mobi, AccountType::PoW2Witness, AccountType::MiningAccount })
        {
            int nFinalAccountPoolTargetSize = nAccountPoolTargetSize;
            switch (shadowSubType)
            {
                case AccountType::MiningAccount:
                    nFinalAccountPoolTargetSize = nAccountPoolTargetSizeMining;
                    break;
                case AccountType::PoW2Witness:
                    nFinalAccountPoolTargetSize = nAccountPoolTargetSizeWitness;
                    break;
                case AccountType::Desktop:
                case AccountType::Mobi:
                case AccountType::WitnessOnlyWitnessAccount:
                case AccountType::ImportedPrivateKeyAccount:
                    nFinalAccountPoolTargetSize = nAccountPoolTargetSizeMobi;
                    break;
            }
            int numShadow = 0;
            {
                for (const auto& accountPair : pactiveWallet->mapAccounts)
                {
                    if (accountPair.second->IsHD() && ((CAccountHD*)accountPair.second)->getSeedUUID() == seedIter.second->getUUID())
                    {
                        if (accountPair.second->m_Type == shadowSubType)
                        {
                            if (accountPair.second->m_State == AccountState::Shadow)
                            {
                                ++numShadow;
                            }
                        }
                    }
                }
            }
            if (numShadow < nFinalAccountPoolTargetSize)
            {
                CWalletDB db(*pactiveWallet->dbw);
                while (numShadow < nFinalAccountPoolTargetSize)
                {
                    // New shadow account
                    CAccountHD* newShadow = seedIter.second->GenerateAccount(shadowSubType, &db);

                    if (newShadow == NULL)
                    {
                        // Only explicitely ask for an unlock if really low on shadow accounts
                        if ((shadowSubType != AccountType::MiningAccount) && (numShadow < std::max(nFinalAccountPoolTargetSize, 2)))
                        {
                            tryLockWallet = false;
                        }
                        return;
                    }

                    ++numShadow;
                    ++nNumNewAccountsAllocated;

                    newShadow->m_State = AccountState::Shadow;

                    // Write new account
                    pactiveWallet->addAccount(newShadow, "Shadow_"+GetAccountTypeString(shadowSubType));

                    if (nNumNewAccountsAllocated > 4)
                        return;
                }
            }
        }
    }
}

static void ThreadShadowPoolManager()
{
    static bool promptOnceForAccountGenerationUnlock = true;
    static bool promptOnceForAddressGenerationUnlock = true;
    int depth = 1;
    int nAccountPoolTargetSize = GetArg("-accountpool", 10);
    int nAccountPoolTargetSizeWitness = GetArg("-accountpool", 2);
    if (IsArgSet("-accountpoolwitness"))
    {
        nAccountPoolTargetSizeWitness = GetArg("-accountpoolwitness", nAccountPoolTargetSizeWitness);
    }
    int nAccountPoolTargetSizeMobi = nAccountPoolTargetSize;
    if (IsArgSet("-accountpoolmobi"))
    {
        nAccountPoolTargetSizeMobi = GetArg("-accountpoolmobi", nAccountPoolTargetSizeMobi);
    }
    int nAccountPoolTargetSizeMining = 1;
    if (IsArgSet("-accountpoolmining"))
    {
        nAccountPoolTargetSizeMining = GetArg("-accountpoolmining", nAccountPoolTargetSizeMining);
    }
    
    int nKeyPoolTargetDepth = GetArg("-keypool", DEFAULT_ACCOUNT_KEYPOOL_SIZE);
    while (true)
    {
        long milliSleep = 500;
        bool tryLockWallet = true;

        if (pactiveWallet)
        {
            LOCK2(cs_main, pactiveWallet->cs_wallet);

            int nNumNewAccountsAllocated = 0;

            // First we expand the amount of shadow accounts until we have the desired amount.
            AllocateShadowAccountsIfNeeded(nAccountPoolTargetSize, nAccountPoolTargetSizeWitness, nAccountPoolTargetSizeMobi, nAccountPoolTargetSizeMining, nNumNewAccountsAllocated, tryLockWallet);
            if (nNumNewAccountsAllocated > 0)
            {
                // Reset the depth to 1, so that our new accounts get their first keys as a priority instead of expanding deeper the other accounts.
                depth = 1;
                milliSleep = 100;
                continue;
            }
            else if (!tryLockWallet)
            {
                // If we reach here we need to unlock to generate more background shadow accounts.
                // So try to initiate an unlock session
                tryLockWallet = false;
                if (promptOnceForAccountGenerationUnlock)
                {
                    promptOnceForAccountGenerationUnlock = false;

                    // If the user cancels then we don't prompt him for this again in this program run.
                    // If user performs the unlock then we leave prompting enabled in case we reach this situation again.
                    //fixme: (FUT) (ACCOUNTS) - Should this maybe be a timer to only prompt once a day or something for users who keep the program open?
                    // Might also want a "don't ask me this again" checkbox on prompt etc.
                    // Discuss with UI team and reconsider how to handle this.

                    pactiveWallet->BeginUnlocked(_("Wallet unlock required for account generation"), [&]() {
                        promptOnceForAccountGenerationUnlock = true;
                        // transfer ownership of unlock session to shadow thread
                        LOCK(pactiveWallet->cs_wallet);
                        pactiveWallet->nUnlockedSessionsOwnedByShadow++;
                    });
                    milliSleep = 1;
                }
            }

            // Once we have allocated all the shadow accounts (or if wallet is locked and unable to allocate shadow accounts) we start to allocate keys for the accounts.
            // Allocate up to 'depth' keys for all accounts, however only do numToAllocatePerRound allocations at a time before sleeping so that we don't hog the entire CPU.
            int targetPoolDepth = nKeyPoolTargetDepth;
            int numToAllocatePerRound = 5;
            if (targetPoolDepth > 40)
                numToAllocatePerRound = 20;
            int numAllocated = pactiveWallet->TopUpKeyPool(depth, numToAllocatePerRound);
            // Increase the depth for next round of allocation.
            if (numAllocated == 0 && depth < targetPoolDepth)
            {
                ++depth;
                // If we didn't allocate any then we want to try again almost immediately and not have a long sleep.
                milliSleep = 1;
            }
            else if (numAllocated >= 0 || depth < targetPoolDepth)
            {
                // Otherwise we sleep for increasingly longer depending on how deep into the allocation we are, the deeper we are the less urgent it becomes to allocate more. 
                //fixme: (FUT) (ACCOUNTS) Look some more into these times, they are a bit arbitrary.
                // If the user has set an especially large depth then we want to try again almost immediately and not have a long sleep.
                if (targetPoolDepth > 40)
                    milliSleep = 1;
                else if (depth < 10) 
                    milliSleep = 80;
                else if (depth < 20)
                    milliSleep = 100;
                else
                    milliSleep = 300;
            }
            else
            {
                // If we reach here we have a non HD account that require wallet unlock to allocate keys.
                // So try to initiate an unlock session
                milliSleep = 500;
                tryLockWallet = false;
                if (promptOnceForAddressGenerationUnlock)
                {
                    promptOnceForAddressGenerationUnlock = false;
                    // If the user cancels then we don't prompt him for this again in this program run.
                    // If user performs the unlock then we leave prompting enabled in case we reach this situation again.
                    //fixme: (FUT) (ACCOUNTS) - Should this maybe be a timer to only prompt once a day or something for users who keep the program open?
                    // Might also want a "don't ask me this again" checkbox on prompt etc.
                    // Discuss with UI team and reconsider how to handle this.

                    pactiveWallet->BeginUnlocked(_("Wallet unlock required for address generation"), [&]() {
                        promptOnceForAddressGenerationUnlock = true;
                        // transfer ownership of unlock session to shadow thread
                        LOCK(pactiveWallet->cs_wallet);
                        pactiveWallet->nUnlockedSessionsOwnedByShadow++;
                    });
                }
            }

            // If we no longer have a need for an unlocked wallet (ie. all work done) end all unlock sessions that we own.
            if (tryLockWallet)
            {
                for (unsigned int i = 0; i < pactiveWallet->nUnlockedSessionsOwnedByShadow; i++) {
                    pactiveWallet->EndUnlocked();
                }
                pactiveWallet->nUnlockedSessionsOwnedByShadow = 0;
            }
        }

        boost::this_thread::interruption_point();
        MilliSleep(milliSleep);
    }
}

void StartShadowPoolManagerThread(boost::thread_group& threadGroup)
{
    threadGroup.create_thread(boost::bind(&util::TraceThread, "shadowpoolmanager", &ThreadShadowPoolManager));
}

std::string accountNameForAddress(const CWallet &wallet, const CTxDestination& dest)
{
    LOCK(wallet.cs_wallet);
    std::string accountNameForAddress;

    isminetype ret = isminetype::ISMINE_NO;
    for (const auto& accountItem : wallet.mapAccounts)
    {
        for (auto keyChain : { KEYCHAIN_EXTERNAL, KEYCHAIN_CHANGE })
        {
            isminetype temp = ( keyChain == KEYCHAIN_EXTERNAL ? IsMine(accountItem.second->externalKeyStore, dest) : IsMine(accountItem.second->internalKeyStore, dest) );
            if (temp > ret)
            {
                ret = temp;
                accountNameForAddress = accountItem.second->getLabel();
            }
        }
    }
    if (ret < isminetype::ISMINE_WATCH_ONLY)
        return "";
    return accountNameForAddress;
}

isminetype IsMine(const CWallet &wallet, const CTxDestination& dest)
{
    LOCK(wallet.cs_wallet);

    isminetype ret = isminetype::ISMINE_NO;
    for (const auto& accountItem : wallet.mapAccounts)
    {
        for (auto keyChain : { KEYCHAIN_EXTERNAL, KEYCHAIN_CHANGE })
        {
            isminetype temp = ( keyChain == KEYCHAIN_EXTERNAL ? IsMine(accountItem.second->externalKeyStore, dest) : IsMine(accountItem.second->internalKeyStore, dest) );
            if (temp > ret)
                ret = temp;
        }
    }
    return ret;
}

isminetype IsMine(const CWallet &wallet, const CTxOut& out)
{
    LOCK(wallet.cs_wallet);

    isminetype ret = isminetype::ISMINE_NO;
    for (const auto& [accountUUID, account] : wallet.mapAccounts)
    {
        (unused)accountUUID;
        isminetype temp = IsMine(*account, out);
        if (temp > ret)
        {
            ret = temp;
        }
        // No need to keep going through the remaining accounts at this point.
        if (ret >= ISMINE_SPENDABLE)
            return ret;
    }
    return ret;
}

isminetype IsMineWitness(const CWallet &wallet, const CTxOut& out)
{
    LOCK(wallet.cs_wallet);

    isminetype ret = isminetype::ISMINE_NO;
    for (const auto& [accountUUID, account] : wallet.mapAccounts)
    {
        if (account->IsPoW2Witness() && account->m_State == AccountState::Normal)
        {
            (unused)accountUUID;
            isminetype temp = IsMine(*account, out);
            if (temp > ret)
            {
                ret = temp;
            }
            // No need to keep going through the remaining accounts at this point.
            if (ret >= ISMINE_SPENDABLE)
                return ret;
        }
    }
    return ret;
}

bool IsMine(const CKeyStore* forAccount, const CWalletTx& tx)
{
    for (const auto& txout : tx.tx->vout)
    {
        isminetype ret = IsMine(*forAccount, txout);
        // No need to keep going through the remaining outputs at this point.
        if (ret > isminetype::ISMINE_NO)
            return true;
        //NB! We don't insert into the "IsNotMineCache" here as it is for the whole wallet
        //This output could still belong to a different account...
    }
    return false;
}

isminetype CExtWallet::IsMine(const CKeyStore &keystore, const CTxIn& txin) const
{
    {
        LOCK(cs_wallet);
        uint256 txHash;
        if (GetTxHash(txin.GetPrevOut(), txHash))
        {
            const CWalletTx* prev = pactiveWallet->GetWalletTx(txHash);
            if (prev && prev->tx->vout.size() != 0)
            {
                if (txin.GetPrevOut().n < prev->tx->vout.size())
                    return ::IsMine(keystore, prev->tx->vout[txin.GetPrevOut().n]);
            }
        }
    }
    return ISMINE_NO;
}

void CExtWallet::MarkKeyUsed(CKeyID keyID, uint64_t usageTime)
{
    {
        // Remove from key pool
        CWalletDB walletdb(*dbw);
        //NB! Must call ErasePool here even if HasPool is false - as ErasePool has other side effects.
        walletdb.ErasePool(static_cast<CWallet*>(this), keyID);
    }

    //Update accounts if needed (creation time - shadow accounts etc.)
    {
        LOCK(cs_wallet);
        for (const auto& accountIter : mapAccounts)
        {
            auto& forAccount = accountIter.second;
            if (forAccount->HaveKey(keyID))
            {
                if (usageTime > 0)
                {
                    CWalletDB walletdb(*dbw);
                    forAccount->possiblyUpdateEarliestTime(usageTime, &walletdb);
                }

                // We only do this the first time MarkKeyUsed is called - otherwise we have the following problem
                // 1) User empties account. 2) User deletes account 3) At a later point MarkKeyUsed is called subsequent times (new blocks) 4) The account user just deleted is now recovered.

                //fixme: (FUT) (ACCOUNTS) This is still not 100% right, if the user does the following there can still be issues:
                //1) Send funds from account
                //2) Immediately close wallet
                //3) Reopen wallet, as the new blocks are processed this code will be called and the just deleted account will be restored.
                //We will need a better way to detect this...

                //fixme: (FUT) (ACCOUNTS)
                //Another edge bug here
                //1) User sends/receives from address
                //2) User deleted account
                //3) User receives on same address again
                //Account will not reappear - NB! This happens IFF there is no wallet restart anywhere in this process, and the user can always rescan still so it's not a disaster.
                static std::set<CKeyID> keyUsedSet;
                if (keyUsedSet.find(keyID) == keyUsedSet.end())
                {
                    keyUsedSet.insert(keyID);

                    if (forAccount->IsMiningAccount())
                    {
                        CKeyID highestKeyID;
                        //fixme: (HIGH) (KEYPOOL) All HD accounts should use a similar (not identical) method to this
                        //To obtain a proper key gap instead of what we have now
                        //Probably we should be storing the 'highest used key' somewhere in this function
                        //And then topupkeypool should utilise it for allocation (instead of the allocation trick we use here)
                        if (dynamic_cast<CAccountHD*>(forAccount)->GetAccountKeyIDWithHighestIndex(highestKeyID, KEYCHAIN_EXTERNAL) && (highestKeyID == keyID))
                        {
                            // Assign 1 extra key, because mining accounts never discard keys the keypool size always grows when new keys are allocated
                            // Ideally most mining accounts will only have 1 key, but due to a previous bug some have more
                            LOCK(forAccount->cs_keypool);
                            uint64_t topupSize = forAccount->GetKeyPoolSize(KEYCHAIN_EXTERNAL)+1;
                            static_cast<CWallet*>(this)->TopUpKeyPool(topupSize, 10, forAccount, topupSize);
                        }
                    }

                    if (forAccount->m_State != AccountState::Normal && forAccount->m_State != AccountState::ShadowChild)
                    {
                        forAccount->m_State = AccountState::Normal;
                        std::string name = forAccount->getLabel();

                        //fixme: (FUT) (ACCOUNTS) remove this in name delete/restore labelling for something less error prone. (translations would break this for instance)
                        //We should just set a restored attribute on the account or something.
                        if (name.find(_("[Deleted]")) != std::string::npos)
                        {
                            name = name.replace(name.find(_("[Deleted]")), _("[Deleted]").length(), _("[Restored]"));
                        }
                        else
                        {
                            name = _("Restored");
                            switch(forAccount->m_Type)
                            {
                                case AccountType::MiningAccount:
                                    name += " mining";
                                    break;
                                case AccountType::PoW2Witness:
                                    name += " holding";
                                    break;
                                case AccountType::Mobi:
                                    name += " mobile";
                                    break;
                                case AccountType::Desktop:
                                case AccountType::WitnessOnlyWitnessAccount:
                                case AccountType::ImportedPrivateKeyAccount:
                                    break;
                            }
                        }
                        // Add the account, don't let it steal focus (this can create issues on e.g. mobile wallets where the UI/Unity lib expects a single account to always be the active one)
                        addAccount(forAccount, name, false);

                        //fixme: (FUT) (ACCOUNTS) Shadow accounts during rescan...
                    }

                    if (forAccount->IsHD() && forAccount->IsPoW2Witness())
                    {
                        //This is here for the sake of restoring wallets from recovery phrase only, in the normal case this has already been done by the funding code...
                        //fixme: (FUT) (ACCOUNTS) Improve this, there are two things that need improving:
                        //1) When restoring from phrase but also encrypting and locking - it won't be able to add the key here.
                        //We try to work around this by using an unlock callback, but if the user refuses to unlock then there might be issues.
                        //2) This will indescriminately add -all- used change keys in a witness account; even if used for normal transactions (which shouldn't be done, but still it would be preferable to avoid this)
                        //Note as witness-only accounts are not HD this is not an issue for witness-only accounts.
                        if (forAccount->getLabel().find(_("[Restored]")) != std::string::npos)
                        {
                            if (forAccount->HaveKeyInternal(keyID))
                            {
                                std::function<void (void)> witnessKeyCallback = [=]()
                                {
                                    CKey privWitnessKey;
                                    if (!forAccount->GetKey(keyID, privWitnessKey))
                                    {
                                        //fixme: (FUT) localise
                                        std::string strErrorMessage = "Failed to mark witnessing key for encrypted usage";
                                        LogPrintf(strErrorMessage.c_str());
                                        CAlert::Notify(strErrorMessage, true, true);
                                    }
                                    if (!static_cast<CWallet*>(this)->AddKeyPubKey(privWitnessKey, privWitnessKey.GetPubKey(), *forAccount, KEYCHAIN_WITNESS))
                                    {
                                        //fixme: (FUT) localise
                                        std::string strErrorMessage = "Failed to mark witnessing key for encrypted usage";
                                        LogPrintf(strErrorMessage.c_str());
                                        CAlert::Notify(strErrorMessage, true, true);
                                    }
                                };
                                if (forAccount->IsLocked())
                                {
                                    //Last ditch effort to try work around 1.
                                    uiInterface.RequestUnlockWithCallback(pactiveWallet, _("Wallet unlock required for witness key"), witnessKeyCallback);
                                    continue;
                                }
                                else
                                {
                                    witnessKeyCallback();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    //Only assign the bare minimum keys - let the background thread do the rest.
    #ifdef DJINNI_NODEJS
    if (fSPV)
    {
        static_cast<CWallet*>(this)->TopUpKeyPool(10);
    }
    else
    #endif
    {
        static_cast<CWallet*>(this)->TopUpKeyPool(1);
    }
}

void CExtWallet::changeAccountName(CAccount* account, const std::string& newName, bool notify)
{
    // Force names to be unique
    std::string finalNewName = newName;
    std::string oldName = account->getLabel();
    {
        LOCK(cs_wallet);

        CWalletDB db(*dbw);

        int nPrefix = 1;
        bool possibleDuplicate = true;
        while(possibleDuplicate)
        {
            possibleDuplicate = false;
            for (const auto& labelIter : mapAccountLabels)
            {
                if (labelIter.second == finalNewName)
                {
                    finalNewName = newName + strprintf("_%d", nPrefix++);
                    possibleDuplicate = true;
                    continue;
                }
            }
        }

        account->setLabel(finalNewName, &db);
        mapAccountLabels[account->getUUID()] = finalNewName;
    }

    if (notify && oldName != finalNewName)
        NotifyAccountNameChanged(static_cast<CWallet*>(this), account);
}

void CExtWallet::deleteAccount(CWalletDB& db, CAccount* account, bool shouldPurge)
{
    LogPrintf("CExtWallet::deleteAccount");
    //fixme: (FUT) (ACCOUNTS) - If we are trying to delete the last remaining account we should return false
    //As this may leave the wallet in an invalid state.
    //Alternatively we must make sure that the wallet can handle having 0 accounts in it.
    if (shouldPurge)
    {
        LogPrintf("CExtWallet::deleteAccount - purge account");

        // We should never be purging an HD account unless we have also purged the seed that it belongs too (or it has no seed)
        // It makes no sense to purge an account while retaining the seed as it can always be regenerated from the seed (so purging is still not permanent) and it can only lead to difficult to understand issues.
        if (account->IsHD() && (mapSeeds.find(dynamic_cast<CAccountHD*>(account)->getSeedUUID()) != mapSeeds.end()))
            assert(0);

        LOCK2(cs_main, cs_wallet);

        LogPrintf("CExtWallet::deleteAccount - wipe account from disk");
        if (!db.EraseAccount(getUUIDAsString(account->getUUID()), account))
        {
            throw std::runtime_error("erasing account failed");
        }
        if (!db.EraseAccountLabel(getUUIDAsString(account->getUUID())))
        {
            throw std::runtime_error("erasing account failed");
        }
        if (!db.EraseAccountCompoundingSettings(getUUIDAsString(account->getUUID())))
        {
            throw std::runtime_error("erasing account failed");
        }
        if (!db.EraseAccountNonCompoundWitnessEarningsScript(getUUIDAsString(account->getUUID())))
        {
            throw std::runtime_error("erasing account failed");
        }

        // Wipe all the keys
        LogPrintf("CExtWallet::deleteAccount - wipe keys from disk");
        {
            std::set<CKeyID> setAddress;
            account->GetKeys(setAddress);

            for (const auto& keyID : setAddress)
            {
                CPubKey pubKey;
                if (!GetPubKey(keyID, pubKey))
                {
                    LogPrintf("deleteAccount: Failed to get pubkey\n");
                }
                else
                {
                    if (IsCrypted())
                    {
                        db.EraseEncryptedKey(pubKey);
                    }
                    db.EraseKey(pubKey);
                }
            }
        }

        LogPrintf("CExtWallet::deleteAccount - wipe keypool from disk");
        // Wipe our keypool
        {
            bool forceErase = true;
            for(int64_t nIndex : account->setKeyPoolInternal)
                db.ErasePool(pactiveWallet, nIndex, forceErase);
            for(int64_t nIndex : account->setKeyPoolExternal)
                db.ErasePool(pactiveWallet, nIndex, forceErase);

            account->setKeyPoolInternal.clear();
            account->setKeyPoolExternal.clear();
        }

        // Wipe all the transactions associated with account
        LogPrintf("CExtWallet::deleteAccount - wipe transactions");
        {
            std::vector<uint256> hashesToErase;
            std::vector<uint256> hashesErased;
            for (const auto& [txHash, tx] : pactiveWallet->mapWallet)
            {
                if (::IsMine(account, tx))
                {
                    hashesToErase.push_back(txHash);
                    auto range = wtxOrdered.equal_range(tx.nOrderPos);
                    for (auto i = range.first; i != range.second;)
                    {
                        if (tx.GetHash() == i->second.first->GetHash())
                            i = wtxOrdered.erase(i);
                        else
                            i++;
                    }
                }
            }
            pactiveWallet->ZapSelectTx(db, hashesToErase, hashesErased);
        }

        LogPrintf("CExtWallet::deleteAccount - wipe account from memory");
        mapAccountLabels.erase(mapAccountLabels.find(account->getUUID()));
        mapAccounts.erase(mapAccounts.find(account->getUUID()));

        // Make sure we are no longer the active account
        if(!getActiveAccount() || (getActiveAccount()->getUUID() == account->getUUID()))
        {
            if (mapAccounts.size() > 0)
            {
                setAnyActiveAccount(db);
            }
        }

        //fixme: (FUT) (ACCOUNTS) (LOW) - this leaks until program exit
        //We can't easily delete the account as other places may still be referencing it...
        // Let UI handle deletion [hide account etc.] (we can't actually delete the account pointer because of this so we leak)
        NotifyAccountDeleted(static_cast<CWallet*>(this), account);
    }
    else
    {
        std::string newLabel = account->getLabel();
        if (newLabel.find(_("[Restored]")) != std::string::npos)
        {
            newLabel = newLabel.replace(newLabel.find(_("[Restored]")), _("[Restored]").length(), _("[Deleted]"));
        }
        else
        {
            newLabel = newLabel + " " + _("[Deleted]");
        }

        {
            LOCK(cs_wallet);

            account->setLabel(newLabel, &db);
            account->m_State = AccountState::Deleted;
            mapAccountLabels[account->getUUID()] = newLabel;
            if (!db.WriteAccount(getUUIDAsString(account->getUUID()), account))
            {
                throw std::runtime_error("Writing account failed");
            }
        }
        NotifyAccountDeleted(static_cast<CWallet*>(this), account);
    }
}

void CExtWallet::addAccount(CAccount* account, const std::string& newName, bool bMakeActive)
{
    {
        LOCK(cs_wallet);

        CWalletDB walletdb(*dbw);
        if (!walletdb.WriteAccount(getUUIDAsString(account->getUUID()), account))
        {
            throw std::runtime_error("Writing account failed");
        }
        mapAccounts[account->getUUID()] = account;
        changeAccountName(account, newName, false);
    }
    NotifyAccountAdded(static_cast<CWallet*>(this), account);

    if (account->m_State == AccountState::Normal)
    {
        if (bMakeActive)
        {
            CWalletDB walletdb(*dbw);
            setActiveAccount(walletdb, account);
        }
    }
}

void CExtWallet::setActiveAccount(CWalletDB& walletdb, CAccount* newActiveAccount)
{
    if (activeAccount != newActiveAccount)
    {
        activeAccount = newActiveAccount;
        walletdb.WritePrimaryAccount(activeAccount);

        NotifyActiveAccountChanged(static_cast<CWallet*>(this), newActiveAccount);
    }
}

void CExtWallet::setAnyActiveAccount(CWalletDB& walletdb)
{
    for (const auto& [accountUUID, account] : pactiveWallet->mapAccounts)
    {
        (unused)accountUUID;
        if (account->m_State == AccountState::Normal)
        {
            setActiveAccount(walletdb, account);
            return;
        }
    }
}

CAccount* CExtWallet::getActiveAccount()
{
    return activeAccount;
}

void CExtWallet::setActiveSeed(CWalletDB& walletdb, CHDSeed* newActiveSeed)
{
    if (activeSeed != newActiveSeed)
    {
        activeSeed = newActiveSeed;
        if (activeSeed)
            walletdb.WritePrimarySeed(*activeSeed);
        else
            walletdb.ErasePrimarySeed();

        //fixme: (FUT) (ACCOUNTS) (LOW)
        //NotifyActiveSeedChanged(this, newActiveAccount);
    }
}

CHDSeed* CExtWallet::GenerateHDSeed(CHDSeed::SeedType seedType)
{
    if (IsCrypted() && (!activeAccount || IsLocked()))
    {
        throw std::runtime_error("Generating seed requires unlocked wallet");
    }

    std::vector<unsigned char> entropy(16);
    GetStrongRandBytes(&entropy[0], 16);
    CHDSeed* newSeed = new CHDSeed(mnemonicFromEntropy(entropy, entropy.size()*8).c_str(), seedType);
    if (!CWalletDB(*dbw).WriteHDSeed(*newSeed))
    {
        throw std::runtime_error("Writing seed failed");
    }
    if (IsCrypted())
    {
        if (!newSeed->Encrypt(activeAccount->vMasterKey))
        {
            throw std::runtime_error("Encrypting seed failed");
        }
    }
    mapSeeds[newSeed->getUUID()] = newSeed;

    return newSeed;
}

void CExtWallet::DeleteSeed(CWalletDB& walletDB, CHDSeed* deleteSeed, bool shouldPurgeAccounts)
{
    LogPrintf("CExtWallet::DeleteSeed");
    mapSeeds.erase(mapSeeds.find(deleteSeed->getUUID()));
    if (!walletDB.DeleteHDSeed(*deleteSeed))
    {
        throw std::runtime_error("Deleting seed failed");
    }

    //fixme: (FUT) (ACCOUNTS) purge accounts completely if empty?
    LogPrintf("CExtWallet::DeleteSeed - testing which accounts to delete [%d]", pactiveWallet->mapAccounts.size());
    std::vector<CAccount*> deleteAccounts;
    for (const auto& accountPair : pactiveWallet->mapAccounts)
    {
        if (accountPair.second->IsHD() && ((CAccountHD*)accountPair.second)->getSeedUUID() == deleteSeed->getUUID())
        {
            deleteAccounts.push_back(accountPair.second);
        }
    }
    LogPrintf("CExtWallet::DeleteSeed - Deleting  accounts [%d]", deleteAccounts.size());
    for (const auto& accountForDeletion : deleteAccounts)
    {
        //fixme: (FUT) (ACCOUNTS) check balance
        deleteAccount(walletDB, accountForDeletion, shouldPurgeAccounts);
    }
    LogPrintf("CExtWallet::DeleteSeed - done deleting accounts");

    if (activeSeed == deleteSeed)
    {
        LogPrintf("CExtWallet::DeleteSeed - set active seed to new seed");
        if (mapSeeds.empty())
        {
            LogPrintf("CExtWallet::DeleteSeed - set NULL active seed");
            setActiveSeed(walletDB, NULL);
        }
        else
        {
            LogPrintf("CExtWallet::DeleteSeed - setting first mapped seed as active");
            setActiveSeed(walletDB, mapSeeds.begin()->second);
        }
    }

    LogPrintf("CExtWallet::DeleteSeed - Finished");
    delete deleteSeed;
}

CHDSeed* CExtWallet::ImportHDSeedFromPubkey(SecureString pubKeyString)
{
    if (IsCrypted() && (!activeAccount || IsLocked()))
    {
        throw std::runtime_error("Generating seed requires unlocked wallet");
    }

    CExtPubKey pubkey;
    try
    {
        CEncodedSecretKeyExt<CExtPubKey> secretExt;
        secretExt.SetString(pubKeyString.c_str());
        pubkey = secretExt.GetKeyFromString();
    }
    catch(...)
    {
        throw std::runtime_error("Not a valid " GLOBAL_APPNAME " extended public key");
    }

    if (!pubkey.pubkey.IsValid())
    {
        throw std::runtime_error("Not a valid " GLOBAL_APPNAME " extended public key");
    }

    CHDSeed* newSeed = new CHDSeed(pubkey, CHDSeed::CHDSeed::BIP44NoHardening);
    if (!CWalletDB(*dbw).WriteHDSeed(*newSeed))
    {
        throw std::runtime_error("Writing seed failed");
    }
    if (IsCrypted())
    {
        if (!newSeed->Encrypt(activeAccount->vMasterKey))
        {
            throw std::runtime_error("Encrypting seed failed");
        }
    }
    mapSeeds[newSeed->getUUID()] = newSeed;

    return newSeed;
}

CHDSeed* CExtWallet::ImportHDSeed(SecureString mnemonic, CHDSeed::SeedType type)
{
    if (IsCrypted() && (!activeAccount || IsLocked()))
    {
        throw std::runtime_error("Generating seed requires unlocked wallet");
    }

    if (!checkMnemonic(mnemonic))
    {
        throw std::runtime_error("Not a valid " GLOBAL_APPNAME " mnemonic");
    }

    std::vector<unsigned char> entropy(16);
    GetStrongRandBytes(&entropy[0], 16);
    CHDSeed* newSeed = new CHDSeed(mnemonic, type);
    if (!CWalletDB(*dbw).WriteHDSeed(*newSeed))
    {
        throw std::runtime_error("Writing seed failed");
    }
    if (IsCrypted())
    {
        if (!newSeed->Encrypt(activeAccount->vMasterKey))
        {
            throw std::runtime_error("Encrypting seed failed");
        }
    }
    mapSeeds[newSeed->getUUID()] = newSeed;

    return newSeed;
}


CHDSeed* CExtWallet::getActiveSeed()
{
    return activeSeed;
}

void CExtWallet::RemoveAddressFromKeypoolIfIsMine(const CTxOut& txout, uint64_t time)
{
    ::RemoveAddressFromKeypoolIfIsMine(*static_cast<CWallet*>(this), txout, time);
}


void CExtWallet::RemoveAddressFromKeypoolIfIsMine(const CTransaction& tx, uint64_t time)
{
    for(const CTxOut& txout : tx.vout)
    {
        RemoveAddressFromKeypoolIfIsMine(txout, time);
    }
}

void CExtWallet::RemoveAddressFromKeypoolIfIsMine(const CTxIn& txin, uint64_t time)
{
    LOCK(cs_wallet);
    const CWalletTx* prev = static_cast<CWallet*>(this)->GetWalletTx(txin.GetPrevOut());
    if (prev)
    {
        if (txin.GetPrevOut().n < prev->tx->vout.size())
            RemoveAddressFromKeypoolIfIsMine(prev->tx->vout[txin.GetPrevOut().n], time);
    }
}

// Shadow accounts... For HD we keep a 'cache' of already created accounts, the reason being that another shared wallet might create new addresses, and we need to be able to detect those.
// So we keep these 'shadow' accounts, and if they ever receive a transaction we automatically 'convert' them into normal accounts in the UI.
// If/When the user wants new accounts, we hand out the previous shadow account and generate a new Shadow account to take it's place...
CAccountHD* CExtWallet::GenerateNewAccount(std::string strAccount, AccountState state, AccountType subType, bool bMakeActive)
{
    assert(state != AccountState::ShadowChild);
    assert(state != AccountState::Deleted);
    CAccountHD* newAccount = NULL;

    // Grab account with lowest index from existing pool of shadow accounts and convert it into a new account.
    if (state == AccountState::Normal || state == AccountState::Shadow)
    {
        LOCK(cs_wallet);

        for (const auto& [accountUUID, forAccount] : mapAccounts)
        {
            (unused) accountUUID;
            if (forAccount->m_Type == subType)
            {
                if (forAccount->m_State == AccountState::Shadow)
                {
                    if (!newAccount || ((CAccountHD*)forAccount)->getIndex() < newAccount->getIndex())
                    {
                        if (((CAccountHD*)forAccount)->getSeedUUID() == getActiveSeed()->getUUID())
                        {
                            //Only consider accounts that are
                            //1) Of the required state
                            //2) Marked as being shadow
                            //3) From the active seed
                            //4) Always take the lowest account index that we can find
                            newAccount = (CAccountHD*)forAccount;
                        }
                    }
                }
            }
        }
    }

    // Create a new account in the (unlikely) event there was no shadow state
    if (!newAccount)
    {
        if (!IsLocked())
        {
            CWalletDB db(*dbw);
            newAccount = activeSeed->GenerateAccount(subType, &db);
        }
        else
        {
            return nullptr;
        }
    }
    newAccount->m_State = state;

    // We don't top up the shadow pool here - we have a thread for that.

    // Write new account
    //NB! We have to do this -after- we create the new shadow account, otherwise the shadow account ends up being the active account.
    addAccount(newAccount, strAccount, bMakeActive);

    if (subType == AccountType::PoW2Witness)
    {
        newAccount->SetWarningState(AccountStatus::WitnessEmpty);
    }
    else
    {
        newAccount->SetWarningState(AccountStatus::Default);
    }

    // Shadow accounts have less keys - so we need to top up the keypool for our new 'non shadow' account at this point.
    if( activeAccount ) //fixme: (FUT) (ACCOUNTS) IsLocked() requires activeAccount - so we avoid calling this if activeAccount not yet set.
        static_cast<CWallet*>(this)->TopUpKeyPool(1, 0, activeAccount);//We only assign the bare minimum addresses here - and let the background thread do the rest

    return newAccount;
}

CAccount* CExtWallet::GenerateNewLegacyAccount(std::string strAccount)
{
    CAccount* newAccount = new CAccount();
    //fixme: (FUT) (ACCOUNTS) Improve the way encryption of legacy accounts is handled
    if (IsCrypted())
    {
        LOCK2(cs_main, cs_wallet);
        if (IsLocked())
            return nullptr;

        if (!activeAccount)
            return nullptr;

        if (!newAccount->Encrypt(activeAccount->vMasterKey))
            return nullptr;
    }
    addAccount(newAccount, strAccount);

    return newAccount;
}

bool CExtWallet::ImportKeysIntoWitnessOnlyWitnessAccount(CAccount* forAccount, std::vector<std::pair<CKey, uint64_t>> privateWitnessKeysWithBirthDates, bool allowRescan)
{
    if (!forAccount)
        return false;

    if (forAccount->m_Type != WitnessOnlyWitnessAccount)
        return false;

    //Don't import an address that is already in wallet.
    for (const auto& [privateWitnessKey, nKeyBirthDate] : privateWitnessKeysWithBirthDates)
    {
        (unused) nKeyBirthDate;
        if (static_cast<CWallet*>(this)->HaveKey(privateWitnessKey.GetPubKey().GetID()))
        {
            std::string strErrorMessage = _("Error importing private key") + "\n" + _("Wallet already contains key.");
            LogPrintf(strErrorMessage.c_str());
            CAlert::Notify(strErrorMessage, true, true);
            return false;
        }
    }

    for (const auto& [privateWitnessKey, nKeyBirthDate] : privateWitnessKeysWithBirthDates)
    {
        CPubKey pubWitnessKey = privateWitnessKey.GetPubKey();
        CKeyID witnessKeyID = pubWitnessKey.GetID();
        static_cast<CWallet*>(this)->importPrivKeyIntoAccount(forAccount, privateWitnessKey, witnessKeyID, nKeyBirthDate, allowRescan);
    }
    return true;
}

std::vector<std::pair<CKey, uint64_t>> CExtWallet::ParseWitnessKeyURL(SecureString sEncodedPrivWitnessKeysURL)
{
    std::string keyPrefix = GLOBAL_APP_URIPREFIX"://witnesskeys?keys=";
    if (!boost::starts_with(sEncodedPrivWitnessKeysURL, keyPrefix))
        throw std::runtime_error("Not a valid \"witness only\" witness account URI");

    std::vector<SecureString> encodedPrivateWitnessKeyStrings;
    SecureString encPrivWitnessKeyWithoutPrefix(sEncodedPrivWitnessKeysURL.begin()+keyPrefix.length(), sEncodedPrivWitnessKeysURL.end());
    boost::split(encodedPrivateWitnessKeyStrings, encPrivWitnessKeyWithoutPrefix, boost::is_any_of(":"));

    std::vector<std::pair<CKey, uint64_t>> privateWitnessKeys;
    privateWitnessKeys.reserve(encodedPrivateWitnessKeyStrings.size());
    for (const auto& encodedPrivateWitnessKeyString : encodedPrivateWitnessKeyStrings)
    {
        std::vector<SecureString> encodedPrivateWitnessKeyAndBirthDate;
        boost::split(encodedPrivateWitnessKeyAndBirthDate, encodedPrivateWitnessKeyString, boost::is_any_of("#"));
        if (encodedPrivateWitnessKeyAndBirthDate.size() > 2)
            throw std::runtime_error("Not a valid " GLOBAL_APPNAME " private witness key/birthdate pair");

        uint64_t nKeyBirthDate = 1;
        if (encodedPrivateWitnessKeyAndBirthDate.size() == 2)
            ParseUInt64(encodedPrivateWitnessKeyAndBirthDate[1].c_str(), &nKeyBirthDate);

        bool keyError = false;
        try
        {
            CEncodedSecretKey secretPrivWitnessKey;
            secretPrivWitnessKey.SetString(encodedPrivateWitnessKeyAndBirthDate[0].c_str());
            if (secretPrivWitnessKey.IsValid())
            {
                privateWitnessKeys.emplace_back(secretPrivWitnessKey.GetKey(), nKeyBirthDate);
            }
            else
            {
                keyError = true;
            }
        }
        catch(...)
        {
            keyError = true;
        }
        if (keyError)
        {
            throw std::runtime_error("Not a valid " GLOBAL_APPNAME " private witness key");
        }
    }
    return privateWitnessKeys;
}

CAccount* CExtWallet::CreateWitnessOnlyWitnessAccount(std::string strAccount, std::vector<std::pair<CKey, uint64_t>> privateWitnessKeysWithBirthDates, bool allowRescan)
{
    CAccount* newAccount = NULL;

    newAccount = new CAccount();
    newAccount->m_Type = WitnessOnlyWitnessAccount;

    if (IsCrypted())
    {
        LOCK2(cs_main, cs_wallet);
        if (IsLocked())
            return nullptr;

        if (!activeAccount)
            return nullptr;

        if (!newAccount->Encrypt(activeAccount->vMasterKey))
            return nullptr;
    }

    if (ImportKeysIntoWitnessOnlyWitnessAccount(newAccount, privateWitnessKeysWithBirthDates, allowRescan))
    {
        // Write new account
        addAccount(newAccount, strAccount);

        return newAccount;
    }
    else
    {
        delete newAccount;
        return nullptr;
    }
}

CAccountHD* CExtWallet::CreateReadOnlyAccount(std::string strAccount, SecureString encExtPubKey)
{
    CAccountHD* newAccount = NULL;

    CExtPubKey pubkey;
    try
    {
        CEncodedSecretKeyExt<CExtPubKey> secretExt;
        secretExt.SetString(encExtPubKey.c_str());
        pubkey = secretExt.GetKeyFromString();
    }
    catch(...)
    {
        throw std::runtime_error("Not a valid " GLOBAL_APPNAME " extended public key");
    }

    newAccount = new CAccountHD(pubkey, boost::uuids::nil_generator()(), AccountType::Desktop);


    // Write new account
    addAccount(newAccount, strAccount);

    //We only assign the bare minimum addresses here - and let the background thread do the rest
    static_cast<CWallet*>(this)->TopUpKeyPool(2, 0, newAccount);

    return newAccount;
}

CAccountHD* CExtWallet::CreateSeedlessHDAccount(std::string strAccount, CEncodedSecretKeyExt<CExtKey> accountExtKey, AccountState state, AccountType type, bool generateKeys)
{
    //fixme: (FUT) (ACCOUNTS) (HIGH) add key validation checks here.

    CAccountHD* newAccount = new CAccountHD(accountExtKey.getKeyRaw(), boost::uuids::nil_generator()(), type);
    newAccount->m_State = state;

    // Write new account
    addAccount(newAccount, strAccount);

    //We only assign the bare minimum addresses here - and let the background thread do the rest
    if (generateKeys)
    {
        static_cast<CWallet*>(this)->TopUpKeyPool(2, 0, newAccount);
    }

    return newAccount;
}


void CExtWallet::ForceRewriteKeys(CAccount& forAccount)
{
    {
        LOCK(cs_wallet);

        std::set<CKeyID> setAddress;
        forAccount.GetKeys(setAddress);

        CWalletDB walletDB(*dbw);
        for (const auto& keyID : setAddress)
        {
            if (!IsCrypted())
            {
                CKey keyPair;
                if (!GetKey(keyID, keyPair))
                {
                    throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade all keys");
                }
                walletDB.EraseKey(keyPair.GetPubKey());
                if (!walletDB.WriteKey(keyPair.GetPubKey(), keyPair.GetPrivKey(), static_cast<CWallet*>(this)->mapKeyMetadata[keyID], getUUIDAsString(forAccount.getUUID()), KEYCHAIN_EXTERNAL))
                {
                    throw std::runtime_error("Fatal error upgrading legacy wallet - could not write all upgraded keys");
                }
            }
            else
            {
                CPubKey pubKey;
                if (!GetPubKey(keyID, pubKey))
                { 
                    throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade all encrypted keys");
                }
                walletDB.EraseEncryptedKey(pubKey);
                std::vector<unsigned char> secret;
                if (!forAccount.GetKey(keyID, secret))
                { 
                    throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade all encrypted keys");
                }
                if (!walletDB.WriteCryptedKey(pubKey, secret, static_cast<CWallet*>(this)->mapKeyMetadata[keyID], getUUIDAsString(forAccount.getUUID()), KEYCHAIN_EXTERNAL))
                {
                    throw std::runtime_error("Fatal error upgrading legacy wallet - could not write all upgraded keys");
                }
            }
        }
    }


    CWalletDB walletDB(*dbw);
    for(int64_t nIndex : forAccount.setKeyPoolInternal)
    {
        CKeyPool keypoolentry;
        if (!walletDB.ReadPool(nIndex, keypoolentry))
        {
            throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade entire keypool");
        }
        if (!walletDB.ErasePool( static_cast<CWallet*>(this), nIndex ))
        {
            throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade entire keypool");
        }
        keypoolentry.accountName = getUUIDAsString(forAccount.getUUID());
        keypoolentry.nChain = KEYCHAIN_CHANGE;
        if ( !walletDB.WritePool( nIndex, keypoolentry ) )
        {
            throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade entire keypool");
        }
    }
    for(int64_t nIndex : forAccount.setKeyPoolExternal)
    {
        CKeyPool keypoolentry;
        if (!walletDB.ReadPool(nIndex, keypoolentry))
        {
            throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade entire keypool");
        }
        if (!walletDB.ErasePool( static_cast<CWallet*>(this), nIndex ))
        {
            throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade entire keypool");
        }
        keypoolentry.accountName = getUUIDAsString(forAccount.getUUID());
        keypoolentry.nChain = KEYCHAIN_EXTERNAL;
        if ( !walletDB.WritePool( nIndex, keypoolentry ) )
        {
            throw std::runtime_error("Fatal error upgrading legacy wallet - could not upgrade entire keypool");
        }
    }
}


bool CExtWallet::AddHDKeyPubKey(int64_t HDKeyIndex, const CPubKey &pubkey, CAccount& forAccount, int keyChain)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!forAccount.AddKeyPubKey(HDKeyIndex, pubkey, keyChain))
    {
        LogPrintf("CExtWallet::AddHDKeyPubKey: AddKeyPubKey failed for account");
        return false;
    }

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script))
        static_cast<CWallet*>(this)->RemoveWatchOnly(script);
    script = GetScriptForRawPubKey(pubkey);
    if (HaveWatchOnly(script))
        static_cast<CWallet*>(this)->RemoveWatchOnly(script);

    if (!CWalletDB(*dbw).WriteKeyHD(pubkey, HDKeyIndex, keyChain, static_cast<CWallet*>(this)->mapKeyMetadata[pubkey.GetID()], getUUIDAsString(forAccount.getUUID())))
    {
        LogPrintf("CExtWallet::AddHDKeyPubKey: WriteKeyHD failed for key");
        return false;
    }
    else if (forAccount.IsPoW2Witness() && keyChain == KEYCHAIN_WITNESS)
    {
        CPrivKey nullKey;
        return CWalletDB(*dbw).WriteKeyOverride(pubkey, nullKey, getUUIDAsString(forAccount.getUUID()), KEYCHAIN_WITNESS);
    }
    return true;
}


bool CExtWallet::LoadHDKey(int64_t HDKeyIndex, int64_t keyChain, const CPubKey &pubkey, const std::string& forAccount)
{
    LOCK(cs_wallet);
    return mapAccounts[getUUIDFromString(forAccount)]->AddKeyPubKey(HDKeyIndex, pubkey, keyChain);
}


bool CExtWallet::Lock()
{
    LOCK(cs_wallet);

    if (nUnlockSessions > 0) {
        fAutoLock = true;
        return true;
    }
    else {
        return LockHard();
    }
}

bool CExtWallet::LockHard()
{
    AssertLockHeld(cs_wallet);

    static_cast<CWallet*>(this)->nRelockTime = 0;

    bool ret = true;
    for (const auto& [accountUUID, forAccount] : mapAccounts)
    {
        (unused) accountUUID;
        if (!forAccount->Lock())
            ret = false;
    }
    for (const auto& [seedUUID, forSeed] : mapSeeds)
    {
        (unused) seedUUID;
        if (!forSeed->Lock())
            ret = false;
    }

    // Only notify changed locking state if the locking actually changed, that is:
    // - actaul locking took place, it is NOT delayed
    // - locking succeeded
    if (ret)
        NotifyLockingChanged(true);

    return ret;
}

CPubKey CWallet::GenerateNewKey(CAccount& forAccount, int keyChain)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    CPubKey pubkey = forAccount.GenerateNewKey(*this, metadata, keyChain);

    mapKeyMetadata[pubkey.GetID()] = metadata;
    UpdateTimeFirstKey(nCreationTime);

    return pubkey;
}

bool CWallet::LoadKey(const CKey& key, const CPubKey &pubkey, const std::string& forAccount, int64_t nKeyChain)
{
    LOCK(cs_wallet);
    return mapAccounts[getUUIDFromString(forAccount)]->AddKeyPubKey(key, pubkey, nKeyChain);
}
