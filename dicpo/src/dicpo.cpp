//
// Created by Jonah Morgan on 4/15/24.
//
#include <driver.h>
#include <sfs_superblock.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sfs_inode.h>
#include <sfs_dir.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>


char* read_inode_blocks(const sfs_inode_t* inode, int amount_bytes) {
    int block_size = 128;
    char* data = (char*)malloc(sizeof(char) * amount_bytes);

    size_t current_size = 0;
    char curr_block_data[128];
    int done = 0;

    // direct pointers
    for (int i = 0; i < NUM_DIRECT; i++) {
        if (inode->direct[i] == 0 || current_size >= amount_bytes) {
            done = 1;
            break;
        }

        driver_read(curr_block_data, inode->direct[i]);

        int mem_amount = amount_bytes - current_size >= block_size ? block_size : amount_bytes % block_size;
        memcpy(data + current_size, curr_block_data, mem_amount);
        current_size += mem_amount;
    }

    if (done || current_size >= amount_bytes) return data;

    // indirect pointers
    char buffer[128];
    uint32_t* block_ptr = (uint32_t*)buffer;
    driver_read(block_ptr, inode->indirect);

    int amountPtrs = block_size / sizeof(uint32_t);
    for(int i = 0; i < amountPtrs; i++) {
        if(*block_ptr == 0 || current_size >= amount_bytes) {
            done = 1;
            break;
        }

        driver_read(curr_block_data, *block_ptr);

        int mem_amount = amount_bytes - current_size >= block_size ? block_size : amount_bytes % block_size;
        memcpy(data + current_size, curr_block_data, mem_amount);
        current_size += mem_amount;

        block_ptr++;
    }

    if(done || current_size >= amount_bytes) return data;

    // double indirect
    uint32_t* dindirect = (uint32_t*)buffer;
    driver_read(dindirect, inode->dindirect);

    for(int i = 0; i < amountPtrs; i++) {
        char indirect_buffer[block_size];
        uint32_t* block_ptrs = (uint32_t*)indirect_buffer;
        driver_read(block_ptrs, *dindirect);

        for(int j = 0; j < amountPtrs; j++) {
            if(*block_ptrs == 0 || current_size >= amount_bytes) {
                done = 1;
                break;
            }

            driver_read(curr_block_data, *block_ptrs);

            int mem_amount = amount_bytes - current_size >= block_size ? block_size : amount_bytes % block_size;
            memcpy(data + current_size, curr_block_data, mem_amount);
            current_size += mem_amount;

            block_ptrs++;
        }

        dindirect++;
    }

    if(done || current_size >= amount_bytes) return data;

    // triple indirect
    uint32_t* tindirect = (uint32_t*)buffer;
    driver_read(tindirect, inode->tindirect);

    for(int k = 0; k < amountPtrs; k++) {
        // double indirect
        char* dindirect_buffer[128];
        uint32_t* dindirect = (uint32_t*)dindirect_buffer;
        driver_read(dindirect, *tindirect);

        for(int i = 0; i < amountPtrs; i++) {
            char indirect_buffer[block_size];
            uint32_t* block_ptrs = (uint32_t*)indirect_buffer;
            driver_read(block_ptrs, *dindirect);

            for(int j = 0; j < amountPtrs; j++) {
                if(*block_ptrs == 0 || current_size >= amount_bytes) {
                    done = 1;
                    break;
                }

                driver_read(curr_block_data, *block_ptrs);

                int mem_amount = amount_bytes - current_size >= block_size ? block_size : amount_bytes % block_size;
                memcpy(data + current_size, curr_block_data, mem_amount);
                current_size += mem_amount;

                block_ptrs++;
            }

            dindirect++;
        }

        if(done || current_size >= amount_bytes) return data;

        tindirect++;
    }

    return data;
}


sfs_inode_t* get_inode_from_table(char* buffer, sfs_superblock* superblock, int table_idx) {
    int offset = 0;

    if(table_idx % 2 == 1) {
        offset = 1;
    }

    table_idx = table_idx / 2;

    sfs_inode_t* inode = (sfs_inode_t*)buffer;
    driver_read(inode, superblock->inodes + table_idx);

    if(offset == 1) {
        inode++;
    }

    return inode;
}


void printUsageStatement() {
    printf("Usage: dicpo DISK_IMAGE_PATH FILENAME\n");
    printf("Copy a file from the specified DISK_IMAGE_PATH to the current directory.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  DISK_IMAGE_PATH   Path to the disk image containing the file\n");
    printf("  FILENAME          Name of the file to copy from the disk image\n");
    printf("\n");
    printf("Example:\n");
    printf("  dicpo disk.img myfile.txt\n");
}


int main(int argc, char** argv) {
    if(argc != 3) {
        printUsageStatement();
        exit(1);
    }

    driver_attach_disk_image(argv[1], 128);
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int file = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, mode);
    if(file < 0) {
        perror("Error opening file");
        return 1;
    }

    char raw_superblock[128];
    sfs_superblock *super = (sfs_superblock *)raw_superblock;

    int found = 0;
    int superblock_index = 0;

    // Find the superblock
    while (found == 0) {
        driver_read(super, superblock_index);

        if (super->fsmagic == VMLARIX_SFS_MAGIC &&
            strcmp(super->fstypestr, VMLARIX_SFS_TYPESTR) == 0) {
            found = 1;
        }

        superblock_index++;
    }

    // Read the root directory block
    char buffer[128];
    sfs_inode_t* inode = (sfs_inode_t*) buffer;

    driver_read(inode, super->inodes);

    // Get the blocks from the root inode.
    char* block_data = read_inode_blocks(inode, inode->size);
    int amount_dirs = inode->size / sizeof(sfs_dirent);
    sfs_dirent* entries = (sfs_dirent*)block_data;

    int entry_found = 0;
    for(int i = 0; i < amount_dirs; i++) {
        if(strcmp(entries->name, argv[2]) == 0) {
            entry_found = 1;
            inode = get_inode_from_table(buffer, super, entries->inode);
        }
        entries++;
    }
    free(block_data);

    if(!entry_found) {
        printf("%s was not found in %s", argv[2], argv[1]);
        close(file);
        return 1;
    }

    block_data = read_inode_blocks(inode, inode->size);
    write(file, block_data, inode->size);

    free(block_data);
    close(file);
    driver_detach_disk_image();
    return 0;
}
