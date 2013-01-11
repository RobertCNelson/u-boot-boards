/*
 * (C) Copyright 2012 Xilinx
 *
 * This file implements buffer descriptor ring related functions.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "xstatus.h"
//#include "xcache.h"
#include "zynq_gem_hw.h"
#include "zynq_gem_bd.h"
#include "zynq_gem_bdring.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/****************************************************************************
 * Define methods to flush and invalidate cache for BDs should they be
 * located in cached memory. These macros may NOPs if the underlying
 * XENV_BD_CACHE macros are not implemented or they do nothing.
 *
 * @note Assume cache line size is in word length. Please update accordingly
 *       for different system.
 ****************************************************************************/
//#define XENV_BD_CACHE_FLUSH
//#define XENV_BD_CACHE_INVALIDATE

#ifdef XENV_BD_CACHE_FLUSH
#  define XEMACPSS_CACHE_FLUSH(BdPtr)               \
      XCache_FlushDCacheRange((unsigned)BdPtr, XEMACPSS_BD_NUM_WORDS * 4)
#else
#  define XEMACPSS_CACHE_FLUSH(BdPtr)
#endif

#ifdef XENV_BD_CACHE_INVALIDATE
#  define XEMACPSS_CACHE_INVALIDATE(BdPtr)          \
      XCache_InvalidateDCacheRange((unsigned)BdPtr, XEMACPSS_BD_NUM_WORDS * 4)
#else
#  define XEMACPSS_CACHE_INVALIDATE(BdPtr)
#endif

/****************************************************************************
 * Compute the virtual address of a descriptor from its physical address
 *
 * @param BdPtr is the physical address of the BD
 *
 * @returns Virtual address of BdPtr
 *
 * @note Assume BdPtr is always a valid BD in the ring
 ****************************************************************************/
#define XEMACPSS_PHYS_TO_VIRT(BdPtr) \
    ((u32)BdPtr + (RingPtr->BaseBdAddr - RingPtr->PhysBaseAddr))

/****************************************************************************
 * Compute the physical address of a descriptor from its virtual address
 *
 * @param BdPtr is the physical address of the BD
 *
 * @returns Physical address of BdPtr
 *
 * @note Assume BdPtr is always a valid BD in the ring
 ****************************************************************************/
#define XEMACPSS_VIRT_TO_PHYS(BdPtr) \
    ((u32)BdPtr - (RingPtr->BaseBdAddr - RingPtr->PhysBaseAddr))

/****************************************************************************
 * Move the BdPtr argument ahead an arbitrary number of BDs wrapping around
 * to the beginning of the ring if needed.
 *
 * We know if a wrapaound should occur if the new BdPtr is greater than
 * the high address in the ring OR if the new BdPtr crosses over the
 * 0xFFFFFFFF to 0 boundary. The latter test is a valid one since we do not
 * allow a BD space to span this boundary.
 *
 * @param RingPtr is the ring BdPtr appears in
 * @param BdPtr on input is the starting BD position and on output is the
 *        final BD position
 * @param NumBd is the number of BD spaces to increment
 *
 ****************************************************************************/
#define XEMACPSS_RING_SEEKAHEAD(RingPtr, BdPtr, NumBd)                  \
    {                                                                   \
        u32 Addr = (u32)BdPtr;                                  \
                                                                        \
        Addr += ((RingPtr)->Separation * NumBd);                        \
        if ((Addr > (RingPtr)->HighBdAddr) || ((u32)BdPtr > Addr))  \
        {                                                               \
            Addr -= (RingPtr)->Length;                                  \
        }                                                               \
                                                                        \
        BdPtr = (XEmacPss_Bd*)Addr;                                     \
    }

/****************************************************************************
 * Move the BdPtr argument backwards an arbitrary number of BDs wrapping
 * around to the end of the ring if needed.
 *
 * We know if a wrapaound should occur if the new BdPtr is less than
 * the base address in the ring OR if the new BdPtr crosses over the
 * 0xFFFFFFFF to 0 boundary. The latter test is a valid one since we do not
 * allow a BD space to span this boundary.
 *
 * @param RingPtr is the ring BdPtr appears in
 * @param BdPtr on input is the starting BD position and on output is the
 *        final BD position
 * @param NumBd is the number of BD spaces to increment
 *
 ****************************************************************************/
#define XEMACPSS_RING_SEEKBACK(RingPtr, BdPtr, NumBd)                   \
    {                                                                   \
        u32 Addr = (u32)BdPtr;                                  \
                                                                        \
        Addr -= ((RingPtr)->Separation * NumBd);                        \
        if ((Addr < (RingPtr)->BaseBdAddr) || ((u32)BdPtr < Addr))  \
        {                                                               \
            Addr += (RingPtr)->Length;                                  \
        }                                                               \
                                                                        \
        BdPtr = (XEmacPss_Bd*)Addr;                                     \
    }


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/

/*****************************************************************************/
/**
 * Using a memory segment allocated by the caller, create and setup the BD list
 * for the given DMA channel.
 *
 * @param RingPtr is the instance to be worked on.
 * @param PhysAddr is the physical base address of user memory region.
 * @param VirtAddr is the virtual base address of the user memory region. If
 *        address translation is not being utilized, then VirtAddr should be
 *        equivalent to PhysAddr.
 * @param Alignment governs the byte alignment of individual BDs. This function
 *        will enforce a minimum alignment of 4 bytes with no maximum as long
 *        as it is specified as a power of 2.
 * @param BdCount is the number of BDs to setup in the user memory region. It
 *        is assumed the region is large enough to contain the BDs.
 *
 * @return
 *
 * - XST_SUCCESS if initialization was successful
 * - XST_NO_FEATURE if the provided instance is a non DMA type
 *   channel.
 * - XST_INVALID_PARAM under any of the following conditions:
 *   1) PhysAddr and/or VirtAddr are not aligned to the given Alignment
 *      parameter;
 *   2) Alignment parameter does not meet minimum requirements or is not a
 *      power of 2 value;
 *   3) BdCount is 0.
 * - XST_DMA_SG_LIST_ERROR if the memory segment containing the list spans
 *   over address 0x00000000 in virtual address space.
 *
 * @note
 * Make sure to pass in the right alignment value.
 *****************************************************************************/
int XEmacPss_BdRingCreate(XEmacPss_BdRing * RingPtr, u32 PhysAddr,
			  u32 VirtAddr, u32 Alignment, unsigned BdCount)
{
	unsigned i;
	u32 BdVirtAddr;
	u32 BdPhyAddr;

	/* In case there is a failure prior to creating list, make sure the
	 * following attributes are 0 to prevent calls to other functions
	 * from doing anything.
	 */
	RingPtr->AllCnt = 0;
	RingPtr->FreeCnt = 0;
	RingPtr->HwCnt = 0;
	RingPtr->PreCnt = 0;
	RingPtr->PostCnt = 0;

	/* Make sure Alignment parameter meets minimum requirements */
	if (Alignment < XEMACPSS_DMABD_MINIMUM_ALIGNMENT) {
		return (XST_INVALID_PARAM);
	}

	/* Make sure Alignment is a power of 2 */
	if ((Alignment - 1) & Alignment) {
		return (XST_INVALID_PARAM);
	}

	/* Make sure PhysAddr and VirtAddr are on same Alignment */
	if ((PhysAddr % Alignment) || (VirtAddr % Alignment)) {
		return (XST_INVALID_PARAM);
	}

	/* Is BdCount reasonable? */
	if (BdCount == 0) {
		return (XST_INVALID_PARAM);
	}

	/* Figure out how many bytes will be between the start of adjacent BDs */
	RingPtr->Separation =
		(sizeof(XEmacPss_Bd) + (Alignment - 1)) & ~(Alignment - 1);

	/* Must make sure the ring doesn't span address 0x00000000. If it does,
	 * then the next/prev BD traversal macros will fail.
	 */
	if (VirtAddr > (VirtAddr + (RingPtr->Separation * BdCount) - 1)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Initial ring setup:
	 *  - Clear the entire space
	 *  - Setup each BD's BDA field with the physical address of the next BD
	 */
	memset((void *) VirtAddr, 0, (RingPtr->Separation * BdCount));

	BdVirtAddr = VirtAddr;
	BdPhyAddr = PhysAddr + RingPtr->Separation;
	for (i = 1; i < BdCount; i++) {
		XEMACPSS_CACHE_FLUSH(BdVirtAddr);
		BdVirtAddr += RingPtr->Separation;
		BdPhyAddr += RingPtr->Separation;
	}

	/* Setup and initialize pointers and counters */
	RingPtr->RunState = XST_DMA_SG_IS_STOPPED;
	RingPtr->BaseBdAddr = VirtAddr;
	RingPtr->PhysBaseAddr = PhysAddr;
	RingPtr->HighBdAddr = BdVirtAddr;
	RingPtr->Length =
		RingPtr->HighBdAddr - RingPtr->BaseBdAddr + RingPtr->Separation;
	RingPtr->AllCnt = BdCount;
	RingPtr->FreeCnt = BdCount;
	RingPtr->FreeHead = (XEmacPss_Bd *) VirtAddr;
	RingPtr->PreHead = (XEmacPss_Bd *) VirtAddr;
	RingPtr->HwHead = (XEmacPss_Bd *) VirtAddr;
	RingPtr->HwTail = (XEmacPss_Bd *) VirtAddr;
	RingPtr->PostHead = (XEmacPss_Bd *) VirtAddr;
	RingPtr->BdaRestart = (XEmacPss_Bd *) PhysAddr;

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Clone the given BD into every BD in the list.
 * every field of the source BD is replicated in every BD of the list.
 *
 * This function can be called only when all BDs are in the free group such as
 * they are immediately after initialization with XEmacPss_BdRingCreate().
 * This prevents modification of BDs while they are in use by hardware or the
 * user.
 *
 * @param RingPtr is the pointer of BD ring instance to be worked on.
 * @param SrcBdPtr is the source BD template to be cloned into the list. This
 *        BD will be modified.
 *
 * @return
 *   - XST_SUCCESS if the list was modified.
 *   - XST_DMA_SG_NO_LIST if a list has not been created.
 *   - XST_DMA_SG_LIST_ERROR if some of the BDs in this channel are under
 *     hardware or user control.
 *   - XST_DEVICE_IS_STARTED if the DMA channel has not been stopped.
 *
 *****************************************************************************/
int XEmacPss_BdRingClone(XEmacPss_BdRing * RingPtr, XEmacPss_Bd * SrcBdPtr,
			 u8 Direction)
{
	unsigned i;
	u32 CurBd;

	/* Can't do this function if there isn't a ring */
	if (RingPtr->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Can't do this function with the channel running */
	if (RingPtr->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Can't do this function with some of the BDs in use */
	if (RingPtr->FreeCnt != RingPtr->AllCnt) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	if ((Direction != XEMACPSS_SEND) && (Direction != XEMACPSS_RECV)) {
		return (XST_INVALID_PARAM);
	}

	/* Starting from the top of the ring, save BD.Next, overwrite the entire
	 * BD with the template, then restore BD.Next
	 */
	for (i = 0, CurBd = (u32) RingPtr->BaseBdAddr;
	     i < RingPtr->AllCnt; i++, CurBd += RingPtr->Separation) {
		memcpy((void *)CurBd, SrcBdPtr, sizeof(XEmacPss_Bd));
		XEMACPSS_CACHE_FLUSH(CurBd);
	}

	CurBd -= RingPtr->Separation;

	if (Direction == XEMACPSS_RECV) {
		XEmacPss_BdSetRxWrap(CurBd);
	}
	else {
		XEmacPss_BdSetTxWrap(CurBd);
	}

	XEMACPSS_CACHE_FLUSH(CurBd);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Reserve locations in the BD list. The set of returned BDs may be modified
 * in preparation for future DMA transaction(s). Once the BDs are ready to be
 * submitted to hardware, the user must call XEmacPss_BdRingToHw() in the same
 * order which they were allocated here. Example:
 *
 * <pre>
 *        NumBd = 2;
 *        Status = XEmacPss_BdRingAlloc(MyRingPtr, NumBd, &MyBdSet);
 *
 *        if (Status != XST_SUCCESS)
 *        {
 *            // Not enough BDs available for the request
 *        }
 *
 *        CurBd = MyBdSet;
 *        for (i=0; i<NumBd; i++)
 *        {
 *            // Prepare CurBd.....
 *
 *            // Onto next BD
 *            CurBd = XEmacPss_BdRingNext(MyRingPtr, CurBd);
 *        }
 *
 *        // Give list to hardware
 *        Status = XEmacPss_BdRingToHw(MyRingPtr, NumBd, MyBdSet);
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be allocated and given to hardware in the correct sequence:
 * <pre>
 *        // Legal
 *        XEmacPss_BdRingAlloc(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingToHw(MyRingPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XEmacPss_BdRingAlloc(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingAlloc(MyRingPtr, NumBd2, &MySet2);
 *        XEmacPss_BdRingToHw(MyRingPtr, NumBd1, MySet1);
 *        XEmacPss_BdRingToHw(MyRingPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XEmacPss_BdRingAlloc(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingAlloc(MyRingPtr, NumBd2, &MySet2);
 *        XEmacPss_BdRingToHw(MyRingPtr, NumBd2, MySet2);
 *        XEmacPss_BdRingToHw(MyRingPtr, NumBd1, MySet1);
 * </pre>
 *
 * Use the API defined in zynq_gem_bd.h to modify individual BDs. Traversal
 * of the BD set can be done using XEmacPss_BdRingNext() and
 * XEmacPss_BdRingPrev().
 *
 * @param RingPtr is a pointer to the BD ring instance to be worked on.
 * @param NumBd is the number of BDs to allocate
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for modification.
 *
 * @return
 *   - XST_SUCCESS if the requested number of BDs was returned in the BdSetPtr
 *     parameter.
 *   - XST_FAILURE if there were not enough free BDs to satisfy the request.
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 * @note Do not modify more BDs than the number requested with the NumBd
 *       parameter. Doing so will lead to data corruption and system
 *       instability.
 *
 *****************************************************************************/
int XEmacPss_BdRingAlloc(XEmacPss_BdRing * RingPtr, unsigned NumBd,
			 XEmacPss_Bd ** BdSetPtr)
{
	/* Enough free BDs available for the request? */
	if (RingPtr->FreeCnt < NumBd) {
		return (XST_FAILURE);
	}

	/* Set the return argument and move FreeHead forward */
	*BdSetPtr = RingPtr->FreeHead;
	XEMACPSS_RING_SEEKAHEAD(RingPtr, RingPtr->FreeHead, NumBd);
	RingPtr->FreeCnt -= NumBd;
	RingPtr->PreCnt += NumBd;
	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
 * Fully or partially undo an XEmacPss_BdRingAlloc() operation. Use this
 * function if all the BDs allocated by XEmacPss_BdRingAlloc() could not be
 * transferred to hardware with XEmacPss_BdRingToHw().
 *
 * This function helps out in situations when an unrelated error occurs after
 * BDs have been allocated but before they have been given to hardware.
 * An example of this type of error would be an OS running out of resources.
 *
 * This function is not the same as XEmacPss_BdRingFree(). The Free function
 * returns BDs to the free list after they have been processed by hardware,
 * while UnAlloc returns them before being processed by hardware.
 *
 * There are two scenarios where this function can be used. Full UnAlloc or
 * Partial UnAlloc. A Full UnAlloc means all the BDs Alloc'd will be returned:
 *
 * <pre>
 *    Status = XEmacPss_BdRingAlloc(MyRingPtr, 10, &BdPtr);
 *        .
 *        .
 *    if (Error)
 *    {
 *        Status = XEmacPss_BdRingUnAlloc(MyRingPtr, 10, &BdPtr);
 *    }
 * </pre>
 *
 * A partial UnAlloc means some of the BDs Alloc'd will be returned:
 *
 * <pre>
 *    Status = XEmacPss_BdRingAlloc(MyRingPtr, 10, &BdPtr);
 *    BdsLeft = 10;
 *    CurBdPtr = BdPtr;
 *
 *    while (BdsLeft)
 *    {
 *       if (Error)
 *       {
 *          Status = XEmacPss_BdRingUnAlloc(MyRingPtr, BdsLeft, CurBdPtr);
 *       }
 *
 *       CurBdPtr = XEmacPss_BdRingNext(MyRingPtr, CurBdPtr);
 *       BdsLeft--;
 *    }
 * </pre>
 *
 * A partial UnAlloc must include the last BD in the list that was Alloc'd.
 *
 * @param RingPtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs to allocate
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for modification.
 *
 * @return
 *   - XST_SUCCESS if the BDs were unallocated.
 *   - XST_FAILURE if NumBd parameter was greater that the number of BDs in
 *     the preprocessing state.
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XEmacPss_BdRingUnAlloc(XEmacPss_BdRing * RingPtr, unsigned NumBd,
			   XEmacPss_Bd * BdSetPtr)
{
	/* Enough BDs in the free state for the request? */
	if (RingPtr->PreCnt < NumBd) {
		return (XST_FAILURE);
	}

	/* Set the return argument and move FreeHead backward */
	XEMACPSS_RING_SEEKBACK(RingPtr, RingPtr->FreeHead, NumBd);
	RingPtr->FreeCnt += NumBd;
	RingPtr->PreCnt -= NumBd;
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Enqueue a set of BDs to hardware that were previously allocated by
 * XEmacPss_BdRingAlloc(). Once this function returns, the argument BD set goes
 * under hardware control. Any changes made to these BDs after this point will
 * corrupt the BD list leading to data corruption and system instability.
 *
 * The set will be rejected if the last BD of the set does not mark the end of
 * a packet (see XEmacPss_BdSetLast()). 
 *
 * @param RingPtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs in the set.
 * @param BdSetPtr is the first BD of the set to commit to hardware.
 *
 * @return 
 *   - XST_SUCCESS if the set of BDs was accepted and enqueued to hardware.
 *   - XST_FAILURE if the set of BDs was rejected because the last BD of the set
 *     did not have its "last" bit set.
 *   - XST_DMA_SG_LIST_ERROR if this function was called out of sequence with
 *     XEmacPss_BdRingAlloc().
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XEmacPss_BdRingToHw(XEmacPss_BdRing * RingPtr, unsigned NumBd,
			XEmacPss_Bd * BdSetPtr)
{
	XEmacPss_Bd *CurBdPtr;
	unsigned i;

	/* if no bds to process, simply return. */
	if (0 == NumBd)
		return (XST_SUCCESS);

	/* Make sure we are in sync with XEmacPss_BdRingAlloc() */
	if ((RingPtr->PreCnt < NumBd) || (RingPtr->PreHead != BdSetPtr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	CurBdPtr = BdSetPtr;
	for (i = 0; i < NumBd; i++) {
		XEMACPSS_CACHE_FLUSH(CurBdPtr);
		CurBdPtr = XEmacPss_BdRingNext(RingPtr, CurBdPtr);
	}

	/* Adjust ring pointers & counters */
	XEMACPSS_RING_SEEKAHEAD(RingPtr, RingPtr->PreHead, NumBd);
	RingPtr->PreCnt -= NumBd;

	RingPtr->HwTail = CurBdPtr;
	RingPtr->HwCnt += NumBd;

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Returns a set of BD(s) that have been processed by hardware. The returned
 * BDs may be examined to determine the outcome of the DMA transaction(s).
 * Once the BDs have been examined, the user must call XEmacPss_BdRingFree()
 * in the same order which they were retrieved here. Example:
 *
 * <pre>
 *        NumBd = XEmacPss_BdRingFromHwTx(MyRingPtr, MaxBd, &MyBdSet);
 *
 *        if (NumBd == 0)
 *        {
 *           // hardware has nothing ready for us yet
 *        }
 *
 *        CurBd = MyBdSet;
 *        for (i=0; i<NumBd; i++)
 *        {
 *           // Examine CurBd for post processing.....
 *
 *           // Onto next BD
 *           CurBd = XEmacPss_BdRingNext(MyRingPtr, CurBd);
 *           }
 *
 *           XEmacPss_BdRingFree(MyRingPtr, NumBd, MyBdSet); // Return list
 *        }
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be retrieved from hardware and freed in the correct sequence:
 * <pre>
 *        // Legal
 *        XEmacPss_BdRingFromHwTx(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XEmacPss_BdRingFromHwTx(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingFromHwTx(MyRingPtr, NumBd2, &MySet2);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd1, MySet1);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XEmacPss_BdRingFromHwTx(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingFromHwTx(MyRingPtr, NumBd2, &MySet2);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd2, MySet2);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd1, MySet1);
 * </pre>
 *
 * If hardware has only partially completed a packet spanning multiple BDs,
 * then none of the BDs for that packet will be included in the results.
 *
 * @param RingPtr is a pointer to the instance to be worked on.
 * @param BdLimit is the maximum number of BDs to return in the set.
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for examination.
 *
 * @return 
 *   The number of BDs processed by hardware. A value of 0 indicates that no
 *   data is available. No more than BdLimit BDs will be returned.
 *
 * @note Treat BDs returned by this function as read-only.
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
unsigned XEmacPss_BdRingFromHwTx(XEmacPss_BdRing * RingPtr, unsigned BdLimit,
				 XEmacPss_Bd ** BdSetPtr)
{
	XEmacPss_Bd *CurBdPtr;
	u32 BdStr = 0;
	unsigned BdCount;
	unsigned BdPartialCount;

	CurBdPtr = RingPtr->HwHead;
	BdCount = 0;
	BdPartialCount = 0;

	/* If no BDs in work group, then there's nothing to search */
	if (RingPtr->HwCnt == 0) {
		*BdSetPtr = NULL;
		return (0);
	}

	/* Starting at HwHead, keep moving forward in the list until:
	 *  - A BD is encountered with its new/used bit set which means
	 *    hardware has not completed processing of that BD.
	 *  - RingPtr->HwTail is reached and RingPtr->HwCnt is reached.
	 *  - The number of requested BDs has been processed
	 */
	while (BdCount < BdLimit) {
		/* Read the status */
		XEMACPSS_CACHE_INVALIDATE(CurBdPtr);
		BdStr = XEmacPss_BdRead(CurBdPtr, XEMACPSS_BD_STAT_OFFSET);

		BdCount++;

		/* hardware has processed this BD so check the "last" bit. If
                 * it is clear, then there are more BDs for the current packet.
                 * Keep a count of these partial packet BDs.
		 */
		if (BdStr & XEMACPSS_TXBUF_LAST_MASK) {
			BdPartialCount = 0;
		}
                else {
			BdPartialCount++;
                }

		/* Reached the end of the work group */
		if (CurBdPtr == RingPtr->HwTail) {
			break;
		}

		/* Move on to next BD in work group */
		CurBdPtr = XEmacPss_BdRingNext(RingPtr, CurBdPtr);
	}

        /* Subtract off any partial packet BDs found */
        BdCount -= BdPartialCount;

	/* If BdCount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return success
	 */
	if (BdCount > 0) {
		*BdSetPtr = RingPtr->HwHead;
		RingPtr->HwCnt -= BdCount;
		RingPtr->PostCnt += BdCount;
		XEMACPSS_RING_SEEKAHEAD(RingPtr, RingPtr->HwHead, BdCount);
		return (BdCount);
	}
	else {
		*BdSetPtr = NULL;
		return (0);
	}
}


/*****************************************************************************/
/**
 * Returns a set of BD(s) that have been processed by hardware. The returned
 * BDs may be examined to determine the outcome of the DMA transaction(s).
 * Once the BDs have been examined, the user must call XEmacPss_BdRingFree()
 * in the same order which they were retrieved here. Example:
 *
 * <pre>
 *        NumBd = XEmacPss_BdRingFromHwRx(MyRingPtr, MaxBd, &MyBdSet);
 *
 *        if (NumBd == 0)
 *        {
 *           // hardware has nothing ready for us yet
 *        }
 *
 *        CurBd = MyBdSet;
 *        for (i=0; i<NumBd; i++)
 *        {
 *           // Examine CurBd for post processing.....
 *
 *           // Onto next BD
 *           CurBd = XEmacPss_BdRingNext(MyRingPtr, CurBd);
 *           }
 *
 *           XEmacPss_BdRingFree(MyRingPtr, NumBd, MyBdSet); // Return list
 *        }
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be retrieved from hardware and freed in the correct sequence:
 * <pre>
 *        // Legal
 *        XEmacPss_BdRingFromHwRx(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XEmacPss_BdRingFromHwRx(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingFromHwRx(MyRingPtr, NumBd2, &MySet2);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd1, MySet1);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XEmacPss_BdRingFromHwRx(MyRingPtr, NumBd1, &MySet1);
 *        XEmacPss_BdRingFromHwRx(MyRingPtr, NumBd2, &MySet2);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd2, MySet2);
 *        XEmacPss_BdRingFree(MyRingPtr, NumBd1, MySet1);
 * </pre>
 *
 * If hardware has only partially completed a packet spanning multiple BDs,
 * then none of the BDs for that packet will be included in the results.
 *
 * @param RingPtr is a pointer to the instance to be worked on.
 * @param BdLimit is the maximum number of BDs to return in the set.
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for examination.
 *
 * @return 
 *   The number of BDs processed by hardware. A value of 0 indicates that no
 *   data is available. No more than BdLimit BDs will be returned.
 *
 * @note Treat BDs returned by this function as read-only.
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
unsigned XEmacPss_BdRingFromHwRx(XEmacPss_BdRing * RingPtr, unsigned BdLimit,
				 XEmacPss_Bd ** BdSetPtr)
{
	XEmacPss_Bd *CurBdPtr;
	u32 BdStr = 0;
	unsigned BdCount;
	unsigned BdPartialCount;

	CurBdPtr = RingPtr->HwHead;
	BdCount = 0;
        BdPartialCount = 0;

	/* If no BDs in work group, then there's nothing to search */
	if (RingPtr->HwCnt == 0) {
		*BdSetPtr = NULL;
		return (0);
	}

	/* Starting at HwHead, keep moving forward in the list until:
	 *  - A BD is encountered with its new/used bit set which means
	 *    hardware has completed processing of that BD.
	 *  - RingPtr->HwTail is reached and RingPtr->HwCnt is reached.
	 *  - The number of requested BDs has been processed
	 */
	while (BdCount < BdLimit) {
		/* Read the status */
		XEMACPSS_CACHE_INVALIDATE(CurBdPtr);
		BdStr = XEmacPss_BdRead(CurBdPtr, XEMACPSS_BD_STAT_OFFSET);

                if (!(XEmacPss_BdIsRxNew(CurBdPtr))) {
                        break;
                }

		BdCount++;

		/* hardware has processed this BD so check the "last" bit. If
                 * it is clear, then there are more BDs for the current packet.
                 * Keep a count of these partial packet BDs.
		 */
		if (BdStr & XEMACPSS_RXBUF_EOF_MASK) {
			BdPartialCount = 0;
		}
                else {
			BdPartialCount++; 
                }

		/* Reached the end of the work group */
		if (CurBdPtr == RingPtr->HwTail) {
			break;
		}

		/* Move on to next BD in work group */
		CurBdPtr = XEmacPss_BdRingNext(RingPtr, CurBdPtr);
	}

        /* Subtract off any partial packet BDs found */
        BdCount -= BdPartialCount;

	/* If BdCount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return success
	 */
	if (BdCount > 0) {
		*BdSetPtr = RingPtr->HwHead;
		RingPtr->HwCnt -= BdCount;
		RingPtr->PostCnt += BdCount;
		XEMACPSS_RING_SEEKAHEAD(RingPtr, RingPtr->HwHead, BdCount);
		return (BdCount);
	}
	else {
		*BdSetPtr = NULL;
		return (0);
	}
}


/*****************************************************************************/
/**
 * Frees a set of BDs that had been previously retrieved with
 * XEmacPss_BdRingFromHw().
 *
 * @param RingPtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs to free. 
 * @param BdSetPtr is the head of a list of BDs returned by
 * XEmacPss_BdRingFromHw().
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was freed.
 *   - XST_DMA_SG_LIST_ERROR if this function was called out of sequence with
 *     XEmacPss_BdRingFromHw().
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XEmacPss_BdRingFree(XEmacPss_BdRing * RingPtr, unsigned NumBd,
			XEmacPss_Bd * BdSetPtr)
{
	/* if no bds to process, simply return. */
	if (0 == NumBd)
		return (XST_SUCCESS);

	/* Make sure we are in sync with XEmacPss_BdRingFromHw() */
	if ((RingPtr->PostCnt < NumBd) || (RingPtr->PostHead != BdSetPtr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Update pointers and counters */
	RingPtr->FreeCnt += NumBd;
	RingPtr->PostCnt -= NumBd;
	XEMACPSS_RING_SEEKAHEAD(RingPtr, RingPtr->PostHead, NumBd);
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Check the internal data structures of the BD ring for the provided channel.
 * The following checks are made:
 *
 *   - Is the BD ring linked correctly in physical address space.
 *   - Do the internal pointers point to BDs in the ring.
 *   - Do the internal counters add up.
 *
 * The channel should be stopped prior to calling this function.
 *
 * @param RingPtr is a pointer to the instance to be worked on.
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was freed.
 *   - XST_DMA_SG_NO_LIST if the list has not been created.
 *   - XST_IS_STARTED if the channel is not stopped.
 *   - XST_DMA_SG_LIST_ERROR if a problem is found with the internal data
 *     structures. If this value is returned, the channel should be reset to
 *     avoid data corruption or system instability.
 *
 * @note This function should not be preempted by another XEmacPss_Bd function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XEmacPss_BdRingCheck(XEmacPss_BdRing * RingPtr, u8 Direction)
{
	u32 AddrV, AddrP;
	unsigned i;

	if ((Direction != XEMACPSS_SEND) && (Direction != XEMACPSS_RECV)) {
		return (XST_INVALID_PARAM);
	}

	/* Is the list created */
	if (RingPtr->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Can't check if channel is running */
	if (RingPtr->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_IS_STARTED);
	}

	/* RunState doesn't make sense */
	else if (RingPtr->RunState != XST_DMA_SG_IS_STOPPED) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify internal pointers point to correct memory space */
	AddrV = (u32) RingPtr->FreeHead;
	if ((AddrV < RingPtr->BaseBdAddr) || (AddrV > RingPtr->HighBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->PreHead;
	if ((AddrV < RingPtr->BaseBdAddr) || (AddrV > RingPtr->HighBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->HwHead;
	if ((AddrV < RingPtr->BaseBdAddr) || (AddrV > RingPtr->HighBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->HwTail;
	if ((AddrV < RingPtr->BaseBdAddr) || (AddrV > RingPtr->HighBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->PostHead;
	if ((AddrV < RingPtr->BaseBdAddr) || (AddrV > RingPtr->HighBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify internal counters add up */
	if ((RingPtr->HwCnt + RingPtr->PreCnt + RingPtr->FreeCnt +
	     RingPtr->PostCnt) != RingPtr->AllCnt) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify BDs are linked correctly */
	AddrV = RingPtr->BaseBdAddr;
	AddrP = RingPtr->PhysBaseAddr + RingPtr->Separation;

	for (i = 1; i < RingPtr->AllCnt; i++) {
		/* Check BDA for this BD. It should point to next physical addr */
		if (XEmacPss_BdRead(AddrV, XEMACPSS_BD_ADDR_OFFSET) != AddrP) {
			return (XST_DMA_SG_LIST_ERROR);
		}

		/* Move on to next BD */
		AddrV += RingPtr->Separation;
		AddrP += RingPtr->Separation;
	}

	/* Last BD should have wrap bit set */
	if (XEMACPSS_SEND == Direction) {
		if (!XEmacPss_BdIsTxWrap(AddrV)) {
			return (XST_DMA_SG_LIST_ERROR);
		}
	}
	else {			/* XEMACPSS_RECV */
		if (!XEmacPss_BdIsRxWrap(AddrV)) {
			return (XST_DMA_SG_LIST_ERROR);
		}
	}

	/* No problems found */
	return (XST_SUCCESS);
}
