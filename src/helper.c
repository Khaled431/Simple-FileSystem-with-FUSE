//
// Created by abdel on 4/29/2018.
//

#include "helper.h"
#include "sfs.h"
#include "bitmap.h"
#include "bytebuffer.h"

int flush_super() {
    ByteBuffer *byteBuffer = allocate_n(BLOCK_SIZE);
    if (!byteBuffer) {
        fprintf(stderr, "Could not allocate byte buffer.\n");
        return -1;
    }

    writeInt(byteBuffer, (__uint32_t) superBlock->numFreeBlocks);
    writeByte(byteBuffer, (__uint8_t) superBlock->numFreeINodes);

    int numPartitions = superBlock->blockBitMap->numPartitions;
    writeInt(byteBuffer, (__uint32_t) numPartitions);
    for (; numPartitions >= 0; numPartitions--) {
        writeInt(byteBuffer, superBlock->blockBitMap->container[numPartitions]);
    }

    numPartitions = superBlock->iNodeBitMap->numPartitions;
    writeInt(byteBuffer, (__uint32_t) numPartitions);
    for (; numPartitions >= 0; numPartitions--) {
        writeInt(byteBuffer, superBlock->iNodeBitMap->container[numPartitions]);
    }

    if (block_write(SUPER_BLOCK_INDEX, byteBuffer->buffer) <= 0) {
        return -1;
    }

    free(byteBuffer);
    return 0;
}

int flush_iNode(INode *node) {
    ByteBuffer *byteBuffer = allocate_n(BLOCK_SIZE);
    if (!byteBuffer) {
        return -1;
    }

    writeLong(byteBuffer, node->id);
    writeInt(byteBuffer, node->userId);
    writeInt(byteBuffer, node->groupId);
    writeInt(byteBuffer, node->st_mode);
    writeLong(byteBuffer, (size_t) node->lastFileModTime.tv_sec);
    writeLong(byteBuffer, (size_t) node->lastAccessTime.tv_sec);
    writeLong(byteBuffer, (size_t) node->lastModifiedTime.tv_sec);
    writeLong(byteBuffer, (size_t) node->numFileLinks);
    writeLong(byteBuffer, node->fileSize);

    int index = 0;
    int numBlockLinks = sizeof(node->blockLinks) / sizeof(short);
    for (; index < numBlockLinks; index++) {
        writeShort(byteBuffer, (__uint16_t) node->blockLinks[index]);
    }

    if (block_write((const int) (INODE_BLOCK_START + node->id), byteBuffer->buffer) <= 0) {
        return -1;
    }

//    free(byteBuffer->buffer);
    free(byteBuffer);

    return 0;
}

INode *findINode(const char *absolutePath) {
    Directory *directory = findDirectory(rootDirectory, "", absolutePath);
    if (!directory) {
        return NULL;
    }

    return iNodeList + directory->entry->ino;
}

void node_stat(INode *node, ino_t id, mode_t st_mode, nlink_t numFileLinks) {
    node->id = id;

    node->userId = getuid();
    node->groupId = getuid();

    node->lastAccessTime.tv_sec = time(NULL);
    node->lastFileModTime.tv_sec = time(NULL);
    node->lastModifiedTime.tv_sec = time(NULL);

    node->st_mode = st_mode;
    if (node->id == ROOT_INODE_ID)
        fprintf(stderr, "ST_MODE: %u %d\n", node->st_mode, S_ISDIR(node->st_mode));
    node->numFileLinks = numFileLinks;

    int index = 0;
    int numDataBlocks = sizeof(node->blockLinks) / sizeof(short);
    for (; index < numDataBlocks; index++) {
        node->blockLinks[index] = -1;
    }
}

int node_destroy(INode *node) {
    if (node->id == ROOT_INODE_ID) {
        return EACCES; // Deny this operation.
    }

    int position = 0;
    int numLinks = sizeof(node->blockLinks) / sizeof(short);
    for (; position < numLinks; position++) {
        int block = node->blockLinks[position];
        if (block == -1) {
            continue;
        }

        char buffer[BLOCK_SIZE];
        memset(buffer, 0, BLOCK_SIZE);

        if (block_write(block, buffer) <= 0) { // Empty out those disk blocks!
            return EFAULT;
        }

        block_unreserve(block);
    }

    //TODO destroy the directory entry as well!

    node_stat(node, node->id, S_IFREG | S_IRUSR | S_IWUSR | S_IXUSR, 0); // <--- no files are linked to it anymore
    node_unreserve(node);

    if (flush_iNode(node) < 0) {
        return EFAULT;
    }


    return 0;
}

void node_reserve(INode *node) {
    BitMap *map = superBlock->iNodeBitMap;
    if (!map) {
        return;
    }

    if (bitmap_get(map, (int) node->id)) { // If the node is already taken, we don't want to continue.
        return;
    }

    bitmap_set(map, (int) node->id);
    superBlock->numFreeINodes--;
}

void node_unreserve(INode *node) {
    BitMap *map = superBlock->iNodeBitMap;
    if (!map) {
        return;
    }

    unsigned int position = (unsigned int) (node->id - INODE_BLOCK_START);
    if (!bitmap_get(map, position)) {
        return;
    }

    bitmap_clear(map, position);
    superBlock->numFreeINodes++;
}

bool isReservedNode(INode *node) {
    BitMap *map = superBlock->iNodeBitMap;
    if (!map) {
        return false;
    }

    return (bool) bitmap_get(map, (int) node->id);
}


ReserveBlock block_reserve(INode *node) {
    ReserveBlock reserveBlock;

    reserveBlock.nextDataBlock = -1;
    reserveBlock.nextLink = -1;

    int nextDataBlock = nextFreeDataBlock();
    if (nextDataBlock == -1) {
        return reserveBlock;
    }

    int nextLink = nextFreeLink(node->blockLinks);
    if (nextLink == -1) {
        return reserveBlock;
    }

    reserveBlock.nextDataBlock = nextDataBlock;
    reserveBlock.nextLink = nextLink;

    node->blockLinks[nextLink] = (short) nextDataBlock; // Link the given i-node to the block that we are reserving it for.

    bitmap_set(superBlock->blockBitMap, nextDataBlock - DATA_BLOCK_START);
    superBlock->numFreeBlocks--;
    return reserveBlock;
}

void block_unreserve(int block) {
    BitMap *map = superBlock->blockBitMap;

    int position = block - DATA_BLOCK_START;
    if (!bitmap_get(map, position)) {
        return;
    }

    bitmap_clear(map, position);
    superBlock->numFreeBlocks++;
}

Directory *directory_allocate(ino_t ino, const char *entryName) {
    Directory *directory = (Directory *) malloc(sizeof(Directory));
    if (!directory) {
        return NULL;
    }

    DirectoryEntry *entry = directory->entry = (DirectoryEntry *) malloc(sizeof(DirectoryEntry));
    if (!entry) {
        fprintf(stderr, "Could not allocate entry in directory_allocate\n");
        return NULL;
    }

    entry->ino = ino;
    _strcpy(entry->entryName, entryName);

    directory->child = NULL;
    directory->sibling = NULL;

    return directory;

    //TODO decide if we need to reserve the block that holds this information now or later
}

void *parseDirectory(void *lambda(Directory *, void *), Directory *directory, void *extra) {
    if (!directory) {
        return NULL;
    }

    void *ret = lambda(directory, extra);
    if (ret) {
        return ret;
    }

    void *sibling = parseDirectory(lambda, directory->sibling, extra); // Goes to last sibling then returns
    if (sibling) {
        return sibling;
    }

    void *child = parseDirectory(lambda, directory->child, extra); // Goes to last child then returns
    if (child) {
        return child;
    }

    return NULL;
}

void *saveDirectory(Directory *directory) { //TODO give access to super block to helper.c
    ByteBuffer *byteBuffer = allocate_n(BLOCK_SIZE);
    if (!byteBuffer) {
        return NULL;
    }

    writeString(byteBuffer, directory->entry->entryName);
    writeShort(byteBuffer, (__uint16_t) directory->entry->ino);

    _Bool siblingExists = directory->sibling ? true : false;
    _Bool childExists = directory->child ? true : false;

    writeShort(byteBuffer, (__uint16_t) (siblingExists ? directory->sibling->entry->ino : -1));
    writeShort(byteBuffer, (__uint16_t) (childExists ? directory->child->entry->ino : -1));

    INode *node = iNodeList + directory->entry->ino;

    if (block_write(node->blockLinks[0], byteBuffer->buffer) <= 0) {
        return NULL;
    }

//    free(buffer->buffer);
    free(byteBuffer);
    return NULL;
}

void *loadDirectory(Directory *directory) {
    DirectoryEntry *entry = directory->entry;

    _Bool root = !entry ? true : false;
    if (root) {
        entry = directory->entry = (DirectoryEntry *) malloc(sizeof(DirectoryEntry));
        if (!entry) {
            return NULL;
        }

        entry->ino = ROOT_INODE_ID;
        _strcpy(entry->entryName, "/");
    }

    char buffer[BLOCK_SIZE];
    memset(buffer, 0, BLOCK_SIZE);

    INode *node = iNodeList + entry->ino;
    if (!node || block_read(node->blockLinks[0], buffer) <= 0) {
        return NULL;
    }

    ByteBuffer *byteBuffer = allocate((Byte *) buffer, 0, BLOCK_SIZE);
    if (!byteBuffer) {
        return NULL;
    }

    char *entryName = readString(byteBuffer);
    if (entryName) {
        _strcpy(entry->entryName, entryName);
        free(entryName);
    }
    entry->ino = (ino_t) readShort(byteBuffer);

    short sibling_ino = readShort(byteBuffer);
    if (sibling_ino != -1) {
        Directory *sibling = directory->sibling = directory_allocate(sibling_ino, "");
        if (!sibling) {
            return NULL;
        }

        sibling->entry->ino = (ino_t) sibling_ino;
    }

    short child_ino = readShort(byteBuffer);
    if (child_ino != -1) {
        Directory *child = directory->child = directory_allocate(child_ino, "");
        if (!child) {
            return NULL;
        }

        child->entry->ino = (ino_t) child_ino;
    }

    free(byteBuffer->buffer);
    free(byteBuffer);

    return NULL;
}

Directory *findParentDirectory(Directory *directory, const char *absolutePath) {
    return findDirectory(directory, "", strTruncDelim(absolutePath));
}

Directory *findDirectory(Directory *directory, char *relativePath, const char *absolutePath) {
    if (!directory) {
        return NULL;
    }

    INode *node = iNodeList + directory->entry->ino;
    if (!node) {
        return NULL;
    }

    char nextRelativePath[PATH_MAX];
    sprintf(nextRelativePath, "%s%s", relativePath, directory->entry->entryName);

    fprintf(stderr, "NEXT RELATIVE PATH: %s\t%s\n", nextRelativePath, absolutePath);

    if (strcmp(absolutePath, nextRelativePath) == 0) {
        fprintf(stderr, "FOUND: %s\t%s\n", nextRelativePath, absolutePath);
        return directory;
    }

    char *subset = strstr(absolutePath, nextRelativePath); // If the nextRelativePath is a subset of absolute path
    if (subset) {
        fprintf(stderr, "SUBSET: FOUND: %s\t%s\n", nextRelativePath, absolutePath);
    }

    Directory *sib = findDirectory(directory->sibling, subset ? nextRelativePath : relativePath, absolutePath);
    if (sib) {
        return sib;
    }

    Directory *child = findDirectory(directory->child, subset ? nextRelativePath : relativePath, absolutePath);
    if (child) {
        return child;
    }

    fprintf(stderr, "NOT FOUND: %s\t%s\n", nextRelativePath, absolutePath);
    return NULL;
}

Directory *findLastSibling(Directory *parent) {
    Directory *child = parent->child;
    if (!child) {
        return NULL; // Means that there is no child.
    }

    for (; child->sibling; child = child->sibling);

    return child;
}

int nextFreeLink(const short links[]) {
    int position = 0;
    size_t numLinks = sizeof(links);
    for (; position < numLinks; position++) {
        int id = links[position];
        if (id == -1)
            return position;
    }

    return -1;
}

int nextFreeBit(BitMap *map) {
    int position = 0;
    for (; position < map->bits; position++) {
        if (!bitmap_get(map, position)) {
            return position;
        }
    }

    return -1;
}

ino_t nextFreeINode() {
    return (ino_t) nextFreeBit(superBlock->iNodeBitMap);
}

unsigned short nextFreeDataBlock() {
    return (unsigned short) (nextFreeBit(superBlock->blockBitMap) + DATA_BLOCK_START);
}

const char *strTruncDelim(const char *absolutePath) {
    if (strcmp(absolutePath, "/") == 0) {
        return "/"; // The root has no parent path so it would just be the empty string, we don't want that!
    }

    size_t len = strlen(absolutePath);


    bool endsWithDelim = absolutePath[len - 1] == '/';
    if (endsWithDelim) {
        len--;
    }

    size_t position = len - 1;
    for (; position >= 0; position--) {
        char character = absolutePath[position];
        if (character == '/') {
            break;
        }
    }

    if (position == -1) {
        return NULL;
    }

    static char parentPath[PATH_MAX];
    memset(parentPath, 0, PATH_MAX);

    _strcpy_n(parentPath, absolutePath, position + 1);

    return parentPath;
}

const char *absoluteToEntry(const char *absolutePath) {
    if (strcmp(absolutePath, "/") == 0) {
        return "/"; // The root has no parent path so it would just be the empty string, we don't want that!
    }

    size_t len = strlen(absolutePath);

    bool endsWithDelim = absolutePath[len - 1] == '/';
    if (endsWithDelim) {
        len--;
    }

    size_t position = len - 1;
    for (; position >= 0; position--) {
        char character = absolutePath[position];
        if (character == '/') {
            break;
        }
    }

    if (position == -1) {
        return NULL;
    }

    static char parentPath[PATH_MAX];
    memset(parentPath, 0, PATH_MAX);

    size_t realLen = strlen(absolutePath);
    _strcpy_n(parentPath, absolutePath + position + 1, realLen - position - 1);

    return parentPath;
}

void _strcpy(char *dest, const char *src) {
    _strcpy_n(dest, src, -1L);
}

void _strcpy_n(char *dest, const char *src, long trunc) {
    int pos = 0;

    while (pos != trunc && src[pos] != '\0') {
        dest[pos] = src[pos];
        pos++;
    }
    dest[pos] = '\0';
}

int _split(const char *str, char delim, char ***tokens) { // TODO remove don't think im going to use it.
    int *tklen, *t, count = 1;
    char **arr, *p = (char *) str;

    while (*p != '\0') if (*p++ == delim) count += 1;
    t = tklen = calloc(count, sizeof(int));
    for (p = (char *) str; *p != '\0'; p++) *p == delim ? *t++ : (*t)++;
    *tokens = arr = malloc(count * sizeof(char *));
    t = tklen;
    p = *arr++ = calloc((size_t) (*(t++) + 1), sizeof(char *));
    while (*str != '\0') {
        if (*str == delim) {
            p = *arr++ = calloc((size_t) (*(t++) + 1), sizeof(char *));
            str++;
        } else *p++ = *str++;
    }
    free(tklen);
    return count;
}



