/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2012-2016 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  See LICENSE file for license information.
 */

#ifndef _PASSENGER_LICENSE_H_
#define _PASSENGER_LICENSE_H_

#ifdef __cplusplus
namespace Passenger {
#endif

#define APPEAL_MESSAGE \
	"If you have a valid license, then you probably did not copy the license key properly. " \
	"The license key must be an exact byte-by-byte copy of the one downloaded from the Customer Area. " \
	"It is already considered corrupted if you add a newline to it, which can happen if you e.g. copy-paste the contents using a text editor. " \
	"Please use a binary copying tool to copy the license key to your server, e.g. 'scp' or FTP in binary mode. " \
	"If the problem persists, please contact support@phusion.nl.\n" \
	"If you do not have a valid license, then we kindly ask you not to try to crack this software or to obtain it illegally. " \
	"We're trying to make a living out of this and the profits are used to fund the development of the " \
	"open source version of Phusion Passenger. We can only do this with your support. If you don't have " \
	"a valid license, please buy it from www.phusionpassenger.com.\n" \
	"Thank you for your support.\n" \
	"- Hongli, Ninh, and the rest of the Phusion team"

#define EXPIRED_APPEAL_MESSAGE \
	"The license is considered invalid if the \"Expires after\" date in your license file is in the past. " \
	"Please contact support@phusion.nl if you believe the license should be valid, or to obtain a new one.\n" \
	"We kindly ask you not to try to crack this software, obtain it illegally, or violate the validity period. " \
	"We're trying to make a living out of this and the profits are used to fund the development of the " \
	"open source version of Phusion Passenger. We can only do this with your support. If you don't have " \
	"a valid license, please buy it from www.phusionpassenger.com.\n" \
	"Thank you for your support.\n" \
	"- Hongli, Ninh, and the rest of the Phusion team"

extern char *licenseKey;

void passenger_enterprise_license_init();
char *passenger_enterprise_license_check();
int passenger_enterprise_should_track_usage();

// exposed for test only
const char *findExpiresAfterDate(const char *licenseKey);
int compareDates(const char *expiresAfterChars, struct tm checkDate);

#ifdef __cplusplus
} // namespace Passenger
#endif

#endif /* _PASSENGER_LICENSE_H_ */