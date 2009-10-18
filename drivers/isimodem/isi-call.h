/*
 * This file is part of oFono - Open Source Telephony
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: <Pekka.Pessi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __GISI_CALL_H
#define __GISI_CALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PN_CALL			0x01

enum isi_call_message_id {
	CALL_CREATE_REQ =                                       0x01,
	CALL_CREATE_RESP =                                      0x02,
	CALL_COMING_IND =                                       0x03,
	CALL_MO_ALERT_IND =                                     0x04,
	CALL_MT_ALERT_IND =                                     0x05,
	CALL_WAITING_IND =                                      0x06,
	CALL_ANSWER_REQ =                                       0x07,
	CALL_ANSWER_RESP =                                      0x08,
	CALL_RELEASE_REQ =                                      0x09,
	CALL_RELEASE_RESP =                                     0x0A,
	CALL_RELEASE_IND =                                      0x0B,
	CALL_TERMINATED_IND =                                   0x0C,
	CALL_STATUS_REQ =                                       0x0D,
	CALL_STATUS_RESP =                                      0x0E,
	CALL_STATUS_IND =                                       0x0F,
	CALL_SERVER_STATUS_IND =                                0x10,
	CALL_CONTROL_REQ =                                      0x11,
	CALL_CONTROL_RESP =                                     0x12,
	CALL_CONTROL_IND =                                      0x13,
	CALL_MODE_SWITCH_REQ =                                  0x14,
	CALL_MODE_SWITCH_RESP =                                 0x15,
	CALL_MODE_SWITCH_IND =                                  0x16,
	CALL_DTMF_SEND_REQ =                                    0x17,
	CALL_DTMF_SEND_RESP =                                   0x18,
	CALL_DTMF_STOP_REQ =                                    0x19,
	CALL_DTMF_STOP_RESP =                                   0x1A,
	CALL_DTMF_STATUS_IND =                                  0x1B,
	CALL_DTMF_TONE_IND =                                    0x1C,
	CALL_RECONNECT_IND =                                    0x1E,
	CALL_PROPERTY_GET_REQ =                                 0x1F,
	CALL_PROPERTY_GET_RESP =                                0x20,
	CALL_PROPERTY_SET_REQ =                                 0x21,
	CALL_PROPERTY_SET_RESP =                                0x22,
	CALL_PROPERTY_SET_IND =                                 0x23,
	CALL_EMERGENCY_NBR_CHECK_REQ =                          0x28,
	CALL_EMERGENCY_NBR_CHECK_RESP =                         0x29,
	CALL_EMERGENCY_NBR_GET_REQ =                            0x26,
	CALL_EMERGENCY_NBR_GET_RESP =                           0x27,
	CALL_EMERGENCY_NBR_MODIFY_REQ =                         0x24,
	CALL_EMERGENCY_NBR_MODIFY_RESP =                        0x25,
	CALL_GSM_NOTIFICATION_IND =                             0xA0,
	CALL_GSM_USER_TO_USER_REQ =                             0xA1,
	CALL_GSM_USER_TO_USER_RESP =                            0xA2,
	CALL_GSM_USER_TO_USER_IND =                             0xA3,
	CALL_GSM_BLACKLIST_CLEAR_REQ =                          0xA4,
	CALL_GSM_BLACKLIST_CLEAR_RESP =                         0xA5,
	CALL_GSM_BLACKLIST_TIMER_IND =                          0xA6,
	CALL_GSM_DATA_CH_INFO_IND =                             0xA7,
	CALL_GSM_CCP_GET_REQ =                                  0xAA,
	CALL_GSM_CCP_GET_RESP =                                 0xAB,
	CALL_GSM_CCP_CHECK_REQ =                                0xAC,
	CALL_GSM_CCP_CHECK_RESP =                               0xAD,
	CALL_GSM_COMING_REJ_IND =                               0xA9,
	CALL_GSM_RAB_IND =                                      0xA8,
	CALL_GSM_IMMEDIATE_MODIFY_IND =                         0xAE,
	CALL_CREATE_NO_SIMATK_REQ =                             0x2A,
	CALL_GSM_SS_DATA_IND =                                  0xAF,
	CALL_TIMER_REQ =                                        0x2B,
	CALL_TIMER_RESP =                                       0x2C,
	CALL_TIMER_NTF =                                        0x2D,
	CALL_TIMER_IND =                                        0x2E,
	CALL_TIMER_RESET_REQ =                                  0x2F,
	CALL_TIMER_RESET_RESP =                                 0x30,
	CALL_EMERGENCY_NBR_IND =                                0x31,
	CALL_SERVICE_DENIED_IND =                               0x32,
	CALL_RELEASE_END_REQ =                                  0x34,
	CALL_RELEASE_END_RESP =                                 0x35,
	CALL_USER_CONNECT_IND =                                 0x33,
	CALL_AUDIO_CONNECT_IND =                                0x40,
	CALL_KODIAK_ALLOW_CTRL_REQ =                            0x36,
	CALL_KODIAK_ALLOW_CTRL_RESP =                           0x37,
	CALL_SERVICE_ACTIVATE_IND =                             0x38,
	CALL_SERVICE_ACTIVATE_REQ =                             0x39,
	CALL_SERVICE_ACTIVATE_RESP =                            0x3A,
	CALL_SIM_ATK_IND =                                      0x3B,
	CALL_CONTROL_OPER_IND =                                 0x3C,
	CALL_TEST_CALL_STATUS_IND =                             0x3E,
	CALL_SIM_ATK_INFO_IND =                                 0x3F,
	CALL_SECURITY_IND =                                     0x41,
	CALL_MEDIA_HANDLE_REQ =                                 0x42,
	CALL_MEDIA_HANDLE_RESP =                                0x43,
	COMMON_MESSAGE =                                        0xF0,
};

enum isi_call_status {
	CALL_STATUS_IDLE =                                      0x00,
	CALL_STATUS_CREATE =                                    0x01,
	CALL_STATUS_COMING =                                    0x02,
	CALL_STATUS_PROCEEDING =                                0x03,
	CALL_STATUS_MO_ALERTING =                               0x04,
	CALL_STATUS_MT_ALERTING =                               0x05,
	CALL_STATUS_WAITING =                                   0x06,
	CALL_STATUS_ANSWERED =                                  0x07,
	CALL_STATUS_ACTIVE =                                    0x08,
	CALL_STATUS_MO_RELEASE =                                0x09,
	CALL_STATUS_MT_RELEASE =                                0x0A,
	CALL_STATUS_HOLD_INITIATED =                            0x0B,
	CALL_STATUS_HOLD =                                      0x0C,
	CALL_STATUS_RETRIEVE_INITIATED =                        0x0D,
	CALL_STATUS_RECONNECT_PENDING =                         0x0E,
	CALL_STATUS_TERMINATED =                                0x0F,
	CALL_STATUS_SWAP_INITIATED =                            0x10,
};

enum isi_call_isi_cause {
	CALL_CAUSE_NO_CAUSE =                                   0x00,
	CALL_CAUSE_NO_CALL =                                    0x01,
	CALL_CAUSE_TIMEOUT =                                    0x02,
	CALL_CAUSE_RELEASE_BY_USER =                            0x03,
	CALL_CAUSE_BUSY_USER_REQUEST =                          0x04,
	CALL_CAUSE_ERROR_REQUEST =                              0x05,
	CALL_CAUSE_COST_LIMIT_REACHED =                         0x06,
	CALL_CAUSE_CALL_ACTIVE =                                0x07,
	CALL_CAUSE_NO_CALL_ACTIVE =                             0x08,
	CALL_CAUSE_INVALID_CALL_MODE =                          0x09,
	CALL_CAUSE_SIGNALLING_FAILURE =                         0x0A,
	CALL_CAUSE_TOO_LONG_ADDRESS =                           0x0B,
	CALL_CAUSE_INVALID_ADDRESS =                            0x0C,
	CALL_CAUSE_EMERGENCY =                                  0x0D,
	CALL_CAUSE_NO_TRAFFIC_CHANNEL =                         0x0E,
	CALL_CAUSE_NO_COVERAGE =                                0x0F,
	CALL_CAUSE_CODE_REQUIRED =                              0x10,
	CALL_CAUSE_NOT_ALLOWED =                                0x11,
	CALL_CAUSE_NO_DTMF =                                    0x12,
	CALL_CAUSE_CHANNEL_LOSS =                               0x13,
	CALL_CAUSE_FDN_NOT_OK =                                 0x14,
	CALL_CAUSE_USER_TERMINATED =                            0x15,
	CALL_CAUSE_BLACKLIST_BLOCKED =                          0x16,
	CALL_CAUSE_BLACKLIST_DELAYED =                          0x17,
	CALL_CAUSE_NUMBER_NOT_FOUND =                           0x18,
	CALL_CAUSE_NUMBER_CANNOT_REMOVE =                       0x19,
	CALL_CAUSE_EMERGENCY_FAILURE =                          0x1A,
	CALL_CAUSE_CS_SUSPENDED =                               0x1B,
	CALL_CAUSE_DCM_DRIVE_MODE =                             0x1C,
	CALL_CAUSE_MULTIMEDIA_NOT_ALLOWED =                     0x1D,
	CALL_CAUSE_SIM_REJECTED =                               0x1E,
	CALL_CAUSE_NO_SIM =                                     0x1F,
	CALL_CAUSE_SIM_LOCK_OPERATIVE =                         0x20,
	CALL_CAUSE_SIMATKCC_REJECTED =                          0x21,
	CALL_CAUSE_SIMATKCC_MODIFIED =                          0x22,
	CALL_CAUSE_DTMF_INVALID_DIGIT =                         0x23,
	CALL_CAUSE_DTMF_SEND_ONGOING =                          0x24,
	CALL_CAUSE_CS_INACTIVE =                                0x25,
	CALL_CAUSE_SECURITY_MODE =                              0x26,
	CALL_CAUSE_TRACFONE_FAILED =                            0x27,
	CALL_CAUSE_TRACFONE_WAIT_FAILED =                       0x28,
	CALL_CAUSE_TRACFONE_CONF_FAILED =                       0x29,
	CALL_CAUSE_TEMPERATURE_LIMIT =                          0x2A,
	CALL_CAUSE_KODIAK_POC_FAILED =                          0x2B,
	CALL_CAUSE_NOT_REGISTERED =                             0x2C,
	CALL_CAUSE_CS_CALLS_ONLY =                              0x2D,
	CALL_CAUSE_VOIP_CALLS_ONLY =                            0x2E,
	CALL_CAUSE_LIMITED_CALL_ACTIVE =                        0x2F,
	CALL_CAUSE_LIMITED_CALL_NOT_ALLOWED =                   0x30,
	CALL_CAUSE_SECURE_CALL_NOT_POSSIBLE =                   0x31,
	CALL_CAUSE_INTERCEPT =                                  0x32,
};

enum isi_call_gsm_cause {
	CALL_GSM_CAUSE_UNASSIGNED_NUMBER =                      0x01,
	CALL_GSM_CAUSE_NO_ROUTE =                               0x03,
	CALL_GSM_CAUSE_CH_UNACCEPTABLE =                        0x06,
	CALL_GSM_CAUSE_OPER_BARRING =                           0x08,
	CALL_GSM_CAUSE_NORMAL =                                 0x10,
	CALL_GSM_CAUSE_USER_BUSY =                              0x11,
	CALL_GSM_CAUSE_NO_USER_RESPONSE =                       0x12,
	CALL_GSM_CAUSE_ALERT_NO_ANSWER =                        0x13,
	CALL_GSM_CAUSE_CALL_REJECTED =                          0x15,
	CALL_GSM_CAUSE_NUMBER_CHANGED =                         0x16,
	CALL_GSM_CAUSE_NON_SELECT_CLEAR =                       0x1A,
	CALL_GSM_CAUSE_DEST_OUT_OF_ORDER =                      0x1B,
	CALL_GSM_CAUSE_INVALID_NUMBER =                         0x1C,
	CALL_GSM_CAUSE_FACILITY_REJECTED =                      0x1D,
	CALL_GSM_CAUSE_RESP_TO_STATUS =                         0x1E,
	CALL_GSM_CAUSE_NORMAL_UNSPECIFIED =                     0x1F,
	CALL_GSM_CAUSE_NO_CHANNEL =                             0x22,
	CALL_GSM_CAUSE_NETW_OUT_OF_ORDER =                      0x26,
	CALL_GSM_CAUSE_TEMPORARY_FAILURE =                      0x29,
	CALL_GSM_CAUSE_CONGESTION =                             0x2A,
	CALL_GSM_CAUSE_ACCESS_INFO_DISC =                       0x2B,
	CALL_GSM_CAUSE_CHANNEL_NA =                             0x2C,
	CALL_GSM_CAUSE_RESOURCES_NA =                           0x2F,
	CALL_GSM_CAUSE_QOS_NA =                                 0x31,
	CALL_GSM_CAUSE_FACILITY_UNSUBS =                        0x32,
	CALL_GSM_CAUSE_COMING_BARRED_CUG =                      0x37,
	CALL_GSM_CAUSE_BC_UNAUTHORIZED =                        0x39,
	CALL_GSM_CAUSE_BC_NA =                                  0x3A,
	CALL_GSM_CAUSE_SERVICE_NA =                             0x3F,
	CALL_GSM_CAUSE_BEARER_NOT_IMPL =                        0x41,
	CALL_GSM_CAUSE_ACM_MAX =                                0x44,
	CALL_GSM_CAUSE_FACILITY_NOT_IMPL =                      0x45,
	CALL_GSM_CAUSE_ONLY_RDI_BC =                            0x46,
	CALL_GSM_CAUSE_SERVICE_NOT_IMPL =                       0x4F,
	CALL_GSM_CAUSE_INVALID_TI =                             0x51,
	CALL_GSM_CAUSE_NOT_IN_CUG =                             0x57,
	CALL_GSM_CAUSE_INCOMPATIBLE_DEST =                      0x58,
	CALL_GSM_CAUSE_INV_TRANS_NET_SEL =                      0x5B,
	CALL_GSM_CAUSE_SEMANTICAL_ERR =                         0x5F,
	CALL_GSM_CAUSE_INVALID_MANDATORY =                      0x60,
	CALL_GSM_CAUSE_MSG_TYPE_INEXIST =                       0x61,
	CALL_GSM_CAUSE_MSG_TYPE_INCOMPAT =                      0x62,
	CALL_GSM_CAUSE_IE_NON_EXISTENT =                        0x63,
	CALL_GSM_CAUSE_COND_IE_ERROR =                          0x64,
	CALL_GSM_CAUSE_MSG_INCOMPATIBLE =                       0x65,
	CALL_GSM_CAUSE_TIMER_EXPIRY =                           0x66,
	CALL_GSM_CAUSE_PROTOCOL_ERROR =                         0x6F,
	CALL_GSM_CAUSE_INTERWORKING =                           0x7F,
};

enum isi_call_cause_type {
	CALL_CAUSE_TYPE_DEFAULT =                               0x00,
	CALL_CAUSE_TYPE_CLIENT =                                0x01,
	CALL_CAUSE_TYPE_SERVER =                                0x02,
	CALL_CAUSE_TYPE_NETWORK =                               0x03,
};

enum isi_call_subblock {
	CALL_ORIGIN_ADDRESS =                                   0x01,
	CALL_ORIGIN_SUBADDRESS =                                0x02,
	CALL_DESTINATION_ADDRESS =                              0x03,
	CALL_DESTINATION_SUBADDRESS =                           0x04,
	CALL_DESTINATION_PRE_ADDRESS =                          0x05,
	CALL_DESTINATION_POST_ADDRESS =                         0x06,
	CALL_MODE =                                             0x07,
	CALL_CAUSE =                                            0x08,
	CALL_OPERATION =                                        0x09,
	CALL_STATUS =                                           0x0A,
	CALL_STATUS_INFO =                                      0x0B,
	CALL_ALERTING_INFO =                                    0x0C,
	CALL_RELEASE_INFO =                                     0x0D,
	CALL_ORIGIN_INFO =                                      0x0E,
	CALL_DTMF_DIGIT =                                       0x0F,
	CALL_DTMF_STRING =                                      0x10,
	CALL_DTMF_BCD_STRING =                                  0x19,
	CALL_DTMF_INFO =                                        0x1A,
	CALL_PROPERTY_INFO =                                    0x13,
	CALL_EMERGENCY_NUMBER =                                 0x14,
	CALL_DTMF_STATUS =                                      0x11,
	CALL_DTMF_TONE =                                        0x12,
	CALL_GSM_CUG_INFO =                                     0xA0,
	CALL_GSM_ALERTING_PATTERN =                             0xA1,
	CALL_GSM_DEFLECTION_ADDRESS =                           0xA2,
	CALL_GSM_DEFLECTION_SUBADDRESS =                        0xA3,
	CALL_GSM_REDIRECTING_ADDRESS =                          0xA4,
	CALL_GSM_REDIRECTING_SUBADDRESS =                       0xA5,
	CALL_GSM_REMOTE_ADDRESS =                               0xA6,
	CALL_GSM_REMOTE_SUBADDRESS =                            0xA7,
	CALL_GSM_USER_TO_USER_INFO =                            0xA8,
	CALL_GSM_DIAGNOSTICS =                                  0xA9,
	CALL_GSM_SS_DIAGNOSTICS =                               0xAA,
	CALL_GSM_NEW_DESTINATION =                              0xAB,
	CALL_GSM_CCBS_INFO =                                    0xAC,
	CALL_GSM_ADDRESS_OF_B =                                 0xAD,
	CALL_GSM_SUBADDRESS_OF_B =                              0xB0,
	CALL_GSM_NOTIFY =                                       0xB1,
	CALL_GSM_SS_NOTIFY =                                    0xB2,
	CALL_GSM_SS_CODE =                                      0xB3,
	CALL_GSM_SS_STATUS =                                    0xB4,
	CALL_GSM_SS_NOTIFY_INDICATOR =                          0xB5,
	CALL_GSM_SS_HOLD_INDICATOR =                            0xB6,
	CALL_GSM_SS_ECT_INDICATOR =                             0xB7,
	CALL_GSM_DATA_CH_INFO =                                 0xB8,
	CALL_DESTINATION_CS_ADDRESS =                           0x16,
	CALL_GSM_CCP =                                          0xBA,
	CALL_GSM_RAB_INFO =                                     0xB9,
	CALL_GSM_FNUR_INFO =                                    0xBB,
	CALL_GSM_CAUSE_OF_NO_CLI =                              0xBC,
	CALL_GSM_MM_CAUSE =                                     0xBD,
	CALL_GSM_EVENT_INFO =                                   0xBE,
	CALL_GSM_DETAILED_CAUSE =                               0xBF,
	CALL_GSM_SS_DATA =                                      0xC0,
	CALL_TIMER =                                            0x17,
	CALL_GSM_ALS_INFO =                                     0xC1,
	CALL_STATE_AUTO_CHANGE =                                0x18,
	CALL_EMERGENCY_NUMBER_INFO =                            0x1B,
	CALL_STATUS_MODE =                                      0x1C,
	CALL_ADDR_AND_STATUS_INFO =                             0x1D,
	CALL_DTMF_TIMERS =                                      0x1E,
	CALL_NAS_SYNC_INDICATOR =                               0x1F,
	CALL_NW_CAUSE =                                         0x20,
	CALL_TRACFONE_RESULT =                                  0x21,
	CALL_KODIAK_POC =                                       0x22,
	CALL_DISPLAY_NUMBER =                                   0x23,
	CALL_DESTINATION_URI =                                  0x24,
	CALL_ORIGIN_URI =                                       0x25,
	CALL_URI =                                              0x26,
	CALL_SYSTEM_INFO =                                      0x27,
	CALL_SYSTEMS =                                          0x28,
	CALL_VOIP_TIMER =                                       0x29,
	CALL_REDIRECTING_URI =                                  0x2A,
	CALL_REMOTE_URI =                                       0x2B,
	CALL_DEFLECTION_URI =                                   0x2C,
	CALL_TRANSFER_INFO =                                    0x2D,
	CALL_FORWARDING_INFO =                                  0x2E,
	CALL_ID_INFO =                                          0x2F,
	CALL_TEST_CALL =                                        0x30,
	CALL_AUDIO_CONF_INFO =                                  0x31,
	CALL_SECURITY_INFO =                                    0x33,
	CALL_SINGLE_TIMERS =                                    0x32,
	CALL_MEDIA_INFO =                                       0x35,
	CALL_MEDIA_HANDLE =                                     0x34,
	CALL_MODE_CHANGE_INFO =                                 0x36,
	CALL_ADDITIONAL_PARAMS =                                0x37,
	CALL_DSAC_INFO =                                        0x38,
};

enum isi_call_id {
	CALL_ID_NONE =                                          0x00,
	CALL_ID_1 =                                             0x01,
	CALL_ID_2 =                                             0x02,
	CALL_ID_3 =                                             0x03,
	CALL_ID_4 =                                             0x04,
	CALL_ID_5 =                                             0x05,
	CALL_ID_6 =                                             0x06,
	CALL_ID_7 =                                             0x07,
	CALL_ID_CONFERENCE =                                    0x10,
	CALL_ID_WAITING =                                       0x20,
	CALL_ID_HOLD =                                          0x40,
	CALL_ID_ACTIVE =                                        0x80,
	CALL_ID_ALL =                                           0xF0,
};

enum isi_call_mode {
	CALL_MODE_EMERGENCY =                                   0x00,
	CALL_MODE_SPEECH =                                      0x01,
	CALL_GSM_MODE_ALS_LINE_1 =                              0xA5,
	CALL_GSM_MODE_ALS_LINE_2 =                              0xA2,
};

enum {
	CALL_MODE_INFO_NONE = 0,
	CALL_MODE_ORIGINATOR = 0x01,
};

enum {
	CALL_PRESENTATION_ALLOWED =                             0x00,
	CALL_PRESENTATION_RESTRICTED =                          0x01,
	CALL_GSM_PRESENTATION_DEFAULT =                         0x07,
};

enum isi_call_operation {
	CALL_OP_HOLD =                                          0x01,
	CALL_OP_RETRIEVE =                                      0x02,
	CALL_OP_SWAP =                                          0x03,
	CALL_OP_CONFERENCE_BUILD =                              0x04,
	CALL_OP_CONFERENCE_SPLIT =                              0x05,
	CALL_OP_DATA_RATE_CHANGE =                              0x06,
	CALL_GSM_OP_CUG =                                       0xA0,
	CALL_GSM_OP_TRANSFER =                                  0xA1,
	CALL_GSM_OP_DEFLECT =                                   0xA2,
	CALL_GSM_OP_CCBS =                                      0xA3,
	CALL_GSM_OP_UUS1 =                                      0xA4,
	CALL_GSM_OP_UUS2 =                                      0xA5,
	CALL_GSM_OP_UUS3 =                                      0xA6,
};

enum {
	CALL_GSM_OP_UUS_REQUIRED =                              0x01,
};

enum call_status_mode {
	CALL_STATUS_MODE_DEFAULT =                              0x00,
	CALL_STATUS_MODE_ADDR =                                 0x01,
	CALL_STATUS_MODE_ADDR_AND_ORIGIN =                      0x02,
	CALL_STATUS_MODE_POC =                                  0x03,
	CALL_STATUS_MODE_VOIP_ADDR =                            0x04,
};

enum {
	CALL_DTMF_ENABLE_TONE_IND_SEND =                        0x01,
	CALL_DTMF_DISABLE_TONE_IND_SEND =                       0x02,
};

char const *isi_call_cause_name(uint8_t cause_type, uint8_t cause);
char const *isi_call_gsm_cause_name(enum isi_call_gsm_cause value);
char const *isi_call_isi_cause_name(enum isi_call_isi_cause value);
char const *isi_call_status_name(enum isi_call_status value);
char const *isi_call_message_id_name(enum isi_call_message_id value);

void isi_call_debug(const void *restrict buf, size_t len, void *data);

#ifdef __cplusplus
};
#endif

#endif
