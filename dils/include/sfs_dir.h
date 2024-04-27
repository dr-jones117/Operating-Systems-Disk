//
// Created by Jonah Morgan on 4/15/24.
//

#ifndef DILS_SFS_DIR_H
#define DILS_SFS_DIR_H

/* We define a very simple directory structure.  Each directory entry
   consists of a null-terminated string of up to 28 characters,
   followed by an inode number.
*/

#define SFS_NAME_MAX (32-sizeof(uint32_t))

typedef struct {
    char name[SFS_NAME_MAX];
    uint32_t inode;
}sfs_dirent;



#endif //DILS_SFS_DIR_H
