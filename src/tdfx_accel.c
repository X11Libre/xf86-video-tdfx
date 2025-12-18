#include "config.h"

/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"

/* Drivers that need to access the PCI config space directly need this */
#include "xf86Pci.h"

#include "xf86fbman.h"
#include "miline.h"
#include "tdfx.h"

void
TDFXNeedSync(ScrnInfoPtr pScrn) {
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  pTDFX->syncDone=FALSE;
}

void
TDFXFirstSync(ScrnInfoPtr pScrn) {
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  if (!pTDFX->syncDone) {
#ifdef TDFXDRI
    if (pTDFX->directRenderingEnabled) {
      DRILock(xf86ScrnToScreen(pScrn), 0);
      TDFXSwapContextFifo(xf86ScrnToScreen(pScrn));
    }
#endif
    pTDFX->syncDone=TRUE;
    pTDFX->sync(pScrn);
  }
}

void
TDFXCheckSync(ScrnInfoPtr pScrn) {
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  if (pTDFX->syncDone) {
    pTDFX->sync(pScrn);
    pTDFX->syncDone=FALSE;
#ifdef TDFXDRI
    if (pTDFX->directRenderingEnabled) {
      DRIUnlock(xf86ScrnToScreen(pScrn));
    }
#endif
  }
}

void
TDFXSelectBuffer(TDFXPtr pTDFX, int which) {
  int fmt;

  TDFXMakeRoom(pTDFX, 4);
  DECLARE(SSTCP_SRCBASEADDR|SSTCP_DSTBASEADDR|SSTCP_SRCFORMAT|SSTCP_DSTFORMAT);
  switch (which) {
  case TDFX_FRONT:
    if (pTDFX->cpp==1) fmt=pTDFX->stride|(1<<16);
    else fmt=pTDFX->stride|((pTDFX->cpp+1)<<16);
    TDFXWriteLong(pTDFX, SST_2D_DSTBASEADDR, pTDFX->fbOffset);
    TDFXWriteLong(pTDFX, SST_2D_DSTFORMAT, fmt);
    pTDFX->sst2DDstFmtShadow = fmt;
    TDFXWriteLong(pTDFX, SST_2D_SRCBASEADDR, pTDFX->fbOffset);
    TDFXWriteLong(pTDFX, SST_2D_SRCFORMAT, fmt);
    pTDFX->sst2DSrcFmtShadow = fmt;
    break;
  case TDFX_BACK:
    if (pTDFX->cpp==2)
      fmt=((pTDFX->stride+127)/128)|(3<<16); /* Tiled 16bpp */
    else
      fmt=((pTDFX->stride+127)/128)|(5<<16); /* Tiled 32bpp */
    TDFXWriteLong(pTDFX, SST_2D_DSTBASEADDR, pTDFX->backOffset|BIT(31));
    TDFXWriteLong(pTDFX, SST_2D_DSTFORMAT, fmt);
    pTDFX->sst2DDstFmtShadow = fmt;
    TDFXWriteLong(pTDFX, SST_2D_SRCBASEADDR, pTDFX->backOffset|BIT(31));
    TDFXWriteLong(pTDFX, SST_2D_SRCFORMAT, fmt);
    pTDFX->sst2DSrcFmtShadow = fmt;
    break;
  case TDFX_DEPTH:
    if (pTDFX->cpp==2)
      fmt=((pTDFX->stride+127)/128)|(3<<16); /* Tiled 16bpp */
    else
      fmt=((pTDFX->stride+127)/128)|(5<<16); /* Tiled 32bpp */
    TDFXWriteLong(pTDFX, SST_2D_DSTBASEADDR, pTDFX->depthOffset|BIT(31));
    TDFXWriteLong(pTDFX, SST_2D_DSTFORMAT, fmt);
    pTDFX->sst2DDstFmtShadow = fmt;
    TDFXWriteLong(pTDFX, SST_2D_SRCBASEADDR, pTDFX->depthOffset|BIT(31));
    TDFXWriteLong(pTDFX, SST_2D_SRCFORMAT, fmt);
    pTDFX->sst2DSrcFmtShadow = fmt;
    break;
  default:
    ;
  }
}

void
TDFXSetLFBConfig(TDFXPtr pTDFX) {
  if (pTDFX->ChipType<=PCI_CHIP_VOODOO3) {
#if X_BYTE_ORDER == X_BIG_ENDIAN
    unsigned int lfbmode;
    lfbmode=TDFXReadLongMMIO(pTDFX, SST_3D_LFBMODE);

    lfbmode&=~BIT(12); /* 0 bit 12 is byte swizzle */
    lfbmode|=BIT(11); /* 1 bit 11 is word swizzle */
    lfbmode&=~BIT(10); /* 0 bit 10  ARGB or ABGR */
    lfbmode&=~BIT(9); /* 0 bit 9 if bit10 = 0:  ARGB else ABGR */

    TDFXWriteLongMMIO(pTDFX, SST_3D_LFBMODE, lfbmode);
#endif
    TDFXWriteLongMMIO(pTDFX, LFBMEMORYCONFIG, (pTDFX->backOffset>>12) |
		      SST_RAW_LFB_ADDR_STRIDE_4K |
		      ((pTDFX->stride+127)/128)<<SST_RAW_LFB_TILE_STRIDE_SHIFT);
  } else {
    int chip;
    int stride, bits;
    int TileAperturePitch, lg2TileAperturePitch;
    if (pTDFX->cpp==2) stride=pTDFX->stride;
    else stride=4*pTDFX->stride/pTDFX->cpp;
    bits=pTDFX->backOffset>>12;
    for (lg2TileAperturePitch = 0, TileAperturePitch = 1024;
         (lg2TileAperturePitch < 5) &&
             TileAperturePitch < stride;
         lg2TileAperturePitch += 1, TileAperturePitch <<= 1);
    for (chip=0; chip<pTDFX->numChips; chip++) {
      TDFXWriteChipLongMMIO(pTDFX, chip, LFBMEMORYCONFIG, (bits&0x1FFF) |
			    SST_RAW_LFB_ADDR_STRIDE(lg2TileAperturePitch) |
			    ((bits&0x6000)<<10) |
			    ((stride+127)/128)<<SST_RAW_LFB_TILE_STRIDE_SHIFT);
    }
  }
}


Bool
TDFXAccelInit(ScreenPtr pScreen)
{
  return FALSE;
}

static void TDFXMakeRoomNoProp(TDFXPtr pTDFX, int size) {
  int stat;

  pTDFX->PciCnt-=size;
  if (pTDFX->PciCnt<1) {
    do {
      stat=TDFXReadLongMMIO(pTDFX, 0);
      pTDFX->PciCnt=stat&0x1F;
    } while (pTDFX->PciCnt<size);
  }
}

static void TDFXSendNOPNoProp(ScrnInfoPtr pScrn)
{
  TDFXPtr pTDFX;

  pTDFX=TDFXPTR(pScrn);
  TDFXMakeRoomNoProp(pTDFX, 1);
  TDFXWriteLongMMIO(pTDFX, SST_2D_COMMAND, SST_2D_NOP);
}

void TDFXSync(ScrnInfoPtr pScrn)
{
  TDFXPtr pTDFX;
  int i;
  int stat;

  TDFXTRACEACCEL("TDFXSync\n");
  pTDFX=TDFXPTR(pScrn);

  TDFXSendNOPNoProp(pScrn);
  i=0;
  do {
    stat=TDFXReadLongMMIO(pTDFX, 0);
    if (stat&SST_BUSY) i=0; else i++;
  } while (i<3);
  pTDFX->PciCnt=stat&0x1F;
}
