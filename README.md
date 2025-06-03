Сбилдить в ядро:
```
make
sudo insmod myfs.ko disk_name="/dev/loop0" n_offset_1=100 n_offset_2=200 max_filename_len=64 max_file_size_sectors=8
sudo mount -t myfs /dev/loop0 /mnt
```

Запуск теста:
```
python3 test_myfs.py
```