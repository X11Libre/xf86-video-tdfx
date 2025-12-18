#include "config.h"

#include "xf86.h"
#include "xf86_OSproc.h"
#include "dgaproc.h"

#include "tdfx.h"
#include "vgaHW.h"

static Bool TDFX_OpenFramebuffer(ScrnInfoPtr, char **, unsigned char **,
				 int *, int *, int *);
static Bool TDFX_SetMode(ScrnInfoPtr, DGAModePtr);
static int  TDFX_GetViewport(ScrnInfoPtr);
static void TDFX_SetViewport(ScrnInfoPtr, int, int, int);

static
DGAFunctionRec TDFX_DGAFuncs = {
  TDFX_OpenFramebuffer,
  0,
  TDFX_SetMode,
  TDFX_SetViewport,
  TDFX_GetViewport,
  TDFXSync,
  NULL, NULL, NULL
};


Bool
TDFXDGAInit(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX;
  DisplayModePtr pMode, firstMode;
  DGAModePtr modes=0, newmodes=0, currentMode;
  int num=0;

  pTDFX = TDFXPTR(pScrn);
  pMode = firstMode = pScrn->modes;

  while (pMode) {
    newmodes = realloc(modes, (num+1)*sizeof(DGAModeRec));

    if (!newmodes) {
      free(modes);
      return FALSE;
    }
    modes = newmodes;

    currentMode = modes+num;
    num++;
    currentMode->mode = pMode;
    currentMode->flags = DGA_CONCURRENT_ACCESS | DGA_PIXMAP_AVAILABLE;
    if (pMode->Flags & V_DBLSCAN)
      currentMode->flags |= DGA_DOUBLESCAN;
    if (pMode->Flags & V_INTERLACE)
      currentMode->flags |= DGA_INTERLACED;
    currentMode->byteOrder = pScrn->imageByteOrder;
    currentMode->depth = pScrn->depth;
    currentMode->bitsPerPixel = pScrn->bitsPerPixel;
    currentMode->red_mask = pScrn->mask.red;
    currentMode->green_mask = pScrn->mask.green;
    currentMode->blue_mask = pScrn->mask.blue;
    currentMode->visualClass = pScrn->defaultVisual;
    currentMode->viewportWidth = pMode->HDisplay;
    currentMode->viewportHeight = pMode->VDisplay;
    currentMode->xViewportStep = 1;
    currentMode->yViewportStep = 1;
    currentMode->viewportFlags = DGA_FLIP_RETRACE;
    currentMode->offset = 0;
    currentMode->address = pTDFX->FbBase;
    currentMode->bytesPerScanline = ((pScrn->displayWidth*pTDFX->cpp)+3) & ~3L;
    currentMode->imageWidth = pScrn->displayWidth;
    currentMode->imageHeight =  pTDFX->pixmapCacheLinesMax;
    currentMode->pixmapWidth = currentMode->imageWidth;
    currentMode->pixmapHeight = currentMode->imageHeight;
    currentMode->maxViewportX = currentMode->imageWidth -
                                currentMode->viewportWidth;
    /* this might need to get clamped to some maximum */
    currentMode->maxViewportY = currentMode->imageHeight -
                                currentMode->viewportHeight;

    pMode = pMode->next;
    if (pMode == firstMode) break;
  }

  pTDFX->DGAModes = modes;

  return DGAInit(pScreen, &TDFX_DGAFuncs, modes, num);
}

static Bool
TDFX_SetMode(ScrnInfoPtr pScrn, DGAModePtr pMode)
{
   static DisplayModePtr OldModes[MAXSCREENS];
   int index = pScrn->pScreen->myNum;

   TDFXPtr pTDFX = TDFXPTR(pScrn);

   if (!pMode) { /* restore the original mode */
     /* put the ScreenParameters back */
     if(pTDFX->DGAactive) {
	TDFXSwitchMode(pScrn, OldModes[index]);
	TDFXAdjustFrame(pScrn, 0, 0);
	pTDFX->DGAactive = FALSE;
     }
   } else {
     if (!pTDFX->DGAactive) {  /* save the old parameters */
        OldModes[index] = pScrn->currentMode;
        pTDFX->DGAactive = TRUE;
     }

     TDFXSwitchMode(pScrn, pMode->mode);
   }

   return TRUE;
}

static int
TDFX_GetViewport(ScrnInfoPtr pScrn)
{
    TDFXPtr pTDFX = TDFXPTR(pScrn);

    return pTDFX->DGAViewportStatus;
}

static void
TDFX_SetViewport(ScrnInfoPtr pScrn, int x, int y, int flags)
{
   TDFXPtr pTDFX = TDFXPTR(pScrn);
   vgaHWPtr hwp = VGAHWPTR(pScrn);

   TDFXAdjustFrame(pScrn, x, y);

   /* fixme */
   while(hwp->readST01(hwp) & 0x08);
   while(!(hwp->readST01(hwp) & 0x08));

   pTDFX->DGAViewportStatus = 0;
}

static Bool
TDFX_OpenFramebuffer(
   ScrnInfoPtr pScrn,
   char **name,
   unsigned char **mem,
   int *size,
   int *offset,
   int *flags
){
    TDFXPtr pTDFX = TDFXPTR(pScrn);

    *name = NULL; 		/* no special device */
    *mem = (unsigned char*)pTDFX->LinearAddr[0] + pTDFX->fbOffset;
    *size = pTDFX->FbMapSize;
    *offset = /* pTDFX->fbOffset */ 0 ;  /* DGA is broken */
    *flags = DGA_NEED_ROOT;

    return TRUE;
}
