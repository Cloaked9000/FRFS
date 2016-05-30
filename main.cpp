#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits>
#include "filesystem.h"


int main()
{
    std::cout << "\nPreparing RAM disk... ";
    //Install filesystem to ramdisk
    formatDisk();
    std::cout << "Done. " << std::endl;
    std::cout << "Disk size: " << DISK_SIZE
              << "\nCluster size: " << CLUSTER_SIZE
              << "\nUsable disk space: " << DISK_SIZE-(CLUSTER_SIZE * 4) << std::endl;


    uint8_t rootName[] = "root";
    uint32_t rootDirectory = createObject(NODE_DIRECTORY, 0, 4, rootName);
    uint32_t currentDirectory = rootDirectory;
    for(uint32_t a = 0; a < 500; a++)
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
            FilepathClusterInfo info = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size());
            if(info.objectIndex == currentDirectory)
            {
                continue;
            }
            removeObjectFromDirectory(info.ownerIndex, info.relativeIndex);
            freeObject(info.objectIndex);
        }
        else if(command == "ls")
        {
            uint32_t dirSize = getDirectorySize(currentDirectory);
            for(uint32_t a = 0; a < dirSize; a++)
            {
                uint32_t node = getDirectoryObject(currentDirectory, a);
                NodeHeader *nodeData = readNodeHeader(node);
                for(uint16_t b = 0; b < nodeData->nameLength; b++)
                    std::cout << nodeData->nameData[b];
                std::cout << std::endl;
                freeNodeHeader(nodeData);
            }
        }
        else if(command == "cd")
        {
            std::cin >> args;
            uint32_t file = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(file == currentDirectory)
            {
                std::cout << args << " not found" << std::endl;
                continue;
            }
            NodeHeader *node = readNodeHeader(file);
            if(node->type != NODE_DIRECTORY)
            {
                freeNodeHeader(node);
                std::cout << args << " is not a directory" << std::endl;
                continue;
            }
            freeNodeHeader(node);
            currentDirectory = file;
        }
        else if(command == "touch")
        {
            std::cin >> args >> args2;
            uint32_t doesExist = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(doesExist != currentDirectory)
            {
                std::cout << args << " already exists" << std::endl;
                continue;
            }

            char *last = strrchr(&args[0], '/');
            uint32_t obj = 0;
            if(last != NULL)
            {
                obj = createObject(NODE_FILE, 0, args.size(), (uint8_t*)last+1);
                uint32_t file = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
                addObjectToDirectory(file, obj);
            }
            else
            {
                obj = createObject(NODE_FILE, 0, args.size(), (uint8_t*)&args[0]);
                addObjectToDirectory(currentDirectory, obj);
            }
            write(obj, (uint8_t*)&args2[0], args2.size());
        }
        else if(command == "less")
        {
            std::cin >> args;
            uint32_t file = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(file == currentDirectory)
            {
                std::cout << args << " not found" << std::endl;
                continue;
            }
            NodeHeader *node = readNodeHeader(file);
            if(node->type != NODE_FILE)
            {
                std::cout << args << " is not a file" << std::endl;
                continue;
            }
            delete node;
            uint32_t fileSize = getFileSize(file);
            uint8_t *b = read(file, fileSize);
            for(uint32_t c = 0; c < fileSize; c++)
                std::cout << b[c];
            std::cout << std::endl;
            delete b;
        }
        else if(command == "sizeof")
        {
            std::cin >> args;
            uint32_t file = getClusterFromFilepath(rootDirectory, currentDirectory, (uint8_t*)&args[0], args.size()).objectIndex;
            if(file == currentDirectory)
            {
                std::cout << args << " not found" << std::endl;
                continue;
            }
            NodeHeader *node = readNodeHeader(file);
            if(node->type != NODE_FILE)
            {
                std::cout << args << " is not a file" << std::endl;
                continue;
            }
            std::cout << getFileSize(file) << " bytes" << std::endl;
        }
        else
        {
            std::cout << "Command not recognised!" << std::endl;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        }
    }
    return 0;
}





