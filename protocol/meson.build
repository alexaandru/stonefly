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
sf_protocol_xmls = [
  'wlr-layer-shell-unstable-v1.xml',
  'wlr-foreign-toplevel-management-unstable-v1.xml',
  'ext-session-lock-v1.xml',
  'ext-idle-notify-v1.xml',
  'idle-inhibit-unstable-v1.xml',
  'wlr-output-power-management-unstable-v1.xml',
  'ext-background-effect-v1.xml',
]

# private-code only for protocols we implement by hand. Layer-shell is served
# via wlroots types; its private-code references xdg_popup_interface and would
# need xdg-shell private-code linked as well.
sf_protocol_code_xmls = [
  'wlr-foreign-toplevel-management-unstable-v1.xml',
  'ext-background-effect-v1.xml',
]

sf_protocol_headers = []
sf_protocol_sources = []
foreach xml : sf_protocol_xmls
  sf_protocol_headers += wayland_scanner_server.process(xml)
endforeach
foreach xml : sf_protocol_code_xmls
  sf_protocol_sources += wayland_scanner_code.process(xml)
endforeach

sf_protocols_dep = declare_dependency(
  sources: sf_protocol_headers + sf_protocol_sources,
)
