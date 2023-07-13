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

#include "crypter.h"

#include "crypto/sha512.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"

#include <string>
#include <vector>

#include <cryptopp/config.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>

int CCrypter::BytesToKeySHA512AES(const std::vector<unsigned char>& chSalt, const SecureString& strKeyData, int count, unsigned char *key,unsigned char *iv) const
{
    // This mimics the behavior of openssl's EVP_BytesToKey with an aes256cbc
    // cipher and sha512 message digest. Because sha512's output size (64b) is
    // greater than the aes256 block size (16b) + aes256 key size (32b),
    // there's no need to process more than once (D_0).

    if(!count || !key || !iv)
        return 0;

    unsigned char buf[CSHA512::OUTPUT_SIZE];
    CSHA512 di;

    di.Write((const unsigned char*)strKeyData.c_str(), strKeyData.size());
    if(chSalt.size())
        di.Write(&chSalt[0], chSalt.size());
    di.Finalize(buf);

    for(int i = 0; i != count - 1; i++)
        di.Reset().Write(buf, sizeof(buf)).Finalize(buf);

    memcpy(key, buf, WALLET_CRYPTO_KEY_SIZE);
    memcpy(iv, buf + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
    memory_cleanse(buf, sizeof(buf));
    return WALLET_CRYPTO_KEY_SIZE;
}

bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const std::vector<unsigned char>& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;
    if (nDerivationMethod == 0)
        i = BytesToKeySHA512AES(chSalt, strKeyData, nRounds, vchKey.data(), vchIV.data());

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE)
    {
        LogPrintf("CCrypter::SetKey Invalid key size [%d] expected [%d]", chNewKey.size(), WALLET_CRYPTO_KEY_SIZE);
        return false;
    }
    if (chNewIV.size() != WALLET_CRYPTO_IV_SIZE)
    {
        LogPrintf("CCrypter::SetKey Invalid IV size [%d] expected [%d]", chNewIV.size(), WALLET_CRYPTO_IV_SIZE);
        return false;
    }

    memcpy(vchKey.data(), chNewKey.data(), chNewKey.size());
    memcpy(vchIV.data(), chNewIV.data(), chNewIV.size());

    fKeySet = true;
    return true;
}

//fixme: See https://github.com/weidai11/cryptopp/issues/953
//Ideally this should be resolved in cryptopp library, when it is remove this class
template <typename T> class CryptoVectorSource : public CryptoPP::SourceTemplate<CryptoPP::StringStore>
{
public:
    CryptoVectorSource(BufferedTransformation *attachment = NULLPTR)
        : SourceTemplate<CryptoPP::StringStore>(attachment) {}
    CryptoVectorSource(const T &vec, bool pumpAll, BufferedTransformation *attachment = NULLPTR)
        : SourceTemplate<CryptoPP::StringStore>(attachment) {SourceInitialize(pumpAll, MakeParameters("InputBuffer", CryptoPP::ConstByteArrayParameter(vec)));}
};

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext) const
{
    if (!fKeySet)
        return false;

    //NB! Some of our calling code (wallet password change) relies on us clearing the cipher text for it (i.e. passes in a non cleared variable) so don't remove this.
    vchCiphertext.clear();

    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption enc;
    enc.SetKeyWithIV(vchKey.data(), vchKey.size(), vchIV.data());
    try
    {
        CryptoVectorSource s(vchPlaintext, true, new CryptoPP::StreamTransformationFilter(enc, new CryptoPP::VectorSink(vchCiphertext)));
        if (vchCiphertext.size() < vchPlaintext.size())
            return false;
    }
    catch(...)
    {
        return false;
    }

    return true;
}

bool CCrypter::Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext) const
{
    if (!fKeySet)
        return false;

    //Always clear for symmetry with Encrypt function; see comment in Encrypt function for why Encrypt behaves in this manner
    vchPlaintext.clear();

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption dec;
    dec.SetKeyWithIV(vchKey.data(), vchKey.size(), vchIV.data());
    try
    {
        {
            CryptoVectorSource s(vchCiphertext, true, new CryptoPP::StreamTransformationFilter(dec, new CryptoPP::StringSinkTemplate<CKeyingMaterial>(vchPlaintext)));
        }
        if (vchPlaintext.size() == 0)
            return false;
    }
    catch(...)
    {
        return false;
    }
    return true;
}

bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const std::vector<unsigned char>& nIV, std::vector<unsigned char> &vchCiphertext)
{
    assert(nIV.size() == WALLET_CRYPTO_IV_SIZE);
    CCrypter cKeyCrypter;
    if(!cKeyCrypter.SetKey(vMasterKey, nIV))
        return false;
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}

bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext)
{
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
    return EncryptSecret(vMasterKey, vchPlaintext, chIV, vchCiphertext);
}

bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const std::vector<unsigned char>& nIV, CKeyingMaterial& vchPlaintext)
{
    assert(nIV.size() == WALLET_CRYPTO_IV_SIZE);
    CCrypter cKeyCrypter;
    if(!cKeyCrypter.SetKey(vMasterKey, nIV))
        return false;
    return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const uint256& nIV, CKeyingMaterial& vchPlaintext)
{
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
    return DecryptSecret(vMasterKey, vchCiphertext, chIV, vchPlaintext);
}

static bool DecryptKey(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCryptedSecret, const CPubKey& vchPubKey, CKey& key)
{
    CKeyingMaterial vchSecret;
    if(!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != 32)
        return false;

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);
    if (fUseCrypto)
        return true;
    if (!mapKeys.empty())
        return false;
    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::Lock()
{
    if (!SetCrypted())
        return false;

    {
        LOCK(cs_KeyStore);
        vMasterKey.clear();
    }

    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn, bool& needsWriteToDisk)
{
    needsWriteToDisk = false;
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = false;
        bool keyFail = false;
        bool KeyNone = true;
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        for (; mi != mapCryptedKeys.end(); ++mi)
        {
            KeyNone = false;
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        if (keyPass && keyFail)
        {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but not all.\n");
            assert(false);
        }
        if (!KeyNone && (keyFail || !keyPass))
            return false;
        vMasterKey = vMasterKeyIn;
        fDecryptionThoroughlyChecked = true;
    }
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::AddKeyPubKey(key, pubkey);

        if (IsLocked())
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        CKeyingMaterial vchSecret(key.begin(), key.end());
        if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret))
            return false;

        if (!AddCryptedKey(pubkey, vchCryptedSecret))
            return false;
    }
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(int64_t HDKeyIndex, const CPubKey &pubkey)
{
    // For HD we don't encrypt anything here - as the public key we need access to anyway, and the index is not special info - we derive the private key when we need it.
    {
        LOCK(cs_KeyStore);
        return CBasicKeyStore::AddKeyPubKey(HDKeyIndex, pubkey);
    }
}


bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        mapCryptedKeys[vchPubKey.GetID()] = std::pair(vchPubKey, vchCryptedSecret);
    }
    return true;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, std::vector<unsigned char>& encryptedKeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return false;

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            encryptedKeyOut = (*mi).second.second;
            return true;
        }
    }
    return false;
}

bool CCryptoKeyStore::GetKeyIDWithHighestIndex(CKeyID &address) const
{
   // For HD we don't encrypt anything here - as the public key we need access to anyway, and the index is not special info - we derive the private key when we need it.
    {
        LOCK(cs_KeyStore);
        return CBasicKeyStore::GetKeyIDWithHighestIndex(address);
    }
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetKey(address, keyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
        }
    }
    return false;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, int64_t& HDKeyIndex) const
{
    // For HD we don't encrypt anything here - as the public key we need access to anyway, and the index is not special info - we derive the private key when we need it.
    {
        LOCK(cs_KeyStore);
        return CBasicKeyStore::GetKey(address, HDKeyIndex);
    }
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            vchPubKeyOut = (*mi).second.first;
            return true;
        }
        // Check for watch-only pubkeys
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
    }
    return false;
}

bool CCryptoKeyStore::EncryptKeys(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!mapCryptedKeys.empty() || IsCrypted())
            return false;

        fUseCrypto = true;
        for(KeyMap::value_type& mKey : mapKeys)
        {
            const CKey &key = mKey.second;
            CPubKey vchPubKey = key.GetPubKey();
            CKeyingMaterial vchSecret(key.begin(), key.end());
            std::vector<unsigned char> vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(), vchCryptedSecret))
                return false;
            if (!AddCryptedKey(vchPubKey, vchCryptedSecret))
                return false;
        }
        mapKeys.clear();
    }
    return true;
}
