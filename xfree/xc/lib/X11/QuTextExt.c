/* $Xorg: QuTextExt.c,v 1.4 2000/08/17 19:44:51 cpqbld Exp $ */
/*

Copyright 1986, 1987, 1998  The Open Group

All Rights Reserved.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

*/
/* $XFree86: xc/lib/X11/QuTextExt.c,v 1.3 2001/01/17 19:41:42 dawes Exp $ */

#define NEED_REPLIES
#include "Xlibint.h"

int
#if NeedFunctionPrototypes
XQueryTextExtents (
    register Display *dpy,
    Font fid,
    register _Xconst char *string,
    register int nchars,
    int *dir,
    int *font_ascent,
    int *font_descent,
    register XCharStruct *overall)
#else
XQueryTextExtents (dpy, fid, string, nchars, dir, font_ascent, font_descent,
                   overall)
    register Display *dpy;
    Font fid;
    register char *string;
    register int nchars;
    int *dir;
    int *font_ascent, *font_descent;
    register XCharStruct *overall;
#endif
{
    register int i;
    register char *ptr;
    char *buf;
    xQueryTextExtentsReply rep;
    long nbytes;
    register xQueryTextExtentsReq *req;

    LockDisplay(dpy);
    nbytes = nchars << 1;
    GetReq(QueryTextExtents, req);
    req->fid = fid;
    if ((buf = _XAllocScratch (dpy, (unsigned long) nbytes))) {
	req->length += (nbytes + 3)>>2;
	req->oddLength = nchars & 1;
	for (ptr = buf, i = nchars; --i >= 0;) {
	    *ptr++ = 0;
	    *ptr++ = *string++;
	}
	Data (dpy, buf, nbytes);
    }
    if (!_XReply (dpy, (xReply *)&rep, 0, xTrue) || !buf) {
        UnlockDisplay(dpy);
	SyncHandle();
	return 0;
    }
    *dir = rep.drawDirection;
    *font_ascent = cvtINT16toInt (rep.fontAscent);
    *font_descent = cvtINT16toInt (rep.fontDescent);
    overall->ascent = (short) cvtINT16toShort (rep.overallAscent);
    overall->descent = (short) cvtINT16toShort (rep.overallDescent);
    /* XXX bogus - we're throwing away information!!! */
    overall->width  = (short) cvtINT32toInt (rep.overallWidth);
    overall->lbearing = (short) cvtINT32toInt (rep.overallLeft);
    overall->rbearing = (short) cvtINT32toInt (rep.overallRight);
    UnlockDisplay(dpy);
    SyncHandle();
    return 1;
}
