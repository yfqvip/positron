/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include <gtk/gtk.h>
#include "nsRenderingContextGTK.h"
#include "nsRegionGTK.h"
#include "nsGfxCIID.h"
#include <math.h>

#define NS_TO_GDK_RGB(ns) (ns & 0xff) << 16 | (ns & 0xff00) | ((ns >> 16) & 0xff)

#define NSRECT_TO_GDKRECT(ns,gdk) \
  PR_BEGIN_MACRO \
  gdk.x = ns.x; \
  gdk.y = ns.y; \
  gdk.width = ns.width; \
  gdk.height = ns.height; \
  PR_END_MACRO

static NS_DEFINE_IID(kRenderingContextIID, NS_IRENDERING_CONTEXT_IID);

class GraphicsState
{
public:
   GraphicsState();
  ~GraphicsState();

  nsTransform2D  *mMatrix;
  nsRect          mLocalClip;
  nsRegionGTK    *mClipRegion;
  nscolor         mColor;
  nsLineStyle     mLineStyle;
  nsIFontMetrics *mFontMetrics;
  GdkFont        *mFont;
};

GraphicsState::GraphicsState()
{
  mMatrix = nsnull;
  mLocalClip.x = mLocalClip.y = mLocalClip.width = mLocalClip.height = 0;
  mClipRegion = nsnull;
  mColor = NS_RGB(0, 0, 0);
  mLineStyle = nsLineStyle_kSolid;
  mFontMetrics = nsnull;
  mFont = nsnull;
}

GraphicsState::~GraphicsState()
{
}


nsRenderingContextGTK::nsRenderingContextGTK()
{
  NS_INIT_REFCNT();

  mFontMetrics = nsnull;
  mContext = nsnull;
  mRenderingSurface = nsnull;
  mOffscreenSurface = nsnull;
  mCurrentColor = 0;
  mCurrentLineStyle = nsLineStyle_kSolid;
  mCurrentFont = nsnull;
  mTMatrix = nsnull;
  mP2T = 1.0f;
  mStateCache = new nsVoidArray();
  mRegion = new nsRegionGTK();
  mRegion->Init();

  PushState();
}

nsRenderingContextGTK::~nsRenderingContextGTK()
{
  delete mRegion;

  mTMatrix = nsnull;

  // Destroy the State Machine
  if (nsnull != mStateCache)
  {
    PRInt32 cnt = mStateCache->Count();

    while (--cnt >= 0)
    {
      GraphicsState *state = (GraphicsState *)mStateCache->ElementAt(cnt);
      mStateCache->RemoveElementAt(cnt);

      if (nsnull != state)
        delete state;
    }

    delete mStateCache;
    mStateCache = nsnull;
  }

  // Destroy the front buffer and it's GC if one was allocated for it
  if (nsnull != mOffscreenSurface) {
    delete mOffscreenSurface;
  }

  NS_IF_RELEASE(mFontMetrics);
  NS_IF_RELEASE(mContext);
}

NS_IMPL_QUERY_INTERFACE(nsRenderingContextGTK, kRenderingContextIID)
NS_IMPL_ADDREF(nsRenderingContextGTK)
NS_IMPL_RELEASE(nsRenderingContextGTK)

NS_IMETHODIMP nsRenderingContextGTK::Init(nsIDeviceContext* aContext,
                                          nsIWidget *aWindow)
{
  mContext = aContext;
  NS_IF_ADDREF(mContext);

//  ::gdk_rgb_init();

  mRenderingSurface = new nsDrawingSurfaceGTK();

  mRenderingSurface->drawable = (GdkDrawable *)aWindow->GetNativeData(NS_NATIVE_WINDOW);
  mRenderingSurface->gc       = (GdkGC *)aWindow->GetNativeData(NS_NATIVE_GRAPHIC);

  mOffscreenSurface = mRenderingSurface;

  return (CommonInit());
}

NS_IMETHODIMP nsRenderingContextGTK::Init(nsIDeviceContext* aContext,
                                          nsDrawingSurface aSurface)
{
  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mRenderingSurface = (nsDrawingSurfaceGTK *) aSurface;

  return (CommonInit());
}

NS_IMETHODIMP nsRenderingContextGTK::CommonInit()
{
  mContext->GetDevUnitsToAppUnits(mP2T);
  float app2dev;
  mContext->GetAppUnitsToDevUnits(app2dev);
  mTMatrix->AddScale(app2dev, app2dev);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetHints(PRUint32& aResult)
{
  PRUint32 result = 0;

  // Most X servers implement 8 bit text rendering alot faster than
  // XChar2b rendering. In addition, we can avoid the PRUnichar to
  // XChar2b conversion. So we set this bit...
  result |= NS_RENDERING_HINT_FAST_8BIT_TEXT;

  // XXX see if we are rendering to the local display or to a remote
  // dispaly and set the NS_RENDERING_HINT_REMOTE_RENDERING accordingly

  aResult = result;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::SelectOffScreenDrawingSurface(nsDrawingSurface aSurface)
{
  if (nsnull == aSurface)
    mRenderingSurface = mOffscreenSurface;
  else
    mRenderingSurface = (nsDrawingSurfaceGTK *)aSurface;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::Reset()
{
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetDeviceContext(nsIDeviceContext *&aContext)
{
  NS_IF_ADDREF(mContext);
  aContext = mContext;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::PushState(void)
{
  GraphicsState * state = new GraphicsState();

  // Push into this state object, add to vector
  state->mMatrix = mTMatrix;

  mStateCache->AppendElement(state);

  if (nsnull == mTMatrix)
    mTMatrix = new nsTransform2D();
  else
    mTMatrix = new nsTransform2D(mTMatrix);

  PRBool clipState;
  GetClipRect(state->mLocalClip, clipState);

  state->mClipRegion = mRegion;

  if (nsnull != state->mClipRegion) {
    mRegion = new nsRegionGTK();
    mRegion->Init();
    mRegion->SetTo(state->mClipRegion);
  }

  state->mColor = mCurrentColor;
  state->mLineStyle = mCurrentLineStyle;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::PopState(PRBool &aClipEmpty)
{
  PRBool bEmpty = PR_FALSE;

  PRUint32 cnt = mStateCache->Count();
  GraphicsState * state;

  if (cnt > 0) {
    state = (GraphicsState *)mStateCache->ElementAt(cnt - 1);
    mStateCache->RemoveElementAt(cnt - 1);

    // Assign all local attributes from the state object just popped
    if (mTMatrix)
      delete mTMatrix;
    mTMatrix = state->mMatrix;

    if (nsnull != mRegion)
      delete mRegion;

    mRegion = state->mClipRegion;

    if (nsnull != mRegion && mRegion->IsEmpty() == PR_TRUE) {
      bEmpty = PR_TRUE;
    }else{

  // Select in the old region.  We probably want to set a dirty flag and only
  // do this IFF we need to draw before the next Pop.  We'd need to check the
 // state flag on every draw operation.
      if (nsnull != mRegion)
      {
         GdkRegion *rgn;
         mRegion->GetNativeRegion((void*&)rgn);
	::gdk_gc_set_clip_region (mRenderingSurface->gc, rgn);
        // can we destroy rgn now?
      }
    }

    if (state->mColor != mCurrentColor)
        SetColor(state->mColor);

    if (state->mLineStyle != mCurrentLineStyle)
      SetLineStyle(state->mLineStyle);


    // Delete this graphics state object
    delete state;
  }

  aClipEmpty = bEmpty;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::IsVisibleRect(const nsRect& aRect,
                                                   PRBool &aVisible)
{
  aVisible = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetClipRect(nsRect &aRect, PRBool &aClipValid)
{
  PRInt32 x, y, w, h;
  if (!mRegion->IsEmpty())
  {
    mRegion->GetBoundingBox(&x,&y,&w,&h);
    aRect.SetRect(x,y,w,h);
    aClipValid = PR_TRUE;
  } else {
    aRect.SetRect(0,0,0,0);
    aClipValid = PR_FALSE;
  }

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::SetClipRect(const nsRect& aRect,
                                                 nsClipCombine aCombine,
                                                 PRBool &aClipEmpty)
{
  nsRect trect = aRect;
  GdkRegion *rgn;

//  mStates->mLocalClip = aRect;

  mTMatrix->TransformCoord(&trect.x, &trect.y,
                           &trect.width, &trect.height);

  switch(aCombine)
  {
    case nsClipCombine_kIntersect:
      mRegion->Intersect(trect.x,trect.y,trect.width,trect.height);
      break;
    case nsClipCombine_kUnion:
      mRegion->Union(trect.x,trect.y,trect.width,trect.height);
      break;
    case nsClipCombine_kSubtract:
      mRegion->Subtract(trect.x,trect.y,trect.width,trect.height);
      break;
    case nsClipCombine_kReplace:
      mRegion->SetTo(trect.x,trect.y,trect.width,trect.height);
      break;
  }

  aClipEmpty = mRegion->IsEmpty();

  mRegion->GetNativeRegion((void*&)rgn);
  gdk_gc_set_clip_region(mRenderingSurface->gc,rgn);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::SetClipRegion(const nsIRegion& aRegion,
                                                   nsClipCombine aCombine,
                                                   PRBool &aClipEmpty)
{
  GdkRegion *rgn;
 
  switch(aCombine)
  {
    case nsClipCombine_kIntersect:
      mRegion->Intersect(aRegion);
      break;
    case nsClipCombine_kUnion:
      mRegion->Union(aRegion);
      break;
    case nsClipCombine_kSubtract:
      mRegion->Subtract(aRegion);
      break;
    case nsClipCombine_kReplace:
      mRegion->SetTo(aRegion);
      break;
  }

  aClipEmpty = mRegion->IsEmpty();
  mRegion->GetNativeRegion((void*&)rgn);
  gdk_gc_set_clip_region(mRenderingSurface->gc,rgn);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetClipRegion(nsIRegion **aRegion)
{
  NS_IF_ADDREF(mRegion);
  *aRegion = (nsIRegion*)&mRegion;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::SetColor(nscolor aColor)
{
  if (nsnull == mContext)  
    return NS_ERROR_FAILURE;
      
  mCurrentColor = aColor;

  ::gdk_rgb_gc_set_foreground(mRenderingSurface->gc, NS_TO_GDK_RGB(mCurrentColor));
  
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetColor(nscolor &aColor) const
{
  aColor = mCurrentColor;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::SetFont(const nsFont& aFont)
{
  NS_IF_RELEASE(mFontMetrics);
  mContext->GetMetricsFor(aFont, mFontMetrics);
  return SetFont(mFontMetrics);
}

NS_IMETHODIMP nsRenderingContextGTK::SetFont(nsIFontMetrics *aFontMetrics)
{
  NS_IF_RELEASE(mFontMetrics);
  mFontMetrics = aFontMetrics;
  NS_IF_ADDREF(mFontMetrics);

  if (mFontMetrics)
  {
    nsFontHandle  fontHandle;
    mFontMetrics->GetFontHandle(fontHandle);
    mCurrentFont = (GdkFont *)fontHandle;

    gdk_gc_set_font(mRenderingSurface->gc,
                    mCurrentFont);
  }

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::SetLineStyle(nsLineStyle aLineStyle)
{
  if (aLineStyle != mCurrentLineStyle)
  {
    switch(aLineStyle)
    { 
      case nsLineStyle_kSolid:
        ::gdk_gc_set_line_attributes(mRenderingSurface->gc,
                          1, GDK_LINE_SOLID, (GdkCapStyle)0, (GdkJoinStyle)0);
        break;

      case nsLineStyle_kDashed: {
        static char dashed[2] = {4,4};

        ::gdk_gc_set_dashes(mRenderingSurface->gc, 
                     0, dashed, 2);
        } break;

      case nsLineStyle_kDotted: {
        static char dotted[2] = {3,1};

        ::gdk_gc_set_dashes(mRenderingSurface->gc, 
                     0, dotted, 2);
         }break;

      default:
        break;

    }
    
    mCurrentLineStyle = aLineStyle ;
  }

  return NS_OK;

}

NS_IMETHODIMP nsRenderingContextGTK::GetLineStyle(nsLineStyle &aLineStyle)
{
  aLineStyle = mCurrentLineStyle;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetFontMetrics(nsIFontMetrics *&aFontMetrics)
{
  NS_IF_ADDREF(mFontMetrics);
  aFontMetrics = mFontMetrics;
  return NS_OK;
}

// add the passed in translation to the current translation
NS_IMETHODIMP nsRenderingContextGTK::Translate(nscoord aX, nscoord aY)
{
  mTMatrix->AddTranslation((float)aX,(float)aY);
  return NS_OK;
}

// add the passed in scale to the current scale
NS_IMETHODIMP nsRenderingContextGTK::Scale(float aSx, float aSy)
{
  mTMatrix->AddScale(aSx, aSy);
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetCurrentTransform(nsTransform2D *&aTransform)
{
  aTransform = mTMatrix;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::CreateDrawingSurface(nsRect *aBounds,
                                                          PRUint32 aSurfFlags,
                                                          nsDrawingSurface &aSurface)
{
  GdkPixmap *pixmap;

  if (nsnull == mRenderingSurface) {
    aSurface = nsnull;
    return NS_ERROR_FAILURE;
  }
 
  g_return_val_if_fail (aBounds != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail ((aBounds->width > 0) && (aBounds->height > 0), NS_ERROR_FAILURE);
 
  pixmap = ::gdk_pixmap_new(mRenderingSurface->drawable, aBounds->width, aBounds->height, gdk_rgb_get_visual()->depth);
  nsDrawingSurfaceGTK * surface = new nsDrawingSurfaceGTK();

  surface->drawable = pixmap;
  surface->gc       = mRenderingSurface->gc;

  aSurface = (nsDrawingSurface)surface;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DestroyDrawingSurface(nsDrawingSurface aDS)
{
  nsDrawingSurfaceGTK * surface = (nsDrawingSurfaceGTK *) aDS;

  g_return_val_if_fail ((surface != NULL), NS_ERROR_FAILURE);
  g_return_val_if_fail ((surface->drawable != NULL), NS_ERROR_FAILURE);
  ::gdk_pixmap_unref (surface->drawable);

  delete surface;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawLine(nscoord aX0, nscoord aY0, nscoord aX1, nscoord aY1)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  mTMatrix->TransformCoord(&aX0,&aY0);
  mTMatrix->TransformCoord(&aX1,&aY1);

  ::gdk_draw_line(mRenderingSurface->drawable,
                  mRenderingSurface->gc,
                  aX0, aY0, aX1, aY1);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawPolyline(const nsPoint aPoints[], PRInt32 aNumPoints)
{
  PRUint32 i;

  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  GdkPoint *pts = new GdkPoint[aNumPoints];
	for (i = 0; i < aNumPoints; i++)
  {
    nsPoint p = aPoints[i];
    mTMatrix->TransformCoord(&p.x,&p.y);
    pts[i].x = p.x;
    pts[i].y = p.y;
  }

  ::gdk_draw_lines(mRenderingSurface->drawable,
                   mRenderingSurface->gc,
                   pts, aNumPoints);

  delete[] pts;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawRect(const nsRect& aRect)
{
  return DrawRect(aRect.x, aRect.y, aRect.width, aRect.height);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  if (nsnull == mTMatrix || nsnull == mRenderingSurface) {
    return NS_ERROR_FAILURE;
  }

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  g_return_val_if_fail ((mRenderingSurface->drawable != NULL) ||
                        (mRenderingSurface->gc != NULL), NS_ERROR_FAILURE);

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::gdk_draw_rectangle(mRenderingSurface->drawable, mRenderingSurface->gc,
                       FALSE,
                       x, y, w, h);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::FillRect(const nsRect& aRect)
{
  return FillRect(aRect.x, aRect.y, aRect.width, aRect.height);
}

NS_IMETHODIMP nsRenderingContextGTK::FillRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  if (nsnull == mTMatrix || nsnull == mRenderingSurface) {
    return NS_ERROR_FAILURE;
  }

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::gdk_draw_rectangle(mRenderingSurface->drawable, mRenderingSurface->gc,
                       TRUE,
                       x, y, w, h);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawPolygon(const nsPoint aPoints[], PRInt32 aNumPoints)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  GdkPoint *pts = new GdkPoint[aNumPoints];
	for (PRInt32 i = 0; i < aNumPoints; i++)
  {
    nsPoint p = aPoints[i];
		mTMatrix->TransformCoord(&p.x,&p.y);
		pts[i].x = p.x;
    pts[i].y = p.y;
	}
  ::gdk_draw_polygon(mRenderingSurface->drawable, mRenderingSurface->gc, FALSE, pts, aNumPoints);

  delete[] pts;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::FillPolygon(const nsPoint aPoints[], PRInt32 aNumPoints)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  GdkPoint *pts = new GdkPoint[aNumPoints];
	for (PRInt32 i = 0; i < aNumPoints; i++)
  {
    nsPoint p = aPoints[i];
		mTMatrix->TransformCoord(&p.x,&p.y);
		pts[i].x = p.x;
    pts[i].y = p.y;
	}
  ::gdk_draw_polygon(mRenderingSurface->drawable, mRenderingSurface->gc, TRUE, pts, aNumPoints);

  delete[] pts;

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawEllipse(const nsRect& aRect)
{
  return DrawEllipse(aRect.x, aRect.y, aRect.width, aRect.height);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::gdk_draw_arc(mRenderingSurface->drawable, mRenderingSurface->gc, FALSE,
                 x, y, w, h,
                 0, 360 * 64);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::FillEllipse(const nsRect& aRect)
{
  return FillEllipse(aRect.x, aRect.y, aRect.width, aRect.height);
}

NS_IMETHODIMP nsRenderingContextGTK::FillEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::gdk_draw_arc(mRenderingSurface->drawable, mRenderingSurface->gc, TRUE,
                 x, y, w, h,
                 0, 360 * 64);

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawArc(const nsRect& aRect,
                                             float aStartAngle, float aEndAngle)
{
  return DrawArc(aRect.x,aRect.y,aRect.width,aRect.height,aStartAngle,aEndAngle);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawArc(nscoord aX, nscoord aY,
                                             nscoord aWidth, nscoord aHeight,
                                             float aStartAngle, float aEndAngle)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::gdk_draw_arc(mRenderingSurface->drawable, mRenderingSurface->gc, FALSE,
                 x, y, w, h,
                 NSToIntRound(aStartAngle * 64.0f),
                 NSToIntRound(aEndAngle * 64.0f));

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::FillArc(const nsRect& aRect,
                                             float aStartAngle, float aEndAngle)
{
  return FillArc(aRect.x,aRect.y,aRect.width,aRect.height,aStartAngle,aEndAngle);
}


NS_IMETHODIMP nsRenderingContextGTK::FillArc(nscoord aX, nscoord aY,
                                             nscoord aWidth, nscoord aHeight,
                                             float aStartAngle, float aEndAngle)
{
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::gdk_draw_arc(mRenderingSurface->drawable, mRenderingSurface->gc, TRUE,
                 x, y, w, h,
                 NSToIntRound(aStartAngle * 64.0f),
                 NSToIntRound(aEndAngle * 64.0f));

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetWidth(char aC, nscoord &aWidth)
{
  gint width;
  GdkFont *font = (GdkFont *)mCurrentFont;
  width = gdk_char_width(font,aC); 
  aWidth = NSToCoordRound(width * mP2T);
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetWidth(PRUnichar aC, nscoord &aWidth, PRInt32 *aFontID)
{
  gint width;
  GdkFont *font = (GdkFont *)mCurrentFont;
  width = gdk_char_width_wc(font,(GdkWChar)aC); 
  aWidth = NSToCoordRound(width * mP2T);
  if (nsnull != aFontID)
    *aFontID = 0;
  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::GetWidth(const nsString& aString, nscoord &aWidth, PRInt32 *aFontID)
{
  char* cStr = aString.ToNewCString();
  NS_IMETHODIMP ret = GetWidth(cStr, aString.Length(), aWidth);
  delete[] cStr;
  if (nsnull != aFontID)
    *aFontID = 0;
  return ret;
}

NS_IMETHODIMP nsRenderingContextGTK::GetWidth(const char *aString, nscoord &aWidth)
{
  return GetWidth(aString, strlen(aString), aWidth);
}

NS_IMETHODIMP nsRenderingContextGTK::GetWidth(const char *aString,
                                              PRUint32 aLength, nscoord &aWidth)
{
  g_return_val_if_fail(aString != NULL, NS_ERROR_FAILURE);
  /* this really shouldn't be 0, and this gets called quite a bit.  not sure
   * why.  could someone fix this on the XP side please?
   */
//  g_return_val_if_fail(aLength != 0, NS_ERROR_FAILURE);

  PRInt32     rc;

  GdkFont *font = (GdkFont *)mCurrentFont;
  rc = gdk_text_width (font, aString, aLength);
  aWidth = NSToCoordRound(rc * mP2T);

  return NS_OK;
}



NS_IMETHODIMP nsRenderingContextGTK::GetWidth(const PRUnichar *aString,
                                              PRUint32 aLength, nscoord &aWidth,
                                              PRInt32 *aFontID)
{
    g_return_val_if_fail(aString != NULL, NS_ERROR_FAILURE);
  /* this really shouldn't be 0, and this gets called quite a bit.  not sure
   * why.  could someone fix this on the XP side please?
   */
//    g_return_val_if_fail(aLength != 0, NS_ERROR_FAILURE);

    nsString nsStr;
    nsStr.SetString(aString, aLength);
    char* cStr = nsStr.ToNewCString();
    NS_IMETHODIMP ret = GetWidth(cStr, aLength, aWidth);
    delete[] cStr;

    if (nsnull != aFontID)
      *aFontID = 0;

    return ret;
}

NS_IMETHODIMP
nsRenderingContextGTK::DrawString(const char *aString, PRUint32 aLength,
                                  nscoord aX, nscoord aY,
                                  const nscoord* aSpacing)
{
  g_return_val_if_fail(aString != NULL, NS_ERROR_FAILURE);
  /* this really shouldn't be 0, and this gets called quite a bit.  not sure
   * why.  could someone fix this on the XP side please?
   */
//  g_return_val_if_fail(aLength != 0, NS_ERROR_FAILURE);
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  nscoord x = aX;
  nscoord y = aY;

  // Substract xFontStruct ascent since drawing specifies baseline
  if (mFontMetrics) {
    mFontMetrics->GetMaxAscent(y);
    y+=aY;
  }

  mTMatrix->TransformCoord(&x,&y);

  ::gdk_draw_text (mRenderingSurface->drawable, mCurrentFont,
                   mRenderingSurface->gc,
                   x, y, aString, aLength);

#if 0
  //this is no longer to be done by this API, but another
  //will take it's place that will need this code again. MMP
  if (mFontMetrics)
  {
    const nsFont *font;
    mFontMetrics->GetFont(font);
    PRUint8 deco = font->decorations;

    if (deco & NS_FONT_DECORATION_OVERLINE)
      DrawLine(aX, aY, aX + aWidth, aY);

    if (deco & NS_FONT_DECORATION_UNDERLINE)
    {
      nscoord ascent,descent;

      mFontMetrics->GetMaxAscent(ascent);
      mFontMetrics->GetMaxDescent(descent);

      DrawLine(aX, aY + ascent + (descent >> 1),
               aX + aWidth, aY + ascent + (descent >> 1));
    }

    if (deco & NS_FONT_DECORATION_LINE_THROUGH)
    {
      nscoord height;

	  mFontMetrics->GetHeight(height);

      DrawLine(aX, aY + (height >> 1), aX + aWidth, aY + (height >> 1));
    }
  }
#endif

  return NS_OK;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawString(const PRUnichar *aString, PRUint32 aLength,
                                                nscoord aX, nscoord aY,
                                                PRInt32 aFontID,
                                                const nscoord* aSpacing)
{
  nsString nsStr;
  nsStr.SetString(aString, aLength);
  char* cStr = nsStr.ToNewCString();
  NS_IMETHODIMP ret = DrawString(cStr, aLength, aX, aY, aSpacing);
  delete[] cStr;
  return ret;
}

NS_IMETHODIMP nsRenderingContextGTK::DrawString(const nsString& aString,
                                                nscoord aX, nscoord aY,
                                                PRInt32 aFontID,
                                                const nscoord* aSpacing)
{
  return DrawString(aString.GetUnicode(), aString.Length(), aX, aY, aFontID, aSpacing);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawImage(nsIImage *aImage, nscoord aX, nscoord aY)
{
  nscoord width,height;
  width = NSToCoordRound(mP2T * aImage->GetWidth());
  height = NSToCoordRound(mP2T * aImage->GetHeight());

  return DrawImage(aImage,aX,aY,width,height);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawImage(nsIImage *aImage, nscoord aX, nscoord aY,
                     nscoord aWidth, nscoord aHeight)
{
  nsRect	tr;

  tr.x = aX;
  tr.y = aY;
  tr.width = aWidth;
  tr.height = aHeight;

  return DrawImage(aImage,tr);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawImage(nsIImage *aImage, const nsRect& aRect)
{
  nsRect	tr;

  tr = aRect;
  mTMatrix->TransformCoord(&tr.x,&tr.y,&tr.width,&tr.height);

  return aImage->Draw(*this,mRenderingSurface,tr.x,tr.y,tr.width,tr.height);
}

NS_IMETHODIMP nsRenderingContextGTK::DrawImage(nsIImage *aImage, const nsRect& aSRect, const nsRect& aDRect)
{
  nsRect	sr,dr;

  sr = aSRect;
  mTMatrix ->TransformCoord(&sr.x,&sr.y,&sr.width,&sr.height);

  dr = aDRect;
  mTMatrix->TransformCoord(&dr.x,&dr.y,&dr.width,&dr.height);

  return aImage->Draw(*this,mRenderingSurface,sr.x,sr.y,sr.width,sr.height,
                      dr.x,dr.y,dr.width,dr.height);
}

NS_IMETHODIMP
nsRenderingContextGTK::CopyOffScreenBits(nsDrawingSurface aSrcSurf,
                                         PRInt32 aSrcX, PRInt32 aSrcY,
                                         const nsRect &aDestBounds,
                                         PRUint32 aCopyFlags)
{
  PRInt32               x = aSrcX;
  PRInt32               y = aSrcY;
  nsRect                drect = aDestBounds;
  nsDrawingSurfaceGTK  *destsurf;

  g_return_val_if_fail(aSrcSurf != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mTMatrix != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->drawable != NULL, NS_ERROR_FAILURE);
  g_return_val_if_fail(mRenderingSurface->gc != NULL, NS_ERROR_FAILURE);

  if (aCopyFlags & NS_COPYBITS_TO_BACK_BUFFER)
  {
    NS_ASSERTION(!(nsnull == mRenderingSurface), "no back buffer");
    destsurf = mRenderingSurface;
  }
  else
    destsurf = mOffscreenSurface;

  if (aCopyFlags & NS_COPYBITS_XFORM_SOURCE_VALUES)
    mTMatrix->TransformCoord(&x, &y);

  if (aCopyFlags & NS_COPYBITS_XFORM_DEST_VALUES)
    mTMatrix->TransformCoord(&drect.x, &drect.y, &drect.width, &drect.height);

  //XXX flags are unused. that would seem to mean that there is
  //inefficiency somewhere... MMP

  ::gdk_draw_pixmap(destsurf->drawable,
                    ((nsDrawingSurfaceGTK *)aSrcSurf)->gc,
                    ((nsDrawingSurfaceGTK *)aSrcSurf)->drawable,
                    x, y,
                    drect.x, drect.y,
                    drect.width, drect.height);

  return NS_OK;
}
