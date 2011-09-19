/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef ITEM_HH
#define ITEM_HH
#include "config.h"

#include <string>
#include <string.h>
#include <stdio.h>
#include <memcached/engine.h>

#include "mutex.hh"
#include "locks.hh"
#include "atomic.hh"
#include "objectregistry.hh"
#include "stats.hh"

/**
 * A blob is a minimal sized storage for data up to 2^32 bytes long.
 */
class Blob : public RCValue {
public:

    // Constructors.

    /**
     * Create a new Blob holding the given data.
     *
     * @param start the beginning of the data to copy into this blob
     * @param len the amount of data to copy in
     *
     * @return the new Blob instance
     */
    static Blob* New(const char *start, const size_t len) {
        size_t total_len = len + sizeof(Blob);
        Blob *t = new (::operator new(total_len)) Blob(start, len);
        assert(t->length() == len);
        return t;
    }

    /**
     * Create a new Blob holding the contents of the given string.
     *
     * @param s the string whose contents go into the blob
     *
     * @return the new Blob instance
     */
    static Blob* New(const std::string& s) {
        return New(s.data(), s.length());
    }

    /**
     * Create a new Blob pre-filled with the given character.
     *
     * @param len the size of the blob
     * @param c the character to fill the blob with
     *
     * @return the new Blob instance
     */
    static Blob* New(const size_t len, const char c) {
        size_t total_len = len + sizeof(Blob);
        Blob *t = new (::operator new(total_len)) Blob(c, len);
        assert(t->length() == len);
        return t;
    }

    // Actual accessorish things.

    /**
     * Get the pointer to the contents of this Blob.
     */
    const char* getData() const {
        return data;
    }

    /**
     * Get the length of this Blob value.
     */
    size_t length() const {
        return size;
    }

    /**
     * Get the size of this Blob instance.
     */
    size_t getSize() const {
        return size + sizeof(Blob);
    }

    /**
     * Get a std::string representation of this blob.
     */
    const std::string to_s() const {
        return std::string(data, size);
    }

    // This is necessary for making C++ happy when I'm doing a
    // placement new on fairly "normal" c++ heap allocations, just
    // with variable-sized objects.
    void operator delete(void* p) { ::operator delete(p); }

    ~Blob() {
        ObjectRegistry::onDeleteBlob(this);
    }

private:

    explicit Blob(const char *start, const size_t len) :
        size(static_cast<uint32_t>(len))
    {
        std::memcpy(data, start, len);
        ObjectRegistry::onCreateBlob(this);
    }

    explicit Blob(const char c, const size_t len) :
        size(static_cast<uint32_t>(len))
    {
        std::memset(data, c, len);
        ObjectRegistry::onCreateBlob(this);
    }

    const uint32_t size;
    char data[1];

    DISALLOW_COPY_AND_ASSIGN(Blob);
};

typedef RCPtr<Blob> value_t;

/**
 * The Item structure we use to pass information between the memcached
 * core and the backend. Please note that the kvstore don't store these
 * objects, so we do have an extra layer of memory copying :(
 */
class Item {
public:
    Item(const void* k, const size_t nk, const size_t nb,
         const uint32_t fl, const time_t exp, uint64_t theCas = 0,
         int64_t i = -1, uint16_t vbid = 0) :
        flags(fl), exptime(exp), cas(theCas), id(i), vbucketId(vbid),
        seqno(1)
    {
        key.assign(static_cast<const char*>(k), nk);
        assert(id != 0);
        setData(NULL, nb);
        ObjectRegistry::onCreateItem(this);
    }

    Item(const std::string &k, const uint32_t fl, const time_t exp,
         const void *dta, const size_t nb, uint64_t theCas = 0,
         int64_t i = -1, uint16_t vbid = 0) :
        flags(fl), exptime(exp), cas(theCas), id(i), vbucketId(vbid),
        seqno(1)
    {
        key.assign(k);
        assert(id != 0);
        setData(static_cast<const char*>(dta), nb);
        ObjectRegistry::onCreateItem(this);
    }

    Item(const std::string &k, const uint32_t fl, const time_t exp,
         value_t val, uint64_t theCas = 0,  int64_t i = -1, uint16_t vbid = 0,
         uint32_t sno = 1) :
        flags(fl), exptime(exp), value(val), cas(theCas), id(i), vbucketId(vbid),
        seqno(sno)
    {
        assert(id != 0);
        key.assign(k);
        ObjectRegistry::onCreateItem(this);
    }

    Item(const void *k, uint16_t nk, const uint32_t fl, const time_t exp,
         const void *dta, const size_t nb, uint64_t theCas = 0,
         int64_t i = -1, uint16_t vbid = 0) :
        flags(fl), exptime(exp), cas(theCas), id(i), vbucketId(vbid),
        seqno(1)
    {
        assert(id != 0);
        key.assign(static_cast<const char*>(k), nk);
        setData(static_cast<const char*>(dta), nb);
        ObjectRegistry::onCreateItem(this);
    }

    ~Item() {
        ObjectRegistry::onDeleteItem(this);
    }

    const char *getData() const {
        return value->getData();
    }

    value_t getValue() const {
        return value;
    }

    const std::string &getKey() const {
        return key;
    }

    int64_t getId() const {
        return id;
    }

    void setId(int64_t to) {
        id = to;
    }

    int getNKey() const {
        return static_cast<int>(key.length());
    }

    uint32_t getNMetaBytes() const {
        return sizeof(meta);
    }

    const char *getMetaData() {
        size_t nb = sizeof(meta);
        encodeMeta(seqno, cas, getNBytes(), flags, meta, nb);
        return (const char*)meta;
    }

    uint32_t getNBytes() const {
        return static_cast<uint32_t>(value->length());
    }

    time_t getExptime() const {
        return exptime;
    }

    uint32_t getFlags() const {
        return flags;
    }

    uint64_t getCas() const {
        return cas;
    }

    void setCas() {
        cas = nextCas();
    }

    void setCas(uint64_t ncas) {
        cas = ncas;
    }

    /**
     * Append another item to this item
     *
     * @param item the item to append to this one
     * @return true if success
     */
    bool append(const Item &item);

    /**
     * Prepend another item to this item
     *
     * @param item the item to prepend to this one
     * @return true if success
     */
    bool prepend(const Item &item);

    uint16_t getVBucketId(void) const {
        return vbucketId;
    }

    void setVBucketId(uint16_t to) {
        vbucketId = to;
    }

    /**
     * Check if this item is expired or not.
     *
     * @param asOf the time to be compared with this item's expiry time
     * @return true if this item's expiry time < asOf
     */
    bool isExpired(time_t asOf) const {
        if (getExptime() != 0 && getExptime() < asOf) {
            return true;
        }
        return false;
    }

    size_t size() {
        return sizeof(Item) + key.size() + value->getSize();
    }

    uint32_t getSeqno() const {
        return seqno;
    }

    void setSeqno(uint32_t to) {
        seqno = to;
    }

    static void encodeMeta(uint32_t seqno, uint64_t cas, uint32_t length,
                           uint32_t flags, std::string &dest)
    {
        uint8_t meta[22];
        size_t len = sizeof(meta);
        encodeMeta(seqno, cas, length, flags, meta, len);
        dest.assign((char*)meta, len);
    }

    static bool encodeMeta(const Item &itm, uint8_t *dest, size_t &nbytes)
    {
        return encodeMeta(itm.seqno, itm.cas, itm.getNBytes(), itm.flags,
                          dest, nbytes);
    }

    static bool encodeMeta(uint32_t seqno, uint64_t cas, uint32_t length,
                           uint32_t flags, uint8_t *dest, size_t &nbytes)
    {
        if (nbytes < 22) {
            return false;
        }
        seqno = htonl(seqno);
        cas = htonll(cas);
        length = htonl(length);
        flags = htonl(flags);

        dest[0] = 0x01;
        dest[1] = 20;
        memcpy(dest + 2, &seqno, 4);
        memcpy(dest + 6, &cas, 8);
        memcpy(dest + 14, &length, 4);
        memcpy(dest + 18, &flags, 4);
        nbytes = 22;
        return true;
    }

    static bool decodeMeta(const uint8_t *dta, uint32_t &seqno, uint64_t &cas,
                           uint32_t &length, uint32_t &flags) {
        if (*dta != 0x01) {
            // Unsupported meta tag
            return false;
        }
        ++dta;
        if (*dta != 20) {
            // Unsupported size
            return false;
        }
        ++dta;
        memcpy(&seqno, dta, 4);
        seqno = ntohl(seqno);
        dta += 4;
        memcpy(&cas, dta, 8);
        cas = ntohll(cas);
        dta += 8;
        memcpy(&length, dta, 4);
        length = ntohl(length);
        dta += 4;
        memcpy(&flags, dta, 4);
        flags = ntohl(flags);

        return true;
    }


private:
    /**
     * Set the item's data. This is only used by constructors, so we
     * make it private.
     */
    void setData(const char *dta, const size_t nb) {
        Blob *data;
        if (dta == NULL) {
            data = Blob::New(nb, '\0');
        } else {
            data = Blob::New(dta, nb);
        }

        assert(data);
        value.reset(data);
    }

    uint32_t flags;
    time_t exptime;
    std::string key;
    value_t value;
    uint64_t cas;
    int64_t id;
    uint16_t vbucketId;

    uint32_t seqno;
    uint8_t meta[22];

    static uint64_t nextCas(void) {
        uint64_t ret = gethrtime();
        if ((ret & 1000) == 0) {
            // we don't have a good enough resolution on the clock
            ret |= casCounter++;
            if (casCounter > 1000) {
                casCounter = 1;
            }
        }

        return ret;
    }

    static Atomic<uint64_t> casCounter;
    DISALLOW_COPY_AND_ASSIGN(Item);
};

#endif
