//
// Created by abdel on 12/7/2017.
//

#ifndef SORTER_SERVER_BYTEBUFFER_H
#define SORTER_SERVER_BYTEBUFFER_H

#include "bytebuffer.h"

typedef char Byte;

typedef struct {
    int readerPosition, writerPosition;
    Byte *buffer;
} ByteBuffer;

ByteBuffer *allocate(Byte *, int, size_t);

ByteBuffer *allocate_n(size_t);

__uint8_t getByte(ByteBuffer*, int);

int mergeBuffers(ByteBuffer *, ByteBuffer *);

void writeByte(ByteBuffer *, __uint8_t);

void writeShort(ByteBuffer *, __uint16_t);

void writeInt(ByteBuffer *, __uint32_t);

void writeLong(ByteBuffer *, __uint64_t);

void writeString(ByteBuffer *, char *);

__uint8_t readByte(ByteBuffer *);

__uint16_t readShort(ByteBuffer *);

__uint32_t readInt(ByteBuffer *);

__uint64_t readLong(ByteBuffer *);

char *readString(ByteBuffer *);

#endif //SORTER_SERVER_BYTEBUFFER_H

