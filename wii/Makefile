#---------------------------------------------------------------------------------
# Old Man Burn, the Wii arcade edition. Builds boot.dol for the Homebrew Channel:
#   make            -> boot.dol (copy it with meta.xml and icon.png to sd:/apps/oldmanburn/)
#   make AUTOPLAY=1 -> a test build that plays itself and saves screenshots (see test/wii-shots.sh)
# Shares the road, the drawing and the menus with the 3DS version in ../source.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif
include $(DEVKITPPC)/wii_rules

TARGET		:=	boot
BUILD		:=	build
SOURCES		:=	source ../source
SHARED		:=	sim.c draw.c look.c ui.c audio.c
FONTS		:=	../fonts/DMMono-Regular.ttf ../fonts/DelaGothicOne-Regular.ttf

CFLAGS	= -g -O2 -Wall -std=gnu11 $(MACHDEP) $(INCLUDE) -Wno-misleading-indentation
ifneq ($(AUTOPLAY),)
CFLAGS	+= -DAUTOPLAY
TARGET	:= boot-autoplay
BUILD	:= build-autoplay
endif
LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS	:=	-lfreetype -lpng -lbz2 -lz -lbrotlidec -lbrotlicommon -lfat -lasnd -lwiiuse -lbte -logc -lm
LIBDIRS	:=	$(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(CURDIR)/source $(CURDIR)/../source $(CURDIR)/../fonts
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
CFILES		:=	$(notdir $(wildcard source/*.c)) $(SHARED)
BINFILES	:=	$(notdir $(FONTS))
export LD	:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CFILES:.c=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE	:=	-iquote $(CURDIR)/source -iquote $(CURDIR)/../source $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(PORTLIBS_PATH)/ppc/include/freetype2 -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: $(BUILD) clean
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr build build-autoplay boot.elf boot.dol boot-autoplay.elf boot-autoplay.dol
else
DEPENDS	:=	$(OFILES:.o=.d)
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OFILES_SOURCES) : $(HFILES)
%.ttf.o	%_ttf.h :	%.ttf
	@echo $(notdir $<)
	$(bin2o)
-include $(DEPENDS)
endif
