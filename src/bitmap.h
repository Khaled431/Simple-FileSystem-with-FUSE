//
// Created by abdel on 4/28/2018.
//

#ifndef ASSIGNMENT3_BITMAP_H
#define ASSIGNMENT3_BITMAP_H

#ifdef bitmap_64
#define bitmap_type unsigned long long int
#define bitmap_shift        6
#define bitmap_mask        63
#define bitmap_wordlength  64
#define bitmap_fmt "%016llx"
#else	// assumed to be 32 bits
#define bitmap_type unsigned int
#define bitmap_shift        5
#define bitmap_mask        31
#define bitmap_wordlength  32
#define bitmap_fmt "%08x"
#endif

typedef struct {
    int bits, numPartitions;
    bitmap_type *container;
} BitMap;

void bitmap_set(BitMap *map, int position);

void bitmap_clear(BitMap *map, int position);

int bitmap_get(BitMap *map, int position);

BitMap *bitmap_allocate(int bits);

void bitmap_deallocate(BitMap *map);

void bitmap_print(BitMap *map);

#endif //ASSIGNMENT3_BITMAP_H
