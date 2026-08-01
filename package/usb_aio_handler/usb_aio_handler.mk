################################################################################
#
# usb_aio_handler
#
################################################################################

# 本地源码树，靠 local.mk 的 USB_AIO_HANDLER_OVERRIDE_SRCDIR 提供

USB_AIO_HANDLER_VERSION = 74dc61110a1ebfef85d697af2a138f3ff9a29c46
USB_AIO_HANDLER_SITE = https://github.com/rhodesepass/usb_aio_handler.git
USB_AIO_HANDLER_SITE_METHOD = git
USB_AIO_HANDLER_DEPENDENCIES =


USB_AIO_HANDLER_CONF_OPTS = -DBUILD_SHARED_LIBS=OFF

define USB_AIO_HANDLER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/usb_aio_handler $(TARGET_DIR)/usr/sbin/usb_aio_handler
	$(INSTALL) -D -m 0755 $(@D)/usbaioctl $(TARGET_DIR)/usr/sbin/usbaioctl
endef

$(eval $(cmake-package))

# ---- host 侧:pyhost 上位机(同一份源码树的 pyhost/,装成 usbaiohost) ----
# OVERRIDE_SRCDIR 由 host 包自动继承 local.mk 里 target 包的设置。
# 纯 python 无构建步骤;运行期需要开发机系统里有 libusb-1.0。
HOST_USB_AIO_HANDLER_DEPENDENCIES = host-python3 host-python-pyusb

define HOST_USB_AIO_HANDLER_INSTALL_CMDS
	$(INSTALL) -D -m 0644 $(@D)/pyhost/client.py $(HOST_DIR)/lib/usbaiohost/client.py
	$(INSTALL) -D -m 0644 $(@D)/pyhost/protocol.py $(HOST_DIR)/lib/usbaiohost/protocol.py
	printf '#!/bin/sh\nexec "$(HOST_DIR)/bin/python3" "$(HOST_DIR)/lib/usbaiohost/client.py" "$$@"\n' \
		> $(HOST_DIR)/bin/usbaiohost
	chmod 0755 $(HOST_DIR)/bin/usbaiohost
endef

$(eval $(host-generic-package))
