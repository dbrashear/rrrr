/* bitset.c : compact enumerable bit array */
#include "bitset.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

inline void bitset_reset(BitSet *self) {
    memset(self->chunks, 0, sizeof(uint64_t) * self->nchunks);
}

void bitset_init(BitSet *self, int capacity) {
    self->capacity = capacity;
    self->nchunks = capacity / 64;
    if (capacity % 64) 
        self->nchunks += 1;
    self->chunks = malloc(sizeof(uint64_t) * self->nchunks);
    if (self->chunks == NULL) {
        printf("bitset chunk allocation failure.");
        exit(1);
    }
    bitset_reset(self);
}

BitSet *bitset_new(int capacity) {
    BitSet *bs = malloc(sizeof(BitSet));
    if (bs == NULL) {
        printf("bitset allocation failure.");
        exit(1);
    }
    bitset_init(bs, capacity);
    return bs;
}

static inline void index_check(BitSet *self, int index) {
    if (index >= self->capacity) {
        printf("bitset index %d out of range [0, %d)\n", index, self->capacity);
        exit(1);    
   }
}

void bitset_set(BitSet *self, int index) {
    index_check(self, index);
    uint64_t bitmask = 1ull << (index % 64);
    self->chunks[index / 64] |= bitmask;
}

void bitset_clear(BitSet *self, int index) {
    index_check(self, index);
    uint64_t bitmask = ~(1ull << (index % 64));
    self->chunks[index / 64] &= bitmask;
}

bool bitset_get(BitSet *self, int index) {
    index_check(self, index);
    uint64_t bitmask = 1ull << (index % 64); // need to specify that literal 1 is >= 64 bits wide.
    return self->chunks[index / 64] & bitmask;
}

void bitset_dump(BitSet *self) {
    for (int i = 0; i < self->capacity; ++i)
        if (bitset_get(self, i))
            printf("%d ", i);
    printf("\n\n");
}

int bitset_enumerate(BitSet *self) {
    BitSetIterator bsi;
    bitset_iter_begin(&bsi, self);
    int total = 0;
    int elem;
    while ((elem = bitset_iter_next(&bsi)) >= 0)
        //printf ("%d ", elem);
        total += elem;
    return total;
}

int bitset_enumerate2(BitSet *self) {
    int total = 0;
    for (int elem = bitset_next_set_bit(self, 0); elem >= 0; elem = bitset_next_set_bit(self, elem + 1)) {
        //printf ("%d ", elem); 
        total += elem;
    }
    return total;
}

void bitset_destroy(BitSet *self) {
    free(self->chunks);
    free(self);
}

void bitset_iter_begin(BitSetIterator *iter, BitSet *bs) {
    iter->chunk = bs->chunks - 1;
    iter->mask = 0; 
    iter->index = -1;
    iter->capacity = bs->capacity;
}

int bitset_iter_next_old(BitSetIterator *bsi) {

    if (bsi->index >= bsi->capacity)
        return -1;

    /* find next set bit in current chunk, if it exists */
    do {
        bsi->mask <<= 1; // no-op on first call because mask is 0
        bsi->index += 1; // brings index to 0 on first call
        // NB: also terminates when bit is pushed off end of register
    } while (bsi->mask && !(bsi->mask & *(bsi->chunk))); 
    
    /* spin forward to next chunk containing a set bit, if no set bit was found */
    if ( ! bsi->mask) {
        while ((bsi->index < bsi->capacity) && (*(bsi->chunk) == 0)) {
            ++(bsi->chunk);
            bsi->index += 64;
        }
        if (bsi->index < bsi->capacity) {
            // now iterating within a 64-bit field known to have a set bit
            bsi->mask = 1ull;
            while ( !(bsi->mask & *(bsi->chunk)) ) { 
                bsi->mask <<= 1;
                bsi->index += 1;
            }
        }
    } 
    
    /* here either the index is out of range, or we have found a set bit */
    if (bsi->index < bsi->capacity)
        return bsi->index;        
    else
        return -1;

}

int bitset_iter_next(BitSetIterator *bsi) {

    while (bsi->index < bsi->capacity) {
        /* find next set bit in current chunk, if it exists */
        bsi->mask <<= 1; // no-op on first call because mask is 0
        bsi->index += 1; // brings index to 0 on first call
        //printf(" idx %d \n", bsi->index);
        /* begin a new 64-bit chunk (including first call) */
        if (bsi->mask == 0) {
            bsi->mask = 1ull;
            ++(bsi->chunk);
            //printf(" chunk %llx", *(bsi->chunk));
            /* spin forward to next chunk containing a set bit, if no set bit was found */
            while ( ! *(bsi->chunk) ) {
                ++(bsi->chunk);
                bsi->index += 64;
                if (bsi->index >= bsi->capacity)
                    return -1;
            }
        }
    
        if (bsi->mask & *(bsi->chunk)) {
            return bsi->index;
        }
    }
    return -1;
}

inline int bitset_next_set_bit(BitSet *bs, int index) {
    uint64_t *chunk = bs->chunks + (index >> 6);
    uint64_t mask = 1ull << (index & 0x3F);
    while (index < bs->capacity) {
        /* check current bit */
        if (mask & *chunk)
            return index;
        /* move to next bit in current chunk */
        mask <<= 1; // no-op on first call because mask is 0
        index += 1; // brings index to 0 on first call
        /* begin a new chunk */
        if (mask == 0) {
            mask = 1ull;
            ++chunk;
            /* spin forward to next chunk containing a set bit, if no set bit was found */
            while ( ! *chunk ) {
                ++chunk;
                index += 64;
                if (index >= bs->capacity)
                    return -1;
            }
        }
    }
    return -1;
}

/*

Enumerating very sparse 50kbit bitset 100k times:
10.592 sec without skipping chunks == 0
0.210 sec with skipping

Enumerating very dense 50kbit bitset 100k times:
20.525 sec without skipping
20.599 sec with skipping

So no real slowdown from skipping checks, but great improvement on sparse sets.
Maybe switching to 32bit ints would allow finer-grained skipping.

Why is the dense set so much slower? Probably function call overhead (1 per result). 
For dense set:
Adding inline keyword and -O2 reduces to 6.1 seconds.
Adding inline keyword and -O3 reduces to 7.6 seconds (note this is higher than -O2).
Adding -O2 without inline keyword reduces to 9.8 seconds.
Adding -O3 without inline keyword reduces to 7.6 seconds.

So enumerating the dense set 8 times takes roughly 0.0005 sec.

For sparse set:
Adding inline keyword and -O2 reduces to 0.064 seconds (0.641 sec for 1M enumerations).

*/

int test_main (void) {
    int max = 50000;
    BitSet *bs = bitset_new(max);
    for (int i = 0; i < 50000; i += 2)
        bitset_set(bs, i);
    for (int i = 0; i < 100000; i++) {
        bitset_enumerate2(bs);
    }
    bitset_destroy(bs);
    return 0;
}
