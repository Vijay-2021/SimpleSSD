
#include "block.h"
#include "embext.h"
#include "embext_directory.h"
#include "utils.h"
#include "string.h"
#include "memory.h"

int ext2_append_to_directory(struct ext2context *context, char *directory, uint32_t inode, 
                             char *filename) {
    int file_length, last_block, i, this_offset;
    int minimum_new_entry_len, minimum_old_entry_len;
    int block_size = ext2_block_size(context);
    char buffer[128];
    struct ext2_dir_header dir_header;
    struct file_ent *fe = ext2_open(context, directory, O_RDWR, 01777);

    printf("\next2_append_to_directory(%p, %s, %u, %s, %p)\n", context, directory,
           inode, filename);
    if(fe == NULL) {
        return -1;
    }
    file_length = ext2_lseek(fe, 0, SEEK_END);
    if(file_length % block_size != 0) {
        ext2_close(fe);
        return -1;
    }
    last_block = file_length - block_size;
    if(last_block < 0) {
        ext2_close(fe);
        return -1;
    }
    this_offset = 0;
    memset(&dir_header, 0, sizeof(dir_header));
    do {
        this_offset += dir_header.rec_len;
        if(ext2_lseek(fe, last_block + this_offset, SEEK_SET) != last_block + this_offset) {
            ext2_close(fe);
            return -1;
        }
        if(ext2_read(fe, &dir_header, sizeof(dir_header)) != sizeof(dir_header)) {
            ext2_close(fe);
            return -1;
        }
    } while(this_offset + dir_header.rec_len < block_size);

    minimum_old_entry_len = ((dir_header.name_len / 4) + (dir_header.name_len % 4 ? 1 : 0)) * 4 + 8;
    minimum_new_entry_len = ((strlen(filename) / 4) + (strlen(filename) % 4 ? 1 : 0)) * 4 + 8;
    
    if(dir_header.rec_len - minimum_old_entry_len > minimum_new_entry_len) {
        printf("\nLast record length = %d, name_len = %d, appending in block\n",
               dir_header.rec_len, dir_header.name_len);
        /* there is room in the existing block to add another entry so shrink the old one and add
         * the new one */
        dir_header.rec_len = minimum_old_entry_len;
        ext2_lseek(fe, this_offset + last_block, SEEK_SET);
        ext2_write(fe, &dir_header, sizeof(dir_header));
        this_offset += dir_header.rec_len;
        ext2_lseek(fe, this_offset + last_block, SEEK_SET);
        dir_header.rec_len = block_size - this_offset;
        dir_header.name_len = strlen(filename);
        dir_header.file_type = 0;
        dir_header.inode = inode;
        ext2_write(fe, &dir_header, sizeof(dir_header));
        ext2_write(fe, filename, strlen(filename));
    } else {
        printf("\nLast record length = %d, name_len = %d, creating new block\n",
               dir_header.rec_len, dir_header.name_len);
        /* there is not enough room in this block to append another entry, add a whole new block. */
        ext2_lseek(fe, 0, SEEK_END);
        memset(&buffer, 0, sizeof(buffer));
        for(i=0;i<(int)(block_size / sizeof(buffer));i++) {
            ext2_write(fe, &buffer, sizeof(buffer));
        }
        ext2_lseek(fe, -block_size, SEEK_END);
        dir_header.rec_len = block_size;
        dir_header.name_len = strlen(filename);
        dir_header.file_type = 0;
        dir_header.inode = inode;
        ext2_write(fe, &dir_header, sizeof(dir_header));
        ext2_write(fe, filename, strlen(filename));
    }
    ext2_close(fe);
    return 0;
}

int ext2_delete_from_directory(struct ext2context *context, char *filename) {

}
