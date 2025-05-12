ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

.PHONY: gfxstream
.PHONY: aemu
.PHONY: clean
.PHONY: install

all: gfxstream

check-sdp:
	@if [ -z "$${QNX_TARGET}" ]; then \
		echo "'QNX_TARGET' not set. Make sure QNX SDP environment is configured"; \
		exit 1; \
	fi
	@if [ -z "$${QNX_HOST}" ]; then \
		echo "'QNX_HOST' not set. Make sure QNX SDP environment is configured"; \
		exit 1; \
	fi

check-install-root:
	@if [ -z "$${INSTALL_ROOT_nto}" ]; then \
		echo "'INSTALL_ROOT_nto' not set."; \
		exit 1; \
	fi

aemu: check-sdp
	../build-aemu.sh

gfxstream: check-sdp aemu
	../build-gfxstream.sh

install: check-sdp check-install-root gfxstream
	$(MAKE) -f ../../pinfo.mk gfxstream/host/libgfxstream*
	$(LN_HOST) -s libgfxstream_backend.so gfxstream/host/libgfxstream.so
	find gfxstream/host/  -maxdepth 1 \( -type f -o -type l \) -name "libgfxstream*" \
		-exec $(CP_HOST) -d {} $(INSTALL_ROOT_nto)/$(VARIANT)/usr/lib/ \;

clean:
	rm -rf aemu
	rm -rf gfxstream
	rm -f *.pinfo
	@if [ -n "$${INSTALL_ROOT_nto}" ]; then \
		rm -f $(INSTALL_ROOT_nto)/$(VARIANT)/usr/lib/libgfxstream* ;\
	fi
	
