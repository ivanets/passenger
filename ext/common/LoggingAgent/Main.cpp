/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  See LICENSE file for license information.
 */
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <signal.h>

#include "../AgentBase.h"
#include "../AccountsDatabase.h"
#include "../Account.h"
#include "../ServerInstanceDir.h"
#include "LoggingServer.h"
#include "../Exceptions.h"
#include "../Utils.h"
#include "../Utils/IOUtils.h"
#include "../Utils/Base64.h"
#include "../Utils/VariantMap.h"

using namespace oxt;
using namespace Passenger;


static struct ev_loop *eventLoop;
static LoggingServer *loggingServer;

static struct ev_loop *
createEventLoop() {
	struct ev_loop *loop;
	
	// libev doesn't like choosing epoll and kqueue because the author thinks they're broken,
	// so let's try to force it.
	loop = ev_default_loop(EVBACKEND_EPOLL);
	if (loop == NULL) {
		loop = ev_default_loop(EVBACKEND_KQUEUE);
	}
	if (loop == NULL) {
		loop = ev_default_loop(0);
	}
	if (loop == NULL) {
		throw RuntimeException("Cannot create an event loop");
	} else {
		return loop;
	}
}

static void
lowerPrivilege(const string &username, const struct passwd *user, const struct group *group) {
	int e;
	
	if (initgroups(username.c_str(), group->gr_gid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to set supplementary groups for " <<
			"PassengerLoggingAgent: " << strerror(e) << " (" << e << ")");
	}
	if (setgid(group->gr_gid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
			"privilege to that of user '" << username <<
			"': cannot set group ID to " << group->gr_gid <<
			": " << strerror(e) <<
			" (" << e << ")");
	}
	if (setuid(user->pw_uid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
			"privilege to that of user '" << username <<
			"': cannot set user ID: " << strerror(e) <<
			" (" << e << ")");
	}
}

void
feedbackFdBecameReadable(ev::io &watcher, int revents) {
	/* This event indicates that the watchdog has been killed.
	 * In this case we'll kill all descendant
	 * processes and exit. There's no point in keeping this agent
	 * running because we can't detect when the web server exits,
	 * and because this agent doesn't own the server instance
	 * directory. As soon as passenger-status is run, the server
	 * instance directory will be cleaned up, making this agent's
	 * services inaccessible.
	 */
	syscalls::killpg(getpgrp(), SIGKILL);
	_exit(2); // In case killpg() fails.
}

void
caughtExitSignal(ev::sig &watcher, int revents) {
	P_DEBUG("Caught signal, exiting...");
	ev_unloop(eventLoop, EVUNLOOP_ONE);
}

void
printInfo(ev::sig &watcher, int revents) {
	loggingServer->dump(cout);
	cout.flush();
}

static string
myself() {
	struct passwd *entry = getpwuid(geteuid());
	if (entry != NULL) {
		return entry->pw_name;
	} else {
		throw NonExistentUserException(string("The current user, UID ") +
			toString(geteuid()) + ", doesn't have a corresponding " +
			"entry in the system's user database. Please fix your " +
			"system's user database first.");
	}
}

int
main(int argc, char *argv[]) {
	VariantMap options        = initializeAgent(argc, argv, "PassengerLoggingAgent");
	string socketAddress      = options.get("logging_agent_address");
	string loggingDir         = options.get("analytics_log_dir");
	string password           = options.get("logging_agent_password");
	string username           = options.get("analytics_log_user",
		false, myself());
	string groupname          = options.get("analytics_log_group", false);
	string permissions        = options.get("analytics_log_permissions",
		false, DEFAULT_ANALYTICS_LOG_PERMISSIONS);
	string unionStationServiceAddress = options.get("union_station_service_address",
		false, DEFAULT_UNION_STATION_SERVICE_ADDRESS);
	int    unionStationServicePort = options.getInt("union_station_service_port",
		false, DEFAULT_UNION_STATION_SERVICE_PORT);
	string unionStationServiceCert = options.get("union_station_service_cert", false);
	
	curl_global_init(CURL_GLOBAL_ALL);
	
	try {
		/********** Now begins the real initialization **********/
		
		/* Create all the necessary objects and sockets... */
		AccountsDatabasePtr  accountsDatabase;
		FileDescriptor       serverSocketFd;
		struct passwd       *user;
		struct group        *group;
		int                  ret;
		
		eventLoop = createEventLoop();
		accountsDatabase = ptr(new AccountsDatabase());
		serverSocketFd = createServer(socketAddress.c_str());
		if (getSocketAddressType(socketAddress) == SAT_UNIX) {
			do {
				ret = chmod(parseUnixSocketAddress(socketAddress).c_str(),
					S_ISVTX |
					S_IRUSR | S_IWUSR | S_IXUSR |
					S_IRGRP | S_IWGRP | S_IXGRP |
					S_IROTH | S_IWOTH | S_IXOTH);
			} while (ret == -1 && errno == EINTR);
		}
		
		/* Sanity check user accounts. */
		
		user = getpwnam(username.c_str());
		if (user == NULL) {
			throw NonExistentUserException(string("The configuration option ") +
				"'PassengerAnalyticsLogUser' (Apache) or " +
				"'passenger_analytics_log_user' (Nginx) was set to '" +
				username + "', but this user doesn't exist. Please fix " +
				"the configuration option.");
		}
		
		if (groupname.empty()) {
			group = getgrgid(user->pw_gid);
			if (group == NULL) {
				throw NonExistentGroupException(string("The configuration option ") +
					"'PassengerAnalyticsLogGroup' (Apache) or " +
					"'passenger_analytics_log_group' (Nginx) wasn't set, " +
					"so PassengerLoggingAgent tried to use the default group " +
					"for user '" + username + "' - which is GID #" +
					toString(user->pw_gid) + " - as the group for the analytics " +
					"log dir, but this GID doesn't exist. " +
					"You can solve this problem by explicitly " +
					"setting PassengerAnalyticsLogGroup (Apache) or " +
					"passenger_analytics_log_group (Nginx) to a group that " +
					"does exist. In any case, it looks like your system's user " +
					"database is broken; Phusion Passenger can work fine even " +
					"with this broken user database, but you should still fix it.");
			} else {
				groupname = group->gr_name;
			}
		} else {
			group = getgrnam(groupname.c_str());
			if (group == NULL) {
				throw NonExistentGroupException(string("The configuration option ") +
					"'PassengerAnalyticsLogGroup' (Apache) or " +
					"'passenger_analytics_log_group' (Nginx) was set to '" +
					groupname + "', but this group doesn't exist. Please fix " +
					"the configuration option.");
			}
		}
		
		/* Create the logging directory if necessary. */
		if (getFileType(loggingDir) == FT_NONEXISTANT) {
			if (geteuid() == 0) {
				makeDirTree(loggingDir, permissions, user->pw_uid, group->gr_gid);
			} else {
				makeDirTree(loggingDir, permissions);
			}
		}
		
		/* Now's a good time to lower the privilege. */
		if (geteuid() == 0) {
			lowerPrivilege(username, user, group);
		}
		
		/* Now setup the actual logging server. */
		accountsDatabase->add("logging", password, false);
		LoggingServer server(eventLoop, serverSocketFd,
			accountsDatabase, loggingDir,
			"u=rwx,g=rx,o=rx", GROUP_NOT_GIVEN,
			unionStationServiceAddress,
			unionStationServicePort,
			unionStationServiceCert);
		loggingServer = &server;
		
		
		ev::io feedbackFdWatcher(eventLoop);
		ev::sig sigintWatcher(eventLoop);
		ev::sig sigtermWatcher(eventLoop);
		ev::sig siginfoWatcher(eventLoop);
		
		if (feedbackFdAvailable()) {
			MessageChannel feedbackChannel(FEEDBACK_FD);
			feedbackFdWatcher.set<&feedbackFdBecameReadable>();
			feedbackFdWatcher.start(FEEDBACK_FD, ev::READ);
			feedbackChannel.write("initialized", NULL);
		}
		sigintWatcher.set<&caughtExitSignal>();
		sigintWatcher.start(SIGINT);
		sigtermWatcher.set<&caughtExitSignal>();
		sigtermWatcher.start(SIGTERM);
		siginfoWatcher.set<&printInfo>();
		siginfoWatcher.start(SIGINFO);
		
		
		/********** Initialized! Enter main loop... **********/
		
		ev_loop(eventLoop, 0);
		return 0;
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}
}
