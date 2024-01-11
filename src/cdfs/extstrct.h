#ifndef _EXTSTRCT_
#define _EXTSTRCT_

typedef z_stream ZSTREAM;
typedef z_streamp PZSTREAM;

template <ULONG tag>
class PAGED_OBJECT
{
public:
#pragma code_seg(push, "PAGE")

	PAGED_OBJECT()
	{
		PAGED_CODE();
	}

	PVOID operator new(size_t lBlockSize)
	{
		PAGED_CODE();
		__try
		{
			return FsRtlAllocatePoolWithTag(CdPagedPool, lBlockSize, tag);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return NULL;
		}
	}

	VOID operator delete(PVOID p)
	{
		PAGED_CODE();
		CdFreePoolWithTag(&p, tag);
	}

	PVOID Allocate(SIZE_T size, POOL_TYPE pool = CdPagedPool)
	{
		PAGED_CODE();
		return FsRtlAllocatePoolWithTag(pool, size, tag);
	}

	VOID Free(PVOID* p)
	{
		PAGED_CODE();
		CdFreePoolWithTag(p, tag);
	}

#pragma code_seg(pop)
};

//
// Block_info
//

class BLOCK_INFO : public PAGED_OBJECT<TAG_COMPRESSION_BLOCKTABLE>
{
public:
	ULONG m_AddrBegin;
	ULONG m_AddrEnd;
	ULONG m_Size;
	BOOLEAN m_IsZero;

#pragma code_seg(push, "PAGE")

	BLOCK_INFO(ULONG BlockSize, ULONG AddrBegin, ULONG AddrEnd)
		: m_AddrBegin(AddrBegin),
		  m_AddrEnd(AddrEnd),
		  m_IsZero(AddrBegin == AddrEnd)
	{
		PAGED_CODE();
		this->m_Size = (this->m_IsZero ? BlockSize : AddrEnd - AddrBegin);
	}

	BLOCK_INFO(CONST BLOCK_INFO& arg)
		: m_AddrBegin(arg.m_AddrBegin),
		  m_AddrEnd(arg.m_AddrEnd),
		  m_IsZero(arg.m_IsZero),
		  m_Size(arg.m_Size)
	{
		PAGED_CODE();
	}

	PVOID operator new(size_t lBlockSize, PVOID pParam)
	{
		PAGED_CODE();
		UNREFERENCED_PARAMETER(lBlockSize);
		NT_ASSERT(lBlockSize > 0);
		NT_ASSERT(pParam != NULL);
		return pParam;
	}

#pragma code_seg(pop)
};

typedef BLOCK_INFO* PBLOCK_INFO;

//
// Vector of block_info
//

#define INITIAL_CAPACITY 16

class VECTOR_OF_BLOCK_INFO : public PAGED_OBJECT<TAG_COMPRESSION_BLOCKTABLE>
{
public:
	// fields

	ULONG m_Count;
	ULONG m_Capacity;
	PBLOCK_INFO m_Data;
	ULONG m_BlockSize;

	// methods

	BOOLEAN AddItem(__in ULONG AddrBegin, __in ULONG AddrEnd);
	BOOLEAN Compact();

#pragma code_seg(push, "PAGE")

	VECTOR_OF_BLOCK_INFO(__in ULONG BlockSize, __in ULONG InitialCapacity = INITIAL_CAPACITY)
		: m_BlockSize(BlockSize), m_Count(0), m_Capacity(InitialCapacity)
	{
		PAGED_CODE();
		this->m_Data = (PBLOCK_INFO)Allocate(InitialCapacity * sizeof(BLOCK_INFO));
		if (!this->m_Data)
		{
			this->m_Capacity = 0;
		}
	}

	~VECTOR_OF_BLOCK_INFO()
	{
		PAGED_CODE();
		Free((PVOID*)&this->m_Data);
		this->m_Count = 0;
		this->m_Capacity = 0;
		this->m_BlockSize = 0;
	}

	PBLOCK_INFO LastItem()
	{
		PAGED_CODE();
		return (this->m_Data ? &this->m_Data[this->m_Count - 1] : NULL);
	}

	BOOLEAN IsEmpty() const
	{
		PAGED_CODE();
		return this->m_Count == 0;
	}

	const BLOCK_INFO& operator[](ULONG index) const
	{
		PAGED_CODE();
		NT_ASSERT(index < this->m_Count);
		return this->m_Data[index];
	}

#pragma code_seg(pop)
};

typedef VECTOR_OF_BLOCK_INFO* PVECTOR_OF_BLOCK_INFO;

#define CdAllocateVectorOfBlockInfo(IC, Capacity) \
	new VECTOR_OF_BLOCK_INFO(Capacity)

#pragma code_seg(push, "PAGE")

INLINE VOID CdDeallocateVectorOfBlockInfo(
	__inout_opt 
	__drv_freesMem(__drv_deref(This)) 
	__drv_out_deref( __null )
	PVECTOR_OF_BLOCK_INFO* This)
{
	PAGED_CODE();
	NT_ASSERT(This != NULL);
	if (*This)
	{
		delete *This;
		(*This) = NULL;
	}
}

#pragma code_seg(pop)

//
// Compression context;
//


class COMPRESSION_CONTEXT : public PAGED_OBJECT<TAG_COMPRESSION_CTX>
{
public:
	// fields
	PZSTREAM m_Zstream;
	//
	PMDL m_Mdl;
	PUCHAR m_Buffer;
	//
	// Real size of buffer (aligned data)
	//
	ULONG m_AlignedStartingOffset;
	ULONG m_AlignedSize;
	//
	ULONG m_RawStartingOffset; //offset to first block	
	ULONG m_ComprByteCount;
	ULONG m_ComprOffsetInFirstBlock; //offset in first block where requested data begins
	ULONG m_BlockCount;
	ULONG m_BlockSize;
	ULONG m_ComprFirstBlockIndex;
	//	
	// user things
	//
	PVOID m_UserBuffer;
	PMDL m_UserMdl;
	ULONG m_UserBufferByteCount;
	//	

	// methods
	BOOLEAN AllocateBuffer();
	BOOLEAN Initialize(PIRP_CONTEXT IrpContext, PFCB Fcb);

#pragma code_seg(push, "PAGE")

	COMPRESSION_CONTEXT()
		: m_Zstream(NULL),
		  m_Buffer(NULL)
	{
		PAGED_CODE();
	}

	~COMPRESSION_CONTEXT()
	{
		PAGED_CODE();
		FreeBuffer();
		FreeZstream();
	}

	BOOLEAN AllocateZstream(PIRP_CONTEXT IrpContext);
	
	VOID FreeZstream();

	VOID FreeBuffer();

	VOID Set(ULONG RawStartingOffset, ULONG OffsetInFirstBlock,
		ULONG ComprByteCount, ULONG BlockCount, ULONG BlockSize,
		ULONG AlignedStartingOffset, ULONG AlignedSize,
		ULONG FirstComprBlockIndex);

	VOID SetUserData(PVOID UserBuffer, PMDL UserMdl, ULONG UserBufferByteCount);

#pragma code_seg(pop)
};

typedef COMPRESSION_CONTEXT* PCOMPRESSION_CONTEXT;

#define CdAllocateCompressionContext(IC) \
	new COMPRESSION_CONTEXT

#pragma code_seg(push, "PAGE")

INLINE VOID CdDeallocateCompressionContext(
	__inout_opt 
	__drv_freesMem(__drv_deref(This)) 
	__drv_out_deref( __null )
	PCOMPRESSION_CONTEXT* This)
{
	PAGED_CODE();
	NT_ASSERT(This != NULL);
	if (*This)
	{
		delete *This;
		(*This) = NULL;
	}
}

#pragma code_seg(pop)

#endif
