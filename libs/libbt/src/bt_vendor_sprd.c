/*
 * Copyright 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/******************************************************************************
 *
 *  Filename:      bt_vendor_sprd.c
 *
 *  Description:   SPRD vendor specific library implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <utils/Log.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include "bt_vendor_sprd.h"
#include "userial_vendor.h"
/******************************************************************************
**  Externs
******************************************************************************/
extern int hw_config(int nState);

extern int is_hw_ready();

/******************************************************************************
**  Variables
******************************************************************************/
int s_bt_fd = -1;

bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
uint8_t vnd_local_bd_addr[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

#if (HW_NEED_END_WITH_HCI_RESET == TRUE)
void hw_epilog_process(void);
#endif


/******************************************************************************
**  Local type definitions
******************************************************************************/


/******************************************************************************
**  Functions
******************************************************************************/
int sprd_config_init(int fd, char *bdaddr, struct termios *ti);
/*****************************************************************************
**
**   BLUETOOTH VENDOR INTERFACE LIBRARY FUNCTIONS
**
*****************************************************************************/

static int init(const bt_vendor_callbacks_t* p_cb, unsigned char *local_bdaddr)
{
    ALOGI("bt-vendor : init");

    if (p_cb == NULL)
    {
        ALOGE("init failed with no user callbacks!");
        return -1;
    }

    //userial_vendor_init();
    //upio_init();

    //vnd_load_conf(VENDOR_LIB_CONF_FILE);

    /* store reference to user callbacks */
    bt_vendor_cbacks = (bt_vendor_callbacks_t *) p_cb;

    /* This is handed over from the stack */
    memcpy(vnd_local_bd_addr, local_bdaddr, 6);

    return 0;
}


/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
    int retval = 0;
    int nCnt = 0;
    int nState = -1;

    ALOGI("bt-vendor : op for %d", opcode);

    switch(opcode)
    {
        case BT_VND_OP_POWER_CTRL:
            {


		 ALOGI("bt-vendor : BT_VND_OP_POWER_CTRL");		
		  #if 0
		  nState = *(int *) param;
                retval = hw_config(nState);
                if(nState == BT_VND_PWR_ON
                   && retval == 0
                   && is_hw_ready() == TRUE)
                {
                    retval = 0;
                }
		  #endif			
            }
            break;

        case BT_VND_OP_FW_CFG:
            {
			ALOGI("bt-vendor : BT_VND_OP_FW_CFG");

			sprd_config_init(s_bt_fd,NULL,NULL);
			ALOGI("bt-vendor : eee");
                // call hciattach to initalize the stack
                if(bt_vendor_cbacks){
                   ALOGI("Bluetooth Firmware and smd is initialized");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
                }
                else{
                   ALOGI("Error : hci, smd initialization Error");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_FAIL);
                }
            }
            break;

        case BT_VND_OP_SCO_CFG:
            {
		ALOGI("bt-vendor : BT_VND_OP_SCO_CFG");						
                bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_USERIAL_OPEN:
            {
		  int idx;
		  ALOGI("bt-vendor : BT_VND_OP_USERIAL_OPEN");
                if(bt_hci_init_transport(&s_bt_fd) != -1)
		  {
			int (*fd_array)[] = (int (*) []) param;
	
			for (idx=0; idx < CH_MAX; idx++)
                   {
                   		(*fd_array)[idx] = s_bt_fd;
			}
			ALOGI("bt-vendor : BT_VND_OP_USERIAL_OPEN ok");
			retval = 1;
		  }
                else 
		 {
		 	ALOGI("bt-vendor : BT_VND_OP_USERIAL_OPEN failed");
                    retval = -1;
                }
            }
            break;

        case BT_VND_OP_USERIAL_CLOSE:
            {
		   ALOGI("bt-vendor : BT_VND_OP_USERIAL_CLOSE");								
                 bt_hci_deinit_transport(s_bt_fd);
            }
            break;

        case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
            break;

        case BT_VND_OP_LPM_SET_MODE:
            {
                ALOGI("bt-vendor : BT_VND_OP_LPM_SET_MODE");				
                bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_LPM_WAKE_SET_STATE:
            ALOGI("bt-vendor : BT_VND_OP_LPM_WAKE_SET_STATE");			
            break;
        case BT_VND_OP_EPILOG:
            {
#if (HW_NEED_END_WITH_HCI_RESET == FALSE)
                if (bt_vendor_cbacks)
                {
                    bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
                }
#else
                hw_epilog_process();
#endif
            }
            break;
    }

    return retval;
}

/** Closes the interface */
static void cleanup( void )
{
    ALOGI("cleanup");

    //upio_cleanup();

    bt_vendor_cbacks = NULL;
}

// Entry point of DLib
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    sizeof(bt_vendor_interface_t),
    init,
    op,
    cleanup
};

// pskey file structure default value
BT_PSKEY_CONFIG_T bt_para_setting={
5,
0,
0,
0,
0,
0x18cba80,
0x001f00,
0x1e,
{0x7a00,0x7600,0x7200,0x5200,0x2300,0x0300},
{0XCe418CFE,
 0Xd0418CFE,0Xd2438CFE,
 0Xd4438CFE,0xD6438CFE},
{0xFF, 0xFF, 0x8D, 0xFE, 0x9B, 0xFF, 0x79, 0x83,
  0xFF, 0xA7, 0xFF, 0x7F, 0x00, 0xE0, 0xF7, 0x3E},
{0x11, 0xFF, 0x0, 0x22, 0x2D, 0xAE},
0,
1,
5,
0x0e,
0xFFFFFFFF,
0x30,
0x3f,
0,
0x65,
0x0,
0x0,
0x0,
0x0,
0x3,
0x1,
0x1,
0x2,
0x4,
0x4,
0x0,
0x12,
0x4,
0x1,
0xd0,
0xc8,
{0x0000,0x0000,0x0000,0x0000}
};


static BOOLEAN checkBluetoothAddress(char* address)
{
    int i=0;
    char add_temp[BT_RAND_MAC_LENGTH+1]={0};
    char c;
    if (address == NULL)
    {
        return FALSE;
    }

    for (i=0; i < BT_RAND_MAC_LENGTH; i++)
    {
        c=add_temp[i]=toupper(address[i]);
        switch (i % 3)
        {
            case 0:
            case 1:
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
                {
                    break;
                }
                return FALSE;
            case 2:
                if (c == ':')
                {
                    break;
                }
                return FALSE;
        }
    }

    if(strstr(add_temp, MAC_ERROR)!=NULL)
        return FALSE;

    return TRUE;
}

int read_mac_from_file(const char * file_path,  char * mac)
{
    int fd_btaddr=0;
    char bt_mac[BT_RAND_MAC_LENGTH+1] = {0};

    fd_btaddr = open(file_path, O_RDWR);
    if(fd_btaddr>=0)
    {
        read(fd_btaddr, bt_mac, BT_RAND_MAC_LENGTH);
        ALOGI("bt mac read ===%s==",bt_mac);
        if(checkBluetoothAddress(bt_mac))
        {
            ALOGI("bt mac already exists, no need to random it");
            memcpy(mac,bt_mac,BT_RAND_MAC_LENGTH);
            return 1;
        }
        close(fd_btaddr);
    }
    ALOGI("bt mac read fail.");

    return 0;
}



//******************create bt addr***********************
static void mac_rand(char *btmac)
{
	int i=0, j=0;
	unsigned int randseed;

	ALOGI("mac_rand");

	//rand seed
	randseed = (unsigned int) time(NULL);
	ALOGI("%s: randseed=%d",__FUNCTION__, randseed);
	srand(randseed);

	//FOR BT
	i=rand(); j=rand();
	ALOGI("%s:  rand i=0x%x, j=0x%x",__FUNCTION__, i,j);
	sprintf(btmac, "00:%02x:%02x:%02x:%02x:%02x", \
							(unsigned char)((i>>8)&0xFF), \
							(unsigned char)((i>>16)&0xFF), \
							(unsigned char)((j)&0xFF), \
							(unsigned char)((j>>8)&0xFF), \
							(unsigned char)((j>>16)&0xFF));
}


static  BOOLEAN write_btmac2file(const char * file_path,char *btmac)
{
    int fd = -1;
    fd = open(file_path, O_CREAT|O_RDWR|O_TRUNC, 0660);
    if(fd > 0) {
        write(fd, btmac, strlen(btmac));
        close(fd);
        return TRUE;
    }
    return FALSE;
}
uint8 ConvertHexToBin(
			uint8        *hex_ptr,     // in: the hexadecimal format string
			uint16       length,       // in: the length of hexadecimal string
			uint8        *bin_ptr      // out: pointer to the binary format string
			){
    uint8        *dest_ptr = bin_ptr;
    uint32        i = 0;
    uint8        ch;

    for(i=0; i<length; i+=2){
		    // the bit 8,7,6,5
				ch = hex_ptr[i];
				// digital 0 - 9
				if (ch >= '0' && ch <= '9')
				    *dest_ptr =(uint8)((ch - '0') << 4);
				// a - f
				else if (ch >= 'a' && ch <= 'f')
				    *dest_ptr = (uint8)((ch - 'a' + 10) << 4);
				// A - F
				else if (ch >= 'A' && ch <= 'F')
				    *dest_ptr = (uint8)((ch -'A' + 10) << 4);
				else{
				    return 0;
				}

				// the bit 1,2,3,4
				ch = hex_ptr[i+1];
				// digtial 0 - 9
				if (ch >= '0' && ch <= '9')
				    *dest_ptr |= (uint8)(ch - '0');
				// a - f
				else if (ch >= 'a' && ch <= 'f')
				    *dest_ptr |= (uint8)(ch - 'a' + 10);
				// A - F
				else if (ch >= 'A' && ch <= 'F')
				    *dest_ptr |= (uint8)(ch -'A' + 10);
				else{
			            return 0;
				}

				dest_ptr++;
	  }

    return 1;
}
//******************create bt addr end***********************

int sprd_config_init(int fd, char *bdaddr, struct termios *ti)
{
    int i,psk_fd,fd_btaddr,ret = 0,r,size=0,read_btmac=0;
    unsigned char resp[30];
    BT_PSKEY_CONFIG_T bt_para_tmp;
    char bt_mac[30] = {0};
    char bt_mac_tmp[20] = {0};
    uint8 bt_mac_bin[32]     = {0};

    ALOGI("init_sprd_config in \n");
    /*
    mac_rand(bt_mac);
    ALOGI("bt random mac=%s",bt_mac);
    printf("bt_mac=%s\n",bt_mac);
    write_btmac2file(bt_mac);

    */
    if(access(BT_MAC_FILE, F_OK) == 0)
    {
        ALOGI("%s: %s exists",__FUNCTION__, BT_MAC_FILE);
        read_btmac=read_mac_from_file(BT_MAC_FILE,bt_mac);
    }

    if(0==read_btmac)
    {
        if(access(BT_MAC_FILE_TEMP, F_OK) == 0)
        {
            ALOGI("%s: %s exists",__FUNCTION__, BT_MAC_FILE_TEMP);
            read_btmac=read_mac_from_file(BT_MAC_FILE_TEMP,bt_mac);
        }
    }

    if(0==read_btmac)
    {
        mac_rand(bt_mac);
        if(write_btmac2file(BT_MAC_FILE_TEMP,bt_mac))
            read_btmac=1;
    }
    
    if(read_btmac == 1)
    {
        for(i=0; i<6; i++)
        {
            bt_mac_tmp[i*2] = bt_mac[3*(5-i)];
            bt_mac_tmp[i*2+1] = bt_mac[3*(5-i)+1];
        }
        ALOGI("====bt_mac_tmp=%s", bt_mac_tmp);
        ConvertHexToBin((uint8*)bt_mac_tmp, strlen(bt_mac_tmp), bt_mac_bin);
    }

    /* Reset the BT Chip */
    memset(resp, 0, sizeof(resp));

    ret = bt_getPskeyFromFile(&bt_para_tmp);
    if(ret != 0)
    {
        ALOGI("get_pskey_from_file fail\n");
        /* Send command from hciattach*/
        if(read_btmac == 1)
        {
            memcpy(bt_para_setting.device_addr, bt_mac_bin, sizeof(bt_para_setting.device_addr));
        }
        if (write(s_bt_fd, (char *)&bt_para_setting, sizeof(BT_PSKEY_CONFIG_T)) != sizeof(BT_PSKEY_CONFIG_T)) 
        {
            ALOGI("Failed to write pskey command from default arry\n");
            return -1;
        }
    }
    else
    {
        ALOGI("get_pskey_from_file ok \n");
        /* Send command from pskey_bt.txt*/
        if(read_btmac == 1)
        {
            memcpy(bt_para_tmp.device_addr, bt_mac_bin, sizeof(bt_para_tmp.device_addr));
        }
        if (write(s_bt_fd, (char *)&bt_para_tmp, sizeof(BT_PSKEY_CONFIG_T)) != sizeof(BT_PSKEY_CONFIG_T)) 
        {
            ALOGI("Failed to write pskey command from pskey file\n");
            return -1;
        }
    }
    ALOGI("sprd_config_init write pskey command ok \n");

    while (1) 
    {
        r = read(s_bt_fd, resp, 1);
        if (r <= 0)
        return -1;
        if (resp[0] == 0x05)
        {
            ALOGI("read pskey response ok \n");
            break;
        }
    }

    ALOGI("sprd_config_init ok \n");

    return 0;
}

