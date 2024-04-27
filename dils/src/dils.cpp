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

void print_inode_details(const sfs_inode_t *inode, const char *pathname) {
    const char *file_types[] = {
            "Regular file",
            "Directory",
    };

    const char *type_str = (inode->type < 7) ? file_types[inode->type] : "Unknown type";
    if(strcmp(type_str, "Directory") == 0) {
        printf("d");
    }
    else {
        printf("-");
    }

    char perm_str[11];
    snprintf(perm_str, sizeof(perm_str), "%c%c%c%c%c%c%c%c%c",
             (inode->perm & 0400) ? 'r' : '-',
             (inode->perm & 0200) ? 'w' : '-',
             (inode->perm & 0100) ? ((inode->perm & 04000) ? 's' : 'x') : ((inode->perm & 04000) ? 'S' : '-'),
             (inode->perm & 040) ? 'r' : '-',
             (inode->perm & 020) ? 'w' : '-',
             (inode->perm & 010) ? ((inode->perm & 02000) ? 's' : 'x') : ((inode->perm & 02000) ? 'S' : '-'),
             (inode->perm & 04) ? 'r' : '-',
             (inode->perm & 02) ? 'w' : '-',
             (inode->perm & 01) ? ((inode->perm & 01000) ? 't' : 'x') : ((inode->perm & 01000) ? 'T' : '-'));

    char atime_str[20];

    time_t atime_t = (time_t)inode->atime;

    struct tm *atime_tm = localtime(&atime_t);

    strftime(atime_str, sizeof(atime_str), "%b %e %H:%M %Y", atime_tm);

    printf("%s%3hhu %5u %5u %6lu %s %s\n",
           perm_str, inode->refcount, inode->owner, inode->group, (unsigned long)inode->size, atime_str, pathname);
}


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
    printf("Usage: dils [OPTIONS] DISK_IMAGE_PATH\n\n");
    printf("Description:\n");
    printf("  dils (disk image list) is a tool for listing the contents of the root directory\n");
    printf("  from a given disk image file.\n\n");
    printf("Options:\n");
    printf("  -l          Print the listing in long format (detailed information).\n");
    printf("              If not specified, the listing will be in regular format.\n\n");
    printf("Arguments:\n");
    printf("  DISK_IMAGE_PATH   Path to the disk image file containing the root directory\n");
    printf("                    to be listed.\n\n");
    printf("Example:\n");
    printf("  dils initrd\n");
    printf("    Print the root directory listing from the disk image file 'initrd' in regular format.\n\n");
    printf("  dils -l initrd\n");
    printf("    Print the root directory listing from the disk image file 'initrd' in long format.\n");
}

int main(int argc, char** argv) {
    uint8_t long_listing = 0;

    // Argument handling
    if(argc > 3 || argc < 2) {
        printUsageStatement();
        return 1;
    }
    else if(argc == 3 && strcmp(argv[1], "-l") != 0) {
        printUsageStatement();
        return 1;
    }
    else if(argc == 3 && strcmp(argv[1], "-l") == 0) {
        long_listing = 1;
    }

    // If -l mode, the arguments will be in different location
    if(long_listing == 1)
        driver_attach_disk_image(argv[2], 128);
    else
        driver_attach_disk_image(argv[1], 128);


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

    if(long_listing == 0) {
        for(int i = 0; i < amount_dirs; i++) {
            printf("%s\n", entries->name);
            entries++;
        }
    }
    else {
        for(int i = 0; i < amount_dirs; i++) {
            inode = get_inode_from_table(buffer, super, entries->inode);
            print_inode_details(inode, entries->name);
            entries++;
        }
    }

    free(block_data);
    driver_detach_disk_image();
    return 0;
}
