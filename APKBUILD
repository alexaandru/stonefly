# Maintainer: Alexandru Ungur <alexaandru@gmail.com>
pkgname=stonefly
pkgver=0.0.1
pkgrel=0
pkgdesc="Wayland compositor/WM (tinywl 0.20 seed)"
url="https://github.com/alexaandru/stonefly"
arch="all"
license="MIT"
depends="
	foot
	wlroots0.20
	"
makedepends="
	clang
	libxkbcommon-dev
	meson
	samurai
	wayland-dev
	wayland-protocols
	wlroots0.20-dev
	"
options="!check"
source="
	stonefly.c
	meson.build
	LICENSE
	LICENSE.tinywl
	"
builddir="$srcdir"

build() {
	export CC=clang
	abuild-meson . output
	meson compile -C output
}

package() {
	DESTDIR="$pkgdir" meson install --no-rebuild -C output
}

sha512sums="
842d50a5f10520547207b07af05d86fe3a9ad48d08abb91a1b1c01f5f0d43a590931837464a17c47c81f175ba27858577dab825a950d074ab28a8958c110553b  stonefly.c
fb55c0b642ac42c891f61f83ca349b8ce6eefe9e7848f60a663894a456909a8468b4a9ce70e5e29b98cdc4efa06ac4965567e1dd1dab4c05e0dbfb849a10dada  meson.build
4108624e3896802d02d9a312418efbd323f115614ce47eb02316d97ba8621a341febf3398baa3e3bfbadeb09b23af43b40fffb7a3f5753d51361d27c606b4e16  LICENSE
82ccd03fe5c99edb10531fab32066256a2555a31d074ad02527c9030a3dfc707f35f2d3f6b1b8de3747c15cdfdbafdf9903696f8a5f33c0a547a83e7b42d6cee  LICENSE.tinywl
"
