################################################################################
#
# epass_applications
#
################################################################################

EPASS_APPLICATIONS_VERSION = a7ea4d1b6ac701168695fa09c722c67f4874297f
EPASS_APPLICATIONS_SITE = $(call github,rhodesepass,epass-applications,$(EPASS_APPLICATIONS_VERSION))
EPASS_APPLICATIONS_DEPENDENCIES = \
	dosfstools \
	e2fsprogs \
	epass-fonts \
	freetype \
	libdrm \
	libpng \
	lvgl
EPASS_APPLICATIONS_CONF_OPTS = -DBUILD_SHARED_LIBS=OFF

# Install only the applications component, excluding bundled LVGL development files.
# Relative CMake destinations plus this prefix produce /app/<app_folder>/.
define EPASS_APPLICATIONS_INSTALL_TARGET_CMDS
	DESTDIR=$(TARGET_DIR) $(BR2_CMAKE) \
		--install $(@D) \
		--prefix /app \
		--component applications
endef

$(eval $(cmake-package))
