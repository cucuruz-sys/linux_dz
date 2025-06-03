import os
import random
import string
import fcntl

# IOCTL Commands
MYFS_RESET_ALL_FILES = 0x123400
MYFS_ERASE_FS = 0x123401
MYFS_GET_META_INFO = 0x123402
MYFS_GET_FILE_SECTORS = 0x123403

def random_string(length):
    return ''.join(random.choice(string.ascii_letters) for _ in range(length))

def test_files(mount_point):
    print("[+] Testing read/write on all files...")
    files = [f for f in os.listdir(mount_point)]
    for fname in files:
        path = os.path.join(mount_point, fname)
        data = random_string(1024)
        try:
            with open(path, 'wb') as f:
                f.write(data.encode())
            with open(path, 'rb') as f:
                read_data = f.read().decode()
            assert data == read_data
            print(f"[OK] {fname}")
        except Exception as e:
            print(f"[FAIL] {fname}: {e}")

def ioctl(fd, cmd, arg=0):
    return fcntl.ioctl(fd, cmd, arg)

if __name__ == '__main__':
    MOUNT_POINT = "/mnt"
    DISK_DEVICE = "/dev/loop0"

    print("[*] Starting tests...")

    # Тестирование файлов
    test_files(MOUNT_POINT)

    # Пример IOCTL вызова: получить сектора файла
    try:
        fd = os.open(DISK_DEVICE, os.O_RDONLY)
        fname = input("Enter filename to get sector mapping: ")
        class SectorInfo:
            def __init__(self):
                self.filename = fname.ljust(64, '\0')[:63] + '\0'
                self.sectors = [0] * 8  # максимум 8 секторов

        import ctypes
        class IoctlStruct(ctypes.Structure):
            _fields_ = [
                ("filename", ctypes.c_char * 64),
                ("sectors", ctypes.c_ulonglong * 8),
            ]

        s = IoctlStruct()
        s.filename = fname.encode()[:63] + b'\0'

        print("Calling IOCTL...")
        res = fcntl.ioctl(fd, MYFS_GET_FILE_SECTORS, s)
        print("Sectors:", list(s.sectors))
        os.close(fd)
    except Exception as e:
        print("[ERROR] IOCTL failed:", e)