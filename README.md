# beadapanelDaemon
Beadapanel Deamon to handle the Panel-Link flow transmit on Raspberry Pi


<img src="https://github.com/NXElec/beadapanelDaemon/blob/master/bprpi.png" width="600"/>

### First time Setup
BeadaPanel will not work automatically when first time plugin to Raspberry Pi. Instead, A beadapanel deamon program should be running on Raspberry Pi to handle the data transmit between two USB peers. Below are procedures to setup this beadapanel deamon.

#### Pre-requirements
* A Raspberry Pi mini PC
* A BeadaPanel display kit

#### Procedures
1. Power on your Raspberry Pi mini PC
2. Connect BeadaPanel to Raspberry Pi by a Micro USB cable
3. BeadaPanel will be automaticaly recognized as a USB mass storage device by Raspberry Pi
```
pi@raspberrypi:~ $ lsblk
NAME        MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
sda           8:0    0  9.1G  0 disk
├─sda1        8:1    0 43.9M  0 part /media/pi/boot
└─sda2        8:2    0  4.9G  0 part /media/pi/rootfs
mmcblk0     179:0    0  7.4G  0 disk
├─mmcblk0p1 179:1    0 43.9M  0 part /boot
└─mmcblk0p2 179:2    0  7.4G  0 part /
pi@raspberrypi:~ $ ls /media/pi/boot/bpd
bpd
```
4. Locate and run beadapanel deamon in /media/pi/boot/bpd folder
```
pi@raspberrypi:~ $ sudo cp /media/pi/boot/bpd/bpd /usr/bin 
pi@raspberrypi:~ $ sudo chmod +x /usr/bin/bpd
pi@raspberrypi:~ $ sudo bpd
BeadaPanel Daemon Ver. 1.0
```
5. Run beadapanel deamon from booting
```
$ sudo nano /etc/rc.local -> add new line before "exit 0" with "/usr/bin/bpd &" without quote 
$ sudo reboot
```
<br>

### How to do native compile on Raspberry Pi:
```
gcc -I/usr/include/libusb-1.0 -I/opt/vc/include -o bpd bpd.c -L/usr/lib -L/opt/vc/lib -lusb-1.0 -lpthread -lbcm_host -DBPD_VC4_ENABLE
```

Compile under environment without brcm VC4/OpenMAX:
```
gcc -I/usr/include/libusb-1.0 -o bpd bpd.c -L/usr/lib -lusb-1.0 -lpthread
```

<br>

[BeadaPanel Official Page](http://www.nxelec.com/products/hmi/beadapanel-media-display)<br>
[BeadaPanel Wiki](https://www.elinux.org/BeadaPanel)
