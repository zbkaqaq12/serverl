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
	rm -rf app/link_obj app/dep serverl
	rm -rf signal/*.gch app/*.gch
	rm -rf net/http/*.o net/http/*.d
	rm -rf misc/json/*.o misc/json/*.d
	rm -rf misc/validator/*.o misc/validator/*.d
	rm -rf logic/controller/*.o logic/controller/*.d
	rm -rf logic/service/*.o logic/service/*.d
	rm -rf logic/model/*.o logic/model/*.d
