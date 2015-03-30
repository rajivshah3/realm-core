#ifdef _MSC_VER
#include <win32/types.h> //ssize_t
#endif

#include <realm/array_string_long.hpp>
#include <realm/array_blob.hpp>
#include <realm/array_integer.hpp>
#include <realm/impl/destroy_guard.hpp>
#include <realm/column.hpp>

using namespace std;
using namespace realm;


void ArrayStringLong::init_from_mem(MemRef mem) REALM_NOEXCEPT
{
    Array::init_from_mem(mem);
    ref_type offsets_ref = get_as_ref(0), blob_ref = get_as_ref(1);
    m_offsets.init_from_ref(offsets_ref);
    m_blob.init_from_ref(blob_ref);
}


void ArrayStringLong::add(StringData value)
{
    bool add_zero_term = true;
    m_blob.add(value.data(), value.size(), add_zero_term);
    size_t end = value.size() + 1;
    if (!m_offsets.is_empty())
        end += to_size_t(m_offsets.back());
    m_offsets.add(end);
}

void ArrayStringLong::set(size_t ndx, StringData value)
{
    REALM_ASSERT_3(ndx, <, m_offsets.size());

    size_t begin = 0 < ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t end   = to_size_t(m_offsets.get(ndx));
    bool add_zero_term = true;
    m_blob.replace(begin, end, value.data(), value.size(), add_zero_term);

    size_t new_end = begin + value.size() + 1;
    int64_t diff =  int64_t(new_end) - int64_t(end);
    m_offsets.adjust(ndx, m_offsets.size(), diff);
}

void ArrayStringLong::insert(size_t ndx, StringData value)
{
    REALM_ASSERT_3(ndx, <=, m_offsets.size());

    size_t pos = 0 < ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    bool add_zero_term = true;
    m_blob.insert(pos, value.data(), value.size(), add_zero_term);

    m_offsets.insert(ndx, pos + value.size() + 1);
    m_offsets.adjust(ndx+1, m_offsets.size(), value.size() + 1);
}

void ArrayStringLong::erase(size_t ndx)
{
    REALM_ASSERT_3(ndx, <, m_offsets.size());

    size_t begin = 0 < ndx ? to_size_t(m_offsets.get(ndx-1)) : 0;
    size_t end   = to_size_t(m_offsets.get(ndx));

    m_blob.erase(begin, end);
    m_offsets.erase(ndx);
    m_offsets.adjust(ndx, m_offsets.size(), int64_t(begin) - int64_t(end));
}

size_t ArrayStringLong::count(StringData value, size_t begin,
                              size_t end) const REALM_NOEXCEPT
{
    size_t num_matches = 0;

    size_t begin_2 = begin;
    for (;;) {
        size_t ndx = find_first(value, begin_2, end);
        if (ndx == not_found)
            break;
        ++num_matches;
        begin_2 = ndx + 1;
    }

    return num_matches;
}

size_t ArrayStringLong::find_first(StringData value, size_t begin,
                                   size_t end) const REALM_NOEXCEPT
{
    size_t n = m_offsets.size();
    if (end == npos)
        end = n;
    REALM_ASSERT(begin <= n && end <= n && begin <= end);

    size_t begin_2 = 0 < begin ? to_size_t(m_offsets.get(begin-1)) : 0;
    for (size_t i = begin; i < end; ++i) {
        size_t end_2 = to_size_t(m_offsets.get(i));
        size_t end_3 = end_2 - 1; // Discount terminating zero
        StringData value_2 = StringData(m_blob.get(begin_2), end_3-begin_2);
        if (value_2 == value)
            return i;
        begin_2 = end_2;
    }

    return not_found;
}

void ArrayStringLong::find_all(Column& result, StringData value, size_t add_offset,
                              size_t begin, size_t end) const
{
    size_t begin_2 = begin;
    for (;;) {
        size_t ndx = find_first(value, begin_2, end);
        if (ndx == not_found)
            break;
        result.add(add_offset + ndx); // Throws
        begin_2 = ndx + 1;
    }
}


StringData ArrayStringLong::get(const char* header, size_t ndx, Allocator& alloc) REALM_NOEXCEPT
{
    pair<int_least64_t, int_least64_t> p = get_two(header, 0);
    ref_type offsets_ref = to_ref(p.first);
    ref_type blob_ref    = to_ref(p.second);

    const char* offsets_header = alloc.translate(offsets_ref);
    size_t begin, end;
    if (0 < ndx) {
        p = get_two(offsets_header, ndx-1);
        begin = to_size_t(p.first);
        end   = to_size_t(p.second);
    }
    else {
        begin = 0;
        end   = to_size_t(Array::get(offsets_header, 0));
    }
    --end; // Discount the terminating zero

    const char* blob_header = alloc.translate(blob_ref);
    const char* data = ArrayBlob::get(blob_header, begin);
    size_t size = end - begin;
    return StringData(data, size);
}


// FIXME: Not exception safe (leaks are possible).
ref_type ArrayStringLong::bptree_leaf_insert(size_t ndx, StringData value, TreeInsertBase& state)
{
    size_t leaf_size = size();
    REALM_ASSERT_3(leaf_size, <=, REALM_MAX_BPNODE_SIZE);
    if (leaf_size < ndx)
        ndx = leaf_size;
    if (REALM_LIKELY(leaf_size < REALM_MAX_BPNODE_SIZE)) {
        insert(ndx, value); // Throws
        return 0; // Leaf was not split
    }

    // Split leaf node
    ArrayStringLong new_leaf(get_alloc());
    new_leaf.create(); // Throws
    if (ndx == leaf_size) {
        new_leaf.add(value); // Throws
        state.m_split_offset = ndx;
    }
    else {
        for (size_t i = ndx; i != leaf_size; ++i)
            new_leaf.add(get(i)); // Throws
        truncate(ndx); // Throws
        add(value); // Throws
        state.m_split_offset = ndx + 1;
    }
    state.m_split_size = leaf_size + 1;
    return new_leaf.get_ref();
}


MemRef ArrayStringLong::create_array(size_t size, Allocator& alloc)
{
    Array top(alloc);
    _impl::DeepArrayDestroyGuard dg(&top);
    top.create(type_HasRefs); // Throws

    _impl::DeepArrayRefDestroyGuard dg_2(alloc);
    {
        bool context_flag = false;
        int_fast64_t value = 0;
        MemRef mem = ArrayInteger::create_array(type_Normal, context_flag, size, value, alloc); // Throws
        dg_2.reset(mem.m_ref);
        int_fast64_t v(mem.m_ref); // FIXME: Dangerous cast (unsigned -> signed)
        top.add(v); // Throws
        dg_2.release();
    }
    {
        size_t blobs_size = 0;
        MemRef mem = ArrayBlob::create_array(blobs_size, alloc); // Throws
        dg_2.reset(mem.m_ref);
        int_fast64_t v(mem.m_ref); // FIXME: Dangerous cast (unsigned -> signed)
        top.add(v); // Throws
        dg_2.release();
    }

    dg.release();
    return top.get_mem();
}


MemRef ArrayStringLong::slice(size_t offset, size_t size, Allocator& target_alloc) const
{
    REALM_ASSERT(is_attached());

    ArrayStringLong slice(target_alloc);
    _impl::ShallowArrayDestroyGuard dg(&slice);
    slice.create(); // Throws
    size_t begin = offset;
    size_t end   = offset + size;
    for (size_t i = begin; i != end; ++i) {
        StringData value = get(i);
        slice.add(value); // Throws
    }
    dg.release();
    return slice.get_mem();
}


#ifdef REALM_DEBUG

void ArrayStringLong::to_dot(ostream& out, StringData title) const
{
    ref_type ref = get_ref();

    out << "subgraph cluster_arraystringlong" << ref << " {" << endl;
    out << " label = \"ArrayStringLong";
    if (title.size() != 0)
        out << "\\n'" << title << "'";
    out << "\";" << endl;

    Array::to_dot(out, "stringlong_top");
    m_offsets.to_dot(out, "offsets");
    m_blob.to_dot(out, "blob");

    out << "}" << endl;
}

#endif // REALM_DEBUG