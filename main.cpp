#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include "filesystem.h"

int isDirectory(const std::string &path) {
   struct stat statbuf;
   if (stat(path.c_str(), &statbuf) != 0)
       return 0;
   return S_ISDIR(statbuf.st_mode);
}

void packStructure(const std::string &filepath, uint32_t rootDirectory)
{
    DIR *pdir = NULL;
    pdir = opendir(filepath.c_str());
    struct dirent *pent = NULL;

    if(pdir == NULL)
    {
        std::cout << "Couldn't initialise directory" << std::endl;
        return;
    }

    while((pent = readdir(pdir)))
    {
        if(pent == NULL)
        {
            std::cout << "Couldn't read directory entry" << std::endl;
        }
        std::string strName = pent->d_name;
        if(strName == ".." || strName == ".")
            continue;
        if(isDirectory(filepath + "/" + strName))
        {
            //Add object to disk
            uint32_t newFile = fs_createObject(NODE_DIRECTORY, 0, strName.size(), (uint8_t*)strName.c_str());

            //Add to current directory
            fs_addObjectToDirectory(rootDirectory, newFile);

            //Now recursively search this directory, passing root as the newly created directory
            packStructure(filepath + "/" + strName, newFile);
        }
        else
        {
            //Add object to disk
            uint32_t newFile = fs_createObject(NODE_FILE, 0, strName.size(), (uint8_t*)strName.c_str());

            //Add to current directory
            fs_addObjectToDirectory(rootDirectory, newFile);

            //Read actual file data and copy into disk
            std::ifstream file(filepath + "/" + strName);
            if(!file.is_open())
            {
                std::cout << "Failed to read: " << filepath + "/" + strName << std::endl;
                return;
            }
            std::string fileData;

            //Get file size
            file.seekg(0, file.end);
            uint32_t fileSize = file.tellg();
            file.seekg(0, file.beg);
            fileData.resize(fileSize);

            //Read in that many bytes into fileData
            file.read(&fileData[0], fileSize);

            //Write that many bytes to disk
            fs_write(newFile, (uint8_t*)&fileData[0], fileSize);

            fileData.clear();
            file.close();
        }

    }
    closedir (pdir);
}


int main()
{
    std::cout << "\nPreparing RAM disk... ";
    //Install filesystem to ramdisk
    fs_formatDisk();
    std::cout << "Done. " << std::endl;
    std::cout << "Disk size: " << DISK_SIZE
              << "\nCluster size: " << CLUSTER_SIZE
              << "\nUsable disk space: " << DISK_SIZE-(CLUSTER_SIZE * 4) << std::endl;


    uint8_t rootName[] = "root";
    uint32_t rootDirectory = fs_createObject(NODE_DIRECTORY, 0, 4, rootName);
    uint32_t currentDirectory = rootDirectory;

    packStructure(".", rootDirectory);

    uint32_t sz = fs_getWritePosition(lastAllocationPosition) + CLUSTER_SIZE;
    std::ofstream file("disk.ffs", std::ios::binary | std::ios::out);
    if(!file.is_open())
        return 1;
    char *arr = (char*)fs_getDisk();
    file.write(&arr[0], sz);
    file.close();
    return 0;

    while(true)
    {
        std::cout << "$: ";
        std::string command, args, args2;
        std::cin >> command;

        if(command == "mkdir")
        {
            std::cin >> args;
            uint32_t newObject = fs_createObject(NODE_DIRECTORY, 0, args.size(), (uint8_t*)&args[0]);
            fs_addObjectToDirectory(currentDirectory, newObject);
        }
        else if(command == "rm")
        {
            std::cin >> args;
            FilepathClusterInfo info = fs_getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size());
            if(info.objectIndex == currentDirectory)
            {
                continue;
            }
            fs_removeObjectFromDirectory(info.ownerIndex, info.relativeIndex);
            fs_freeObject(info.objectIndex);
        }
        else if(command == "ls")
        {
            uint32_t dirSize = fs_getDirectorySize(currentDirectory);
            for(uint32_t a = 0; a < dirSize; a++)
            {
                uint32_t node = fs_getDirectoryObject(currentDirectory, a);
                NodeHeader *nodeData = fs_readNodeHeader(node);
                for(uint16_t b = 0; b < nodeData->nameLength; b++)
                    std::cout << nodeData->nameData[b];
                std::cout << std::endl;
                fs_freeNodeHeader(nodeData);
            }
        }
        else if(command == "cd")
        {
            std::cin >> args;
            uint32_t file = fs_getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(file == currentDirectory)
            {
                std::cout << args << " not found" << std::endl;
                continue;
            }
            NodeHeader *node = fs_readNodeHeader(file);
            if(node->type != NODE_DIRECTORY)
            {
                fs_freeNodeHeader(node);
                std::cout << args << " is not a directory" << std::endl;
                continue;
            }
            fs_freeNodeHeader(node);
            currentDirectory = file;
        }
        else if(command == "touch")
        {
            std::cin >> args >> args2;
            uint32_t doesExist = fs_getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(doesExist != currentDirectory)
            {
                std::cout << args << " already exists" << std::endl;
                continue;
            }

            char *last = strrchr(&args[0], '/');
            uint32_t obj = 0;
            if(last != NULL)
            {
                obj = fs_createObject(NODE_FILE, 0, args.size(), (uint8_t*)last+1);
                uint32_t file = fs_getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
                fs_addObjectToDirectory(file, obj);
            }
            else
            {
                obj = fs_createObject(NODE_FILE, 0, args.size(), (uint8_t*)&args[0]);
                fs_addObjectToDirectory(currentDirectory, obj);
            }
            fs_write(obj, (uint8_t*)&args2[0], args2.size());
        }
        else if(command == "less")
        {
            std::cin >> args;
            uint32_t file = fs_getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(file == currentDirectory)
            {
                std::cout << args << " not found" << std::endl;
                continue;
            }
            NodeHeader *node = fs_readNodeHeader(file);
            if(node->type != NODE_FILE)
            {
                std::cout << args << " is not a file" << std::endl;
                continue;
            }
            delete node;
            uint32_t fileSize = fs_getFileSize(file);
            uint8_t *b = fs_read(file, fileSize);
            for(uint32_t c = 0; c < fileSize; c++)
                std::cout << b[c];
            std::cout << std::endl;
            delete b;
        }
        else if(command == "sizeof")
        {
            std::cin >> args;
            uint32_t file = fs_getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(file == currentDirectory)
            {
                std::cout << args << " not found" << std::endl;
                continue;
            }
            NodeHeader *node = fs_readNodeHeader(file);
            if(node->type != NODE_FILE)
            {
                std::cout << args << " is not a file" << std::endl;
                continue;
            }
            std::cout << fs_getFileSize(file) << " bytes" << std::endl;
        }
        else
        {
            std::cout << "Command not recognised!" << std::endl;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        }
    }
    return 0;
}





