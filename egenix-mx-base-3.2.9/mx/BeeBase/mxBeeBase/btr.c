/* Source file extracted from btr.c -- an ANSI C implementation
   included in the source code distribution of 

   SORTING AND SEARCHING ALGORITHMS: A COOKBOOK

   by THOMAS NIEMANN Portland, Oregon 
   email: thomasn@jps.net 
   home: http://members.xoom.com/thomasn/s_man.htm

   From the cookbook:

   Permission to reproduce this document, in whole or in part, is
   given provided the original web site listed below is referenced,
   and no additional restrictions apply. Source code, when part of a
   software project, may be used freely without reference to the
   author.

   Includes modifications by Marc-Andre Lemburg, 1998,
   mal@lemburg.com. See btr.h for details.

*/

/* Logging file used by debugging facility */
#ifndef MAL_DEBUG_OUTPUTFILE
# define MAL_DEBUG_OUTPUTFILE "mxBeeBase.log"
#endif

#include "mxstdlib.h"
#include "btr.h"

/* --- Globals ------------------------------------------------------------ */

/* line number for last IO or memory error */
int bErrLineNo;

/* --- Internal APIs ------------------------------------------------------ */

/*
 *  algorithm:
 *    A B+tree implementation, with keys stored in internal nodes,
 *    and keys/record addresses stored in leaf nodes.  Each node is
 *    one sector in length, except the root node whose length is
 *    3 sectors.  When traversing the tree to insert a key, full
 *    children are adjusted to make room for possible new entries.
 *    Similarly, on deletion, half-full nodes are adjusted to allow for
 *    possible deleted entries.  Adjustments are first done by 
 *    examining 2 nearest neighbors at the same level, and redistibuting
 *    the keys if possible.  If redistribution won't solve the problem,
 *    nodes are split/joined as needed.  Typically, a node is 3/4 full.
 *    On insertion, if 3 nodes are full, they are split into 4 nodes,
 *    each 3/4 full.  On deletion, if 3 nodes are 1/2 full, they are
 *    joined to create 2 nodes 3/4 full.
 *
 *    A LRR (least-recently-read) buffering scheme for nodes is used to
 *    simplify storage management, and, assuming some locality of reference,
 *    improve performance.
 *
 *    To simplify matters, both internal nodes and leafs contain the
 *    same fields.
 *   
 */

/* macros for addressing fields */

/* primitives */
#define bAdr(p) *(bIdxAddr *)(p)
#define eAdr(p) *(bRecAddr *)(p)

/* based on k = &[key,rec,childGE] */
#define childLT(k) bAdr((char *)k - sizeof(bIdxAddr))
#define key(k) (k)
#define rec(k) eAdr((char *)(k) + h->keySize)
#define childGE(k) bAdr((char *)(k) + h->keySize + sizeof(bRecAddr))

/* based on b = &bBuffer */
#define leaf(b) b->p->leaf
#define ct(b) b->p->ct
#define next(b) b->p->next
#define prev(b) b->p->prev
#define fkey(b) &b->p->fkey
#define lkey(b) (fkey(b) + ks((ct(b) - 1)))
#define p(b) (char *)(b->p)

/* shortcuts */
#define ks(ct) ((ct) * h->ks)
#define error(rc) lineError(__LINE__, rc)

static 
bError lineError(int lineno, 
		 bError rc) 
{
    if (rc == bErrIO || rc == bErrMemory)
        if (!bErrLineNo) 
            bErrLineNo = lineno;
    return rc;
}

static 
bIdxAddr allocAdr(bHandle *h) 
{
    bIdxAddr adr;
    adr = h->nextFreeAdr;
    h->nextFreeAdr += h->sectorSize;
    return adr;
}

static 
bError flush(bHandle *h,
	     bBuffer *buf) 
{
    int len;            /* number of bytes to write */

    /* flush buffer to disk */
    len = h->sectorSize;
    if (buf->adr == 0) len *= 3;        /* root */
    if (fseek(h->fp, buf->adr, SEEK_SET)) return error(bErrIO);
    if (fwrite(buf->p, len, 1, h->fp) != 1) return error(bErrIO);
    buf->modified = false;
    h->nDiskWrites++;
    return bErrOk;
}

static 
bError flushAll(bHandle *h) 
{
    bError rc;                /* return code */
    bBuffer *buf;               /* buffer */

    if (h->root.modified)
        if ((rc = flush(h,&h->root)) != 0) return rc;

    buf = h->bufList.next;
    while (buf != &h->bufList) {
        if (buf->modified)
            if ((rc = flush(h,buf)) != 0) return rc;
        buf = buf->next;
    }

    /* Now make sure the data is really written to disk */
    fflush(h->fp);

    return bErrOk;
}

static 
bError assignBuf(bHandle *h,
		 bIdxAddr adr, 
		 bBuffer **b) 
{
    /* assign buf to adr */
    bBuffer *buf;               /* buffer */
    bError rc;                /* return code */

    if (adr == 0) {
        *b = &h->root;
        return bErrOk;
    }

    /* search for buf with matching adr */
    buf = h->bufList.next;
    while (buf->next != &h->bufList) {
        if (buf->valid && buf->adr == adr) break;
        buf = buf->next;
    }

    /* either buf points to a match, or it's last one in list (LRR) */
    if (buf->valid) {
        if (buf->adr != adr) {
            if (buf->modified) {
                if ((rc = flush(h,buf)) != 0) return rc;
            }
            buf->adr = adr;
            buf->valid = false;
        }
    } else {
        buf->adr = adr;
    }

    /* remove from current position and place at front of list */
    buf->next->prev = buf->prev;
    buf->prev->next = buf->next;
    buf->next = h->bufList.next;
    buf->prev = &h->bufList;
    buf->next->prev = buf;
    buf->prev->next = buf;
    *b = buf;
    return bErrOk;
}

static 
bError writeDisk(bHandle *h,
		 bBuffer *buf) 
{
    /* write buf to disk */
    buf->valid = true;
    buf->modified = true;
    return bErrOk;
}

static 
bError readDisk(bHandle *h,
		bIdxAddr adr, 
		bBuffer **b) 
{
    /* read data into buf */
    int len;
    bBuffer *buf;               /* buffer */
    bError rc;                /* return code */

    if ((rc = assignBuf(h, adr, &buf)) != 0) return rc;
    if (!buf->valid) {
        len = h->sectorSize;
        if (adr == 0) len *= 3;         /* root */
        if (fseek(h->fp, adr, SEEK_SET)) return error(bErrIO);
        if (fread(buf->p, len, 1, h->fp) != 1) return error(bErrIO);
        buf->modified = false;
        buf->valid = true;
        h->nDiskReads++;
    }
    *b = buf;
    return bErrOk;
}

static
void dumpBuf(bHandle *h,
	     char *msg,
	     bBuffer *buf) 
{
    unsigned int i;
    bKey *k;

    if (!buf) {
        DPRINTF("\n%s: buf empty\n", msg);
        return;
    }
    k = key(fkey(buf));
    DPRINTF("\n%s: buf[%04lx], ct=%d, leaf=%d", 
            msg, (unsigned long)buf->adr, ct(buf), leaf(buf));
    if (childLT(k)) 
	DPRINTF(", LT(%04lx)", childLT(k));
    if (leaf(buf)) 
	DPRINTF(", prev(%04lx), next(%04lx)", 
		(unsigned long)prev(buf), (unsigned long)next(buf));
    DPRINTF("\n");
    for (i = 0; i < ct(buf); i++) {
        DPRINTF("  key %3d: %08x rec(%08lx)",
		i, *(int *)key(k), rec(k));
        if (childGE(k)) 
	    DPRINTF(" GE(%04lx)", (unsigned long)childGE(k));
        DPRINTF("\n");
        k += ks(1);
    }
}

#define report(rc) reportErr(__LINE__, rc)

static
void reportErr(int lineno, bError rc) 
{
    if (rc == bErrIO || rc == bErrMemory) {
        perror("aborting");
        DPRINTF("line %d: error %d\n", bErrLineNo, rc);
    } else {
        DPRINTF("line %d: error %d\n", lineno, rc);
    }
}

static
int dumpNode(bHandle *h,
	     char *msg,
	     bIdxAddr adr) 
{
    bBuffer *buf;               /* buffer */
    bError rc;                /* return code */
    bKey *k;
    unsigned int i;

    if ((rc = readDisk(h, adr, &buf)) != 0) {
        report(rc);
        return -1;
    }
    dumpBuf(h, msg, buf);
    k = fkey(buf);
    for (i = 0; i < ct(buf); i++) {
        if (childLT(k)) dumpNode(h, msg, childLT(k));
        if (childGE(k)) dumpNode(h, msg, childGE(k));
        k += ks(1);
    }
    return 0;
}

#if 0
static
int dump(bHandle *h,
	 char *msg) 
{
    return dumpNode(h, msg, h->root.adr);
}
#endif

static
int _validateTree(bHandle *h,
		  bBuffer *b,
		  char *visited,
		  int level) 
{
    bKey *k;
    bError rc;
    unsigned int i;
    char p[3*MAX_SECTOR_SIZE];
    bBuffer *cbuf, bufx;
    bBuffer *buf = &bufx;

    if (h->sectorSize > MAX_SECTOR_SIZE) {
	DPRINTF("sectorSize exceeds MAX_SECTOR_SIZE; aborting check\n");
	return -1;
    }
    memcpy(buf, b, sizeof(bBuffer));
    memcpy(p, b->p, 3*h->sectorSize);
    buf->p = (bNode *)p;
    dumpBuf(h,"validate", buf);
    if (visited[buf->adr >> 8]) {
        DPRINTF("previous visit, buf[%04lx]\n", (unsigned long)buf->adr);
        return -1;
    }
    visited[buf->adr >> 8] = 1;
    DPRINTF("\n");
    if (ct(buf)) {
        if (!leaf(buf)) {
            DPRINTF("level %d: recursing on buf[%04lx] LT\n", 
		    level, childLT(fkey(buf)));
            if ((rc = readDisk(h, childLT(fkey(buf)), &cbuf)) != 0) {
                DPRINTF("unable to read buffer %04lx\n", 
			childLT(fkey(buf)));
                return -1;
            }
            if (*(unsigned int *)key(lkey(cbuf)) > *(unsigned int *)key(fkey(buf))) {
                DPRINTF("last element in child buf[%04lx] LT "
			"> first element of parent buf[%04lx]\n", 
			(unsigned long)cbuf->adr, 
			(unsigned long)buf->adr);
                return -1;
            }
            _validateTree(h, cbuf, visited, level+1);
            k = fkey(buf);
            for (i = 0; i < ct(buf); i++) {
                DPRINTF("level %d: recursing on buf[%04lx] GE[%d]\n", 
			level, key(childGE(k)), i);
                if ((rc = readDisk(h, childGE(k), &cbuf)) != 0) {
                    DPRINTF("unable to read buffer %04lx\n", childGE(k));
                    return -1;
                }
                if (*(unsigned int *)key(fkey(cbuf)) < *(unsigned int *)key(k)) {
                    DPRINTF("first element in child buf[%04lx] "
			    "< parent buf[%04lx] GE (%08x < %08x)\n", 
			    (unsigned long)cbuf->adr,
			    (unsigned long)buf->adr,
			    *(int *)key(fkey(cbuf)),
			    *(int *)key(k));

                    dumpBuf(h,"buf", buf);
                    dumpBuf(h,"cbuf", cbuf);
                    return -1;
                }
                if (!leaf(cbuf) && *(unsigned int *)key(fkey(cbuf)) ==  *(unsigned int *)key(k)) {
                    DPRINTF("first element in child buf[%04lx] "
			    "= parent buf[%04lx] GE (%08x < %08x)\n",
			    (unsigned long)cbuf->adr,
			    (unsigned long)buf->adr,
			    *(int *)key(fkey(cbuf)),
			    *(int *)key(k));
                    dumpBuf(h,"buf", buf);
                    dumpBuf(h,"cbuf", cbuf);
                    return -1;
                }
                _validateTree(h,cbuf,visited,level+1);
                k += ks(1);
            }
        }
    }
    return 0;
}

typedef enum { MODE_FIRST, MODE_MATCH } modeEnum;

static 
int search(bHandle *h,
	   bBuffer *buf,
	   void *key, 
	   bRecAddr rec, 
	   bKey **mkey,
	   modeEnum mode) 
{
    /*
     * input:
     *   p                      pointer to node
     *   key                    key to find
     *   rec                    record address (dupkey only)
     *   mode			MODE_FIRST (find first),
     *				MODE_MATCH (rec's must match too) [dupkey only]
     * output:
     *   mkey                   pointer to bKey info
     *
     * returns:
     *   CC_EQ                  key = mkey
     *   CC_LT                  key < mkey
     *   CC_GT                  key > mkey
     */
    int cc = CC_LT;             /* condition code */
    int m;                      /* midpoint of search */
    int lb;                     /* lower-bound of binary search */
    int ub;                     /* upper-bound of binary search */
    bool foundDup;              /* true if found a duplicate key */

    /* Test for empty buffer */
    if (ct(buf) == 0) {
        *mkey = fkey(buf);
        return cc;
    }

    /* Scan current node for key using binary search */
    foundDup = false;
    lb = 0; 
    ub = ct(buf) - 1;
    while (lb <= ub) {
        m = (lb + ub) / 2;
        *mkey = fkey(buf) + ks(m);
        cc = h->comp(h->keySize, key, key(*mkey));
        if (cc < 0)
            /* key less than key[m] */
            ub = m - 1;
        else if (cc > 0)
            /* key greater than key[m] */
            lb = m + 1;
        else {
            /* keys match */
            if (h->dupKeys) {
                switch (mode) {
                case MODE_FIRST:
                    /* backtrack to first key */
                    ub = m - 1;
                    foundDup = true;
                    break;
                case MODE_MATCH:
                    /* rec's must also match */
                    if (rec < rec(*mkey)) {
                        ub = m - 1;
                        cc = CC_LT;
                    } else if (rec > rec(*mkey)) {
                        lb = m + 1;
                        cc = CC_GT;
                    } else {
                        return CC_EQ;
                    }
                    break;
                }
            } else {
		return CC_EQ;
            }
        }
    }

    /* Handle set of duplicates */
    if (h->dupKeys && (mode == MODE_FIRST) && foundDup) {
#if 0
	DPRINTF("found dups: cc=%i, lb=%i, ub=%i, key(*mkey)=%i next=%i\n",
	       cc,lb,ub,*(int*)key(*mkey),*(int*)key((*mkey+ks(1))));
#endif
        if (cc == CC_GT)
	    /* next key is first key */
	    *mkey += ks(1);
        return CC_EQ;
    }

    /* didn't find key */
    return cc < 0 ? CC_LT : CC_GT;
}

static 
bError scatterRoot(bHandle *h)
{
    bBuffer *gbuf;
    bBuffer *root;

    /* scatter gbuf to root */

    root = &h->root;
    gbuf = &h->gbuf;
    memcpy(fkey(root), fkey(gbuf), ks(ct(gbuf)));
    childLT(fkey(root)) = childLT(fkey(gbuf));
    ct(root) = ct(gbuf);
    leaf(root) = leaf(gbuf);
    return bErrOk;
}

static 
bError scatter(bHandle *h,
	       bBuffer *pbuf, 
	       bKey *pkey, 
	       int is, 
	       bBuffer **tmp) 
{
    bBuffer *gbuf;              /* gather buf */
    bKey *gkey;              /* gather buf key */
    bError rc;                /* return code */
    int iu;                     /* number of tmp's used */
    int k0Min;                  /* min #keys that can be mapped to tmp[0] */
    int knMin;                  /* min #keys that can be mapped to tmp[1..3] */
    int k0Max;                  /* max #keys that can be mapped to tmp[0] */
    int knMax;                  /* max #keys that can be mapped to tmp[1..3] */
    int sw;                     /* shift width */
    int len;                    /* length of remainder of buf */
    int base;                   /* base count distributed to tmps */
    int extra;                  /* extra counts */
    int ct;
    int i;

    /*
     * input:
     *   pbuf                   parent buffer of gathered keys
     *   pkey                   where we insert a key if needed in parent
     *   is                     number of supplied tmps
     *   tmp                    array of tmp's to be used for scattering
     * output:
     *   tmp                    array of tmp's used for scattering
     */

    /* scatter gbuf to tmps, placing 3/4 max in each tmp */

    gbuf = &h->gbuf;
    gkey = fkey(gbuf);
    ct = ct(gbuf);

   /****************************************
    * determine number of tmps to use (iu) *
    ****************************************/
    iu = is;

    /* determine limits */
    if (leaf(gbuf)) {
        /* minus 1 to allow for insertion */
        k0Max= h->maxCt - 1;
        knMax= h->maxCt - 1;
        /* plus 1 to allow for deletion */
        k0Min= (h->maxCt / 2) + 1;
        knMin= (h->maxCt / 2) + 1;
    } else {
        /* can hold an extra gbuf key as it's translated to a LT pointer */
        k0Max = h->maxCt - 1;
        knMax = h->maxCt;
        k0Min = (h->maxCt / 2) + 1;
        knMin = ((h->maxCt+1) / 2) + 1;
    }

    /* calculate iu, number of tmps to use */
    while(1) {
        if (iu == 0 || ct > (k0Max + (iu-1)*knMax)) {
            /* add a buffer */
            if ((rc = assignBuf(h, allocAdr(h), &tmp[iu])) != 0) 
                return rc;
            /* update sequential links */
            if (leaf(gbuf)) {
                /* adjust sequential links */
                if (iu == 0) {
                    /* no tmps supplied when splitting root for first time */
                    prev(tmp[0]) = 0;
                    next(tmp[0]) = 0;
                } else {
                    prev(tmp[iu]) = tmp[iu-1]->adr;
                    next(tmp[iu]) = next(tmp[iu-1]);
                    next(tmp[iu-1]) = tmp[iu]->adr;
                }
            }
            iu++;
            h->nNodesIns++;
        } else if (iu > 1 && ct < (k0Min + (iu-1)*knMin)) {
            /* del a buffer */
            iu--;
            /* adjust sequential links */
            if (leaf(gbuf) && tmp[iu-1]->adr) {
                next(tmp[iu-1]) = next(tmp[iu]);
            }
            next(tmp[iu-1]) = next(tmp[iu]);
            h->nNodesDel++;
        } else {
            break;
        }
    }

    /* establish count for each tmp used */
    base = ct / iu;
    extra = ct % iu;
    for (i = 0; i < iu; i++) {
        int n;

        n = base;
        /* distribute extras, one at a time */
        /* don't do to 1st node, as it may be internal and can't hold it */
        if (i && extra) {
            n++;
            extra--;
        }
        ct(tmp[i]) = n;
    }


    /**************************************
     * update sequential links and parent *
     **************************************/
    if (iu != is) {
        /* link last node to next */
        if (leaf(gbuf) && next(tmp[iu-1])) {
            bBuffer *buf;
            if ((rc = readDisk(h, next(tmp[iu-1]), &buf)) != 0) return rc;
            prev(buf) = tmp[iu-1]->adr;
            if ((rc = writeDisk(h, buf)) != 0) return rc;
        }
        /* shift keys in parent */
        sw = ks(iu - is);
        if (sw < 0) {
            len = ks(ct(pbuf)) - (pkey - fkey(pbuf)) + sw;
            memmove(pkey, pkey - sw, len);
        } else {
            len = ks(ct(pbuf)) - (pkey - fkey(pbuf));
            memmove(pkey + sw, pkey, len);
        }
        /* don't count LT buffer for empty parent */
        if (ct(pbuf))
            ct(pbuf) += iu - is;
        else
            ct(pbuf) += iu - is - 1;
    }

   /*******************************
    * distribute keys to children *
    *******************************/
    for (i = 0; i < iu; i++) {

        /* update LT pointer and parent nodes */
        if (leaf(gbuf)) {
            /* update LT, tmp[i] */
            childLT(fkey(tmp[i])) = 0;

            /* update parent */
            if (i == 0) {
                childLT(pkey) = tmp[i]->adr;
            } else {
                memcpy(pkey, gkey, ks(1));
                childGE(pkey) = tmp[i]->adr;
                pkey += ks(1);
            }
        } else {
            if (i == 0) {
                /* update LT, tmp[0] */
                childLT(fkey(tmp[i])) = childLT(gkey);
                /* update LT, parent */
                childLT(pkey) = tmp[i]->adr;
            } else {
                /* update LT, tmp[i] */
                childLT(fkey(tmp[i])) = childGE(gkey);
                /* update parent key */
                memcpy(pkey, gkey, ks(1));
                childGE(pkey) = tmp[i]->adr;
                gkey += ks(1);
                pkey += ks(1);
                ct(tmp[i])--;
            }
        }

        /* install keys, tmp[i] */
        memcpy(fkey(tmp[i]), gkey, ks(ct(tmp[i])));
        leaf(tmp[i]) = leaf(gbuf);

        gkey += ks(ct(tmp[i]));
    }
    leaf(pbuf) = false;

   /************************
    * write modified nodes *
    ************************/
    if ((rc = writeDisk(h, pbuf)) != 0) return rc;
    for (i = 0; i < iu; i++)
        if ((rc = writeDisk(h, tmp[i])) != 0) return rc;

    return bErrOk;
}

static 
bError gatherRoot(bHandle *h)
{
    bBuffer *gbuf;
    bBuffer *root;

    /* gather root to gbuf */
    root = &h->root;
    gbuf = &h->gbuf;
    memcpy(p(gbuf), root->p, 3 * h->sectorSize);
    leaf(gbuf) = leaf(root);
    ct(root) = 0;
    return bErrOk;
}

static 
bError gather(bHandle *h, 
	      bBuffer *pbuf, 
	      bKey **pkey, 
	      bBuffer **tmp) 
{
    bError rc;                /* return code */
    bBuffer *gbuf;
    bKey *gkey;

    /*
     * input:
     *   pbuf                   parent buffer
     *   pkey                   pointer to match key in parent
     * output:
     *   tmp                    buffers to use for scatter
     *   pkey                   pointer to match key in parent
     * returns:
     *   bErrOk                 operation successful
     * notes:
     *   Gather 3 buffers to gbuf.  Setup for subsequent scatter by
     *   doing the following:
     *     - setup tmp buffer array for scattered buffers
     *     - adjust pkey to point to first key of 3 buffers
     */

    /* find 3 adjacent buffers */
    if (*pkey == lkey(pbuf))
        *pkey -= ks(1);
    if ((rc = readDisk(h, childLT(*pkey), &tmp[0])) != 0) return rc;
    if ((rc = readDisk(h, childGE(*pkey), &tmp[1])) != 0) return rc;
    if ((rc = readDisk(h, childGE(*pkey + ks(1)), &tmp[2])) != 0) return rc;

    /* gather nodes to gbuf */
    gbuf = &h->gbuf;
    gkey = fkey(gbuf);

    /* tmp[0] */
    childLT(gkey) = childLT(fkey(tmp[0]));
    memcpy(gkey, fkey(tmp[0]), ks(ct(tmp[0])));
    gkey += ks(ct(tmp[0]));
    ct(gbuf) = ct(tmp[0]);

    /* tmp[1] */
    if (!leaf(tmp[1])) {
        memcpy(gkey, *pkey, ks(1));
        childGE(gkey) = childLT(fkey(tmp[1]));
        ct(gbuf)++;
        gkey += ks(1);
    }
    memcpy(gkey, fkey(tmp[1]), ks(ct(tmp[1])));
    gkey += ks(ct(tmp[1]));
    ct(gbuf) += ct(tmp[1]);

    /* tmp[2] */
    if (!leaf(tmp[2])) {
        memcpy(gkey, *pkey+ks(1), ks(1));
        childGE(gkey) = childLT(fkey(tmp[2]));
        ct(gbuf)++;
        gkey += ks(1);
    }
    memcpy(gkey, fkey(tmp[2]), ks(ct(tmp[2])));
    ct(gbuf) += ct(tmp[2]);

    leaf(gbuf) = leaf(tmp[0]);

    return bErrOk;
}

/* --- Interface --------------------------------------------------------- */

bError bOpen(bDescription info,
	     bHandle **handle) 
{
    bError rc;                	/* return code */
    int bufCt;                  /* number of tmp buffers */
    bBuffer *buf;               /* buffer */
    int maxCt;                  /* maximum number of keys in a node */
    bBuffer *root;
    int i;
    bNode *p;
    bHandle *h;

    if ((info.sectorSize < sizeof(bNode)) 
	|| (info.sectorSize % 4)
	|| (info.sectorSize > MAX_SECTOR_SIZE))
        return bErrSectorSize;

    /* determine sizes and offsets */
    /* leaf/n, prev, next, [childLT,key,rec]... childGE */
    /* ensure that there are at least 3 children/parent for gather/scatter */
    maxCt = info.sectorSize - (sizeof(bNode) - sizeof(bKey));
    maxCt /= sizeof(bIdxAddr) + info.keySize + sizeof(bRecAddr);
    if (maxCt < 6) return bErrSectorSize;

    /* copy parms to bHandle */
    if ((h = calloc(sizeof(bHandle),1)) == NULL) return error(bErrMemory);
    h->keySize = info.keySize;
    h->dupKeys = info.dupKeys;
    h->sectorSize = info.sectorSize;
    h->comp = info.comp;

    /* childLT, key, rec */
    h->ks = sizeof(bIdxAddr) + h->keySize + sizeof(bRecAddr);
    h->maxCt = maxCt;

    /* Allocate buflist.
     * During insert/delete, need simultaneous access to 7 buffers:
     *  - 4 adjacent child bufs
     *  - 1 parent buf
     *  - 1 next sequential link
     *  - 1 lastGE
     */
    bufCt = 7 /*+ EXTRA_BUFFERS*/;
    if ((h->malloc1 = calloc(bufCt * sizeof(bBuffer),1)) == NULL) 
        return error(bErrMemory);
    buf = h->malloc1;

    /*
     * Allocate bufs.
     * We need space for the following:
     *  - bufCt buffers, of size sectorSize
     *  - 1 buffer for root, of size 3*sectorSize
     *  - 1 buffer for gbuf, size 3*sectorsize + 2 extra keys
     *    to allow for LT pointers in last 2 nodes when gathering 3 full nodes
     */
    if ((h->malloc2 = calloc((bufCt+6) * h->sectorSize + 2 * h->ks,1)) == NULL) 
        return error(bErrMemory);
    p = h->malloc2;

    /* initialize buflist */
    h->bufList.next = buf;
    h->bufList.prev = buf + (bufCt - 1);
    for (i = 0; i < bufCt; i++) {
        buf->next = buf + 1;
        buf->prev = buf - 1;
        buf->modified = false;
        buf->valid = false;
        buf->p = p;
        p = (bNode *)((char *)p + h->sectorSize);
        buf++;
    }
    h->bufList.next->prev = &h->bufList;
    h->bufList.prev->next = &h->bufList;

    /* initialize root */
    root = &h->root;
    root->p = p;
    p = (bNode *)((char *)p + 3*h->sectorSize);
    h->gbuf.p = p;      /* done last to include extra 2 keys */

    /* Open the file */
    switch (info.filemode) {

    case 1: /* Open in read-only mode */
	if ((h->fp = fopen(info.iName, "rb")) != NULL) {
	    /* open an existing database */
	    if ((rc = readDisk(h, 0, &root)) != 0) return rc;
	    if (fseek(h->fp, 0, SEEK_END)) return error(bErrIO);
	    if ((h->nextFreeAdr = ftell(h->fp)) == -1) return error(bErrIO);
	}
	else {
	    free(h);
	    return bErrFileNotOpen;
	}
	break;

    case 0: /* Open in update mode, revert to creating a new file */
    case 3: /* Open an existing file in update mode, fail if non-existing */
	if ((h->fp = fopen(info.iName, "r+b")) != NULL) {
	    /* open an existing database */
	    if ((rc = readDisk(h, 0, &root)) != 0) return rc;
	    if (fseek(h->fp, 0, SEEK_END)) return error(bErrIO);
	    if ((h->nextFreeAdr = ftell(h->fp)) == -1) return error(bErrIO);
	    break;
	}
	else if (info.filemode == 3) {
	    free(h);
	    return bErrFileNotOpen;
	}
	/* On error and filemode 0: fall through */

    case 2: /* Create a new file */
	if ((h->fp = fopen(info.iName, "w+b")) != NULL) {
	    /* initialize root */
	    memset(root->p, 0, 3*h->sectorSize);
	    leaf(root) = 1;
	    root->modified = true;
	    h->nextFreeAdr = 3 * h->sectorSize;
	    /* flush buffers to create a valid file stub */
	    flushAll(h);
	    break;
	}
	/* On error: fall through */
	
    default:
        /* Something's wrong */
        free(h);
        return bErrFileNotOpen;
    }

    *handle = h;
    return bErrOk;
}

bError bFlush(bHandle *h)
{
    if (h == NULL) return bErrOk;

    /* flush idx */
    if (h->fp) {
        flushAll(h);
    }
    return bErrOk;
}

bError bClose(bHandle *h) 
{
    if (h == NULL) return bErrOk;

    /* flush idx */
    if (h->fp) {
        flushAll(h);
        fclose(h->fp);
    }

    if (h->malloc2) free(h->malloc2);
    if (h->malloc1) free(h->malloc1);
    free(h);
    return bErrOk;
}

bError bFindKey(bHandle *h, 
		bCursor *c,
		void *key, 
		bRecAddr *rec) 
{
    bKey *mkey = 0;            	/* matched key */
    bBuffer *buf;               /* buffer */
    bError rc;                	/* return code */

    buf = &h->root;

    /* find key, and return address */
    while (1) {
        if (leaf(buf)) {
	    int cc;
	    
            if ((cc=search(h, buf, key, 0, &mkey, MODE_FIRST)) == CC_EQ) {
                if (rec) 
		    *rec = rec(mkey);
                c->buffer = buf; 
		c->key = mkey;
                return bErrOk;
            } else {
		DPRINTF("not found; cc=%i\n", cc);
		
                return bErrKeyNotFound;
            }
        } else {
            if (search(h, buf, key, 0, &mkey, MODE_FIRST) == CC_LT) {
                if ((rc = readDisk(h, childLT(mkey), &buf)) != 0) 
		    return rc;
            } else {
                if ((rc = readDisk(h, childGE(mkey), &buf)) != 0) 
		    return rc;
            }
        }
    }
}

bError bInsertKey(bHandle *h, 
		  void *key, 
		  bRecAddr rec) 
{
    int rc;                     /* return code */
    bKey *mkey;              	/* match key */
    int len;                    /* length to shift */
    int cc;                     /* condition code */
    bBuffer *buf, *root;
    bBuffer *tmp[4];
    unsigned int keyOff;
    bool lastGEvalid;           /* true if GE branch taken */
    bool lastLTvalid;           /* true if LT branch taken after GE branch */
    bIdxAddr lastGE = 0;        /* last childGE traversed */
    unsigned int lastGEkey = 0; /* last childGE key traversed */
    int height;                 /* height of tree */

    root = &h->root;
    lastGEvalid = false;
    lastLTvalid = false;

    /* check for full root */
    if (ct(root) == 3 * h->maxCt) {
        /* gather root and scatter to 4 bufs */
        /* this increases b-tree height by 1 */
        if ((rc = gatherRoot(h)) != 0) return rc;
        if ((rc = scatter(h, root, fkey(root), 0, tmp)) != 0) return rc;
    }
    buf = root;
    height = 0;
    while(1) {
        if (leaf(buf)) {
            /* in leaf, and there' room guaranteed */

            if (height > h->maxHeight) h->maxHeight = height;

            /* set mkey to point to insertion point */
            switch (search(h, buf, key, rec, &mkey, MODE_MATCH)) {
            case CC_LT:  /* key < mkey */
		if (ct(buf) == 0)
		    break;
                if (!h->dupKeys && 
		    h->comp(h->keySize, key, mkey) == CC_EQ)
                    return bErrDupKeys;
                break;
            case CC_EQ:  /* key = mkey */
                return bErrDupKeys;
                break;
            case CC_GT:  /* key > mkey */
                if (!h->dupKeys && 
		    h->comp(h->keySize, key, mkey) == CC_EQ)
                    return bErrDupKeys;
                mkey += ks(1);
                break;
            }

            /* shift items GE key to right */
            keyOff = mkey - fkey(buf);
            len = ks(ct(buf)) - keyOff;
            if (len) memmove(mkey + ks(1), mkey, len);

            /* insert new key */
            memcpy(key(mkey), key, h->keySize);
            rec(mkey) = rec;
            childGE(mkey) = 0;
            ct(buf)++;
            if ((rc = writeDisk(h, buf)) != 0) return rc;

            /* if new key is first key, then fixup lastGE key */
            if (!keyOff && lastLTvalid) {
                bBuffer *tbuf;
                bKey *tkey;
                if ((rc = readDisk(h, lastGE, &tbuf)) != 0) return rc;
                tkey = fkey(tbuf) + lastGEkey;
                memcpy(key(tkey), key, h->keySize);
                rec(tkey) = rec;
                if ((rc = writeDisk(h, tbuf)) != 0) return rc;
            }
            h->nKeysIns++;
            break;

        } else {
            /* internal node, descend to child */
            bBuffer *cbuf;      /* child buf */

            height++;
          
            /* read child */
            if ((cc = search(h, buf, key, rec, &mkey, MODE_MATCH)) == CC_LT) {
                if ((rc = readDisk(h, childLT(mkey), &cbuf)) != 0) return rc;
            } else {
                if ((rc = readDisk(h, childGE(mkey), &cbuf)) != 0) return rc;
            }

            /* check for room in child */
            if (ct(cbuf) == h->maxCt) {

                /* gather 3 bufs and scatter */
                if ((rc = gather(h, buf, &mkey, tmp)) != 0) return rc;
                if ((rc = scatter(h, buf, mkey, 3, tmp)) != 0) return rc;

                /* read child */
                if ((cc = search(h, buf, key, rec, &mkey, MODE_MATCH)) == CC_LT) {
                    if ((rc = readDisk(h, childLT(mkey), &cbuf)) != 0) 
			return rc;
                } else {
                    if ((rc = readDisk(h, childGE(mkey), &cbuf)) != 0) 
			return rc;
                }
            }
            if (cc >= 0 || mkey != fkey(buf)) {
                lastGEvalid = true;
                lastLTvalid = false;
                lastGE = buf->adr;
                lastGEkey = mkey - fkey(buf);
                if (cc < 0) lastGEkey -= ks(1);
            } else {
                if (lastGEvalid) lastLTvalid = true;
            }
            buf = cbuf;
        }
    }

#if BTREE_DEBUG
    DPRINTF("I %i = 0x%08x -> %i = 0x%08x\n",
	    *(long*)key,*(long*)key,(long)rec,(long)rec);
#endif

    return bErrOk;
}

bError bUpdateKey(bHandle *h, 
		  void *key, 
		  bRecAddr rec) 
{
    int rc;                     /* return code */
    bKey *mkey = 0;            	/* match key */
    int cc;                     /* condition code */
    bBuffer *buf, *root;

    if (h->dupKeys)
	return bErrNotWithDupKeys;
    
    root = &h->root;

    buf = root;
    while(1) {
        if (leaf(buf)) {
            if (search(h, buf, key, rec, &mkey, MODE_MATCH) != CC_EQ)
		return bErrKeyNotFound;

            /* update record */
            rec(mkey) = rec;
            if ((rc = writeDisk(h, buf)) != 0) return rc;

            h->nKeysUpd++;
            break;

        } else {
            bBuffer *cbuf;      /* child buf */

            /* read child */
            if ((cc = search(h, buf, key, rec, &mkey, MODE_MATCH)) == CC_LT) {
                if ((rc = readDisk(h, childLT(mkey), &cbuf)) != 0) return rc;
            } else {
                if ((rc = readDisk(h, childGE(mkey), &cbuf)) != 0) return rc;
            }

            if (cc == CC_EQ) {
		/* update internal key copy too */
		rec(mkey) = rec;
            }
            buf = cbuf;
        }
    }

#if BTREE_DEBUG
    DPRINTF("U %i = 0x%08x -> %i = 0x%08x\n",
	    *(long*)key,*(long*)key,(long)rec,(long)rec);
#endif

    return bErrOk;
}

bError bDeleteKey(bHandle *h, 
		  void *key, 
		  bRecAddr *rec) 
{
    int rc;                     /* return code */
    bKey *mkey;              /* match key */
    int len;                    /* length to shift */
    int cc;                     /* condition code */
    bBuffer *buf;               /* buffer */
    bBuffer *tmp[4];
    unsigned int keyOff;
    bool lastGEvalid = false;   /* true if GE branch taken */
    bool lastLTvalid = false;   /* true if LT branch taken after GE branch */
    bIdxAddr lastGE = 0;        /* last childGE traversed */
    unsigned int lastGEkey = 0; /* last childGE key traversed */
    bBuffer *root;
    bBuffer *gbuf;

    root = &h->root;
    gbuf = &h->gbuf;
    lastGEvalid = false;
    lastLTvalid = false;

    buf = root;
    while(1) {
        if (leaf(buf)) {

            /* set mkey to point to deletion point */
            if (search(h, buf, key, *rec, &mkey, MODE_MATCH) == CC_EQ)
                *rec = rec(mkey);
            else
                return bErrKeyNotFound;

            /* shift items GT key to left */
            keyOff = mkey - fkey(buf);
            len = ks(ct(buf)-1) - keyOff;
            if (len) memmove(mkey, mkey + ks(1), len);
            ct(buf)--;
            if ((rc = writeDisk(h, buf)) != 0) return rc;

            /* if deleted key is first key, then fixup lastGE key */
            if (!keyOff && lastLTvalid) {
                bBuffer *tbuf;
                bKey *tkey;
                if ((rc = readDisk(h, lastGE, &tbuf)) != 0) return rc;
                tkey = fkey(tbuf) + lastGEkey;
                memcpy(key(tkey), mkey, h->keySize);
                rec(tkey) = rec(mkey);
                if ((rc = writeDisk(h, tbuf)) != 0) return rc;
            }
            h->nKeysDel++;
            break;
        } else {
            /* internal node, descend to child */
            bBuffer *cbuf;      /* child buf */
          
            /* read child */
            if ((cc = search(h, buf, key, *rec, &mkey, MODE_MATCH)) == CC_LT) {
                if ((rc = readDisk(h, childLT(mkey), &cbuf)) != 0) return rc;
            } else {
                if ((rc = readDisk(h, childGE(mkey), &cbuf)) != 0) return rc;
            }

            /* check for room to delete */
            if (ct(cbuf) == h->maxCt/2) {

                /* gather 3 bufs and scatter */
                if ((rc = gather(h, buf, &mkey, tmp)) != 0) return rc;

                /* if last 3 bufs in root, and count is low enough... */
                if (buf == root
                && ct(root) == 2 
                && ct(gbuf) < (3*(3*h->maxCt))/4) {
                    /* collapse tree by one level */
                    scatterRoot(h);
                    h->nNodesDel += 3;
                    continue;
                }

                if ((rc = scatter(h, buf, mkey, 3, tmp)) != 0) return rc;

                /* read child */
                if ((cc = search(h, buf, key, *rec, &mkey, MODE_MATCH)) == CC_LT) {
                    if ((rc = readDisk(h, childLT(mkey), &cbuf)) != 0) 
			return rc;
                } else {
                    if ((rc = readDisk(h, childGE(mkey), &cbuf)) != 0) 
			return rc;
                }
            }
            if (cc >= 0 || mkey != fkey(buf)) {
                lastGEvalid = true;
                lastLTvalid = false;
                lastGE = buf->adr;
                lastGEkey = mkey - fkey(buf);
                if (cc < 0) lastGEkey -= ks(1);
            } else {
                if (lastGEvalid) lastLTvalid = true;
            }
            buf = cbuf;
        }
    }

#if BTREE_DEBUG
    DPRINTF("D %i = 0x%08x -> %i = 0x%08x\n",
	    *(long*)key,*(long*)key,(long)rec,(long)rec);
#endif

    return bErrOk;
}

bError bFindFirstKey(bHandle *h, 
		     bCursor *c,
		     void *key, 
		     bRecAddr *rec) 
{
    bError rc;                /* return code */
    bBuffer *buf;               /* buffer */

    buf = &h->root;
    while (!leaf(buf)) {
        if ((rc = readDisk(h, childLT(fkey(buf)), &buf)) != 0) return rc;
    }
    if (ct(buf) == 0) return bErrKeyNotFound;
    if (key) memcpy(key, key(fkey(buf)), h->keySize);
    if (rec) *rec = rec(fkey(buf));
    c->buffer = buf; c->key = fkey(buf);
    return bErrOk;
}

bError bFindLastKey(bHandle *h, 
		    bCursor *c,
		    void *key, 
		    bRecAddr *rec) 
{
    bError rc;                /* return code */
    bBuffer *buf;               /* buffer */

    buf = &h->root;
    while (!leaf(buf)) {
        if ((rc = readDisk(h, childGE(lkey(buf)), &buf)) != 0) return rc;
    }
    if (ct(buf) == 0) return bErrKeyNotFound;
    if (key) memcpy(key, key(lkey(buf)), h->keySize);
    if (rec) *rec = rec(lkey(buf));
    c->buffer = buf; c->key = lkey(buf);
    return bErrOk;
}

bError bFindNextKey(bHandle *h, 
		    bCursor *c,
		    void *key, 
		    bRecAddr *rec) 
{
    bError rc;                /* return code */
    bKey *nkey;              /* next key */
    bBuffer *buf;               /* buffer */

    if ((buf = c->buffer) == NULL) return bErrKeyNotFound;
    if (c->key == lkey(buf)) {
        /* current key is last key in leaf node */
        if (next(buf)) {
            /* fetch next set */
            if ((rc = readDisk(h, next(buf), &buf)) != 0) return rc;
            nkey = fkey(buf);
        } else {
            /* no more sets */
            return bErrKeyNotFound;
        }
    } else {
        /* bump to next key */
        nkey = c->key + ks(1);
    }
    if (key) memcpy(key, key(nkey), h->keySize);
    if (rec) *rec = rec(nkey);
    c->buffer = buf; c->key = nkey;
    return bErrOk;
}

bError bFindPrevKey(bHandle *h, 
		    bCursor *c,
		    void *key, 
		    bRecAddr *rec)
{
    bError rc;                /* return code */
    bKey *pkey;              /* previous key */
    bKey *fkey;              /* first key */
    bBuffer *buf;               /* buffer */

    if ((buf = c->buffer) == NULL) return bErrKeyNotFound;
    fkey = fkey(buf);
    if (c->key == fkey) {
        /* current key is first key in leaf node */
        if (prev(buf)) {
            /* fetch previous set */
            if ((rc = readDisk(h, prev(buf), &buf)) != 0) return rc;
            pkey = fkey(buf) + ks((ct(buf) - 1));
        } else {
            /* no more sets */
            return bErrKeyNotFound;
        }
    } else {
        /* bump to previous key */
        pkey = c->key - ks(1);
    }
    if (key) memcpy(key, key(pkey), h->keySize);
    if (rec) *rec = rec(pkey);
    c->buffer = buf; c->key = pkey;
    return bErrOk;
}

bError bCursorReadData(bHandle *h, 
		       bCursor *c,
		       void *key, 
		       bRecAddr *rec)
{
    if (c->buffer == NULL || !c->buffer->valid)
	return bErrBufferInvalid;
    if (key) memcpy(key, key(c->key), h->keySize);
    if (rec) *rec = rec(c->key);
    return bErrOk;
}

int bValidateTree(bHandle *h) 
{
    char *visited;

    visited = (char*)calloc(10240,1);
    if (!visited)
	return -1;
    flushAll(h);
    DPRINTF("Validating BTree with handle %0lx, root buffer at %0lx",
	    (long)h,(long)&h->root);
    return _validateTree(h,&h->root,visited,1);
}

#if 0

static
int comp(const void *key1, const void *key2) {
    unsigned int const *p1;
    unsigned int const *p2;
    p1 = key1; p2 = key2;
    return (*p1 == *p2) ? CC_EQ : (*p1 > *p2 ) ? CC_GT : CC_LT;
}

#define DO(xyz) \
    if ((rc = xyz) != bErrOk) {				\
        DPRINTF("Error in line %d: rc = %d\n", __LINE__, rc);	\
        exit(0);					\
    }

int main(void) {
    bDescription info;
    bHandle *handle;
    bCursor c;
    bError rc;
    long key;
    bRecAddr value;

    remove("t1.dat");

    info.iName = "t1.dat";
    info.keySize = sizeof(int);
    info.dupKeys = false;
    info.sectorSize = 256;
    info.comp = comp;

    DO (bOpen(info, &handle));

    key = 0x123;
    value = 0x456;
    DO (bInsertKey(handle, &key, value));

    DO (bFindKey(handle, &c, &key, &value));
    DPRINTF("Found key %x with value %x\n",key,value);

    bFlush(handle);
    DPRINTF("Buffers flushed.\n");

    key = 0x222;
    value = 0xbeef;
    DO (bInsertKey(handle, &key, value));

    key = 0x123;
    DO (bFindKey(handle, &c, &key, &value));
    DPRINTF("Found key %x with value %x\n",key,value);

    key = 0x222;
    DO (bFindKey(handle, &c, &key, &value));
    DPRINTF("Found key %x with value %x\n",key,value);

    DPRINTF("statistics:\n");
    DPRINTF("    maximum height: %8d\n", handle->maxHeight);
    DPRINTF("    nodes inserted: %8d\n", handle->nNodesIns);
    DPRINTF("    nodes deleted:  %8d\n", handle->nNodesDel);
    DPRINTF("    keys inserted:  %8d\n", handle->nKeysIns);
    DPRINTF("    keys deleted:   %8d\n", handle->nKeysDel);
    DPRINTF("    disk reads:     %8d\n", handle->nDiskReads);
    DPRINTF("    disk writes:    %8d\n", handle->nDiskWrites);

    bClose(handle);

    /* Second time... */
    DPRINTF("\nSecond time...\n\n");

    DO (bOpen(info, &handle));

    key = 0x123;
    DO (bFindKey(handle, &c, &key, &value));
    DPRINTF("Found key %x with value %x\n",key,value);

    key = 0x222;
    DO (bFindKey(handle, &c, &key, &value));
    DPRINTF("Found key %x with value %x\n",key,value);

    DPRINTF("statistics:\n");
    DPRINTF("    maximum height: %8d\n", handle->maxHeight);
    DPRINTF("    nodes inserted: %8d\n", handle->nNodesIns);
    DPRINTF("    nodes deleted:  %8d\n", handle->nNodesDel);
    DPRINTF("    keys inserted:  %8d\n", handle->nKeysIns);
    DPRINTF("    keys deleted:   %8d\n", handle->nKeysDel);
    DPRINTF("    disk reads:     %8d\n", handle->nDiskReads);
    DPRINTF("    disk writes:    %8d\n", handle->nDiskWrites);

    bClose(handle);

    return 0;
}

#endif
