#ifndef _READCOMPR_
#define _READCOMPR_

#pragma once

#if defined(__cplusplus)
extern "C"
{
#endif

	__drv_mustHoldCriticalRegion
	NTSTATUS
	CdInitializeFcbBlockOffsetTable(
		__in PIRP_CONTEXT IrpContext,
		     __in PIRP Irp,
		     __inout PFCB Fcb,
		     __inout PCCB Ccb);

	__drv_mustHoldCriticalRegion
	NTSTATUS
	CdInflateData(
		__in PIRP_CONTEXT IrpContext,
		     __inout PIRP Irp,
		     __in PFCB Fcb,
		     __in PCOMPRESSION_CONTEXT CompressionCtx);


	__drv_mustHoldCriticalRegion
	VOID
	CdTranslateCompressedReadParams(
		__in PIRP_CONTEXT IrpContext,
		     __in PFCB Fcb,
		     __inout PCOMPRESSION_CONTEXT CompressionCtx,
		     __in ULONG StartingOffset,
		     __in ULONG ByteCount);

	//__drv_mustHoldCriticalRegion
	//	NTSTATUS
	//	CdCompressionAdjustBuffer(
	//	PIRP_CONTEXT IrpContext,
	//	PIRP Irp,
	//	PFCB Fcb,
	//	PCCB Ccb );

	__drv_mustHoldCriticalRegion
	VOID
	CdComprPrepareBuffer(
		PIRP_CONTEXT IrpContext,
		PIRP Irp,
		ULONG UserBufferByteCount,
		PCOMPRESSION_CONTEXT CompressionCtx);

	__drv_mustHoldCriticalRegion
	VOID
	CdComprFinishBuffers(
		PIRP_CONTEXT IrpContext,
		PIRP Irp,
		PCOMPRESSION_CONTEXT CompressionCtx);

#if defined(__cplusplus)
}
#endif

#endif
