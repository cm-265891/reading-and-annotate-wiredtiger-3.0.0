/*-
 * Copyright (c) 2014-2017 MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

#ifdef HAVE_TIMESTAMPS
/*
 * __wt_timestamp_to_hex_string --
 *	Convert a timestamp to hex string representation.
 */
int
__wt_timestamp_to_hex_string(
    WT_SESSION_IMPL *session, char *hex_timestamp, const wt_timestamp_t *ts_src)
{
	wt_timestamp_t ts;

	__wt_timestamp_set(&ts, ts_src);

	if (__wt_timestamp_iszero(&ts)) {
		hex_timestamp[0] = '0';
		hex_timestamp[1] = '\0';
		return (0);
	}

#if WT_TIMESTAMP_SIZE == 8
	{
	char *p, v;

	for (p = hex_timestamp; ts.val != 0; ts.val >>= 4)
		*p++ = (char)__wt_hex((u_char)(ts.val & 0x0f));
	*p = '\0';

	/* Reverse the string. */
	for (--p; p > hex_timestamp;) {
		v = *p;
		*p-- = *hex_timestamp;
		*hex_timestamp++ = v;
	}
	WT_UNUSED(session);
	}
#else
	{
	WT_ITEM hexts;
	size_t len;
	uint8_t *tsp;

	/* Avoid memory allocation: set up an item guaranteed large enough. */
	hexts.data = hexts.mem = hex_timestamp;
	hexts.memsize = 2 * WT_TIMESTAMP_SIZE + 1;
	/* Trim leading zeros. */
	for (tsp = ts.ts, len = WT_TIMESTAMP_SIZE;
	    len > 0 && *tsp == 0;
	    ++tsp, --len)
		;
	WT_RET(__wt_raw_to_hex(session, tsp, len, &hexts));
	}
#endif
	return (0);
}

/*
 * __wt_verbose_timestamp --
 *	Output a verbose message along with the specified timestamp
 */
void
__wt_verbose_timestamp(WT_SESSION_IMPL *session,
    const wt_timestamp_t *ts, const char *msg)
{
#ifdef HAVE_VERBOSE
	char timestamp_buf[2 * WT_TIMESTAMP_SIZE + 1];

	if (__wt_timestamp_to_hex_string(session, timestamp_buf, ts) != 0)
	       return;

	__wt_verbose(session,
	    WT_VERB_TIMESTAMP, "Timestamp %s : %s", timestamp_buf, msg);
#else
	WT_UNUSED(session);
	WT_UNUSED(ts);
	WT_UNUSED(msg);
#endif
}

/*
 * __wt_txn_parse_timestamp --
 *	Decodes and sets a timestamp.
 */ //对cval对应的时间撮字符串做检查，看是否符合要求
int
__wt_txn_parse_timestamp(WT_SESSION_IMPL *session,
     const char *name, wt_timestamp_t *timestamp, WT_CONFIG_ITEM *cval)
{
	__wt_timestamp_set_zero(timestamp);

	if (cval->len == 0)
		return (0);

	/* Protect against unexpectedly long hex strings. */
	if (cval->len > 2 * WT_TIMESTAMP_SIZE)
		WT_RET_MSG(session, EINVAL,
		    "%s timestamp too long '%.*s'",
		    name, (int)cval->len, cval->str);

#if WT_TIMESTAMP_SIZE == 8
	{
	static const int8_t hextable[] = {
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	     0,  1,   2,   3,   4,   5,   6,   7,
	     8,  9,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, 10,  11,  12,  13,  14,  15,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1,
	    -1, 10,  11,  12,  13,  14,  15,  -1
	};
	wt_timestamp_t ts;
	size_t len;
	int hex_val;
	const char *hex_itr;

	for (ts.val = 0, hex_itr = cval->str, len = cval->len; len > 0; --len) {
		if ((size_t)*hex_itr < WT_ELEMENTS(hextable))
			hex_val = hextable[(size_t)*hex_itr++];
		else
			hex_val = -1;
		if (hex_val < 0)
			WT_RET_MSG(session, EINVAL,
			    "Failed to parse %s timestamp '%.*s'",
			    name, (int)cval->len, cval->str);
		ts.val = (ts.val << 4) | (uint64_t)hex_val;
	}
	__wt_timestamp_set(timestamp, &ts);
	}
#else
	{
	WT_DECL_RET;
	WT_ITEM ts;
	wt_timestamp_t tsbuf;
	size_t hexlen;
	const char *hexts;
	char padbuf[2 * WT_TIMESTAMP_SIZE + 1];

	/*
	 * The decoding function assumes it is decoding data produced by dump
	 * and so requires an even number of hex digits.
	 */
	if ((cval->len & 1) == 0) {
		hexts = cval->str;
		hexlen = cval->len;
	} else {
		padbuf[0] = '0';
		memcpy(padbuf + 1, cval->str, cval->len);
		hexts = padbuf;
		hexlen = cval->len + 1;
	}

	/* Avoid memory allocation to decode timestamps. */
	ts.data = ts.mem = tsbuf.ts;
	ts.memsize = sizeof(tsbuf.ts);

	if ((ret = __wt_nhex_to_raw(session, hexts, hexlen, &ts)) != 0)
		WT_RET_MSG(session, ret, "Failed to parse %s timestamp '%.*s'",
		    name, (int)cval->len, cval->str);
	WT_ASSERT(session, ts.size <= WT_TIMESTAMP_SIZE);

	/* Copy the raw value to the end of the timestamp. */
	memcpy(timestamp->ts + WT_TIMESTAMP_SIZE - ts.size,
	    ts.data, ts.size);
	}
#endif
	if (__wt_timestamp_iszero(timestamp))
		WT_RET_MSG(session, EINVAL,
		    "Failed to parse %s timestamp '%.*s': zero not permitted",
		    name, (int)cval->len, cval->str);

	return (0);
}

/*
 * __txn_global_query_timestamp --
 *	Query a timestamp.
 */ //默认为read_timestamp和oldest_timestamp的较小值
static int
__txn_global_query_timestamp( 
    WT_SESSION_IMPL *session, wt_timestamp_t *tsp, const char *cfg[])
{
	WT_CONFIG_ITEM cval;
	WT_CONNECTION_IMPL *conn;
	WT_TXN *txn;
	WT_TXN_GLOBAL *txn_global;
	wt_timestamp_t ts;

	conn = S2C(session);
	txn_global = &conn->txn_global;

    //默认配置是"get=pinned"
	WT_RET(__wt_config_gets(session, cfg, "get", &cval));
	if (WT_STRING_MATCH("all_committed", cval.str, cval.len)) {
		if (!txn_global->has_commit_timestamp)
			return (WT_NOTFOUND);
		WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
		    __wt_timestamp_set(&ts, &txn_global->commit_timestamp));
		WT_ASSERT(session, !__wt_timestamp_iszero(&ts));

		/* Skip the lock if there are no running transactions. */
		if (TAILQ_EMPTY(&txn_global->commit_timestamph))
			goto done;

		/* Compare with the oldest running transaction. */
		__wt_readlock(session, &txn_global->commit_timestamp_rwlock);
		txn = TAILQ_FIRST(&txn_global->commit_timestamph);
		if (txn != NULL &&
		    __wt_timestamp_cmp(&txn->first_commit_timestamp, &ts) < 0) {
			__wt_timestamp_set(&ts, &txn->first_commit_timestamp);
			WT_ASSERT(session, !__wt_timestamp_iszero(&ts));
		}
		__wt_readunlock(session, &txn_global->commit_timestamp_rwlock);
	} else if (WT_STRING_MATCH("oldest", cval.str, cval.len)) {
		if (!txn_global->has_oldest_timestamp)
			return (WT_NOTFOUND);
		WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
		    __wt_timestamp_set(&ts, &txn_global->oldest_timestamp));
	} else if (WT_STRING_MATCH("pinned", cval.str, cval.len)) { //默认走这个分支
	    //该分支默认为read_timestamp和oldest_timestamp的较小值
		if (!txn_global->has_oldest_timestamp)
			return (WT_NOTFOUND);
		__wt_readlock(session, &txn_global->rwlock);
		__wt_timestamp_set(&ts, &txn_global->oldest_timestamp);

		/* Check for a running checkpoint */
		txn = txn_global->checkpoint_txn;
		if (txn_global->checkpoint_state.pinned_id != WT_TXN_NONE && //有session在做checkpoint操作
		    !__wt_timestamp_iszero(&txn->read_timestamp) &&
		    __wt_timestamp_cmp(&txn->read_timestamp, &ts) < 0)
			__wt_timestamp_set(&ts, &txn->read_timestamp);
		__wt_readunlock(session, &txn_global->rwlock);

		/* Look for the oldest ordinary reader. */
		__wt_readlock(session, &txn_global->read_timestamp_rwlock);
		txn = TAILQ_FIRST(&txn_global->read_timestamph);
		if (txn != NULL &&
		    __wt_timestamp_cmp(&txn->read_timestamp, &ts) < 0)
			__wt_timestamp_set(&ts, &txn->read_timestamp);
		__wt_readunlock(session, &txn_global->read_timestamp_rwlock);
	} else if (WT_STRING_MATCH("stable", cval.str, cval.len)) {
		if (!txn_global->has_stable_timestamp)
			return (WT_NOTFOUND);
		WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
		    __wt_timestamp_set(&ts, &txn_global->stable_timestamp));
	} else
		WT_RET_MSG(session, EINVAL,
		    "unknown timestamp query %.*s", (int)cval.len, cval.str);

done:	__wt_timestamp_set(tsp, &ts);
	return (0);
}
#endif

/*
 * __wt_txn_global_query_timestamp --
 *	Query a timestamp.
 */ //mongodb中的conn->query_timestamp()调用
int
__wt_txn_global_query_timestamp(
    WT_SESSION_IMPL *session, char *hex_timestamp, const char *cfg[])
{
#ifdef HAVE_TIMESTAMPS
	wt_timestamp_t ts;

	WT_RET(__txn_global_query_timestamp(session, &ts, cfg));
	return (__wt_timestamp_to_hex_string(session, hex_timestamp, &ts));
#else
	WT_UNUSED(hex_timestamp);
	WT_UNUSED(cfg);

	WT_RET_MSG(session, ENOTSUP,
	    "requires a version of WiredTiger built with timestamp support");
#endif
}

#ifdef HAVE_TIMESTAMPS
/*
 * __wt_txn_update_pinned_timestamp --
 *	Update the pinned timestamp (the oldest timestamp that has to be
 *	maintained for current or future readers).
 */
int
__wt_txn_update_pinned_timestamp(WT_SESSION_IMPL *session, bool force)
{
	WT_DECL_RET;
	WT_TXN_GLOBAL *txn_global;
	wt_timestamp_t active_timestamp, last_pinned_timestamp;
	wt_timestamp_t oldest_timestamp, pinned_timestamp;
	const char *query_cfg[] = { WT_CONFIG_BASE(session,
	    WT_CONNECTION_query_timestamp), "get=pinned", NULL };  //默认为pinned

	txn_global = &S2C(session)->txn_global;

	/* Skip locking and scanning when the oldest timestamp is pinned. */
//如果pinned_timestamp就是oldest_timestamp，在更新oldest_timestamp前都不会走后面的
//流程进行pinned_timestamp更新
	if (txn_global->oldest_is_pinned) 
		return (0);

    //获取oldest_timestamp
	WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
	    __wt_timestamp_set(
		&oldest_timestamp, &txn_global->oldest_timestamp));

	/* Scan to find the global pinned timestamp. */
	//获取pinned_timestamp,如果已经设置直接使用，正常情况下不会返回
	//active_timestamp默认为read_timestamp和oldest_timestamp的较小值
	if ((ret = __txn_global_query_timestamp( 
	    session, &active_timestamp, query_cfg)) != 0)
		return (ret == WT_NOTFOUND ? 0 : ret);

    //pinned_timestamp必须是oldest_timestamp和active_timestamp中的较小值
	if (__wt_timestamp_cmp(&oldest_timestamp, &active_timestamp) < 0) {
		__wt_timestamp_set(&pinned_timestamp, &oldest_timestamp);
	} else
		__wt_timestamp_set(&pinned_timestamp, &active_timestamp);

	if (txn_global->has_pinned_timestamp && !force) { //txn_global已经有pinned_timestamp
		WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
		    __wt_timestamp_set(
			&last_pinned_timestamp, &txn_global->pinned_timestamp));

        //如果已有的pinned_timestamp小于等于oldest_timestamp，则直接返回
		if (__wt_timestamp_cmp(
		    &pinned_timestamp, &last_pinned_timestamp) <= 0)
			return (0);
	}

	__wt_writelock(session, &txn_global->rwlock);
	//如果还没有设置pinned_timestamp，或者已经有pinned_timestamp但是本次获取的pinned_timestamp比之前的大，则更新
	//从这里可以看出txn_global->pinned_timestamp实际上就是当前多个session中最大的pinned_timestamp
	if (!txn_global->has_pinned_timestamp || force || __wt_timestamp_cmp(
	    &txn_global->pinned_timestamp, &pinned_timestamp) < 0) {
		__wt_timestamp_set(
		    &txn_global->pinned_timestamp, &pinned_timestamp);
		txn_global->has_pinned_timestamp = true;
		txn_global->oldest_is_pinned = __wt_timestamp_cmp(
		    &txn_global->pinned_timestamp,
		    &txn_global->oldest_timestamp) == 0; //如果pinned_timestamp就是oldest_timestamp
		__wt_verbose_timestamp(session,
		    &pinned_timestamp, "Updated pinned timestamp");
	}
	__wt_writeunlock(session, &txn_global->rwlock);

	return (0);
}
#endif

/*
 * __wt_txn_global_set_timestamp --
 *	Set a global transaction timestamp.
 */ 
/*
Parameters
connection	the connection handle
config	Configuration string, see Configuration Strings. Permitted values:
Name	Effect	Values
commit_timestamp	reset the maximum commit timestamp tracked by WiredTiger. This will cause future calls to WT_CONNECTION::query_timestamp to ignore commit timestamps greater than the specified value until the next commit moves the tracked commit timestamp forwards. This is only intended for use where the application is rolling back locally committed transactions. The supplied value should not be older than the current oldest and stable timestamps. See Application-specified Transaction Timestamps.	a string; default empty.
force	set timestamps even if they violate normal ordering requirements. For example allow the oldest_timestamp to move backwards.	a boolean flag; default false.
oldest_timestamp	future commits and queries will be no earlier than the specified timestamp. Supplied values must be monotonically increasing, any attempt to set the value to older than the current is silently ignored. The supplied value should not be newer than the current stable timestamp. See Application-specified Transaction Timestamps.	a string; default empty.
stable_timestamp	checkpoints will not include commits that are newer than the specified timestamp in tables configured with log=(enabled=false). Supplied values must be monotonically increasing, any attempt to set the value to older than the current is silently ignored. The supplied value should not be older than the current oldest timestamp. See Application-specified Transaction Timestamps.	a string; default empty.
*/
//注意__wt_txn_global_set_timestamp和__wt_txn_set_commit_timestamp的区别
int
__wt_txn_global_set_timestamp(WT_SESSION_IMPL *session, const char *cfg[])
{
	WT_CONFIG_ITEM commit_cval, oldest_cval, stable_cval;
	bool has_commit, has_oldest, has_stable;

	WT_RET(__wt_config_gets_def(session,
	    cfg, "commit_timestamp", 0, &commit_cval));
	has_commit = commit_cval.len != 0;

	WT_RET(__wt_config_gets_def(session,
	    cfg, "oldest_timestamp", 0, &oldest_cval));
	has_oldest = oldest_cval.len != 0;

	WT_RET(__wt_config_gets_def(session,
	    cfg, "stable_timestamp", 0, &stable_cval));
	has_stable = stable_cval.len != 0;

	/* If no timestamp was supplied, there's nothing to do. */
	if (!has_commit && !has_oldest && !has_stable)
		return (0);

#ifdef HAVE_TIMESTAMPS
	{
	WT_CONFIG_ITEM cval;
	WT_TXN_GLOBAL *txn_global;
	wt_timestamp_t commit_ts, oldest_ts, stable_ts;
	wt_timestamp_t last_oldest_ts, last_stable_ts;
	bool force;

	txn_global = &S2C(session)->txn_global;

	/*
	 * Parsing will initialize the timestamp to zero even if
	 * it is not configured.
	 */ 
	/* 关系必须满足一下条件才会有效，见后面比较
    oldest >= commit
    stable >= commit
    oldest >= stable
	*/
	WT_RET(__wt_txn_parse_timestamp(
	    session, "commit", &commit_ts, &commit_cval));
	WT_RET(__wt_txn_parse_timestamp(
	    session, "oldest", &oldest_ts, &oldest_cval));
	WT_RET(__wt_txn_parse_timestamp(
	    session, "stable", &stable_ts, &stable_cval));

	WT_RET(__wt_config_gets_def(session,
	    cfg, "force", 0, &cval));
	force = cval.val != 0;

	if (force)
		goto set;

	__wt_readlock(session, &txn_global->rwlock);

    //本次设置之前的上一次调用的值暂存起来
	__wt_timestamp_set(&last_oldest_ts, &txn_global->oldest_timestamp);
	__wt_timestamp_set(&last_stable_ts, &txn_global->stable_timestamp);

	/*
	 * First do error checking on the timestamp values.  The
	 * oldest timestamp must always be less than or equal to
	 * the stable timestamp.  If we're only setting one
	 * then compare against the system timestamp.  If we're
	 * setting both then compare the passed in values.
	 */
	//例如本次配置没有指定commit_timestamp配置，则使用上次的commit_timestamp配置，上次的配置存到txn_global->has_commit_timestamp中的
	if (!has_commit && txn_global->has_commit_timestamp)
		__wt_timestamp_set(&commit_ts, &txn_global->commit_timestamp);
	if (!has_oldest && txn_global->has_oldest_timestamp)
		__wt_timestamp_set(&oldest_ts, &last_oldest_ts);
	if (!has_stable && txn_global->has_stable_timestamp)
		__wt_timestamp_set(&stable_ts, &last_stable_ts);

    
	/*
	 * If a commit timestamp was supplied, check that it is no older than
	 * either the stable timestamp or the oldest timestamp.
	 */
	/* 关系必须满足一下条件才会有效，见后面比较
    oldest <= commit
    stable <= commit
    oldest <= stable

    也就是oldest <= stable <= commit
	*/
	if (has_commit && (has_oldest || txn_global->has_oldest_timestamp) &&
	    __wt_timestamp_cmp(&oldest_ts, &commit_ts) > 0) {
		__wt_readunlock(session, &txn_global->rwlock);
		WT_RET_MSG(session, EINVAL,
		    "set_timestamp: oldest timestamp must not be later than "
		    "commit timestamp"); //注意这里的几个检查提示
	}

	if (has_commit && (has_stable || txn_global->has_stable_timestamp) &&
	    __wt_timestamp_cmp(&stable_ts, &commit_ts) > 0) {
		__wt_readunlock(session, &txn_global->rwlock);
		WT_RET_MSG(session, EINVAL,
		    "set_timestamp: stable timestamp must not be later than "
		    "commit timestamp");//注意这里的几个检查提示
	}

	/*
	 * The oldest and stable timestamps must always satisfy the condition
	 * that oldest <= stable.
	 */
	if ((has_oldest || has_stable) &&
	    (has_oldest || txn_global->has_oldest_timestamp) &&
	    (has_stable || txn_global->has_stable_timestamp) &&
	    __wt_timestamp_cmp(&oldest_ts, &stable_ts) > 0) {
		__wt_readunlock(session, &txn_global->rwlock);
		WT_RET_MSG(session, EINVAL,
		    "set_timestamp: oldest timestamp must not be later than "
		    "stable timestamp");//注意这里的几个检查提示
	}

	__wt_readunlock(session, &txn_global->rwlock);

	/* Check if we are actually updating anything. */
	if (has_oldest && txn_global->has_oldest_timestamp &&
	    __wt_timestamp_cmp(&oldest_ts, &last_oldest_ts) <= 0)
		has_oldest = false;

	if (has_stable && txn_global->has_stable_timestamp &&
	    __wt_timestamp_cmp(&stable_ts, &last_stable_ts) <= 0)
		has_stable = false;

	if (!has_commit && !has_oldest && !has_stable)
		return (0);

//这几个配置满足要求，则更新全局txn_global时间撮相关成员
set:	__wt_writelock(session, &txn_global->rwlock);
	/*
	 * This method can be called from multiple threads, check that we are
	 * moving the global timestamps forwards.
	 *
	 * The exception is the commit timestamp, where the application can
	 * move it backwards (in fact, it only really makes sense to explicitly
	 * move it backwards because it otherwise tracks the largest
	 * commit_timestamp so it moves forward whenever transactions are
	 * assigned timestamps).
	 */
	if (has_commit) {
	    //这里直接赋值，没有和之前的txn_global->commit_timestamp做比较
		__wt_timestamp_set(&txn_global->commit_timestamp, &commit_ts); 
		txn_global->has_commit_timestamp = true;
		__wt_verbose_timestamp(session, &commit_ts,
		    "Updated global commit timestamp");
	}

    //例如有多个线程，每个线程的session在调用该函数进行oldest_timestamp设置，则txn_global->oldest_timestamp是这些设置中的最大值
	if (has_oldest && (!txn_global->has_oldest_timestamp ||
	    force || __wt_timestamp_cmp(
	    &oldest_ts, &txn_global->oldest_timestamp) > 0)) {
		__wt_timestamp_set(&txn_global->oldest_timestamp, &oldest_ts);
		txn_global->has_oldest_timestamp = true;
		txn_global->oldest_is_pinned = false;
		__wt_verbose_timestamp(session, &oldest_ts,
		    "Updated global oldest timestamp");
	}

    //例如有多个线程，每个线程的session在调用该函数进行stable_timestamp设置，则txn_global->stable_timestamp是这些设置中的最大值
	if (has_stable && (!txn_global->has_stable_timestamp ||
	    force || __wt_timestamp_cmp(
	    &stable_ts, &txn_global->stable_timestamp) > 0)) {
		__wt_timestamp_set(&txn_global->stable_timestamp, &stable_ts);
		txn_global->has_stable_timestamp = true;
		txn_global->stable_is_pinned = false;
		__wt_verbose_timestamp(session, &stable_ts,
		    "Updated global stable timestamp");
	}
	__wt_writeunlock(session, &txn_global->rwlock);

	if (has_oldest || has_stable)
		WT_RET(__wt_txn_update_pinned_timestamp(session, force));
	}
	return (0);
#else
	WT_RET_MSG(session, ENOTSUP, "set_timestamp requires a "
	    "version of WiredTiger built with timestamp support");
#endif
}

#ifdef HAVE_TIMESTAMPS
/*
 * __wt_timestamp_validate --
 *	Validate a timestamp to be not older than the global oldest and/or
 *	global stable and/or running transaction commit timestamp.
 */ //ts必须比oldest_timestamp和stable_timestamp大
int
__wt_timestamp_validate(WT_SESSION_IMPL *session, const char *name,
    wt_timestamp_t *ts, WT_CONFIG_ITEM *cval,
    bool cmp_oldest, bool cmp_stable, bool cmp_commit)
{
	WT_TXN *txn = &session->txn;
	WT_TXN_GLOBAL *txn_global = &S2C(session)->txn_global;
	char hex_timestamp[2 * WT_TIMESTAMP_SIZE + 1];
	bool older_than_oldest_ts, older_than_stable_ts;

	/*
	 * Compare against the oldest and the stable timestamp. Return an error
	 * if the given timestamp is older than oldest and/or stable timestamp.
	 */
	WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
	    older_than_oldest_ts = (cmp_oldest &&
		txn_global->has_oldest_timestamp &&
		__wt_timestamp_cmp(ts, &txn_global->oldest_timestamp) < 0);
	    older_than_stable_ts = (cmp_stable &&
		txn_global->has_stable_timestamp &&
		__wt_timestamp_cmp(ts, &txn_global->stable_timestamp) < 0));

	if (older_than_oldest_ts)
		WT_RET_MSG(session, EINVAL,
		    "%s timestamp %.*s older than oldest timestamp",
		    name, (int)cval->len, cval->str);
	if (older_than_stable_ts)
		WT_RET_MSG(session, EINVAL,
		    "%s timestamp %.*s older than stable timestamp",
		    name, (int)cval->len, cval->str);

	/*
	 * Compare against the commit timestamp of the current transaction.
	 * Return an error if the given timestamp is older than the first
	 * commit timestamp.
	 */
	if (cmp_commit && F_ISSET(txn, WT_TXN_HAS_TS_COMMIT) &&
	    __wt_timestamp_cmp(ts, &txn->first_commit_timestamp) < 0) {
		WT_RET(__wt_timestamp_to_hex_string(
		    session, hex_timestamp, &txn->first_commit_timestamp));
		WT_RET_MSG(session, EINVAL,
		    "%s timestamp %.*s older than the first "
		    "commit timestamp %s for this transaction",
		    name, (int)cval->len, cval->str, hex_timestamp);
	}

	return (0);
}
#endif

/*
 * __wt_txn_set_timestamp --
 *	Set a transaction's timestamp.
 */ //WT_SESSION->timestamp_transaction中调用
int
__wt_txn_set_timestamp(WT_SESSION_IMPL *session, const char *cfg[])
{
	WT_CONFIG_ITEM cval;
	WT_DECL_RET;

	/*
	 * Look for a commit timestamp.
	 */
	ret = __wt_config_gets_def(session, cfg, "commit_timestamp", 0, &cval);
	if (ret == 0 && cval.len != 0) {
#ifdef HAVE_TIMESTAMPS
		WT_TXN *txn = &session->txn;
		wt_timestamp_t ts;

		if (!F_ISSET(txn, WT_TXN_RUNNING))
			WT_RET_MSG(session, EINVAL,
			    "Transaction must be running "
			    "to set a commit_timestamp");
		WT_RET(__wt_txn_parse_timestamp(session, "commit", &ts, &cval));
		WT_RET(__wt_timestamp_validate(session,
		    "commit", &ts, &cval, true, true, true));
		__wt_timestamp_set(&txn->commit_timestamp, &ts);
		__wt_txn_set_commit_timestamp(session);
#else
		WT_RET_MSG(session, ENOTSUP, "commit_timestamp requires a "
		    "version of WiredTiger built with timestamp support");
#endif
	}
	WT_RET_NOTFOUND_OK(ret);

	return (0);
}

#ifdef HAVE_TIMESTAMPS
/*
 * __wt_txn_set_commit_timestamp --
 *	Publish a transaction's commit timestamp.
 */ 
 //只要mongodb有调用WT_SESSION->timestamp_transaction(指定commit_timestamp配置项)或者调用session->commit_transaction(并且
 //指定commit_timestamp配置项)接口的时候都会执行该函数,向全局队列txn_global->commit_timestamph中添加该txn


void //__wt_txn_set_commit_timestamp和__wt_txn_clear_commit_timestamp对应
__wt_txn_set_commit_timestamp(WT_SESSION_IMPL *session)
{//注意__wt_txn_global_set_timestamp和__wt_txn_set_commit_timestamp的区别
	WT_TXN *prev, *txn;
	WT_TXN_GLOBAL *txn_global;
	wt_timestamp_t ts;

	txn = &session->txn;
	txn_global = &S2C(session)->txn_global;

	if (F_ISSET(txn, WT_TXN_PUBLIC_TS_COMMIT))
		return;

	/*
	 * Copy the current commit timestamp (which can change while the
	 * transaction is running) into the first_commit_timestamp, which is
	 * fixed.
	 */
	//把本次session的上一次操作的commit_timestamp保存到first_commit_timestamp
	__wt_timestamp_set(&ts, &txn->commit_timestamp);
	__wt_timestamp_set(&txn->first_commit_timestamp, &ts);  

    //下面添加到全局队列中，真正生效的地方在__txn_global_query_timestamp
	__wt_writelock(session, &txn_global->commit_timestamp_rwlock);
	//找到这个新的ts应该放到列表中的那个位置，也就是该队列是从小到大排序的
	for (prev = TAILQ_LAST(&txn_global->commit_timestamph, __wt_txn_cts_qh);
	    prev != NULL &&
	    __wt_timestamp_cmp(&prev->first_commit_timestamp, &ts) > 0;
	    prev = TAILQ_PREV(prev, __wt_txn_cts_qh, commit_timestampq))
		;
	if (prev == NULL) {
		TAILQ_INSERT_HEAD(
		    &txn_global->commit_timestamph, txn, commit_timestampq);
		//增加统计，往头部添加的次数，也就是txn_global->commit_timestamph为空的时候添加的次数
		WT_STAT_CONN_INCR(session, txn_commit_queue_head);
	} else //插入到前面查找定位的位置
		TAILQ_INSERT_AFTER(&txn_global->commit_timestamph,
		    prev, txn, commit_timestampq);
	++txn_global->commit_timestampq_len;
	WT_STAT_CONN_INCR(session, txn_commit_queue_inserts);
	__wt_writeunlock(session, &txn_global->commit_timestamp_rwlock);
	F_SET(txn, WT_TXN_HAS_TS_COMMIT | WT_TXN_PUBLIC_TS_COMMIT);
}

/*
 * __wt_txn_clear_commit_timestamp --
 *	Clear a transaction's published commit timestamp.
 */ //__wt_txn_set_commit_timestamp和__wt_txn_clear_commit_timestamp对应
void
__wt_txn_clear_commit_timestamp(WT_SESSION_IMPL *session)
{
	WT_TXN *txn;
	WT_TXN_GLOBAL *txn_global;

	txn = &session->txn;
	txn_global = &S2C(session)->txn_global;

	if (!F_ISSET(txn, WT_TXN_PUBLIC_TS_COMMIT))
		return;

	__wt_writelock(session, &txn_global->commit_timestamp_rwlock);
	TAILQ_REMOVE(&txn_global->commit_timestamph, txn, commit_timestampq);
	--txn_global->commit_timestampq_len;
	__wt_writeunlock(session, &txn_global->commit_timestamp_rwlock);
	F_CLR(txn, WT_TXN_PUBLIC_TS_COMMIT);
}

/*
 * __wt_txn_set_read_timestamp --
 *	Publish a transaction's read timestamp.
 */ //__wt_txn_set_read_timestamp和__wt_txn_clear_read_timestamp对应
void //__wt_txn_begin->__wt_txn_config->__wt_txn_set_read_timestamp
__wt_txn_set_read_timestamp(WT_SESSION_IMPL *session)
{
	WT_TXN *prev, *txn;
	WT_TXN_GLOBAL *txn_global;

	txn = &session->txn;
	txn_global = &S2C(session)->txn_global;

	if (F_ISSET(txn, WT_TXN_PUBLIC_TS_READ))
		return;

	__wt_writelock(session, &txn_global->read_timestamp_rwlock);
	//找到这个新的ts应该放到列表中的那个位置，也就是该队列是从小到大排序的
	for (prev = TAILQ_LAST(&txn_global->read_timestamph, __wt_txn_rts_qh);
	    prev != NULL && __wt_timestamp_cmp(
	    &prev->read_timestamp, &txn->read_timestamp) > 0;
	    prev = TAILQ_PREV(prev, __wt_txn_rts_qh, read_timestampq))
		;
	if (prev == NULL) {
		TAILQ_INSERT_HEAD(
		    &txn_global->read_timestamph, txn, read_timestampq);
		WT_STAT_CONN_INCR(session, txn_read_queue_head);
	 } else //插入指定位置
		TAILQ_INSERT_AFTER(
		    &txn_global->read_timestamph, prev, txn, read_timestampq);
	++txn_global->read_timestampq_len;
	WT_STAT_CONN_INCR(session, txn_read_queue_inserts);
	__wt_writeunlock(session, &txn_global->read_timestamp_rwlock);
	F_SET(txn, WT_TXN_HAS_TS_READ | WT_TXN_PUBLIC_TS_READ);
}

/*
 * __wt_txn_clear_read_timestamp --
 *	Clear a transaction's published read timestamp.
 */
void
__wt_txn_clear_read_timestamp(WT_SESSION_IMPL *session)
{
	WT_TXN *txn;
	WT_TXN_GLOBAL *txn_global;

	txn = &session->txn;
	txn_global = &S2C(session)->txn_global;

	if (!F_ISSET(txn, WT_TXN_PUBLIC_TS_READ))
		return;

#ifdef HAVE_DIAGNOSTIC
	{
	wt_timestamp_t pinned_ts;

	WT_WITH_TIMESTAMP_READLOCK(session, &txn_global->rwlock,
	    __wt_timestamp_set(&pinned_ts, &txn_global->pinned_timestamp));
	WT_ASSERT(session,
	    __wt_timestamp_cmp(&txn->read_timestamp, &pinned_ts) >= 0);
	}
#endif

	__wt_writelock(session, &txn_global->read_timestamp_rwlock);
	TAILQ_REMOVE(&txn_global->read_timestamph, txn, read_timestampq);
	--txn_global->read_timestampq_len;
	__wt_writeunlock(session, &txn_global->read_timestamp_rwlock);
	F_CLR(txn, WT_TXN_PUBLIC_TS_READ);
}
#endif
