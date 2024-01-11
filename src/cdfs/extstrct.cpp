#include "CdProcs.h"
#include "extstrct.h"

//
//  The Bug check file id for this module
//

#define BugCheckFileId                   (CDFS_BUG_CHECK_EXTSTRCT)

#pragma code_seg(push, "PAGE")

BOOLEAN VECTOR_OF_BLOCK_INFO::AddItem(__in ULONG AddrBegin, __in ULONG AddrEnd)
{
	ULONG newCapacity;
	PBLOCK_INFO newData;

	PAGED_CODE();

	if (this->m_Count == this->m_Capacity)
	{
		newCapacity = this->m_Capacity + (this->m_Capacity >> 1);
		//allocation
		newData = (PBLOCK_INFO)Allocate(newCapacity * sizeof(BLOCK_INFO));
		if (newData == NULL)
		{
			return FALSE;
		}

		//relocate current data
		//RtlCopyMemory(newData, this->m_Data, this->m_Capacity);		 
		for (ULONG i = 0; i < this->m_Count; ++i)
		{
			new(&newData[i]) BLOCK_INFO(this->m_Data[i]);
		}
		RtlZeroMemory(newData + this->m_Count, sizeof(BLOCK_INFO)*(newCapacity - this->m_Count));
		Free((PVOID*)&this->m_Data);
		//
		this->m_Data = newData;
		this->m_Capacity = newCapacity;
	}
	new(&this->m_Data[this->m_Count]) BLOCK_INFO(this->m_BlockSize, AddrBegin, AddrEnd);
	++(this->m_Count);
	return TRUE;
}

BOOLEAN VECTOR_OF_BLOCK_INFO::Compact()
{
	PBLOCK_INFO newData;
	PAGED_CODE();
	if (this->m_Count == 0)
	{
		Free((PVOID*)&this->m_Data);
		this->m_Capacity = 0;
		return TRUE;
	}
	else if (this->m_Capacity != this->m_Count)
	{
		newData = (PBLOCK_INFO)Allocate(this->m_Count * sizeof(BLOCK_INFO));
		if (newData == NULL)
		{
			return FALSE;
		}
		else
		{
			//RtlCopyMemory(newData, This->m_Data, This->m_Count);
			for (ULONG i = 0; i < this->m_Count; ++i)
			{
				new(&newData[i]) BLOCK_INFO(this->m_Data[i]);
			}
			Free((PVOID*)&this->m_Data);
			this->m_Data = newData;
			this->m_Capacity = this->m_Count;
			return TRUE;
		}
	}
	else
	{
		return TRUE;
	}
}

BOOLEAN COMPRESSION_CONTEXT::AllocateZstream(PIRP_CONTEXT IrpContext)
{
	PAGED_CODE();
	if (!this->m_Zstream)
	{
		this->m_Zstream = (PZSTREAM)Allocate(sizeof(ZSTREAM));
		SafeZeroMemory(IrpContext, this->m_Zstream, sizeof(ZSTREAM));
		inflateInit(this->m_Zstream);
	}
	return this->m_Zstream != NULL;
}

VOID COMPRESSION_CONTEXT::FreeZstream()
{
	PAGED_CODE();
	if (this->m_Zstream)
	{
		inflateEnd(this->m_Zstream);
		Free(reinterpret_cast<PVOID*>(&this->m_Zstream));
	}
}

VOID COMPRESSION_CONTEXT::FreeBuffer()
{
	PAGED_CODE();
	if (!this->m_Mdl)
	{
		return;
	}
	MmUnmapReservedMapping(this->m_Buffer, TAG_COMPRESSION_CTX_MAPPING, this->m_Mdl);
	MmFreeMappingAddress(this->m_Buffer, TAG_COMPRESSION_CTX_MAPPING);
	MmFreePagesFromMdl(this->m_Mdl);
	ExFreePool(this->m_Mdl);
	this->m_Mdl = NULL;
	this->m_Buffer = NULL;
}

VOID COMPRESSION_CONTEXT::Set(ULONG RawStartingOffset, ULONG OffsetInFirstBlock,
	ULONG ComprByteCount, ULONG BlockCount, ULONG BlockSize,
	ULONG AlignedStartingOffset, ULONG AlignedSize,
	ULONG FirstComprBlockIndex)
{
	PAGED_CODE();
	this->m_ComprByteCount = ComprByteCount;
	this->m_RawStartingOffset = RawStartingOffset;
	this->m_ComprOffsetInFirstBlock = OffsetInFirstBlock;
	this->m_BlockCount = BlockCount;
	this->m_BlockSize = BlockSize;
	this->m_AlignedSize = AlignedSize;
	this->m_AlignedStartingOffset = AlignedStartingOffset;
	this->m_ComprFirstBlockIndex = FirstComprBlockIndex;
}

VOID COMPRESSION_CONTEXT::SetUserData(PVOID UserBuffer, PMDL UserMdl, ULONG UserBufferByteCount)
{
	PAGED_CODE();
	this->m_UserBuffer = (
		UserMdl
		?
		MmGetSystemAddressForMdlSafe(UserMdl, NormalPagePriority)
		:
		UserBuffer
		);

	this->m_UserMdl = UserMdl;
	this->m_UserBufferByteCount = UserBufferByteCount;
}

BOOLEAN COMPRESSION_CONTEXT::AllocateBuffer()
{
	PVOID mapping;
	PHYSICAL_ADDRESS Min;
	PHYSICAL_ADDRESS Max;
	PHYSICAL_ADDRESS Skip;
	PAGED_CODE();

	NT_ASSERT(this->m_AlignedSize > 0);

	Min.QuadPart = 0LL;
	Max.QuadPart = -1LL;
	Skip.QuadPart = PAGE_SIZE;

	if (this->m_Buffer)
	{
		FreeBuffer();
	}

	this->m_Buffer = reinterpret_cast<PUCHAR>(MmAllocateMappingAddress(this->m_AlignedSize, TAG_COMPRESSION_CTX_MAPPING));
	if (!this->m_Buffer)
	{
		return FALSE;
	}

	this->m_Mdl = MmAllocatePagesForMdlEx(Min, Max, Skip,
	                                      this->m_AlignedSize, MmNonCached, MM_ALLOCATE_FULLY_REQUIRED);
	if (!this->m_Mdl)
	{
		MmFreeMappingAddress(this->m_Buffer, TAG_COMPRESSION_CTX_MAPPING);
		return FALSE;
	}

	mapping = MmMapLockedPagesWithReservedMapping(this->m_Buffer, TAG_COMPRESSION_CTX_MAPPING, this->m_Mdl, MmNonCached);
	NT_ASSERT(mapping == this->m_Buffer);

	if (!mapping)
	{
		MmFreePagesFromMdl(this->m_Mdl);
		ExFreePool(this->m_Mdl);
		this->m_Mdl = NULL;
		MmFreeMappingAddress(this->m_Buffer, TAG_COMPRESSION_CTX_MAPPING);
		return FALSE;
	}

	return TRUE;
}


#pragma code_seg(pop)
