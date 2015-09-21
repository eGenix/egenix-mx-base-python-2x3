#!/usr/bin/env python

""" Calculate the sectorsize for various keysizes.

    See the conditions in btr.c:
    
    if ((info.sectorSize < sizeof(bNode))
        || (info.sectorSize % 4)
        || (info.sectorSize > MAX_SECTOR_SIZE))
        return bErrSectorSize;
   
    /* ensure that there are at least 3 children/parent for gather/scatter */
    maxCt = info.sectorSize - (sizeof(bNode) - sizeof(bKey));
    maxCt /= sizeof(bIdxAddr) + info.keySize + sizeof(bRecAddr);
    if (maxCt < 6) return bErrSectorSize;

    MAX_SECTOR_SIZE is 1024.

    For keysize = 25 on a 64-bit platform:

    (gdb) print maxCt
    $4 = 5
    (gdb) print info.sectorSize
    $5 = 256
    (gdb) print sizeof(bNode)
    $6 = 40
    (gdb) print sizeof(bKey)
    $7 = 1
    (gdb) print sizeof(bIdxAddr)
    $8 = 8
    (gdb) print info.keySize
    $9 = 26
    (gdb) print sizeof(bRecAddr)
    $10 = 8
    (gdb) print sizeof(bIdxAddr) + info.keySize + sizeof(bRecAddr)
    $11 = 42
    (gdb) print info.sectorSize - (sizeof(bNode) - sizeof(bKey))
    $12 = 217

"""
from mx.BeeBase import BeeIndex

### Globals

# Debug level
_debug = 0

# Allowed sector sizes; these have to be == 0 mod 4 and should ideally
# be multiples of disk sector sizes for better performance
SECTOR_SIZES = (
    256,
    512,
    1024,
    2048,
    4096,
    )

###

def calc_maxCt(sectorSize,
               keySize,
               sizeof_bNode=BeeIndex.sizeof_bNode,
               sizeof_bKey=BeeIndex.sizeof_bKey,
               sizeof_bRecAddr=BeeIndex.sizeof_bRecAddr,
               sizeof_bIdxAddr=BeeIndex.sizeof_bIdxAddr):
    maxCt = sectorSize - (sizeof_bNode - sizeof_bKey)
    maxCt /= sizeof_bIdxAddr + keySize + sizeof_bRecAddr
    return maxCt

def find_sectorsizes():

    l = []
    # Sanity check
    for sectorsize in SECTOR_SIZES:
        assert sectorsize % 4 == 0, 'invalid sectorsize %i' % sectorsize
    # Find keysizes
    for keysize in range(1, SECTOR_SIZES[-1]):
        # The keysize used in mxBeeBase's Python interface does
        # not include the terminating 0-byte, so add one to the
        # keysize
        bytesize = keysize + 1
        for sectorsize in SECTOR_SIZES:
            maxct = calc_maxCt(sectorsize,
                               bytesize)
            if maxct < 6:
                continue
            else:
                l.append((keysize, sectorsize))
                break
        else:
            # Max. sectorsize reached, stop searching
            if _debug:
                print ('WARNING: no sectorsize found for keysize=%i' % keysize)
            break
    return l

###

if __name__ == '__main__':
    print ('mxBeeBase BeeIndex - Valid sectorsizes for various keysizes')
    print ('')
    print ('keysize : sectorsize')
    print ('--------------------')
    for (keysize, sectorsize) in find_sectorsizes():
        print ('%-8i: %i' % (keysize, sectorsize))

          
