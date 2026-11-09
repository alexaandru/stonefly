-- Default bootstrap (Fennel-first). Policy lives in the *.fnl modules below.
-- Reorder requires here if modules ever depend on each other.
stonefly.packadd("fennel-shim")
require("options")
require("binds")
require("scratchpads")
require("rules")
require("inputs")
require("outputs")
