/* $Xorg: XSetDVal.c,v 1.4 2001/02/09 02:03:51 xorgcvs Exp $ */

/************************************************************

Copyright 1989, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

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

Copyright 1989 by Hewlett-Packard Company, Palo Alto, California.

			All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Hewlett-Packard not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

HEWLETT-PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
HEWLETT-PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/
/* $XFree86: xc/lib/Xi/XSetDVal.c,v 3.3 2001/12/14 19:55:22 dawes Exp $ */

/***********************************************************************
 *
 * XSetDeviceValuators - Set the value of valuators on an extension input
 * device.
 *
 */

#include "XI.h"
#include "XIproto.h"
#include "Xlibint.h"
#include "XInput.h"
#include "extutil.h"
#include "XIint.h"

int
XSetDeviceValuators (dpy, dev, valuators, first_valuator, num_valuators)
    register Display 	*dpy;
    XDevice 		*dev;
    int			*valuators;
    int			first_valuator;
    int			num_valuators;
    {       
    xSetDeviceValuatorsReq 		*req;
    xSetDeviceValuatorsReply 	rep;
    XExtDisplayInfo *info = XInput_find_display (dpy);

    LockDisplay (dpy);
    if (_XiCheckExtInit(dpy, XInput_Add_XSetDeviceValuators) == -1)
	return (NoSuchExtension);

    GetReq(SetDeviceValuators,req);		
    req->reqType = info->codes->major_opcode;
    req->ReqType = X_SetDeviceValuators;
    req->deviceid = dev->device_id;
    req->first_valuator = first_valuator;
    req->num_valuators = num_valuators;
    req->length += num_valuators;

    /* note: Data is a macro that uses its arguments multiple
       times, so "nvalues" is changed in a separate assignment
       statement */

    num_valuators <<= 2;
    Data (dpy, (char *) valuators, num_valuators);

    (void) _XReply (dpy, (xReply *) &rep, 0, xTrue);
    UnlockDisplay(dpy);
    SyncHandle();
    return (rep.status);
    }

