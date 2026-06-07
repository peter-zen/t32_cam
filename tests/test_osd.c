#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_framesource.h>
#include <imp/imp_isp.h>
#include <imp/imp_encoder.h>

int main(int argc, char *argv[])
{
    int ret = 0;

    ret = IMP_ISP_Open();
    if (ret < 0) { printf("IMP_ISP_Open failed\n"); return -1; }

    ret = IMP_System_Init();
    if (ret < 0) { printf("IMP_System_Init failed\n"); return -1; }

    ret = IMP_ISP_EnableTuning();
    if (ret < 0) { printf("IMP_ISP_EnableTuning failed\n"); return -1; }

    ret = IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
    if (ret < 0) { printf("IMP_ISP_Tuning_SetOsdPoolSize failed\n"); return -1; }

    int handle = IMP_ISP_Tuning_CreateOsdRgn(0, NULL);
    if (handle < 0) { printf("IMP_ISP_Tuning_CreateOsdRgn failed, handle=%d\n", handle); return -1; }
    printf("CreateOsdRgn success, handle=%d\n", handle);

    printf("sizeof(IMPIspOsdAttrAsm)=%zu\n", sizeof(IMPIspOsdAttrAsm));
    printf("sizeof(IMPISPOSDSingleAttr)=%zu\n", sizeof(IMPISPOSDSingleAttr));
    printf("sizeof(IMPISPOSDAttr)=%zu\n", sizeof(IMPISPOSDAttr));
    printf("sizeof(IMPISPOSDBlockAttr)=%zu\n", sizeof(IMPISPOSDBlockAttr));

    // Test 1: memset to zero + set all fields (original approach)
    {
        IMPIspOsdAttrAsm attr;
        memset(&attr, 0, sizeof(attr));
        attr.type = ISP_OSD_REG_PIC;
        attr.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
        attr.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
        attr.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
        attr.stsinglepicAttr.pic.pinum = handle;
        attr.stsinglepicAttr.pic.osd_enable = 1;
        attr.stsinglepicAttr.pic.osd_left = 10;
        attr.stsinglepicAttr.pic.osd_top = 10;
        attr.stsinglepicAttr.pic.osd_width = 320;
        attr.stsinglepicAttr.pic.osd_height = 34;

        uint32_t *data = (uint32_t *)malloc(320 * 34 * sizeof(uint32_t));
        memset(data, 0xFF, 320 * 34 * sizeof(uint32_t));
        attr.stsinglepicAttr.pic.osd_image = (char *)data;
        attr.stsinglepicAttr.pic.osd_stride = 320 * 4;

        ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, handle, &attr);
        printf("Test1 (memset+all fields): SetOsdRgnAttr ret=%d\n", ret);
        free(data);
    }

    // Test 2: Get default first, then only change type and enable
    {
        IMPIspOsdAttrAsm attr;
        memset(&attr, 0, sizeof(attr));
        ret = IMP_ISP_Tuning_GetOsdRgnAttr(0, handle, &attr);
        printf("Test2: GetOsdRgnAttr ret=%d type=%d pinum=%d enable=%d left=%d top=%d w=%d h=%d stride=%d\n",
               ret, attr.type, attr.stsinglepicAttr.pic.pinum, attr.stsinglepicAttr.pic.osd_enable,
               attr.stsinglepicAttr.pic.osd_left, attr.stsinglepicAttr.pic.osd_top,
               attr.stsinglepicAttr.pic.osd_width, attr.stsinglepicAttr.pic.osd_height,
               attr.stsinglepicAttr.pic.osd_stride);

        // Only change type and enable, keep other defaults from Get
        attr.type = ISP_OSD_REG_PIC;
        attr.stsinglepicAttr.pic.osd_enable = 1;
        attr.stsinglepicAttr.pic.osd_left = 10;
        attr.stsinglepicAttr.pic.osd_top = 10;
        attr.stsinglepicAttr.pic.osd_width = 320;
        attr.stsinglepicAttr.pic.osd_height = 34;
        uint32_t *data = (uint32_t *)malloc(320 * 34 * sizeof(uint32_t));
        memset(data, 0xFF, 320 * 34 * sizeof(uint32_t));
        attr.stsinglepicAttr.pic.osd_image = (char *)data;
        attr.stsinglepicAttr.pic.osd_stride = 320 * 4;

        ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, handle, &attr);
        printf("Test2 (Get default + change type/enable): SetOsdRgnAttr ret=%d\n", ret);
        free(data);
    }

    // Test 3: memset to zero, only set type=PIC and osd_enable=1, nothing else
    {
        IMPIspOsdAttrAsm attr;
        memset(&attr, 0, sizeof(attr));
        attr.type = ISP_OSD_REG_PIC;
        attr.stsinglepicAttr.pic.osd_enable = 1;

        ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, handle, &attr);
        printf("Test3 (only type+enable): SetOsdRgnAttr ret=%d\n", ret);
    }

    // Test 4: memset to zero, set type=PIC, osd_enable=1, osd_image=NULL, osd_stride=0
    {
        IMPIspOsdAttrAsm attr;
        memset(&attr, 0, sizeof(attr));
        attr.type = ISP_OSD_REG_PIC;
        attr.stsinglepicAttr.pic.osd_enable = 1;
        attr.stsinglepicAttr.pic.osd_left = 10;
        attr.stsinglepicAttr.pic.osd_top = 10;
        attr.stsinglepicAttr.pic.osd_width = 320;
        attr.stsinglepicAttr.pic.osd_height = 34;
        attr.stsinglepicAttr.pic.osd_image = NULL;
        attr.stsinglepicAttr.pic.osd_stride = 0;

        ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, handle, &attr);
        printf("Test4 (NULL image, 0 stride): SetOsdRgnAttr ret=%d\n", ret);
    }

    // Test 5: memset to zero, set type=PIC, osd_enable=1, small size 64x64
    {
        IMPIspOsdAttrAsm attr;
        memset(&attr, 0, sizeof(attr));
        attr.type = ISP_OSD_REG_PIC;
        attr.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
        attr.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
        attr.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
        attr.stsinglepicAttr.pic.pinum = handle;
        attr.stsinglepicAttr.pic.osd_enable = 1;
        attr.stsinglepicAttr.pic.osd_left = 10;
        attr.stsinglepicAttr.pic.osd_top = 10;
        attr.stsinglepicAttr.pic.osd_width = 64;
        attr.stsinglepicAttr.pic.osd_height = 64;
        uint32_t *data = (uint32_t *)malloc(64 * 64 * sizeof(uint32_t));
        memset(data, 0xFF, 64 * 64 * sizeof(uint32_t));
        attr.stsinglepicAttr.pic.osd_image = (char *)data;
        attr.stsinglepicAttr.pic.osd_stride = 64 * 4;

        ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, handle, &attr);
        printf("Test5 (64x64): SetOsdRgnAttr ret=%d\n", ret);
        free(data);
    }

    IMP_ISP_Tuning_DestroyOsdRgn(0, handle);
    IMP_ISP_DisableTuning();
    IMP_System_Exit();
    IMP_ISP_Close();
    return 0;
}
