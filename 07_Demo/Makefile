# Import this project in Efinity RISC-V IDE, then Project > Build Project.
EFINITY_HOME ?= F:/Efinity/2026.1
EF := $(subst \,/,$(EFINITY_HOME))
CC := $(EF)/toolchain/bin/riscv-none-elf-gcc.exe
OBJCOPY := $(EF)/toolchain/bin/riscv-none-elf-objcopy.exe
SIZE := $(EF)/toolchain/bin/riscv-none-elf-size.exe
ENGINE := ../01_Engine/software
BSP := ../01_Engine/BSP
CFLAGS := -march=rv32im_zicsr_zifencei -mabi=ilp32 -O2 -g3 -DUSE_GP -DSMP -DFS_DEMO_COMPACT -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-main
CFLAGS += -DSYSTEM_UART_A_APB=SYSTEM_UART_0_IO_APB -DSYSTEM_GPIO_A_APB=SYSTEM_GPIO_0_IO_APB -DSYSTEM_I2C_A_APB=SYSTEM_I2C_0_IO_APB
CFLAGS += -Isrc -I../shared -I$(ENGINE)/include -I$(BSP)/include -I$(BSP)/driver
NAMES := demo_main.c taisei_render.c suite_demo.c demo_control.c sh_assets.c sh_background.c fs_blitter.c fs_draw.c fs_scanline.c fs_sprite_trim.c
OBJECTS := $(addprefix build/,$(NAMES:.c=.o)) build/start.o build/background.o
VPATH := src:$(ENGINE)/src
.PHONY: all clean
all: build/framespeed.elf build/framespeed.bin build/framespeed.hex
build:
	-mkdir build
build/%.o: %.c | build
	"$(CC)" $(CFLAGS) -MMD -MP -c $< -o $@
build/start.o: $(BSP)/common/start.S | build
	"$(CC)" $(CFLAGS) -c $< -o $@
build/background.o: ../03_Assets/background_store.bin | build
	"$(OBJCOPY)" -I binary -O elf32-littleriscv -B riscv --rename-section .data=.background,alloc,load,readonly,data,contents $< $@
build/framespeed.elf: $(OBJECTS) suite.ld
	"$(CC)" $(CFLAGS) -o $@ $(OBJECTS) -L$(BSP)/common -specs=nosys.specs -nostartfiles -Wl,-Bstatic,-T,suite.ld,-Map,build/framespeed.map,--print-memory-usage,--gc-sections -Wl,--start-group -lc -lm -lgcc -Wl,--end-group
	"$(SIZE)" $@
build/framespeed.bin: build/framespeed.elf
	"$(OBJCOPY)" -O binary --remove-section=.background $< $@
build/framespeed.hex: build/framespeed.elf
	"$(OBJCOPY)" -O ihex --remove-section=.background $< $@
clean:
	-$(RM) build/*.o build/*.d build/*.elf build/*.bin build/*.hex build/*.map
-include $(OBJECTS:.o=.d)
