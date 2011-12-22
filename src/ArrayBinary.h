#ifndef __TDB_ARRAY_BINARY__
#define __TDB_ARRAY_BINARY__

#include "ArrayBlob.h"

class ArrayBinary : public Array {
public:
	ArrayBinary(Array* parent=NULL, size_t pndx=0, Allocator& alloc=GetDefaultAllocator());
	ArrayBinary(size_t ref, const Array* parent, size_t pndx, Allocator& alloc=GetDefaultAllocator());
	//ArrayBinary(Allocator& alloc);
	~ArrayBinary();

	bool IsEmpty() const;
	size_t Size() const;

	const void* Get(size_t ndx) const;
	size_t GetLen(size_t ndx) const;

	void Add(const void* value, size_t len);
	void Set(size_t ndx, const void* value, size_t len);
	void Insert(size_t ndx, const void* value, size_t len);
	void Delete(size_t ndx);
	void Clear();

	template<class S> size_t Write(S& out, size_t& pos) const;

private:
	Array m_offsets;
	ArrayBlob m_blob;
};

// Templates

template<class S>
size_t ArrayBinary::Write(S& out, size_t& pos) const{
	// Write out offsets
	const size_t offsets_pos = pos;
	pos += m_offsets.Write(out);

	// Write out data
	const size_t blob_pos = pos;
	pos += m_blob.Write(out);

	// Write new array with node info
	const size_t node_pos = pos;
	Array node(COLUMN_HASREFS);
	node.Add(offsets_pos);
	node.Add(blob_pos);
	pos += node.Write(out);

	// Clean-up
	node.SetType(COLUMN_NORMAL); // avoid recursive del
	node.Destroy();

	return node_pos;
}


#endif //__TDB_ARRAY_BINARY__
