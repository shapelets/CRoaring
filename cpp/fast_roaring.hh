// Copyright (c) 2022 Shapelets.io
//
// This software is released under the MIT License.
// https://opensource.org/licenses/MIT

#pragma once

#include "roaring.hh"
using roaring::Roaring;

namespace roaring {

class FastRankRoaringSetBitForwardIterator;

class FastRankRoaring final : public Roaring {
    typedef api::roaring_bitmap_t roaring_bitmap_t;  // class-local name alias

public:
    /**
     * Create an empty bitmap in the existing memory for the class.
     * The bitmap will be in the "clear" state with no auxiliary allocations.
     */
    FastRankRoaring() : roaring{} {
        // The empty constructor roaring{} silences warnings from pedantic static analyzers.
        api::roaring_bitmap_init_cleared(&roaring);
    }

    /**
     * Construct a bitmap from a list of integer values.
     */
    FastRankRoaring(size_t n, const uint32_t *data) : Roaring() {
        api::roaring_bitmap_add_many(&roaring, n, data);
    }

    /**
     * Copy constructor
     */
    FastRankRoaring(const Roaring &r) : Roaring() {
        if (!api::roaring_bitmap_overwrite(&roaring, &r.roaring)) {
            ROARING_TERMINATE("failed roaring_bitmap_overwrite in constructor");
        }
        api::roaring_bitmap_set_copy_on_write(
            &roaring,
            api::roaring_bitmap_get_copy_on_write(&r.roaring));
    }

    /**
     * Move constructor. The moved object remains valid, i.e.
     * all methods can still be called on it.
     */
    explicit FastRankRoaring(Roaring &&r) noexcept : roaring(r.roaring) {
        //
        // !!! This clones the bits of the roaring structure to a new location
        // and then overwrites the old bits...assuming that this will still
        // work.  There are scenarios where this could break; e.g. if some of
        // those bits were pointers into the structure memory itself.  If such
        // things were possible, a roaring_bitmap_move() API would be needed.
        //
        api::roaring_bitmap_init_cleared(&r.roaring);
    }

    /**
     * Construct a roaring object by taking control of a malloc()'d C struct.
     *
     * Passing a NULL pointer is unsafe.
     * The pointer to the C struct will be invalid after the call.
     */
    explicit FastRankRoaring(roaring_bitmap_t *s) noexcept : roaring (*s) {
        roaring_free(s);  // deallocate the passed-in pointer
    }

    /**
     * Construct a bitmap from a list of integer values.
     */
    static FastRankRoaring bitmapOf(size_t n, ...) {
        FastRankRoaring ans;
        va_list vl;
        va_start(vl, n);
        for (size_t i = 0; i < n; i++) {
            ans.add(va_arg(vl, uint32_t));
        }
        va_end(vl);
        return ans;
    }

    /**
     * Add value x
     */
    void add(uint32_t x) {
        api::_resetCache();
        api::roaring_bitmap_add(&roaring, x);
    }

    /**
     * Add value x
     * Returns true if a new value was added, false if the value was already
     * existing.
     */
    bool addChecked(uint32_t x) {
        api::_resetCache();
        return api::roaring_bitmap_add_checked(&roaring, x);
    }

    /**
     * Add all values from x (included) to y (excluded)
     */
    void addRange(const uint64_t x, const uint64_t y)  {
        api::_resetCache();
        return api::roaring_bitmap_add_range(&roaring, x, y);
    }

    /**
     * Add value n_args from pointer vals
     */
    void addMany(size_t n_args, const uint32_t *vals) {
        api::_resetCache();
        api::roaring_bitmap_add_many(&roaring, n_args, vals);
    }

    /**
     * Remove value x
     */
    void remove(uint32_t x) {
        api::_resetCache();
        api::roaring_bitmap_remove(&roaring, x);
    }

    /**
     * Remove value x
     * Returns true if a new value was removed, false if the value was not
     * existing.
     */
    bool removeChecked(uint32_t x) {
        api::_resetCache();
        return api::roaring_bitmap_remove_checked(&roaring, x);
    }

    /**
     * Return the largest value (if not empty)
     */
    uint32_t maximum() const { return api::roaring_bitmap_maximum(&roaring); }

    /**
     * Return the smallest value (if not empty)
     */
    uint32_t minimum() const { return api::roaring_bitmap_minimum(&roaring); }

    /**
     * Check if value x is present
     */
    bool contains(uint32_t x) const {
        return api::roaring_bitmap_contains(&roaring, x);
    }

    /**
     * Check if all values from x (included) to y (excluded) are present
     */
    bool containsRange(const uint64_t x, const uint64_t y) const {
        return api::roaring_bitmap_contains_range(&roaring, x, y);
    }

    /**
     * Destructor.  By contract, calling roaring_bitmap_clear() is enough to
     * release all auxiliary memory used by the structure.
     */
    ~FastRankRoaring() {
        if (!(roaring.high_low_container.flags & ROARING_FLAG_FROZEN)) {
            api::roaring_bitmap_clear(&roaring);
        } else {
            // The roaring member variable copies the `roaring_bitmap_t` and
            // nested `roaring_array_t` structures by value and is freed in the
            // constructor, however the underlying memory arena used for the
            // container data is not freed with it. Here we derive the arena
            // pointer from the second arena allocation in
            // `roaring_bitmap_frozen_view` and free it as well.
            roaring_bitmap_free(
                (roaring_bitmap_t *)((char *)
                                     roaring.high_low_container.containers -
                                     sizeof(roaring_bitmap_t)));
        }
    }

    /**
     * Copies the content of the provided bitmap, and
     * discard the current content.
     */
    Roaring &operator=(const Roaring &r) {
        if (!api::roaring_bitmap_overwrite(&roaring, &r.roaring)) {
            ROARING_TERMINATE("failed memory alloc in assignment");
        }
        api::roaring_bitmap_set_copy_on_write(
            &roaring,
            api::roaring_bitmap_get_copy_on_write(&r.roaring));
        api::_resetCache();
        return *this;
    }

    /**
     * Moves the content of the provided bitmap, and
     * discard the current content.
     */
    Roaring &operator=(Roaring &&r) noexcept {
        api::roaring_bitmap_clear(&roaring);  // free this class's allocations

        // !!! See notes in the Move Constructor regarding roaring_bitmap_move()
        //
        roaring = r.roaring;
        api::roaring_bitmap_init_cleared(&r.roaring);
        api::_resetCache();

        return *this;
    }

    /**
     * Compute the intersection between the current bitmap and the provided
     * bitmap, writing the result in the current bitmap. The provided bitmap
     * is not modified.
     */
    Roaring &operator&=(const Roaring &r) {
        api::roaring_bitmap_and_inplace(&roaring, &r.roaring);
        api::_resetCache();
        return *this;
    }

    /**
     * Compute the difference between the current bitmap and the provided
     * bitmap, writing the result in the current bitmap. The provided bitmap
     * is not modified.
     */
    Roaring &operator-=(const Roaring &r) {
        api::roaring_bitmap_andnot_inplace(&roaring, &r.roaring);
        api::_resetCache();
        return *this;
    }

    /**
     * Compute the union between the current bitmap and the provided bitmap,
     * writing the result in the current bitmap. The provided bitmap is not
     * modified.
     *
     * See also the fastunion function to aggregate many bitmaps more quickly.
     */
    Roaring &operator|=(const Roaring &r) {
        api::roaring_bitmap_or_inplace(&roaring, &r.roaring);
        api::_resetCache();
        return *this;
    }

    /**
     * Compute the symmetric union between the current bitmap and the provided
     * bitmap, writing the result in the current bitmap. The provided bitmap
     * is not modified.
     */
    Roaring &operator^=(const Roaring &r) {
        api::roaring_bitmap_xor_inplace(&roaring, &r.roaring);
        api::_resetCache();
        return *this;
    }

    /**
     * Exchange the content of this bitmap with another.
     */
    void swap(Roaring &r) {
        std::swap(r.roaring, roaring);
        api::_resetCache();
    }

    /**
     * Get the cardinality of the bitmap (number of elements).
     */
    uint64_t cardinality() const {
        return api::roaring_bitmap_get_cardinality(&roaring);
    }

    /**
     * Returns true if the bitmap is empty (cardinality is zero).
     */
    bool isEmpty() const { return api::roaring_bitmap_is_empty(&roaring); }

    /**
     * Returns true if the bitmap is subset of the other.
     */
    bool isSubset(const Roaring &r) const {
        return api::roaring_bitmap_is_subset(&roaring, &r.roaring);
    }

    /**
     * Returns true if the bitmap is strict subset of the other.
     */
    bool isStrictSubset(const Roaring &r) const {
        return api::roaring_bitmap_is_strict_subset(&roaring, &r.roaring);
    }

    /**
     * Convert the bitmap to an array. Write the output to "ans", caller is
     * responsible to ensure that there is enough memory allocated
     * (e.g., ans = new uint32[mybitmap.cardinality()];)
     */
    void toUint32Array(uint32_t *ans) const {
        api::roaring_bitmap_to_uint32_array(&roaring, ans);
    }
    /**
     * To int array with pagination
     */
    void rangeUint32Array(uint32_t *ans, size_t offset, size_t limit) const {
        api::roaring_bitmap_range_uint32_array(&roaring, offset, limit, ans);
    }

    /**
     * Return true if the two bitmaps contain the same elements.
     */
    bool operator==(const Roaring &r) const {
        return api::roaring_bitmap_equals(&roaring, &r.roaring);
    }

    /**
     * Compute the negation of the roaring bitmap within a specified interval.
     * Areas outside the range are passed through unchanged.
     */
    void flip(uint64_t range_start, uint64_t range_end) {
        api::roaring_bitmap_flip_inplace(&roaring, range_start, range_end);
        api::_resetCache();
    }

    /**
     * Remove run-length encoding even when it is more space efficient.
     * Return whether a change was applied.
     */
    bool removeRunCompression() {
        api::_resetCache();
        return api::roaring_bitmap_remove_run_compression(&roaring);
    }

    /**
     * Convert array and bitmap containers to run containers when it is more
     * efficient; also convert from run containers when more space efficient.
     * Returns true if the result has at least one run container.  Additional
     * savings might be possible by calling shrinkToFit().
     */
    bool runOptimize() {
        api::_resetCache();
        return api::roaring_bitmap_run_optimize(&roaring);
    }

    /**
     * If needed, reallocate memory to shrink the memory usage. Returns
     * the number of bytes saved.
     */
    size_t shrinkToFit() {
        api::_resetCache();
        return api::roaring_bitmap_shrink_to_fit(&roaring);
    }

    /**
     * Iterate over the bitmap elements. The function iterator is called once
     * for all the values with ptr (can be NULL) as the second parameter of
     * each call.
     *
     * roaring_iterator is simply a pointer to a function that returns bool
     * (true means that the iteration should continue while false means that it
     * should stop), and takes (uint32_t,void*) as inputs.
     */
    void iterate(api::roaring_iterator iterator, void *ptr) const {
        api::roaring_iterate(&roaring, iterator, ptr);
    }

    /**
     * Selects the value at index rnk in the bitmap, where the smallest value
     * is at index 0.
     *
     * If the size of the roaring bitmap is strictly greater than rank, then
     * this function returns true and sets element to the element of given rank.
     * Otherwise, it returns false.
     */
    bool select(uint32_t rnk, uint32_t *element) const {
        return api::roaring_bitmap_select(&roaring, rnk, element);
    }

    /**
     * Computes the size of the intersection between two bitmaps.
     */
    uint64_t and_cardinality(const Roaring &r) const {
        return api::roaring_bitmap_and_cardinality(&roaring, &r.roaring);
    }

    /**
     * Check whether the two bitmaps intersect.
     */
    bool intersect(const Roaring &r) const {
        return api::roaring_bitmap_intersect(&roaring, &r.roaring);
    }

    /**
     * Computes the Jaccard index between two bitmaps. (Also known as the
     * Tanimoto distance,
     * or the Jaccard similarity coefficient)
     *
     * The Jaccard index is undefined if both bitmaps are empty.
     */
    double jaccard_index(const Roaring &r) const {
        return api::roaring_bitmap_jaccard_index(&roaring, &r.roaring);
    }

    /**
     * Computes the size of the union between two bitmaps.
     */
    uint64_t or_cardinality(const Roaring &r) const {
        return api::roaring_bitmap_or_cardinality(&roaring, &r.roaring);
    }

    /**
     * Computes the size of the difference (andnot) between two bitmaps.
     */
    uint64_t andnot_cardinality(const Roaring &r) const {
        return api::roaring_bitmap_andnot_cardinality(&roaring, &r.roaring);
    }

    /**
     * Computes the size of the symmetric difference (andnot) between two
     * bitmaps.
     */
    uint64_t xor_cardinality(const Roaring &r) const {
        return api::roaring_bitmap_xor_cardinality(&roaring, &r.roaring);
    }

    /**
     * Returns the number of integers that are smaller or equal to x.
     * Thus the rank of the smallest element is one.  If
     * x is smaller than the smallest element, this function will return 0.
     * The rank and select functions differ in convention: this function returns
     * 1 when ranking the smallest value, but the select function returns the
     * smallest value when using index 0.
     */
    uint64_t rank(uint32_t x) const {
        return api::roaring_bitmap_fast_rank(&roaring, x);
    }

    /**
     * Write a bitmap to a char buffer. This is meant to be compatible with
     * the Java and Go versions. Returns how many bytes were written which
     * should be getSizeInBytes().
     *
     * Setting the portable flag to false enable a custom format that
     * can save space compared to the portable format (e.g., for very
     * sparse bitmaps).
     *
     * Boost users can serialize bitmaps in this manner:
     *
     *       BOOST_SERIALIZATION_SPLIT_FREE(Roaring)
     *       namespace boost {
     *       namespace serialization {
     *
     *       template <class Archive>
     *       void save(Archive& ar, const Roaring& bitmask,
     *          const unsigned int version) {
     *         std::size_t expected_size_in_bytes = bitmask.getSizeInBytes();
     *         std::vector<char> buffer(expected_size_in_bytes);
     *         std::size_t       size_in_bytes = bitmask.write(buffer.data());
     *
     *         ar& size_in_bytes;
     *         ar& boost::serialization::make_binary_object(buffer.data(),
     *             size_in_bytes);
     *      }
     *      template <class Archive>
     *      void load(Archive& ar, Roaring& bitmask,
     *          const unsigned int version) {
     *         std::size_t size_in_bytes = 0;
     *         ar& size_in_bytes;
     *         std::vector<char> buffer(size_in_bytes);
     *         ar&  boost::serialization::make_binary_object(buffer.data(),
     *            size_in_bytes);
     *         bitmask = Roaring::readSafe(buffer.data(), size_in_bytes);
     *      }
     *      }  // namespace serialization
     *      }  // namespace boost
     */
    size_t write(char *buf, bool portable = true) const {
        api::_resetCache();
        if (portable)
            return api::roaring_bitmap_portable_serialize(&roaring, buf);
        else
            return api::roaring_bitmap_serialize(&roaring, buf);
    }

    /**
     * Read a bitmap from a serialized version. This is meant to be compatible
     * with the Java and Go versions.
     *
     * Setting the portable flag to false enable a custom format that
     * can save space compared to the portable format (e.g., for very
     * sparse bitmaps).
     *
     * This function is unsafe in the sense that if you provide bad data,
     * many, many bytes could be read. See also readSafe.
     */
    static Roaring read(const char *buf, bool portable = true) {
        roaring_bitmap_t * r = portable
            ? api::roaring_bitmap_portable_deserialize(buf)
            : api::roaring_bitmap_deserialize(buf);
        if (r == NULL) {
            ROARING_TERMINATE("failed alloc while reading");
        }
        return Roaring(r);
    }

    /**
     * Read a bitmap from a serialized version, reading no more than maxbytes
     * bytes.  This is meant to be compatible with the Java and Go versions.
     *
     */
    static Roaring readSafe(const char *buf, size_t maxbytes) {
        roaring_bitmap_t * r =
            api::roaring_bitmap_portable_deserialize_safe(buf,maxbytes);
        if (r == NULL) {
            ROARING_TERMINATE("failed alloc while reading");
        }
        return Roaring(r);
    }

    /**
     * How many bytes are required to serialize this bitmap (meant to be
     * compatible with Java and Go versions)
     *
     * Setting the portable flag to false enable a custom format that
     * can save space compared to the portable format (e.g., for very
     * sparse bitmaps).
     */
    size_t getSizeInBytes(bool portable = true) const {
        if (portable)
            return api::roaring_bitmap_portable_size_in_bytes(&roaring);
        else
            return api::roaring_bitmap_size_in_bytes(&roaring);
    }

    static const Roaring frozenView(const char *buf, size_t length) {
        const roaring_bitmap_t *s =
            api::roaring_bitmap_frozen_view(buf, length);
        if (s == NULL) {
            ROARING_TERMINATE("failed to read frozen bitmap");
        }
        Roaring r;
        r.roaring = *s;
        return r;
    }

    void writeFrozen(char *buf) const {
        roaring_bitmap_frozen_serialize(&roaring, buf);
    }

    size_t getFrozenSizeInBytes() const {
        return roaring_bitmap_frozen_size_in_bytes(&roaring);
    }

    /**
     * Computes the intersection between two bitmaps and returns new bitmap.
     * The current bitmap and the provided bitmap are unchanged.
     */
    Roaring operator&(const Roaring &o) const {
        roaring_bitmap_t *r = api::roaring_bitmap_and(&roaring, &o.roaring);
        if (r == NULL) {
            ROARING_TERMINATE("failed materalization in and");
        }
        return Roaring(r);
    }

    /**
     * Computes the difference between two bitmaps and returns new bitmap.
     * The current bitmap and the provided bitmap are unchanged.
     */
    Roaring operator-(const Roaring &o) const {
        roaring_bitmap_t *r = api::roaring_bitmap_andnot(&roaring, &o.roaring);
        if (r == NULL) {
            ROARING_TERMINATE("failed materalization in andnot");
        }
        return Roaring(r);
    }

    /**
     * Computes the union between two bitmaps and returns new bitmap.
     * The current bitmap and the provided bitmap are unchanged.
     */
    Roaring operator|(const Roaring &o) const {
        roaring_bitmap_t *r = api::roaring_bitmap_or(&roaring, &o.roaring);
        if (r == NULL) {
            ROARING_TERMINATE("failed materalization in or");
        }
        return Roaring(r);
    }

    /**
     * Computes the symmetric union between two bitmaps and returns new bitmap.
     * The current bitmap and the provided bitmap are unchanged.
     */
    Roaring operator^(const Roaring &o) const {
        roaring_bitmap_t *r = api::roaring_bitmap_xor(&roaring, &o.roaring);
        if (r == NULL) {
            ROARING_TERMINATE("failed materalization in xor");
        }
        return Roaring(r);
    }

    /**
     * Whether or not we apply copy and write.
     */
    void setCopyOnWrite(bool val) {
        api::roaring_bitmap_set_copy_on_write(&roaring, val);
    }

    /**
     * Print the content of the bitmap
     */
    void printf() const { api::roaring_bitmap_printf(&roaring); }

    /**
     * Print the content of the bitmap into a string
     */
    std::string toString() const {
        struct iter_data {
            std::string str{}; // The empty constructor silences warnings from pedantic static analyzers.
            char first_char = '{';
        } outer_iter_data;
        if (!isEmpty()) {
            iterate(
                [](uint32_t value, void *inner_iter_data) -> bool {
                    ((iter_data *)inner_iter_data)->str +=
                        ((iter_data *)inner_iter_data)->first_char;
                    ((iter_data *)inner_iter_data)->str +=
                        std::to_string(value);
                    ((iter_data *)inner_iter_data)->first_char = ',';
                    return true;
                },
                (void *)&outer_iter_data);
        } else
            outer_iter_data.str = '{';
        outer_iter_data.str += '}';
        return outer_iter_data.str;
    }

    /**
     * Whether or not copy and write is active.
     */
    bool getCopyOnWrite() const {
        return api::roaring_bitmap_get_copy_on_write(&roaring);
    }

    /**
     * Computes the logical or (union) between "n" bitmaps (referenced by a
     * pointer).
     */
    static Roaring fastunion(size_t n, const Roaring **inputs) {
        const roaring_bitmap_t **x =
            (const roaring_bitmap_t **)roaring_malloc(n * sizeof(roaring_bitmap_t *));
        if (x == NULL) {
            ROARING_TERMINATE("failed memory alloc in fastunion");
        }
        for (size_t k = 0; k < n; ++k) x[k] = &inputs[k]->roaring;

        roaring_bitmap_t *c_ans = api::roaring_bitmap_or_many(n, x);
        if (c_ans == NULL) {
            roaring_free(x);
            ROARING_TERMINATE("failed memory alloc in fastunion");
        }
        Roaring ans(c_ans);
        roaring_free(x);
        return ans;
    }

    typedef FastRankRoaringSetBitForwardIterator const_iterator;

    /**
     * Returns an iterator that can be used to access the position of the set
     * bits. The running time complexity of a full scan is proportional to the
     * number of set bits: be aware that if you have long strings of 1s, this
     * can be very inefficient.
     *
     * It can be much faster to use the toArray method if you want to retrieve
     * the set bits.
     */
    const_iterator begin() const;

    /**
     * A bogus iterator that can be used together with begin()
     * for constructions such as for (auto i = b.begin(); * i!=b.end(); ++i) {}
     */
    const_iterator &end() const;

    roaring_bitmap_t roaring;
};

/**
 * Used to go through the set bits. Not optimally fast, but convenient.
 */
class FastRankRoaringSetBitForwardIterator final {
public:
    typedef std::forward_iterator_tag iterator_category;
    typedef uint32_t *pointer;
    typedef uint32_t &reference_type;
    typedef uint32_t value_type;
    typedef int32_t difference_type;
    typedef FastRankRoaringSetBitForwardIterator type_of_iterator;

    /**
     * Provides the location of the set bit.
     */
    value_type operator*() const { return i.current_value; }

    bool operator<(const type_of_iterator &o) const {
        if (!i.has_value) return false;
        if (!o.i.has_value) return true;
        return i.current_value < *o;
    }

    bool operator<=(const type_of_iterator &o) const {
        if (!o.i.has_value) return true;
        if (!i.has_value) return false;
        return i.current_value <= *o;
    }

    bool operator>(const type_of_iterator &o)  const {
        if (!o.i.has_value) return false;
        if (!i.has_value) return true;
        return i.current_value > *o;
    }

    bool operator>=(const type_of_iterator &o)  const {
        if (!i.has_value) return true;
        if (!o.i.has_value) return false;
        return i.current_value >= *o;
    }

    /**
     * Move the iterator to the first value >= val.
     */
    void equalorlarger(uint32_t val) {
        api::roaring_move_uint32_iterator_equalorlarger(&i,val);
    }

    type_of_iterator &operator++() {  // ++i, must returned inc. value
        api::roaring_advance_uint32_iterator(&i);
        return *this;
    }

    type_of_iterator operator++(int) {  // i++, must return orig. value
        FastRankRoaringSetBitForwardIterator orig(*this);
        api::roaring_advance_uint32_iterator(&i);
        return orig;
    }

    type_of_iterator& operator--() { // prefix --
        api::roaring_previous_uint32_iterator(&i);
        return *this;
    }

    type_of_iterator operator--(int) { // postfix --
        FastRankRoaringSetBitForwardIterator orig(*this);
        api::roaring_previous_uint32_iterator(&i);
        return orig;
    }

    bool operator==(const FastRankRoaringSetBitForwardIterator &o) const {
        return i.current_value == *o && i.has_value == o.i.has_value;
    }

    bool operator!=(const FastRankRoaringSetBitForwardIterator &o) const {
        return i.current_value != *o || i.has_value != o.i.has_value;
    }

    FastRankRoaringSetBitForwardIterator(const FastRankRoaring &parent,
                                 bool exhausted = false) {
        if (exhausted) {
            i.parent = &parent.roaring;
            i.container_index = INT32_MAX;
            i.has_value = false;
            i.current_value = UINT32_MAX;
        } else {
            api::roaring_init_iterator(&parent.roaring, &i);
        }
    }

    api::roaring_uint32_iterator_t i{}; // The empty constructor silences warnings from pedantic static analyzers.
};

inline FastRankRoaringSetBitForwardIterator FastRankRoaring::begin() const {
    return FastRankRoaringSetBitForwardIterator(*this);
}

inline FastRankRoaringSetBitForwardIterator &FastRankRoaring::end() const {
    static FastRankRoaringSetBitForwardIterator e(*this, true);
    return e;
}

}  // namespace roaring
