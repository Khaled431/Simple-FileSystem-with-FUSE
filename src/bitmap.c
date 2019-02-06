#include <stdio.h>
#include <stdlib.h>

#include "bitmap.h"

// get the types right
#define bitmap_one        (bitmap_type)1

void bitmap_set(BitMap *map, int position) {
    int word = position >> bitmap_shift;
    int shift = position & bitmap_mask;
    map->container[word] |= bitmap_one << shift;
}

void bitmap_clear(BitMap *map, int position) {
    int word = position >> bitmap_shift;
    int shift = position & bitmap_mask;
    map->container[word] &= ~(bitmap_one << shift);
}

int bitmap_get(BitMap *map, int position) {
    int word = position >> bitmap_shift;
    int shift = position & bitmap_mask;
    return (map->container[word] >> shift) & bitmap_one;
}

BitMap *bitmap_allocate(int bits) {
    BitMap *map = (BitMap *) malloc(sizeof(BitMap));
    if (!map) {
        return NULL;
    }

    map->bits = bits;
    map->numPartitions = (bits + bitmap_wordlength - 1) / bitmap_wordlength;

    fprintf(stderr, "Partitions: %d\n", map->numPartitions);

    map->container = (bitmap_type *) calloc((size_t) map->numPartitions, sizeof(bitmap_type));
    if (!map->container)
        return NULL;

    return map;
}

void bitmap_deallocate(BitMap *map) {
    free(map->container);
    free(map);
}

void bitmap_print(BitMap *map) {
    int index = 0;
    for (; index < map->numPartitions; index++) {
        printf(" " bitmap_fmt, map->container[index]);
    }
    printf("\n");
}