# Stonefly root Makefile — run from this directory.
ROOT := $(CURDIR)
# aports-style: dist/testing/stonefly → repo name "testing" under REPODEST
# (default REPODEST is often /home/apk → /home/apk/testing/x86_64/).
APORT := dist/testing/stonefly
PKGVER := 0.0.1
TARBALL := $(APORT)/stonefly-$(PKGVER).tar.gz

# Directory mtimes do not bump when a file inside src/ changes — depend on
# every packed path so `make apk` cannot ship a stale tree.
SRC_FILES := $(shell find src protocol -type f \( -name '*.c' -o -name '*.h' -o -name '*.xml' -o -name 'meson.build' \) | sort)
PACK_META := meson.build LICENSE LICENSE.tinywl README.md

.PHONY: apk dist-tarball clean

dist-tarball: $(TARBALL)

$(TARBALL): $(PACK_META) $(SRC_FILES)
	mkdir -p "$(APORT)"
	tar -C "$(ROOT)" -czf "$(TARBALL)" \
		--transform 's,^,stonefly-$(PKGVER)/,' \
		meson.build src protocol LICENSE LICENSE.tinywl README.md

# checksum + build; APK lands under $$REPODEST/testing/x86_64/.
apk: $(TARBALL)
	cd "$(APORT)" && abuild checksum && abuild -r

clean:
	rm -f "$(TARBALL)"
	rm -rf "$(APORT)/src" "$(APORT)/pkg"
