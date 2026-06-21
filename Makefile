TARGET = adrenaline_disc_swap
BUILD_PRX = 1

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

OBJS = adrenaline_disc_swap.o

INCDIR =
CFLAGS = -Os -G0 -Wall -Wextra -fno-strict-aliasing -fno-builtin-printf
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

# The default build is the working Adrenaline/Inferno path:
# reopen the selected CSO, then refresh disc0: so isofs sees the new disc.
ifndef NO_REMOUNT
CFLAGS += -DDISC_CHANGE_REMOUNT
endif

ifdef DIRECT_REMOUNT
CFLAGS += -DDISC_CHANGE_REMOUNT_DIRECT
endif

ifdef DIAG
CFLAGS += -DDISC_CHANGE_DIAG
endif

LIBDIR =
LDFLAGS =
LIBS = -ltinyfont -lpspdisplay_driver -lpspctrl_driver -lpspumd_driver -lpspsystemctrl_kernel -lpspsystemctrl_user -lpspkernel

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
