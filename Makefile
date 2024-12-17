include config.mk

.PHONY: all clean

all: $(BUILD_DIR)
	@echo "Starting build process..."
	@echo "BUILD_DIR = $(BUILD_DIR)"
	@for dir in $(BUILD_DIR); \
	do \
		echo "Building in $$dir..."; \
		make -C $$dir || exit "$$?"; \
	done

clean:
	rm -rf app/link_obj app/dep nginx
	rm -rf signal/*.gch app/*.gch

