/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "sfs.h"
#include "helper.h"
#include "bitmap.h"
#include "bytebuffer.h"

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn) {
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    fprintf(stderr, "in connected in bb-init\n");

    // Initailize all the blocks here.

    disk_open(SFS_DATA->diskfile);

    if (pthread_mutex_init(&init_mutex, NULL) < 0) {
        return NULL;
    }

    pthread_mutex_lock(&init_mutex);

    char buffer[BLOCK_SIZE];
    memset(buffer, 0, BLOCK_SIZE);

    superBlock = (SuperBlock *) malloc(sizeof(SuperBlock));
    if (!superBlock) {
        fprintf(stderr, "Could not allocate super block.\n");
        return NULL;
    }

    superBlock->blockBitMap = bitmap_allocate(NUM_DATA_BLOCKS);
    if (!superBlock->blockBitMap) {
        fprintf(stderr, "Could not allocate block bit map.\n");
        return NULL;
    }

    superBlock->iNodeBitMap = bitmap_allocate(NUM_INODE_BLOCKS);
    if (!superBlock->iNodeBitMap) {
        fprintf(stderr, "Could not allocate block bit map.\n");
        return NULL;
    }

    if (block_read(SUPER_BLOCK_INDEX, buffer) <= 0) { // read super block, if empty create it.
        superBlock->numFreeBlocks = NUM_DATA_BLOCKS;
        superBlock->numFreeINodes = NUM_INODE_BLOCKS;
    } else {
        ByteBuffer *byteBuffer = allocate((Byte *) buffer, BLOCK_SIZE, BLOCK_SIZE);
        if (!byteBuffer) {
            fprintf(stderr, "Could not allocate byte buffer.\n");
            return NULL;
        }

        superBlock->numFreeBlocks = readInt(byteBuffer);
        superBlock->numFreeINodes = readByte(byteBuffer);

        int numPartitions = (int) readInt(byteBuffer);
        for (; numPartitions >= 0; numPartitions--) {
            superBlock->blockBitMap->container[numPartitions] = (bitmap_type) readInt(byteBuffer);
        }

        numPartitions = (int) readInt(byteBuffer);
        for (; numPartitions >= 0; numPartitions--) {
            superBlock->iNodeBitMap->container[numPartitions] = (bitmap_type) readInt(byteBuffer);
        }

        free(byteBuffer->buffer);
        free(byteBuffer);
    }

    iNodeList = (INode *) malloc(sizeof(INode) * NUM_INODE_BLOCKS);
    if (!iNodeList) {
        fprintf(stderr, "Could not malloc i-node list.\n");
        return NULL;
    }

    ino_t node_id = ROOT_INODE_ID;
    for (; node_id < NUM_INODE_BLOCKS; node_id++) { // TODO check if it's already in file system
        INode *node = iNodeList + node_id;

        memset(buffer, 0, BLOCK_SIZE);
        if (block_read((const int) (node_id + INODE_BLOCK_START), buffer) <= 0) {
            _Bool root = node_id == ROOT_INODE_ID;

            node_stat(node, node_id, (mode_t) ((root ? S_IFDIR : S_IFREG) | S_IRWXU),
                      (nlink_t) (root ? 2 : DEFAULT_NUM_DIRECTORIES));

            if (root) {
                node_reserve(node);

                ReserveBlock reserveBlock = block_reserve(node);

                fprintf(stderr, "Reserve block status: [free block: %d] [free link: %d]\n", reserveBlock.nextDataBlock,
                        reserveBlock.nextLink);

                if (flush_super() < 0) {
                    fprintf(stderr, "Could not flush super block.\n");
                    return NULL;
                }
            }

            if (flush_iNode(node) < 0) {
                fprintf(stderr, "Could not flush i-node in init.\n");
                return NULL;
            }
        } else {
            ByteBuffer *byteBuffer = allocate((Byte *) buffer, BLOCK_SIZE, BLOCK_SIZE);
            if (!byteBuffer) {
                return NULL;
            }

            node->id = (ino_t) readLong(byteBuffer);
            node->userId = (uid_t) readInt(byteBuffer);
            node->groupId = (gid_t) readInt(byteBuffer);

            node->st_mode = (mode_t) readInt(byteBuffer);

            node->lastFileModTime.tv_sec = readLong(byteBuffer);
            node->lastAccessTime.tv_sec = readLong(byteBuffer);
            node->lastModifiedTime.tv_sec = readLong(byteBuffer);

            node->numFileLinks = (nlink_t) readLong(byteBuffer);
            node->fileSize = (size_t) readLong(byteBuffer);

            int index = 0;
            int numBlockLinks = sizeof(node->blockLinks) / sizeof(short);
            for (; index < numBlockLinks; index++) {
                node->blockLinks[index] = readShort(byteBuffer);
            }

            free(byteBuffer);
        }
    }

    memset(buffer, 0, BLOCK_SIZE);

    INode *rootINode = iNodeList + ROOT_INODE_ID;
    if (!rootINode) {
        return NULL;
    }

    if (block_read(rootINode->blockLinks[0], buffer) <= 0) { //TODO maybe remove this boilerplate code.
        rootDirectory = directory_allocate(rootINode->id, "/");
        if (!rootDirectory) {
            fprintf(stderr, "Could not allocate root directory.\n");
            return NULL;
        }

        saveDirectory(rootDirectory);
    } else {
        rootDirectory = malloc(sizeof(Directory));
        if (!rootDirectory) {
            fprintf(stderr, "Could not allocate root directory.\n");
            return NULL;
        }

        rootDirectory->entry = NULL;
        rootDirectory->sibling = NULL;
        rootDirectory->child = NULL;

        parseDirectory((void *(*)(Directory *, void *)) loadDirectory, rootDirectory, NULL);
    }

    pthread_mutex_unlock(&init_mutex);

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata) {
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *st) {
    int retstat = 0;

    memset(st, 0, sizeof(struct stat));

    INode *node = findINode(path);
    if (node) {
        st->st_uid = node->userId;
        st->st_gid = node->groupId;

        st->st_ino = node->id;

        st->st_mode = node->st_mode;
        st->st_nlink = node->numFileLinks;

        st->st_atime = node->lastAccessTime.tv_sec;
        st->st_mtime = node->lastModifiedTime.tv_sec;

        if (S_ISREG(node->st_mode)) {
            st->st_size = node->fileSize;
            st->st_blksize = BLOCK_SIZE;
        }
    } else {
        /*st->st_uid = getuid();
        st->st_gid = getgid();

        st->st_ino = 1;

        st->st_mode = S_IRWXU;
        size_t len = strlen(path);
        if (len > 0) {
            char character = path[len - 1];

            st->st_mode |= (character == '/' ? S_IFDIR : S_IFREG);
            st->st_nlink = (nlink_t) (character == '/' ? 0 : 2);
        }

        st->st_atime = time(NULL);
        st->st_mtime = time(NULL);*/
        return -ENOENT;
    }

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, st);
    fprintf(stderr, "\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, st);

    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *absolutePath, mode_t mode, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
            absolutePath, mode, fi);
    fprintf(stderr, "\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
            absolutePath, mode, fi);

    Directory *directory = findDirectory(rootDirectory, "", absolutePath);
    if (directory) { // The file's entry exists, we should return.
        return 0;
    }

    Directory *parentDirectory = findParentDirectory(rootDirectory, absolutePath);
    if (!parentDirectory) { // Parent folder doesn't exist, big big problemo
        return ENOENT;
    }

    ino_t ino = nextFreeINode(); // Find the next free i-node.
    if (ino == -1) {
        return ENOSPC;
    }

    INode *node = iNodeList + ino;
    if (!node) {
        return ENOMSG;
    }

    node_reserve(node); // reserve it's place, do this first to avoid any race issues.
    node_stat(node, ino, mode, 1); // Populate the node with the given data.

    Directory *nextDirectory = directory_allocate(ino, absoluteToEntry(absolutePath));
    if (!nextDirectory) {
        return ENOMSG;
    }

    Directory *lastDirectory = findLastSibling(parentDirectory);
    if (lastDirectory == NULL) {
        parentDirectory->child = nextDirectory;
        saveDirectory(parentDirectory);
    } else {
        lastDirectory->sibling = nextDirectory;
        saveDirectory(lastDirectory);
    }

    saveDirectory(nextDirectory);
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *absolutePath) {
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", absolutePath);
    fprintf(stderr, "sfs_unlink(path=\"%s\")\n", absolutePath);

    Directory *directory = findDirectory(rootDirectory, "", absolutePath);
    if (!directory) { // The file's entry exists, we should return.
        return 0;
    }

    INode *node = iNodeList + directory->entry->ino;
    if (!node) {
        return ENOMSG;
    }

    if (S_ISDIR(node->st_mode)) {
        return EISDIR; // Return if it's removing a directory!
    }

    node_destroy(node);
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */

int sfs_open(const char *path, struct fuse_file_info *fi) {
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n", path, fi);
    fprintf(stderr, "\nsfs_open(path\"%s\", fi=0x%08x)\n", path, fi);

    int retstat = 0;

    // Get inode corresponding to path
    INode *node = findINode(path);
    if (!node) {
        return -ENOENT;
    }

    if (S_ISDIR(node->st_mode)) {
        return EISDIR;
    }

    if ((node->st_mode & S_IXUSR) == 0) {
        return EACCES;
    }

    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    // Turn i-node into default empty state using node-stat w/ default params in sfs_init
    // unreserve the i-node
    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    INode *node = findINode(path);
    if (!node) {
        return -ENOENT;
    }

    int nextDataBlock = nextFreeDataBlock();
    if (nextDataBlock == -1) {
        return -1;
    }

    block_reserve(node);

    if (block_read(nextDataBlock, buf) <= 0) {
        return -1;
    }

    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    INode *node = findINode(path);
    if (!node) {
        return -ENOENT;
    }

    int nextDataBlock = nextFreeDataBlock();
    if (nextDataBlock == -1) {
        return -1;
    }

    block_reserve(node);

    if (block_write(nextDataBlock, buf) <= 0) {
        return -1;
    }

    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *absolutePath, mode_t mode) {
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            absolutePath, mode);
    fprintf(stderr, "\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            absolutePath, mode);
    size_t length = strlen(absolutePath);
    if (length >= PATH_MAX) {
        return ENAMETOOLONG;
    }

    Directory *parent = findParentDirectory(rootDirectory, absolutePath);
    if (!parent) {
        return ENOENT;
    }

    ino_t ino = nextFreeINode(); // Find the next free i-node.
    if (ino == -1) {
        return ENOSPC;
    }

    INode *node = iNodeList + ino;
    if (!node) {
        return ENOMSG;
    }

    node_reserve(node); // reserve it's place, do this first to avoid any race issues.
    node_stat(node, ino, mode, 2); // Populate the node with the given data.

    Directory *nextDirectory = directory_allocate(ino, absoluteToEntry(absolutePath));
    if (!nextDirectory) {
        return ENOMSG;
    }

    Directory *lastDirectory = findLastSibling(parent);
    if (lastDirectory == NULL) {
        parent->child = nextDirectory;
        saveDirectory(parent);
    } else {
        lastDirectory->sibling = nextDirectory;
        saveDirectory(lastDirectory);
    }

    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path) {
    int retstat = 0;
    fprintf(stderr, "sfs_rmdir(path=\"%s\")\n", path);
    log_msg("sfs_rmdir(path=\"%s\")\n", path);

    Directory *directory = findDirectory(rootDirectory, "", path);
    if (!directory) { // The file's entry exists, we should return.
        return 0;
    }

    INode *node = iNodeList + directory->entry->ino;
    if (!node) {
        return ENOMSG;
    }

    if (S_ISREG(node->st_mode)) {
        return ENOTDIR; // Return if it's removing a directory!
    }

    node_destroy(node);
    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);
    fprintf(stderr, "\nsfs_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);

    INode *node = findINode(path);
    if (!node) {
        return 0;
    }

    if (S_ISREG(node->st_mode)) {
        return EISDIR;
    }

    if ((node->st_mode & S_IXUSR) == 0) {
        return EACCES;
    }

    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                struct fuse_file_info *fi) {
    fprintf(stderr, "sfs_readaddr: path:%s", path);
    int retstat = 0;

    Directory *parent = findParentDirectory(rootDirectory, path);
    if (!parent) {
        return ENOENT;
    }

    Directory *directory = parent->child;
    while (directory) {
        fprintf(stderr, "Entry name: %s\n", directory->entry->entryName);
        filler(buf, directory->entry->entryName, NULL, 0);

        directory = directory->sibling;
    }
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi) {
    int retstat = 0;

    // Dont know what this does either :S

    return retstat;
}

struct fuse_operations sfs_oper = {
        .init = sfs_init,
        .destroy = sfs_destroy,

        .getattr = sfs_getattr,
        .create = sfs_create,
        .unlink = sfs_unlink,
        .open = sfs_open,
        .release = sfs_release,
        .read = sfs_read,
        .write = sfs_write,

        .rmdir = sfs_rmdir,
        .mkdir = sfs_mkdir,

        .opendir = sfs_opendir,
        .readdir = sfs_readdir,
        .releasedir = sfs_releasedir
};

void sfs_usage() {
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[]) {
    int fuse_stat;
    struct sfs_state *sfs_data;

    // sanity checking on the command line
    if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-'))
        sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
        perror("main calloc");
        abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc - 2];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    sfs_data->logfile = log_open();

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
