/* $Xorg: GrKeybd.c,v 1.3 2000/08/17 19:44:36 cpqbld Exp $ */
/*

Copyright 1986, 1998  The Open Group

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

#define NEED_REPLIES
#include "Xlibint.h"
int XGrabKeyboard (dpy, window, ownerEvents, pointerMode, keyboardMode, time)
    register Display *dpy;
    Window window;
    Bool ownerEvents;
    int pointerMode, keyboardMode;
    Time time;
{
        xGrabKeyboardReply rep;
	register xGrabKeyboardReq *req;
	register int status;
	LockDisplay(dpy);
        GetReq(GrabKeyboard, req);
	req->grabWindow = window;
	req->ownerEvents = ownerEvents;
	req->pointerMode = pointerMode;
	req->keyboardMode = keyboardMode;
	req->time = time;

       /* if we ever return, suppress the error */
	if (_XReply (dpy, (xReply *) &rep, 0, xTrue) == 0) 
		rep.status = GrabSuccess;
	status = rep.status;
	UnlockDisplay(dpy);
	SyncHandle();
	return (status);
}

