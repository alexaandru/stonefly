# wlroots headers include "*-protocol.h" (wayland-scanner server-header).
# Compositors must generate those; Alpine does not ship them from -dev.

wayland_scanner = find_program('wayland-scanner')

wayland_scanner_server = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.h',
  arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)

stonefly_protocol_xmls = [
  'wlr-layer-shell-unstable-v1.xml',
]

stonefly_protocol_headers = []
foreach xml : stonefly_protocol_xmls
  stonefly_protocol_headers += wayland_scanner_server.process(xml)
endforeach

stonefly_protocols_dep = declare_dependency(
  sources: stonefly_protocol_headers,
)
