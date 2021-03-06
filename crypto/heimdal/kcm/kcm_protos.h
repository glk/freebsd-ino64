/* This is a generated file */
#ifndef __kcm_protos_h__
#define __kcm_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

krb5_error_code
kcm_access (
	krb5_context /*context*/,
	kcm_client */*client*/,
	kcm_operation /*opcode*/,
	kcm_ccache /*ccache*/);

krb5_error_code
kcm_ccache_acquire (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_creds **/*credp*/);

krb5_error_code
kcm_ccache_destroy (
	krb5_context /*context*/,
	const char */*name*/);

krb5_error_code
kcm_ccache_destroy_client (
	krb5_context /*context*/,
	kcm_client */*client*/,
	const char */*name*/);

krb5_error_code
kcm_ccache_destroy_if_empty (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/);

krb5_error_code
kcm_ccache_enqueue_default (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_creds */*newcred*/);

krb5_error_code
kcm_ccache_gen_new (
	krb5_context /*context*/,
	pid_t /*pid*/,
	uid_t /*uid*/,
	gid_t /*gid*/,
	kcm_ccache */*ccache*/);

krb5_error_code
kcm_ccache_new (
	krb5_context /*context*/,
	const char */*name*/,
	kcm_ccache */*ccache*/);

krb5_error_code
kcm_ccache_new_client (
	krb5_context /*context*/,
	kcm_client */*client*/,
	const char */*name*/,
	kcm_ccache */*ccache_p*/);

char *kcm_ccache_nextid (
	pid_t /*pid*/,
	uid_t /*uid*/,
	gid_t /*gid*/);

krb5_error_code
kcm_ccache_refresh (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_creds **/*credp*/);

krb5_error_code
kcm_ccache_remove_cred (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_flags /*whichfields*/,
	const krb5_creds */*mcreds*/);

krb5_error_code
kcm_ccache_remove_cred_internal (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_flags /*whichfields*/,
	const krb5_creds */*mcreds*/);

krb5_error_code
kcm_ccache_remove_creds (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/);

krb5_error_code
kcm_ccache_remove_creds_internal (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/);

krb5_error_code
kcm_ccache_resolve (
	krb5_context /*context*/,
	const char */*name*/,
	kcm_ccache */*ccache*/);

krb5_error_code
kcm_ccache_resolve_client (
	krb5_context /*context*/,
	kcm_client */*client*/,
	kcm_operation /*opcode*/,
	const char */*name*/,
	kcm_ccache */*ccache*/);

krb5_error_code
kcm_ccache_retrieve_cred (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_flags /*whichfields*/,
	const krb5_creds */*mcreds*/,
	krb5_creds **/*credp*/);

krb5_error_code
kcm_ccache_retrieve_cred_internal (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_flags /*whichfields*/,
	const krb5_creds */*mcreds*/,
	krb5_creds **/*creds*/);

krb5_error_code
kcm_ccache_store_cred (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_creds */*creds*/,
	int /*copy*/);

krb5_error_code
kcm_ccache_store_cred_internal (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/,
	krb5_creds */*creds*/,
	int /*copy*/,
	krb5_creds **/*credp*/);

krb5_error_code
kcm_chmod (
	krb5_context /*context*/,
	kcm_client */*client*/,
	kcm_ccache /*ccache*/,
	uint16_t /*mode*/);

krb5_error_code
kcm_chown (
	krb5_context /*context*/,
	kcm_client */*client*/,
	kcm_ccache /*ccache*/,
	uid_t /*uid*/,
	gid_t /*gid*/);

krb5_error_code
kcm_cleanup_events (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/);

void
kcm_configure (
	int /*argc*/,
	char **/*argv*/);

krb5_error_code
kcm_cursor_delete (
	krb5_context /*context*/,
	pid_t /*pid*/,
	kcm_ccache /*ccache*/,
	uint32_t /*key*/);

krb5_error_code
kcm_cursor_find (
	krb5_context /*context*/,
	pid_t /*pid*/,
	kcm_ccache /*ccache*/,
	uint32_t /*key*/,
	kcm_cursor **/*cursor*/);

krb5_error_code
kcm_cursor_new (
	krb5_context /*context*/,
	pid_t /*pid*/,
	kcm_ccache /*ccache*/,
	uint32_t */*cursor*/);

krb5_error_code
kcm_debug_ccache (krb5_context /*context*/);

krb5_error_code
kcm_debug_events (krb5_context /*context*/);

krb5_error_code
kcm_dispatch (
	krb5_context /*context*/,
	kcm_client */*client*/,
	krb5_data */*req_data*/,
	krb5_data */*resp_data*/);

krb5_error_code
kcm_enqueue_event (
	krb5_context /*context*/,
	kcm_event */*event*/);

krb5_error_code
kcm_enqueue_event_internal (
	krb5_context /*context*/,
	kcm_event */*event*/);

krb5_error_code
kcm_enqueue_event_relative (
	krb5_context /*context*/,
	kcm_event */*event*/);

krb5_error_code
kcm_internal_ccache (
	krb5_context /*context*/,
	kcm_ccache /*c*/,
	krb5_ccache /*id*/);

void
kcm_log (
	int /*level*/,
	const char */*fmt*/,
	...);

char*
kcm_log_msg (
	int /*level*/,
	const char */*fmt*/,
	...);

char*
kcm_log_msg_va (
	int /*level*/,
	const char */*fmt*/,
	va_list /*ap*/);

void
kcm_loop (void);

const char *kcm_op2string (kcm_operation /*opcode*/);

void
kcm_openlog (void);

krb5_error_code
kcm_release_ccache (
	krb5_context /*context*/,
	kcm_ccache */*ccache*/);

krb5_error_code
kcm_remove_event (
	krb5_context /*context*/,
	kcm_event */*event*/);

krb5_error_code
kcm_retain_ccache (
	krb5_context /*context*/,
	kcm_ccache /*ccache*/);

krb5_error_code
kcm_run_events (
	krb5_context /*context*/,
	time_t /*now*/);

krb5_error_code
kcm_zero_ccache_data (
	krb5_context /*context*/,
	kcm_ccache /*cache*/);

krb5_error_code
kcm_zero_ccache_data_internal (
	krb5_context /*context*/,
	kcm_ccache_data */*cache*/);

#ifdef __cplusplus
}
#endif

#endif /* __kcm_protos_h__ */
