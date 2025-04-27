#ifndef FAT12_H
#define FAT12_H

void fat12_init();
int fat12_read_file(const char* filename, char* buffer, int size);

#endif // FAT12_H