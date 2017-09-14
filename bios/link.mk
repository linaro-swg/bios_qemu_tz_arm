link-script = bios/kern.ld.S
link-script-pp = $(out-dir)bios/kern.ld
link-script-dep = $(out-dir).kern.ld.d

all: $(out-dir)bios.elf $(out-dir)bios.dmp $(out-dir)bios.bin
all: $(out-dir)bios.symb_sizes
cleanfiles += $(out-dir)bios.elf $(out-dir)bios.dmp $(out-dir)bios.bin
cleanfiles += $(out-dir)bios.map $(out-dir)bios.symb_sizes
cleanfiles += $(out-dir)bios.bin
cleanfiles += $(link-script-pp) $(link-script-dep)

link-ldflags  = $(LDFLAGS)
link-ldflags += -T $(link-script-pp) -Map=$(out-dir)bios.map

link-ldadd  = $(LDADD)
link-ldadd += $(addprefix -L,$(libdirs))
link-ldadd += $(addprefix -l,$(libnames))

ldargs-bios.elf := $(link-ldflags) $(objs) $(link-ldadd) $(libgcc)

link-script-cppflags :=  \
	$(filter-out $(CPPFLAGS_REMOVE) $(cppflags-remove), \
		$(nostdinc) $(CPPFLAGS) \
		$(addprefix -I,$(incdirs$(sm))) $(cppflags$(sm)))


-include $(link-script-dep)

$(link-script-pp): $(link-script)
	@echo '  CPP     $@'
	@mkdir -p $(dir $@)
	$(q)$(CPP) -Wp,-P,-MT,$@,-MD,$(link-script-dep) \
		$(link-script-cppflags) $< > $@

$(out-dir)bios.elf: $(objs) $(libdeps) $(link-script-pp)
	@echo '  LD      $@'
	$(q)$(LD) $(ldargs-bios.elf) -o $@

$(out-dir)bios.dmp: $(out-dir)bios.elf
	@echo '  OBJDUMP $@'
	$(q)$(OBJDUMP) -l -x -d $< > $@

$(out-dir)bios.bin: $(out-dir)bios.elf
	@echo '  OBJCOPY $@'
	$(q)$(OBJCOPY) -O binary $< $@

$(out-dir)bios.symb_sizes: $(out-dir)bios.elf
	@echo '  GEN     $@'
	$(q)$(NM) --print-size --reverse-sort --size-sort $< > $@
