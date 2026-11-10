/* Snap edge identity shared by toplevel state and drag-snap. */
#ifndef SF_WM_SNAP_H
#define SF_WM_SNAP_H

enum sf_snap {
	SF_SNAP_NONE = 0,
	SF_SNAP_LEFT,
	SF_SNAP_RIGHT,
	SF_SNAP_TOP,
	SF_SNAP_BOTTOM,
};

/* IPC / debug name; empty string for SF_SNAP_NONE. */
static inline const char *sf_snap_name(enum sf_snap snap) {
	switch (snap) {
	case SF_SNAP_LEFT:
		return "left";
	case SF_SNAP_RIGHT:
		return "right";
	case SF_SNAP_TOP:
		return "top";
	case SF_SNAP_BOTTOM:
		return "bottom";
	case SF_SNAP_NONE:
		break;
	}
	return "";
}

#endif
