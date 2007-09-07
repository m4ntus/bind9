/*
 * Copyright (c) 1996,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef lint
static const char rcsid[] = "$Id: ns_name.c,v 1.1 2000/12/07 00:52:29 tale Exp $";
#endif

#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/nameser.h>

#include <errno.h>
#include <resolv.h>
#include <string.h>
#include <ctype.h>

#include <mdn/resconf.h>
#include <mdn/res.h>

#ifndef NS_CMPRSFLGS
#define NS_CMPRSFLGS	0xc0
#endif
#ifndef NS_MAXCDNAME
#define NS_MAXCDNAME	255
#endif

/* Data. */

static const char	digits[] = "0123456789";

static int 		mdn_initialized = 0;
static mdn_resconf_t	mdn_conf = NULL;

/* Forward. */

static int		special(int);
static int		dn_find(const u_char *, const u_char *,
				const u_char * const *,
				const u_char * const *);
static void		mdn_initialize(void);
static const char	*convert_from_local(const char *local_name,
					    char *dns_name,
					    size_t dns_name_len);
static const char	*convert_to_local(const char *dns_name,
					  char *local_name,
					  size_t local_name_len);

/* Public. */

/*
 * ns_name_ntop(src, dst, dstsiz)
 *	Convert an encoded domain name to printable ascii as per RFC1035.
 * return:
 *	Number of bytes written to buffer, or -1 (with errno set)
 * notes:
 *	The root is returned as "."
 *	All other domains are returned in non absolute form
 */
int
ns_name_ntop(const u_char *src, char *dst, size_t dstsiz) {
	const u_char *cp;
	char *dn, *eom;
	u_char c;
	u_int n;
	char mdnbuf[256];

	cp = src;
	dn = mdnbuf;
	eom = mdnbuf + sizeof(mdnbuf);

	while ((n = *cp++) != 0) {
		if ((n & NS_CMPRSFLGS) != 0) {
			/* Some kind of compression pointer. */
			errno = EMSGSIZE;
			return (-1);
		}
		if (dn != mdnbuf) {
			if (dn >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			*dn++ = '.';
		}
		if (dn + n >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		for ((void)NULL; n > 0; n--) {
			c = *cp++;
			if (special(c)) {
				if (dn + 1 >= eom) {
					errno = EMSGSIZE;
					return (-1);
				}
				*dn++ = '\\';
				*dn++ = (char)c;
			} else {
				if (dn >= eom) {
					errno = EMSGSIZE;
					return (-1);
				}
				*dn++ = (char)c;
			}
		}
	}
	if (dn == mdnbuf) {
		if (dn >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		*dn++ = '.';
	}
	if (dn >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	*dn++ = '\0';

	n = dn - mdnbuf;
	if (convert_to_local(mdnbuf, dst, dstsiz) == mdnbuf) {
		if (dstsiz < n) {
			errno = EMSGSIZE;
			return (-1);
		}
		(void)memcpy(dst, mdnbuf, n);
	}
	return (n);
}

/*
 * For glibc-2.1, we want to define __ns_name_ntop() in addition with
 * ns_name_ntop().
 * However, on some systems the header files contain a macro that
 * expands ns_name_ntop into __ns_name_ntop, causing function redefinition
 * error.  To prevent the error, we prepend "__" to what is the result
 * of macro expansion of "ns_name_ntop".  This requires two macros below.
 */
#define mdn_prepend(x) _mdn_prepend(x)
#define _mdn_prepend(x) __ ## x
int
mdn_prepend(ns_name_ntop)(const u_char *src, char *dst, size_t dstsiz) {
	return (ns_name_ntop(src, dst, dstsiz));
}

/*
 * ns_name_pton(src, dst, dstsiz)
 *	Convert a ascii string into an encoded domain name as per RFC1035.
 * return:
 *	-1 if it fails
 *	1 if string was fully qualified
 *	0 is string was not fully qualified
 * notes:
 *	Enforces label and domain length limits.
 */

int
ns_name_pton(const char *src, u_char *dst, size_t dstsiz) {
	u_char *label, *bp, *eom;
	int c, n, escaped;
	char *cp;
	char mdnbuf[256];

	src = convert_from_local(src, mdnbuf, sizeof(mdnbuf));

	escaped = 0;
	bp = dst;
	eom = dst + dstsiz;
	label = bp++;

	while ((c = *src++) != 0) {
		if (c == '.') {
			c = (bp - label - 1);
			if ((c & NS_CMPRSFLGS) != 0) {	/* Label too big. */
				errno = EMSGSIZE;
				return (-1);
			}
			if (label >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			*label = c;
			/* Fully qualified ? */
			if (*src == '\0') {
				if (c != 0) {
					if (bp >= eom) {
						errno = EMSGSIZE;
						return (-1);
					}
					*bp++ = '\0';
				}
				if ((bp - dst) > MAXCDNAME) {
					errno = EMSGSIZE;
					return (-1);
				}
				return (1);
			}
			if (c == 0 || *src == '.') {
				errno = EMSGSIZE;
				return (-1);
			}
			label = bp++;
			continue;
		}
		if (bp >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		*bp++ = (u_char)c;
	}
	c = (bp - label - 1);
	if ((c & NS_CMPRSFLGS) != 0) {		/* Label too big. */
		errno = EMSGSIZE;
		return (-1);
	}
	if (label >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	*label = c;
	if (c != 0) {
		if (bp >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		*bp++ = 0;
	}
	if ((bp - dst) > MAXCDNAME) {	/* src too big */
		errno = EMSGSIZE;
		return (-1);
	}
	return (0);
}

/*
 * ns_name_ntol(src, dst, dstsiz)
 *	Convert a network strings labels into all lowercase.
 * return:
 *	Number of bytes written to buffer, or -1 (with errno set)
 * notes:
 *	Enforces label and domain length limits.
 */

int
ns_name_ntol(const u_char *src, u_char *dst, size_t dstsiz) {
	const u_char *cp;
	u_char *dn, *eom;
	u_char c;
	u_int n;

	cp = src;
	dn = dst;
	eom = dst + dstsiz;

	while ((n = *cp++) != 0) {
		if ((n & NS_CMPRSFLGS) != 0) {
			/* Some kind of compression pointer. */
			errno = EMSGSIZE;
			return (-1);
		}
		*dn++ = n;
		if (dn + n >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		for ((void)NULL; n > 0; n--) {
			c = *cp++;
			if (c < 128 && isupper(c))
				*dn++ = tolower(c);
			else
				*dn++ = c;
		}
	}
	*dn++ = '\0';
	return (dn - dst);
}

/*
 * ns_name_unpack(msg, eom, src, dst, dstsiz)
 *	Unpack a domain name from a message, source may be compressed.
 * return:
 *	-1 if it fails, or consumed octets if it succeeds.
 */
int
ns_name_unpack(const u_char *msg, const u_char *eom, const u_char *src,
	       u_char *dst, size_t dstsiz)
{
	const u_char *srcp, *dstlim;
	u_char *dstp;
	int n, len, checked;

	len = -1;
	checked = 0;
	dstp = dst;
	srcp = src;
	dstlim = dst + dstsiz;
	if (srcp < msg || srcp >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	/* Fetch next label in domain name. */
	while ((n = *srcp++) != 0) {
		/* Check for indirection. */
		switch (n & NS_CMPRSFLGS) {
		case 0:
			/* Limit checks. */
			if (dstp + n + 1 >= dstlim || srcp + n >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			checked += n + 1;
			*dstp++ = n;
			memcpy(dstp, srcp, n);
			dstp += n;
			srcp += n;
			break;

		case NS_CMPRSFLGS:
			if (srcp >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			if (len < 0)
				len = srcp - src + 1;
			srcp = msg + (((n & 0x3f) << 8) | (*srcp & 0xff));
			if (srcp < msg || srcp >= eom) {  /* Out of range. */
				errno = EMSGSIZE;
				return (-1);
			}
			checked += 2;
			/*
			 * Check for loops in the compressed name;
			 * if we've looked at the whole message,
			 * there must be a loop.
			 */
			if (checked >= eom - msg) {
				errno = EMSGSIZE;
				return (-1);
			}
			break;

		default:
			errno = EMSGSIZE;
			return (-1);			/* flag error */
		}
	}
	*dstp = '\0';
	if (len < 0)
		len = srcp - src;
	return (len);
}

/*
 * ns_name_pack(src, dst, dstsiz, dnptrs, lastdnptr)
 *	Pack domain name 'domain' into 'comp_dn'.
 * return:
 *	Size of the compressed name, or -1.
 * notes:
 *	'dnptrs' is an array of pointers to previous compressed names.
 *	dnptrs[0] is a pointer to the beginning of the message. The array
 *	ends with NULL.
 *	'lastdnptr' is a pointer to the end of the array pointed to
 *	by 'dnptrs'.
 * Side effects:
 *	The list of pointers in dnptrs is updated for labels inserted into
 *	the message as we compress the name.  If 'dnptr' is NULL, we don't
 *	try to compress names. If 'lastdnptr' is NULL, we don't update the
 *	list.
 */
int
ns_name_pack(const u_char *src, u_char *dst, int dstsiz,
	     const u_char **dnptrs, const u_char **lastdnptr)
{
	u_char *dstp;
	const u_char **cpp, **lpp, *eob, *msg;
	const u_char *srcp;
	int n, l;

	srcp = src;
	dstp = dst;
	eob = dstp + dstsiz;
	lpp = cpp = NULL;
	if (dnptrs != NULL) {
		if ((msg = *dnptrs++) != NULL) {
			for (cpp = dnptrs; *cpp != NULL; cpp++)
				(void)NULL;
			lpp = cpp;	/* end of list to search */
		}
	} else
		msg = NULL;

	/* make sure the domain we are about to add is legal */
	l = 0;
	do {
		n = *srcp;
		if ((n & NS_CMPRSFLGS) != 0) {
			errno = EMSGSIZE;
			return (-1);
		}
		l += n + 1;
		if (l > MAXCDNAME) {
			errno = EMSGSIZE;
			return (-1);
		}
		srcp += n + 1;
	} while (n != 0);

	/* from here on we need to reset compression pointer array on error */
	srcp = src;
	do {
		/* Look to see if we can use pointers. */
		n = *srcp;
		if (n != 0 && msg != NULL) {
			l = dn_find(srcp, msg, (const u_char * const *)dnptrs,
				    (const u_char * const *)lpp);
			if (l >= 0) {
				if (dstp + 1 >= eob) {
					goto cleanup;
				}
				*dstp++ = (l >> 8) | NS_CMPRSFLGS;
				*dstp++ = l % 256;
				return (dstp - dst);
			}
			/* Not found, save it. */
			if (lastdnptr != NULL && cpp < lastdnptr - 1 &&
			    (dstp - msg) < 0x4000) {
				*cpp++ = dstp;
				*cpp = NULL;
			}
		}
		/* copy label to buffer */
		if (n & NS_CMPRSFLGS) {		/* Should not happen. */
			goto cleanup;
		}
		if (dstp + 1 + n >= eob) {
			goto cleanup;
		}
		memcpy(dstp, srcp, n + 1);
		srcp += n + 1;
		dstp += n + 1;
	} while (n != 0);

	if (dstp > eob) {
cleanup:
		if (msg != NULL)
			*lpp = NULL;
		errno = EMSGSIZE;
		return (-1);
	} 
	return (dstp - dst);
}

/*
 * ns_name_uncompress(msg, eom, src, dst, dstsiz)
 *	Expand compressed domain name to presentation format.
 * return:
 *	Number of bytes read out of `src', or -1 (with errno set).
 * note:
 *	Root domain returns as "." not "".
 */
int
ns_name_uncompress(const u_char *msg, const u_char *eom, const u_char *src,
		   char *dst, size_t dstsiz)
{
	u_char tmp[NS_MAXCDNAME];
	int n;
	
	if ((n = ns_name_unpack(msg, eom, src, tmp, sizeof tmp)) == -1)
		return (-1);
	if (ns_name_ntop(tmp, dst, dstsiz) == -1)
		return (-1);
	return (n);
}

/*
 * ns_name_compress(src, dst, dstsiz, dnptrs, lastdnptr)
 *	Compress a domain name into wire format, using compression pointers.
 * return:
 *	Number of bytes consumed in `dst' or -1 (with errno set).
 * notes:
 *	'dnptrs' is an array of pointers to previous compressed names.
 *	dnptrs[0] is a pointer to the beginning of the message.
 *	The list ends with NULL.  'lastdnptr' is a pointer to the end of the
 *	array pointed to by 'dnptrs'. Side effect is to update the list of
 *	pointers for labels inserted into the message as we compress the name.
 *	If 'dnptr' is NULL, we don't try to compress names. If 'lastdnptr'
 *	is NULL, we don't update the list.
 */
int
ns_name_compress(const char *src, u_char *dst, size_t dstsiz,
		 const u_char **dnptrs, const u_char **lastdnptr)
{
	u_char tmp[NS_MAXCDNAME];

	if (ns_name_pton(src, tmp, sizeof tmp) == -1)
		return (-1);
	return (ns_name_pack(tmp, dst, dstsiz, dnptrs, lastdnptr));
}

/*
 * ns_name_skip(ptrptr, eom)
 *	Advance *ptrptr to skip over the compressed name it points at.
 * return:
 *	0 on success, -1 (with errno set) on failure.
 */
int
ns_name_skip(const u_char **ptrptr, const u_char *eom) {
	const u_char *cp;
	u_int n;

	cp = *ptrptr;
	while (cp < eom && (n = *cp++) != 0) {
		/* Check for indirection. */
		switch (n & NS_CMPRSFLGS) {
		case 0:			/* normal case, n == len */
			cp += n;
			continue;
		case NS_CMPRSFLGS:	/* indirection */
			cp++;
			break;
		default:		/* illegal type */
			errno = EMSGSIZE;
			return (-1);
		}
		break;
	}
	if (cp > eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	*ptrptr = cp;
	return (0);
}

/* Private. */

/*
 * special(ch)
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	is this characted special ("in need of quoting") ?
 * return:
 *	boolean.
 */
static int
special(int ch) {
	switch (ch) {
	case 0x22: /* '"' */
	case 0x2E: /* '.' */
	case 0x3B: /* ';' */
	case 0x5C: /* '\\' */
	/* Special modifiers in zone files. */
	case 0x40: /* '@' */
	case 0x24: /* '$' */
		return (1);
	default:
		return (0);
	}
}

/*
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	convert this character to lower case if it's upper case.
 */
static int
mklower(int ch) {
	if (ch >= 0x41 && ch <= 0x5A)
		return (ch + 0x20);
	return (ch);
}

/*
 * dn_find(domain, msg, dnptrs, lastdnptr)
 *	Search for the counted-label name in an array of compressed names.
 * return:
 *	offset from msg if found, or -1.
 * notes:
 *	dnptrs is the pointer to the first name on the list,
 *	not the pointer to the start of the message.
 */
static int
dn_find(const u_char *domain, const u_char *msg,
	const u_char * const *dnptrs,
	const u_char * const *lastdnptr)
{
	const u_char *dn, *cp, *sp;
	const u_char * const *cpp;
	u_int n;

	for (cpp = dnptrs; cpp < lastdnptr; cpp++) {
		dn = domain;
		sp = cp = *cpp;
		while ((n = *cp++) != 0) {
			/*
			 * check for indirection
			 */
			switch (n & NS_CMPRSFLGS) {
			case 0:			/* normal case, n == len */
				if (n != *dn++)
					goto next;
				for ((void)NULL; n > 0; n--)
					if (mklower(*dn++) != mklower(*cp++))
						goto next;
				/* Is next root for both ? */
				if (*dn == '\0' && *cp == '\0')
					return (sp - msg);
				if (*dn)
					continue;
				goto next;

			case NS_CMPRSFLGS:	/* indirection */
				cp = msg + (((n & 0x3f) << 8) | *cp);
				break;

			default:	/* illegal type */
				errno = EMSGSIZE;
				return (-1);
			}
		}
 next: ;
	}
	errno = ENOENT;
	return (-1);
}

/*
 * MDN support.
 */

static void
mdn_initialize() {
	mdn_resconf_t ctx;

	if (mdn_initialized)
		return;

	mdn_initialized = 1;
	if (mdn_resconf_initialize() != mdn_success)
		return;
	if (mdn_resconf_create(&ctx) != mdn_success)
		return;
	if (mdn_resconf_loadfile(ctx, NULL) != mdn_success)
		return;
	mdn_conf = ctx;
}

static const char *
convert_from_local(const char *local_name, char *dns_name,
		   size_t dns_name_len)
{
	mdn_result_t r;
	char buf1[256], buf2[256];

	mdn_initialize();
	r = mdn_res_localtoucs(mdn_conf, local_name, buf1, sizeof(buf1));
	if (r != mdn_success)
		return (local_name);
	r = mdn_res_normalize(mdn_conf, buf1, buf2, sizeof(buf2));
	if (r != mdn_success)
		return (local_name);
	r = mdn_res_ucstodns(mdn_conf, buf2, dns_name, dns_name_len);
	if (r != mdn_success)
		return (local_name);
	return (dns_name);
}

static const char *
convert_to_local(const char *dns_name, char *local_name,
		 size_t local_name_len)
{
	mdn_result_t r;
	char buf[256];

	mdn_initialize();
	r = mdn_res_dnstoucs(mdn_conf, dns_name, buf, sizeof(buf));
	if (r != mdn_success)
		return (dns_name);
	r = mdn_res_ucstolocal(mdn_conf, buf, local_name, local_name_len);
	if (r != mdn_success)
		return (dns_name);
	return (local_name);
}