/* Override Retro68's default 1 MB partition -- Relic's code+BSS alone is
 * ~1.4 MB, plus RetroConsole heap and TLS buffers. */
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	notHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	8 * 1024 * 1024,	/* preferred */
	4 * 1024 * 1024 	/* minimum */
};
