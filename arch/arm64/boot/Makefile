#
# arch/arm64/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2012, ARM Ltd.
# Author: Will Deacon <will.deacon@arm.com>
#
# Based on the ia64 boot/Makefile.
#

OBJCOPYFLAGS_Image :=-O binary -R .note -R .note.gnu.build-id -R .comment -S

targets := Image Image.gz Image.fit

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/Image.bz2: $(obj)/Image FORCE
	$(call if_changed,bzip2)

$(obj)/Image.gz: $(obj)/Image FORCE
	$(call if_changed,gzip)

$(obj)/Image.lz4: $(obj)/Image FORCE
	$(call if_changed,lz4)

$(obj)/Image.lzma: $(obj)/Image FORCE
	$(call if_changed,lzma)

$(obj)/Image.lzo: $(obj)/Image FORCE
	$(call if_changed,lzo)

install:
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

quiet_cmd_fitimage = DTC     $@ ($(DTB_OBJS))
      cmd_fitimage = $(CONFIG_SHELL) $(srctree)/scripts/generate-its-script.sh \
			-a $(ARCH) $< $(DTB_OBJS) | \
			$(objtree)/scripts/dtc/dtc -I dts -O dtb -p 1024 > $@
			#-a $(ARCH) -r ~/src/pbs.git/build/tegra210/initrd.gz $< $(DTB_OBJS) |

$(obj)/Image.fit: $(obj)/Image $(DTB_OBJS) FORCE
	$(call if_changed,fitimage)

zinstall:
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image.gz System.map "$(INSTALL_PATH)"
