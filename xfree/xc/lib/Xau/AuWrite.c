/* $TOG: AuWrite.c /main/7 1998/02/06 14:15:18 kaleb $ */

/*

Copyright 1988, 1998  The Open Group

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
/* $XFree86$ */

#include <X11/Xauth.h>

static int
write_short (unsigned short s, FILE *file)
{
    unsigned char   file_short[2];

    file_short[0] = (s & (unsigned)0xff00) >> 8;
    file_short[1] = s & 0xff;
    if (fwrite ((char *) file_short, (int) sizeof (file_short), 1, file) != 1)
	return 0;
    return 1;
}

static int
write_counted_string (unsigned short count, char *string, FILE *file)
{
    if (write_short (count, file) == 0)
	return 0;
    if (fwrite (string, (int) sizeof (char), (int) count, file) != count)
	return 0;
    return 1;
}

int
XauWriteAuth (auth_file, auth)
FILE	*auth_file;
Xauth	*auth;
{
    if (write_short (auth->family, auth_file) == 0)
	return 0;
    if (write_counted_string (auth->address_length, auth->address, auth_file) == 0)
	return 0;
    if (write_counted_string (auth->number_length, auth->number, auth_file) == 0)
	return 0;
    if (write_counted_string (auth->name_length, auth->name, auth_file) == 0)
	return 0;
    if (write_counted_string (auth->data_length, auth->data, auth_file) == 0)
	return 0;
    return 1;
}
