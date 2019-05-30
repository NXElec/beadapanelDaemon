# beadapanelDaemon
Beadapanel Deamon to handle the Panel-Link flow transmit on Raspberry Pi


<img src="https://github.com/NXElec/beadapanelDaemon/blob/master/bprpi.png" width="600"/><br>

How to compile:
```
gcc -I/usr/include/libusb-1.0 -o bpd bpd.c -L/usr/lib -lusb-1.0 -lpthread -DBPD_VC4_ENABLE
```

Compile without brcm VC4/OpenMAX support:
```
gcc -I/usr/include/libusb-1.0 -o bpd bpd.c -L/usr/lib -lusb-1.0 -lpthread
```

[BeadaPanel Official Page](http://www.nxelec.com/products/hmi/beadapanel-media-display)<br>
[BeadaPanel Wiki](https://www.elinux.org/BeadaPanel)
