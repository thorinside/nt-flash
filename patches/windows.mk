# Stage patched translation units instead of modifying the checkout. Each file
# has its own work directory so parallel builds and interrupted applies are safe.
WIN_STAGE := build/windows-patched
ifeq ($(filter $(SRC_DIR)/main.o,$(OBJS)),)
$(error Windows patch expects main.o)
endif
ifeq ($(filter $(BLFWK_SRC)/hid-windows.o,$(BLFWK_OBJS)),)
$(error Windows patch expects hid-windows.o)
endif
ifneq ($(MIDI_OBJS),$(SRC_DIR)/midi_win.o)
$(error Windows patch expects midi_win.o)
endif
OBJS := $(patsubst $(SRC_DIR)/main.o,$(WIN_STAGE)/main.o,$(OBJS))
MIDI_OBJS := $(WIN_STAGE)/midi_win.o
BLFWK_OBJS := $(patsubst $(BLFWK_SRC)/hid-windows.o,$(WIN_STAGE)/hid-windows.o,$(BLFWK_OBJS))
PLATFORM_LIBS += -lcfgmgr32

# The pinned NXP sources have CRLF in Git; apply the LF patches to normalized
# copies, independently of the host's core.autocrlf setting.
define stage_patch
	@mkdir -p $@.tmp
	$(if $(filter %.patch,$<),@rm -f $@.tmp/$(@F),@tr -d '\r' < $< > $@.tmp/$(@F))
	git -c core.autocrlf=false apply --directory=$@.tmp patches/windows/$(@F).patch
	@mv $@.tmp/$(@F) $@
	@rmdir $@.tmp
endef

$(WIN_STAGE)/main.cpp $(WIN_STAGE)/midi_win.c: $(WIN_STAGE)/%: $(SRC_DIR)/% patches/windows/%.patch
	$(stage_patch)

$(WIN_STAGE)/win_handoff.h: patches/windows/win_handoff.h.patch
	$(stage_patch)

$(WIN_STAGE)/main.o: $(WIN_STAGE)/win_handoff.h

$(WIN_STAGE)/hid-windows.c: $(WIN_STAGE)/%: $(BLFWK_SRC)/% patches/windows/%.patch
	$(stage_patch)

$(WIN_STAGE)/%.o: $(WIN_STAGE)/%.cpp patches/windows.mk
	$(CXX) $(CXXFLAGS) -MMD -MP -I$(SRC_DIR) -c -o $@ $<

$(WIN_STAGE)/%.o: $(WIN_STAGE)/%.c patches/windows.mk
	$(CC) $(CFLAGS) -MMD -MP -I$(SRC_DIR) -c -o $@ $<

-include $(wildcard $(WIN_STAGE)/*.d)

$(WIN_STAGE)/winmm-handoff.exe: tests/windows/winmm_handoff.c $(WIN_STAGE)/midi_win.c $(SRC_DIR)/midi.h
	$(CC) -I$(WIN_STAGE) -I$(SRC_DIR) $(CFLAGS) -UNDEBUG -o $@ $<

$(WIN_STAGE)/flashloader-reset.exe: tests/windows/flashloader_reset.cpp $(WIN_STAGE)/win_handoff.h $(BLFWK_OBJS)
	$(CXX) -I$(WIN_STAGE) $(CXXFLAGS) -UNDEBUG $(LDFLAGS) -o $@ $< $(BLFWK_OBJS) $(PLATFORM_LIBS)

$(WIN_STAGE)/hid-handoff.exe: tests/windows/hid_handoff.c $(WIN_STAGE)/hid-windows.c
	$(CC) -I$(WIN_STAGE) $(CFLAGS) -UNDEBUG -o $@ $< -lsetupapi

.PHONY: check-windows
check-windows: $(WIN_STAGE)/winmm-handoff.exe $(WIN_STAGE)/flashloader-reset.exe $(WIN_STAGE)/hid-handoff.exe
	./$(WIN_STAGE)/hid-handoff.exe
	./$(WIN_STAGE)/winmm-handoff.exe
	./$(WIN_STAGE)/flashloader-reset.exe

patch-lib: $(WIN_STAGE)/main.cpp $(WIN_STAGE)/midi_win.c $(WIN_STAGE)/win_handoff.h $(WIN_STAGE)/hid-windows.c

.PHONY: clean-windows-patches
clean: clean-windows-patches
clean-windows-patches:
	rm -rf $(WIN_STAGE)
