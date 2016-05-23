#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits>
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

const uint32_t diskSize = 1048576000; //10MB
const uint16_t clusterSize = 4096;
const uint32_t clusterCount = (diskSize / clusterSize) - clusterCount;
static uint8_t disk[diskSize];
uint32_t lastAllocationPosition = 0;

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
    for(uint16_t a = 0; a < header->nameLength; a++)
        disk[index + 15 + a] = header->nameData[a];
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
    for(uint32_t a = lastAllocationPosition; a < clusterCount-20; a++)
    {
        if(disk[a] == CLUSTER_FREE)
        {
            //Store this cluster position for future allocations
            lastAllocationPosition = a;

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

//Returns cluster location of an object, the index is relative to the directory NOT the disk
uint32_t getDirectoryFileObject(uint32_t directoryIndex, uint32_t fileIndex)
{
    //Figure out if the provided index is in the current cluster or another one
    if(fileIndex > ((clusterSize - 280) / 4))
    {
        fileIndex -= ((clusterSize - 280) / 4);
        //Read directory header to find next cluster
        ClusterHeader *header = readClusterHeader(directoryIndex);
        if(header->next == 0)
        {
            return 0;
            delete header;
        }

        uint32_t fileCount = getDirectoryFileObject(header->next, fileIndex);
        delete header;
        return fileCount;
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

//Add an object to a directory
void addObjectToDirectory(uint32_t directoryIndex, uint32_t objectIndex)
{
    //Fetch the directories' cluster header
    ClusterHeader *directoryClusterHeader = readClusterHeader(directoryIndex);
    if(directoryClusterHeader->clusterLength == clusterSize)
    {
        //Cluster full, expand the directory!
        if(directoryClusterHeader->next == 0)
        {
            uint32_t newCluster = extendCluster(directoryIndex);
            addObjectToDirectory(newCluster, objectIndex);
        }
        else //Else there's another section of the directory, add it to that instead
        {
            addObjectToDirectory(directoryClusterHeader->next, objectIndex);
        }
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

//Remove an object from a directory. Note: Wont free object, will just unlist from THIS directory
uint8_t removeObjectFromDirectory(uint32_t directoryIndex, uint32_t objectIndex)
{
    //Fetch the directories' cluster header
    ClusterHeader *directoryClusterHeader = readClusterHeader(directoryIndex);

    if(objectIndex > ((clusterSize - 280) / 4)) //If object reference in ANOTHER connected directory cluster
    {
        if(directoryClusterHeader->next != 0)
        {
            objectIndex -= ((clusterSize - 280) / 4);
            removeObjectFromDirectory(directoryClusterHeader->next, objectIndex);
        }
    }
    else //If object reference is in current cluster
    {
        //Calculate name offset in current cluster

        //Set it to 0 in directory
        uint32_t relativeObjectIndex = 280 + directoryIndex + (objectIndex*4);

        //Shift all entries in the rest of the cluster down so as not to leave empty space in the cluster
        for(; relativeObjectIndex < (directoryIndex + clusterSize); relativeObjectIndex+=4)
        {
            disk[relativeObjectIndex] = disk[relativeObjectIndex + 4];
            disk[relativeObjectIndex+1] = disk[relativeObjectIndex + 5];
            disk[relativeObjectIndex+2] = disk[relativeObjectIndex + 6];
            disk[relativeObjectIndex+3] = disk[relativeObjectIndex + 7];
        }

        //Reduce header size and update on disk
        directoryClusterHeader->clusterLength -= 4;
        writeClusterHeader(directoryIndex, directoryClusterHeader);

        //Cleanup and return success
        delete directoryClusterHeader;
        return 1;
    }
    delete directoryClusterHeader;
    return 0;
}

//Get the number of objects in a directory
uint32_t getDirectorySize(uint32_t index)
{
    uint32_t objectCount = 0;

    //Fetch the directories' cluster header
    ClusterHeader *directoryClusterHeader = readClusterHeader(index);

    //If there's a next cluster, recursively scan that too
    if(directoryClusterHeader->next != 0)
    {
        objectCount += getDirectorySize(directoryClusterHeader->next);
    }

    //Add in the number of objects to the total
    objectCount += (directoryClusterHeader->clusterLength - 280) / 4;

    //Cleanup
    delete directoryClusterHeader;

    return objectCount;
}

//Get the number of bytes in a file (NOT a directory!)
uint32_t getFileSize(uint32_t index)
{
    uint32_t objectCount = 0;

    //Fetch the files cluster header
    ClusterHeader *fileClusterHeader = readClusterHeader(index);

    //If there's a next cluster, recursively scan that too
    while(fileClusterHeader->next != 0)
    {
        objectCount += getFileSize(fileClusterHeader->next);
    }

    //Add in the number of objects to the total
    objectCount += (fileClusterHeader->clusterLength - 280);

    //Cleanup
    delete fileClusterHeader;

    return objectCount;
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

//Marks a cluster tree as free
void freeObject(uint32_t index)
{
    //Go through each cluster in the object and mark as free
    ClusterHeader *current = readClusterHeader(index);
    while(true) //Keep going until we run out of connected headers
    {
        //Mark cluster space as free
        disk[index / clusterSize] = CLUSTER_FREE;

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

//Converts a string filepath to a cluster index
uint32_t getClusterFromFilepath(uint32_t rootDirectory, uint32_t currentDirectory, uint8_t *path, uint32_t pathLength)
{
    //Reset to root directory if filepath is preceded with a '/'
    if(path[0] == '/')
    {
        currentDirectory = rootDirectory;
    }

    char *token = strtok((char*)path, "/");
    while(token)
    {
        //Get list of files in current directory and check if this name matches any of them
        uint32_t dirSize = getDirectorySize(currentDirectory);
        for(uint32_t a = 0; a < dirSize; a++)
        {
            uint32_t node = getDirectoryFileObject(currentDirectory, a);
            NodeHeader *nodeData = readNodeHeader(node);
            //If we've found a file match
            if(strcmp((char*)nodeData->nameData, token) == 0)
            {
                currentDirectory = node;
                //If 'currentDirectory' is a file, return
                NodeHeader *h = readNodeHeader(currentDirectory);
                if(h->type == NODE_FILE)
                {
                    delete h;
                    return currentDirectory;
                }
                delete h;
            }
            delete nodeData;
        }
        token = strtok(NULL, "/");
    }

    return currentDirectory;
}


int main()
{
    std::cout << "\nPreparing RAM disk... ";
    //Install filesystem to ramdisk
    formatDisk();
    std::cout << "Done. " << std::endl;
    std::cout << "Disk size: " << diskSize
              << "\nCluster size: " << clusterSize
              << "\nUsable disk space: " << diskSize-(clusterSize * 4) << std::endl;


    uint8_t rootName[] = "root";
    uint32_t rootDirectory = createObject(NODE_DIRECTORY, 0, 4, rootName);
    uint32_t currentDirectory = rootDirectory;
    for(uint32_t a = 0; a < 5; a++)
    {
        std::string args = "dir" + std::to_string(a);
        uint32_t newObject = createObject(NODE_DIRECTORY, 0, args.size(), (uint8_t*)&args[0]);
        addObjectToDirectory(currentDirectory, newObject);
    }
    while(true)
    {
        std::cout << "$: ";
        std::string command, args, args2;
        std::cin >> command;
        if(command == "mkdir")
        {
            std::cin >> args;
            uint32_t newObject = createObject(NODE_DIRECTORY, 0, args.size(), (uint8_t*)&args[0]);
            addObjectToDirectory(currentDirectory, newObject);
        }
        else if(command == "rm")
        {
            std::cin >> args;
            //Find object by that name
            uint32_t dirSize = getDirectorySize(currentDirectory);
            for(uint32_t a = 0; a < dirSize; a++)
            {
                uint32_t node = getDirectoryFileObject(currentDirectory, a);
                NodeHeader *nodeData = readNodeHeader(node);
                if(strcmp(&args[0], (char*)nodeData->nameData) == 0)
                {
                    removeObjectFromDirectory(currentDirectory, a);
                    freeObject(node);
                }
                delete nodeData;
            }
        }
        else if(command == "ls")
        {
            uint32_t dirSize = getDirectorySize(currentDirectory);
            for(uint32_t a = 0; a < dirSize; a++)
            {
                uint32_t node = getDirectoryFileObject(currentDirectory, a);
                NodeHeader *nodeData = readNodeHeader(node);
                for(uint16_t b = 0; b < nodeData->nameLength; b++)
                    std::cout << nodeData->nameData[b];
                std::cout << std::endl;
                delete nodeData;
            }
        }
        else if(command == "cd")
        {
            std::cin >> args;
            currentDirectory = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size());
        }
        else if(command == "touch")
        {
            std::cin >> args >> args2;
            uint32_t obj = createObject(NODE_FILE, 0, args.size(), (uint8_t*)&args[0]);
            addObjectToDirectory(currentDirectory, obj);
            write(obj, (uint8_t*)&args2[0], args2.size());
        }
        else if(command == "tell")
        {
            std::cin >> args;
            uint32_t file = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size());
            uint32_t fileSize = getFileSize(file);
            uint8_t *b = read(file, fileSize);
            for(uint32_t c = 0; c < fileSize; c++)
                std::cout << b[c];
            std::cout << std::endl;
            delete b;
        }
        else
        {
            std::cout << "Command not recognised!" << std::endl;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        }
    }
    return 0;
}





