#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
#include "ion_sprd.h"
using namespace android;

#include "avc_dec_api.h"
#include "util.h"

#define H264_DECODER_INTERNAL_BUFFER_SIZE (0x100000)
#define ONEFRAME_BITSTREAM_BFR_SIZE	(1024*1024*2)  //for bitstream size of one encoded frame.
#define DEC_YUV_BUFFER_NUM  17

static void usage() {
    INFO("usage:\n");
    INFO("utest_vsp_dec -i filename_bitstream -o filename_yuv [OPTIONS]\n");
    INFO("-i       string: input bitstream filename\n");
    INFO("[OPTIONS]:\n");
    INFO("-o       string: output yuv filename\n");
    INFO("-format integer: video format(0:ITU_H263 / 1:MPEG4 / 2:MJPG / 3:FLVH263 / 4:H264 / 5:VP8 / 6: VP9), auto detection if default\n");
    INFO("-frames integer: number of frames to decode, default is 0 means all frames\n");
    INFO("-help          : show this help message\n");
    INFO("Built on %s %s, Written by XiaoweiLuo(xiaowei.luo@spreadtrum.com)\n", __DATE__, __TIME__);
}

void* mLibHandle;
FT_H264DecGetNALType mH264DecGetNALType;
FT_H264DecGetInfo mH264DecGetInfo;
FT_H264DecInit mH264DecInit;
FT_H264DecDecode mH264DecDecode;
FT_H264DecRelease mH264DecRelease;
FT_H264Dec_SetCurRecPic  mH264Dec_SetCurRecPic;
FT_H264Dec_GetLastDspFrm  mH264Dec_GetLastDspFrm;
FT_H264Dec_ReleaseRefBuffers  mH264Dec_ReleaseRefBuffers;
FT_H264DecMemInit mH264DecMemInit;

static int dec_init(AVCHandle *mHandle, int32 format, uint8 *pheader_buffer, uint32 header_size,
                    uint8 *pbuf_inter, uint32 pbuf_inter_phy, uint32 size_inter) {
    MMCodecBuffer InterMemBfr;
    MMDecVideoFormat video_format;
    int32 ret;

    INFO("dec_init IN\n");

    InterMemBfr.common_buffer_ptr = pbuf_inter;
    InterMemBfr.common_buffer_ptr_phy = (uint32)pbuf_inter_phy;
    InterMemBfr.size = size_inter;

    video_format.video_std = format;
    video_format.i_extra = header_size;
    video_format.p_extra = pheader_buffer;
    video_format.frame_width = 0;
    video_format.frame_height = 0;
    video_format.yuv_format = YUV420SP_NV12;
    ret = (*mH264DecInit)(mHandle, &InterMemBfr, &video_format);

    INFO("dec_init OUT\n");

    return ret;
}

static int32 dec_decode_frame(AVCHandle *mHandle, uint8 *pframe, uint32 pframe_y, uint32 size, MMDecOutput *dec_out) {
    MMDecInput dec_in;

    dec_in.pStream  = pframe;
    dec_in.pStream_phy= (uint32)pframe_y;
    dec_in.dataLen = size;
    dec_in.beLastFrm = 0;
    dec_in.expected_IVOP = 0;
    dec_in.beDisplayed = 1;
    dec_in.err_pkt_num = 0;

    dec_out->frameEffective = 0;

    MMDecRet ret = (*mH264DecDecode)(mHandle, &dec_in, dec_out);
    if (ret != MMDEC_OK) {
        ERR ("mH264DecDecode error.\n ");
        return -1;
    }

    return 0;
}

static int32 dec_release(AVCHandle *mHandle) {
    (*mH264Dec_ReleaseRefBuffers)(mHandle);
    if ((*mH264DecRelease)(mHandle) != MMDEC_OK) {
        return -1;
    }
    return 0;
}

static const char* type2str(int type) {
    if (type == 2)    {
        return "I";
    } else if (type == 0) {
        return "P";
    } else if (type == 1) {
        return "B";
    } else {
        return "N";
    }
}

sp<MemoryHeapIon> s_pmem_extra;
uint8 *s_pbuf_extra_v;
uint32 s_pbuf_extra_p;
uint32 s_pbuf_extra_size;

static bool mIOMMUEnabled = false;

static int32 VSP_malloc_cb(void* mHandle, uint32 size_extra) {

    MMCodecBuffer extra_mem[MAX_MEM_TYPE];

    if (mIOMMUEnabled) {
        s_pmem_extra = new MemoryHeapIon("/dev/ion", size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        s_pmem_extra = new MemoryHeapIon("/dev/ion", size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    int32 fd = s_pmem_extra->getHeapID();
    if(fd >= 0) {
        int32 ret,phy_addr, buffer_size;
        if (mIOMMUEnabled) {
            ret = s_pmem_extra->get_mm_iova(&phy_addr, &buffer_size);
        } else {
            ret = s_pmem_extra->get_phy_addr_from_ion(&phy_addr, &buffer_size);
        }
        if((ret < 0) || (buffer_size < (int32)size_extra)) {
            ERR ("s_pmem_extra: get phy addr fail, ret: %d, buffer_size: %d, size_extra: %d\n", ret, buffer_size, size_extra);
            return -1;
        }

        s_pbuf_extra_p =(uint32)phy_addr;
        s_pbuf_extra_v = (uint8 *)s_pmem_extra->base();
        s_pbuf_extra_size = buffer_size;
        extra_mem[HW_NO_CACHABLE].common_buffer_ptr =(uint8 *) s_pbuf_extra_v;
        extra_mem[HW_NO_CACHABLE].common_buffer_ptr_phy = (uint32)s_pbuf_extra_p;
        extra_mem[HW_NO_CACHABLE].size = size_extra;
    } else {
        return -1;
    }

    (*mH264DecMemInit)((AVCHandle *)mHandle, extra_mem);
    return 0;
}

static int VSP_bind_unbind_NULL (void *userdata,void *pHeader) {
    return 0;
}

static int ActivateSPS(void* aUserData, uint width,uint height, uint aNumBuffers) {
    return 1;
}

int32 openDecoder(const char* libName) {
    if(mLibHandle) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    INFO("openDecoder, lib: %s\n",libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ERR("openDecoder, can't open lib: %s",libName);
        return -1;
    }

    mH264DecGetNALType = (FT_H264DecGetNALType)dlsym(mLibHandle, "H264DecGetNALType");
    if(mH264DecGetNALType == NULL) {
        ERR("Can't find H264DecGetNALType in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264DecGetInfo = (FT_H264DecGetInfo)dlsym(mLibHandle, "H264DecGetInfo");
    if(mH264DecGetInfo == NULL) {
        ERR("Can't find H264DecGetInfo in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264DecInit = (FT_H264DecInit)dlsym(mLibHandle, "H264DecInit");
    if(mH264DecInit == NULL) {
        ERR("Can't find H264DecInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264DecDecode = (FT_H264DecDecode)dlsym(mLibHandle, "H264DecDecode");
    if(mH264DecDecode == NULL) {
        ERR("Can't find H264DecDecode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }
    mH264DecRelease = (FT_H264DecRelease)dlsym(mLibHandle, "H264DecRelease");
    if(mH264DecRelease == NULL) {
        ERR("Can't find H264DecRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264Dec_SetCurRecPic = (FT_H264Dec_SetCurRecPic)dlsym(mLibHandle, "H264Dec_SetCurRecPic");
    if(mH264Dec_SetCurRecPic == NULL) {
        ERR("Can't find H264Dec_SetCurRecPic in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264Dec_GetLastDspFrm = (FT_H264Dec_GetLastDspFrm)dlsym(mLibHandle, "H264Dec_GetLastDspFrm");
    if(mH264Dec_GetLastDspFrm == NULL) {
        ERR("Can't find H264Dec_GetLastDspFrm in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264Dec_ReleaseRefBuffers = (FT_H264Dec_ReleaseRefBuffers)dlsym(mLibHandle, "H264Dec_ReleaseRefBuffers");
    if(mH264Dec_ReleaseRefBuffers == NULL) {
        ERR("Can't find H264Dec_ReleaseRefBuffers in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    mH264DecMemInit = (FT_H264DecMemInit)dlsym(mLibHandle, "H264DecMemInit");
    if(mH264DecMemInit == NULL) {
        ERR("Can't find H264DecMemInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    char *filename_bs = NULL;
    FILE *fp_bs = NULL;
    char *filename_yuv = NULL;
    FILE *fp_yuv = NULL;
    int32 format = H264;
    uint32 frames = 0;
    uint32 width = 1280;
    uint32 height = 720;

    uint32 startcode = 0;
    uint32 maskcode = 0;
    int32 i;

    AVCHandle *mHandle = NULL;

    // bitstream buffer, read from bs file
    uint8 buffer_data[ONEFRAME_BITSTREAM_BFR_SIZE];
    int32 buffer_size = 0;

    // yuv420sp buffer, decode from bs buffer
    sp<MemoryHeapIon> pmem_yuv420sp[DEC_YUV_BUFFER_NUM] = {NULL};
    int32 size_yuv[DEC_YUV_BUFFER_NUM]  = {0};
    uint8 *pyuv[DEC_YUV_BUFFER_NUM] = {NULL};
    uint32 pyuv_phy[DEC_YUV_BUFFER_NUM] = {0};

    // yuv420p buffer, transform from yuv420sp and write to yuv file
    uint8 *py = NULL;
    uint8 *pu = NULL;
    uint8 *pv = NULL;

    uint32 framenum_bs = 0;
    uint32 framenum_err = 0;
    uint32 framenum_yuv = 0;
    uint32 time_total_ms = 0;

    // VSP buffer
    uint8* pbuf_inter = NULL;
    int32 size_inter = 0;

    sp<MemoryHeapIon> pmem_stream = NULL;
    uint8 *pbuf_stream = NULL;
    uint32 pbuf_stream_phy = 0;
    int32 size_stream = 0;

    int32 phy_addr = 0;
    int32 size = 0;

    /* parse argument */
    if (argc < 3) {
        usage();
        return -1;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && (i < argc-1)) {
            filename_bs = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && (i < argc-1)) {
            filename_yuv = argv[++i];
        } else if (strcmp(argv[i], "-format") == 0 && (i < argc-1)) {
            format = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-frames") == 0 && (i < argc-1)) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-help") == 0) {
            usage();
            return 0;
        } else {
            usage();
            return -1;
        }
    }

    /* check argument */
    if (filename_bs == NULL) {
        usage();
        return -1;
    }

    fp_bs = fopen(filename_bs, "rb");
    if (fp_bs == NULL) {
        ERR("Failed to open file %s\n", filename_bs);
        goto err;
    }
    if (filename_yuv != NULL) {
        fp_yuv = fopen(filename_yuv, "wb");
        if (fp_yuv == NULL) {
            ERR("Failed to open file %s\n", filename_yuv);
            goto err;
        }
    }

    if (format != H264) {
        ERR("error format %s\n", format2str(format));
        goto err;
    }

    /*MMU Enable or not enable.  shark:not enable;dophin:enable */
    mIOMMUEnabled = MemoryHeapIon::Mm_iommu_is_enabled();
    INFO("IOMMU enabled: %d\n", mIOMMUEnabled);

    /* bs buffer */
    if (mIOMMUEnabled) {
        pmem_stream = new MemoryHeapIon("/dev/ion", ONEFRAME_BITSTREAM_BFR_SIZE, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        pmem_stream = new MemoryHeapIon("/dev/ion", ONEFRAME_BITSTREAM_BFR_SIZE, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    if (pmem_stream->getHeapID() < 0) {
        ERR("Failed to alloc bitstream pmem buffer\n");
        goto err;
    }
    if (mIOMMUEnabled) {
        pmem_stream->get_mm_iova(&phy_addr, &size_stream);
    } else {
        pmem_stream->get_phy_addr_from_ion(&phy_addr, &size_stream);
    }
    pbuf_stream = (uint8 *)pmem_stream->base();
    pbuf_stream_phy = (uint32)phy_addr;
    if (pbuf_stream == NULL) {
        ERR("Failed to alloc bitstream pmem buffer\n");
        goto err;
    }

    /* yuv420sp buffer */
    for (i=0; i<DEC_YUV_BUFFER_NUM; i++) {
        if (mIOMMUEnabled) {
            pmem_yuv420sp[i] = new MemoryHeapIon("/dev/ion", width*height*3/2, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
        } else {
            pmem_yuv420sp[i] = new MemoryHeapIon("/dev/ion", width*height*3/2 * DEC_YUV_BUFFER_NUM, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
        }
        if (pmem_yuv420sp[i]->getHeapID() < 0) {
            ERR("Failed to alloc yuv pmem buffer\n");
            goto err;
        }

        if (mIOMMUEnabled) {
            pmem_yuv420sp[i]->get_mm_iova(&phy_addr, &(size_yuv[i]));
        } else {
            pmem_yuv420sp[i]->get_phy_addr_from_ion(&phy_addr, &(size_yuv[i]));
        }
        pyuv[i] = ((uint8 *)pmem_yuv420sp[i]->base()) ;
        pyuv_phy[i] = ((uint32)phy_addr) ;

        if (pyuv[i] == NULL) {
            ERR("Failed to alloc yuv pmem buffer\n");
            goto err;
        }
    }

    /* yuv420p buffer */
    py = (uint8 *)vsp_malloc(width * height * sizeof(uint8), 4);
    if (py == NULL) {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    pu = (uint8 *)vsp_malloc(width/2 * height/2 * sizeof(uint8), 4);
    if (pu == NULL) {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }
    pv = (uint8 *)vsp_malloc(width/2 * height/2 * sizeof(uint8), 4);
    if (pv == NULL) {
        ERR("Failed to alloc yuv buffer\n");
        goto err;
    }

    INFO("Try to decode %s to %s, format = %s\n", filename_bs, filename_yuv, format2str(format));

    mHandle = (AVCHandle *)vsp_malloc(sizeof(AVCHandle), 4);
    if (mHandle == NULL) {
        ERR("Failed to alloc mHandle\n");
        goto err;
    }
    memset(mHandle, 0, sizeof(AVCHandle));

    mHandle->userdata = (void *)mHandle;
    mHandle->VSP_extMemCb = VSP_malloc_cb;
    mHandle->VSP_bindCb = NULL;
    mHandle->VSP_unbindCb = NULL;

    if (openDecoder("libomx_avcdec_hw_sprd.so") < -1) {
        ERR("Failed to open library.\n");
        goto err;
    }

    /* step 1 - init vsp */
    size_inter = H264_DECODER_INTERNAL_BUFFER_SIZE;
    pbuf_inter = (uint8 *)vsp_malloc(size_inter, 4);
    if (dec_init(mHandle, format, NULL, 0, pbuf_inter, 0, size_inter) != 0) {
        ERR("Failed to init VSP\n");
        goto err;
    }

    /* step 2 - decode with vsp */
    startcode = table_startcode2[format];
    maskcode = table_maskcode2[format];
    while (!feof(fp_bs)) {
        int read_size = fread(buffer_data+buffer_size, 1, ONEFRAME_BITSTREAM_BFR_SIZE-buffer_size, fp_bs);
        int iCount = 0;
        if (read_size <= 0) {
            break;
        }
        buffer_size += read_size;

        uint8 *ptmp = buffer_data;
        uint32 frame_size = 0;
        while (buffer_size > 0) {
            // search a frame
            frame_size = find_frame(ptmp, buffer_size, startcode, maskcode);
            if (frame_size == 0) {
                if ((ptmp == buffer_data) || feof(fp_bs)) {
                    frame_size = buffer_size;
                } else {
                    break;
                }
            }

            // read a bitstream frame
            memcpy(pbuf_stream, ptmp, frame_size);
            ptmp += frame_size;
            buffer_size -= frame_size;

            // decode bitstream to yuv420sp
            uint8 *pyuv420sp = pyuv[framenum_bs % DEC_YUV_BUFFER_NUM];
            uint32 pyuv420sp_phy = pyuv_phy[framenum_bs % DEC_YUV_BUFFER_NUM];
            (*mH264Dec_SetCurRecPic)(mHandle, pyuv420sp, (uint8 *)pyuv420sp_phy, NULL, framenum_yuv);
            framenum_bs++;
            MMDecOutput dec_out;
            dec_out.frameEffective = 0;
            int64_t start = systemTime();
            if (dec_decode_frame(mHandle, pbuf_stream, pbuf_stream_phy, frame_size, &dec_out) < 0) {
                ERR("failed to decode one frame");
                goto err;
            }
            int64_t end = systemTime();
            uint32 duration = (uint32)((end-start) / 1000000L);
            time_total_ms += duration;

            if (duration < 40) {
                usleep((40 - duration)*1000);
            }

            INFO("frame %d[%dx%d]: time = %dms, size = %d, effective(%d)\n",
                 framenum_bs, dec_out.frame_width, dec_out.frame_height, duration, frame_size, dec_out.frameEffective);

            if ((dec_out.frameEffective) && (fp_yuv != NULL)) {
                // yuv420sp to yuv420p
                yuv420sp_to_yuv420p(dec_out.pOutFrameY, dec_out.pOutFrameU, py, pu, pv, dec_out.frame_width, dec_out.frame_height);

                // write yuv420p
                if (write_yuv_frame(py, pu, pv, dec_out.frame_width, dec_out.frame_height, fp_yuv)!= 0)	{
                    break;
                }

                framenum_yuv ++;
            }

            if (frames != 0) {
                if (framenum_yuv >= frames) {
                    goto early_terminate;
                }
            }
        }

        if (buffer_size > 0) {
            memmove(buffer_data, ptmp, buffer_size);
        }
    }

early_terminate:
    /* step 3 - release vsp */
    if (dec_release(mHandle) < 0) {
        ERR("failed to release decoder");
        goto err;
    }

    INFO("Finish decoding %s(%s, %d frames) to %s(%d frames)", filename_bs, format2str(format), framenum_bs, filename_yuv, framenum_yuv);
    if (framenum_err > 0) {
        INFO(", %d frames failed", framenum_err);
    }
    if (framenum_bs > 0) {
        INFO(", average time = %dms", time_total_ms/framenum_bs);
    }
    INFO("\n");

err:
    if(mLibHandle) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    vsp_free (mHandle);

    if(s_pbuf_extra_v) {
        if (mIOMMUEnabled) {
            s_pmem_extra->free_mm_iova((int32)s_pbuf_extra_p, s_pbuf_extra_size);
        }

        s_pmem_extra.clear();
        s_pbuf_extra_v = NULL;
        s_pbuf_extra_p = 0;
        s_pbuf_extra_size = 0;
    }

    vsp_free(pbuf_inter);

    if (pbuf_stream != NULL) {
        if (mIOMMUEnabled) {
            pmem_stream->free_mm_iova((int32)pbuf_stream_phy, size_stream);
        }
        pmem_stream.clear();
        pbuf_stream = NULL;
        pbuf_stream_phy = 0;
        size_stream = 0;
    }

    for (i=0; i<DEC_YUV_BUFFER_NUM; i++) {
        if (pyuv[i] != NULL) {
            if (mIOMMUEnabled) {
                pmem_yuv420sp[i]->free_mm_iova((int32)pyuv_phy[i], size_yuv[i]);
            }
            pmem_yuv420sp[i].clear();
            pyuv[i] = NULL;
            pyuv_phy[i] = 0;
            size_yuv[i] = 0;
        }
    }

    if (py != NULL) {
        vsp_free(py);
        py = NULL;
    }
    if (pu != NULL) {
        vsp_free(pu);
        pu = NULL;
    }
    if (pv != NULL) {
        vsp_free(pv);
        pv = NULL;
    }

    if (fp_yuv != NULL) {
        fclose(fp_yuv);
        fp_yuv = NULL;
    }
    if (fp_bs != NULL) {
        fclose(fp_bs);
        fp_bs = NULL;
    }

    return 0;
}

