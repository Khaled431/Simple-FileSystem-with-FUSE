#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include "bytebuffer.h"

ByteBuffer *allocate(Byte *bufferSource, int writerPosition, size_t length) {
    ByteBuffer *buffer = (ByteBuffer *) malloc(sizeof(ByteBuffer));
    if (!buffer) {
        return NULL;
    }

    buffer->writerPosition = writerPosition;
    buffer->readerPosition = 0;

    buffer->buffer = (Byte *) calloc(length, sizeof(Byte));
    if (!buffer->buffer)
        return NULL;

    memcpy(buffer->buffer, bufferSource, sizeof(Byte) * length);
    return buffer;
}

ByteBuffer *allocate_n(size_t length) {
    Byte buffer[length];
    memset(buffer, 0, sizeof(Byte) * length);

    return allocate(buffer, 0, length);
}

int mergeBuffers(ByteBuffer *from, ByteBuffer *to) {
    int mergedLength = (to->writerPosition + from->writerPosition);
    if (mergedLength < 0) {
        return 0;
    }

    Byte *bufferRealloc = (Byte *) realloc(to->buffer, sizeof(Byte) * mergedLength);
    if (!bufferRealloc) {
        return 0;
    }

    to->buffer = bufferRealloc;
    memcpy((to->buffer + to->writerPosition), from->buffer, from->writerPosition);
    to->writerPosition = mergedLength;
    return 1;
}

__uint8_t getByte(ByteBuffer *buffer, int index) {
    return (__uint8_t) buffer->buffer[index];
}

void writeByte(ByteBuffer *buffer, __uint8_t value) {
    buffer->buffer[buffer->writerPosition++] = ((__uint8_t) (value & 0xFF));
}

void writeShort(ByteBuffer *buffer, __uint16_t value) {
    writeByte(buffer, (__uint8_t) (value >> 8)); // 2
    writeByte(buffer, (__uint8_t) (value & 0xFF)); //-50
}

void writeInt(ByteBuffer *buffer, __uint32_t value) {
    writeByte(buffer, (__uint8_t) (value >> 24));
    writeByte(buffer, (__uint8_t) (value >> 16));
    writeByte(buffer, (__uint8_t) (value >> 8));
    writeByte(buffer, (__uint8_t) (value & 0xFF));
}

void writeLong(ByteBuffer *buffer, __uint64_t value) {
    writeByte(buffer, (__uint8_t) (value >> 56));
    writeByte(buffer, (__uint8_t) (value >> 48));
    writeByte(buffer, (__uint8_t) (value >> 40));
    writeByte(buffer, (__uint8_t) (value >> 32));
    writeByte(buffer, (__uint8_t) (value >> 24));
    writeByte(buffer, (__uint8_t) (value >> 16));
    writeByte(buffer, (__uint8_t) (value >> 8));
    writeByte(buffer, (__uint8_t) value);
}

void writeString(ByteBuffer *buffer, char *string) {
    int length = 0;
    while (string[length] != '\0') {
        length++;
    }

    if (strlen(string) != length) {
        printf("%lu %d\n", strlen(string), length);
    }

    if (length == 0)
        return;

    int count = 0;
    for (; count < length; count++) {
        writeByte(buffer, string[count]);
    }
    writeByte(buffer, 0);
}

__uint8_t readByte(ByteBuffer *buffer) {
    return buffer->buffer[buffer->readerPosition++];
}

__uint16_t readShort(ByteBuffer *buffer) {
    return (readByte(buffer) << 8) | readByte(buffer);
}

__uint32_t readInt(ByteBuffer *buffer) {
    return (readByte(buffer) << 24) | (readByte(buffer) << 16) | (readByte(buffer) << 8) | readByte(buffer);
}

__uint64_t readLong(ByteBuffer *buffer) {
    size_t l = (size_t) (readInt(buffer) & 0xffffffffL);
    size_t l1 = (size_t) (readInt(buffer) & 0xffffffffL);
    return (size_t) ((l << 32) + l1);
}

char *readString(ByteBuffer *buffer) {
    int length = 0;
    while (getByte(buffer, length + buffer->readerPosition) != '\0') {
        length++;
    }

    char *string = (char *) calloc(length + 1, sizeof(char));
    if (!string)
        return NULL;

    int index = 0;
    for (; index <= length; index++) {
        string[index] = (char) readByte(buffer);
    }

    return string;
}
