/* zutil.c -- target dependent utility functions for the compression library
 * Copyright (C) 1995-2005, 2010, 2011, 2012 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/* @(#) $Id$ */
#include "../cdprocs.h"
#include "zutil.h"

//
//  The Bug check file id for this module
//

#define BugCheckFileId                   (CDFS_BUG_CHECK_ZUTIL)

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, zcalloc)
#pragma alloc_text(PAGE, zcfree)
#endif


ZEXTERN voidpf zcalloc(voidpf opaque, unsigned items, unsigned size)
{
	UNREFERENCED_PARAMETER(opaque);
	PAGED_CODE();
	__try
	{
		return FsRtlAllocatePoolWithTag(CdPagedPool, items*size, TAG_COMPRESSION_ZLIB);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return NULL;
	}	
}

ZEXTERN void zcfree(voidpf opaque, voidpf ptr)
{
	UNREFERENCED_PARAMETER(opaque);
	PAGED_CODE();
	CdFreePoolWithTag(&ptr, TAG_COMPRESSION_ZLIB);
}
