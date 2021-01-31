/* -*- Mode: C; indent-tabs-mode:t ; c-basic-offset:8 -*- */
/*
 * libusb example program for hotplug API
 * Copyright © 2012-2013 Nathan Hjelm <hjelmn@mac.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
/*
gcc -I/usr/include/libusb-1.0 -o bpd bpd.c -L/usr/lib -lusb-1.0 -lpthread -DBPD_VC4_ENABLE
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include "libusb.h"

#ifdef BPD_VC4_ENABLE
#include "bcm_host.h"
#endif 



/******************** Messages and Errors ***********************************/
static const char argv0[] = "bpd";
static unsigned int verbosity = 6;
static const char ver_str[] = "BeadaPanel Daemon Ver. 1.1";
static const char usage_str[] = "Usage:\n"
                              " -h Help\n"
                              " -f Update frequency(fps). e.g. -f 30\n";

void set_verbosity(unsigned int vb)
{
	verbosity = vb;
}

void _msg(unsigned level, const char *fmt, ...)
{
	if (level < 2)
		level = 2;
	else if (level > 7)
		level = 7;

	if (level <= verbosity) {
		static const char levels[8][6] = {
			[2] = "crit:",
			[3] = "err: ",
			[4] = "warn:",
			[5] = "note:",
			[6] = "info:",
			[7] = "dbg: "
		};

		int _errno = errno;
		va_list ap;

		fprintf(stderr, "%s: %s ", argv0, levels[level]);
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);

		if (fmt[strlen(fmt) - 1] != '\n') {
			char buffer[128];
			strerror_r(_errno, buffer, sizeof buffer);
			fprintf(stderr, ": (-%d) %s\n", _errno, buffer);
		}

		fflush(stderr);
	}
}

#define die(...)  (_msg(2, __VA_ARGS__), exit(1))
#define err(...)   _msg(3, __VA_ARGS__)
#define warn(...)  _msg(4, __VA_ARGS__)
#define note(...)  _msg(5, __VA_ARGS__)
#define info(...)  _msg(6, __VA_ARGS__)
#define debug(...) _msg(7, __VA_ARGS__)

#define die_on(cond, ...) do { \
	if (cond) \
		die(__VA_ARGS__); \
	} while (0)

/********************  Panel-Link Daemon  ***********************************/
unsigned short checkSum16(unsigned short *buf, int nword)
{
	unsigned long sum;

	for (sum = 0; nword > 0; nword--)
		sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return ~sum;
}

/* Define for pack type, per PanelLink protocol. */
#define TYPE_START 1
#define TYPE_END 2
#define TYPE_RESET 3
#define TYPE_CLS 4
#define FMT_STR_LEN 256

const char protocol_str[] = "PANEL-LINK";

typedef struct _PANELLINK_STREAM_TAG {
	char protocol_name[10];
	unsigned char version;
	unsigned char type;
	char fmtstr[FMT_STR_LEN];
	unsigned short checksum16;
} __attribute__((packed)) PANELLINK_STREAM_TAG;

/******************** Global Variables ***********************************/
/* Due to limit of linux read/write IO, we have to have a max buffer size of 12800. */
#define PL_BUFF_SIZE (512*25)

#define STREAM_DAEMON_INTERVAL 10000
#define STREAM_FPS_INTERVAL 1000
#define FB_PATH_LENGTH 256
enum {VC4, STD_IN, FB};
enum {BP, STD_OUT};
enum {idle, quit, busy};

static int stream_src = VC4;
static int stream_to = BP;
static struct fb_var_screeninfo  vinfo;
static struct fb_fix_screeninfo  finfo;
static unsigned char *ptrFB;
static unsigned char *stream_buff;
static unsigned char *pingbuff;
static unsigned char *pongbuff;
static unsigned char *xfr_id;
static int buff_size;
static int fps_in_us = STREAM_FPS_INTERVAL;
static int bp_count;

static int data_in, data_out;
static int bp_stat;
static int bp_interface;
static unsigned char bp_ep;
static libusb_hotplug_callback_handle bp_cb1, bp_cb2;
static libusb_device_handle *bp_handle = NULL;
static struct libusb_transfer *bp_transfer = NULL;
static sem_t bp_sem;

#ifdef BPD_VC4_ENABLE
    DISPMANX_DISPLAY_HANDLE_T display;
    DISPMANX_MODEINFO_T display_info;
    DISPMANX_RESOURCE_HANDLE_T screen_resource;
    uint32_t image_prt;
    VC_RECT_T rect1;    
#endif

static int get_interface_ep(libusb_device *dev)
{
	int rc, i, j;
	struct libusb_config_descriptor *config;
	const struct libusb_interface_descriptor *id;
			
	if (LIBUSB_SUCCESS != (rc = libusb_get_config_descriptor(dev, 0, &config))) {
		err ("Error getting device config descriptor - %s\n", libusb_error_name(rc));
		return -1;
	}

	for (i = 0; i < config->bNumInterfaces; i++) {
		id = &config->interface[i].altsetting[0];
		if ((id->bInterfaceClass == 255) && (id->bInterfaceSubClass == 0)) {
			for (j = 0; j < id->bNumEndpoints; j++) {
				debug("interface:%d endpoint:%d bmAttributes:%x Address:%x\n", i, j, 
				id->endpoint[j].bmAttributes, id->endpoint[j].bEndpointAddress);
			    if ((id->endpoint[j].bmAttributes == LIBUSB_TRANSFER_TYPE_BULK) 
			    	&& ((id->endpoint[j].bEndpointAddress & LIBUSB_ENDPOINT_IN) == LIBUSB_ENDPOINT_OUT)) {
			    	bp_interface = i;
			    	bp_ep = id->endpoint[j].bEndpointAddress;
	                libusb_free_config_descriptor(config);			
	                return 0;		
			    }				
			}
		}
	} 
	
	err("Error getting device interface and endpoint.\n");	
	libusb_free_config_descriptor(config);			
	return -1;		
}

static int bp_attach(libusb_device *dev)
{
	int rc;

	if (bp_handle) {
        info("Already attached!\n");   
        return -1;
	}

	if (get_interface_ep(dev)) {
		err("Error getting device descriptor\n");
		return -1;
	}
	
	if (LIBUSB_SUCCESS != (rc = libusb_open (dev, &bp_handle))) {
		err("Error opening device:%s\n", libusb_error_name(rc));
		return -1;
	}

/*    bp_transfer = libusb_alloc_transfer(0); */

    if ((rc = libusb_claim_interface(bp_handle, bp_interface)) != LIBUSB_SUCCESS)
    {
        err("claim interface error:%s\n", libusb_error_name(rc));

	    if (bp_handle) {
		    libusb_close(bp_handle);
		    bp_handle = NULL;
	    }
        return -1;
    }

    xfr_id = NULL;
    bp_count = 0;
    bp_stat = busy;
	return 0;	
}

static void find_device(void) 
{	
	int i;
	libusb_device **devs;
    struct libusb_device_descriptor desc;
    
	if (libusb_get_device_list(NULL, &devs) < 0)
		return;

	for (i = 0; devs[i]; ++i) {
        libusb_get_device_descriptor(devs[i], &desc);

/*        debug("desc.idVendor =%x\n", desc.idVendor); */
	    if ((desc.idVendor == 0x4e58) && (desc.idProduct == 0x1001) && 
	    	(desc.bDeviceClass == 0) && (desc.bDeviceSubClass == 0)) {
		    bp_attach(devs[i]);
		    break;
	    }
	}
	
	libusb_free_device_list(devs, 1);
}
						

static int LIBUSB_CALL hotplug_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
	(void)ctx;
	(void)dev;
	(void)event;
	(void)user_data;

	info("Device attached\n");	
	return(bp_attach(dev));

}

static int bp_deattach(void) 
{
	bp_stat = idle;
	
	if (bp_handle) {

/*        libusb_free_transfer(bp_transfer); */
        libusb_release_interface(bp_handle, bp_interface);

		libusb_close(bp_handle);
		bp_handle = NULL;
	}
	
	return 0;
}

static int LIBUSB_CALL hotplug_callback_detach(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
	(void)ctx;
	(void)dev;
	(void)event;
	(void)user_data;

	info("Device detached\n");

    bp_deattach();

	return 0;
}


/******************** Send stream to STDOUT ***********************************/
static void convFB(void)
{
	int i;
	unsigned int k;
	unsigned short *temp;
	unsigned int *ptemp;

	if (xfr_id == pingbuff)
		stream_buff = pongbuff;
	else
		stream_buff = pingbuff;	
			
	temp = (unsigned short *)stream_buff;
	ptemp = (unsigned int *)ptrFB;
	
	for (i = 0; i < buff_size/2; i++) {
		k = ptemp[i];
        temp[i] = (unsigned short) (((k>>8) & 0xF800) | ((k>>5) & 0x07E0) | ((k>>3) & 0x001F));		
	}	
		
}

void callbackUSBTransferComplete(struct libusb_transfer *xfr)
{
    switch(xfr->status)
    {
        case LIBUSB_TRANSFER_COMPLETED:
            // Success here, data transfered are inside 
            // xfr->buffer
            // and the length is
            // xfr->actual_length
 
            xfr_id = NULL;           
            		
            break;

        case LIBUSB_TRANSFER_NO_DEVICE:
        case LIBUSB_TRANSFER_ERROR:
            err("LIBUSB_TRANSFER_ERROR %d", xfr->status);
            libusb_cancel_transfer(xfr); 
            bp_deattach();
            break;
        case LIBUSB_TRANSFER_CANCELLED:
        case LIBUSB_TRANSFER_TIMED_OUT:
        case LIBUSB_TRANSFER_STALL:
        case LIBUSB_TRANSFER_OVERFLOW:
            // Various type of errors here
            err("LIBUSB_TRANSFER_CANCELLED %d", xfr->status);
            bp_deattach();
            break;
    }
}

/******************** Send stream to BeadaPanel ***********************************/
static int transmitBP(void *arg)
{
    ssize_t len;
	int ret, transferred;
    PANELLINK_STREAM_TAG tag;
                    	
    while (1) {
 
    	if (bp_stat == busy) {                                  

            /* Prepare tag header */
            tag.type = TYPE_START;
            tag.version = 1;
            memcpy(tag.protocol_name, protocol_str, sizeof tag.protocol_name);
            memset(tag.fmtstr, 0, sizeof tag.fmtstr);
		    
            if (stream_src == FB) 
                sprintf(tag.fmtstr, "image/x-raw, format=BGR16, height=%d, width=%d, framerate=0/1", vinfo.yres, vinfo.xres);
            else if (stream_src == VC4) {

                #ifdef BPD_VC4_ENABLE
                if ((display_info.transform==1) || (display_info.transform==3))
                    sprintf(tag.fmtstr, "image/x-raw, format=BGR16, height=%d, width=%d, framerate=0/1", display_info.width, display_info.height);	
                else
                    sprintf(tag.fmtstr, "image/x-raw, format=BGR16, height=%d, width=%d, framerate=0/1", display_info.height, display_info.width);	
                 #endif	
            }             
            tag.checksum16 = checkSum16((unsigned short *)&tag, (sizeof tag - 2) / 2);
 
    		/* Send tag header */
            if ((ret = libusb_bulk_transfer(bp_handle, bp_ep, (unsigned char *)&tag, 
            	sizeof tag, &transferred, 0)) != LIBUSB_SUCCESS) {
                debug("%s", libusb_error_name(ret));
                bp_deattach();
                continue;
            }
    		
 /*   		while (bp_stat == busy) {*/
            {

    			/*  Send stream */
    			if (stream_src == STD_IN) {

    				/* Pend on read() */
	                if ((len = read(data_in, stream_buff, buff_size)) > 0) {

                        /* HexDump(szbuf, ret, szbuf); */
                        if ((ret = libusb_bulk_transfer(bp_handle, bp_ep, (unsigned char *)stream_buff, len, 
                        	&transferred, 0)) != LIBUSB_SUCCESS) {

                            debug("libusb_bulk_transfer 2 %s", libusb_error_name(ret));
                            bp_deattach();
                            break;
                        }
                    }
	                else if (!len) {
		                debug("stdin: EOF");

                        /* Prepare tag header */
		                tag.type = TYPE_END;           
		                tag.checksum16 = checkSum16((unsigned short *)&tag, (sizeof tag - 2) / 2);
                        libusb_bulk_transfer(bp_handle, bp_ep, (unsigned char *)&tag, sizeof tag, &transferred, 0);

		                exit(1);
	                } else {
		                debug("read stdin error, %x", errno);
		                exit(1);
	                } 
	                   
    			}
    			else {

                    /* wait for next snapshot */
    	            sem_wait(&bp_sem);   

                    if (xfr_id) {

                        if ((ret = libusb_bulk_transfer(bp_handle, bp_ep, xfr_id, buff_size, 
            	              &transferred, 0)) != LIBUSB_SUCCESS) {
                           debug("%s", libusb_error_name(ret));
                           bp_deattach();
                           continue;
                        }
                                                                     
                        xfr_id = NULL;
 
                        if (!(++bp_count%20)) {
                    	    debug("frame no. %d\n", bp_count);
                        }
                    }                        	                  				
    			}    				
    		}
 
            /* Prepare tag header */
		    tag.type = TYPE_END;
            
		    tag.checksum16 = checkSum16((unsigned short *)&tag, (sizeof tag - 2) / 2);
 
    		/* Send tag header */
            if ((ret = libusb_bulk_transfer(bp_handle, bp_ep, (unsigned char *)&tag, 
            	sizeof tag, &transferred, 0)) != LIBUSB_SUCCESS) {
                debug("%s", libusb_error_name(ret));
                bp_deattach();
                continue;
            }
    	}

        /* release cpu resource */    		
        usleep(STREAM_DAEMON_INTERVAL);   	
    }
}

/******************** Send stream to BeadaPanel ***********************************/
static int streamBP(void *arg)
{
	int rc;
    pthread_t tidp;
    libusb_context *context;
    
	die_on((rc = libusb_init(&context)) != LIBUSB_SUCCESS, "failed to initialise libusb: %s\n", libusb_error_name(rc));
        
	die_on((rc = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) == 0, 
	    "Hotplug capabilites are not supported on this platform %s\n", libusb_error_name(rc));

	die_on(LIBUSB_SUCCESS != (rc = libusb_hotplug_register_callback (NULL, 
	                      LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, 0, 0x4e58,
		                  0x1001, 0, hotplug_callback, NULL, &bp_cb1)), 
		                  "Error registering callback %s\n", 
		                  libusb_error_name(rc));

	die_on(LIBUSB_SUCCESS != (rc = libusb_hotplug_register_callback (NULL, 
	                      LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0, 0x4e58,
		                  0x1001, 0, hotplug_callback_detach, NULL, &bp_cb2)), 
		                  "Error registering callback %s\n", 
		                  libusb_error_name(rc));		                  
			                  
    find_device();

    die_on(pthread_create(&tidp, NULL, (void*(*)(void*))transmitBP, NULL) < 0, "pthread_create(streamBP)");    	
    	        
	while (1) {

		if ((rc = libusb_handle_events(NULL)) != LIBUSB_SUCCESS)
	        err("libusb_handle_events() failed: %s\n", libusb_error_name(rc));
	}          	
 
}


int main(int argc, char *argv[])
{
    int rc, fp, opt, fps, bs;
    pthread_t tidp;
    char path[FB_PATH_LENGTH] = "/dev/fb0";	    
    char rpipe[FB_PATH_LENGTH] = "/dev/bpread";	    
    char wpipe[FB_PATH_LENGTH] = "/dev/bpwrite";	    

    printf("%s\n", ver_str);
                
    while ((opt = getopt(argc, argv, "i:b:o:hv:f:")) != -1) {
        switch (opt)
        {
            case 'i':
    			stream_src = STD_IN; 
    			if (optarg != NULL) {
    		        strcpy(rpipe, optarg);
    		    }
	            break;
            case 'b':
   		        stream_src = FB;
    			if (optarg != NULL) {
    		        strcpy(path, optarg);
    		    }
	            break;
            case 'o':
    			stream_to = STD_OUT;
    			if (optarg != NULL) {
    		        strcpy(wpipe, optarg);
    		    }
	            break;
            case 'h':
    			printf("%s\n", usage_str);
	            exit(0);
            case 'v':
			    set_verbosity(atoi(optarg));
			    break;
            case 'f':
            	if ((fps = atoi(optarg)) > 0) {
            		if (fps < 25)
			            fps_in_us = 40000 * (25 - fps);
			        else 
			        	fps_in_us = 1000;
			    }
			    break;
	    }
    }
    
    if (stream_src == STD_IN) {
        stream_buff = malloc(PL_BUFF_SIZE);	
        buff_size = PL_BUFF_SIZE;

        die_on((data_in = open(rpipe, O_RDONLY)) < 0, "Error openning %s %d\n", rpipe, data_in);

    }
    else if (stream_src == FB) {
 
	    die_on((fp = open (path, O_RDWR)) < 0, "Can not open framebuffer device\n");
	    die_on(ioctl(fp, FBIOGET_FSCREENINFO, &finfo), "Error reading fixed information\n");
	    die_on(ioctl(fp, FBIOGET_VSCREENINFO, &vinfo), "Error reading variable information\n");	 
 
 	    bs = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
        debug("Total length=%d, xres=%d, yres=%d, bits/pixel=%d\n", bs, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
        
	    /**/
	    die_on((int)(ptrFB = (unsigned char *) mmap (0, bs, 
	               PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0)) == -1, 
	              "failed to map framebuffer device to memory.\n");

 	    buff_size = bs/2;	              
	    die_on((pingbuff = (unsigned char *)malloc(buff_size))==NULL, "malloc() error\n");
	    die_on((pongbuff = (unsigned char *)malloc(buff_size))==NULL, "malloc() error\n");

	    die_on(sem_init(&bp_sem, 0, 0), "Error creating semaphore\n");

    } 
    else {
    	
#ifdef BPD_VC4_ENABLE
        bcm_host_init();

        die_on(!(display = vc_dispmanx_display_open(0)), "Unable to open primary display\n");
 
        die_on(vc_dispmanx_display_get_info(display, &display_info), "Unable to get primary display information\n");
 
        debug("Primary display is %d x %d", display_info.width, display_info.height);
 
        if ((display_info.transform==1) || (display_info.transform==3)) {
            die_on(!(screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, display_info.height, display_info.width, &image_prt)), 
           "Unable to create screen buffer\n");

             vc_dispmanx_rect_set(&rect1, 0, 0, display_info.height, display_info.width);
        }
        else {
              die_on(!(screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, display_info.width, display_info.height, &image_prt)), 
             "Unable to create screen buffer\n");

              vc_dispmanx_rect_set(&rect1, 0, 0, display_info.width, display_info.height);    	
        }

        buff_size = display_info.width*display_info.height*2;  
        stream_buff = malloc(buff_size);
        die_on(sem_init(&bp_sem, 0, 0), "Error creating semaphore\n");       	
#endif

    }


    if (stream_to == BP) {
    	die_on(pthread_create(&tidp, NULL, (void*(*)(void*))streamBP, NULL) < 0, "pthread_create(streamBP)");
    }  
    else {
        die_on((data_out = open(wpipe, O_WRONLY)) == -1, "Error openning %s\n", wpipe);
    }

  	
    while (stream_src != STD_IN) {

        if (stream_src == VC4) {

#ifdef BPD_VC4_ENABLE
           vc_dispmanx_snapshot(display, screen_resource, 0);

            if ((display_info.transform==1) || (display_info.transform==3))
                vc_dispmanx_resource_read_data(screen_resource, &rect1, stream_buff, display_info.height*2);  
            else
                vc_dispmanx_resource_read_data(screen_resource, &rect1, stream_buff, display_info.width*2);  
#endif
        }
        else 
        	convFB();   
 
        if (stream_to == STD_OUT) {

            write(data_out, stream_buff, buff_size);
        }
	    else if (!xfr_id) {

		    xfr_id = stream_buff;
	        sem_post(&bp_sem);
        }  
                  
        /* wait for next snapshot */
        usleep(fps_in_us);   	
    }

    if (stream_to == BP)
        pthread_join(tidp, NULL);
        
	return 0;
}


