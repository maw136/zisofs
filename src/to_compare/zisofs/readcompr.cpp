#include "CdProcs.h"
#include "ReadCompr.h"

//
//  The Bug check file id for this module
//

#define BugCheckFileId                   (CDFS_BUG_CHECK_READCOMPR)

class __LOCAL_Buffer;

extern "C"
__drv_mustHoldCriticalRegion
NTSTATUS
CdRawReadFile(
	__in PIRP_CONTEXT IrpContext,
	     __in PFCB Fcb,
	     __in LONGLONG Offset,
	     __in ULONG Length,
	     __inout __LOCAL_Buffer& Buff);


#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, CdInitializeFcbBlockOffsetTable)
#pragma alloc_text(PAGE, CdInflateData)
#pragma alloc_text(PAGE, CdRawReadFile)
#pragma alloc_text(PAGE, CdTranslateCompressedReadParams)
#pragma alloc_text(PAGE, CdComprPrepareBuffer)
#pragma alloc_text(PAGE, CdComprFinishBuffers)
#endif

#define CdAllocateCompressionBuffer(IC, S) \
	(PUCHAR)FsRtlAllocatePoolWithTag( CdNonPagedPool, (S), TAG_COMPRESSION_BUFFER )

#define CdDeallocateCompressionBuffer(IC, B) \
	CdFreePool( (PVOID*) &(B) )

//helper structs
static const UCHAR MAGIC[] = {0x37, 0xe4, 0x53, 0x96, 0xc9, 0xdb, 0xd6, 0x07};

class ZISO_HEADER
{
public:
	UCHAR Magic[8];
	ULONG RealSize;
	UCHAR HeaderSize; //>>2
	UCHAR BlockSize; //log2
	UCHAR Reserved[2]; //0
};

typedef ZISO_HEADER* PZISO_HEADER;

//stack only class
class __LOCAL_Buffer
{
public:
	PUCHAR Buff;
	PMDL MdlBuff;

	__LOCAL_Buffer() : Buff(NULL), MdlBuff(NULL)
	{
	}


	void Allocate(PIRP_CONTEXT IrpContext)
	{
		Buff = (PUCHAR)FsRtlAllocatePoolWithTag(
			CdNonPagedPool, CD_SECTOR_SIZE, TAG_COMPRESSION_BUFFER );

		if (!Buff)
		{
			CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
		}

		MdlBuff = IoAllocateMdl(Buff, CD_SECTOR_SIZE, FALSE, FALSE, NULL);
		if (!MdlBuff)
		{
			CdFreePool((PVOID*)&Buff);
			CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
		}
		MmBuildMdlForNonPagedPool(MdlBuff);
	}

	void Zero(PIRP_CONTEXT IrpContext)
	{
		SafeZeroMemory(IrpContext, Buff, CD_SECTOR_SIZE);
	}

	void Deallocate()
	{
		if (MdlBuff)
		{
			IoFreeMdl(MdlBuff);
			MdlBuff = NULL;
		}
		if (Buff)
		{
			CdFreePool((PVOID*)&Buff);
		}
	}
};


// local support routine

__drv_mustHoldCriticalRegion
NTSTATUS CdInflateData(
	PIRP_CONTEXT IrpContext, PIRP Irp, PFCB Fcb, PCOMPRESSION_CONTEXT CompressionCtx)
{
	UNREFERENCED_PARAMETER(IrpContext);
	UNREFERENCED_PARAMETER(Irp);

	PUCHAR UserBuffer;
	const PZSTREAM Zstream = CompressionCtx->m_Zstream;
	PUCHAR HelperCompressedDataPointer;
	PUCHAR HelperCopySource;
	ULONG LastBlock;
	int Err;
	ULONG ToCopyCount = 0;
	ULONG LeftToCopyCount = CompressionCtx->m_ComprByteCount;
	ULONG Block = CompressionCtx->m_ComprFirstBlockIndex;
	const VECTOR_OF_BLOCK_INFO& BlockOffsetTable = *Fcb->BlockOffsetTable;

	PUCHAR TempBuffer;
	const ULONG BlockSize = CompressionCtx->m_BlockSize;

	PAGED_CODE();

	UserBuffer = (PUCHAR)CompressionCtx->m_UserBuffer;

	if (!UserBuffer)
	{
		CdRaiseStatus(IrpContext, STATUS_INSUFFICIENT_RESOURCES);
	}

	TempBuffer = CdAllocateCompressionBuffer(IrpContext, BlockSize);

	if (!TempBuffer)
	{
		CdRaiseStatus(IrpContext, STATUS_INSUFFICIENT_RESOURCES);
	}

	HelperCompressedDataPointer = CompressionCtx->m_Buffer + CompressionCtx->m_RawStartingOffset;
	//
	// Decompression here
	//

	NT_ASSERT(CompressionCtx->m_BlockCount > 0);

	if (CompressionCtx->m_BlockCount == 1)
	{
		//single block, possible real data less than block ->copy error
		ToCopyCount = LeftToCopyCount;
		if (BlockOffsetTable[Block].m_IsZero)
		{
			SafeZeroMemory(IrpContext, UserBuffer, ToCopyCount);
		}
		else
		{
			HelperCopySource = TempBuffer + CompressionCtx->m_ComprOffsetInFirstBlock;
			SafeZeroMemory(IrpContext, TempBuffer, BlockSize);
			Zstream->total_out = 0;
			Zstream->avail_out = BlockSize;
			Zstream->next_out = TempBuffer;
			//
			Zstream->next_in = HelperCompressedDataPointer;
			Zstream->total_in = 0;
			Zstream->avail_in = BlockOffsetTable[Block].m_Size;

			inflateReset(Zstream);
			Err = inflate(Zstream, Z_SYNC_FLUSH);

			NT_ASSERT(Err == Z_STREAM_END);
			NT_ASSERT(Zstream->total_in == BlockOffsetTable[Block].m_Size);

			NT_ASSERT((ULONG)((UserBuffer + ToCopyCount) - (PUCHAR)CompressionCtx->m_UserBuffer) < CompressionCtx->m_UserBufferByteCount);
			NT_ASSERT(ToCopyCount <= BlockSize);
			NT_ASSERT((ULONG)(HelperCopySource - TempBuffer) < BlockSize);
			RtlCopyMemory(UserBuffer, HelperCopySource, ToCopyCount);
		}
	}
	else
	{
		HelperCopySource = TempBuffer;
		LastBlock = CompressionCtx->m_ComprFirstBlockIndex + CompressionCtx->m_BlockCount - 1;
		for (; Block <= LastBlock;
		       UserBuffer += ToCopyCount , LeftToCopyCount -= ToCopyCount , ++Block)
		{
			if (!BlockOffsetTable[Block].m_IsZero)
			{
				SafeZeroMemory(IrpContext, TempBuffer, BlockSize);
				Zstream->total_out = 0;
				Zstream->avail_out = BlockSize;
				Zstream->next_out = TempBuffer;
				//
				DbgPrint("%c\n", *HelperCompressedDataPointer);
				Zstream->next_in = HelperCompressedDataPointer;
				Zstream->total_in = 0;
				Zstream->avail_in = BlockOffsetTable[Block].m_Size;

				inflateReset(Zstream);
				Err = inflate(Zstream, Z_SYNC_FLUSH);

				if (Err != Z_STREAM_END)
				{
					DbgPrint("Inflate error, probably wrong input data");
					DbgBreakPoint();
				}
				NT_ASSERT(Err == Z_STREAM_END);
				NT_ASSERT(Zstream->total_in == BlockOffsetTable[Block].m_Size);

				HelperCompressedDataPointer += Zstream->total_in;
			}

			if (Block == CompressionCtx->m_ComprFirstBlockIndex)
			{
				//first block specific	
				HelperCopySource = TempBuffer + CompressionCtx->m_ComprOffsetInFirstBlock;
				ToCopyCount = BlockSize - CompressionCtx->m_ComprOffsetInFirstBlock;
			}
			else if (Block == LastBlock)
			{
				//last block specific	
				HelperCopySource = TempBuffer;
				ToCopyCount = LeftToCopyCount;
			}
			else
			{
				//standard
				HelperCopySource = TempBuffer;
				ToCopyCount = BlockSize;
			}
			if (!BlockOffsetTable[Block].m_IsZero)
			{
				NT_ASSERT((ULONG)((UserBuffer + ToCopyCount) - (PUCHAR)CompressionCtx->m_UserBuffer) < CompressionCtx->m_UserBufferByteCount);
				NT_ASSERT(ToCopyCount <= BlockSize);
				NT_ASSERT((ULONG)(HelperCopySource - TempBuffer) < BlockSize);
				RtlCopyMemory(UserBuffer, HelperCopySource, ToCopyCount);
			}
			else
			{
				SafeZeroMemory(IrpContext, UserBuffer, ToCopyCount);
			}
		}
	}

	CdDeallocateCompressionBuffer(IrpContext, TempBuffer);

	return STATUS_SUCCESS ;
}

//must be sector alligned (reads 2048 bytes)
__drv_mustHoldCriticalRegion
NTSTATUS CdRawReadFile(
	PIRP_CONTEXT IrpContext, PFCB Fcb, LONGLONG Offset, ULONG Length, __LOCAL_Buffer& Buffer)
{
	NTSTATUS Status;
	//	PIRP Irp = IrpContext->Irp;
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(IrpContext->Irp);

	PIRP IrpRead;
	PIRP_CONTEXT IrpContextRead;
	PIO_STACK_LOCATION IrpSpRead;

	CD_IO_CONTEXT LocalIoContext;

	PAGED_CODE();

	KeInitializeEvent(&LocalIoContext.SyncEvent,
	                  NotificationEvent,
	                  FALSE);

	IrpRead = IoAllocateIrp(IrpContext->Vcb->TargetDeviceObject->StackSize + 1, FALSE);

	if (IrpRead == NULL)
	{
		CdRaiseStatus(IrpContext, STATUS_INSUFFICIENT_RESOURCES);
	}
	IrpRead->UserBuffer = Buffer.Buff;
	IrpRead->MdlAddress = Buffer.MdlBuff;

	IoSetNextIrpStackLocation(IrpRead);
	IrpSpRead = IoGetCurrentIrpStackLocation(IrpRead);
	IrpSpRead->MajorFunction = IRP_MJ_READ;
	IrpSpRead->MinorFunction = IRP_MN_USER_FS_REQUEST;
	IrpSpRead->FileObject = IrpSp->FileObject;
	IrpSpRead->DeviceObject = IrpSp->DeviceObject;

	IrpContextRead = CdCreateIrpContext(IrpRead, TRUE);
	IrpContextRead->IoContext = &LocalIoContext;
	ClearFlag( IrpContextRead->Flags, IRP_CONTEXT_FLAG_ALLOC_IO );

	IrpRead->IoStatus.Information = Length;

	Status = CdNonCachedRead(IrpContextRead, Fcb, Offset, Length);
	//	

	if (!NT_SUCCESS(Status) || IrpRead->IoStatus.Information != Length)
	{
		DbgBreakPoint();

		//
		//  Raise if this is a user induced error.
		//
		if (IoIsErrorUserInduced( Status ))
		{
			CdRaiseStatus( IrpContext, Status );
		}
		Status = FsRtlNormalizeNtstatus(Status, STATUS_UNEXPECTED_IO_ERROR);
	}

	CdCleanupIrpContext(IrpContextRead, FALSE);
	IoFreeIrp(IrpRead);

	return Status;
}

__drv_mustHoldCriticalRegion
INLINE
BOOLEAN
CdParseBuffer(
	PIRP_CONTEXT IrpContext,
	PFCB Fcb,
	PULONG& BlockPointer,
	ULONG RawLength,
	PUCHAR Buff,
	PVECTOR_OF_BLOCK_INFO BlockOffsetTable)
{
	if ((PUCHAR)BlockPointer == Buff)
	{
		BlockOffsetTable->AddItem(BlockOffsetTable->LastItem()->m_AddrEnd, BlockPointer[0]);
		++BlockPointer;
	}

	while (((PUCHAR)BlockPointer - Buff) < (LONGLONG)RawLength)
	{
		BlockOffsetTable->AddItem(BlockPointer[-1], BlockPointer[0]);

		if (BlockPointer[0] == (ULONG)Fcb->FileSizeOnDisk.QuadPart)
		{
			if (!BlockOffsetTable->Compact())
			{
				CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
			}
			return TRUE;
		}

		++BlockPointer;
	}
	return FALSE;
}

__drv_mustHoldCriticalRegion
NTSTATUS CdInitializeFcbBlockOffsetTable(
	PIRP_CONTEXT IrpContext, PIRP Irp, PFCB Fcb, PCCB Ccb)
{
	__LOCAL_Buffer Buffer;
	PZISO_HEADER Header;
	PVECTOR_OF_BLOCK_INFO BlockOffsetTable = NULL;
	LONGLONG RawOffset = 0;
	NTSTATUS Status = STATUS_SUCCESS;
	ULONG SectorsForBlockOffsetTable;
	ULONG RawBlockCount;
	ULONG RawLength;
	PULONG BlockPointer;
	//
	UNREFERENCED_PARAMETER(Irp);
	UNREFERENCED_PARAMETER(Ccb);
	//
	PAGED_CODE();
	//			
	//
	if (Fcb->FileSizeOnDisk.QuadPart < 24)
	{
		return STATUS_FILE_INVALID;
	}
	__try
	{
		__try
		{
			Buffer.Allocate(IrpContext);

			RawLength = (Fcb->AllocationSizeOnDisk.QuadPart < CD_SECTOR_SIZE ?
				             (ULONG)Fcb->AllocationSizeOnDisk.QuadPart :
				             CD_SECTOR_SIZE);

			Buffer.Zero(IrpContext);
			//
			Status = CdRawReadFile(IrpContext, Fcb, RawOffset, RawLength, Buffer);

			if (!NT_SUCCESS( Status ))
			{
				try_return( Status = STATUS_FILE_CORRUPT_ERROR );
			}
			RawOffset += RawLength;
			Header = (PZISO_HEADER)Buffer.Buff;
			if ((RtlCompareMemory(Header->Magic, MAGIC, 8) != 8) ||
				(Header->HeaderSize != (Fcb->HeaderSize >> 2)) ||
				Header->BlockSize != Fcb->BlockSizeLog2 ||
				Fcb->FileSize.QuadPart > MAXULONG ||
				Header->RealSize != (ULONG)Fcb->FileSize.QuadPart)
			{
				try_return( Status = STATUS_FILE_CORRUPT_ERROR );
			}

			BlockPointer = Add2Ptr(Buffer.Buff, sizeof(ZISO_HEADER), PULONG);
			if ((LONGLONG)BlockPointer[0] > Fcb->FileSizeOnDisk.QuadPart)
			{
				try_return( Status = STATUS_FILE_CORRUPT_ERROR );
			}

			SectorsForBlockOffsetTable = (BlockPointer[0] % CD_SECTOR_SIZE ?
				                              BlockPointer[0] / CD_SECTOR_SIZE + 1 :
				                              BlockPointer[0] / CD_SECTOR_SIZE);

			RawBlockCount = (BlockPointer[0] - sizeof(ZISO_HEADER)) >> 2;

			BlockOffsetTable = CdAllocateVectorOfBlockInfo(IrpContext, RawBlockCount - 1);
			if (!BlockOffsetTable)
			{
				Buffer.Deallocate();
				CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
			}

			BlockOffsetTable->AddItem(BlockPointer[0], BlockPointer[1]);
			BlockPointer += 2;
			RawBlockCount -= 2;
			--SectorsForBlockOffsetTable;

			if (BlockPointer[-1] == (ULONG)Fcb->FileSizeOnDisk.QuadPart)
			{
				if (BlockOffsetTable->Compact())
				{
					try_return(Status);
				}
				else
				{
					CdDeallocateVectorOfBlockInfo(&BlockOffsetTable);
					Buffer.Deallocate();
					CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
				}
			}
			else
			{
				NT_ASSERT(RawBlockCount >= 1 );

				while (RawBlockCount-- > 0)
				{
					if (CdParseBuffer(IrpContext, Fcb, BlockPointer, RawLength, Buffer.Buff, BlockOffsetTable))
					{
						try_return(Status);
					}

					RawLength = SectorsForBlockOffsetTable == 1 ? RawBlockCount << 2 : CD_SECTOR_SIZE;

					Buffer.Zero(IrpContext);
					//
					Status = CdRawReadFile(IrpContext, Fcb, RawOffset, RawLength, Buffer);

					if (!NT_SUCCESS( Status ))
					{
						try_return( Status = STATUS_FILE_CORRUPT_ERROR );
					}

					BlockPointer = (PULONG)Buffer.Buff;
					RawOffset += RawLength;
				}
			}

			//
			RETURN_OUT_OF_SEH_SUPPORTED(try_exit: NOTHING);
		}
		__finally
		{
			Buffer.Deallocate();
			if (NT_SUCCESS(Status))
			{
				CdLockFcb(IrpContext, Fcb);
				Fcb->BlockOffsetTable = BlockOffsetTable;
				Fcb->BlockOffsetTableInitiated = TRUE;
				CdUnlockFcb(IrpContext, Fcb);
			}
			else
			{
				CdDeallocateVectorOfBlockInfo(&BlockOffsetTable);
			}
		}
	}
	__except (CdExceptionFilter(IrpContext, GetExceptionInformation()))
	{
		//WTF

		DbgBreakPoint();
		DbgPrint("EXCEPTION");
		Status = (NTSTATUS)GetExceptionCode();
		DbgPrint(" %ui \n", Status);
	}
	//	
	return Status;
}

__drv_mustHoldCriticalRegion
VOID
CdTranslateCompressedReadParams(
	__in PIRP_CONTEXT IrpContext,
	     __in PFCB Fcb,
	     __inout PCOMPRESSION_CONTEXT CompressionCtx,
	     __in ULONG StartingOffset,
	     __in ULONG ByteCount)
{
	UNREFERENCED_PARAMETER(IrpContext);
	ULONG ComprRange;
	ULONG OffsetInFirstBlock;
	ULONG ComprBlockCount = 0;
	ULONG RawStartingOffset;
	ULONG RawByteCount = 0;
	ULONG FirstBlockIndex;
	ULONG AfterLastBlockIndex;
	ULONG AlignedStartingOffset; //	
	ULONG AlignedSize; //
	const VECTOR_OF_BLOCK_INFO& BlockOffsetTable = *Fcb->BlockOffsetTable;

	PAGED_CODE();
	ASSERT(Fcb->BlockOffsetTableInitiated);
	ASSERT(CompressionCtx);

	const ULONG BlockSize = 1 << Fcb->BlockSizeLog2;
	const ULONG BlockSizeMask = BlockSize - 1;
	const ULONG BlockSizeInverseMask = ~BlockSizeMask;

	//#define ComprBlockAlign(L) (                                               \
//	((ULONG)(L) + BlockSizeMask) & BlockSizeInverseMask)              

	//#define LlBlockAlign(V,L) (                                                     \
//	((LONGLONG)(L) + BlockSizeMask) & (LONGLONG)((LONG)BlockSizeInverseMask) )

	//

	OffsetInFirstBlock = StartingOffset & BlockSizeMask;
	ComprRange = StartingOffset + ByteCount;

	ComprBlockCount = ((ByteCount + OffsetInFirstBlock + BlockSizeMask) & BlockSizeInverseMask) >> Fcb->BlockSizeLog2;

	//
	// Parse BlockOffsetTable and calculate raw offset and byte count
	//

	RawByteCount = 0;
	FirstBlockIndex = StartingOffset >> Fcb->BlockSizeLog2;
	AfterLastBlockIndex = FirstBlockIndex + ComprBlockCount;

	for (ULONG Block = FirstBlockIndex; Block < AfterLastBlockIndex; ++Block)
	{
		if (!BlockOffsetTable[Block].m_IsZero)
		{
			RawByteCount += BlockOffsetTable[Block].m_Size;
		}
	}

	AlignedSize = BlockAlign( Fcb->Vcb, RawByteCount );
	AlignedStartingOffset = (BlockOffsetTable[FirstBlockIndex].m_AddrBegin) & ~SECTOR_MASK;
	//	
	if ((AlignedStartingOffset + AlignedSize) > Fcb->AllocationSizeOnDisk.QuadPart)
	{
		AlignedSize = (ULONG)(Fcb->AllocationSizeOnDisk.QuadPart - AlignedStartingOffset);
	}

	RawStartingOffset = BlockOffsetTable[FirstBlockIndex].m_AddrBegin - AlignedStartingOffset; //eroor -> RawStartingOffset -> wzglêdem aligned data -> offset od pocz¹tku

	CompressionCtx->Set(
		RawStartingOffset,
		OffsetInFirstBlock,
		ByteCount,
		ComprBlockCount,
		BlockSize,
		AlignedStartingOffset,
		AlignedSize,
		FirstBlockIndex);	
}

__drv_mustHoldCriticalRegion
VOID
CdComprPrepareBuffer(
	__in PIRP_CONTEXT IrpContext,
	     __inout PIRP Irp,
	     __in ULONG UserBufferByteCount,
	     __inout PCOMPRESSION_CONTEXT CompressionCtx)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER( IrpContext );

	ASSERT_IRP_CONTEXT( IrpContext );
	ASSERT_IRP( Irp );
	ASSERT(CompressionCtx);


	if (!CompressionCtx->AllocateZstream(IrpContext))
	{
		CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
	}

	if (!CompressionCtx->AllocateBuffer())
	{
		CompressionCtx->FreeZstream();
		CdRaiseStatus( IrpContext, STATUS_INSUFFICIENT_RESOURCES );
	}

	if (!Irp->MdlAddress)
	{
		CdCreateUserMdl(IrpContext, UserBufferByteCount, TRUE, IoWriteAccess);
	}

	CompressionCtx->SetUserData(Irp->UserBuffer, Irp->MdlAddress, UserBufferByteCount);
	Irp->MdlAddress = CompressionCtx->m_Mdl;
	Irp->UserBuffer = CompressionCtx->m_Buffer;	
}

__drv_mustHoldCriticalRegion
VOID
CdComprFinishBuffers(
	PIRP_CONTEXT IrpContext,
	PIRP Irp,
	PCOMPRESSION_CONTEXT CompressionCtx)
{
	PAGED_CODE();
	if (CompressionCtx->m_UserBufferByteCount > CompressionCtx->m_ComprByteCount)
	{
		SafeZeroMemory(IrpContext,
			Add2Ptr(CompressionCtx->m_UserBuffer, CompressionCtx->m_ComprByteCount, PVOID),
			CompressionCtx->m_UserBufferByteCount - CompressionCtx->m_ComprByteCount);
	}
	Irp->UserBuffer = CompressionCtx->m_UserBuffer;
	Irp->MdlAddress = CompressionCtx->m_UserMdl;
	Irp->IoStatus.Information = CompressionCtx->m_ComprByteCount;
	CompressionCtx->FreeBuffer();
}
