################################################################################
#
# epass_drm_app
#
################################################################################


EPASS_DRM_APP_VERSION = 0cea8ba016970f6a9eefb631d13332bb06f72bcd
EPASS_DRM_APP_SITE = $(call github,rhodesepass,drm_app_neo,$(EPASS_DRM_APP_VERSION))
# epass-fonts: 提供 pkg-config 'epass-fonts', app 构建期据此取共享字体目录,
# 不再自带字体 (字体由 epass-fonts 包装到 /usr/share/fonts/epass)。
EPASS_DRM_APP_DEPENDENCIES = freetype libdrm libpng libevdev epass-fonts lvgl
EPASS_DRM_APP_CONF_OPTS = -DBUILD_SHARED_LIBS=OFF --fresh

define EPASS_DRM_APP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/app_360 $(TARGET_DIR)/root/app_360
	$(INSTALL) -D -m 0755 $(@D)/app_720 $(TARGET_DIR)/root/app_720
	cp -a $(@D)/res $(TARGET_DIR)/root/
endef


$(eval $(cmake-package))
