#include <iostream>
#include <stdint.h>
#include <string>
using namespace std;

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

inline uint16_t intConcat(uint8_t a, uint8_t b)
{
    return (b << 8) | a;
}

inline uint32_t intConcatL(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (d << 24) | (c << 16) | (b << 8) | a;
}

struct ClusterHeader
{
    uint32_t clusterLength; //Actual length of node, is all of it being used?
    uint32_t next; //Index of next node in object
};

const uint32_t diskSize = 10485760; //10MB
const uint16_t clusterSize = 4096;
const uint32_t clusterCount = (diskSize / clusterSize) - clusterCount;
static uint8_t disk[diskSize];

//Writes a cluster header
void writeClusterHeader(uint32_t index, ClusterHeader *header)
{
    //Write cluster length
    disk[index] = header->clusterLength;
    disk[index + 1] = header->clusterLength >> 8;
    disk[index + 2] = header->clusterLength >> 16;
    disk[index + 3] = header->clusterLength >> 24;

    //Write next
    disk[index + 4] = header->next;
    disk[index + 5] = header->next >> 8;
    disk[index + 6] = header->next >> 16;
    disk[index + 7] = header->next >> 24;
}

//Writes a node header
void writeNodeHeader(uint32_t index, NodeHeader *header)
{
    //Write type
    disk[index + 8] = header->type; //Skip over the cluster header

    //Write permissions
    disk[index + 9] = header->permissions;
    disk[index + 10] = header->permissions >> 8;
    disk[index + 11] = header->permissions >> 16;
    disk[index + 12] = header->permissions >> 24;

    //Write name length
    disk[index + 13] = header->nameLength;
    disk[index + 14] = header->nameLength >> 8;

    //Write the name itself
    for(uint16_t a = 15; a < header->nameLength; a++)
        disk[index + a] = header->nameData[a];
}

//Reads a cluster header
ClusterHeader *readClusterHeader(uint32_t index)
{
    //Create new object to store data
    ClusterHeader *header = new ClusterHeader;

    //Read data from disk into structure
    header->clusterLength = intConcatL(disk[index], disk[index + 1], disk[index + 2], disk[index + 3]);
    header->next = intConcatL(disk[index + 4], disk[index + 5], disk[index + 6], disk[index + 7]);
    return header;
}

//Reads a node header
NodeHeader *readNodeHeader(uint32_t index)
{
    //Create new object to store data
    NodeHeader *header = new NodeHeader;

    //Read node header into structure
    header->type = disk[index + 8]; //Skip over cluster header
    header->permissions = intConcatL(disk[index + 9], disk[index + 10], disk[index + 11], disk[index + 12]);
    header->nameLength = intConcat(disk[index + 13], disk[index + 14]);
    header->nameData = &disk[index + 15];

    return header;
}

//Installs the filesystem on the disk
void formatDisk()
{
    //Set cluster index
    for(uint32_t a = 0; a < clusterCount; a++)
    {
        disk[a] = CLUSTER_FREE;
    }
}

//Find a new cluster to use
uint32_t allocateCluster()
{
    //Go through disk index to find a free one
    for(uint32_t a = 0; a < clusterCount; a++)
    {
        if(disk[a] == CLUSTER_FREE)
        {
            //Mark found cluster as used
            disk[a] = CLUSTER_USED;

            //Return its index in the disk (bytes)
            return ((a * clusterSize) + clusterCount);
        }
    }

    //Uh oh, no free clusters found. Return 0 to indicate failure.
    return 0;
}

uint32_t createObject(uint8_t type, uint32_t permissions, uint16_t nameLength, uint8_t *name)
{
    //Allocate a cluster for the object
    uint32_t cluster = allocateCluster();

    //If we failed to allocate a new cluster, return 0
    if(cluster == 0)
        return 0;

    //Prepare cluster header for the new object
    ClusterHeader clusterHeader;
    clusterHeader.clusterLength = 280; //24 bytes to take into account the cluster and node headers and 256 byte name limit
    clusterHeader.next = 0;

    //Convert function arguments into a structure
    NodeHeader nodeHeader;
    nodeHeader.type = type;
    nodeHeader.permissions = permissions;
    nodeHeader.nameLength = nameLength;
    nodeHeader.nameData = name;

    //Write the cluster header and node header to disk
    writeClusterHeader(cluster, &clusterHeader);
    writeNodeHeader(cluster, &nodeHeader);

    return cluster; //Return the index of the newly created cluster
}

//Add an object to a directory
void addObjectToDirectory(uint32_t directoryIndex, uint32_t objectIndex)
{
    //Fetch the directories' cluster header
    ClusterHeader *directoryClusterHeader = readClusterHeader(directoryIndex);
    if(directoryClusterHeader->clusterLength == clusterSize)
    {
        //Cluster full, todo: expand to another cluster
    }
    else
    {
        //There's space remaining in this cluster so add the object in
        uint32_t currentIndex = directoryIndex + directoryClusterHeader->clusterLength;
        disk[currentIndex] = objectIndex;
        disk[currentIndex + 1] = objectIndex >> 8;
        disk[currentIndex + 2] = objectIndex >> 16;
        disk[currentIndex + 3] = objectIndex >> 24;

        //Update the cluster size on disk
        directoryClusterHeader->clusterLength += 4;
        writeClusterHeader(directoryIndex, directoryClusterHeader);
    }

    //Cleanup
    delete directoryClusterHeader;
}

//Get the number of objects in a directory
uint32_t getDirectorySize(uint32_t index)
{
    uint32_t objectCount = 0;

    //Fetch the directories' cluster header
    ClusterHeader *directoryClusterHeader = readClusterHeader(index);
    if(directoryClusterHeader->clusterLength == clusterSize)
    {
        //Cluster full, todo: have it recursively scan all connected clusters
    }

    //Add in the number of objects to the total
    objectCount += (directoryClusterHeader->clusterLength - 280) / 4;

    //Cleanup
    delete directoryClusterHeader;

    return objectCount;
}

//Returns cluster location of an object, the index is relative to the directory NOT the disk
uint32_t getDirectoryFileObject(uint32_t directoryIndex, uint32_t fileIndex)
{
    //Figure out if the provided index is in the current cluster or another one
    if(fileIndex > ((clusterSize - 280) / 4))
    {
        //Todo: getting objects in connected clusters
        return 0;
    }
    else
    {
        //Get the file index of the directory and combine the bytes into a single uint32_t
        uint32_t fileCluster = directoryIndex + 280 + (fileIndex * 4);
        return intConcatL(disk[fileCluster], disk[fileCluster + 1], disk[fileCluster + 2], disk[fileCluster + 3]);
    }
    return 0;
}

//Extend an object with another cluster
uint32_t extendCluster(uint32_t clusterIndex)
{
    //Get cluster to extend
    ClusterHeader *header = readClusterHeader(clusterIndex);

    //Set it's 'next' to a newly allocated cluster
    header->next = allocateCluster();

    //If we failed to allocate a new cluster, return 0
    if(header->next == 0)
    {
        delete header;
        return 0;
    }

    //Update the cluster on disk
    writeClusterHeader(clusterIndex, header);

    //Setup the newly allocated cluster
    ClusterHeader clusterHeader;
    clusterHeader.clusterLength = 280; //24 bytes to take into account the cluster and node headers and 256 byte name limit
    clusterHeader.next = 0;
    writeClusterHeader(header->next, &clusterHeader);

    //Cleanup
    uint32_t next = header->next;
    delete header;

    //Return newly allocated cluster index
    return next;
}

//Follows a cluster list until we reach the final one
uint32_t getClusterHead(uint32_t clusterIndex)
{
    ClusterHeader *current = readClusterHeader(clusterIndex);
    while(true)
    {
        if(current->next == 0)
        {
            delete current;
            return clusterIndex;
        }
        clusterIndex = current->next;
        delete current;
        current = readClusterHeader(clusterIndex);
    }
    return 0;
}

//Write a lump of data to an object, the object is automatically extended if space runs out
void write(uint32_t clusterIndex, uint8_t *data, uint32_t dataLength)
{
    //Get cluster head
    clusterIndex = getClusterHead(clusterIndex);
    ClusterHeader *cluster = readClusterHeader(clusterIndex);

    //Write data
    for(uint32_t a = 0; a < dataLength; a++)
    {
        //TODO: Optimise so that an if isn't needed every byte, calculate how many we can write in one go then use a for loop to that point and THEN allocate more if needed
        if(cluster->clusterLength == clusterSize) //If this cluster is full, create a new one
        {
            //Update saved size for cluster first
            writeClusterHeader(clusterIndex, cluster);

            //Allocate new cluster for more data
            delete cluster;
            clusterIndex = extendCluster(clusterIndex);
            cluster = readClusterHeader(clusterIndex);
        }

        //Write data to disk
        disk[clusterIndex + cluster->clusterLength++] = data[a];
    }

    //Update the cluster we've written to with its new size
    writeClusterHeader(clusterIndex, cluster);
    delete cluster;
}

//Read a lump of data from an object
uint8_t *read(uint32_t clusterIndex, uint32_t length)
{
    //Allocate a buffer for the data
    uint8_t *buffer = new uint8_t[length];
    uint32_t bufferOffset = 0;

    //Get cluster to start reading from
    ClusterHeader *cluster = readClusterHeader(clusterIndex);

    //Keep reading until we've read length bytes
    while(bufferOffset < length)
    {
        //Read from cluster until either we've read enough bytes or we've finished this cluster
        for(uint32_t a = 280; a < cluster->clusterLength && bufferOffset < length; a++)
        {
            buffer[bufferOffset++] = disk[clusterIndex + a];
        }

        //If we've finished this cluster and still haven't read enough bytes, get the next cluster
        if(bufferOffset < length)
        {
            if(cluster->next == 0) //No more clusters to read, just return what we've got so far
                return buffer;

            clusterIndex = cluster->next;
            delete cluster;
            cluster = readClusterHeader(clusterIndex);
        }
    }

    //Cleanup
    delete cluster;

    //Return read data
    return buffer;
}

void freeObject(uint32_t index)
{
    //Go through each cluster in the object and mark as free
    ClusterHeader *current = readClusterHeader(index);
    while(true)
    {
        disk[index / clusterSize] = CLUSTER_FREE;
        std::cout << "\nFreed cluster: " << (index / clusterSize) << std::endl;
        if(current->next == 0)
        {
            delete current;
            return;
        }
        index = current->next;
        delete current;
        current = readClusterHeader(index);
    }
    delete current;
}


int main()
{
    //Install filesystem to ramdisk
    formatDisk();

    uint8_t name2[] = "file";

    uint32_t fc = 0;
    while(fc < clusterCount)
    {
        uint32_t file = createObject(NODE_FILE, 0, 4, name2);
        if(file == 0)
        {
            std::cout << "\nOut of disk space!" << std::endl;
            break;
        }
        std::cout << "\nAllocated: " << file << std::endl;
        fc++;
    }

    return 0;
}





