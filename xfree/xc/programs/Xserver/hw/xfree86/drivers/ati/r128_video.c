/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/r128_video.c,v 1.18 2001/03/03 22:26:10 tsi Exp $ */

#include "r128.h"
#include "r128_reg.h"

#include "xf86.h"
#include "dixstruct.h"

#include "Xv.h"
#include "fourcc.h"

#define OFF_DELAY       250  /* milliseconds */
#define FREE_DELAY      15000

#define OFF_TIMER       0x01
#define FREE_TIMER      0x02
#define CLIENT_VIDEO_ON 0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#ifndef XvExtension
void R128InitVideo(ScreenPtr pScreen) {}
#else

static XF86VideoAdaptorPtr R128SetupImageVideo(ScreenPtr);
static int  R128SetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int  R128GetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void R128StopVideo(ScrnInfoPtr, pointer, Bool);
static void R128QueryBestSize(ScrnInfoPtr, Bool, short, short, short, short,
			unsigned int *, unsigned int *, pointer);
static int  R128PutImage(ScrnInfoPtr, short, short, short, short, short,
			short, short, short, int, unsigned char*, short,
			short, Bool, RegionPtr, pointer);
static int  R128QueryImageAttributes(ScrnInfoPtr, int, unsigned short *,
			unsigned short *,  int *, int *);


static void R128ResetVideo(ScrnInfoPtr);

static void R128VideoTimerCallback(ScrnInfoPtr pScrn, Time now);


#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvColorKey, xvSaturation, xvDoubleBuffer;


typedef struct {
   int           brightness;
   int           saturation;
   Bool          doubleBuffer;
   unsigned char currentBuffer;
   FBLinearPtr   linear;
   RegionRec     clip;
   CARD32        colorKey;
   CARD32        videoStatus;
   Time          offTime;
   Time          freeTime;
} R128PortPrivRec, *R128PortPrivPtr;


void R128InitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    R128InfoPtr info  = R128PTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    if(info->accel && info->accel->FillSolidRects)
	newAdaptor = R128SetupImageVideo(pScreen);

    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor) {
	if(!num_adaptors) {
	    num_adaptors = 1;
	    adaptors = &newAdaptor;
	} else {
	    newAdaptors =  /* need to free this someplace */
		xalloc((num_adaptors + 1) * sizeof(XF86VideoAdaptorPtr*));
	    if(newAdaptors) {
		memcpy(newAdaptors, adaptors, num_adaptors *
					sizeof(XF86VideoAdaptorPtr));
		newAdaptors[num_adaptors] = newAdaptor;
		adaptors = newAdaptors;
		num_adaptors++;
	    }
	}
    }

    if(num_adaptors)
	xf86XVScreenInit(pScreen, adaptors, num_adaptors);

    if(newAdaptors)
	xfree(newAdaptors);
}

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{
   0,
   "XV_IMAGE",
   2048, 2048,
   {1, 1}
};

#define NUM_FORMATS 12

static XF86VideoFormatRec Formats[NUM_FORMATS] =
{
   {8, TrueColor}, {8, DirectColor}, {8, PseudoColor},
   {8, GrayScale}, {8, StaticGray}, {8, StaticColor},
   {15, TrueColor}, {16, TrueColor}, {24, TrueColor},
   {15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};


#define NUM_ATTRIBUTES 4

static XF86AttributeRec Attributes[NUM_ATTRIBUTES] =
{
   {XvSettable | XvGettable, 0, (1 << 24) - 1, "XV_COLORKEY"},
   {XvSettable | XvGettable, -64, 63, "XV_BRIGHTNESS"},
   {XvSettable | XvGettable, 0, 31, "XV_SATURATION"},
   {XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"}
};

#define NUM_IMAGES 4

static XF86ImageRec Images[NUM_IMAGES] =
{
	XVIMAGE_YUY2,
	XVIMAGE_UYVY,
	XVIMAGE_YV12,
	XVIMAGE_I420
};

static void
R128ResetVideo(ScrnInfoPtr pScrn)
{
    R128InfoPtr   info      = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    R128PortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;


    OUTREG(R128_OV0_SCALE_CNTL, 0x80000000);
    OUTREG(R128_OV0_EXCLUSIVE_HORZ, 0);
    OUTREG(R128_OV0_AUTO_FLIP_CNTL, 0);   /* maybe */
    OUTREG(R128_OV0_FILTER_CNTL, 0x0000000f);
    OUTREG(R128_OV0_COLOUR_CNTL, (pPriv->brightness & 0x7f) |
				 (pPriv->saturation << 8) |
				 (pPriv->saturation << 16));
    OUTREG(R128_OV0_GRAPHICS_KEY_MSK, (1 << pScrn->depth) - 1);
    OUTREG(R128_OV0_GRAPHICS_KEY_CLR, pPriv->colorKey);
    OUTREG(R128_OV0_KEY_CNTL, R128_GRAPHIC_KEY_FN_NE);
    OUTREG(R128_OV0_TEST, 0);
}


static XF86VideoAdaptorPtr
R128AllocAdaptor(ScrnInfoPtr pScrn)
{
    XF86VideoAdaptorPtr adapt;
    R128InfoPtr info = R128PTR(pScrn);
    R128PortPrivPtr pPriv;

    if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
	return NULL;

    if(!(pPriv = xcalloc(1, sizeof(R128PortPrivRec) + sizeof(DevUnion))))
    {
	xfree(adapt);
	return NULL;
    }

    adapt->pPortPrivates = (DevUnion*)(&pPriv[1]);
    adapt->pPortPrivates[0].ptr = (pointer)pPriv;

    xvBrightness   = MAKE_ATOM("XV_BRIGHTNESS");
    xvSaturation   = MAKE_ATOM("XV_SATURATION");
    xvColorKey     = MAKE_ATOM("XV_COLORKEY");
    xvDoubleBuffer = MAKE_ATOM("XV_DOUBLE_BUFFER");

    pPriv->colorKey = info->videoKey;
    pPriv->doubleBuffer = TRUE;
    pPriv->videoStatus = 0;
    pPriv->brightness = 0;
    pPriv->saturation = 16;
    pPriv->currentBuffer = 0;

    return adapt;
}

static XF86VideoAdaptorPtr
R128SetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    R128InfoPtr info = R128PTR(pScrn);
    R128PortPrivPtr pPriv;
    XF86VideoAdaptorPtr adapt;

    if(!(adapt = R128AllocAdaptor(pScrn)))
	return NULL;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
    adapt->name = "ATI Rage128 Video Overlay";
    adapt->nEncodings = 1;
    adapt->pEncodings = &DummyEncoding;
    adapt->nFormats = NUM_FORMATS;
    adapt->pFormats = Formats;
    adapt->nPorts = 1;
    adapt->nAttributes = NUM_ATTRIBUTES;
    adapt->pAttributes = Attributes;
    adapt->nImages = NUM_IMAGES;
    adapt->pImages = Images;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = R128StopVideo;
    adapt->SetPortAttribute = R128SetPortAttribute;
    adapt->GetPortAttribute = R128GetPortAttribute;
    adapt->QueryBestSize = R128QueryBestSize;
    adapt->PutImage = R128PutImage;
    adapt->QueryImageAttributes = R128QueryImageAttributes;

    info->adaptor = adapt;

    pPriv = (R128PortPrivPtr)(adapt->pPortPrivates[0].ptr);
    REGION_INIT(pScreen, &(pPriv->clip), NullBox, 0);

    R128ResetVideo(pScrn);

    return adapt;
}

/* I really should stick this in miregion */
static Bool
RegionsEqual(RegionPtr A, RegionPtr B)
{
    int *dataA, *dataB;
    int num;

    num = REGION_NUM_RECTS(A);
    if(num != REGION_NUM_RECTS(B))
	return FALSE;

    if((A->extents.x1 != B->extents.x1) ||
       (A->extents.x2 != B->extents.x2) ||
       (A->extents.y1 != B->extents.y1) ||
       (A->extents.y2 != B->extents.y2))
	return FALSE;

    dataA = (pointer)REGION_RECTS(A);
    dataB = (pointer)REGION_RECTS(B);

    while(num--) {
	if((dataA[0] != dataB[0]) || (dataA[1] != dataB[1]))
	   return FALSE;
	dataA += 2;
	dataB += 2;
    }

    return TRUE;
}


/* R128ClipVideo -

   Takes the dst box in standard X BoxRec form (top and left
   edges inclusive, bottom and right exclusive).  The new dst
   box is returned.  The source boundaries are given (xa, ya
   inclusive, xb, yb exclusive) and returned are the new source
   boundaries in 16.16 fixed point.
*/

#define DummyScreen screenInfo.screens[0]

static Bool
R128ClipVideo(
  BoxPtr dst,
  INT32 *xa,
  INT32 *xb,
  INT32 *ya,
  INT32 *yb,
  RegionPtr reg,
  INT32 width,
  INT32 height
){
    INT32 vscale, hscale, delta;
    BoxPtr extents = REGION_EXTENTS(DummyScreen, reg);
    int diff;

    hscale = ((*xb - *xa) << 16) / (dst->x2 - dst->x1);
    vscale = ((*yb - *ya) << 16) / (dst->y2 - dst->y1);

    *xa <<= 16; *xb <<= 16;
    *ya <<= 16; *yb <<= 16;

    diff = extents->x1 - dst->x1;
    if(diff > 0) {
	dst->x1 = extents->x1;
	*xa += diff * hscale;
    }
    diff = dst->x2 - extents->x2;
    if(diff > 0) {
	dst->x2 = extents->x2;
	*xb -= diff * hscale;
    }
    diff = extents->y1 - dst->y1;
    if(diff > 0) {
	dst->y1 = extents->y1;
	*ya += diff * vscale;
    }
    diff = dst->y2 - extents->y2;
    if(diff > 0) {
	dst->y2 = extents->y2;
	*yb -= diff * vscale;
    }

    if(*xa < 0) {
	diff =  (- *xa + hscale - 1)/ hscale;
	dst->x1 += diff;
	*xa += diff * hscale;
    }
    delta = *xb - (width << 16);
    if(delta > 0) {
	diff = (delta + hscale - 1)/ hscale;
	dst->x2 -= diff;
	*xb -= diff * hscale;
    }
    if(*xa >= *xb) return FALSE;

    if(*ya < 0) {
	diff =  (- *ya + vscale - 1)/ vscale;
	dst->y1 += diff;
	*ya += diff * vscale;
    }
    delta = *yb - (height << 16);
    if(delta > 0) {
	diff = (delta + vscale - 1)/ vscale;
	dst->y2 -= diff;
	*yb -= diff * vscale;
    }
    if(*ya >= *yb) return FALSE;

    if((dst->x1 != extents->x1) || (dst->x2 != extents->x2) ||
       (dst->y1 != extents->y1) || (dst->y2 != extents->y2))
    {
	RegionRec clipReg;
	REGION_INIT(DummyScreen, &clipReg, dst, 1);
	REGION_INTERSECT(DummyScreen, reg, reg, &clipReg);
	REGION_UNINIT(DummyScreen, &clipReg);
    }
    return TRUE;
}

static void
R128StopVideo(ScrnInfoPtr pScrn, pointer data, Bool cleanup)
{
  R128InfoPtr info = R128PTR(pScrn);
  unsigned char *R128MMIO = info->MMIO;
  R128PortPrivPtr pPriv = (R128PortPrivPtr)data;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if(cleanup) {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
	OUTREG(R128_OV0_SCALE_CNTL, 0);
     }
     if(pPriv->linear) {
	xf86FreeOffscreenLinear(pPriv->linear);
	pPriv->linear = NULL;
     }
     pPriv->videoStatus = 0;
  } else {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
	pPriv->videoStatus |= OFF_TIMER;
	pPriv->offTime = currentTime.milliseconds + OFF_DELAY;
     }
  }
}

static int
R128SetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 value,
  pointer data
){
  R128InfoPtr info = R128PTR(pScrn);
  unsigned char *R128MMIO = info->MMIO;
  R128PortPrivPtr pPriv = (R128PortPrivPtr)data;

  if(attribute == xvBrightness) {
	if((value < -64) || (value > 63))
	   return BadValue;
	pPriv->brightness = value;

	OUTREG(R128_OV0_COLOUR_CNTL, (pPriv->brightness & 0x7f) |
				     (pPriv->saturation << 8) |
				     (pPriv->saturation << 16));
  } else
  if(attribute == xvSaturation) {
	if((value < 0) || (value > 31))
	   return BadValue;
	pPriv->saturation = value;

	OUTREG(R128_OV0_COLOUR_CNTL, (pPriv->brightness & 0x7f) |
				     (pPriv->saturation << 8) |
				     (pPriv->saturation << 16));
  } else
  if(attribute == xvDoubleBuffer) {
	if((value < 0) || (value > 1))
	   return BadValue;
	pPriv->doubleBuffer = value;
  } else
  if(attribute == xvColorKey) {
	pPriv->colorKey = value;
	OUTREG(R128_OV0_GRAPHICS_KEY_CLR, pPriv->colorKey);

	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else return BadMatch;

  return Success;
}

static int
R128GetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 *value,
  pointer data
){
  R128PortPrivPtr pPriv = (R128PortPrivPtr)data;

  if(attribute == xvBrightness) {
	*value = pPriv->brightness;
  } else
  if(attribute == xvSaturation) {
	*value = pPriv->saturation;
  } else
  if(attribute == xvDoubleBuffer) {
	*value = pPriv->doubleBuffer ? 1 : 0;
  } else
  if(attribute == xvColorKey) {
	*value = pPriv->colorKey;
  } else return BadMatch;

  return Success;
}


static void
R128QueryBestSize(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  pointer data
){
   if(vid_w > (drw_w << 4))
	drw_w = vid_w >> 4;
   if(vid_h > (drw_h << 4))
	drw_h = vid_h >> 4;

  *p_w = drw_w;
  *p_h = drw_h;
}


static void
R128CopyData422(
  unsigned char *src,
  unsigned char *dst,
  int srcPitch,
  int dstPitch,
  int h,
  int w
){
    w <<= 1;
    while(h--) {
	memcpy(dst, src, w);
	src += srcPitch;
	dst += dstPitch;
    }
}

static void
R128CopyData420(
   unsigned char *src1,
   unsigned char *src2,
   unsigned char *src3,
   unsigned char *dst1,
   unsigned char *dst2,
   unsigned char *dst3,
   int srcPitch,
   int srcPitch2,
   int dstPitch,
   int h,
   int w
){
   int count;

   count = h;
   while(count--) {
	memcpy(dst1, src1, w);
	src1 += srcPitch;
	dst1 += dstPitch;
   }

   w >>= 1;
   h >>= 1;
   dstPitch >>= 1;

   count = h;
   while(count--) {
	memcpy(dst2, src2, w);
	src2 += srcPitch2;
	dst2 += dstPitch;
   }

   count = h;
   while(count--) {
	memcpy(dst3, src3, w);
	src3 += srcPitch2;
	dst3 += dstPitch;
   }
}


static FBLinearPtr
R128AllocateMemory(
   ScrnInfoPtr pScrn,
   FBLinearPtr linear,
   int size
){
   ScreenPtr pScreen;
   FBLinearPtr new_linear;

   if(linear) {
	if(linear->size >= size)
	   return linear;

	if(xf86ResizeOffscreenLinear(linear, size))
	   return linear;

	xf86FreeOffscreenLinear(linear);
   }

   pScreen = screenInfo.screens[pScrn->scrnIndex];

   new_linear = xf86AllocateOffscreenLinear(pScreen, size, 16,
						NULL, NULL, NULL);

   if(!new_linear) {
	int max_size;

	xf86QueryLargestOffscreenLinear(pScreen, &max_size, 16,
						PRIORITY_EXTREME);

	if(max_size < size)
	   return NULL;

	xf86PurgeUnlockedOffscreenAreas(pScreen);
	new_linear = xf86AllocateOffscreenLinear(pScreen, size, 16,
						NULL, NULL, NULL);
   }

   return new_linear;
}

static void
R128DisplayVideo422(
    ScrnInfoPtr pScrn,
    int id,
    int offset,
    short width, short height,
    int pitch,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    int v_inc, h_inc, step_by, tmp;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init;

    v_inc = (src_h << 20) / drw_h;
    h_inc = (src_w << 12) / drw_w;
    step_by = 1;

    while(h_inc >= (2 << 12)) {
	step_by++;
	h_inc >>= 1;
    }

    /* keep everything in 16.16 */

    offset += ((left >> 16) & ~7) << 1;

    tmp = (left & 0x0003ffff) + 0x00028000 + (h_inc << 3);
    p1_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		      ((tmp << 12) & 0xf0000000);

    tmp = ((left >> 1) & 0x0001ffff) + 0x00028000 + (h_inc << 2);
    p23_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		       ((tmp << 12) & 0x70000000);

    tmp = (top & 0x0000ffff) + 0x00018000;
    p1_v_accum_init = ((tmp << 4) & 0x03ff8000) | 0x00000001;

    left = (left >> 16) & 7;

    OUTREG(R128_OV0_REG_LOAD_CNTL, 1);
    while(!(INREG(R128_OV0_REG_LOAD_CNTL) & (1 << 3)));

    OUTREG(R128_OV0_H_INC, h_inc | ((h_inc >> 1) << 16));
    OUTREG(R128_OV0_STEP_BY, step_by | (step_by << 8));
    OUTREG(R128_OV0_Y_X_START, dstBox->x1 | (dstBox->y1 << 16));
    OUTREG(R128_OV0_Y_X_END,   dstBox->x2 | (dstBox->y2 << 16));
    OUTREG(R128_OV0_V_INC, v_inc);
    OUTREG(R128_OV0_P1_BLANK_LINES_AT_TOP, 0x00000fff | ((src_h - 1) << 16));
    OUTREG(R128_OV0_VID_BUF_PITCH0_VALUE, pitch);
    OUTREG(R128_OV0_P1_X_START_END, (width - 1) | (left << 16));
    left >>= 1; width >>= 1;
    OUTREG(R128_OV0_P2_X_START_END, (width - 1) | (left << 16));
    OUTREG(R128_OV0_P3_X_START_END, (width - 1) | (left << 16));
    OUTREG(R128_OV0_VID_BUF0_BASE_ADRS, offset & 0xfffffff0);
    OUTREG(R128_OV0_P1_V_ACCUM_INIT, p1_v_accum_init);
    OUTREG(R128_OV0_P23_V_ACCUM_INIT, 0);
    OUTREG(R128_OV0_P1_H_ACCUM_INIT, p1_h_accum_init);
    OUTREG(R128_OV0_P23_H_ACCUM_INIT, p23_h_accum_init);

    if(id == FOURCC_UYVY)
       OUTREG(R128_OV0_SCALE_CNTL, 0x41FF8C03);
    else
       OUTREG(R128_OV0_SCALE_CNTL, 0x41FF8B03);

    OUTREG(R128_OV0_REG_LOAD_CNTL, 0);
}

static void
R128DisplayVideo420(
    ScrnInfoPtr pScrn,
    short width, short height,
    int pitch,
    int offset1, int offset2, int offset3,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    int v_inc, h_inc, step_by, tmp, leftUV;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init, p23_v_accum_init;

    v_inc = (src_h << 20) / drw_h;
    h_inc = (src_w << 12) / drw_w;
    step_by = 1;

    while(h_inc >= (2 << 12)) {
	step_by++;
	h_inc >>= 1;
    }

    /* keep everything in 16.16 */

    offset1 += (left >> 16) & ~15;
    offset2 += (left >> 17) & ~15;
    offset3 += (left >> 17) & ~15;

    tmp = (left & 0x0003ffff) + 0x00028000 + (h_inc << 3);
    p1_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		      ((tmp << 12) & 0xf0000000);

    tmp = ((left >> 1) & 0x0001ffff) + 0x00028000 + (h_inc << 2);
    p23_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		       ((tmp << 12) & 0x70000000);

    tmp = (top & 0x0000ffff) + 0x00018000;
    p1_v_accum_init = ((tmp << 4) & 0x03ff8000) | 0x00000001;

    tmp = ((top >> 1) & 0x0000ffff) + 0x00018000;
    p23_v_accum_init = ((tmp << 4) & 0x01ff8000) | 0x00000001;

    leftUV = (left >> 17) & 15;
    left = (left >> 16) & 15;

    OUTREG(R128_OV0_REG_LOAD_CNTL, 1);
    while(!(INREG(R128_OV0_REG_LOAD_CNTL) & (1 << 3)));

    OUTREG(R128_OV0_H_INC, h_inc | ((h_inc >> 1) << 16));
    OUTREG(R128_OV0_STEP_BY, step_by | (step_by << 8));
    OUTREG(R128_OV0_Y_X_START, dstBox->x1 | (dstBox->y1 << 16));
    OUTREG(R128_OV0_Y_X_END,   dstBox->x2 | (dstBox->y2 << 16));
    OUTREG(R128_OV0_V_INC, v_inc);
    OUTREG(R128_OV0_P1_BLANK_LINES_AT_TOP, 0x00000fff | ((src_h - 1) << 16));
    src_h = (src_h + 1) >> 1;
    OUTREG(R128_OV0_P23_BLANK_LINES_AT_TOP, 0x000007ff | ((src_h - 1) << 16));
    OUTREG(R128_OV0_VID_BUF_PITCH0_VALUE, pitch);
    OUTREG(R128_OV0_VID_BUF_PITCH1_VALUE, pitch >> 1);
    OUTREG(R128_OV0_P1_X_START_END, (width - 1) | (left << 16));
    width >>= 1;
    OUTREG(R128_OV0_P2_X_START_END, (width - 1) | (leftUV << 16));
    OUTREG(R128_OV0_P3_X_START_END, (width - 1) | (leftUV << 16));
    OUTREG(R128_OV0_VID_BUF0_BASE_ADRS, offset1 & 0xfffffff0);
    OUTREG(R128_OV0_VID_BUF1_BASE_ADRS, (offset2 & 0xfffffff0) | 0x00000001);
    OUTREG(R128_OV0_VID_BUF2_BASE_ADRS, (offset3 & 0xfffffff0) | 0x00000001);
    OUTREG(R128_OV0_P1_V_ACCUM_INIT, p1_v_accum_init);
    OUTREG(R128_OV0_P23_V_ACCUM_INIT, p23_v_accum_init);
    OUTREG(R128_OV0_P1_H_ACCUM_INIT, p1_h_accum_init);
    OUTREG(R128_OV0_P23_H_ACCUM_INIT, p23_h_accum_init);
    OUTREG(R128_OV0_SCALE_CNTL, 0x41FF8A03);

    OUTREG(R128_OV0_REG_LOAD_CNTL, 0);
}



static int
R128PutImage(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, unsigned char* buf,
  short width, short height,
  Bool Sync,
  RegionPtr clipBoxes, pointer data
){
   R128InfoPtr info = R128PTR(pScrn);
   R128PortPrivPtr pPriv = (R128PortPrivPtr)data;
   INT32 xa, xb, ya, yb;
   int pitch, new_size, offset, s1offset, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int d1line, d2line, d3line, d1offset, d2offset, d3offset;
   int top, left, npixels, nlines, bpp;
   BoxRec dstBox;
   CARD32 tmp;

   /*
    * s1offset, s2offset, s3offset - byte offsets to the Y, U and V planes
    *                                of the source.
    *
    * d1offset, d2offset, d3offset - byte offsets to the Y, U and V planes
    *                                of the destination.
    *
    * offset - byte offset within the framebuffer to where the destination
    *          is stored.
    *
    * d1line, d2line, d3line - byte offsets within the destination to the
    *                          first displayed scanline in each plane.
    *
    */

   if(src_w > (drw_w << 4))
	drw_w = src_w >> 4;
   if(src_h > (drw_h << 4))
	drw_h = src_h >> 4;

   /* Clip */
   xa = src_x;
   xb = src_x + src_w;
   ya = src_y;
   yb = src_y + src_h;

   dstBox.x1 = drw_x;
   dstBox.x2 = drw_x + drw_w;
   dstBox.y1 = drw_y;
   dstBox.y2 = drw_y + drw_h;

   if(!R128ClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
	return Success;

   dstBox.x1 -= pScrn->frameX0;
   dstBox.x2 -= pScrn->frameX0;
   dstBox.y1 -= pScrn->frameY0;
   dstBox.y2 -= pScrn->frameY0;

   bpp = pScrn->bitsPerPixel >> 3;
   pitch = bpp * pScrn->displayWidth;

   switch(id) {
   case FOURCC_YV12:
   case FOURCC_I420:
	srcPitch = (width + 3) & ~3;
	srcPitch2 = ((width >> 1) + 3) & ~3;
	dstPitch = (width + 31) & ~31;  /* of luma */
	new_size = ((dstPitch * (height + (height >> 1))) + bpp - 1) / bpp;
	s1offset = 0;
	s2offset = srcPitch * height;
	s3offset = (srcPitch2 * (height >> 1)) + s2offset;
	break;
   case FOURCC_UYVY:
   case FOURCC_YUY2:
   default:
	srcPitch = width << 1;
	srcPitch2 = 0;
	dstPitch = ((width << 1) + 15) & ~15;
	new_size = ((dstPitch * height) + bpp - 1) / bpp;
	s1offset = 0;
	s2offset = 0;
	s3offset = 0;
	break;
   }

   if(!(pPriv->linear = R128AllocateMemory(pScrn, pPriv->linear,
		pPriv->doubleBuffer ? (new_size << 1) : new_size)))
   {
	return BadAlloc;
   }

   pPriv->currentBuffer ^= 1;

    /* copy data */
   top = ya >> 16;
   left = (xa >> 16) & ~1;
   npixels = ((((xb + 0xffff) >> 16) + 1) & ~1) - left;

   offset = pPriv->linear->offset * bpp;
   if(pPriv->doubleBuffer)
	offset += pPriv->currentBuffer * new_size * bpp;

   switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	d1line = top * dstPitch;
	d2line = (height * dstPitch) + ((top >> 1) * (dstPitch >> 1));
	d3line = d2line + ((height >> 1) * (dstPitch >> 1));

	top &= ~1;

	d1offset = (top * dstPitch) + left + offset;
	d2offset = d2line + (left >> 1) + offset;
	d3offset = d3line + (left >> 1) + offset;

	s1offset += (top * srcPitch) + left;
	tmp = ((top >> 1) * srcPitch2) + (left >> 1);
	s2offset += tmp;
	s3offset += tmp;
	if(id == FOURCC_YV12) {
	   tmp = s2offset;
	   s2offset = s3offset;
	   s3offset = tmp;
	}

	nlines = ((((yb + 0xffff) >> 16) + 1) & ~1) - top;
	{
#if X_BYTE_ORDER == X_BIG_ENDIAN
	   unsigned char *R128MMIO = info->MMIO;
	   CARD32 config_cntl;

	   /* We need to disabled byte swapping, or the data gets mangled */
	   config_cntl = INREG(R128_CONFIG_CNTL);
	   OUTREG(R128_CONFIG_CNTL, config_cntl &
	         ~(APER_0_BIG_ENDIAN_16BPP_SWAP|APER_0_BIG_ENDIAN_32BPP_SWAP));
#endif
	   R128CopyData420(buf + s1offset, buf + s2offset, buf + s3offset,
			  info->FB+d1offset, info->FB+d2offset, info->FB+d3offset,
			  srcPitch, srcPitch2, dstPitch, nlines, npixels);
#if X_BYTE_ORDER == X_BIG_ENDIAN
	   /* restore byte swapping */
	   OUTREG(R128_CONFIG_CNTL, config_cntl);
#endif
	}
	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
	left <<= 1;
	d1line = top * dstPitch;
	d2line = 0;
	d3line = 0;
	d1offset = d1line + left + offset;
	d2offset = 0;
	d3offset = 0;
	s1offset += (top * srcPitch) + left;
	nlines = ((yb + 0xffff) >> 16) - top;
	R128CopyData422(buf + s1offset, info->FB + d1offset,
			srcPitch, dstPitch, nlines, npixels);
	break;
    }


    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	(*info->accel->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy,
					(CARD32)~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
    }


    switch(id) {
     case FOURCC_YV12:
     case FOURCC_I420:
	R128DisplayVideo420(pScrn, width, height, dstPitch,
		     offset + d1line, offset + d2line, offset + d3line,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);
	break;
     case FOURCC_UYVY:
     case FOURCC_YUY2:
     default:
	R128DisplayVideo422(pScrn, id, offset + d1line, width, height, dstPitch,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);
	break;
    }

    pPriv->videoStatus = CLIENT_VIDEO_ON;

    info->VideoTimerCallback = R128VideoTimerCallback;

    return Success;
}


static int
R128QueryImageAttributes(
    ScrnInfoPtr pScrn,
    int id,
    unsigned short *w, unsigned short *h,
    int *pitches, int *offsets
){
    int size, tmp;

    if(*w > 2048) *w = 2048;
    if(*h > 2048) *h = 2048;

    *w = (*w + 1) & ~1;
    if(offsets) offsets[0] = 0;

    switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	*h = (*h + 1) & ~1;
	size = (*w + 3) & ~3;
	if(pitches) pitches[0] = size;
	size *= *h;
	if(offsets) offsets[1] = size;
	tmp = ((*w >> 1) + 3) & ~3;
	if(pitches) pitches[1] = pitches[2] = tmp;
	tmp *= (*h >> 1);
	size += tmp;
	if(offsets) offsets[2] = size;
	size += tmp;
	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
	size = *w << 1;
	if(pitches) pitches[0] = size;
	size *= *h;
	break;
    }

    return size;
}

static void
R128VideoTimerCallback(ScrnInfoPtr pScrn, Time now)
{
    R128InfoPtr info = R128PTR(pScrn);
    R128PortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;

    if(pPriv->videoStatus & TIMER_MASK) {
	if(pPriv->videoStatus & OFF_TIMER) {
	    if(pPriv->offTime < now) {
		unsigned char *R128MMIO = info->MMIO;
		OUTREG(R128_OV0_SCALE_CNTL, 0);
		pPriv->videoStatus = FREE_TIMER;
		pPriv->freeTime = now + FREE_DELAY;
	    }
	} else {  /* FREE_TIMER */
	    if(pPriv->freeTime < now) {
		if(pPriv->linear) {
		   xf86FreeOffscreenLinear(pPriv->linear);
		   pPriv->linear = NULL;
		}
		pPriv->videoStatus = 0;
		info->VideoTimerCallback = NULL;
	    }
	}
    } else  /* shouldn't get here */
	info->VideoTimerCallback = NULL;
}


#endif  /* !XvExtension */
