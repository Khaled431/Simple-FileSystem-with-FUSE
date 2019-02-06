//
// Created by abdel on 4/29/2018.
//

#ifndef ASSIGNMENT3_HELPER_H
#define ASSIGNMENT3_HELPER_H

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "bytebuffer.h"
#include "fuse.h"
#include "log.h"
#include "bitmap.h"

/**
 * The total allocation size of the file disk.
 */
#define ALLOCATION_BYTES 16777216

/**
 * The minimum number of i-nodes to be allocated.
 */
#define NUM_INODE_BLOCKS 0x80

/**
 * Block size adjusted by the metadata of the block.
 */
#define ADJUSTED_BLOCK_SIZE (BLOCK_SIZE - sizeof(Block))

/**
 * The number of blocks allocated.
 */
#define NUM_TOTAL_BLOCKS ALLOCATION_BYTES / ADJUSTED_BLOCK_SIZE

#define NUM_DATA_BLOCKS (NUM_TOTAL_BLOCKS - NUM_INODE_BLOCKS - 1)

/**
 * The position of the super block.
 */
#define SUPER_BLOCK_INDEX 0

/**
 * The identifer of the beggining i_node.
 */
#define ROOT_INODE_ID 0

/**
 * The position of the first i-node.
 */
#define INODE_BLOCK_START 1

/**
 * The default number of directories for the file system.
 */
#define DEFAULT_NUM_DIRECTORIES 1

/**
 * The position of the first data block.
 */
#define DATA_BLOCK_START (INODE_BLOCK_START + NUM_INODE_BLOCKS)

typedef struct timespec timestruc_t;

typedef char *Block;

/**
 * I-node contains: owner, type (directory, file, device), last modified
 * time, last accessed time, last I-node modified time, access
 * permissions, number of links to the file, size, and block pointers
 */
typedef struct {

    /**
     *  The user id of the file's owner.
     */
    uid_t userId; // TODO find out what owner represents (more clarification needed)

    /**
     * The group id of the file's owner.
     */
    gid_t groupId;

    /**
     * Type (directory, file, device)
     */
    mode_t st_mode;

    /**
     * The identifer of the i-node.
     */
    ino_t id;

    /**
     * Last FILE modified time.
     */
    timestruc_t lastFileModTime;

    /**
     * Last accessed time.
     */
    timestruc_t lastAccessTime;

    /**
    * Last iNode modified time.
    */
    timestruc_t lastModifiedTime;

    /**
     * Number of links to the file.
     */
    nlink_t numFileLinks;

    /**
     * The size of the file in bytes.
     */
    size_t fileSize;

    /**
     * The list of blocks to support.
     */
    short blockLinks[200];
} INode;

/**
 * Info stored on the SB: size of the file system, number of free blocks,
 * list of free blocks, index to the next free block, size of the I-node list,
 *number of free I-nodes, list of free I-nodes, index to the next free
 *I-node, locks for free block and free I-node lists, and flag to indicate
 *a modification to the SB
 */
typedef struct {
    /**
    * List of free blocks.
    */
    BitMap *blockBitMap;

    /**
     * List of free I-nodes.
     */
    BitMap *iNodeBitMap;

    /**
     * Number of free blocks
     */
    long numFreeBlocks;

    /**
     * Number of free I-nodes
     */
    short numFreeINodes;
} SuperBlock;

/**
 * Each entry has an inode number, record length (the
 * total bytes for the name plus any left over space), string length (the actual
 * length of the name), and finally the name of the entry
 */
typedef struct {//TODO the length of an entry's name is the strlen + the zero operator.

    /**
     * The number of the i-node.
     */
    ino_t ino;

    /**
     * The name of the entry.
     */
    char entryName[NAME_MAX];
} DirectoryEntry;

/**
 * Contains a list of (entry name, inode number) pair
 */
typedef struct Directory { //TODO this structure requires marshaling as it uses pointers.

    /**
     * The entry of this directory.
     */
    DirectoryEntry *entry;

    /**
     * The parent of this directory.
     */
    struct Directory *parent;

    /**
     * The next sibling of this directory. TODO maybe make this an array instead?
     */
    struct Directory *sibling;

    /**
     * The next child of this directory.
     */
    struct Directory *child;
} Directory;

typedef struct {
    int nextDataBlock;
    int nextLink;
} ReserveBlock;

/**
 * Pointer to the root of the 'root' directory.
 */
Directory *rootDirectory;
/**
 * Pointer to the super block.
 */
SuperBlock *superBlock; //TODO we need to synchro

/**
 * Array of i'nodes. Pointer to the first i-node in the list.
 */
INode *iNodeList;

/**
 * Mutex for the sfs_init method.
 */
pthread_mutex_t init_mutex;

/**
 * Given a super block, write it's disk block.
 * @return If the super block was sucessfully written.
 */
int flush_super();

/**
 * Given an i-node, write it's disk block.
 * @return If the disk block was sucessfully written.
 */
int flush_iNode(INode *);

/**
 * Given a path, find the current i-node.
 * @return The i-node linked to this path.
 */
INode *findINode(const char *);

/**
 * Populate's the attributes of the i-node.
 */
void node_stat(INode *, ino_t, mode_t, nlink_t);

/**
 * Destroys the entire node, it's block-list and it's directory.
 */
int node_destroy(INode *node);

/**
 * Reserves the i-node.
 */
void node_reserve(INode *);

/**
 * Unreserved the i-node.
 */
void node_unreserve(INode *);

/**
 * Reserves a data block.
 * @return 0 on success, -1 on failure.
 */
ReserveBlock block_reserve(INode *);

/**
 * Release the data block from the bitmap.
 */
void block_unreserve(int block);

/**
 * Reserves
 */

/**
 * Returns whether or not a node has been reserved.
 * @return 0 isReserved = True, 1 isReserved = False, -1 bitmap allocation error.
 */
_Bool isReservedNode(INode *);

/**
 * Creates a directory.
 */
Directory *directory_allocate(ino_t, const char *);

/**
 * Parses through a directory, directory function is requried here, can only take one argument.
 */
void *parseDirectory(void *(Directory *, void *), Directory *, void *extra);

/**
 * Gets the directory's information from the specified block. TODO maybe an i-node parameter?
 * @return
 */
void *loadDirectory(Directory *);

/**
 * Save the given directory.
 * @return
 */
void *saveDirectory(Directory *);

/**
 * Find's a parent directory given a path.
 * @return The directory that was matched.
 */
Directory *findParentDirectory(Directory *, const char *);

/**
 * Find's a directory given a path.
 * @return The directory that was matched.
 */
Directory *findDirectory(Directory *, char *, const char *);

/**
 * Find the last sibling of a given directory.
 */
Directory *findLastSibling(Directory *);

/**
 * Returns the next free i-node link.
 * @return The next i-node link.
 */
int nextFreeLink(const short[]);

/**
 * Returns the next free position for a given bit map.
 * @return The position that's free.
 */
int nextFreeBit(BitMap *);

/**
 * Returns the next free i-node position.
 * @return The next free i-node position.
 */
ino_t nextFreeINode();

/**
 * Returns the next free data block position.
 * @return The next free data block position.
 */
unsigned short nextFreeDataBlock();

/**
 * Truncates the string's end delimiter subset.
 * @return The truncated string.
 */
const char *strTruncDelim(const char *);

const char *absoluteToEntry(const char *);

void _strcpy(char *, const char *);

void _strcpy_n(char *, const char *, long);

int _split(const char *, char, char ***);

#endif //ASSIGNMENT3_HELPER_H
