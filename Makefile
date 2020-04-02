bpd:
	cc -I/usr/include/libusb-1.0 -I/opt/vc/include -o bpd bpd.c -L/usr/lib -L/opt/vc/lib -lusb-1.0 -lpthread -lbcm_host -DBPD_VC4_ENABLE
bpd-novc4:
	cc -I/usr/include/libusb-1.0 -o bpd-novc4 bpd.c -L/usr/lib -lusb-1.0 -lpthread

.PHONY:clean
clean:
	$(RM) bpd bpd-novc4 
