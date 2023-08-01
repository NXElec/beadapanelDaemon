# beadapanelDaemon
Beadapanel Deamon to handle Panel-Link flow on Raspberry Pi


<img src="https://github.com/NXElec/beadapanelDaemon/blob/master/bprpi.png" width="600"/>

### First time Setup
BeadaPanel will not work automatically when first time plugined to Raspberry Pi. Instead, A beadapanel deamon program should be running on Raspberry Pi to handle the data transfer between two USB peers. Below are procedures to setup this beadapanel deamon.

#### Pre-requirements
* A Raspberry Pi mini PC
* A BeadaPanel display kit

#### Procedures
1. Power on your Raspberry Pi mini PC
2. Connect BeadaPanel to Raspberry Pi by a Micro USB cable
3. Native compile beadapanel deamon on Raspberry Pi
```
git clone https://github.com/NXElec/beadapanelDaemon
cd beadapanelDaemon
make
```
4. Locate and run beadapanel deamon
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

### Compile beadapanel deamon under environment without brcm VC4/OpenMAX support:
```
git clone https://github.com/NXElec/beadapanelDaemon
cd beadapanelDaemon
make bpd-novc4
```
<br>

[BeadaPanel Official Page](http://www.nxelec.com/products/beadapanel-media-display)<br>
[BeadaPanel Wiki](https://www.elinux.org/BeadaPanel)
