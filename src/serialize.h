// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#ifndef CORE_SERIALIZE_H
#define CORE_SERIALIZE_H

#include "compat/endian.h"

#include <algorithm>
#include <assert.h>
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdint.h>
#include <string>
#include <string.h>
#include <utility>
#include <vector>
#include <type_traits>

#include <prevector.h>
#include "support/allocators/secure.h"
#include <span.h>

/**
 * The maximum size of a serialized object in bytes or number of elements
 * (for eg vectors) when the size is encoded as CompactSize.
 */
static constexpr uint64_t MAX_SIZE = 0x02000000;

/** Maximum amount of memory (in bytes) to allocate at once when deserializing vectors. */
static const unsigned int MAX_VECTOR_ALLOCATE = 5000000;

/**
 * Dummy data type to identify deserializing constructors.
 *
 * By convention, a constructor of a type T with signature
 *
 *   template <typename Stream> T::T(deserialize_type, Stream& s)
 *
 * is a deserializing constructor, which builds the type by
 * deserializing it from s. If T contains const fields, this
 * is likely the only way to do so.
 */
struct deserialize_type {};
constexpr deserialize_type deserialize {};

/**
 * Used to bypass the rule against non-const reference to temporary
 * where it makes sense with wrappers such as CFlatData or CTxDB
 */
template<typename T>
inline T& REF(const T& val)
{
    return const_cast<T&>(val);
}

/**
 * Used to acquire a non-const pointer "this" to generate bodies
 * of const serialization operations from a template
 */
template<typename T>
inline T* NCONST_PTR(const T* val)
{
    return const_cast<T*>(val);
}

/*
 * Lowest-level serialization and conversion.
 */
template<typename Stream> inline void ser_writedata8(Stream &s, uint8_t obj)
{
    s.write(AsBytes(Span{&obj, 1}));
}
template<typename Stream> inline void ser_writedata16(Stream &s, uint16_t obj)
{
    obj = htole16(obj);
    s.write(AsBytes(Span{&obj, 1}));
}
template<typename Stream> inline void ser_writedata16be(Stream &s, uint16_t obj)
{
    obj = htobe16(obj);
    s.write(AsBytes(Span{&obj, 1}));
}
template<typename Stream> inline void ser_writedata32(Stream &s, uint32_t obj)
{
    obj = htole32(obj);
    s.write(AsBytes(Span{&obj, 1}));
}
template<typename Stream> inline void ser_writedata32be(Stream &s, uint32_t obj)
{
    obj = htobe32(obj);
    s.write(AsBytes(Span{&obj, 1}));
}
template<typename Stream> inline void ser_writedata64(Stream &s, uint64_t obj)
{
    obj = htole64(obj);
    s.write(AsBytes(Span{&obj, 1}));
}
template<typename Stream> inline uint8_t ser_readdata8(Stream &s)
{
    uint8_t obj;
    s.read(AsWritableBytes(Span{&obj, 1}));
    return obj;
}
template<typename Stream> inline uint16_t ser_readdata16(Stream &s)
{
    uint16_t obj;
    s.read(AsWritableBytes(Span{&obj, 1}));
    return le16toh(obj);
}
template<typename Stream> inline uint16_t ser_readdata16be(Stream &s)
{
    uint16_t obj;
    s.read(AsWritableBytes(Span{&obj, 1}));
    return be16toh(obj);
}
template<typename Stream> inline uint32_t ser_readdata32(Stream &s)
{
    uint32_t obj;
    s.read(AsWritableBytes(Span{&obj, 1}));
    return le32toh(obj);
}
template<typename Stream> inline uint32_t ser_readdata32be(Stream &s)
{
    uint32_t obj;
    s.read(AsWritableBytes(Span{&obj, 1}));
    return be32toh(obj);
}
template<typename Stream> inline uint64_t ser_readdata64(Stream &s)
{
    uint64_t obj;
    s.read(AsWritableBytes(Span{&obj, 1}));
    return le64toh(obj);
}
inline uint64_t ser_double_to_uint64(double x)
{
    union { double x; uint64_t y; } tmp;
    tmp.x = x;
    return tmp.y;
}
inline uint32_t ser_float_to_uint32(float x)
{
    union { float x; uint32_t y; } tmp;
    tmp.x = x;
    return tmp.y;
}
inline double ser_uint64_to_double(uint64_t y)
{
    union { double x; uint64_t y; } tmp;
    tmp.y = y;
    return tmp.x;
}
inline float ser_uint32_to_float(uint32_t y)
{
    union { float x; uint32_t y; } tmp;
    tmp.y = y;
    return tmp.x;
}


/////////////////////////////////////////////////////////////////
//
// Templates for serializing to anything that looks like a stream,
// i.e. anything that supports .read(Span<std::byte>) and .write(Span<const std::byte>)
//

class CSizeComputer;

enum
{
    // primary actions
    SER_NETWORK         = (1 << 0),
    SER_DISK            = (1 << 1),
    SER_GETHASH         = (1 << 2),
};

#define READWRITE(obj)      (::SerReadWrite(s, (obj), ser_action))
#define READWRITEMANY(...)  (::SerReadWriteMany(s, ser_action, __VA_ARGS__))
#define STRWRITE(obj)       (::SerWrite(s, (obj), ser_action))
#define STRREAD(obj)        (::SerRead(s, (obj), ser_action))

#define STRPEEK(obj)       (::SerPeek(s, (obj), ser_action))
#define STRSKIP(obj)       (::SerSkip(s, (obj), ser_action))


/** 
 * Implement three methods for serializable objects. These are actually wrappers over
 * "SerializationOp" template, which implements the body of each class' serialization
 * code. Adding "ADD_SERIALIZE_METHODS" in the body of the class causes these wrappers to be
 * added as members. 
 */
#define ADD_SERIALIZE_METHODS                                         \
    template<typename Stream>                                         \
    void Serialize(Stream& s) const {                                 \
        NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize());  \
    }                                                                 \
    template<typename Stream>                                         \
    void Unserialize(Stream& s) {                                     \
        SerializationOp(s, CSerActionUnserialize());                  \
    }

template<typename Stream> inline void Serialize(Stream& s, char a    ) { ser_writedata8(s, a); } // TODO Get rid of bare char
template<typename Stream> inline void Serialize(Stream& s, int8_t a  ) { ser_writedata8(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint8_t a ) { ser_writedata8(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int16_t a ) { ser_writedata16(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint16_t a) { ser_writedata16(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int32_t a ) { ser_writedata32(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint32_t a) { ser_writedata32(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int64_t a ) { ser_writedata64(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint64_t a) { ser_writedata64(s, a); }
template<typename Stream> inline void Serialize(Stream& s, float a   ) { ser_writedata32(s, ser_float_to_uint32(a)); }
template<typename Stream> inline void Serialize(Stream& s, double a  ) { ser_writedata64(s, ser_double_to_uint64(a)); }

template<typename Stream> inline void Unserialize(Stream& s, char& a    ) { a = ser_readdata8(s); } // TODO Get rid of bare char
template<typename Stream> inline void Unserialize(Stream& s, int8_t& a  ) { a = ser_readdata8(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint8_t& a ) { a = ser_readdata8(s); }
template<typename Stream> inline void Unserialize(Stream& s, int16_t& a ) { a = ser_readdata16(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint16_t& a) { a = ser_readdata16(s); }
template<typename Stream> inline void Unserialize(Stream& s, int32_t& a ) { a = ser_readdata32(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint32_t& a) { a = ser_readdata32(s); }
template<typename Stream> inline void Unserialize(Stream& s, int64_t& a ) { a = ser_readdata64(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint64_t& a) { a = ser_readdata64(s); }
template<typename Stream> inline void Unserialize(Stream& s, float& a   ) { a = ser_uint32_to_float(ser_readdata32(s)); }
template<typename Stream> inline void Unserialize(Stream& s, double& a  ) { a = ser_uint64_to_double(ser_readdata64(s)); }

template<typename Stream> inline void Serialize(Stream& s, bool a)    { char f=a; ser_writedata8(s, f); }
template<typename Stream> inline void Unserialize(Stream& s, bool& a) { char f=ser_readdata8(s); a=f; }






/**
 * Compact Size
 * size <  253        -- 1 byte
 * size <= USHRT_MAX  -- 3 bytes  (253 + 2 bytes)
 * size <= UINT_MAX   -- 5 bytes  (254 + 4 bytes)
 * size >  UINT_MAX   -- 9 bytes  (255 + 8 bytes)
 */
inline unsigned int GetSizeOfCompactSize(uint64_t nSize)
{
    if (nSize < 253)             return sizeof(unsigned char);
    else if (nSize <= std::numeric_limits<uint16_t>::max()) return sizeof(unsigned char) + sizeof(uint16_t);
    else if (nSize <= std::numeric_limits<unsigned int>::max())  return sizeof(unsigned char) + sizeof(unsigned int);
    else                         return sizeof(unsigned char) + sizeof(uint64_t);
}

inline void WriteCompactSize(CSizeComputer& os, uint64_t nSize);

template<typename Stream>
void WriteCompactSize(Stream& os, uint64_t nSize)
{
    if (nSize < 253)
    {
        ser_writedata8(os, nSize);
    }
    else if (nSize <= std::numeric_limits<uint16_t>::max())
    {
        ser_writedata8(os, 253);
        ser_writedata16(os, nSize);
    }
    else if (nSize <= std::numeric_limits<unsigned int>::max())
    {
        ser_writedata8(os, 254);
        ser_writedata32(os, nSize);
    }
    else
    {
        ser_writedata8(os, 255);
        ser_writedata64(os, nSize);
    }
    return;
}

/**
 * Decode a CompactSize-encoded variable-length integer.
 *
 * As these are primarily used to encode the size of vector-like serializations, by default a range
 * check is performed. When used as a generic number encoding, range_check should be set to false.
 */
template<typename Stream>
uint64_t ReadCompactSize(Stream& is, bool range_check = true)
{
    uint8_t chSize = ser_readdata8(is);
    uint64_t nSizeRet = 0;
    if (chSize < 253)
    {
        nSizeRet = chSize;
    }
    else if (chSize == 253)
    {
        nSizeRet = ser_readdata16(is);
        if (nSizeRet < 253)
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
    }
    else if (chSize == 254)
    {
        nSizeRet = ser_readdata32(is);
        if (nSizeRet < 0x10000u)
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
    }
    else
    {
        nSizeRet = ser_readdata64(is);
        if (nSizeRet < 0x100000000ULL)
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
    }
    if (range_check && nSizeRet > MAX_SIZE) {
        throw std::ios_base::failure("ReadCompactSize(): size too large");
    }
    return nSizeRet;
}

/**
 * Variable-length integers: bytes are a MSB base-128 encoding of the number.
 * The high bit in each byte signifies whether another digit follows. To make
 * sure the encoding is one-to-one, one is subtracted from all but the last digit.
 * Thus, the byte sequence a[] with length len, where all but the last byte
 * has bit 128 set, encodes the number:
 *
 *  (a[len-1] & 0x7F) + sum(i=1..len-1, 128^i*((a[len-i-1] & 0x7F)+1))
 *
 * Properties:
 * * Very small (0-127: 1 byte, 128-16511: 2 bytes, 16512-2113663: 3 bytes)
 * * Every integer has exactly one encoding
 * * Encoding does not depend on size of original integer type
 * * No redundancy: every (infinite) byte sequence corresponds to a list
 *   of encoded integers.
 *
 * 0:         [0x00]  256:        [0x81 0x00]
 * 1:         [0x01]  16383:      [0xFE 0x7F]
 * 127:       [0x7F]  16384:      [0xFF 0x00]
 * 128:  [0x80 0x00]  16511:      [0xFF 0x7F]
 * 255:  [0x80 0x7F]  65535: [0x82 0xFE 0x7F]
 * 2^32:           [0x8E 0xFE 0xFE 0xFF 0x00]
 */

template<typename I>
inline unsigned int GetSizeOfVarInt(I n)
{
    int nRet = 0;
    while(true) {
        nRet++;
        if (n <= 0x7F)
            break;
        n = (n >> 7) - 1;
    }
    return nRet;
}

template<typename I>
inline void WriteVarInt(CSizeComputer& os, I n);

template<typename Stream, typename I>
void WriteVarInt(Stream& os, I n)
{
    unsigned char tmp[(sizeof(n)*8+6)/7];
    int len=0;
    while(true) {
        tmp[len] = (n & 0x7F) | (len ? 0x80 : 0x00);
        if (n <= 0x7F)
            break;
        n = (n >> 7) - 1;
        len++;
    }
    do {
        ser_writedata8(os, tmp[len]);
    } while(len--);
}

template<typename Stream, typename I>
I ReadVarInt(Stream& is)
{
    I n = 0;
    while(true) {
        unsigned char chData = ser_readdata8(is);
        if (n > (std::numeric_limits<I>::max() >> 7)) {
           throw std::ios_base::failure("ReadVarInt(): size too large");
        }
        n = (n << 7) | (chData & 0x7F);
        if (chData & 0x80) {
            if (n == std::numeric_limits<I>::max()) {
                throw std::ios_base::failure("ReadVarInt(): size too large");
            }
            n++;
        } else {
            return n;
        }
    }
}

#define FLATDATA(obj) REF(CFlatData((char*)&(obj), (char*)&(obj) + sizeof(obj)))
#define VARINT(obj) REF(WrapVarInt(REF(obj)))
#define COMPACTSIZE(obj) REF(MakeCCompactSize(REF(obj)))
#define LIMITED_STRING(obj,n) REF(LimitedString< n >(REF(obj)))

/** 
 * Wrapper for serializing arrays and POD.
 */
class CFlatData
{
protected:
    char* pbegin;
    char* pend;
public:
    CFlatData(void* pbeginIn, void* pendIn) : pbegin((char*)pbeginIn), pend((char*)pendIn) { }
    template <class T, class TAl>
    explicit CFlatData(std::vector<T,TAl> &v)
    {
        pbegin = (char*)v.data();
        pend = (char*)(v.data() + v.size());
    }
    template <unsigned int N, typename T, typename S, typename D>
    explicit CFlatData(prevector<N, T, S, D> &v)
    {
        pbegin = (char*)v.data();
        pend = (char*)(v.data() + v.size());
    }
    char* begin() { return pbegin; }
    const char* begin() const { return pbegin; }
    char* end() { return pend; }
    const char* end() const { return pend; }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s.write(AsBytes(Span(pbegin, pend)));
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        s.read(AsWritableBytes(Span(pbegin, pend)));
    }
};

template<typename I>
class CVarInt
{
protected:
    I &n;
public:
    CVarInt(I& nIn) : n(nIn) { }

    template<typename Stream>
    void Serialize(Stream &s) const {
        WriteVarInt<Stream,I>(s, n);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        n = ReadVarInt<Stream,I>(s);
    }
};

template<typename T> class CCompactSize
{
protected:
    T &n;
public:
    CCompactSize(T& nIn) : n(nIn) { }

    template<typename Stream>
    void Serialize(Stream &s) const {
        WriteCompactSize<Stream>(s, n);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        n = ReadCompactSize<Stream>(s);
    }
};

template <class T> CCompactSize<T> MakeCCompactSize(T& t)
{
    return CCompactSize<T>(t);
}

template<size_t Limit>
class LimitedString
{
protected:
    std::string& string;
public:
    LimitedString(std::string& _string) : string(_string) {}

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        size_t size = ReadCompactSize(s);
        if (size > Limit) {
            throw std::ios_base::failure("String length limit exceeded");
        }
        string.resize(size);
        if (size != 0)
            s.read(MakeWritableByteSpan(string));
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        WriteCompactSize(s, string.size());
        if (!string.empty())
            s.write((char*)&string[0], string.size());
    }
};

template<typename I>
CVarInt<I> WrapVarInt(I& n) { return CVarInt<I>(n); }

/**
 * Forward declarations
 */

/**
 *  string
 */
template<typename Stream, typename C> void Serialize(Stream& os, const std::basic_string<C>& str);
template<typename Stream, typename C> void Unserialize(Stream& is, std::basic_string<C>& str);
template<typename C, typename T, typename A> unsigned int GetSerializeSize(const std::basic_string<C, T, A>& str);
template<typename Stream, typename C, typename T, typename A> void Serialize(Stream& os, const std::basic_string<C, T, A>& str);
template<typename Stream, typename C, typename T, typename A> void Unserialize(Stream& is, std::basic_string<C, T, A>& str);

/**
 * prevector
 * prevectors of unsigned char are a special case and are intended to be serialized as a single opaque blob.
 */
template<typename Stream, unsigned int N, typename T> void Serialize_impl(Stream& os, const prevector<N, T>& v, const unsigned char&);
template<typename Stream, unsigned int N, typename T, typename V> void Serialize_impl(Stream& os, const prevector<N, T>& v, const V&);
template<typename Stream, unsigned int N, typename T> inline void Serialize(Stream& os, const prevector<N, T>& v);
template<typename Stream, unsigned int N, typename T> void Unserialize_impl(Stream& is, prevector<N, T>& v, const unsigned char&);
template<typename Stream, unsigned int N, typename T, typename V> void Unserialize_impl(Stream& is, prevector<N, T>& v, const V&);
template<typename Stream, unsigned int N, typename T> inline void Unserialize(Stream& is, prevector<N, T>& v);

/**
 * vectoroverwritewrapper
 * used by compactsizevectorwrapper, varintvectorwrapper, nosizevectorwrapper to allow template specilisation of serilisation with as little overhead as possible.
 */
template <typename VectorType, typename WrapperType> class vectoroverwritewrapper
{
public:
    VectorType& vector;
    vectoroverwritewrapper(VectorType& vector_) : vector(vector_)
    {
    }
};

/**
 * compactsizevectorwrapper
 * same as normal vector but using compact size to represent the size when serializing
 */
struct compactsizevectorwrapper
{
    compactsizevectorwrapper() {};
};

template<typename T> vectoroverwritewrapper<T, compactsizevectorwrapper> MakeCompactSizeVector(T& obj)
{
    return vectoroverwritewrapper<T, compactsizevectorwrapper> (obj);
}
template<typename T> const vectoroverwritewrapper<const T, const compactsizevectorwrapper> MakeCompactSizeVector(const T& obj)
{
    return vectoroverwritewrapper<const T, const compactsizevectorwrapper> (obj);
}

#define COMPACTSIZEVECTOR(obj) REF(MakeCompactSizeVector(obj))
#define READWRITECOMPACTSIZEVECTOR(obj) (READWRITE(REF(COMPACTSIZEVECTOR(obj))))

/**
 * varintvectorwrapper
 * same as normal vector but using CVarInt to represent the size when serializing
 */
struct varintvectorwrapper
{
    varintvectorwrapper() {};
};

template<typename T> vectoroverwritewrapper<T, varintvectorwrapper> MakeVarIntVector(T& obj)
{
    return vectoroverwritewrapper<T, varintvectorwrapper> (obj);
}
template<typename T> const vectoroverwritewrapper<const T, const varintvectorwrapper> MakeVarIntVector(const T& obj)
{
    return vectoroverwritewrapper<const T, const varintvectorwrapper> (obj);
}

#define VARINTVECTOR(obj) REF(MakeVarIntVector(REF(obj)))
#define READWRITEVARINTVECTOR(obj) (READWRITE(REF(VARINTVECTOR(REF(obj)))))

/**
 * nosizevectorwrapper
 * same as normal vector but no size output when serializing, size must be obtained/set independently.
 */
struct nosizevectorwrapper
{
    nosizevectorwrapper() {};
};

template<typename T> vectoroverwritewrapper<T, nosizevectorwrapper> MakeNoSizeVector(T& obj)
{
    return vectoroverwritewrapper<T, nosizevectorwrapper> (obj);
}
template<typename T> const vectoroverwritewrapper<const T, const nosizevectorwrapper> MakeNoSizeVector(const T& obj)
{
    return vectoroverwritewrapper<const T, const nosizevectorwrapper> (obj);
}

#define NOSIZEVECTOR(obj) REF(MakeNoSizeVector(REF(obj)))
#define READWRITENOSIZEVECTOR(obj) (READWRITE(REF(NOSIZEVECTOR(REF(obj)))))

/**
 * pair
 */
template<typename Stream, typename K, typename T> void Serialize(Stream& os, const std::pair<K, T>& item);
template<typename Stream, typename K, typename T> void Unserialize(Stream& is, std::pair<K, T>& item);

/**
 * tuple
 */
template<typename T1, typename T2, typename T3> unsigned int GetSerializeSize(const std::tuple<T1, T2, T3>& item);
template<typename Stream, typename T1, typename T2, typename T3> void Serialize(Stream& os, const std::tuple<T1, T2, T3>& item);
template<typename Stream, typename T1, typename T2, typename T3> void Unserialize(Stream& is, std::tuple<T1, T2, T3>& item);

template<typename T1, typename T2, typename T3, typename T4> unsigned int GetSerializeSize(const std::tuple<T1, T2, T3, T4>& item);
template<typename Stream, typename T1, typename T2, typename T3, typename T4> void Serialize(Stream& os, const std::tuple<T1, T2, T3, T4>& item);
template<typename Stream, typename T1, typename T2, typename T3, typename T4> void Unserialize(Stream& is, std::tuple<T1, T2, T3, T4>& item);

/**
 * map
 */
template<typename Stream, typename K, typename T, typename Pred, typename A> void Serialize(Stream& os, const std::map<K, T, Pred, A>& m);
template<typename Stream, typename K, typename T, typename Pred, typename A> void Unserialize(Stream& is, std::map<K, T, Pred, A>& m);

/**
 * set
 */
template<typename Stream, typename K, typename Pred, typename A> void Serialize(Stream& os, const std::set<K, Pred, A>& m);
template<typename Stream, typename K, typename Pred, typename A> void Unserialize(Stream& is, std::set<K, Pred, A>& m);

/**
 * shared_ptr
 */
template<typename Stream, typename T> void Serialize(Stream& os, const std::shared_ptr<const T>& p);
template<typename Stream, typename T> void Unserialize(Stream& os, std::shared_ptr<const T>& p);

/**
 * unique_ptr
 */
template<typename Stream, typename T> void Serialize(Stream& os, const std::unique_ptr<const T>& p);
template<typename Stream, typename T> void Unserialize(Stream& os, std::unique_ptr<const T>& p);


template<typename Stream, typename T, typename std::enable_if<std::is_enum<T>::value,int>::type =0>
inline void Serialize(Stream& os, const T& a)
{
    Serialize(os, static_cast<typename std::underlying_type<T>::type>(a));
}

template<typename Stream, typename T, typename std::enable_if<std::is_enum<T>::value,int>::type =0>
inline void Unserialize(Stream& os, T& a)
{
    typename std::underlying_type<T>::type value;
    Unserialize(os, value);
    a = static_cast<T>(value);
}

/**
 * If none of the specialized versions above matched, default to calling member function.
 */
template<typename Stream, typename T, typename std::enable_if<!std::is_enum<T>::value,int>::type =0>
inline void Serialize(Stream& os, const T& a)
{
    a.Serialize(os);
}

template<typename Stream, typename T, typename std::enable_if<!std::is_enum<T>::value,int>::type =0>
inline void Unserialize(Stream& is, T& a)
{
    a.Unserialize(is);
}





/**
 * string
 */
template<typename Stream, typename C>
void Serialize(Stream& os, const std::basic_string<C>& str)
{
    WriteCompactSize(os, str.size());
    if (!str.empty())
        os.write(MakeByteSpan(str));
}

template<typename Stream, typename C>
void Unserialize(Stream& is, std::basic_string<C>& str)
{
    unsigned int nSize = ReadCompactSize(is);
    str.resize(nSize);
    if (nSize != 0)
        is.read(MakeWritableByteSpan(str));
}

template<typename C, typename T, typename A>
unsigned int GetSerializeSize(const std::basic_string<C, T, A>& str)
{
    return GetSizeOfCompactSize(str.size()) + str.size() * sizeof(str[0]);
}

template<typename Stream, typename C, typename T, typename A>
void Serialize(Stream& os, const std::basic_string<C, T, A>& str)
{
    WriteCompactSize(os, str.size());
    if (!str.empty())
        os.write((char*)&str[0], str.size() * sizeof(str[0]));
}

template<typename Stream, typename C, typename T, typename A>
void Unserialize(Stream& is, std::basic_string<C, T, A>& str)
{
    unsigned int nSize = ReadCompactSize(is);
    str.resize(nSize);
    if (nSize != 0)
        is.read((char*)&str[0], nSize * sizeof(str[0]));
}





/**
 * prevector
 */
template<typename Stream, unsigned int N, typename T>
void Serialize_impl(Stream& os, const prevector<N, T>& v, const unsigned char&)
{
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(MakeByteSpan(v));
}

template<typename Stream, unsigned int N, typename T, typename V>
void Serialize_impl(Stream& os, const prevector<N, T>& v, const V&)
{
    WriteCompactSize(os, v.size());
    for (typename prevector<N, T>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, unsigned int N, typename T>
inline void Serialize(Stream& os, const prevector<N, T>& v)
{
    Serialize_impl(os, v, T());
}


template<typename Stream, unsigned int N, typename T>
void Unserialize_impl(Stream& is, prevector<N, T>& v, const unsigned char&)
{
    // Limit size per read so bogus size value won't cause out of memory
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(T)));
        v.resize_uninitialized(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}

template<typename Stream, unsigned int N, typename T, typename V>
void Unserialize_impl(Stream& is, prevector<N, T>& v, const V&)
{
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    unsigned int nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(T);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, v[i]);
    }
}

template<typename Stream, unsigned int N, typename T>
inline void Unserialize(Stream& is, prevector<N, T>& v)
{
    Unserialize_impl(is, v, T());
}



/**
 * vector
 */
template<typename Stream, typename K>
void Serialize(Stream& os, const vectoroverwritewrapper<const std::vector<K>, const compactsizevectorwrapper>& item)
{
    const std::vector<K>& v = item.vector;
    WriteCompactSize(os, v.size());
    for (typename std::vector<K>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, typename K>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<K>, compactsizevectorwrapper>& item)
{
    const std::vector<K>& v = item.vector;
    WriteCompactSize(os, v.size());
    for (typename std::vector<K>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, typename K>
void Serialize(Stream& os, const vectoroverwritewrapper<const std::vector<std::vector<K>>, const compactsizevectorwrapper>& item)
{
    const std::vector<std::vector<K>>& v = item.vector;
    WriteCompactSize(os, v.size());
    for (typename std::vector<std::vector<K>>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, COMPACTSIZEVECTOR(*vi));
}

template<typename Stream>
void Serialize(Stream& os, const vectoroverwritewrapper<const std::vector<unsigned char>, const compactsizevectorwrapper>& item)
{
    const std::vector<unsigned char>& v = item.vector;
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(AsBytes(MakeByteSpan(v)));
}

template<typename Stream>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<unsigned char>, const compactsizevectorwrapper>& item)
{
    const std::vector<unsigned char>& v = item.vector;
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(MakeByteSpan(v));
}

template<typename Stream>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<unsigned char>, compactsizevectorwrapper>& item)
{
    const std::vector<unsigned char>& v = item.vector;
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(MakeByteSpan(v));
}




template<typename Stream, typename A>
void Serialize(Stream& os, const vectoroverwritewrapper<const std::vector<unsigned char, A>, const compactsizevectorwrapper>& item)
{
    const std::vector<unsigned char, A>& v = item.vector;
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(MakeByteSpan(v));
}

template<typename Stream, typename A>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<unsigned char, A>, const compactsizevectorwrapper>& item)
{
    const std::vector<unsigned char, A>& v = item.vector;
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(MakeByteSpan(v));
}

template<typename Stream, typename A>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<unsigned char, A>, compactsizevectorwrapper>& item)
{
    const std::vector<unsigned char, A>& v = item.vector;
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write(MakeByteSpan(v));
}




template<typename Stream, typename K>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<K>, compactsizevectorwrapper>& item)
{
    std::vector<K>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    unsigned int nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(K);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, v[i]);
    }
}

template<typename Stream, typename K>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<std::vector<K>>, compactsizevectorwrapper>& item)
{
    std::vector<std::vector<K>>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    unsigned int nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(K);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, COMPACTSIZEVECTOR(v[i]));
    }
}

template<typename Stream>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<unsigned char>, compactsizevectorwrapper>& item)
{
    std::vector<unsigned char>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(unsigned char)));
        v.resize(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}

template<typename Stream, typename A>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<unsigned char, A>, compactsizevectorwrapper>& item)
{
    std::vector<unsigned char, A>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(unsigned char)));
        v.resize(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}

template<typename Stream>
void Unserialize(Stream& is, const vectoroverwritewrapper<std::vector<unsigned char>, compactsizevectorwrapper>& item)
{
    std::vector<unsigned char>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(unsigned char)));
        v.resize(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}

template<typename Stream, typename A>
void Unserialize(Stream& is, const vectoroverwritewrapper<std::vector<unsigned char, A>, compactsizevectorwrapper>& item)
{
    std::vector<unsigned char, A>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(unsigned char)));
        v.resize(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}

template<typename Stream, typename A>
void Unserialize(Stream& is, vectoroverwritewrapper<const std::vector<unsigned char, A>, compactsizevectorwrapper>& item)
{
    std::vector<unsigned char, A>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(unsigned char)));
        v.resize(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}

/**
 * varintvectorwrapper
 */
template<typename Stream, typename K>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<K>, varintvectorwrapper>& item)
{
    const std::vector<K>& v = item.vector;
    WriteVarInt(os, v.size());
    for (typename std::vector<K>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, typename K>
void Serialize(Stream& os, vectoroverwritewrapper<std::vector<K>, varintvectorwrapper>& item)
{
    std::vector<K>& v = item.vector;
    WriteVarInt(os, v.size());
    for (typename std::vector<K>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, typename K>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<std::vector<K>>, varintvectorwrapper>& item)
{
    const std::vector<std::vector<K>>& v = item.vector;
    WriteVarInt(os, v.size());
    for (typename std::vector<std::vector<K>>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, VARINTVECTOR(*vi));
}



template<typename Stream>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<unsigned char>, varintvectorwrapper>& item)
{
    const std::vector<unsigned char>& v = item.vector;
    WriteVarInt(os, v.size());
    if (!v.empty())
        os.write((char*)&v[0], v.size() * sizeof(unsigned char));
}

template<typename Stream>
void Serialize(Stream& os, vectoroverwritewrapper<std::vector<unsigned char>, varintvectorwrapper>& item)
{
    std::vector<unsigned char>& v = item.vector;
    WriteVarInt(os, v.size());
    if (!v.empty())
        os.write((char*)&v[0], v.size() * sizeof(unsigned char));
}

template<typename Stream, typename K>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<K>, varintvectorwrapper>& item)
{
    std::vector<K>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadVarInt<Stream, unsigned int>(is);
    unsigned int i = 0;
    unsigned int nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(K);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, v[i]);
    }
}

template<typename Stream, typename K>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<std::vector<K>>, varintvectorwrapper>& item)
{
    std::vector<std::vector<K>>& v = item.vector;
    v.clear();
    unsigned int nSize = ReadVarInt<Stream, unsigned int>(is);
    unsigned int i = 0;
    unsigned int nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(K);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, VARINTVECTOR(v[i]));
    }
}





/**
 * nosizevectorwrapper
 */
template<typename Stream, typename K>
void Serialize(Stream& os, const vectoroverwritewrapper<std::vector<K>, nosizevectorwrapper>& item)
{
    const std::vector<K>& v = item.vector;
    for (typename std::vector<K>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream>
void Serialize(Stream& os, const vectoroverwritewrapper<const std::vector<unsigned char>, nosizevectorwrapper>& item)
{
    const std::vector<unsigned char>& v = item.vector;
    if (!v.empty())
        os.write((char*)&v[0], v.size() * sizeof(unsigned char));
}

template<typename Stream, typename K>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<K>, nosizevectorwrapper>& item)
{
    std::vector<K>& v = item.vector;
    //v.clear();
    unsigned int nSize = v.size();
    unsigned int i = 0;
    unsigned int nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(K);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            ::Unserialize(is, v[i]);
    }
}

template<typename Stream>
void Unserialize(Stream& is, vectoroverwritewrapper<std::vector<unsigned char>, nosizevectorwrapper>& item)
{
    std::vector<unsigned char>& v = item.vector;
    //v.clear();
    unsigned int nSize = v.size();
    unsigned int i = 0;
    while (i < nSize)
    {
        unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(unsigned char)));
        v.resize(i + blk);
        is.read(AsWritableBytes(Span{&v[i], blk}));
        i += blk;
    }
}


/**
 * pair
 */
template<typename Stream, typename K, typename T>
void Serialize(Stream& os, const std::pair<K, T>& item)
{
    static_assert(!std::is_same<T, compactsizevectorwrapper>::value, "Must not execute on vector wrapper");
    static_assert(!std::is_same<T, nosizevectorwrapper>::value, "Must not execute on vector wrapper");
    static_assert(!std::is_same<T, varintvectorwrapper>::value, "Must not execute on vector wrapper");
    
    Serialize(os, item.first);
    Serialize(os, item.second);
}

template<typename Stream, typename K, typename T>
void Unserialize(Stream& is, std::pair<K, T>& item)
{
    Unserialize(is, item.first);
    Unserialize(is, item.second);
}



/**
 * tuple
 */
template<typename T1, typename T2, typename T3>
unsigned int GetSerializeSize(const std::tuple<T1, T2, T3>& item)
{
    return GetSerializeSize(std::get<0>(item)) + GetSerializeSize(std::get<1>(item)) + GetSerializeSize(std::get<2>(item));
}

template<typename Stream, typename T1, typename T2, typename T3>
void Serialize(Stream& os, const std::tuple<T1, T2, T3>& item)
{
    Serialize(os, std::get<0>(item));
    Serialize(os, std::get<1>(item));
    Serialize(os, std::get<2>(item));
}

template<typename Stream, typename T1, typename T2, typename T3>
void Unserialize(Stream& is, std::tuple<T1, T2, T3>& item)
{
    Unserialize(is, std::get<0>(item));
    Unserialize(is, std::get<1>(item));
    Unserialize(is, std::get<2>(item));
}

template<typename T1, typename T2, typename T3, typename T4>
unsigned int GetSerializeSize(const std::tuple<T1, T2, T3, T4>& item)
{
    return GetSerializeSize(std::get<0>(item)) + GetSerializeSize(std::get<1>(item)) + GetSerializeSize(std::get<2>(item)) + GetSerializeSize(std::get<3>(item));
}

template<typename Stream, typename T1, typename T2, typename T3, typename T4>
void Serialize(Stream& os, const std::tuple<T1, T2, T3, T4>& item)
{
    Serialize(os, std::get<0>(item));
    Serialize(os, std::get<1>(item));
    Serialize(os, std::get<2>(item));
    Serialize(os, std::get<3>(item));
}

template<typename Stream, typename T1, typename T2, typename T3, typename T4>
void Unserialize(Stream& is, std::tuple<T1, T2, T3, T4>& item)
{
    Unserialize(is, std::get<0>(item));
    Unserialize(is, std::get<1>(item));
    Unserialize(is, std::get<2>(item));
    Unserialize(is, std::get<3>(item));
}



/**
 * map
 */
template<typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize(Stream& os, const std::map<K, T, Pred, A>& m)
{
    WriteCompactSize(os, m.size());
    for (const auto& entry : m)
        Serialize(os, entry);
}

template<typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize(Stream& is, std::map<K, T, Pred, A>& m)
{
    m.clear();
    unsigned int nSize = ReadCompactSize(is);
    typename std::map<K, T, Pred, A>::iterator mi = m.begin();
    for (unsigned int i = 0; i < nSize; i++)
    {
        std::pair<K, T> item;
        Unserialize(is, item);
        mi = m.insert(mi, item);
    }
}



/**
 * set
 */
template<typename Stream, typename K, typename Pred, typename A>
void Serialize(Stream& os, const std::set<K, Pred, A>& m)
{
    WriteCompactSize(os, m.size());
    for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it)
        Serialize(os, (*it));
}

template<typename Stream, typename K, typename Pred, typename A>
void Unserialize(Stream& is, std::set<K, Pred, A>& m)
{
    m.clear();
    unsigned int nSize = ReadCompactSize(is);
    typename std::set<K, Pred, A>::iterator it = m.begin();
    for (unsigned int i = 0; i < nSize; i++)
    {
        K key;
        Unserialize(is, key);
        it = m.insert(it, key);
    }
}



/**
 * unique_ptr
 */
template<typename Stream, typename T> void
Serialize(Stream& os, const std::unique_ptr<const T>& p)
{
    Serialize(os, *p);
}

template<typename Stream, typename T>
void Unserialize(Stream& is, std::unique_ptr<const T>& p)
{
    p.reset(new T(deserialize, is));
}



/**
 * shared_ptr
 */
template<typename Stream, typename T> void
Serialize(Stream& os, const std::shared_ptr<const T>& p)
{
    Serialize(os, *p);
}

template<typename Stream, typename T>
void Unserialize(Stream& is, std::shared_ptr<const T>& p)
{
    p = std::make_shared<const T>(deserialize, is);
}



/**
 * Support for ADD_SERIALIZE_METHODS and READWRITE macro
 */
struct CSerActionSerialize
{
    constexpr bool ForRead() const { return false; }
};
struct CSerActionUnserialize
{
    constexpr bool ForRead() const { return true; }
};

template<typename Stream, typename T>
inline void SerReadWrite(Stream& s, const T& obj, [[maybe_unused]] CSerActionSerialize ser_action)
{
    ::Serialize(s, obj);
}

template<typename Stream, typename T>
inline void SerReadWrite(Stream& s, T& obj, [[maybe_unused]] CSerActionUnserialize ser_action)
{
    ::Unserialize(s, obj);
}

template<typename Stream, typename T>
inline void SerWrite(Stream& s, const T& obj, [[maybe_unused]] CSerActionSerialize ser_action)
{
    ::Serialize(s, obj);
}

template<typename Stream, typename T>
inline void SerWrite([[maybe_unused]] Stream& s, [[maybe_unused]] const T& obj, [[maybe_unused]] CSerActionUnserialize ser_action)
{
}

template<typename Stream, typename T>
inline void SerRead([[maybe_unused]] Stream& s, [[maybe_unused]] const T& obj, [[maybe_unused]] CSerActionSerialize ser_action)
{
}

template<typename Stream, typename T>
inline void SerRead(Stream& s, T& obj, [[maybe_unused]] CSerActionUnserialize ser_action)
{
    ::Unserialize(s, obj);
}

template<typename Stream, typename T>
inline void SerPeek(Stream& s, T& obj, [[maybe_unused]] CSerActionSerialize ser_action)
{
}

template<typename Stream, typename T>
inline void SerPeek(Stream& s, T& obj, [[maybe_unused]] CSerActionUnserialize ser_action)
{
    s.peek((char*)&obj, sizeof(obj));
}

template<typename Stream, typename T>
inline void SerSkip(Stream& s, const T& obj, [[maybe_unused]] CSerActionSerialize ser_action)
{
}

template<typename Stream, typename T>
inline void SerSkip(Stream& s, T& obj, CSerActionUnserialize ser_action)
{
    s.ignore(sizeof(obj));
}





/* ::GetSerializeSize implementations
 *
 * Computing the serialized size of objects is done through a special stream
 * object of type CSizeComputer, which only records the number of bytes written
 * to it.
 *
 * If your Serialize or SerializationOp method has non-trivial overhead for
 * serialization, it may be worthwhile to implement a specialized version for
 * CSizeComputer, which uses the s.seek() method to record bytes that would
 * be written instead.
 */
class CSizeComputer
{
protected:
    size_t nSize;

    const int nType;
    const int nVersion;
public:
    CSizeComputer(int nTypeIn, int nVersionIn) : nSize(0), nType(nTypeIn), nVersion(nVersionIn) {}

    void write([[maybe_unused]] const char *psz, size_t _nSize)
    {
        this->nSize += _nSize;
    }

    void write(Span<const std::byte> src)
    {
        this->nSize += src.size();
    }

    /** Pretend _nSize bytes are written, without specifying them. */
    void seek(size_t _nSize)
    {
        this->nSize += _nSize;
    }

    template<typename T>
    CSizeComputer& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return (*this);
    }

    size_t size() const {
        return nSize;
    }

    int GetVersion() const { return nVersion; }
    int GetType() const { return nType; }
};

template<typename Stream>
void SerializeMany(Stream& s)
{
}

template<typename Stream, typename Arg>
void SerializeMany(Stream& s, Arg&& arg)
{
    ::Serialize(s, std::forward<Arg>(arg));
}

template<typename Stream, typename Arg, typename... Args>
void SerializeMany(Stream& s, Arg&& arg, Args&&... args)
{
    ::Serialize(s, std::forward<Arg>(arg));
    ::SerializeMany(s, std::forward<Args>(args)...);
}

template<typename Stream>
inline void UnserializeMany(Stream& s)
{
}

template<typename Stream, typename Arg>
inline void UnserializeMany(Stream& s, Arg& arg)
{
    ::Unserialize(s, arg);
}

template<typename Stream, typename Arg, typename... Args>
inline void UnserializeMany(Stream& s, Arg& arg, Args&... args)
{
    ::Unserialize(s, arg);
    ::UnserializeMany(s, args...);
}

template<typename Stream, typename... Args>
inline void SerReadWriteMany(Stream& s, [[maybe_unused]] CSerActionSerialize ser_action, Args&&... args)
{
    ::SerializeMany(s, std::forward<Args>(args)...);
}

template<typename Stream, typename... Args>
inline void SerReadWriteMany(Stream& s, [[maybe_unused]] CSerActionUnserialize ser_action, Args&... args)
{
    ::UnserializeMany(s, args...);
}

template<typename I>
inline void WriteVarInt(CSizeComputer &s, I n)
{
    s.seek(GetSizeOfVarInt<I>(n));
}

inline void WriteCompactSize(CSizeComputer &s, uint64_t nSize)
{
    s.seek(GetSizeOfCompactSize(nSize));
}

template <typename T>
size_t GetSerializeSize(const T& t, int nType, int nVersion = 0)
{
    return (CSizeComputer(nType, nVersion) << t).size();
}

template <typename S, typename T>
size_t GetSerializeSize(const S& s, const T& t)
{
    return (CSizeComputer(s.GetType(), s.GetVersion()) << t).size();
}

#endif // CORE_SERIALIZE_H
