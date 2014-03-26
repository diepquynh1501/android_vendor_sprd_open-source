#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include "engopt.h"
#include "cutils/properties.h"
#include "private/android_filesystem_config.h"
#include "eng_diag.h"
#include "calibration.h"

#define CONFIG_AP_ADC_CALIBRATION
#define ADC_CALIBR_FGU_ON
#ifdef CONFIG_AP_ADC_CALIBRATION

#define PRECISION_1MV       (1<<24)
#define PRECISION_10MV      (0)
#define MAX_VOLTAGE         (0xFFFFFF)

#define ADC_CAL_TYPE_FILE "/sys/class/power_supply/sprdfgu/fgu_cal_from_type"

static	int	vbus_charger_disconnect = 0;
void	disconnect_vbus_charger(void)
{
    int fd;
    if(vbus_charger_disconnect == 0){

        fd = open(CHARGER_STOP_PATH,O_WRONLY);
        if(fd >= 0){
            write(fd,"1",2);
            close(fd);
            sleep(1);
            vbus_charger_disconnect = 1;
        }
    }
}
void	initialize_ctrl_file(void)
{
    CALI_INFO_DATA_T	cali_info;
    int ret;

    int fd = open(CALI_CTRL_FILE_PATH,O_RDWR);

    if(fd < 0){
        fd = open(CALI_CTRL_FILE_PATH,O_RDWR|O_CREAT, 0666);
    }
    if(fd < 0){
        ALOGE("%s open %s failed\n",__func__,CALI_CTRL_FILE_PATH);
        return;
    }
    ret = read(fd,&cali_info,sizeof(cali_info));
    if(ret < 0){
        ALOGE(" %s read failed...\n",__func__);
        close(fd);
        return;
    }
    if(cali_info.magic!=CALI_MAGIC){
        memset(&cali_info,0xff,sizeof(cali_info));
        cali_info.magic = CALI_MAGIC;
    }

#ifdef ADC_CALIBR_FGU_ON
    cali_info.adc_para.reserved[6] = 2;
#else
    cali_info.adc_para.reserved[6] = 0;
#endif
    lseek(fd,SEEK_SET,0);
    write(fd,&cali_info,sizeof(cali_info));
    close(fd);
}

void	disable_calibration(void)
{
    CALI_INFO_DATA_T        cali_info;

    int fd = open(CALI_CTRL_FILE_PATH,O_RDWR);

    if(fd < 0){
        ALOGE("%s open %s failed\n",__func__,CALI_CTRL_FILE_PATH);
        return;
    }
    read(fd,&cali_info,sizeof(cali_info));
    cali_info.magic = CALI_MAGIC;
    cali_info.cali_flag = CALI_COMP;

    lseek(fd,SEEK_SET,0);
    write(fd,&cali_info,sizeof(cali_info));
    close(fd);
}

void	enable_calibration(void)
{
    CALI_INFO_DATA_T        cali_info;

    int fd = open(CALI_CTRL_FILE_PATH,O_RDWR);

    if(fd < 0){
        ALOGE("%s open %s failed\n",__func__,CALI_CTRL_FILE_PATH);
        return;
    }

    read(fd,&cali_info,sizeof(cali_info));
    cali_info.magic = 0xFFFFFFFF;
    cali_info.cali_flag = 0xFFFFFFFF;

    lseek(fd,SEEK_SET,0);
    write(fd,&cali_info,sizeof(cali_info));
    close(fd);
}

void adc_get_result(char* chan)
{
    int fd = open(ADC_CHAN_FILE_PATH,O_RDWR);
    if(fd < 0){
        ALOGE("%s open %s failed\n",__func__,ADC_CHAN_FILE_PATH);
        return 0;
    }
    write(fd, chan, strlen(chan));
    lseek(fd,SEEK_SET,0);
    memset(chan, 0, 8);
    read(fd, chan , 8);
    close(fd);
}
static int AccessADCDataFile(unsigned char flag, char *lpBuff, int size)
{
    int fd = -1;
    int ret = 0;
    CALI_INFO_DATA_T        cali_info;

    fd = open(BATTER_CALI_CONFIG_FILE,O_RDWR);
    if(flag == 1){
        if(fd < 0){
            fd = open(BATTER_CALI_CONFIG_FILE,O_CREAT|O_WRONLY, 0666);
            if(fd < 0)
                return 0;
        }
        ret = read(fd,&cali_info,sizeof(cali_info));

        if(size < sizeof(cali_info.adc_para))
            memcpy(&cali_info.adc_para,lpBuff,size);
        else
            memcpy(&cali_info.adc_para,lpBuff,sizeof(cali_info.adc_para));
        lseek(fd,SEEK_SET,0);
        cali_info.magic = CALI_MAGIC;
        ret = write(fd,&cali_info,sizeof(cali_info));
        fsync(fd);
        sleep(1);
    } else {
        if(fd < 0)
            return 0;
        ret = read(fd,&cali_info,sizeof(cali_info));

        if(size < sizeof(cali_info.adc_para))
            memcpy(lpBuff,&cali_info.adc_para,size);
        else
            memcpy(lpBuff,&cali_info.adc_para,sizeof(cali_info.adc_para));
    }
    close(fd);
    sync();

    return ret;
}
static int get_battery_voltage(void)
{
    int fd = -1;
    int read_len = 0;
    char buffer[16]={0};
    char *endptr;
    int value =0;

    fd = open(BATTERY_VOL_PATH,O_RDONLY);

    if(fd >= 0){
        read_len = read(fd,buffer,sizeof(buffer));
        if(read_len > 0)
            value = strtol(buffer,&endptr,0);
        close(fd);
    }
    return value;
}
static int get_battery_adc_value(void)
{
    int fd = -1;
    int read_len = 0;
    char buffer[16]={0};
    char *endptr;
    int  value = 0;

    fd = open(BATTERY_ADC_PATH,O_RDONLY);

    if(fd >= 0){
        read_len = read(fd,buffer,sizeof(buffer));
        if(read_len > 0)
            value = strtol(buffer,&endptr,0);
        close(fd);
    }
    return value;
}
static int get_fgu_current_adc(int *value)
{
    int fd = -1;
    int read_len = 0;
    char buffer[16]={0};
    char *endptr;

    fd = open(FGU_CURRENT_ADC_FILE_PATH,O_RDONLY);

    if(fd >= 0){
        read_len = read(fd,buffer,sizeof(buffer));
        if(read_len > 0)
            *value = strtol(buffer,&endptr,0);
        close(fd);
        ALOGE("%s %s value = %d\n",__func__,FGU_VOL_ADC_FILE_PATH, *value);
    }
    else{
        ALOGE("%s open %s failed\n",__func__,FGU_CURRENT_ADC_FILE_PATH);
    }
    return read_len;
}
static int get_fgu_vol_adc(int *value)
{
    int fd = -1;
    int read_len = 0;
    char buffer[16]={0};
    char *endptr;

    fd = open(FGU_VOL_ADC_FILE_PATH,O_RDONLY);

    if(fd >= 0){
        read_len = read(fd,buffer,sizeof(buffer));
        if(read_len > 0)
            *value = strtol(buffer,&endptr,0);
        close(fd);
        ALOGE("%s %s value = %d, read_len = %d \n",__func__,FGU_VOL_ADC_FILE_PATH, *value, read_len);
    }
    else{
        ALOGE("%s open %s failed\n",__func__,FGU_VOL_ADC_FILE_PATH);
    }
    return read_len;
}

static void ap_get_fgu_current_adc(MSG_AP_ADC_CNF *pMsgADC)
{
    int	current_adc = 0;
    int      read_len = 0;

    read_len = get_fgu_current_adc(&current_adc);
    if(read_len>0){
        pMsgADC->ap_adc_req.parameters[0] = current_adc;
        pMsgADC->diag_ap_cnf.status = 0;
    }
    else{
        pMsgADC->diag_ap_cnf.status = 1;
    }
}

static void ap_get_fgu_vol_adc(MSG_AP_ADC_CNF *pMsgADC)
{
    int	vol_adc = 0;
    int      read_len = 0;
    read_len = get_fgu_vol_adc(&vol_adc);

    if(read_len>0){
        pMsgADC->ap_adc_req.parameters[0] = vol_adc;
        pMsgADC->diag_ap_cnf.status = 0;
    }
    else{
        pMsgADC->diag_ap_cnf.status = 1;
    }
}

/*copy from packet.c and modify*/
static int untranslate_packet_header(char *dest,char *src,int size, int unpackSize)
{
    int i;
    int translated_size = 0;
    int status = 0;
    int flag = 0;
    for(i=0;i<size;i++){
        switch(status){
            case 0:
                if(src[i] == 0x7e)
                    status = 1;
                break;
            case 1:
                if(src[i] != 0x7e){
                    status = 2;
                    dest[translated_size++] = src[i];
                }
                break;
            case 2:
                if(src[i] == 0x7E){
                    //unsigned short crc;
                    //crc = crc_16_l_calc((char const *)dest,translated_size-2);
                    return translated_size;
                } else {
                    if((dest[translated_size-1] == 0x7D)&&(!flag)){
                        flag = 1;
                        if(src[i] == 0x5E)
                            dest[translated_size-1] = 0x7E;
                        else if(src[i] == 0x5D)
                            dest[translated_size-1] = 0x7D;
                    } else {
                        flag = 0;
                        dest[translated_size++] = src[i];
                    }

                    if (translated_size >= unpackSize+1 && unpackSize != -1)
                        return translated_size;
                }
                break;
        }
    }

    return translated_size;
}

int translate_packet(char *dest,char *src,int size)
{
    int i;
    int translated_size = 0;

    dest[translated_size++] = 0x7E;

    for(i=0;i<size;i++){
        if(src[i] == 0x7E){
            dest[translated_size++] = 0x7D;
            dest[translated_size++] = 0x5E;
        } else if(src[i] == 0x7D) {
            dest[translated_size++] = 0x7D;
            dest[translated_size++] = 0x5D;
        } else
            dest[translated_size++] = src[i];
    }
    dest[translated_size++] = 0x7E;
    return translated_size;
}

static int is_adc_calibration(char *dest, int destSize, char *src,int srcSize)
{
    int translated_size = 0;

    memset(dest, 0, destSize);
    if(srcSize > destSize)
        memcpy(dest, (src+1), destSize);
    else
        memcpy(dest, (src+1), (srcSize-1));
    MSG_HEAD_T* lpHeader = (MSG_HEAD_T *)dest;
    if (DIAG_CMD_APCALI  == lpHeader->type){
        TOOLS_DIAG_AP_CMD_T *lpAPCmd =(TOOLS_DIAG_AP_CMD_T *)(lpHeader+1);
        switch (lpAPCmd->cmd)
        {
            case DIAG_AP_CMD_ADC:
                {
                    TOOLS_AP_ADC_REQ_T *lpAPADCReq =(TOOLS_AP_ADC_REQ_T *)(lpAPCmd+1);
                    if (lpAPADCReq->operate == 0)
                        return AP_ADC_CALIB;
                    else if (lpAPADCReq->operate == 1)
                        return AP_ADC_LOAD;
                    else if (lpAPADCReq->operate == 2)
                        return AP_ADC_SAVE;
                    else if (lpAPADCReq->operate == 3)
                        return AP_GET_FGU_VOL_ADC;
                    else if (lpAPADCReq->operate == 4)
                        return AP_GET_FGU_CURRENT_ADC;
                    else if (lpAPADCReq->operate == 5)
                        return AP_GET_FGU_TYPE;
                    else
                        return 0;
                }
                break;
            case DIAG_AP_CMD_LOOP:
                return AP_DIAG_LOOP;
                break;

            default:
                break;
        }
    } else if(DIAG_CMD_GETVOLTAGE  == lpHeader->type){
        return AP_GET_VOLT;
    }
    return 0;
}

static int ap_adc_calibration( MSG_AP_ADC_CNF *pMsgADC)
{
    int adc_value = 0;
    int  adc_result = 0;
    int i = 0;

    for(i=0; i < 16; i++){
        adc_value = get_battery_adc_value();
        adc_result += adc_value;
    }
    adc_result >>= 4;
    pMsgADC->diag_ap_cnf.status  = 0;
    pMsgADC->ap_adc_req. parameters[0]= (unsigned short)(adc_result&0xFFFF);

    return adc_result;
}

static int ap_adc_save(TOOLS_AP_ADC_REQ_T *pADCReq, MSG_AP_ADC_CNF *pMsgADC)
{
    AP_ADC_T adcValue;
    int ret = 0;

    memset(&adcValue,0,sizeof(adcValue));
    ret = AccessADCDataFile(1, (char *)pADCReq->parameters, sizeof(pADCReq->parameters));
    if (ret > 0)
        pMsgADC->diag_ap_cnf.status = 0;
    else
        pMsgADC->diag_ap_cnf.status = 1;

    return ret;
}
static int ap_adc_load(MSG_AP_ADC_CNF *pMsgADC)
{
    int ret = AccessADCDataFile(0, (char *)pMsgADC->ap_adc_req.parameters, sizeof(pMsgADC->ap_adc_req.parameters));
    if (ret > 0)
        pMsgADC->diag_ap_cnf.status = 0;
    else
        pMsgADC->diag_ap_cnf.status = 1;

    return ret;
}
static unsigned int ap_get_voltage(MSG_AP_ADC_CNF *pMsgADC)
{
    int	voltage = 0;
    int  	*para=NULL;
    int 	i = 0;
    MSG_HEAD_T *Msg = (MSG_HEAD_T *)pMsgADC;

    for(; i < 16; i++)
        voltage += get_battery_voltage();
    voltage >>= 4;

    para = (int *)(Msg +1);
    //        *para = (voltage/10);
    if (voltage > MAX_VOLTAGE)
    {
        *para = (PRECISION_10MV | ((voltage/10) & MAX_VOLTAGE));
    }
    else
    {
        *para = (PRECISION_1MV | (voltage & MAX_VOLTAGE));
    }
    pMsgADC->msg_head.len = 12;

    return voltage;
}

static void ap_get_fgu_type(MSG_AP_ADC_CNF *pMsgADC)
{
    int fd = -1;
    int r_cnt = 0;
    char fgu_type[2] = {0};
    MSG_HEAD_T *Msg = (MSG_HEAD_T*)pMsgADC;

    fd = open(ADC_CAL_TYPE_FILE, O_RDONLY);
    if(fd >= 0){
        r_cnt = read(fd, fgu_type, 2);
        if(r_cnt > 0){
            ENG_LOG("%s: fgu_type:%s\n", __FUNCTION__, fgu_type);
            pMsgADC->ap_adc_req.parameters[0] = atoi(fgu_type);
            pMsgADC->diag_ap_cnf.status = 0;
        }
        else
        {
            ENG_LOG("%s: read fgu cali file failed,read:%d\n", __FUNCTION__, r_cnt);
            pMsgADC->diag_ap_cnf.status = 1;
        }
    }
    else
    {
        ENG_LOG("%s: open fgu cali file failed, err: %s\n", __FUNCTION__, strerror(errno));
        pMsgADC->diag_ap_cnf.status = 1;
    }
}

int  ap_adc_process(int adc_cmd, char * src, int size, MSG_AP_ADC_CNF * pMsgADC)
{
    MSG_HEAD_T *lpHeader = (MSG_HEAD_T *)src;
    TOOLS_DIAG_AP_CMD_T *lpAPCmd =(TOOLS_DIAG_AP_CMD_T *)(lpHeader+1);
    TOOLS_AP_ADC_REQ_T *lpApADCReq = (TOOLS_AP_ADC_REQ_T *)(lpAPCmd+1);
    memcpy(&(pMsgADC->msg_head), lpHeader, sizeof(MSG_HEAD_T));
    pMsgADC->msg_head.len = sizeof(TOOLS_DIAG_AP_CNF_T)+sizeof(TOOLS_AP_ADC_REQ_T)+sizeof(MSG_HEAD_T);
    pMsgADC->diag_ap_cnf.length = sizeof(TOOLS_AP_ADC_REQ_T);
    memcpy(&(pMsgADC->ap_adc_req), lpApADCReq, sizeof(TOOLS_AP_ADC_REQ_T));

    switch (adc_cmd)
    {
        case AP_ADC_CALIB:
            ap_adc_calibration(pMsgADC);
            break;
        case AP_ADC_LOAD:
            ap_adc_load(pMsgADC);
            break;
        case AP_ADC_SAVE:
            ap_adc_save(lpApADCReq, pMsgADC);
            break;
        case AP_GET_VOLT:
            ap_get_voltage(pMsgADC);
            break;
        case AP_GET_FGU_VOL_ADC:
            ap_get_fgu_vol_adc(pMsgADC);
            break;
        case AP_GET_FGU_CURRENT_ADC:
            ap_get_fgu_current_adc(pMsgADC);
            break;
        case AP_GET_FGU_TYPE:
            ap_get_fgu_type(pMsgADC);
            break;
        default:
            return 0;
    }

    return 1;
}
#endif
int 	write_adc_calibration_data(char *data, int size)
{
    int ret = 0;
#ifdef CONFIG_AP_ADC_CALIBRATION
    ret = AccessADCDataFile(1, data, size);
#endif
    return ret;
}
int 	read_adc_calibration_data(char *buffer,int size)
{
#ifdef CONFIG_AP_ADC_CALIBRATION
    int ret;
    if(size > 48)
        size = 48;
    ret = AccessADCDataFile(0, buffer, size);
    if(ret > 0)
        return size;
#endif
    return 0;
}
int eng_battery_calibration(char *data,int count,char *out_msg,int out_len)
{
#ifdef CONFIG_AP_ADC_CALIBRATION
    int adc_cmd = 0;
    int ret = 0;
    MSG_AP_ADC_CNF adcMsg;

    if(count > 0){
        adc_cmd = is_adc_calibration(out_msg, out_len, data, count );

        if (adc_cmd != 0){
            disconnect_vbus_charger();
            switch(adc_cmd)
            {
                case AP_DIAG_LOOP:
                    if(out_len > count){
                        ret = count;
                    } else {
                        ret = out_len;
                    }
                    memcpy(out_msg,data,out_len);
                    break;
                default:
                    memset(&adcMsg,0,sizeof(adcMsg));
                    ap_adc_process(adc_cmd, out_msg, count, &adcMsg);
                    ret = translate_packet(out_msg, (char *)&adcMsg, adcMsg.msg_head.len);
                    break;
            }
        }
    }
#endif
    return ret;
}

