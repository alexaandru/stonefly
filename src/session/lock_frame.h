#ifndef SF_LOCK_FRAME_H
#define SF_LOCK_FRAME_H

#include <stdbool.h>
#include <stdint.h>

/* Per enabled head, reset on lock acquisition or a coverage change. */
struct sf_lock_frame {
	uint32_t seq;
	bool submitted;
	bool presented;
};

static inline bool sf_lock_frame_feedback(struct sf_lock_frame *frame, uint32_t seq,
					  bool presented) {
	if (!frame->submitted || seq != frame->seq) {
		return false;
	}
	frame->submitted = false;
	frame->presented = presented;
	return true;
}

#endif
