// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

/**
 * Why base-58 instead of standard base-64 encoding?
 * - Don't want 0OIl characters that look the same in some fonts and
 *      could be used to create visually identical looking data.
 * - A string with non-alphanumeric characters is not as easily accepted as input.
 * - E-mail usually won't line-break if there's no punctuation to break at.
 * - Double-clicking selects the whole string as one word if it's all alphanumeric.
 */
#ifndef BASE58_H
#define BASE58_H

#include "chainparams.h"
#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "support/allocators/zeroafterfree.h"
#include "appname.h"

#include <string>
#include <vector>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp> // for starts_with() and ends_with()

/**
 * Encode a byte sequence as a base58-encoded string.
 * pbegin and pend cannot be NULL, unless both are.
 */
std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend);

/**
 * Encode a byte vector as a base58-encoded string
 */
std::string EncodeBase58(const std::vector<unsigned char>& vch);

/**
 * Decode a base58-encoded string (psz) into a byte vector (vchRet).
 * return true if decoding is successful.
 * psz cannot be NULL.
 */
bool DecodeBase58(const char* psz, std::vector<unsigned char>& vchRet);

/**
 * Decode a base58-encoded string (str) into a byte vector (vchRet).
 * return true if decoding is successful.
 */
bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet);

/**
 * Encode a byte vector into a base58-encoded string, including checksum
 */
std::string EncodeBase58Check(const std::vector<unsigned char>& vchIn);

/**
 * Decode a base58-encoded string (psz) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBase58Check(const char* psz, std::vector<unsigned char>& vchRet);

/**
 * Decode a base58-encoded string (str) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet);

/**
 * Base class for all base58-encoded data
 */
class CBase58Data
{
protected:
    //! the version byte(s)
    std::vector<unsigned char> vchVersion;

    //! the actually encoded data
    typedef std::vector<unsigned char, zero_after_free_allocator<unsigned char> > vector_uchar;
    vector_uchar vchData;

    CBase58Data();
    void SetData(const std::vector<unsigned char> &vchVersionIn, const void* pdata, size_t nSize);
    void SetData(const std::vector<unsigned char> &vchVersionIn, const unsigned char *pbegin, const unsigned char *pend);

public:
    bool SetString(const char* psz, unsigned int nVersionBytes = 1);
    bool SetString(const std::string& str);
    std::string ToString() const;
    int CompareTo(const CBase58Data& b58) const;

    bool operator==(const CBase58Data& b58) const { return CompareTo(b58) == 0; }
    bool operator<=(const CBase58Data& b58) const { return CompareTo(b58) <= 0; }
    bool operator>=(const CBase58Data& b58) const { return CompareTo(b58) >= 0; }
    bool operator< (const CBase58Data& b58) const { return CompareTo(b58) <  0; }
    bool operator> (const CBase58Data& b58) const { return CompareTo(b58) >  0; }
};

/** base58-encoded addresses.
 * Public-key-hash-addresses have version 0 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 5 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
class CNativeAddress : public CBase58Data {
public:
    bool Set(const CKeyID& spendingKeyID, const CKeyID& witnessKeyID);
    bool Set(const CKeyID &id);
    bool Set(const CScriptID &id);
    bool Set(const CTxDestination &dest);

    //! Returns whether the address represents a valid address (this includes witness addresses as well)
    bool IsValid() const;
    bool IsValid(const CChainParams &params) const;

    //! Returns whether the address represents a valid witness address as opposed to just a valid address.
    bool IsValidWitness() const;
    bool IsValidWitness(const CChainParams& params) const;

    //! Returns whether the address represents a valid Bitcoin address, which can be used by third party payment integrations
    bool IsValidBitcoin() const;

    CNativeAddress() {}
    CNativeAddress(const CTxDestination &dest) { Set(dest); }
    CNativeAddress(const std::string& strAddress) { SetString(strAddress); }
    CNativeAddress(const char* pszAddress) { SetString(pszAddress); }

    CTxDestination Get() const;

    //! Returns the keyID associated with the address 
    //! In the case of a witness address this is the 'witness key ID'
    //! If 'pSecondaryKeyID' is passed in then this will be set to the 'spending key ID'
    bool GetKeyID(CKeyID& keyID, CKeyID* pSecondaryKeyID=nullptr) const;

    bool IsScript() const;

    bool operator==(const CNativeAddress& otherAddress) const { return CBase58Data::CompareTo((CBase58Data)otherAddress) == 0; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITECOMPACTSIZEVECTOR(vchVersion);
        READWRITECOMPACTSIZEVECTOR(vchData);
    }
};

/**
 * A base58-encoded secret key
 */
class CEncodedSecretKey : public CBase58Data
{
public:
    void SetKey(const CKey& vchSecret);
    CKey GetKey();
    bool IsValid() const;
    bool SetString(const char* pszSecret);
    bool SetString(const std::string& strSecret);

    CEncodedSecretKey(const CKey& vchSecret) { SetKey(vchSecret); }
    CEncodedSecretKey() {}
};

/**
 * A combination base58 and hex encoded secret extended key
 */
template <typename KeyType> class CEncodedSecretKeyExt 
{
public:
    void SetKey(const KeyType& vchSecret) { key = vchSecret; }
    KeyType GetKeyFromString()
    {
        //fixme: (Bitcoin) Strip creationTime and payAccount
        KeyType retExt;

        SecureString secretKey = SecureString(secret.begin(), secret.begin() + secret.find('-'));
        SecureString secretCode = SecureString(secret.begin() + secret.find('-') + 1, secret.end());

        std::vector<unsigned char> vchSecretKey;
        std::vector<unsigned char> vchSecretCode;
        DecodeBase58(secretKey.c_str(), vchSecretKey);
        DecodeBase58(secretCode.c_str(), vchSecretCode);

        if (vchSecretCode.size() == 32)
        {
            retExt.GetMutableKey().Set(vchSecretKey.begin(), vchSecretKey.end());
            retExt.chaincode = uint256(vchSecretCode);
        }
        else
        {
            //fixme: (PHASE5) Better error handling here - though this should never happen.
            assert(0);
        }

        return retExt;
    }

    bool SetString(const char* pszSecret)
    {
        secret = pszSecret;
        return true;
    }


    bool SetString(const std::string& strSecret)
    {
        secret = strSecret;
        return true;
    }

    CEncodedSecretKeyExt<KeyType>& SetCreationTime(std::string newCreationTime)
    {
        creationTime = newCreationTime;
        return *this;
    }

    int64_t getCreationTime() const
    {
        if (creationTime.empty())
        {
            return -1;
        }
        else
        {
            return atoi64(creationTime);
        }
    }

    CEncodedSecretKeyExt<KeyType>& SetPayAccount(std::string newPayAccount)
    {
        payAccount = newPayAccount;
        return *this;
    }

    std::string getPayAccount()
    {
        return payAccount;
    }

    bool fromURIString(std::string uri)
    {
        std::string syncPrefix = GLOBAL_APP_URIPREFIX"sync:";
        if (!boost::starts_with(uri, syncPrefix))
            return false;

        uri = std::string(uri.begin()+syncPrefix.length(),uri.end());
        std::vector<unsigned char> vchSecretKey;
        std::vector<unsigned char> vchSecretCode;
        std::vector<unsigned char> vchCreationTime;

        std::vector<std::string> vStrInputParts;
        boost::split(vStrInputParts, uri, boost::is_any_of("-"));
        if (vStrInputParts.size() != 2)
            return false;
        if (!DecodeBase58(vStrInputParts[0].c_str(), vchSecretKey))
            return false;

        boost::split(vStrInputParts, vStrInputParts[1], boost::is_any_of(":"));
        if (vStrInputParts.size() != 2)
            return false;
        if (!DecodeBase58(vStrInputParts[0].c_str(), vchSecretCode))
            return false;

        boost::split(vStrInputParts, vStrInputParts[1], boost::is_any_of(";"));
        if (vStrInputParts.size() != 2)
            return false;
        if (!DecodeBase58(vStrInputParts[0].c_str(), vchCreationTime))
            return false;

        creationTime = std::string(vchCreationTime.begin(), vchCreationTime.end());
        payAccount = vStrInputParts[1];
        if (vchSecretCode.size() != 32)
            return false;

        key.GetMutableKey().Set(vchSecretKey.begin(), vchSecretKey.end(), true);
        key.chaincode = uint256(vchSecretCode);

        return true;
    }

    std::string ToURIString() const
    {
        std::string encodedURI = ToString();
        encodedURI = encodedURI + ":" + EncodeBase58( (const unsigned char*)&creationTime[0], (const unsigned char*)&creationTime[0]+creationTime.size() );
        encodedURI = encodedURI + ";" + payAccount;

        return encodedURI;
    }

    std::string ToString() const
    {
        std::string encodedString =  EncodeBase58( (const unsigned char*)key.GetKey().begin(), (const unsigned char*)key.GetKey().end() );
        encodedString = encodedString + "-" + EncodeBase58( key.chaincode.begin(), key.chaincode.end() );

        return encodedString;
    }

    KeyType getKeyRaw() { return key; }
    CEncodedSecretKeyExt(const KeyType& vchSecret) { SetKey(vchSecret); }
    CEncodedSecretKeyExt(const std::string& strSecret) { SetString(strSecret); }
    CEncodedSecretKeyExt() {}

private:
    KeyType key;
    std::string secret;

    std::string payAccount;
    std::string creationTime;
};

template<typename K, int Size, CChainParams::Base58Type Type> class CEncodedSecretExtKeyBase : public CBase58Data
{
public:
    void SetKey(const K &key) {
        unsigned char vch[Size];
        key.Encode(vch);
        SetData(Params().Base58Prefix(Type), vch, vch+Size);
    }

    K GetKey() {
        K ret;
        if (vchData.size() == Size) {
            // If base58 encoded data does not hold an ext key, return a !IsValid() key
            ret.Decode(&vchData[0]);
        }
        return ret;
    }

    CEncodedSecretExtKeyBase(const K &key) {
        SetKey(key);
    }

    CEncodedSecretExtKeyBase(const std::string& strBase58c) {
        SetString(strBase58c.c_str(), Params().Base58Prefix(Type).size());
    }

    CEncodedSecretExtKeyBase() {}
};

typedef CEncodedSecretExtKeyBase<CExtKey, BIP32_EXTKEY_SIZE, CChainParams::EXT_SECRET_KEY> CEncodedSecretExt;
typedef CEncodedSecretExtKeyBase<CExtPubKey, BIP32_EXTKEY_SIZE, CChainParams::EXT_PUBLIC_KEY> CEncodedSecretExtPubKey;

#endif
