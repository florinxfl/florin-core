// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "account.h"
#include "wallet/wallet.h"
#include <wallet/mnemonic.h>
#include "base58.h"
#include "wallet/walletdb.h"
#include "wallet/crypter.h"

#include <map>

#define BOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/nil_generator.hpp>

std::string GetAccountStateString(AccountState state)
{
    switch(state)
    {
        case Normal:
            return "Normal";
        case Shadow:
            return "Shadow";
        case ShadowChild:
            return "ShadowChild";
        case Deleted:
            return "Deleted";
    }
    return "";
}

std::string getUUIDAsString(const boost::uuids::uuid& uuid)
{
    return boost::uuids::to_string(uuid);
}

boost::uuids::uuid getUUIDFromString(const std::string& uuid)
{
    try
    {
        boost::uuids::string_generator generator;
        return generator(uuid);
    }
    catch(...)
    {
        return boost::uuids::nil_generator()();
    }
}

std::string GetAccountTypeString(AccountType type)
{
    switch(type)
    {
        case Desktop:
            return "Desktop";
        case Mobi:
            return "Mobile";
        case PoW2Witness:
            return "Holding";
        case WitnessOnlyWitnessAccount:
            return "Holding-only holding";
        case ImportedPrivateKeyAccount:
            return "Imported private key";
        case MiningAccount:
            return "Mining";
    }
    return "Regular";
}

CAccount* CreateAccountHelper(CWallet* pwallet, std::string accountName, std::string accountType, bool bMakeActive)
{
    CAccount* account = nullptr;

    if (pwallet->IsLocked())
        return nullptr;

    if (accountType == "HD")
    {
        account = pwallet->GenerateNewAccount(accountName, AccountState::Normal, AccountType::Desktop, bMakeActive);
    }
    else if (accountType == "Mobile")
    {
        account = pwallet->GenerateNewAccount(accountName.c_str(), AccountState::Normal, AccountType::Mobi, bMakeActive);
    }
    else if (accountType == "Witness" || accountType == "Holding")
    {
        account = pwallet->GenerateNewAccount(accountName.c_str(), AccountState::Normal, AccountType::PoW2Witness, bMakeActive);
    }
    else if (accountType == "Mining")
    {
        account = pwallet->GenerateNewAccount(accountName.c_str(), AccountState::Normal, AccountType::MiningAccount, bMakeActive);
    }
    else if (accountType == "Legacy")
    {
        account = pwallet->GenerateNewLegacyAccount(accountName.c_str());
    }

    return account;
}


CHDSeed::CHDSeed()
: m_type(BIP44)
, m_UUID(boost::uuids::nil_generator()())
{
}

CHDSeed::CHDSeed(SecureString mnemonic, SeedType type)
: m_type(type)
, m_UUID(boost::uuids::nil_generator()())
{
    //fixme: (FUT) (ACCOUNTS) Encrypt the seeds immediately upon creation so that they are never written to disk unencrypted.
    unencryptedMnemonic = mnemonic;
    Init();
}

CHDSeed::CHDSeed(CExtPubKey& pubkey, SeedType type)
: m_type(type)
, m_UUID(boost::uuids::nil_generator()())
, m_readOnly(true)
{
    unencryptedMnemonic = "";
    masterKeyPub = pubkey;

    //Random key - not actually used, but written to disk to avoid unnecessary complexity in serialisation code
    masterKeyPriv.GetMutableKey().MakeNewKey(true);
    assert(masterKeyPriv.key.IsValid());
    cointypeKeyPriv.GetMutableKey().MakeNewKey(true);
    purposeKeyPriv.GetMutableKey().MakeNewKey(true);

    InitReadOnly();
}

void CHDSeed::Init()
{
    unsigned char* seed = seedFromMnemonic(unencryptedMnemonic);
    static const std::vector<unsigned char> hashkey = {'N','o','v','o',' ','H','D',' ','s','e','e','d'};
    static const std::vector<unsigned char> hashkeylegacy = {'B','i','t','c','o','i','n',' ','s','e','e','d'};

    // Legacy support for old iOS and android 'Guldencoin' wallets.
    if (m_type == BIP32Legacy || m_type == BIP44External)
    {
        masterKeyPriv.SetMaster(hashkeylegacy, seed, 64);
    }
    else
    {
        masterKeyPriv.SetMaster(hashkey, seed, 64);
    }


    switch (m_type)
    {
        case BIP32:
        case BIP32Legacy:
            masterKeyPriv.Derive(purposeKeyPriv, 100 | BIP32_HARDENED_KEY_LIMIT);  //Unused - but we generate anyway so that we don't save predictable/blank encrypted data to disk (tiny security precaution)
            purposeKeyPriv.Derive(cointypeKeyPriv, 100 | BIP32_HARDENED_KEY_LIMIT);  //Unused - but we generate anyway so that we don't save predictable/blank encrypted data to disk (tiny security precaution)
            break;
        case BIP44External:
        case BIP44:
            {
                masterKeyPriv.Derive(purposeKeyPriv, 44 | BIP32_HARDENED_KEY_LIMIT);  //m/44'
                purposeKeyPriv.Derive(cointypeKeyPriv, 530 | BIP32_HARDENED_KEY_LIMIT);  //m/44'/530'
            }
            break;
        case BIP44NoHardening:
            {
                masterKeyPriv.Derive(purposeKeyPriv, 44);  //m/44
                purposeKeyPriv.Derive(cointypeKeyPriv, 530);  //m/44/530
            }
            break;
        default:
            assert(0);
    }
    masterKeyPub = masterKeyPriv.Neuter();
    purposeKeyPub = purposeKeyPriv.Neuter();
    cointypeKeyPub = cointypeKeyPriv.Neuter();

    if(m_UUID.is_nil())
    {
        m_UUID = boost::uuids::random_generator()();
    }
}

void CHDSeed::InitReadOnly()
{
    assert(m_type == BIP44NoHardening);
    masterKeyPub.Derive(purposeKeyPub, 44);  //m/44
    purposeKeyPub.Derive(cointypeKeyPub, 530);  //m/44/530

    if(m_UUID.is_nil())
    {
        m_UUID = boost::uuids::random_generator()();
    }
}


CAccountHD* CHDSeed::GenerateAccount(AccountType type, CWalletDB* Db)
{
    CAccountHD* account = nullptr;
    switch (type)
    {
        case Desktop:
            assert(m_nAccountIndex < HDMobileStartIndex);
            account = GenerateAccount(m_nAccountIndex, type);
            if (!account)
                return nullptr;
            ++m_nAccountIndex;
            break;
        case Mobi:
            assert(m_nAccountIndexMobi < HDWitnessStartIndex);
            account = GenerateAccount(m_nAccountIndexMobi, type);
            if (!account)
                return nullptr;
            ++m_nAccountIndexMobi;
            break;
        case PoW2Witness:
            assert(m_nAccountIndexWitness < HDMiningStartIndex);
            account = GenerateAccount(m_nAccountIndexWitness, type);
            if (!account)
                return nullptr;
            ++m_nAccountIndexWitness;
            break;
        case MiningAccount:
            assert(m_nAccountIndexMining < HDFutureReservedStartIndex);
            account = GenerateAccount(m_nAccountIndexMining, type);
            if (!account)
                return nullptr;
            ++m_nAccountIndexMining;
            break;
            
        default:
            ; // fall through on purpose with null account
    }

    if (Db)
    {
        //fixme: (FUT) (ACCOUNTS) Can we just set dirty or something and then it gets saved later? That would be cleaner than doing this here.
        Db->WriteHDSeed(*this);
    }

    if (IsCrypted())
    {
        account->Encrypt(vMasterKey);
    }
    return account;
}

CAccountHD* CHDSeed::GenerateAccount(int nAccountIndex, AccountType type)
{
    if ( IsReadOnly() )
    {
        //fixme: (FUT) (ACCOUNTS) (LOW) We should be able to combine this with IsLocked() (BIP44NoHardening) case below to simplify the code here.
        CExtPubKey accountKeyPub;
        cointypeKeyPub.Derive(accountKeyPub, nAccountIndex);  // m/44/530/n (BIP44)
        return new CAccountHD(accountKeyPub, m_UUID, type);
    }
    else if (IsLocked())
    {
        return nullptr;
    }
    else
    {
        CExtKey accountKeyPriv;
        GetPrivKeyForAccountInternal(nAccountIndex, accountKeyPriv);
        return new CAccountHD(accountKeyPriv, m_UUID, type);
    }
}

bool CHDSeed::GetPrivKeyForAccount(uint64_t nAccountIndex, CExtKey& accountKeyPriv)
{
    if (IsReadOnly() || IsLocked())
        return false;

    GetPrivKeyForAccountInternal(nAccountIndex, accountKeyPriv);
    return true;
}

bool CHDSeed::GetPrivKeyForAccountInternal(uint64_t nAccountIndex, CExtKey& accountKeyPriv)
{
    switch (m_type)
    {
        case BIP32:
        case BIP32Legacy:
            masterKeyPriv.Derive(accountKeyPriv, nAccountIndex | BIP32_HARDENED_KEY_LIMIT);  // m/n' (BIP32) 
            break;
        case BIP44:
        case BIP44External:
            cointypeKeyPriv.Derive(accountKeyPriv, nAccountIndex | BIP32_HARDENED_KEY_LIMIT);  // m/44'/530'/n' (BIP44)
            break;
        case BIP44NoHardening:
            cointypeKeyPriv.Derive(accountKeyPriv, nAccountIndex);  // m/44'/530'/n (BIP44 without hardening (for read only sync))
            break;
    }
    return true;
}

boost::uuids::uuid CHDSeed::getUUID() const
{
    return m_UUID;
}


SecureString CHDSeed::getMnemonic()
{
    return unencryptedMnemonic;
}

SecureString CHDSeed::getPubkey()
{
    return CEncodedSecretKeyExt<CExtPubKey>(masterKeyPub).ToString().c_str();
}

bool CHDSeed::IsLocked() const
{
    if (unencryptedMnemonic.size() > 0 || m_readOnly)
    {
        return false;
    }
    return true;
}

bool CHDSeed::IsCrypted() const
{
    return encrypted;
}

bool CHDSeed::Lock()
{
    // We can't lock if we are not encrypted, nothing to do.
    if (!encrypted)
        return false;

    // The below is a 'SecureString' - so no memory burn necessary, it should burn itself.
    unencryptedMnemonic = "";
    // The below is a 'SecureString' - so no memory burn necessary, it should burn itself.
    masterKeyPriv = CExtKey();
    purposeKeyPriv = CExtKey();
    cointypeKeyPriv = CExtKey();

    //fixme: (FUT) (ACCOUNTS) - Also burn the memory just to be sure?
    vMasterKey.clear();

    return true;
}

bool CHDSeed::Unlock(const CKeyingMaterial& vMasterKeyIn)
{
    // Decrypt mnemonic
    assert(sizeof(m_UUID) == WALLET_CRYPTO_IV_SIZE);
    CKeyingMaterial vchMnemonic;
    if (!DecryptSecret(vMasterKeyIn, encryptedMnemonic, std::vector<unsigned char>(m_UUID.begin(), m_UUID.end()), vchMnemonic))
        return false;
    unencryptedMnemonic = SecureString(vchMnemonic.begin(), vchMnemonic.end());

    // Decrypt master key
    CKeyingMaterial vchMasterKeyPrivEncoded;
    if (!DecryptSecret(vMasterKeyIn, masterKeyPrivEncrypted, masterKeyPub.pubkey.GetHash(), vchMasterKeyPrivEncoded))
        return false;
    masterKeyPriv.Decode(vchMasterKeyPrivEncoded.data());

    // Decrypt purpose key
    CKeyingMaterial vchPurposeKeyPrivEncoded;
    if (!DecryptSecret(vMasterKeyIn, purposeKeyPrivEncrypted, purposeKeyPub.pubkey.GetHash(), vchPurposeKeyPrivEncoded))
        return false;
    purposeKeyPriv.Decode(vchPurposeKeyPrivEncoded.data());

    // Decrypt coin type key
    CKeyingMaterial vchCoinTypeKeyPrivEncoded;
    if (!DecryptSecret(vMasterKeyIn, cointypeKeyPrivEncrypted, cointypeKeyPub.pubkey.GetHash(), vchCoinTypeKeyPrivEncoded))
        return false;
    cointypeKeyPriv.Decode(vchCoinTypeKeyPrivEncoded.data());

    vMasterKey = vMasterKeyIn;

    return true;
}

bool CHDSeed::Encrypt(const CKeyingMaterial& vMasterKeyIn)
{
    // Encrypt mnemonic
    assert(sizeof(m_UUID) == WALLET_CRYPTO_IV_SIZE);
    encryptedMnemonic.clear();
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(unencryptedMnemonic.begin(), unencryptedMnemonic.end()), std::vector<unsigned char>(m_UUID.begin(), m_UUID.end()), encryptedMnemonic))
    {
        LogPrintf("CHDSeed::Encrypt failed to encrypt mnemonic");
        return false;
    }

    // Encrypt master key
    SecureUnsignedCharVector masterKeyPrivEncoded(BIP32_EXTKEY_SIZE);
    masterKeyPriv.Encode(masterKeyPrivEncoded.data());
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(masterKeyPrivEncoded.begin(), masterKeyPrivEncoded.end()), masterKeyPub.pubkey.GetHash(), masterKeyPrivEncrypted))
    {
        LogPrintf("CHDSeed::Encrypt failed to encrypt master key");
        return false;
    }

    // Encrypt purpose key
    SecureUnsignedCharVector purposeKeyPrivEncoded(BIP32_EXTKEY_SIZE);
    purposeKeyPriv.Encode(purposeKeyPrivEncoded.data());
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(purposeKeyPrivEncoded.begin(), purposeKeyPrivEncoded.end()), purposeKeyPub.pubkey.GetHash(), purposeKeyPrivEncrypted))
    {
        LogPrintf("CHDSeed::Encrypt failed to encrypt purpose key");
        return false;
    }

    // Encrypt coin type key
    SecureUnsignedCharVector cointypeKeyPrivEncoded(BIP32_EXTKEY_SIZE);
    cointypeKeyPriv.Encode(cointypeKeyPrivEncoded.data());
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(cointypeKeyPrivEncoded.begin(), cointypeKeyPrivEncoded.end()), cointypeKeyPub.pubkey.GetHash(), cointypeKeyPrivEncrypted))
    {
        LogPrintf("CHDSeed::Encrypt failed to encrypt coin type key");
        return false;
    }

    encrypted = true;
    vMasterKey = vMasterKeyIn;

    return true;
}



CAccountHD::CAccountHD(CExtKey accountKey_, boost::uuids::uuid seedID, AccountType type)
: CAccount()
, m_SeedID(seedID)
, m_nIndex(accountKey_.nChild)
, accountKeyPriv(accountKey_)
{
    m_Type = type;

    accountKeyPriv.Derive(primaryChainKeyPriv, 0);  //a'/0
    if (m_Type != AccountType::PoW2Witness) 
        accountKeyPriv.Derive(changeChainKeyPriv, 1);  //a'/1
    else
        accountKeyPriv.Derive(changeChainKeyPriv, 1 | BIP32_HARDENED_KEY_LIMIT);  //a'/1' - Witness account requires hardening at the change level for extra security (it will be quite common to share witness account change keys)
    primaryChainKeyPub = primaryChainKeyPriv.Neuter();
    changeChainKeyPub = changeChainKeyPriv.Neuter();
}

CAccountHD::CAccountHD(CExtPubKey accountKey_, boost::uuids::uuid seedID, AccountType type)
: CAccount()
, m_SeedID(seedID)
, m_nIndex(accountKey_.nChild)
{
    m_Type = type;

    //Random key - not actually used, but written to disk to avoid unnecessary complexity in serialisation code
    accountKeyPriv.GetMutableKey().MakeNewKey(true);
    primaryChainKeyPriv.GetMutableKey().MakeNewKey(true);
    changeChainKeyPriv.GetMutableKey().MakeNewKey(true);

    //NB! The change keys will be wrong here for read only witness accounts, but it doesn't matter because we only need to match one of the two keys to be able to tell it is ours.
    accountKey_.Derive(primaryChainKeyPub, 0);  //a/0
    accountKey_.Derive(changeChainKeyPub, 1);  //a/1
    m_readOnly = true;
}

bool CAccountHD::GetAccountKeyIDWithHighestIndex(CKeyID& HDKeyID, int nChain) const
{
    if (nChain == KEYCHAIN_EXTERNAL)
    {
        return externalKeyStore.GetKeyIDWithHighestIndex(HDKeyID);
    }
    else
    {
        return internalKeyStore.GetKeyIDWithHighestIndex(HDKeyID);
    }
}

bool CAccountHD::GetKey(CExtKey& childKey, int nChain) const
{
    assert(!m_readOnly);
    assert(!IsLocked());
    if (nChain == KEYCHAIN_EXTERNAL)
    {
        return primaryChainKeyPriv.Derive(childKey, m_nNextChildIndex++);
    }
    else
    {
        return changeChainKeyPriv.Derive(childKey, m_nNextChangeIndex++);
    }
}

bool CAccountHD::GetKey(const CKeyID& keyID, CKey& key) const
{
    assert(!m_readOnly);
    assert(IsHD());

    // Witness keys are a special case.
    // They are stored instead of generated on demand (so that they work in encrypted wallets)
    if (IsPoW2Witness())
    {
        if (internalKeyStore.GetKey(keyID, key))
        {
            // But skip it if it is an empty key... (Which can happen by design)
            // Technical: Witness keystore is filled with "public/empty" pairs as the public keys are created and then populated on demand with "public/witness" keys only when they are needed/generated (first use)
            if (key.IsValid())
                return true;
        }
    }

    if (IsLocked())
        return false;

    int64_t nKeyIndex = -1;
    CExtKey privKey;
    if (externalKeyStore.GetKey(keyID, nKeyIndex))
    {
        primaryChainKeyPriv.Derive(privKey, nKeyIndex);
        if(privKey.Neuter().pubkey.GetID() != keyID)
            assert(0);
        key = privKey.key;
        return true;
    }
    if (internalKeyStore.GetKey(keyID, nKeyIndex))
    {
        changeChainKeyPriv.Derive(privKey, nKeyIndex);
        if(privKey.Neuter().pubkey.GetID() != keyID)
            assert(0);
        key = privKey.key;
        return true;
    }
    return false;
}

bool CAccountHD::GetKey([[maybe_unused]] const CKeyID &address, [[maybe_unused]] std::vector<unsigned char>& encryptedKeyOut) const
{
    assert(0); // asserts are always compiled in even in releae builds, should this ever change we fallback to the throw below
    throw std::runtime_error("Should never call CAccountHD::GetKey(const CKeyID &address, std::vector<unsigned char>& encryptedKeyOut)");
}

void CAccountHD::GetPubKey(CExtPubKey& childKey, int nChain) const
{
    if (nChain == KEYCHAIN_EXTERNAL)
    {
        primaryChainKeyPub.Derive(childKey, m_nNextChildIndex++);
    }
    else
    {
        changeChainKeyPub.Derive(childKey, m_nNextChangeIndex++);
    }
}

bool CAccountHD::GetPubKeyManual(int64_t HDKeyIndex, int keyChain, CExtPubKey& childKey) const
{
    if (keyChain == KEYCHAIN_EXTERNAL)
    {
        primaryChainKeyPub.Derive(childKey, HDKeyIndex);
        return true;
    }
    else
    {
        changeChainKeyPub.Derive(childKey, HDKeyIndex);
        return true;
    }
    return false;
}

bool CAccountHD::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    int64_t nKeyIndex = -1;
    if(externalKeyStore.GetKey(address, nKeyIndex))
    {
        CExtPubKey extPubKey;
        primaryChainKeyPub.Derive(extPubKey, nKeyIndex);
        vchPubKeyOut = extPubKey.pubkey;
        return true;
    }
    if(internalKeyStore.GetKey(address, nKeyIndex))
    {
        CExtPubKey extPubKey;
        changeChainKeyPub.Derive(extPubKey, nKeyIndex);
        vchPubKeyOut = extPubKey.pubkey;
        return true;
    }
    return false;
}

bool CAccountHD::Lock()
{
    // NB! We don't encrypt the keystores for HD accounts - as they only contain public keys.
    // So we don't need to unlock the underlying keystore.
    if (!IsReadOnly())
    {
        return true;
    }

    // We can't lock if we are not encrypted, nothing to do.
    if (!encrypted)
        return false;

    // We don't encrypt the keystores for HD accounts - as they only contain public keys.
    // The exception being witness accounts, which may contain (select) witness keys in their keystores.
    // However these must -never- be encrypted anyway.
    //CAccount::Lock()

    //fixme: (FUT) (ACCOUNTS) - Also burn the memory just to be sure?
    vMasterKey.clear();

    //fixme: (FUT) (ACCOUNTS) burn the memory here.
    accountKeyPriv = CExtKey();
    primaryChainKeyPriv = CExtKey();
    changeChainKeyPriv = CExtKey();

    return true;
}

bool CAccountHD::Unlock(const CKeyingMaterial& vMasterKeyIn, bool& needsWriteToDisk)
{
    needsWriteToDisk = false;
    assert(sizeof(accountUUID) == WALLET_CRYPTO_IV_SIZE);

    // NB! We don't encrypt the keystores for HD accounts - as they only contain public keys.
    // So we don't need to unlock the underlying keystore.
    if (IsReadOnly())
        return true;

    // Decrypt account key
    CKeyingMaterial vchAccountKeyPrivEncoded;
    if (!DecryptSecret(vMasterKeyIn, accountKeyPrivEncrypted, std::vector<unsigned char>(accountUUID.begin(), accountUUID.end()), vchAccountKeyPrivEncoded))
    {
        LogPrintf("CAccountHD::Unlock Failed to decrypt secret account key");
        return false;
    }
    accountKeyPriv.Decode(vchAccountKeyPrivEncoded.data());

    // Decrypt primary chain key
    CKeyingMaterial vchPrimaryChainKeyPrivEncoded;
    if (!DecryptSecret(vMasterKeyIn, primaryChainKeyEncrypted, primaryChainKeyPub.pubkey.GetHash(), vchPrimaryChainKeyPrivEncoded))
    {
        LogPrintf("CAccountHD::Unlock Failed to decrypt secret primary chain key");
        return false;
    }
    primaryChainKeyPriv.Decode(vchPrimaryChainKeyPrivEncoded.data());

    // Decrypt change chain key
    CKeyingMaterial vchChangeChainKeyPrivEncoded;
    if (!DecryptSecret(vMasterKeyIn, changeChainKeyEncrypted, changeChainKeyPub.pubkey.GetHash(), vchChangeChainKeyPrivEncoded))
    {
        LogPrintf("CAccountHD::Unlock Failed to decrypt secret change chanin key");
        return false;
    }
    changeChainKeyPriv.Decode(vchChangeChainKeyPrivEncoded.data());

    vMasterKey = vMasterKeyIn;

    return true;
}

bool CAccountHD::Encrypt(const CKeyingMaterial& vMasterKeyIn)
{
    assert(sizeof(accountUUID) == WALLET_CRYPTO_IV_SIZE);

    if (IsReadOnly())
    {
        return true;
    }

    // NB! We don't encrypt the keystores for HD accounts - as they only contain public keys.

    // Encrypt account key
    SecureUnsignedCharVector accountKeyPrivEncoded(BIP32_EXTKEY_SIZE);
    accountKeyPriv.Encode(accountKeyPrivEncoded.data());
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(accountKeyPrivEncoded.begin(), accountKeyPrivEncoded.end()), std::vector<unsigned char>(accountUUID.begin(), accountUUID.end()), accountKeyPrivEncrypted))
        return false;

    // Encrypt primary chain key
    SecureUnsignedCharVector primaryChainKeyPrivEncoded(BIP32_EXTKEY_SIZE);
    primaryChainKeyPriv.Encode(primaryChainKeyPrivEncoded.data());
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(primaryChainKeyPrivEncoded.begin(), primaryChainKeyPrivEncoded.end()), primaryChainKeyPub.pubkey.GetHash(), primaryChainKeyEncrypted))
        return false;

    // Encrypt change chain key
    SecureUnsignedCharVector changeChainKeyPrivEncoded(BIP32_EXTKEY_SIZE);
    changeChainKeyPriv.Encode(changeChainKeyPrivEncoded.data());
    if (!EncryptSecret(vMasterKeyIn, CKeyingMaterial(changeChainKeyPrivEncoded.begin(), changeChainKeyPrivEncoded.end()), changeChainKeyPub.pubkey.GetHash(), changeChainKeyEncrypted))
        return false;

    encrypted = true;
    vMasterKey = vMasterKeyIn;

    return true;
}

bool CAccountHD::IsCrypted() const
{
    return encrypted;
}

bool CAccountHD::IsLocked() const
{
    return !accountKeyPriv.key.IsValid();
}

bool CAccountHD::AddKeyPubKey(const CKey& key, const CPubKey &pubkey, int keyChain)
{
    // This should never be called on a HD account.
    // We don't store private keys for HD we just generate them as needed.
    // The exception being PoW² witnesses which may contain witness keys unencrypted (change chain keys)
    assert(IsPoW2Witness());
    assert(keyChain == KEYCHAIN_WITNESS);
    return internalKeyStore.AddKeyPubKey(key, pubkey);
}

bool CAccountHD::AddKeyPubKey(int64_t HDKeyIndex, const CPubKey &pubkey, int keyChain)
{
    if(keyChain == KEYCHAIN_EXTERNAL)
        return externalKeyStore.AddKeyPubKey(HDKeyIndex, pubkey);
    else
    {
        // Add public key with null private key - we later write the private key IFF it is used in a witnessing operation
        if (IsPoW2Witness())
        {
            CKey nullKey;
            //fixme: (FUT) (ACCOUNTS) This is possibly a bit inefficient - see if it can be sped up.
            if(!internalKeyStore.HaveKey(pubkey.GetID()))
            {
                if(!internalKeyStore.AddKeyPubKey(nullKey, pubkey))
                {
                    throw std::runtime_error("CAccountHD::AddKeyPubKey failed to store witness key.");
                }
            }
        }
        return internalKeyStore.AddKeyPubKey(HDKeyIndex, pubkey);
    }
}


CPubKey CAccountHD::GenerateNewKey(CWallet& wallet, CKeyMetadata& metadata, int keyChain)
{
    CExtPubKey childKey;
    do
    {
        GetPubKey(childKey, keyChain);
    }
    while( wallet.HaveKey(childKey.pubkey.GetID()) );//fixme: (FUT) (ACCOUNTS) (BIP44) No longer need wallet here.

    //LogPrintf("CAccount::GenerateNewKey(): NewHDKey [%s]\n", CNativeAddress(childKey.pubkey.GetID()).ToString());

    metadata.hdKeypath = std::string("m/44'/530'/") +  std::to_string(m_nIndex)  + "/" + std::to_string(keyChain) + "/" + std::to_string(childKey.nChild) + "'";
    metadata.hdAccountUUID = getUUIDAsString(getUUID());

    if (!wallet.AddHDKeyPubKey(childKey.nChild, childKey.pubkey, *this, keyChain))
        throw std::runtime_error("CAccount::GenerateNewKey(): AddKeyPubKey failed");

    return childKey.pubkey;
}


CExtKey* CAccountHD::GetAccountMasterPrivKey()
{
    if (IsLocked())
        return NULL;

    return &accountKeyPriv;
}

SecureString CAccountHD::GetAccountMasterPubKeyEncoded()
{
    if (IsLocked())
        return NULL;

    return CEncodedSecretKeyExt<CExtPubKey>(accountKeyPriv.Neuter()).ToString().c_str();
}

boost::uuids::uuid CAccountHD::getSeedUUID() const
{
    return m_SeedID;
}

uint32_t CAccountHD::getIndex()
{
    return m_nIndex;
}

CAccount::CAccount()
{
    SetNull();
    //Start at current time and go backwards as we find transactions.
    earliestPossibleCreationTime = GetTime();
    accountUUID = boost::uuids::random_generator()();
    parentUUID = boost::uuids::nil_generator()();
    m_State = AccountState::Normal;
    m_Type = AccountType::Desktop;
    m_readOnly = false;
}

void CAccount::SetNull()
{
    vchPubKey = CPubKey();
}

CPubKey CAccount::GenerateNewKey(CWallet& wallet, CKeyMetadata& metadata, int keyChain)
{
    if (IsFixedKeyPool())
        throw std::runtime_error(strprintf("GenerateNewKey called on a \"%s\" witness account - this is invalid", GetAccountTypeString(m_Type).c_str()));

    CKey secret;
    secret.MakeNewKey(true);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    if (!wallet.AddKeyPubKey(secret, pubkey, *this, keyChain))
        throw std::runtime_error("CAccount::GenerateNewKey(): AddKeyPubKey failed");

    return pubkey;
}


//fixme: (FUT) (ACCOUNTS) (WATCH_ONLY)
bool CAccount::HaveWalletTx(const CTransaction& tx)
{
    for(const CTxOut& txout : tx.vout)
    {
        isminetype ret = isminetype::ISMINE_NO;
        for (auto keyChain : { KEYCHAIN_EXTERNAL, KEYCHAIN_CHANGE })
        {
            isminetype temp = ( keyChain == KEYCHAIN_EXTERNAL ? IsMine(externalKeyStore, txout) : IsMine(internalKeyStore, txout) );
            if (temp > ret)
                ret = temp;
        }
        if (ret > isminetype::ISMINE_NO)
            return true;
    }
    for(const CTxIn& txin : tx.vin)
    {
        isminetype ret = isminetype::ISMINE_NO;
        const CWalletTx* prev = pactiveWallet->GetWalletTx(txin.GetPrevOut());
        if (prev && prev->tx->vout.size() != 0)
        {
            if (txin.GetPrevOut().n < prev->tx->vout.size())
            {
                for(const CTxOut& txout : prev->tx->vout)
                {
                    for (auto keyChain : { KEYCHAIN_EXTERNAL, KEYCHAIN_CHANGE })
                    {
                        isminetype temp = ( keyChain == KEYCHAIN_EXTERNAL ? IsMine(externalKeyStore, txout) : IsMine(internalKeyStore, txout) );
                        if (temp > ret)
                            ret = temp;
                    }
                }
            }
        }
        if (ret > isminetype::ISMINE_NO)
            return true;
    }
    return false;
}


bool CAccount::HaveKey(const CKeyID &address) const
{
    return externalKeyStore.HaveKey(address) || internalKeyStore.HaveKey(address);
}

bool CAccount::HaveKeyInternal(const CKeyID &address) const
{
    return internalKeyStore.HaveKey(address);
}

bool CAccount::HaveWatchOnly(const CScript &dest) const
{
    return externalKeyStore.HaveWatchOnly(dest) || internalKeyStore.HaveWatchOnly(dest);
}

bool CAccount::HaveWatchOnly() const
{
    return externalKeyStore.HaveWatchOnly() || internalKeyStore.HaveWatchOnly();
}

bool CAccount::HaveCScript(const CScriptID &hash) const
{
    return externalKeyStore.HaveCScript(hash) || internalKeyStore.HaveCScript(hash);
}

bool CAccount::GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const
{
    return externalKeyStore.GetCScript(hash, redeemScriptOut) || internalKeyStore.GetCScript(hash, redeemScriptOut);
}

bool CAccount::IsLocked() const
{
    return externalKeyStore.IsLocked() || internalKeyStore.IsLocked();
}

bool CAccount::IsCrypted() const
{
    return externalKeyStore.IsCrypted() || internalKeyStore.IsCrypted();
}

bool CAccount::Lock()
{
    // NB! We don't encrypt the keystores for witness-only accounts - as they only contain keys for witnessing.
    // So we don't need to unlock the underlying keystore..
    if (IsWitnessOnly())
    {
        return true;
    }

    //fixme: (FUT) (ACCOUNTS) - Also burn the memory just to be sure?
    vMasterKey.clear();

    return externalKeyStore.Lock() && internalKeyStore.Lock();
}

bool CAccount::Unlock(const CKeyingMaterial& vMasterKeyIn, bool& needsWriteToDisk)
{
    // NB! We don't encrypt the keystores for witness-only accounts - as they only contain keys for witnessing.
    // So we don't need to unlock the underlying keystore..
    if (IsWitnessOnly())
    {
        return true;
    }

    needsWriteToDisk = false;
    vMasterKey = vMasterKeyIn;

    return externalKeyStore.Unlock(vMasterKeyIn, needsWriteToDisk) && internalKeyStore.Unlock(vMasterKeyIn, needsWriteToDisk);
}

bool CAccount::GetKey(const CKeyID& keyID, CKey& key) const
{
    return externalKeyStore.GetKey(keyID, key) || internalKeyStore.GetKey(keyID, key);
}

bool CAccount::GetKey(const CKeyID &address, std::vector<unsigned char>& encryptedKeyOut) const
{
    return externalKeyStore.GetKey(address, encryptedKeyOut) || internalKeyStore.GetKey(address, encryptedKeyOut);
}

void CAccount::GetKeys(std::set<CKeyID> &setAddress) const
{
    std::set<CKeyID> setExternal;
    externalKeyStore.GetKeys(setExternal);
    std::set<CKeyID> setInternal;
    internalKeyStore.GetKeys(setInternal);
    setAddress.clear();
    setAddress.insert(setExternal.begin(), setExternal.end());
    setAddress.insert(setInternal.begin(), setInternal.end());
}

void CAccount::GetKeys(std::set<CKeyID> &setKeysExternal, std::set<CKeyID> &setKeysInternal) const
{
    externalKeyStore.GetKeys(setKeysExternal);
    internalKeyStore.GetKeys(setKeysInternal);
}

bool CAccount::EncryptKeys(const CKeyingMaterial& vMasterKeyIn)
{
    if (!externalKeyStore.EncryptKeys(vMasterKeyIn))
        return false;
    if (!internalKeyStore.EncryptKeys(vMasterKeyIn))
        return false;

    if (pactiveWallet)
    {
        {
            std::set<CKeyID> setAddress;
            GetKeys(setAddress);

            LOCK(pactiveWallet->cs_wallet);
            for (const auto& keyID : setAddress)
            {
                CPubKey pubKey;
                if (!GetPubKey(keyID, pubKey))
                {
                    LogPrintf("CAccount::EncryptKeys(): Failed to get pubkey\n");
                    return false;
                }
                if (pactiveWallet->pwalletdbEncryption)
                    pactiveWallet->pwalletdbEncryption->EraseKey(pubKey);
                else
                    CWalletDB(*pactiveWallet->dbw).EraseKey(pubKey);

                std::vector<unsigned char> secret;
                if (!GetKey(keyID, secret))
                { 
                    LogPrintf("CAccount::EncryptKeys(): Failed to get crypted key\n");
                    return false;
                }
                if (pactiveWallet->pwalletdbEncryption)
                {
                    if (!pactiveWallet->pwalletdbEncryption->WriteCryptedKey(pubKey, secret, pactiveWallet->mapKeyMetadata[keyID], getUUIDAsString(getUUID()), KEYCHAIN_EXTERNAL))
                    {
                        LogPrintf("CAccount::EncryptKeys(): Failed to write key\n");
                        return false;
                    }
                }
                else
                {
                    if (!CWalletDB(*pactiveWallet->dbw).WriteCryptedKey(pubKey, secret, pactiveWallet->mapKeyMetadata[keyID], getUUIDAsString(getUUID()), KEYCHAIN_EXTERNAL))
                    {
                        LogPrintf("CAccount::EncryptKeys(): Failed to write key\n");
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool CAccount::Encrypt(const CKeyingMaterial& vMasterKeyIn)
{
    // NB! We don't encrypt the keystores for witness-only accounts - as they only contain keys for witnessing.
    // So we don't need to unlock the underlying keystore..
    if (IsWitnessOnly())
    {
        return true;
    }

    bool needsWriteToDisk;
    if (!EncryptKeys(vMasterKeyIn))
        return false;
    if (!externalKeyStore.SetCrypted() || !externalKeyStore.Unlock(vMasterKeyIn, needsWriteToDisk))
        return false;
    if (!internalKeyStore.SetCrypted() || !internalKeyStore.Unlock(vMasterKeyIn, needsWriteToDisk))
        return false;

    vMasterKey = vMasterKeyIn;

    return true;
}



bool CAccount::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return externalKeyStore.GetPubKey(address, vchPubKeyOut) || internalKeyStore.GetPubKey(address, vchPubKeyOut);
}


bool CAccount::AddKeyPubKey(const CKey& key, const CPubKey &pubkey, int keyChain)
{
    if(keyChain == KEYCHAIN_EXTERNAL)
    {
        return externalKeyStore.AddKeyPubKey(key, pubkey);
    }
    else
    {
        return internalKeyStore.AddKeyPubKey(key, pubkey);
    }
}

bool CAccount::AddKeyPubKey(int64_t HDKeyIndex, const CPubKey &pubkey, int keyChain)
{
    //This should never be called on a non-HD wallet
    assert(0);
    return true;
}

bool CAccount::AddWatchOnly(const CScript &dest)
{
    //fixme: (FUT) (ACCOUNTS) (WATCH_ONLY) (REIMPLEMENT AS SPECIAL WATCH ACCOUNT)
    assert(0);
    return externalKeyStore.AddWatchOnly(dest);
}

bool CAccount::RemoveWatchOnly(const CScript &dest)
{
    //fixme: (FUT) (ACCOUNTS) (WATCH_ONLY) (REIMPLEMENT AS SPECIAL WATCH ACCOUNT)
    assert(0);
    return externalKeyStore.RemoveWatchOnly(dest) || internalKeyStore.RemoveWatchOnly(dest);
}

//checkme: CHILD ACCOUNTS - DOES THIS END UP IN THE RIGHT PLACE?
//fixme: (FUT) (ACCOUNTS)
bool CAccount::AddCScript(const CScript& redeemScript)
{
    return externalKeyStore.AddCScript(redeemScript);
}

bool CAccount::AddCryptedKeyWithChain(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret, int64_t nKeyChain)
{
    //fixme: (FUT) (ACCOUNTS) This is essentially dead code now - it has been replaced at the bottom of CWallet::AddKeyPubKey
    //For technical reasons (wallet upgrade)

    //This should never be called on a non-HD wallet
    assert(!IsHD());

    if (nKeyChain == KEYCHAIN_EXTERNAL)
    {
        if (!externalKeyStore.AddCryptedKey(vchPubKey, vchCryptedSecret))
            return false;
    }
    else
    {
        if (!internalKeyStore.AddCryptedKey(vchPubKey, vchCryptedSecret))
            return false;
    }

    // If we don't have a wallet yet (busy during wallet upgrade) - then the below not being called is fine as the wallet does a 'force resave' of all keys at the end of the upgrade.
    if (pactiveWallet)
    {
        {
            LOCK(pactiveWallet->cs_wallet);
            if (pactiveWallet->pwalletdbEncryption)
                return pactiveWallet->pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, pactiveWallet->mapKeyMetadata[vchPubKey.GetID()], getUUIDAsString(getUUID()), nKeyChain);
            else
                return CWalletDB(*pactiveWallet->dbw).WriteCryptedKey(vchPubKey, vchCryptedSecret, pactiveWallet->mapKeyMetadata[vchPubKey.GetID()], getUUIDAsString(getUUID()), nKeyChain);
        }
    }
    else
    {
        return true;
    }
    return false;
}

void CAccount::AddChild(CAccount* childAccount)
{
    childAccount->parentUUID = accountUUID;
}

void CAccount::possiblyUpdateEarliestTime(uint64_t creationTime, CWalletDB* Db)
{
    if (creationTime < earliestPossibleCreationTime)
        earliestPossibleCreationTime = creationTime;

    //fixme: (FUT) (ACCOUNTS) Can we just set dirty or something and then it gets saved later? This would be cleaner instead of taking the Db here and writing directly to it...
    if (Db)
    {
        Db->WriteAccount(getUUIDAsString(getUUID()), this);
    }
}

uint64_t CAccount::getEarliestPossibleCreationTime()
{
    return earliestPossibleCreationTime;
}

unsigned int CAccount::GetKeyPoolSize()
{
    AssertLockHeld(cs_keypool); // setKeyPool
    return setKeyPoolExternal.size();
}

unsigned int CAccount::GetKeyPoolSize(int nChain)
{
    AssertLockHeld(cs_keypool); // setKeyPool
    if (nChain == KEYCHAIN_EXTERNAL)
    {
        return setKeyPoolExternal.size();
    }
    else
    {
        return setKeyPoolInternal.size();
    }
}

std::string CAccount::getLabel() const
{
    return accountLabel;
}

void CAccount::setLabel(const std::string& label, CWalletDB* Db)
{
    accountLabel = label;
    if (Db)
    {
        Db->EraseAccountLabel(getUUIDAsString(getUUID()));
        Db->WriteAccountLabel(getUUIDAsString(getUUID()), label);
    }
}

void CAccount::addLink(const std::string& serviceName, const std::string& serviceData, CWalletDB* Db)
{
    if (accountLinks.count(serviceName) == 0 || accountLinks[serviceName] != serviceData)
    {
        accountLinks[serviceName] = serviceData;
        if (Db)
        {
            Db->EraseAccountLinks(getUUIDAsString(getUUID()));
            Db->WriteAccountLinks(getUUIDAsString(getUUID()), accountLinks);
            if (pactiveWallet)
            {
                dynamic_cast<CExtWallet*>(pactiveWallet)->NotifyAccountModified(pactiveWallet, this);
            }
        }
    }
}

void CAccount::removeLink(const std::string& serviceName, CWalletDB* Db)
{
    if (accountLinks.find(serviceName) != accountLinks.end())
    {
        accountLinks.erase(accountLinks.find(serviceName));
        if (Db)
        {
            Db->EraseAccountLinks(getUUIDAsString(getUUID()));
            Db->WriteAccountLinks(getUUIDAsString(getUUID()), accountLinks);
            if (pactiveWallet)
            {
                dynamic_cast<CExtWallet*>(pactiveWallet)->NotifyAccountModified(pactiveWallet, this);
            }
        }
    }
}

std::map<std::string, std::string> CAccount::getLinks() const
{
    return accountLinks;
}

void CAccount::loadLinks(std::map<std::string, std::string>& accountLinks_)
{
    accountLinks = accountLinks_;
}

CAmount CAccount::getCompounding() const
{
    // If compound earnings percent is set in any way then it overrides this, in which case we always set 0.
    if (compoundEarningsPercent == std::numeric_limits<int32_t>::max())
    {
        return compoundEarnings;
    }
    else
    {
        return 0;
    }
}

void CAccount::setCompounding(CAmount compoundAmount_, CWalletDB* Db)
{
    if (pactiveWallet)
    {
        dynamic_cast<CExtWallet*>(pactiveWallet)->NotifyAccountCompoundingChanged(pactiveWallet, this);
    }
    compoundEarnings = compoundAmount_;
    if (Db)
    {
        Db->EraseAccountCompoundingSettings(getUUIDAsString(getUUID()));
        if (compoundEarnings > 0)
        {
            Db->WriteAccountCompoundingSettings(getUUIDAsString(getUUID()), compoundEarnings);
        }
    }
}


int32_t CAccount::getCompoundingPercent() const
{
    if (compoundEarningsPercent == std::numeric_limits<int32_t>::max())
        return 0;
    return compoundEarningsPercent;
}

void CAccount::setCompoundingPercent(int32_t compoundPercent_, CWalletDB* Db)
{
    if (pactiveWallet)
    {
        dynamic_cast<CExtWallet*>(pactiveWallet)->NotifyAccountCompoundingChanged(pactiveWallet, this);
    }
    compoundEarningsPercent = compoundPercent_;
    if (Db)
    {
        Db->EraseAccountCompoundingSettings(getUUIDAsString(getUUID()));
        Db->EraseAccountCompoundingPercentSettings(getUUIDAsString(getUUID()));
        Db->WriteAccountCompoundingPercentSettings(getUUIDAsString(getUUID()), compoundEarningsPercent);
    }
}

bool CAccount::hasNonCompoundRewardScript() const
{
    return !nonCompoundRewardScript.empty();
}

CScript CAccount::getNonCompoundRewardScript() const
{
    return nonCompoundRewardScript;
}

void CAccount::setNonCompoundRewardScript(const CScript& rewardScript, CWalletDB* Db)
{
    nonCompoundRewardScript = rewardScript;
    if (Db)
    {
        Db->EraseAccountNonCompoundWitnessEarningsScript(getUUIDAsString(getUUID()));
        Db->WriteAccountNonCompoundWitnessEarningsScript(getUUIDAsString(getUUID()), nonCompoundRewardScript);
    }
}

bool CAccount::hasRewardTemplate() const
{
    return !rewardTemplate.empty();
}

CWitnessRewardTemplate CAccount::getRewardTemplate() const
{
    return rewardTemplate;
}

void CAccount::setRewardTemplate(const CWitnessRewardTemplate& _rewardTemplate, CWalletDB* Db)
{
    rewardTemplate = _rewardTemplate;
    if (Db)
    {
        Db->EraseAccountRewardTemplate(getUUIDAsString(getUUID()));
        Db->WriteAccountRewardTemplate(getUUIDAsString(getUUID()), rewardTemplate);
    }
}

boost::uuids::uuid CAccount::getUUID() const
{
    return accountUUID;
}

void CAccount::setUUID(const std::string& stringUUID)
{
    accountUUID = getUUIDFromString(stringUUID);
}

boost::uuids::uuid CAccount::getParentUUID() const
{
    return parentUUID;
}




