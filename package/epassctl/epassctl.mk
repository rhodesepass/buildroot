################################################################################
#
# epassctl
#
################################################################################


EPASSCTL_VERSION = 3fb8a167298b4d050e5446eba0997ef2e5467006
EPASSCTL_SITE = https://github.com/rhodesepass/epassctl.git
EPASSCTL_SITE_METHOD = git
EPASSCTL_DEPENDENCIES = 
EPASSCTL_CONF_OPTS = 

define EPASSCTL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/epassctl $(TARGET_DIR)/usr/bin/
endef

$(eval $(cmake-package))
