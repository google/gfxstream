ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

define PINFO
PINFO DESCRIPTION=virtio-gpu backend renderer
PINFO COMMIT=${shell git log --pretty=format:'%H' -n1 }
endef
USEFILE=

include $(MKFILES_ROOT)/qtargets.mk

FORCE:
%.so: FORCE
	$(ADD_PINFO)
	$(ADD_USAGE)
