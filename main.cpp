#include <iostream>
#include <stdint.h>
using namespace std;

enum ClusterType
{
    CLUSTER_FILE = 0x0,
    CLUSTER_DIRECTORY = 0x1,
    CLUSTER_SYMLINK = 0x2,
};

enum ClusterState
{
    CLUSTER_FREE = 0x0,
    CLUSTER_USED = 0x1,
};

struct NodeHeader //128 Bytes
{
    uint8_t type; //ClusterType. Type of node.
    uint32_t permissions; //Access permissions
    uint16_t nameLength; //Length of object name
    uint8_t *nameData; //Name data
    uint32_t nodeLength; //Actual length of node, is all of it being used?
    uint32_t next; //Index of the next cluster of the object
};

const uint32_t diskSize = 10485760;
const uint16_t clusterSize = 4096;
const uint32_t clusterCount = diskSize / clusterSize;
static uint8_t disk[diskSize]; //10MB

//Installs the filesystem on the disk
void formatDisk()
{
    //Set cluster index
    for(uint32_t a = 0; a < clusterCount; a++)
    {
        disk[a] = CLUSTER_FREE;
    }
}

int main()
{

    cout << "Hello world!" << endl;
    return 0;
}





