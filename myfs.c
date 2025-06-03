#define MYFS_MAGIC 0x4D594653
#define MAX_FILES 100
#define MAX_FILENAME_LEN 64
#define SECTOR_SIZE 512

// Superblock
struct myfs_super_block {
    u32 magic;
    u32 crc32;
    u64 total_sectors;
    u64 num_files;
    u64 max_file_size_sectors;
    u64 file_table_start;
    u64 data_start;
};

struct myfs_file_entry {
    char name[MAX_FILENAME_LEN];
    u64 sector_count;
    u64 sectors[0];
};

// IOCTLS
#define MYFS_IOCTL_BASE 'm'
#define MYFS_RESET_ALL_FILES   _IO(MYFS_IOCTL_BASE, 0)
#define MYFS_ERASE_FS          _IO(MYFS_IOCTL_BASE, 1)
#define MYFS_GET_META_INFO     _IOR(MYFS_IOCTL_BASE, 2, char *)
#define MYFS_GET_FILE_SECTORS  _IOWR(MYFS_IOCTL_BASE, 3, struct myfs_ioctl_get_sectors)

struct myfs_ioctl_get_sectors {
    char filename[MAX_FILENAME_LEN];
    u64 sectors[0];
};