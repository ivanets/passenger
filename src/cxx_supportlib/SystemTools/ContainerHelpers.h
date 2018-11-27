/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  See LICENSE file for license information.
 */
#ifndef _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_
#define _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_

#include <boost/predef.h>
#include <FileTools/FileManip.h>

namespace Passenger {

using namespace std;


// adapted from https://github.com/systemd/systemd/blob/042ca/src/basic/virt.c
inline bool
detect_container() {
	enum {
		  CONTAINER_FOUND = 1,
		  CONTAINER_NONE = 0,
		  CONTAINER_NOT_CHECKED = -1
	};

	static thread_local int cached_found = CONTAINER_NOT_CHECKED;
	const char *e = NULL;

	if (cached_found != CONTAINER_NOT_CHECKED) {
		return cached_found;
	}

	// https://github.com/moby/moby/issues/26102#issuecomment-253621560
	if (fileExists("/.dockerenv")) {
		cached_found = CONTAINER_FOUND;
		return true;
	}

	if (fileExists("/proc/vz") && !fileExists("/proc/bc")) {
		cached_found = CONTAINER_FOUND;
		return true;
	}

	if (::getpid() == 1) {
		e = getenv("container");
		if (e == NULL || *e == '\0') {
			cached_found = CONTAINER_NONE;
			return false;
		} else {
			cached_found = CONTAINER_FOUND;
			return true;
		}
	}

	if (fileExists("/run/systemd/container")) {
		string file = unsafeReadFile("/run/systemd/container");
		if (file.length() > 0) {
			cached_found = CONTAINER_FOUND;
			return true;
		} else {
			cached_found = CONTAINER_NONE;
			return false;
		}
	}

	if (fileExists("/proc/1/sched")) {
		string file = unsafeReadFile("/proc/1/sched");
		if (file.length() >= 0) {
			const char t = file[0];
			if (t == '\0') {
				cached_found = CONTAINER_NONE;
				return false;
			}

			if (!startsWith(file, "(1,")) {
				cached_found = CONTAINER_FOUND;
				return true;
			}
		}
	}

	return false;
}

inline bool
autoDetectInContainer() {
	#if BOOST_OS_LINUX
		return detect_container();
	#else
		return false;
	#endif
}


} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_ */
