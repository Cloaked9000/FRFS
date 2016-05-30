#include "filesystem.h"
#include <string.h>
uint32_t lastAllocationPosition = FIRST_ALLOCATION_POSITION;

//Writes a cluster header
void writeClusterHeader(uint32_t index, ClusterHeader *header)
{
    //Get write position
    uint64_t writePos = getWritePosition(index);

    //Write cluster length
    write32(writePos, header->clusterLength);

    //Write next
    write32(writePos + 4, header->next);
}

//Writes a node header
void writeNodeHeader(uint32_t index, NodeHeader *header)
{
    //Get write position
    uint64_t writePos = getWritePosition(index);

    //Write type
    write8(writePos + 8, header->type); //Skip over the cluster header

    //Write permissions
    write32(writePos + 9, header->permissions);

    //Write name length. + 1 for the null character if needed
    if(header->nameData[header->nameLength-1] == '\0')
        write16(writePos + 13, header->nameLength);
    else
        write16(writePos + 13, header->nameLength + 1);

    //Write the name itself
    for(uint16_t a = 0; a < header->nameLength; a++)
        write8(writePos + 15 + a, header->nameData[a]);

    if(header->nameData[header->nameLength-1] != '\0') //Add in a null terminating character as none is provided
        write8(writePos + 15 + header->nameLength, '\0');
}

//Reads a cluster header
ClusterHeader *readClusterHeader(uint32_t index)
{
    //Create new object to store data
    ClusterHeader *header = new ClusterHeader;

    //Read data from disk into structure
    uint64_t writePos = getWritePosition(index);
    header->clusterLength = read32(writePos);
    header->next = read32(writePos + 4);
    return header;
}

//Reads a node header
NodeHeader *readNodeHeader(uint32_t index)
{
    //Create new object to store data
    NodeHeader *header = new NodeHeader;

    //Read node header into structure
    uint64_t writePos = getWritePosition(index);
    header->type = read8(writePos + 8); //Skip over cluster header
    header->permissions = read32(writePos + 9);
    header->nameLength = read16(writePos + 13);
    header->nameData = new uint8_t[header->nameLength];
    for(uint16_t a = 0; a < header->nameLength; a++)
        header->nameData[a] = read8(writePos + 15 + a);

    return header;
}

//Installs the filesystem on the disk
void formatDisk()
{
    //Set cluster index
    for(uint32_t a = 0; a < CLUSTER_COUNT; a++)
    {
        disk[a] = CLUSTER_FREE;
    }
}

//Find a new cluster to use
uint32_t allocateCluster()
{
    //If allocateCluster is searching all available spots, and not from last allocation position
    uint8_t isFirstSweep = 0;
    if(lastAllocationPosition == FIRST_ALLOCATION_POSITION)
        isFirstSweep = 1;

    //Go through disk index to find a free one
    for(uint32_t a = lastAllocationPosition; a < CLUSTER_COUNT; a++)
    {
        if(disk[a] == CLUSTER_FREE)
        {
            //Store this cluster position for future allocations
            lastAllocationPosition = a;

            //Mark found cluster as used
            disk[a] = CLUSTER_USED;

            //Return its cluster index in the disk
            return a;
        }
    }

    if(!isFirstSweep) //If this was a search from last allocation position and not from the start, do a search from the first available position
    {
        lastAllocationPosition = FIRST_ALLOCATION_POSITION;
        return allocateCluster();
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
    clusterHeader.clusterLength = HEADER_SIZE;
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

uint8_t getDirectoryClusterFromObjectIndex(uint32_t *directoryIndex, uint32_t *objectIndex, uint32_t *clusterSize) //These long names are killing me
{
    //Find the cluster which the object index is stored in within the directory
    *clusterSize = HEADER_SIZE;
    ClusterHeader *directoryHeader = readClusterHeader(*directoryIndex);
    while(*clusterSize + (*objectIndex * DIRECTORY_ENTRY_SIZE) >= directoryHeader->clusterLength) //Keep going until the index is within the current cluster
    {
        if(directoryHeader->next == 0) //If there's no next cluster, index out of range, return 0.
        {
            delete directoryHeader;
            return 0;
        }
        //Else move onto next cluster within the directory
        *directoryIndex = directoryHeader->next;

        //Reduce the objectIndex by the total storable within a directory as that's how many we've skipepd over
        *objectIndex -= (directoryHeader->clusterLength - *clusterSize) / DIRECTORY_ENTRY_SIZE;
        *clusterSize = CLUSTER_HEADER_SIZE;

        delete directoryHeader;
        directoryHeader = readClusterHeader(*directoryIndex);
    }
    delete directoryHeader;
    return 1;
}


//Returns cluster location of an object, the index is relative to the directory NOT the disk
uint32_t getDirectoryObject(uint32_t directoryIndex, uint32_t objectIndex)
{
    //Get which cluster of the directory the object is in
    uint32_t clusterHeaderSize;
    if(getDirectoryClusterFromObjectIndex(&directoryIndex, &objectIndex, &clusterHeaderSize) == 0)
        return 0;

    //Get the file index of the directory and combine the bytes into a single uint32_t
    uint64_t writePos = getWritePosition(directoryIndex);
    return read32(writePos + clusterHeaderSize + (objectIndex * DIRECTORY_ENTRY_SIZE));
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
    clusterHeader.clusterLength = CLUSTER_HEADER_SIZE;
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
    if(directoryClusterHeader->clusterLength == CLUSTER_SIZE)
    {
        //Cluster full, expand the directory!
        if(directoryClusterHeader->next == 0)
        {
            uint32_t newCluster = extendCluster(directoryIndex);
            addObjectToDirectory(newCluster, objectIndex);
        }
        else //Else there's another section of the directory, add it to that instead and let it deal with allocation
        {
            addObjectToDirectory(directoryClusterHeader->next, objectIndex);
        }
    }
    else
    {
        //There's space remaining in this cluster so add the object in
        uint64_t writePos = getWritePosition(directoryIndex);
        write32(writePos + directoryClusterHeader->clusterLength, objectIndex);

        //Update the cluster size on disk
        directoryClusterHeader->clusterLength += DIRECTORY_ENTRY_SIZE;
        writeClusterHeader(directoryIndex, directoryClusterHeader);
    }

    //Cleanup
    delete directoryClusterHeader;
}

//Remove an object from a directory. Note: Wont free object, will just unlist from THIS directory
uint8_t removeObjectFromDirectory(uint32_t directoryIndex, uint32_t objectIndex)
{
    //Get which cluster of the directory the object is in
    uint32_t clusterHeaderSize;
    if(getDirectoryClusterFromObjectIndex(&directoryIndex, &objectIndex, &clusterHeaderSize) == 0)
        return 0;

    //Calculate name offset in current cluster
    uint32_t relativeObjectIndex = clusterHeaderSize + (objectIndex*DIRECTORY_ENTRY_SIZE);
    uint64_t writePos = getWritePosition(directoryIndex);

    //Shift all entries in the rest of the cluster down so as not to leave empty space in the cluster
    for(; relativeObjectIndex < CLUSTER_SIZE; relativeObjectIndex+=DIRECTORY_ENTRY_SIZE)
    {
        for(uint32_t a = 0; a < DIRECTORY_ENTRY_SIZE; a++) //Shift each byte in the entry
        {
            write8(writePos + relativeObjectIndex + a, read8(writePos + relativeObjectIndex + DIRECTORY_ENTRY_SIZE + a));
        }
    }

    //Reduce header size and update on disk
    ClusterHeader *directoryClusterHeader = readClusterHeader(directoryIndex);
    directoryClusterHeader->clusterLength -= DIRECTORY_ENTRY_SIZE;
    writeClusterHeader(directoryIndex, directoryClusterHeader);

    //Cleanup and return success
    delete directoryClusterHeader;
    return 1;
}

//Get the number of bytes in a file (NOT a directory!)
uint64_t getFileSize(uint32_t index)
{
    uint64_t objSize = 0;
    uint32_t headerSize = HEADER_SIZE;
    ClusterHeader *header;
    do
    {
        header = readClusterHeader(index);
        objSize += header->clusterLength - headerSize;
        index = header->next;
        delete header;
        headerSize = CLUSTER_HEADER_SIZE;
    } while(index != 0);
    return objSize;
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
    uint64_t writePos = getWritePosition(clusterIndex);
    for(uint32_t a = 0; a < dataLength; a++)
    {
        //TODO: Optimise so that an if isn't needed every byte, calculate how many we can write in one go then use a for loop to that point and THEN allocate more if needed
        if(cluster->clusterLength == CLUSTER_SIZE) //If this cluster is full, create a new one
        {
            //Update saved size for cluster first
            writeClusterHeader(clusterIndex, cluster);

            //Allocate new cluster for more data
            delete cluster;
            clusterIndex = extendCluster(clusterIndex);
            cluster = readClusterHeader(clusterIndex);
            writePos = getWritePosition(clusterIndex);
        }

        //Write data to disk
        write8(writePos + cluster->clusterLength++, data[a]);
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
    uint32_t headerSize = HEADER_SIZE;
    uint64_t writePos = getWritePosition(clusterIndex);
    ClusterHeader *cluster = readClusterHeader(clusterIndex);

    //Keep reading until we've read length bytes
    while(bufferOffset < length)
    {
        //Read from cluster until either we've read enough bytes or we've finished this cluster
        for(uint32_t a = headerSize; a < cluster->clusterLength && bufferOffset < length; a++)
        {
            buffer[bufferOffset++] = read8(writePos + a);
        }

        //If we've finished this cluster and still haven't read enough bytes, get the next cluster
        if(bufferOffset < length)
        {
            if(cluster->next == 0) //No more clusters to read, just return what we've got so far
                return buffer;

            clusterIndex = cluster->next;
            writePos = getWritePosition(clusterIndex);
            delete cluster;
            cluster = readClusterHeader(clusterIndex);
            headerSize = CLUSTER_HEADER_SIZE;
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
        disk[index] = CLUSTER_FREE;

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
FilepathClusterInfo getClusterFromFilepath(uint32_t rootDirectory, uint32_t currentDirectory, uint8_t *path, uint32_t pathLength)
{
    //Reset to root directory if filepath is preceded with a '/'
    if(path[0] == '/')
    {
        currentDirectory = rootDirectory;
    }

    FilepathClusterInfo info;
    uint32_t oldDirInfo = currentDirectory;

    char *token = strtok((char*)path, "/");
    while(token)
    {
        //Get list of files in current directory and check if this name matches any of them
        uint32_t dirSize = getDirectorySize(currentDirectory);
        for(uint32_t a = 0; a < dirSize; a++)
        {
            uint32_t node = getDirectoryObject(currentDirectory, a);
            NodeHeader *nodeData = readNodeHeader(node);
            //If we've found a file match
            if(strcmp((char*)nodeData->nameData, token) == 0)
            {
                info.relativeIndex = a;
                oldDirInfo = currentDirectory;
                currentDirectory = node;
                //If 'currentDirectory' is a file, return
                NodeHeader *h = readNodeHeader(currentDirectory);
                if(h->type == NODE_FILE)
                {
                    freeNodeHeader(h);
                    info.objectIndex = currentDirectory;
                    info.ownerIndex = oldDirInfo;
                    return info;
                }
                freeNodeHeader(h);
            }
            freeNodeHeader(nodeData);
        }
        token = strtok(NULL, "/");
    }
    info.objectIndex = currentDirectory;
    info.ownerIndex = oldDirInfo;
    return info;
}
