# The default product packages treated as base.mk in sprdroid4.1
PRODUCT_PACKAGES += \
	AudioProfile \
	FMPlayer \
	SprdRamOptimizer \
        Flashlight \
	FileExplorer \
	NoteBook \
	EngineerMode \
	ValidationTools \
	DrmProvider \
	CellBroadcastReceiver \
	SprdQuickSearchBox \
        Carddav-Sync \
	SystemUpdate \
        Caldav-Sync.apk\
        libsprd_agps_agent
#	libsprddm \

ifneq (none,$(strip $(PRODUCT_VIDEO_WALLPAPERS)))
PRODUCT_VIDEO_WALLPAPERS := Sunny Cloud Rain Cartoon
$(call inherit-product-if-exists, frameworks/base/data/videos/VideoPackageForUUI.mk)
PRODUCT_PACKAGES += VideoWallpaper
endif

ifeq ($(TARGET_LOWCOST_SUPPORT),true)
    ifneq ($(MULTILANGUAGE_SUPPORT),true)
        PRODUCT_PACKAGES += PinyinIME
		PRODUCT_PACKAGES += OpenWnn
    endif
else
    PRODUCT_PACKAGES += PinyinIME
endif

#add for apn mnc mcc
ifneq ($(shell ls -d vendor/sprd/operator/operator_res 2>/dev/null),)
DEVICE_PACKAGE_OVERLAYS += vendor/sprd/operator/operator_res/operatorname_overlay
APN_VERSION := $(shell cat frameworks/base/core/res/res/xml/apns.xml|grep "<apns version"|cut -d \" -f 2)
PRODUCT_COPY_FILES += vendor/sprd/operator/operator_res/apn/apns-conf_$(APN_VERSION).xml:system/etc/apns-conf.xml
endif
