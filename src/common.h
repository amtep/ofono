/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* 27.007 Section 7.3 <stat> */
enum operator_status {
	OPERATOR_STATUS_UNKNOWN = 0,
	OPERATOR_STATUS_AVAILABLE = 1,
	OPERATOR_STATUS_CURRENT = 2,
	OPERATOR_STATUS_FORBIDDEN = 3
};

/* 27.007 Section 7.3 <AcT> */
enum access_technology {
	ACCESS_TECHNOLOGY_GSM = 0,
	ACCESS_TECHNOLOGY_GSM_COMPACT = 1,
	ACCESS_TECHNOLOGY_UTRAN = 2,
	ACCESS_TECHNOLOGY_GSM_EGPRS = 3,
	ACCESS_TECHNOLOGY_UTRAN_HSDPA = 4,
	ACCESS_TECHNOLOGY_UTRAN_HSUPA = 5,
	ACCESS_TECHNOLOGY_UTRAN_HSDPA_HSUPA = 6,
	ACCESS_TECHNOLOGY_EUTRAN = 7
};

/* 27.007 Section 7.2 <stat> */
enum network_registration_status {
	NETWORK_REGISTRATION_STATUS_NOT_REGISTERED = 0,
	NETWORK_REGISTRATION_STATUS_REGISTERED = 1,
	NETWORK_REGISTRATION_STATUS_SEARCHING = 2,
	NETWORK_REGISTRATION_STATUS_DENIED = 3,
	NETWORK_REGISTRATION_STATUS_UNKNOWN = 4,
	NETWORK_REGISTRATION_STATUS_ROAMING = 5
};

/* 27.007 Section 7.7 */
enum clir_status {
	CLIR_STATUS_NOT_PROVISIONED = 0,
	CLIR_STATUS_PROVISIONED_PERMANENT,
	CLIR_STATUS_UNKNOWN,
	CLIR_STATUS_TEMPORARY_RESTRICTED,
	CLIR_STATUS_TEMPORARY_ALLOWED
};

/* 27.007 Section 7.6 */
enum clip_status {
	CLIP_STATUS_NOT_PROVISIONED = 0,
	CLIP_STATUS_PROVISIONED,
	CLIP_STATUS_UNKNOWN
};

/* 27.007 Section 7.6 */
enum clip_validity {
	CLIP_VALIDITY_VALID = 0,
	CLIP_VALIDITY_WITHHELD = 1,
	CLIP_VALIDITY_NOT_AVAILABLE = 2
};

/* 27.007 Section 7.8 */
enum colp_status {
	COLP_STATUS_NOT_PROVISIONED = 0,
	COLP_STATUS_PROVISIONED = 1,
	COLP_STATUS_UNKNOWN = 2
};

/* This is not defined in 27.007, but presumably the same as CLIP/COLP */
enum colr_status {
	COLR_STATUS_NOT_PROVISIONED = 0,
	COLR_STATUS_PROVISIONED = 1,
	COLR_STATUS_UNKNOWN = 2
};

/* 27.007 Section 7.18 */
enum call_status {
	CALL_STATUS_ACTIVE = 0,
	CALL_STATUS_HELD = 1,
	CALL_STATUS_DIALING = 2,
	CALL_STATUS_ALERTING = 3,
	CALL_STATUS_INCOMING = 4,
	CALL_STATUS_WAITING = 5,
	CALL_STATUS_DISCONNECTED
};

/* 27.007 Section 7.18 */
enum call_direction {
	CALL_DIRECTION_MOBILE_ORIGINATED = 0,
	CALL_DIRECTION_MOBILE_TERMINATED = 1
};

/* 27.007 Section 7.11 */
enum bearer_class {
	BEARER_CLASS_VOICE = 1,
	BEARER_CLASS_DATA = 2,
	BEARER_CLASS_FAX = 4,
	BEARER_CLASS_DEFAULT = 7,
	BEARER_CLASS_SMS = 8,
	BEARER_CLASS_DATA_SYNC = 16,
	BEARER_CLASS_DATA_ASYNC = 32,
	/* According to 22.030, types 1-12 */
	BEARER_CLASS_SS_DEFAULT = 61,
	BEARER_CLASS_PACKET = 64,
	BEARER_CLASS_PAD = 128
};

enum own_number_service_type {
	OWN_NUMBER_SERVICE_TYPE_ASYNC = 0,
	OWN_NUMBER_SERVICE_TYPE_SYNC = 1,
	OWN_NUMBER_SERVICE_TYPE_PAD = 2,
	OWN_NUMBER_SERVICE_TYPE_PACKET = 3,
	OWN_NUMBER_SERVICE_TYPE_VOICE = 4,
	OWN_NUMBER_SERVICE_TYPE_FAX = 5
};

enum call_forwarding_type {
	CALL_FORWARDING_TYPE_UNCONDITIONAL = 0,
	CALL_FORWARDING_TYPE_BUSY = 1,
	CALL_FORWARDING_TYPE_NO_REPLY = 2,
	CALL_FORWARDING_TYPE_NOT_REACHABLE = 3,
	CALL_FORWARDING_TYPE_ALL = 4,
	CALL_FORWARDING_TYPE_ALL_CONDITIONAL = 5
};

enum ussd_status {
	USSD_STATUS_NOTIFY = 0,
	USSD_STATUS_ACTION_REQUIRED = 1,
	USSD_STATUS_TERMINATED = 2,
	USSD_STATUS_LOCAL_CLIENT_RESPONDED = 3,
	USSD_STATUS_NOT_SUPPORTED = 4,
	USSD_STATUS_TIMED_OUT = 5,
};

/* 22.030 Section 6.5.2 */
enum ss_control_type {
	SS_CONTROL_TYPE_ACTIVATION,
	SS_CONTROL_TYPE_DEACTIVATION,
	SS_CONTROL_TYPE_QUERY,
	SS_CONTROL_TYPE_REGISTRATION,
	SS_CONTROL_TYPE_ERASURE,
};

/* TS 27.007 Supplementary service notifications +CSSN */
enum ss_cssi {
	SS_MO_UNCONDITIONAL_FORWARDING	= 0,
	SS_MO_CONDITIONAL_FORWARDING	= 1,
	SS_MO_CALL_FORWARDED		= 2,
	SS_MO_CALL_WAITING		= 3,
	SS_MO_CUG_CALL			= 4,
	SS_MO_OUTGOING_BARRING		= 5,
	SS_MO_INCOMING_BARRING		= 6,
	SS_MO_CLIR_SUPPRESSION_REJECTED	= 7,
	SS_MO_CALL_DEFLECTED		= 8,
};

enum ss_cssu {
	SS_MT_CALL_FORWARDED		= 0,
	SS_MT_CUG_CALL			= 1,
	SS_MT_VOICECALL_ON_HOLD		= 2,
	SS_MT_VOICECALL_RETRIEVED	= 3,
	SS_MT_MULTIPARTY_VOICECALL	= 4,
	SS_MT_VOICECALL_HOLD_RELEASED	= 5,
	SS_MT_FORWARD_CHECK_SS_MESSAGE	= 6,
	SS_MT_VOICECALL_IN_TRANSFER	= 7,
	SS_MT_VOICECALL_TRANSFERRED	= 8,
	SS_MT_CALL_DEFLECTED		= 9,
};

const char *telephony_error_to_str(const struct ofono_error *error);

gboolean valid_phone_number_format(const char *number);
const char *phone_number_to_string(const struct ofono_phone_number *ph);
void string_to_phone_number(const char *str, struct ofono_phone_number *ph);

int mmi_service_code_to_bearer_class(int code);

gboolean valid_ussd_string(const char *str);

gboolean parse_ss_control_string(char *str, int *ss_type,
					char **sc, char **sia,
					char **sib, char **sic,
					char **sid, char **dn);

const char *ss_control_type_to_string(enum ss_control_type type);

const char *bearer_class_to_string(enum bearer_class cls);

gboolean is_valid_pin(const char *pin);
