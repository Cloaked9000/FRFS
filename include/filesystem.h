#ifndef FILESYSTEM_H
#define FILESYSTEM_H
#include <stdint.h>
#include <iostream>
enum NodeType
{
    NODE_FILE = 0x0,
    NODE_DIRECTORY = 0x1,
    NODE_SYMLINK = 0x2,
};

enum ClusterState
{
    CLUSTER_FREE = 0x0,
    CLUSTER_USED = 0x1,
};

struct NodeHeader
{
    uint8_t type; //NodeType. Type of node.
    uint32_t permissions; //Access permissions
    uint16_t nameLength; //Length of object name
    uint8_t *nameData; //Array of characters containing name
};

struct ClusterHeader
{
    uint32_t clusterLength; //Actual length of node, is all of it being used?
    uint32_t next; //Index of next node in object
};

struct FilepathClusterInfo
{
    uint32_t objectIndex; //Index of the object itself
    uint32_t ownerIndex; //Index of the directory containing the object
    uint32_t relativeIndex; //Index relative to the owner directory
};

inline uint16_t intConcat(uint8_t a, uint8_t b)
{
    return (b << 8) | a;
}

inline uint32_t intConcatL(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (d << 24) | (c << 16) | (b << 8) | a;
}

const uint64_t DISK_SIZE = 819200000; //800MB
const uint16_t CLUSTER_SIZE = 512;
const uint32_t CLUSTER_COUNT = DISK_SIZE / CLUSTER_SIZE;
static uint8_t disk[DISK_SIZE];
#define FIRST_ALLOCATION_POSITION (CLUSTER_COUNT / CLUSTER_SIZE)+1
#define HEADER_SIZE (uint16_t)280 //16 bytes to take into account the cluster and node headers and 264 byte name limit
#define CLUSTER_HEADER_SIZE (uint8_t)8 //Reserved number of bytes at the start of each cluster
#define DIRECTORY_ENTRY_SIZE (uint8_t)4 //Each directory entry is 4 bytes
static uint32_t lastAllocationPosition = FIRST_ALLOCATION_POSITION;

void fs_writeClusterHeader(uint32_t index, ClusterHeader *header);
void fs_writeNodeHeader(uint32_t index, NodeHeader *header);
ClusterHeader *fs_readClusterHeader(uint32_t index);
NodeHeader *fs_readNodeHeader(uint32_t index);
void fs_formatDisk();
uint32_t fs_allocateCluster();
uint32_t fs_createObject(uint8_t type, uint32_t permissions, uint16_t nameLength, uint8_t *name);
uint8_t fs_getDirectoryClusterFromObjectIndex(uint32_t *directoryIndex, uint32_t *objectIndex, uint32_t *clusterSize);
uint32_t fs_getDirectoryObject(uint32_t directoryIndex, uint32_t objectIndex);
uint32_t fs_extendCluster(uint32_t clusterIndex);
void fs_addObjectToDirectory(uint32_t directoryIndex, uint32_t objectIndex);
uint8_t fs_removeObjectFromDirectory(uint32_t directoryIndex, uint32_t objectIndex);
uint64_t fs_getFileSize(uint32_t index);
uint32_t fs_getClusterHead(uint32_t clusterIndex);
void fs_write(uint32_t clusterIndex, uint8_t *data, uint32_t dataLength);
uint8_t *fs_read(uint32_t clusterIndex, uint32_t length);
void fs_freeObject(uint32_t index);
uint8_t *fs_getDisk(); //Temporary RAM disk stuff
FilepathClusterInfo fs_getClusterFromFilepath(uint32_t rootDirectory, uint32_t currentDirectory, uint8_t *path, uint32_t pathLength);

//Converts a cluster index and cluster offset into actual disk index. Must be used to get the value to pass to the read/write functions
inline uint64_t fs_getWritePosition(uint32_t clusterIndex)
{
    return clusterIndex * CLUSTER_SIZE;
}

//Write a byte to disk index
inline void fs_write8(uint64_t writePos, uint8_t byte)
{
    disk[writePos] = byte;
}

//Read byte from disk index
inline uint8_t fs_read8(uint64_t writePos)
{
    return disk[writePos];
}

//Write a 16bit integer to disk
inline void fs_write16(uint64_t writePos, uint16_t data)
{
    disk[writePos] = data;
    disk[writePos + 1] = data >> 8;
}

//Read a 16bit integer from disk
inline uint16_t fs_read16(uint64_t writePos)
{
    return intConcat(disk[writePos], disk[writePos + 1]);
}

//Write a 32bit integer to disk
inline void fs_write32(uint64_t writePos, uint32_t data)
{
    disk[writePos] = data;
    disk[writePos + 1] = data >> 8;
    disk[writePos + 2] = data >> 16;
    disk[writePos + 3] = data >> 24;
}

//Read a 32bit integer from disk
inline uint32_t fs_read32(uint64_t writePos)
{
    return intConcatL(disk[writePos], disk[writePos + 1], disk[writePos + 2], disk[writePos + 3]);
}

//Delete all dynamically allocated NodeHeader memory
inline void fs_freeNodeHeader(NodeHeader *header)
{
    delete[] header->nameData;
    delete header;
}

//Return the number of objects in a directory
inline uint32_t fs_getDirectorySize(uint32_t index)
{
    return fs_getFileSize(index) / DIRECTORY_ENTRY_SIZE;
}
#endif // FILESYSTEM_H
