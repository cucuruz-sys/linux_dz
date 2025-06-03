#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/crc32.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>

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

static unsigned long n_offset_1 = 100;
static unsigned long n_offset_2 = 200;
static int max_filename_len = MAX_FILENAME_LEN;
static int max_file_size_sectors = 8;

module_param(n_offset_1, ulong, 0);
module_param(n_offset_2, ulong, 0);
module_param(max_filename_len, int, 0);
module_param(max_file_size_sectors, int, 0);

MODULE_PARM_DESC(n_offset_1, "Offset for primary superblock");
MODULE_PARM_DESC(n_offset_2, "Offset for backup superblock");

static struct file_system_type myfs_type;
static struct inode_operations myfs_inode_ops;
static struct file_operations myfs_file_ops;
static struct super_operations myfs_super_ops;

static struct myfs_super_block *sb_main = NULL;
static struct myfs_super_block *sb_backup = NULL;
static struct myfs_file_entry **file_table = NULL;

// CRC32 check
static int verify_superblock(struct myfs_super_block *sb) {
    u32 crc = sb->crc32;
    sb->crc32 = 0;
    u32 computed_crc = crc32(0, sb, sizeof(*sb));
    sb->crc32 = crc;
    return computed_crc == crc && sb->magic == MYFS_MAGIC;
}

// Reading superblock
static struct myfs_super_block *read_superblock(struct super_block *sb, sector_t offset) {
    struct buffer_head *bh = sb_bread(sb, offset);
    if (!bh)
        return NULL;

    struct myfs_super_block *super = kmalloc(sizeof(struct myfs_super_block), GFP_KERNEL);
    memcpy(super, bh->b_data, sizeof(*super));
    brelse(bh);

    if (!verify_superblock(super)) {
        kfree(super);
        return NULL;
    }

    return super;
}

// meta
static int read_file_table(struct super_block *sb) {
    struct myfs_super_block *fs_sb = sb->s_fs_info;
    size_t table_size = fs_sb->num_files * sizeof(struct myfs_file_entry);
    struct buffer_head *bh = sb_bread(sb, fs_sb->file_table_start);
    if (!bh)
        return -EIO;

    file_table = kmalloc_array(fs_sb->num_files, sizeof(struct myfs_file_entry *), GFP_KERNEL);
    for (int i = 0; i < fs_sb->num_files; ++i) {
        file_table[i] = kmalloc(sizeof(struct myfs_file_entry), GFP_KERNEL);
        memcpy(file_table[i], bh->b_data + i * sizeof(struct myfs_file_entry), sizeof(struct myfs_file_entry));
    }
    brelse(bh);
    return 0;
}

// Инициализация инода
static struct inode *myfs_get_inode(struct super_block *sb, umode_t mode, int idx) {
    struct inode *inode = new_inode(sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    inode_init_owner(&init_user_ns, inode, NULL, mode);
    inode->i_ino = idx;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

    if (S_ISREG(mode)) {
        inode->i_op = &myfs_inode_ops;
        inode->i_fop = &myfs_file_ops;
        inode->i_size = SECTOR_SIZE * file_table[idx - 1]->sector_count;
    } else if (S_ISDIR(mode)) {
        inode->i_op = &myfs_inode_ops;
        inode->i_fop = &simple_dir_operations;
        inode->i_size = 0;
    }

    return inode;
}

// Заполнение superblock
static int myfs_fill_super(struct super_block *sb, void *data, int silent) {
    struct buffer_head *bh;
    sb->s_blocksize = SECTOR_SIZE;
    sb->s_blocksize_bits = blksize_bits(SECTOR_SIZE);

    sb_main = read_superblock(sb, n_offset_1);
    sb_backup = read_superblock(sb, n_offset_2);

    if (!sb_main && !sb_backup)
        return -EINVAL;

    struct myfs_super_block *chosen_sb = sb_main ? sb_main : sb_backup;
    sb->s_fs_info = chosen_sb;

    if (read_file_table(sb))
        return -EIO;

    sb->s_magic = chosen_sb->magic;
    sb->s_op = &myfs_super_ops;

    struct inode *root_inode = myfs_get_inode(sb, S_IFDIR | 0755, 0);
    struct dentry *root_dentry = d_make_root(root_inode);
    sb->s_root = root_dentry;

    for (int i = 0; i < chosen_sb->num_files; ++i) {
        struct inode *inode = myfs_get_inode(sb, S_IFREG | 0644, i + 1);
        struct dentry *de = d_alloc_name(root_dentry, file_table[i]->name);
        d_add(de, inode);
    }

    return 0;
}

// Mount
static struct dentry *myfs_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, myfs_fill_super);
}

// Clearing
static void myfs_kill_super(void *sb) {
    kill_block_super(sb);
    if (sb_main) kfree(sb_main);
    if (sb_backup) kfree(sb_backup);
    if (file_table) {
        for (int i = 0; i < sb_main->num_files; ++i)
            kfree(file_table[i]);
        kfree(file_table);
    }
}


ssize_t myfs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct inode *inode = file_inode(file);
    int idx = inode->i_ino - 1;
    struct myfs_file_entry *fe = file_table[idx];

    struct super_block *sb = inode->i_sb;
    char *data = kmalloc(count, GFP_KERNEL);
    struct buffer_head *bh = sb_bread(sb, fe->sectors[*ppos / SECTOR_SIZE]);

    if (!bh) return -EIO;
    memcpy(data, bh->b_data + (*ppos % SECTOR_SIZE), count);
    copy_to_user(buf, data, count);
    *ppos += count;
    kfree(data);
    brelse(bh);
    return count;
}

ssize_t myfs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    struct inode *inode = file_inode(file);
    int idx = inode->i_ino - 1;
    struct myfs_file_entry *fe = file_table[idx];

    struct super_block *sb = inode->i_sb;
    char *data = kmalloc(count, GFP_KERNEL);
    copy_from_user(data, buf, count);

    struct buffer_head *bh = sb_bread(sb, fe->sectors[*ppos / SECTOR_SIZE]);
    if (!bh) return -EIO;
    memcpy(bh->b_data + (*ppos % SECTOR_SIZE), data, count);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    kfree(data);
    *ppos += count;
    return count;
}

const struct inode_operations myfs_inode_ops = {
    .lookup = simple_lookup,
};

const struct file_operations myfs_file_ops = {
    .read = myfs_read,
    .write = myfs_write,
    .llseek = generic_file_llseek,
};

const struct super_operations myfs_super_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static struct file_system_type myfs_type = {
    .owner = THIS_MODULE,
    .name  = "myfs",
    .mount = myfs_mount,
    .kill_sb = myfs_kill_super,
    .fs_flags = FS_REQUIRES_DEV,
};

// IOCTL
long myfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct myfs_super_block *fs_sb = sb->s_fs_info;

    switch(cmd) {
        case MYFS_RESET_ALL_FILES:
            printk(KERN_INFO "Resetting all files\n");
            break;
        case MYFS_ERASE_FS:
            printk(KERN_INFO "Erasing filesystem\n");
            break;
        case MYFS_GET_META_INFO:
            printk(KERN_INFO "Getting metadata hashes\n");
            break;
        case MYFS_GET_FILE_SECTORS: {
            struct myfs_ioctl_get_sectors *info = kmalloc(sizeof(*info) + fs_sb->max_file_size_sectors * sizeof(u64), GFP_KERNEL);
            if (copy_from_user(info, (void __user *)arg, sizeof(*info)))
                return -EFAULT;

            for (int i = 0; i < fs_sb->num_files; ++i) {
                if (strcmp(file_table[i]->name, info->filename) == 0) {
                    memcpy(info->sectors, file_table[i]->sectors, file_table[i]->sector_count * sizeof(u64));
                    copy_to_user((void __user *)arg, info, sizeof(*info) + file_table[i]->sector_count * sizeof(u64));
                    break;
                }
            }
            kfree(info);
            break;
        }
        default:
            return -ENOTTY;
    }
    return 0;
}

static int __init myfs_init(void) {
    return register_filesystem(&myfs_type);
}

static void __exit myfs_exit(void) {
    unregister_filesystem(&myfs_type);
}

module_init(myfs_init);
module_exit(myfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vasilkov S.K.");
MODULE_DESCRIPTION("Simple custom filesystem with dual superblocks and IOCTL support");