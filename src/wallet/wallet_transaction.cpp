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

#include "wallet.h"
#include "wallet/wallettx.h"

#include "consensus/validation.h"
#include "validation/validation.h"
#include "validation/witnessvalidation.h"
#include "net.h"
#include "scheduler.h"
#include "timedata.h"
#include "init.h"
#include "key.h"
#include "keystore.h"
#include "wallet/coincontrol.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "witnessutil.h"
#include "alert.h"

std::vector<CAccount*> CWallet::FindAccountsForTransaction(const CTxOut& out)
{
    std::vector<CAccount*> ret;
    for (const auto& [accountUUID, forAccount] : mapAccounts)
    {
        (unused) accountUUID;
        if (::IsMine(*forAccount, out) == ISMINE_SPENDABLE)
        {
            ret.push_back(forAccount);
        }
    }
    return ret;
}

CAccount* CWallet::FindBestWitnessAccountForTransaction(const CTxOut& out)
{
    // Find all accounts for this address
    CAccount* witnessAccount = nullptr;
    std::vector<CAccount*> potentialWitnessAccounts = FindAccountsForTransaction(out);
    
    // Filter for witness accounts
    for (auto account : potentialWitnessAccounts)
    {
        if (account->IsPoW2Witness())
        {
            // Try to prefer full witness account to witness only
            if (!witnessAccount || witnessAccount->IsWitnessOnly())
            {
                witnessAccount = account;
            }
        }
    }
    return witnessAccount;
}

bool CWallet::SignTransaction(CAccount* fromAccount, CMutableTransaction &tx, SignType type, CTxOut* prevOutOverride)
{
    AssertLockHeld(cs_wallet); // mapWallet

    // sign the new tx
    CTransaction txNewConst(tx);
    int nIn = 0;
    for (const auto& input : tx.vin)
    {
        if (input.GetPrevOut().IsNull() && txNewConst.IsPoW2WitnessCoinBase())
        {
            nIn++;
            continue;
        }

        const CWalletTx* prev = GetWalletTx(input.GetPrevOut());
        const CTxOut* prevout = nullptr;
        if(!prev || input.GetPrevOut().n >= prev->tx->vout.size())
        {
            if (prevOutOverride)
            {
                prevout = prevOutOverride;
            }
            else
            {
                return false;
            }
        }
        else
        {
            prevout = &prev->tx->vout[input.GetPrevOut().n];
        }
        
        const CAmount& amount = prevout->nValue;
        CKeyID signingKeyID = ExtractSigningPubkeyFromTxOutput(*prevout, type);
        SignatureData sigdata;
        std::vector<CAccount*> accountsToTry;
        if (!fromAccount)
            accountsToTry = FindAccountsForTransaction(*prevout);
        else
            accountsToTry.push_back(fromAccount);    
        if (accountsToTry.empty())
            return false;
        
        std::string strAccountsToTry;
        std::vector<CKeyStore*> keystores;
        keystores.reserve(accountsToTry.size());
        for (auto account : accountsToTry)
        {
            strAccountsToTry += account->getLabel() + " ";
            keystores.push_back(account);
        }
        
        if (!ProduceSignature(TransactionSignatureCreator(signingKeyID, keystores, &txNewConst, nIn, amount, SIGHASH_ALL), *prevout, sigdata, type, txNewConst.nVersion))
        {
            LogPrintf("CWallet::SignTransaction failed to produce signature [signing accounts: %s] [transaction: %s]\n", strAccountsToTry.c_str(), txNewConst.ToString());
            return false;
        }
        UpdateTransaction(tx, nIn, sigdata);
        nIn++;
    }
    return true;
}



bool CWallet::FundTransaction(CAccount* fromAccount, CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosInOut, std::string& strFailReason, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CCoinControl coinControl, CReserveKeyOrScript& reservekey)
{
    std::vector<CRecipient> vecSend;

    // Turn the txout set into a CRecipient vector
    for (size_t idx = 0; idx < tx.vout.size(); idx++)
    {
        const CTxOut& txOut = tx.vout[idx];
        CRecipient recipient = GetRecipientForTxOut(txOut, txOut.nValue, setSubtractFeeFromOutputs.count(idx) == 1);
        vecSend.push_back(recipient);
    }

    coinControl.fAllowOtherInputs = true;

    for(const CTxIn& txin : tx.vin)
        coinControl.Select(txin.GetPrevOut());

    CWalletTx wtx;
    if (!CreateTransaction(fromAccount, vecSend, wtx, reservekey, nFeeRet, nChangePosInOut, strFailReason, &coinControl, false))
        return false;

    if (nChangePosInOut != -1)
        tx.vout.insert(tx.vout.begin() + nChangePosInOut, wtx.tx->vout[nChangePosInOut]);

    // Copy output sizes from new transaction; they may have had the fee subtracted from them
    for (unsigned int idx = 0; idx < tx.vout.size(); idx++)
        tx.vout[idx].nValue = wtx.tx->vout[idx].nValue;

    // Add new txins (keeping original txin scriptSig/order)
    for(const CTxIn& txin : wtx.tx->vin)
    {
        //NB!!!
        //If txin.prevout was hash based and not index based, and a valid index based outpoint representation also exists (mature enough) then we end up with a double output here.
        //CreateTransaction ends up selecting the index based outpoint and the two txin no longer match
        //We work around this by looking up the hash (if its in the wallet) and using that instead
        //However callers to this function should also rather just pass in index based inputs where possible
        if (!coinControl.IsSelected(txin.GetPrevOut()))
        {
            bool add = false;
            if (txin.GetPrevOut().isHash)
            {
                add = true;
            }
            else
            {
                uint256 convertedHash;
                if (CWallet::GetTxHash(txin.GetPrevOut(), convertedHash))
                {
                    if(!coinControl.IsSelected(COutPoint(convertedHash, txin.GetPrevOut().n)))
                    {
                        add = true;
                    }
                }
            }
            if (add)
            {
                tx.vin.push_back(txin);
                if (lockUnspents)
                {
                    LOCK2(cs_main, cs_wallet);
                    LockCoin(txin.GetPrevOut());
                }
            }
        }
    }

    return true;
}


void CWallet::AddTxInput(CMutableTransaction& tx, const CInputCoin& inputCoin, bool rbf)
{
    std::set<CInputCoin> setCoins;
    setCoins.insert(inputCoin);
    AddTxInputs(tx, setCoins, rbf);
}

void CWallet::AddTxInputs(CMutableTransaction& tx, std::set<CInputCoin>& setCoins, bool rbf)
{
    uint8_t nFlags = 0;
    uint32_t nSequence = 0;
    if (IsOldTransactionVersion(tx.nVersion))
    {
        if (rbf)
        {
            // Use sequence number to signal RBF
            nSequence = MAX_BIP125_RBF_SEQUENCE;
        }
        else if(tx.nLockTime == 0)
        {
            // Use sequence number to signal locktime
            nSequence = std::numeric_limits<unsigned int>::max() - 1;
        }
        else
        {
            // Final sequence number
            nSequence = std::numeric_limits<unsigned int>::max();
        }
    }
    else
    {
        if (rbf)
        {
            nFlags |= CTxInFlags::OptInRBF;
        }
        else if(tx.nLockTime == 0)
        {
            //fixme: (PHASE4POSTREL) (SEGSIG) (LOCKTIME) (SEQUENCE) - Look closer into the various lock mechanisms again, temporarily set as non standard
            //nFlags |= CTxInFlags::HasAbsoluteLock;
        }
        else
        {
            // Final sequence number
            nSequence = std::numeric_limits<unsigned int>::max();
        }
    }
    for (const auto& coin : setCoins)
        tx.vin.push_back(CTxIn(coin.outpoint,CScript(), nSequence, nFlags));
}

bool CWallet::CreateTransaction(CAccount* forAccount, const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKeyOrScript& reservekey, CAmount& nFeeRet,
                                int& nChangePosInOut, std::string& strFailReason, const CCoinControl* coinControl, bool sign)
{

    if (forAccount->IsReadOnly())
    {
        strFailReason = _("Can't send from read only (watch) account.");
        return false;
    }
    std::vector<CKeyStore*> accountsToTry;
    accountsToTry.push_back(forAccount);
    return CreateTransaction(accountsToTry, vecSend, wtxNew, reservekey, nFeeRet, nChangePosInOut, strFailReason, coinControl, sign);
}

bool CWallet::CreateTransaction(std::vector<CKeyStore*>& accountsToTry, const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKeyOrScript& reservekey, CAmount& nFeeRet,
                                int& nChangePosInOut, std::string& strFailReason, const CCoinControl* coinControl, bool sign)
{

    //fixme: (HIGH) (UNITY)
    //for (const auto& forAccount : accountsToTry)
    //{
        //if (forAccount->IsReadOnly())
        //{
            //strFailReason = _("Can't send from read only (watch) account.");
            //return false;
        //}
    //}

    CAmount nValue = 0;
    int nChangePosRequest = nChangePosInOut;
    unsigned int nSubtractFeeFromAmount = 0;
    for (const auto& recipient : vecSend)
    {
        if (nValue < 0 || recipient.nAmount < 0)
        {
            strFailReason = _("Transaction amounts must not be negative");
            return false;
        }
        nValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }
    if (vecSend.empty())
    {
        strFailReason = _("Transaction must have at least one recipient");
        return false;
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);
    CMutableTransaction txNew(CURRENT_TX_VERSION_POW2);

    // Discourage fee sniping.
    //
    // For a large miner the value of the transactions in the best block and
    // the mempool can exceed the cost of deliberately attempting to mine two
    // blocks to orphan the current best block. By setting nLockTime such that
    // only the next block can include the transaction, we discourage this
    // practice as the height restricted and limited blocksize gives miners
    // considering fee sniping fewer options for pulling off this attack.
    //
    // A simple way to think about this is from the wallet's point of view we
    // always want the blockchain to move forward. By setting nLockTime this
    // way we're basically making the statement that we only want this
    // transaction to appear in the next block; we don't want to potentially
    // encourage reorgs by allowing transactions to appear at lower heights
    // than the next block in forks of the best chain.
    //
    // Of course, the subsidy is high enough, and transaction volume low
    // enough, that fee sniping isn't a problem yet, but by implementing a fix
    // now we ensure code won't be written that makes assumptions about
    // nLockTime that preclude a fix later.
    //fixme: (PHASE4POSTREL) (SEGSIG) (LOCKTIME) (SEQUENCE) - Look closer into the various lock mechanisms again, temporarily set as non standard
    //if (GetRandInt(10) == 0)//Munt - we only set this on 10% of blocks to avoid unnecessary space wastage. //fixme: (PHASE5) (only set this for high fee [per byte] transactions?)
        //txNew.nLockTime = chainActive.Height();

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    //fixme: (PHASE4POSTREL) (SEGSIG) (LOCKTIME) (SEQUENCE) - Look closer into the various lock mechanisms again, temporarily set as non standard
    //if (GetRandInt(100) == 0)
        //txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);

    {
        std::set<CInputCoin> setCoins;
        LOCK2(cs_main, cs_wallet);
        {
            std::vector<COutput> vAvailableCoins;
            AvailableCoins(accountsToTry, vAvailableCoins, true, coinControl);

            nFeeRet = 0;
            // Start with no fee and loop until there is enough fee
            while (true)
            {
                nChangePosInOut = nChangePosRequest;
                txNew.vin.clear();
                txNew.vout.clear();
                wtxNew.fFromMe = true;
                bool fFirst = true;

                CAmount nValueToSelect = nValue;
                if (nSubtractFeeFromAmount == 0)
                    nValueToSelect += nFeeRet;
                // vouts to the payees
                for (const auto& recipient : vecSend)
                {
                    CTxOut txout = recipient.GetTxOut();

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }

                    if (IsDust(txout, ::dustRelayFee))
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0) { strFailReason = _("The transaction amount is too small to pay the fee"); }
                            else { strFailReason = _("The transaction amount is too small to send after the fee has been deducted"); }
                        }
                        else { strFailReason = _("Transaction amount too small"); }
                        return false;
                    }
                    txNew.vout.push_back(txout);
                }

                // Choose coins to use
                CAmount nValueIn = 0;
                setCoins.clear();
                if (!SelectCoins(!IsOldTransactionVersion(txNew.nVersion), vAvailableCoins, nValueToSelect, setCoins, nValueIn, coinControl))
                {
                    strFailReason = _("Insufficient funds");
                    return false;
                }

                const CAmount nChange = nValueIn - nValueToSelect;
                if (nChange > 0)
                {
                    std::shared_ptr<CTxOut> newTxOut = nullptr;
                    //fixme: (FUT) (COIN_CONTROL)
                    //We might want to allow coincontrol to produce script transactions even though segsig is enabled.
                    if (!IsOldTransactionVersion(txNew.nVersion))
                    {
                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        bool ret;
                        ret = reservekey.GetReservedKey(vchPubKey);
                        if (!ret)
                        {
                            if (reservekey.account && (reservekey.account->IsFixedKeyPool() || reservekey.account->IsMinimalKeyPool()))
                            {
                                std::string strFailReason = _("This type of account only supports emptying the entire balance in one go, no partial transactions.");
                                CAlert::Notify(strFailReason, true, true);
                                LogPrintf("%s", strFailReason.c_str());
                            }
                            else
                            {
                                strFailReason = _("Keypool ran out, please call keypoolrefill first");
                            }
                            return false;
                        }

                        CTxOutStandardKeyHash standardKeyHashKeyID(vchPubKey.GetID());
                        newTxOut = std::make_shared<CTxOut>(CTxOut(nChange, standardKeyHashKeyID));
                    }
                    else
                    {
                        // Fill a vout to ourself
                        // TODO: pass in scriptChange instead of reservekey so
                        // change transaction isn't always pay-to-Munt-address
                        CScript scriptChange;

                        // coin control: send change to custom address
                        if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                            scriptChange = GetScriptForDestination(coinControl->destChange);

                        // no coin control: send change to newly generated address
                        else
                        {
                            // Note: We use a new key here to keep it from being obvious which side is the change.
                            //  The drawback is that by not reusing a previous key, the change may be lost if a
                            //  backup is restored, if the backup doesn't have the new private key for the change.
                            //  If we reused the old key, it would be possible to add code to look for and
                            //  rediscover unknown transactions that were written with keys of ours to recover
                            //  post-backup change.

                            // Reserve a new key pair from key pool
                            CPubKey vchPubKey;
                            bool ret = false;
                            try
                            {
                                ret = reservekey.GetReservedKey(vchPubKey);
                            }
                            catch(...)
                            {
                                
                            }
                            if (!ret)
                            {
                                if (reservekey.account && (reservekey.account->IsFixedKeyPool() || reservekey.account->IsMinimalKeyPool()))
                                {
                                    std::string strFailReason = _("This type of account only supports emptying the entire balance in one go, no partial transactions.");
                                    CAlert::Notify(strFailReason, true, true);
                                    LogPrintf("%s", strFailReason.c_str());
                                }
                                else
                                {
                                    strFailReason = _("Keypool ran out, please call keypoolrefill first");
                                }
                                return false;
                            }

                            scriptChange = GetScriptForDestination(vchPubKey.GetID());
                        }
                        newTxOut = std::make_shared<CTxOut>(CTxOut(nChange, scriptChange));
                    }

                    // We do not move dust-change to fees, because the sender would end up paying more than requested.
                    // This would be against the purpose of the all-inclusive feature.
                    // So instead we raise the change and deduct from the recipient.
                    if (nSubtractFeeFromAmount > 0 && IsDust(*newTxOut, ::dustRelayFee))
                    {
                        CAmount nDust = GetDustThreshold(*newTxOut, ::dustRelayFee) - newTxOut->nValue;
                        newTxOut->nValue += nDust; // raise change until no more dust
                        for (unsigned int i = 0; i < vecSend.size(); i++) // subtract from first recipient
                        {
                            if (vecSend[i].fSubtractFeeFromAmount)
                            {
                                txNew.vout[i].nValue -= nDust;
                                if (IsDust(txNew.vout[i], ::dustRelayFee))
                                {
                                    strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                                    return false;
                                }
                                break;
                            }
                        }
                    }

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (IsDust(*newTxOut, ::dustRelayFee))
                    {
                        nChangePosInOut = -1;
                        nFeeRet += nChange;
                        reservekey.ReturnKey();
                    }
                    else
                    {
                        if (nChangePosInOut == -1)
                        {
                            // Insert change txn at random position:
                            nChangePosInOut = GetRandInt(txNew.vout.size()+1);
                        }
                        else if ((unsigned int)nChangePosInOut > txNew.vout.size())
                        {
                            strFailReason = _("Change index out of range");
                            return false;
                        }

                        std::vector<CTxOut>::iterator position = txNew.vout.begin()+nChangePosInOut;
                        txNew.vout.insert(position, *newTxOut);
                    }
                }
                else
                {
                    reservekey.ReturnKey();
                    nChangePosInOut = -1;
                }

                // Fill vin
                //
                // Note how the sequence number is set to non-maxint so that
                // the nLockTime set above actually works.
                //
                // BIP125 defines opt-in RBF as any nSequence < maxint-1, so
                // we use the highest possible value in that range (maxint-2)
                // to avoid conflicting with other possible uses of nSequence,
                // and in the spirit of "smallest possible change from prior
                // behavior."
                bool rbf = coinControl ? coinControl->signalRbf : fWalletRbf;
                AddTxInputs(txNew, setCoins , rbf);

                // Fill in dummy signatures for fee calculation.
                if (!DummySignTx(accountsToTry, txNew, setCoins, Spend)) {
                    SignatureData sigdata;
                    if (!ProduceSignature(DummySignatureCreator(accountsToTry), CTxOut(), sigdata, Spend, txNew.nVersion))
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    }
                }

                unsigned int numCoinBase = 0;
                for (const auto& coin : setCoins)
                {
                    if (coin.isCoinBase)
                    {
                        ++numCoinBase;
                    }
                }
                unsigned int nBytesDiscounted = GetVirtualTransactionSizeDiscounted(txNew, numCoinBase);

                CTransaction txNewConst(txNew);

                // Remove scriptSigs to eliminate the fee calculation dummy signatures
                for (auto& vin : txNew.vin) {
                    vin.scriptSig = CScript();
                    vin.segregatedSignatureData.SetNull();
                }

                // Allow to override the default confirmation target over the CoinControl instance
                int currentConfirmationTarget = nTxConfirmTarget;
                if (coinControl && coinControl->nConfirmTarget > 0)
                    currentConfirmationTarget = coinControl->nConfirmTarget;

                CAmount nFeeNeeded = GetMinimumFee(nBytesDiscounted, currentConfirmationTarget, ::mempool, ::feeEstimator);
                if (coinControl && coinControl->fOverrideFeeRate)
                    nFeeNeeded = coinControl->nFeeRate.GetFee(nBytesDiscounted);

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytesDiscounted))
                {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeRet >= nFeeNeeded) {
                    // Reduce fee to only the needed amount if we have change
                    // output to increase.  This prevents potential overpayment
                    // in fees if the coins selected to meet nFeeNeeded result
                    // in a transaction that requires less fee than the prior
                    // iteration.
                    // TODO: The case where nSubtractFeeFromAmount > 0 remains
                    // to be addressed because it requires returning the fee to
                    // the payees and not the change output.
                    // TODO: The case where there is no change output remains
                    // to be addressed so we avoid creating too small an output.
                    if (nFeeRet > nFeeNeeded && nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                        CAmount extraFeePaid = nFeeRet - nFeeNeeded;
                        std::vector<CTxOut>::iterator change_position = txNew.vout.begin()+nChangePosInOut;
                        change_position->nValue += extraFeePaid;
                        nFeeRet -= extraFeePaid;
                    }
                    break; // Done, enough fee included.
                }

                // Try to reduce change to include necessary fee
                if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                    CAmount additionalFeeNeeded = nFeeNeeded - nFeeRet;
                    std::vector<CTxOut>::iterator change_position = txNew.vout.begin()+nChangePosInOut;
                    // Only reduce change if remaining amount is still a large enough output.
                    if (change_position->nValue >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
                        change_position->nValue -= additionalFeeNeeded;
                        nFeeRet += additionalFeeNeeded;
                        break; // Done, able to increase fee from change
                    }
                }

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                continue;
            }
        }

        if (sign)
        {
            CTransaction txNewConst(txNew);
            int nIn = 0;
            for (const auto& coin : setCoins)
            {
                SignatureData sigdata;

                CKeyID signingKeyID = ExtractSigningPubkeyFromTxOutput(coin.txout, SignType::Spend);

                if (!ProduceSignature(TransactionSignatureCreator(signingKeyID, accountsToTry, &txNewConst, nIn, coin.txout.nValue, SIGHASH_ALL),  coin.txout, sigdata, Spend, txNewConst.nVersion))
                {
                    strFailReason = _("Signing transaction failed");
                    return false;
                }
                else
                {
                    UpdateTransaction(txNew, nIn, sigdata);
                }

                nIn++;
            }
        }

        // Embed the constructed transaction data in wtxNew.
        wtxNew.SetTx(MakeTransactionRef(std::move(txNew)));

        // Limit size
        if (GetTransactionWeight(wtxNew) >= MAX_STANDARD_TX_WEIGHT)
        {
            strFailReason = _("Transaction too large");
            return false;
        }
    }

    if (GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS)) {
        // Lastly, ensure this tx will pass the mempool's chain limits
        LockPoints lp;
        CTxMemPoolEntry entry(wtxNew.tx, 0, 0, 0, 0, 0, lp);
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!mempool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            strFailReason = _("Transaction has too long of a mempool chain");
            return false;
        }
    }

    // NB! This part of the process is super important.
    // We insert the private witnessing (**not** -spending- only -witnessing-) key into the unencrypted keychain for the HD account (even on encrypted wallet)
    // This allows witnessing to work correctly on encrypted wallets.
    for (const auto& recipient : vecSend)
    {
        if (recipient.witnessForAccount != nullptr)
        {
            CKey privWitnessKey;
            if (!recipient.witnessForAccount->GetKey(recipient.witnessDetails.witnessKeyID, privWitnessKey))
            {
                //fixme: (FUT) Localise
                strFailReason = strprintf("Wallet error, failed to retrieve private witness key.");
                return false;
            }
            if (!AddKeyPubKey(privWitnessKey, privWitnessKey.GetPubKey(), *recipient.witnessForAccount, KEYCHAIN_WITNESS))
            {
                //fixme: (FUT) Localise
                strFailReason = strprintf("Wallet error, failed to store witness key.");
                return false;
            }
        }
    }
    return true;
}


bool CWallet::AddFeeForTransaction(CAccount* forAccount, CMutableTransaction& txNew, CReserveKeyOrScript& reservekey, CAmount& nFeeOut, bool sign, std::string& strFailReason, const CCoinControl* coinControl)
{
    std::vector<CKeyStore*> accountsToTry;
    accountsToTry.push_back(forAccount);
                
    CWalletTx wtxNew;
    if (forAccount->IsReadOnly())
    {
        strFailReason = _("Can't send from read only (watch) account.");
        return false;
    }

    {
        std::set<CInputCoin> setCoins;
        int nChangePos = -1;
        LOCK2(cs_main, cs_wallet);
        {
            std::vector<COutput> vAvailableCoins;
            AvailableCoins(forAccount, vAvailableCoins, true, nullptr);

            CAmount nMandatoryWitnessPenaltyFee = 0;
            for(const auto& output : txNew.vout)
            {
                nMandatoryWitnessPenaltyFee += CalculateWitnessPenaltyFee(output);
            }
            nFeeOut = nMandatoryWitnessPenaltyFee;

            // Start with no fee and loop until there is enough fee
            while (true)
            {
                // Choose coins to use
                CAmount nValueSelected = 0;
                setCoins.clear();
                if (!SelectCoins(IsOldTransactionVersion(txNew.nVersion), vAvailableCoins, nFeeOut, setCoins, nValueSelected, coinControl))
                {
                    strFailReason = _("Insufficient funds");
                    return false;
                }

                const CAmount nChange = nValueSelected - nFeeOut;
                if (nChange > 0)
                {
                    //fixme: (FUT) (COIN_CONTROL)
                    //We might want to allow coincontrol to produce script transactions even though segsig is enabled.
                    std::shared_ptr<CTxOut> newTxOut = nullptr;
                    if (!IsOldTransactionVersion(txNew.nVersion))
                    {
                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        bool ret;
                        ret = reservekey.GetReservedKey(vchPubKey);
                        if (!ret)
                        {
                            strFailReason = _("Keypool ran out, please call keypoolrefill first");
                            return false;
                        }

                        CTxOutStandardKeyHash standardKeyHashKeyID(vchPubKey.GetID());
                        newTxOut = std::make_shared<CTxOut>(CTxOut(nChange, standardKeyHashKeyID));
                    }
                    else
                    {
                        // Fill a vout to ourself
                        // TODO: pass in scriptChange instead of reservekey so
                        // change transaction isn't always pay-to-Munt-address
                        CScript scriptChange;

                        // coin control: send change to custom address
                        if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                        {
                            scriptChange = GetScriptForDestination(coinControl->destChange);
                        }
                        else // no coin control: send change to newly generated address
                        {
                            // Note: We use a new key here to keep it from being obvious which side is the change.
                            //  The drawback is that by not reusing a previous key, the change may be lost if a
                            //  backup is restored, if the backup doesn't have the new private key for the change.
                            //  If we reused the old key, it would be possible to add code to look for and
                            //  rediscover unknown transactions that were written with keys of ours to recover
                            //  post-backup change.

                            // Reserve a new key pair from key pool
                            CPubKey vchPubKey;
                            bool ret;
                            ret = reservekey.GetReservedKey(vchPubKey);
                            if (!ret)
                            {
                                strFailReason = _("Keypool ran out, please call keypoolrefill first");
                                return false;
                            }

                            scriptChange = GetScriptForDestination(vchPubKey.GetID());
                        }
                        newTxOut = std::make_shared<CTxOut>(CTxOut(nChange, scriptChange));
                    }

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (IsDust(*newTxOut, ::dustRelayFee))
                    {
                        nFeeOut += nChange;
                        reservekey.ReturnKey();
                    }
                    else
                    {
                        txNew.vout.push_back(*newTxOut);
                        nChangePos = txNew.vout.size()-1;
                    }
                }
                else
                {
                    reservekey.ReturnKey();
                }

                // Add inputs
                AddTxInputs(txNew, setCoins, false);

                // Fill in dummy signatures for fee calculation.
                if (!DummySignTx(accountsToTry, txNew, setCoins, Spend)) {
                    SignatureData sigdata;
                    if (!ProduceSignature(DummySignatureCreator(accountsToTry), CTxOut(), sigdata, Spend, txNew.nVersion))
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    }
                }

                unsigned int nBytes = GetVirtualTransactionSize(txNew);

                CTransaction txNewConst(txNew);

                // Remove scriptSigs to eliminate the fee calculation dummy signatures
                {
                    LOCK2(cs_main, cs_wallet);
                    for (auto& txin : txNew.vin)
                    {
                        txin.scriptSig = CScript();
                        txin.segregatedSignatureData.SetNull();
                        
                        // Prevent input being spent twice
                        LockCoin(txin.GetPrevOut());
                    }
                }

                // Allow to override the default confirmation target over the CoinControl instance
                int currentConfirmationTarget = nTxConfirmTarget;
                if (coinControl && coinControl->nConfirmTarget > 0)
                    currentConfirmationTarget = coinControl->nConfirmTarget;

                CAmount nMinimumFee = GetMinimumFee(nBytes, currentConfirmationTarget, ::mempool, ::feeEstimator);
                CAmount nFeeNeeded = (nMinimumFee > nMandatoryWitnessPenaltyFee) ? nMinimumFee : nMandatoryWitnessPenaltyFee;
                if (coinControl && coinControl->fOverrideFeeRate)
                    nFeeNeeded = (coinControl->nFeeRate.GetFee(nBytes) > nMandatoryWitnessPenaltyFee) ? (coinControl->nFeeRate.GetFee(nBytes)) : nMandatoryWitnessPenaltyFee;

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes))
                {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeOut >= nFeeNeeded) {
                    // Reduce fee to only the needed amount if we have change
                    // output to increase.  This prevents potential overpayment
                    // in fees if the coins selected to meet nFeeNeeded result
                    // in a transaction that requires less fee than the prior
                    // iteration.
                    // TODO: The case where there is no change output remains
                    // to be addressed so we avoid creating too small an output.
                    if (nFeeOut > nFeeNeeded && nChangePos != -1)
                    {
                        CAmount extraFeePaid = nFeeOut - nFeeNeeded;
                        std::vector<CTxOut>::iterator change_position = txNew.vout.begin()+nChangePos;
                        change_position->nValue += extraFeePaid;
                        nFeeOut -= extraFeePaid;
                    }
                    break; // Done, enough fee included.
                }

                // Try to reduce change to include necessary fee
                if (nChangePos != -1)
                {
                    CAmount additionalFeeNeeded = nFeeNeeded - nFeeOut;
                    std::vector<CTxOut>::iterator change_position = txNew.vout.begin()+nChangePos;
                    // Only reduce change if remaining amount is still a large enough output.
                    if (change_position->nValue >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
                        change_position->nValue -= additionalFeeNeeded;
                        nFeeOut += additionalFeeNeeded;
                        break; // Done, able to increase fee from change
                    }
                }

                // Include more fee and try again.
                nFeeOut = nFeeNeeded;
                continue;
            }
        }

        if (sign)
        {
            CTransaction txNewConst(txNew);
            int nIn = 0;
            for (const auto& coin : setCoins)
            {
                SignatureData sigdata;

                CKeyID signingKeyID = ExtractSigningPubkeyFromTxOutput(coin.txout, SignType::Spend);

                if (!ProduceSignature(TransactionSignatureCreator(signingKeyID, accountsToTry, &txNewConst, nIn, coin.txout.nValue, SIGHASH_ALL),  coin.txout, sigdata, Spend, txNewConst.nVersion))
                {
                    LogPrintf("CWallet::AddFeeForTransaction ProduceSignature call failed\n");
                    strFailReason = _("Signing transaction failed");
                    return false;
                }
                else
                {
                    UpdateTransaction(txNew, nIn, sigdata);
                }

                nIn++;
            }
        }

        // Embed the constructed transaction data in wtxNew.
        wtxNew.SetTx(MakeTransactionRef(txNew));

        // Limit size
        if (GetTransactionWeight(wtxNew) >= MAX_STANDARD_TX_WEIGHT)
        {
            strFailReason = _("Transaction too large");
            return false;
        }
    }

    if (GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS)) {
        // Lastly, ensure this tx will pass the mempool's chain limits
        LockPoints lp;
        CTxMemPoolEntry entry(wtxNew.tx, 0, 0, 0, 0, 0, lp);
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!mempool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            strFailReason = _("Transaction has too long of a mempool chain");
            return false;
        }
    }
    return true;
}


bool CWallet::PrepareRenewWitnessAccountTransaction(CAccount* funderAccount, CAccount* targetWitnessAccount, CReserveKeyOrScript& changeReserveKey, CMutableTransaction& tx, CAmount& nFeeOut, std::string& strError, uint64_t* skipPastTransaction, CCoinControl* coinControl)
{
    LOCK2(cs_main, cs_wallet); // cs_main required for ReadBlockFromDisk.

    //fixme: (FUT) (COIN_CONTROL)
    CCoinControl tempCoinControl;
    if (!coinControl)
        coinControl = &tempCoinControl;
    
    bool allowIndexBased = !IsOldTransactionVersion(tx.nVersion);

    CGetWitnessInfo witnessInfo;
    CBlock block;
    if (!ReadBlockFromDisk(block, chainActive.Tip(), Params()))
    {
        strError = "Error reading block from disk.";
        return false;
    }
    GetWitnessInfo(chainActive, Params(), nullptr, chainActive.Tip()->pprev, block, witnessInfo, chainActive.Tip()->nHeight);
    bool addedAny=false;
    uint64_t nExpiredCount=0;
    for (const auto& witCoin : witnessInfo.witnessSelectionPoolUnfiltered)
    {
        if (::IsMine(*targetWitnessAccount, witCoin.coin.out))
        {
            if (witnessHasExpired(witCoin.nAge, witCoin.nWeight, witnessInfo.nTotalWeightRaw))
            {
                if (skipPastTransaction && nExpiredCount++ < *skipPastTransaction)
                    continue;

                addedAny = true;
               
                // Add witness input
                AddTxInput(tx, CInputCoin(witCoin.outpoint, witCoin.coin.out, allowIndexBased, false, witCoin.coin.nHeight, witCoin.coin.nTxIndex), false);

                // Add witness output
                CTxOut renewedWitnessTxOutput;
                CTxOutPoW2Witness witnessDestination;
                if (!GetPow2WitnessOutput(witCoin.coin.out, witnessDestination))
                {
                    strError = "Unable to correctly retrieve data";
                    return false;
                }

                // Increment fail count appropriately
                IncrementWitnessFailCount(witnessDestination.failCount);

                // Ensure consistent lock from
                if (witnessDestination.lockFromBlock == 0)
                {
                    witnessDestination.lockFromBlock = witCoin.coin.nHeight;
                }


                if (GetPoW2Phase(chainActive.Tip()) >= 4)
                {
                    renewedWitnessTxOutput.SetType(CTxOutType::PoW2WitnessOutput);
                    renewedWitnessTxOutput.output.witnessDetails.spendingKeyID = witnessDestination.spendingKeyID;
                    renewedWitnessTxOutput.output.witnessDetails.witnessKeyID = witnessDestination.witnessKeyID;
                    renewedWitnessTxOutput.output.witnessDetails.lockFromBlock = witnessDestination.lockFromBlock;
                    renewedWitnessTxOutput.output.witnessDetails.lockUntilBlock = witnessDestination.lockUntilBlock;
                    renewedWitnessTxOutput.output.witnessDetails.failCount = witnessDestination.failCount;
                    renewedWitnessTxOutput.output.witnessDetails.actionNonce = witnessDestination.actionNonce+1;
                }
                else
                {
                    CPoW2WitnessDestination dest;
                    dest.spendingKey = witnessDestination.spendingKeyID;
                    dest.witnessKey = witnessDestination.witnessKeyID;
                    dest.lockFromBlock = witnessDestination.lockFromBlock;
                    dest.lockUntilBlock = witnessDestination.lockUntilBlock;
                    dest.failCount = witnessDestination.failCount;
                    dest.actionNonce = witnessDestination.actionNonce+1;
                    renewedWitnessTxOutput.SetType(CTxOutType::ScriptLegacyOutput);
                    renewedWitnessTxOutput.output.scriptPubKey = GetScriptForDestination(dest);
                }
                renewedWitnessTxOutput.nValue = witCoin.coin.out.nValue;
                tx.vout.push_back(renewedWitnessTxOutput);
                
                // Re-add the witness key in case the wallet doesn't already have it for whatever reason
                CKey privWitnessKey;
                targetWitnessAccount->GetKey(witnessDestination.witnessKeyID, privWitnessKey);
                targetWitnessAccount->AddKeyPubKey(privWitnessKey, privWitnessKey.GetPubKey(), KEYCHAIN_WITNESS);

                //fixme: (PHASE5) - Remove this and do all in one tx instead (see note in tx_verify) as blockchain must support this first
                if(skipPastTransaction)
                    *skipPastTransaction = nExpiredCount;
                break;
            }
        }
    }
    if (addedAny)
    {
        // Add fee input and change output
        std::string sFailReason;
        if (!AddFeeForTransaction(funderAccount, tx, changeReserveKey, nFeeOut, true, sFailReason, coinControl))
        {
            strError = "Unable to add fee: " + sFailReason;
            return false;
        }
        return true;
    }
    strError = "Unable to locate any expired inputs for account";
    return false;
}

void CWallet::PrepareUpgradeWitnessAccountTransaction(CAccount* funderAccount, CAccount* targetWitnessAccount, CReserveKeyOrScript& changeReserveKey, CMutableTransaction& tx, CAmount& nFeeOut)
{
    LOCK2(cs_main, cs_wallet); // cs_main required for ReadBlockFromDisk.

    if (!IsSegSigEnabled(chainActive.TipPrev()))
        throw std::runtime_error("Cannot use this command before segsig activates");

    //fixme: (FUT) (COIN_CONTROL)
    CCoinControl coinControl;

    CGetWitnessInfo witnessInfo;
    CBlock block;
    if (!ReadBlockFromDisk(block, chainActive.Tip(), Params()))
        throw std::runtime_error("Error reading block from disk.");

    GetWitnessInfo(chainActive, Params(), nullptr, chainActive.Tip()->pprev, block, witnessInfo, chainActive.Tip()->nHeight);
    for (const auto& witCoin : witnessInfo.witnessSelectionPoolUnfiltered)
    {
        if (::IsMine(*targetWitnessAccount, witCoin.coin.out))
        {
            // Add witness input
            AddTxInput(tx, CInputCoin(witCoin.outpoint, witCoin.coin.out, true, false,witCoin.coin.nHeight, witCoin.coin.nTxIndex), false);

            // Add witness output
            CTxOut renewedWitnessTxOutput;
            CTxOutPoW2Witness witnessDestination;
            if (!GetPow2WitnessOutput(witCoin.coin.out, witnessDestination))
                throw std::runtime_error("Unable to correctly retrieve data");

            // Increment fail count appropriately
            // Note that for upgrade there was no real failure, but the fail count needs to be incremented for the upgrade to pass
            // validation as a renewal
            IncrementWitnessFailCount(witnessDestination.failCount);

            // Ensure consistent lock from
            if (witnessDestination.lockFromBlock == 0)
            {
                witnessDestination.lockFromBlock = witCoin.coin.nHeight;
            }

            renewedWitnessTxOutput.SetType(CTxOutType::PoW2WitnessOutput);
            renewedWitnessTxOutput.output.witnessDetails.spendingKeyID = witnessDestination.spendingKeyID;
            renewedWitnessTxOutput.output.witnessDetails.witnessKeyID = witnessDestination.witnessKeyID;
            renewedWitnessTxOutput.output.witnessDetails.lockFromBlock = witnessDestination.lockFromBlock;
            renewedWitnessTxOutput.output.witnessDetails.lockUntilBlock = witnessDestination.lockUntilBlock;
            renewedWitnessTxOutput.output.witnessDetails.failCount = witnessDestination.failCount;
            renewedWitnessTxOutput.output.witnessDetails.actionNonce = witnessDestination.actionNonce+1;
            renewedWitnessTxOutput.nValue = witCoin.coin.out.nValue;
            tx.vout.push_back(renewedWitnessTxOutput);

            // Add fee input and change output
            std::string sFailReason;
            if (!AddFeeForTransaction(funderAccount, tx, changeReserveKey, nFeeOut, true, sFailReason, &coinControl))
                throw std::runtime_error("Unable to add fee");

            return;
        }
    }

    throw std::runtime_error("Could not find suitable witness to upgrade");
}

bool CWallet::SignAndSubmitTransaction(CReserveKeyOrScript& changeReserveKey, CMutableTransaction& tx, std::string& strError, uint256* pTransactionHashOut, SignType type)
{
    if (!SignTransaction(nullptr, tx, type))
    {
        strError = "Unable to sign transaction";
        return false;
    }

    // Accept the transaction to wallet and broadcast.
    CWalletTx wtxNew;
    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);
    wtxNew.SetTx(MakeTransactionRef(std::move(tx)));
    CValidationState state;
    if (!CommitTransaction(wtxNew, changeReserveKey, g_connman.get(), state))
    {
        strError = "Unable to commit transaction";
        return false;
    }
    if (pTransactionHashOut)
        *pTransactionHashOut = wtxNew.GetHash();

    return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKeyOrScript& reservekey, CConnman* connman, CValidationState& state)
{
    // Refuse to add a null hash to the wallet, as this flags the wallet as corrupted on restart
    if(wtxNew.GetHash().IsNull())
        return false;
    
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.tx->ToString());
        CDataStream ssTx(SER_NETWORK, 0);
        ssTx << wtxNew.tx;
        LogPrintf("TransactionHex[%s]\n", HexStr(ssTx.begin(), ssTx.end()));
        {
            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew, true, true);

            // Notify that old coins are spent
            for(const CTxIn& txin : wtxNew.tx->vin)
            {
                CWalletTx* prev = GetWalletTx(txin.GetPrevOut());
                if (prev)
                {
                    prev->BindWallet(this);
                    NotifyTransactionChanged(this, prev->GetHash(), CT_UPDATED, false);
                }
            }
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        if (fBroadcastTransactions)
        {
            // Broadcast
            if (!wtxNew.AcceptToMemoryPool(maxTxFee, state)) {
                LogPrintf("CommitTransaction(): Transaction cannot be broadcast immediately, %s\n", state.GetRejectReason());
                // TODO: if we expect the failure to be long term or permanent, instead delete wtx from the wallet and return failure.
            } else {
                wtxNew.RelayWalletTransaction(connman);
            }
        }
    }
    return true;
}
