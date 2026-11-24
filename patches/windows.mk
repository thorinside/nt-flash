# Stage patched translation units instead of modifying the checkout. Each file
# has its own work directory so parallel builds and interrupted applies are safe.
WIN_STAGE := build/windows-patched
OBJS := $(patsubst $(SRC_DIR)/main.o,$(WIN_STAGE)/main.o,$(OBJS))
MIDI_OBJS := $(WIN_STAGE)/midi_win.o
BLFWK_OBJS := $(patsubst $(BLFWK_SRC)/hid-windows.o,$(WIN_STAGE)/hid-windows.o,$(BLFWK_OBJS))
BLFWK_OBJS := $(patsubst $(BLFWK_SRC)/Command.o,$(WIN_STAGE)/Command.o,$(BLFWK_OBJS))
PLATFORM_LIBS += -lcfgmgr32

# The pinned NXP sources have CRLF in Git; apply the LF patches to normalized
# copies, independently of the host's core.autocrlf setting.
define stage_patch
	@mkdir -p $@.tmp
	@tr -d '\r' < $< > $@.tmp/$(@F)
	git -c core.autocrlf=false apply --directory=$@.tmp patches/windows/$(@F).patch
	@mv $@.tmp/$(@F) $@
	@rmdir $@.tmp
endef

$(WIN_STAGE)/main.cpp $(WIN_STAGE)/midi_win.c: $(WIN_STAGE)/%: $(SRC_DIR)/% patches/windows/%.patch
	$(stage_patch)

$(WIN_STAGE)/Command.cpp $(WIN_STAGE)/hid-windows.c: $(WIN_STAGE)/%: $(BLFWK_SRC)/% patches/windows/%.patch
	$(stage_patch)

$(WIN_STAGE)/%.o: $(WIN_STAGE)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -c -o $@ $<

$(WIN_STAGE)/%.o: $(WIN_STAGE)/%.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c -o $@ $<

$(WIN_STAGE)/winmm-handoff.exe: tests/windows/winmm_handoff.c $(WIN_STAGE)/midi_win.c $(SRC_DIR)/midi.h
	$(CC) -I$(WIN_STAGE) -I$(SRC_DIR) $(CFLAGS) -UNDEBUG -o $@ $<

$(WIN_STAGE)/flashloader-reset.exe: tests/windows/flashloader_reset.cpp $(BLFWK_OBJS)
	$(CXX) $(CXXFLAGS) -UNDEBUG $(LDFLAGS) -o $@ $^ $(PLATFORM_LIBS)

$(WIN_STAGE)/hid-handoff.exe: tests/windows/hid_handoff.c $(WIN_STAGE)/hid-windows.c
	$(CC) -I$(WIN_STAGE) $(CFLAGS) -UNDEBUG -o $@ $< -lsetupapi

.PHONY: check-windows
check-windows: $(WIN_STAGE)/winmm-handoff.exe $(WIN_STAGE)/flashloader-reset.exe $(WIN_STAGE)/hid-handoff.exe
	./$(WIN_STAGE)/hid-handoff.exe
	./$(WIN_STAGE)/winmm-handoff.exe
	./$(WIN_STAGE)/flashloader-reset.exe

patch-lib: $(WIN_STAGE)/main.cpp $(WIN_STAGE)/midi_win.c $(WIN_STAGE)/Command.cpp $(WIN_STAGE)/hid-windows.c

.PHONY: clean-windows-patches
clean: clean-windows-patches
clean-windows-patches:
	rm -rf $(WIN_STAGE)
