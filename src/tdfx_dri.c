
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Pci.h"
#include "fb.h"
#include "miline.h"
#include "tdfx.h"
#include "tdfx_dri.h"
#include "tdfx_dripriv.h"

static char TDFXKernelDriverName[] = "tdfx";
static char TDFXClientDriverName[] = "tdfx";

static Bool TDFXCreateContext(ScreenPtr pScreen, VisualPtr visual,
			      drm_context_t hwContext, void *pVisualConfigPriv,
			      DRIContextType contextStore);
static void TDFXDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
			       DRIContextType contextStore);
static void TDFXDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
			       DRIContextType readContextType,
			       void *readContextStore,
			       DRIContextType writeContextType,
			       void *writeContextStore);
static Bool TDFXDRIOpenFullScreen(ScreenPtr pScreen);
static Bool TDFXDRICloseFullScreen(ScreenPtr pScreen);
static void TDFXDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index);
static void TDFXDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
			       RegionPtr prgnSrc, CARD32 index);
static void TDFXDRITransitionTo2d(ScreenPtr pScreen);
static void TDFXDRITransitionTo3d(ScreenPtr pScreen);

static void
TDFXDoWakeupHandler(WAKEUPHANDLER_ARGS_DECL)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  pTDFX->pDRIInfo->wrap.WakeupHandler = pTDFX->coreWakeupHandler;
  (*pTDFX->pDRIInfo->wrap.WakeupHandler) (WAKEUPHANDLER_ARGS);
  pTDFX->pDRIInfo->wrap.WakeupHandler = TDFXDoWakeupHandler;


  TDFXNeedSync(pScrn);
}

static void
TDFXDoBlockHandler(BLOCKHANDLER_ARGS_DECL)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  TDFXCheckSync(pScrn);

  pTDFX->pDRIInfo->wrap.BlockHandler = pTDFX->coreBlockHandler;
  (*pTDFX->pDRIInfo->wrap.BlockHandler) (BLOCKHANDLER_ARGS);
  pTDFX->pDRIInfo->wrap.BlockHandler = TDFXDoBlockHandler;

}

Bool TDFXDRIScreenInit(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  DRIInfoPtr pDRIInfo;
  TDFXDRIPtr pTDFXDRI;
  Bool bppOk = FALSE;

  switch (pScrn->bitsPerPixel) {
  case 16:
    bppOk = TRUE;
    break;
  case 32:
    if (pTDFX->ChipType > PCI_CHIP_VOODOO3) {
      bppOk = TRUE;
    }
    break;
  }
  if (!bppOk) {
    xf86DrvMsg(pScreen->myNum, X_ERROR,
            "[dri] tdfx DRI not supported in %d bpp mode, disabling DRI.\n",
            (pScrn->bitsPerPixel));
    if (pTDFX->ChipType <= PCI_CHIP_VOODOO3) {
      xf86DrvMsg(pScreen->myNum, X_INFO,
              "[dri] To use DRI, invoke the server using 16 bpp\n"
	      "\t(-depth 15 or -depth 16).\n");
    } else {
      xf86DrvMsg(pScreen->myNum, X_INFO,
              "[dri] To use DRI, invoke the server using 16 bpp\n"
	      "\t(-depth 15 or -depth 16) or 32 bpp (-depth 24 -fbbpp 32).\n");
    }
    return FALSE;
  }

    /* Check that the DRI, and DRM modules have been loaded by testing
       for canonical symbols in each module. */
    if (!xf86LoaderCheckSymbol("drmAvailable"))        return FALSE;
    if (!xf86LoaderCheckSymbol("DRIQueryVersion")) {
      xf86DrvMsg(pScreen->myNum, X_ERROR,
                 "TDFXDRIScreenInit failed (libdri.a too old)\n");
      return FALSE;
    }

  /* Check the DRI version */
  {
    int major, minor, patch;
    DRIQueryVersion(&major, &minor, &patch);
    if (major != DRIINFO_MAJOR_VERSION || minor < DRIINFO_MINOR_VERSION) {
      xf86DrvMsg(pScreen->myNum, X_ERROR,
                 "[dri] TDFXDRIScreenInit failed because of a version mismatch.\n"
                 "[dri] libdri version is %d.%d.%d but version %d.%d.x is needed.\n"
                 "[dri] Disabling the DRI.\n",
                 major, minor, patch,
                 DRIINFO_MAJOR_VERSION, DRIINFO_MINOR_VERSION);
      return FALSE;
    }
  }

  pDRIInfo = DRICreateInfoRec();
  if (!pDRIInfo) {
    xf86DrvMsg(pScreen->myNum, X_ERROR,
               "[dri] DRICreateInfoRect() failed, disabling DRI.\n");
    return FALSE;
  }

  pTDFX->pDRIInfo = pDRIInfo;

  pDRIInfo->drmDriverName = TDFXKernelDriverName;
  pDRIInfo->clientDriverName = TDFXClientDriverName;
#ifdef XSERVER_LIBPCIACCESS
    pDRIInfo->busIdString = DRICreatePCIBusID(pTDFX->PciInfo[0]);
#else
  if (xf86LoaderCheckSymbol("DRICreatePCIBusID")) {
    pDRIInfo->busIdString = DRICreatePCIBusID(pTDFX->PciInfo);
  } else {
    pDRIInfo->busIdString = malloc(64);
    snprintf(pDRIInfo->busIdString, 64, "PCI:%d:%d:%d",
	    ((pciConfigPtr)pTDFX->PciInfo->thisCard)->busnum,
	    ((pciConfigPtr)pTDFX->PciInfo->thisCard)->devnum,
	    ((pciConfigPtr)pTDFX->PciInfo->thisCard)->funcnum);
  }
#endif
  pDRIInfo->ddxDriverMajorVersion = TDFX_MAJOR_VERSION;
  pDRIInfo->ddxDriverMinorVersion = TDFX_MINOR_VERSION;
  pDRIInfo->ddxDriverPatchVersion = TDFX_PATCHLEVEL;
  pDRIInfo->frameBufferPhysicalAddress = (pointer) pTDFX->LinearAddr[0];
  pDRIInfo->frameBufferSize = pTDFX->FbMapSize;
  pDRIInfo->frameBufferStride = pTDFX->stride;
  pDRIInfo->ddxDrawableTableEntry = TDFX_MAX_DRAWABLES;

  pTDFX->coreBlockHandler = pDRIInfo->wrap.BlockHandler;
  pDRIInfo->wrap.BlockHandler = TDFXDoBlockHandler;
  pTDFX->coreWakeupHandler = pDRIInfo->wrap.WakeupHandler;
  pDRIInfo->wrap.WakeupHandler = TDFXDoWakeupHandler;

  if (SAREA_MAX_DRAWABLES < TDFX_MAX_DRAWABLES)
    pDRIInfo->maxDrawableTableEntry = SAREA_MAX_DRAWABLES;
  else
    pDRIInfo->maxDrawableTableEntry = TDFX_MAX_DRAWABLES;

  /* For now the mapping works by using a fixed size defined
   * in the SAREA header
   */
  if (sizeof(XF86DRISAREARec)+sizeof(TDFXSAREAPriv)>SAREA_MAX) {
    xf86DrvMsg(pScreen->myNum, X_ERROR, "Data does not fit in SAREA\n");
    return FALSE;
  }
  pDRIInfo->SAREASize = SAREA_MAX;

  if (!(pTDFXDRI = (TDFXDRIPtr)calloc(1, sizeof(TDFXDRIRec)))) {
    xf86DrvMsg(pScreen->myNum, X_ERROR,
               "[dri] DRI memory allocation failed, disabling DRI.\n");
    DRIDestroyInfoRec(pTDFX->pDRIInfo);
    pTDFX->pDRIInfo=0;
    return FALSE;
  }
  pDRIInfo->devPrivate = pTDFXDRI;
  pDRIInfo->devPrivateSize = sizeof(TDFXDRIRec);
  pDRIInfo->contextSize = sizeof(TDFXDRIContextRec);

  pDRIInfo->CreateContext = TDFXCreateContext;
  pDRIInfo->DestroyContext = TDFXDestroyContext;
  pDRIInfo->SwapContext = TDFXDRISwapContext;
  pDRIInfo->InitBuffers = TDFXDRIInitBuffers;
  pDRIInfo->MoveBuffers = TDFXDRIMoveBuffers;
  pDRIInfo->OpenFullScreen = TDFXDRIOpenFullScreen;
  pDRIInfo->CloseFullScreen = TDFXDRICloseFullScreen;
  pDRIInfo->TransitionTo2d = TDFXDRITransitionTo2d;
  pDRIInfo->TransitionTo3d = TDFXDRITransitionTo3d;
  pDRIInfo->bufferRequests = DRI_ALL_WINDOWS;

  pDRIInfo->createDummyCtx = FALSE;
  pDRIInfo->createDummyCtxPriv = FALSE;

  if (!DRIScreenInit(pScreen, pDRIInfo, &pTDFX->drmSubFD)) {
    free(pDRIInfo->devPrivate);
    pDRIInfo->devPrivate=0;
    DRIDestroyInfoRec(pTDFX->pDRIInfo);
    pTDFX->pDRIInfo=0;
    xf86DrvMsg(pScreen->myNum, X_ERROR,
               "[dri] DRIScreenInit failed, disabling DRI.\n");

    return FALSE;
  }

  /* Check the TDFX DRM version */
  {
     drmVersionPtr version = drmGetVersion(pTDFX->drmSubFD);
     if (version) {
        if (version->version_major != 1 ||
            version->version_minor < 0) {
           /* incompatible drm version */
           xf86DrvMsg(pScreen->myNum, X_ERROR,
                      "[dri] TDFXDRIScreenInit failed because of a version mismatch.\n"
                      "[dri] tdfx.o kernel module version is %d.%d.%d but version 1.0.x is needed.\n"
                      "[dri] Disabling the DRI.\n",
                      version->version_major,
                      version->version_minor,
                      version->version_patchlevel);
           TDFXDRICloseScreen(pScreen);
           drmFreeVersion(version);
           return FALSE;
        }
        drmFreeVersion(version);
     }
  }

  pTDFXDRI->regsSize=TDFXIOMAPSIZE;
  if (drmAddMap(pTDFX->drmSubFD, (drm_handle_t)pTDFX->MMIOAddr[0],
		pTDFXDRI->regsSize, DRM_REGISTERS, 0, &pTDFXDRI->regs)<0) {
    TDFXDRICloseScreen(pScreen);
    xf86DrvMsg(pScreen->myNum, X_ERROR, "drmAddMap failed, disabling DRI.\n");
    return FALSE;
  }
  xf86DrvMsg(pScreen->myNum, X_INFO, "[drm] Registers = 0x%08lx\n",
             (unsigned long) pTDFXDRI->regs);

  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "visual configs initialized\n" );

  return TRUE;
}

void
TDFXDRICloseScreen(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  DRICloseScreen(pScreen);

  if (pTDFX->pDRIInfo) {
    if (pTDFX->pDRIInfo->devPrivate) {
      free(pTDFX->pDRIInfo->devPrivate);
      pTDFX->pDRIInfo->devPrivate=0;
    }
    DRIDestroyInfoRec(pTDFX->pDRIInfo);
    pTDFX->pDRIInfo=0;
  }
}

static Bool
TDFXCreateContext(ScreenPtr pScreen, VisualPtr visual,
		  drm_context_t hwContext, void *pVisualConfigPriv,
		  DRIContextType contextStore)
{
  return TRUE;
}

static void
TDFXDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
		   DRIContextType contextStore)
{
}

Bool
TDFXDRIFinishScreenInit(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  TDFXDRIPtr pTDFXDRI;

  pTDFX->pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;

  pTDFXDRI=(TDFXDRIPtr)pTDFX->pDRIInfo->devPrivate;
#ifdef XSERVER_LIBPCIACCESS
  pTDFXDRI->deviceID = DEVICE_ID(pTDFX->PciInfo[0]);
#else
  pTDFXDRI->deviceID = DEVICE_ID(pTDFX->PciInfo);
#endif
  pTDFXDRI->width=pScrn->virtualX;
  pTDFXDRI->height=pScrn->virtualY;
  pTDFXDRI->mem=pScrn->videoRam*1024;
  pTDFXDRI->cpp=pTDFX->cpp;
  pTDFXDRI->stride=pTDFX->stride;
  pTDFXDRI->fifoOffset=pTDFX->fifoOffset;
  pTDFXDRI->fifoSize=pTDFX->fifoSize;
  pTDFXDRI->textureOffset=pTDFX->texOffset;
  pTDFXDRI->textureSize=pTDFX->texSize;
  pTDFXDRI->fbOffset=pTDFX->fbOffset;
  pTDFXDRI->backOffset=pTDFX->backOffset;
  pTDFXDRI->depthOffset=pTDFX->depthOffset;
  pTDFXDRI->sarea_priv_offset = sizeof(XF86DRISAREARec);
  return DRIFinishScreenInit(pScreen);
}

static void
TDFXDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
		   DRIContextType oldContextType, void *oldContext,
		   DRIContextType newContextType, void *newContext)
{
}

static void
TDFXDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index)
{
}

static void
TDFXDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
		   RegionPtr prgnSrc, CARD32 index)
{
}

/*
 * the FullScreen DRI code is dead; this is just left in place to show how
 * to set up SLI mode.
 */
static Bool
TDFXDRIOpenFullScreen(ScreenPtr pScreen)
{
  return TRUE;
}

static Bool
TDFXDRICloseFullScreen(ScreenPtr pScreen)
{
  return TRUE;
}

static void
TDFXDRITransitionTo2d(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  xf86FreeOffscreenArea(pTDFX->reservedArea); 
}

static void
TDFXDRITransitionTo3d(ScreenPtr pScreen)
{
  ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  FBAreaPtr pArea;

  if(pTDFX->overlayBuffer) {
	xf86FreeOffscreenLinear(pTDFX->overlayBuffer);
	pTDFX->overlayBuffer = NULL;
  }

  if(pTDFX->overlayBuffer2) {
	xf86FreeOffscreenLinear(pTDFX->overlayBuffer2);
	pTDFX->overlayBuffer2 = NULL;
  }

  if(pTDFX->textureBuffer) {
	xf86FreeOffscreenArea(pTDFX->textureBuffer);
	pTDFX->textureBuffer = NULL;
  }

  xf86PurgeUnlockedOffscreenAreas(pScreen);
  
  pArea = xf86AllocateOffscreenArea(pScreen, pScrn->displayWidth,
				    pTDFX->pixmapCacheLinesMin,
				    pScrn->displayWidth, NULL, NULL, NULL);
  pTDFX->reservedArea = xf86AllocateOffscreenArea(pScreen, pScrn->displayWidth,
			pTDFX->pixmapCacheLinesMax - pTDFX->pixmapCacheLinesMin,
			pScrn->displayWidth, NULL, NULL, NULL);
  xf86FreeOffscreenArea(pArea);
}
