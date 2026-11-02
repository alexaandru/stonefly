# wlroots headers include "*-protocol.h" (wayland-scanner server-header).
# Compositors must generate those; Alpine does not ship them from -dev.

wayland_scanner = find_program('wayland-scanner')

wayland_scanner_server = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.h',
  arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_code = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

# Headers for every XML we include from stonefly or that wlroots headers pull in.
stonefly_protocol_xmls = [
  'wlr-layer-shell-unstable-v1.xml',
  'wlr-foreign-toplevel-management-unstable-v1.xml',
]

# private-code only for protocols we implement by hand. Layer-shell is served
# via wlroots types; its private-code references xdg_popup_interface and would
# need xdg-shell private-code linked as well.
stonefly_protocol_code_xmls = [
  'wlr-foreign-toplevel-management-unstable-v1.xml',
]

stonefly_protocol_headers = []
stonefly_protocol_sources = []
foreach xml : stonefly_protocol_xmls
  stonefly_protocol_headers += wayland_scanner_server.process(xml)
endforeach
foreach xml : stonefly_protocol_code_xmls
  stonefly_protocol_sources += wayland_scanner_code.process(xml)
endforeach

stonefly_protocols_dep = declare_dependency(
  sources: stonefly_protocol_headers + stonefly_protocol_sources,
)
