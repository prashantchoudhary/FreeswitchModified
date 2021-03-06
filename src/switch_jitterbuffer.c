/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 * Anthony Minessale II <anthm@freeswitch.org>
 *
 * switch_jitterbuffer.c -- Audio/Video Jitter Buffer
 *
 */
#include <switch.h>
#include <switch_jitterbuffer.h>
#include "private/switch_hashtable_private.h"

#define PERIOD_LEN 500
#define MAX_FRAME_PADDING 2
#define MAX_MISSING_SEQ 20
#define jb_debug(_jb, _level, _format, ...) if (_jb->debug_level >= _level) switch_log_printf(SWITCH_CHANNEL_SESSION_LOG_CLEAN(_jb->session), SWITCH_LOG_ALERT, "JB:%p:%s lv:%d ln:%d sz:%u/%u/%u c:%u %u/%u/%u/%u %.2f%% ->" _format, (void *) _jb, (jb->type == SJB_AUDIO ? "aud" : "vid"), _level, __LINE__,  _jb->min_frame_len, _jb->max_frame_len, _jb->frame_len, _jb->period_count, _jb->consec_good_count, _jb->period_good_count, _jb->consec_miss_count, _jb->period_miss_count, _jb->period_miss_pct, __VA_ARGS__)

const char *TOKEN_1 = "ONE";
const char *TOKEN_2 = "TWO";

struct switch_jb_s;

typedef struct switch_jb_node_s {
	struct switch_jb_s *parent;
	switch_rtp_packet_t packet;
	uint32_t len;
	uint8_t visible;
	uint8_t bad_hits;
	struct switch_jb_node_s *prev;
	struct switch_jb_node_s *next;
} switch_jb_node_t;

struct switch_jb_s {
	struct switch_jb_node_s *node_list;
	uint32_t last_target_seq;
	uint32_t highest_read_ts;
	uint32_t highest_read_seq;
	uint32_t highest_wrote_ts;
	uint32_t highest_wrote_seq;
	uint16_t target_seq;
	uint32_t target_ts;
	uint32_t last_target_ts;
	uint16_t psuedo_seq;
	uint32_t visible_nodes;
	uint32_t complete_frames;
	uint32_t frame_len;
	uint32_t min_frame_len;
	uint32_t max_frame_len;
	uint32_t highest_frame_len;
	uint32_t period_miss_count;
	uint32_t consec_miss_count;
	double period_miss_pct;
	uint32_t period_good_count;
	uint32_t consec_good_count;
	uint32_t period_count;
	uint32_t dropped;
	uint32_t samples_per_frame;
	uint32_t samples_per_second;
	uint8_t write_init;
	uint8_t read_init;
	uint8_t debug_level;
	uint16_t next_seq;
	switch_size_t last_len;
	switch_inthash_t *missing_seq_hash;
	switch_inthash_t *node_hash;
	switch_inthash_t *node_hash_ts;
	switch_mutex_t *mutex;
	switch_mutex_t *list_mutex;
	switch_memory_pool_t *pool;
	int free_pool;
	switch_jb_flag_t flags;
	switch_jb_type_t type;
	switch_core_session_t *session;
	switch_channel_t *channel;
};


/*
 * https://gist.github.com/ideasman42/5921b0edfc6aa41a9ce0
 *
 * Originally from: http://stackoverflow.com/a/27663998/432509
 *
 * With following modifications:
 * - Use a pointer to the tail (remove 2x conditional checks, reduces code-size).
 * - Avoid re-assigning empty values the size doesn't change.
 * - Corrected comments.
 */

static int node_cmp(const void *l, const void *r)
{
	switch_jb_node_t *a = (switch_jb_node_t *) l;
	switch_jb_node_t *b = (switch_jb_node_t *) r;

	if (!a->visible && !b->visible) {
		return 0;
	}

	if (a->visible != b->visible) {
		if (a->visible) {
			return 1;
		} else {
			return -1;
		}
	}

	return a->packet.header.seq - b->packet.header.seq;
}

static switch_jb_node_t *sort_nodes(switch_jb_node_t *head, int (*cmp)(const void *, const void *))
{
	int block_size = 1, block_count;

	do {
		/* Maintain two lists pointing to two blocks, 'l' and 'r' */
		switch_jb_node_t *l = head, *r = head, **tail_p;
		
		head = NULL; /* Start a new list */
		tail_p = &head;

		block_count = 0;

		/* Walk through entire list in blocks of 'block_size'*/
		while (l != NULL) {
			/* Move 'r' to start of next block, measure size of 'l' list while doing so */
			int l_size, r_size = block_size;
			int l_empty, r_empty;

			block_count++;
			for (l_size = 0; (l_size < block_size) && (r != NULL); l_size++) {
				r = r->next;
			}

			/* Merge two list until their individual ends */
			l_empty = (l_size == 0);
			r_empty = (r_size == 0 || r == NULL);
			while (!l_empty || !r_empty) {
				switch_jb_node_t *s;
				/* Using <= instead of < gives us sort stability */
				if (r_empty || (!l_empty && cmp(l, r) <= 0)) {
					s = l;
					l = l->next;
					l_size--;
					l_empty = (l_size == 0);
				} else {
					s = r;
					r = r->next;
					r_size--;
					r_empty = (r_size == 0) || (r == NULL);
				}

				/* Update new list */
				(*tail_p) = s;
				tail_p = &(s->next);
			}

			/* 'r' now points to next block for 'l' */
			l = r;
		}

		/* terminate new list, take care of case when input list is NULL */
		*tail_p = NULL;

		/* Lg n iterations */
		block_size <<= 1;

	} while (block_count > 1);

	if (head) {
		switch_jb_node_t *np, *last = NULL;

		head->prev = NULL;

		for (np = head; np ; np = np->next) {
			if (last) {
				np->prev = last;
			}
			last = np;
		}
	}
				 

	return head;
}

static inline switch_jb_node_t *new_node(switch_jb_t *jb)
{
	switch_jb_node_t *np;

	switch_mutex_lock(jb->list_mutex);

	for (np = jb->node_list; np; np = np->next) {
		if (!np->visible) {
			break;
		}
	}

	if (!np) {
		
		np = switch_core_alloc(jb->pool, sizeof(*np));
		
		np->next = jb->node_list;
		if (np->next) {
			np->next->prev = np;
		}
		jb->node_list = np;
		
	}

	switch_assert(np);
	np->bad_hits = 0;
	np->visible = 1;
	jb->visible_nodes++;
	np->parent = jb;

	switch_mutex_unlock(jb->list_mutex);

	return np;
}

static inline void push_to_top(switch_jb_t *jb, switch_jb_node_t *node)
{
	if (node == jb->node_list) {
		jb->node_list = node->next;
	} else if (node->prev) {
		node->prev->next = node->next;
	}
			
	if (node->next) {
		node->next->prev = node->prev;
	}

	node->next = jb->node_list;
	node->prev = NULL;

	if (node->next) {
		node->next->prev = node;
	}

	jb->node_list = node;

	switch_assert(node->next != node);
	switch_assert(node->prev != node);
}

static inline void hide_node(switch_jb_node_t *node, switch_bool_t pop)
{
	switch_jb_t *jb = node->parent;

	switch_mutex_lock(jb->list_mutex);

	if (node->visible) {
		node->visible = 0;
		node->bad_hits = 0;
		jb->visible_nodes--;

		if (pop) {
			push_to_top(jb, node);
		}
	}

	if (jb->node_hash_ts) {
		switch_core_inthash_delete(jb->node_hash_ts, node->packet.header.ts);
	}

	switch_core_inthash_delete(jb->node_hash, node->packet.header.seq);

	switch_mutex_unlock(jb->list_mutex);
}

static inline void sort_free_nodes(switch_jb_t *jb)
{
	switch_jb_node_t *np, *this_np;
	int start = 0;

	switch_mutex_lock(jb->list_mutex);
	np = jb->node_list;

	while(np) {
		this_np = np;
		np = np->next;

		if (this_np->visible) {
			start++;
		}

		if (start && !this_np->visible) {
			push_to_top(jb, this_np);
		}		
	}

	switch_mutex_unlock(jb->list_mutex);
}

static inline void hide_nodes(switch_jb_t *jb)
{
	switch_jb_node_t *np;

	switch_mutex_lock(jb->list_mutex);
	for (np = jb->node_list; np; np = np->next) {
		hide_node(np, SWITCH_FALSE);
	}
	switch_mutex_unlock(jb->list_mutex);
}

static inline void drop_ts(switch_jb_t *jb, uint32_t ts)
{
	switch_jb_node_t *np;
	int x = 0;

	switch_mutex_lock(jb->list_mutex);
	for (np = jb->node_list; np; np = np->next) {
		if (!np->visible) continue;

		if (ts == np->packet.header.ts) {
			hide_node(np, SWITCH_FALSE);
			x++;
		}
	}

	if (x) {
		sort_free_nodes(jb);
	}

	switch_mutex_unlock(jb->list_mutex);
	
	if (x) jb->complete_frames--;
}

static inline switch_jb_node_t *jb_find_lowest_seq(switch_jb_t *jb, uint32_t ts)
{
	switch_jb_node_t *np, *lowest = NULL;
	
	switch_mutex_lock(jb->list_mutex);
	for (np = jb->node_list; np; np = np->next) {
		if (!np->visible) continue;

		if (ts && ts != np->packet.header.ts) continue;

		if (!lowest || ntohs(lowest->packet.header.seq) > ntohs(np->packet.header.seq)) {
			lowest = np;
		}
	}
	switch_mutex_unlock(jb->list_mutex);

	return lowest;
}

static inline switch_jb_node_t *jb_find_lowest_node(switch_jb_t *jb)
{
	switch_jb_node_t *np, *lowest = NULL;

	switch_mutex_lock(jb->list_mutex);
	for (np = jb->node_list; np; np = np->next) {
		if (!np->visible) continue;

		if (!lowest || ntohl(lowest->packet.header.ts) > ntohl(np->packet.header.ts)) {
			lowest = np;
		}
	}
	switch_mutex_unlock(jb->list_mutex);

	return lowest ? lowest : NULL;
}

static inline uint32_t jb_find_lowest_ts(switch_jb_t *jb)
{
	switch_jb_node_t *lowest = jb_find_lowest_node(jb);

	return lowest ? lowest->packet.header.ts : 0;
}

static inline void jb_hit(switch_jb_t *jb)
{
	jb->period_good_count++;
	jb->consec_good_count++;
	jb->consec_miss_count = 0;
}

static void jb_frame_inc(switch_jb_t *jb, int i)
{
	uint32_t old_frame_len = jb->frame_len;
	
	if (i == 0) {
		jb->frame_len = jb->min_frame_len;
		goto end;
	}

	if (i > 0) {
		if ((jb->frame_len + i) < jb->max_frame_len) {
			jb->frame_len += i;
		} else {
			jb->frame_len = jb->max_frame_len;
		}

		goto end;
	}

	if (i < 0) {
		if ((jb->frame_len + i) > jb->min_frame_len) {
			jb->frame_len += i;
		} else {
			jb->frame_len = jb->min_frame_len;
		}
	}

 end:

	if (jb->frame_len > jb->highest_frame_len) {
		jb->highest_frame_len = jb->frame_len;
	}

	if (jb->type == SJB_VIDEO && (jb->frame_len > jb->min_frame_len || jb->frame_len == jb->min_frame_len)) {
		if (jb->channel) {
			if (jb->frame_len == jb->min_frame_len) {
				jb_debug(jb, 2, "%s", "Allow BITRATE changes\n");
				switch_channel_clear_flag(jb->channel, CF_VIDEO_BITRATE_UNMANAGABLE);
			} else {
				switch_core_session_message_t msg = { 0 };
				int new_bitrate = 512;

				msg.message_id = SWITCH_MESSAGE_INDICATE_BITRATE_REQ;
				msg.numeric_arg = new_bitrate * 1024;
				msg.from = __FILE__;

				jb_debug(jb, 2, "Force BITRATE to %d\n", new_bitrate);
				switch_core_session_receive_message(jb->session, &msg);

				switch_channel_set_flag(jb->channel, CF_VIDEO_BITRATE_UNMANAGABLE);
				
			}
		}
	}

	if (old_frame_len != jb->frame_len) {
		jb_debug(jb, 2, "Change framelen from %u to %u\n", old_frame_len, jb->frame_len);
	}

}

static inline void jb_miss(switch_jb_t *jb)
{
	jb->period_miss_count++;
	jb->consec_miss_count++;
	jb->consec_good_count = 0;
}

static inline int verify_oldest_frame(switch_jb_t *jb)
{
	switch_jb_node_t *lowest = jb_find_lowest_node(jb), *np = NULL;
	int r = 0;

	if (!lowest || !(lowest = jb_find_lowest_seq(jb, lowest->packet.header.ts))) {
		goto end;
	}
	
	switch_mutex_lock(jb->mutex);

	jb->node_list = sort_nodes(jb->node_list, node_cmp);

	for (np = lowest->next; np; np = np->next) {
			
		if (!np->visible) continue;
			
		if (ntohs(np->packet.header.seq) != ntohs(np->prev->packet.header.seq) + 1) {
			switch_core_inthash_insert(jb->missing_seq_hash, (uint32_t)htons(ntohs(np->prev->packet.header.seq) + 1), (void *) TOKEN_1);
			break;
		}
				
		if (np->packet.header.ts != lowest->packet.header.ts || !np->next) {
			if (np->prev->packet.header.m) {
				r = 1;
			}
		}
	}
	
	switch_mutex_unlock(jb->mutex);

 end:

	if (!r && jb->session) {
		switch_core_session_request_video_refresh(jb->session);
	}

	return r;
}

static inline void drop_oldest_frame(switch_jb_t *jb)
{
	uint32_t ts = jb_find_lowest_ts(jb);

	drop_ts(jb, ts);
	jb_debug(jb, 1, "Dropping oldest frame ts:%u\n", ntohl(ts));
}

static inline void add_node(switch_jb_t *jb, switch_rtp_packet_t *packet, switch_size_t len)
{
	switch_jb_node_t *node = new_node(jb);

	node->packet = *packet;
	node->len = len;
	memcpy(node->packet.body, packet->body, len);

	switch_core_inthash_insert(jb->node_hash, node->packet.header.seq, node);

	if (jb->node_hash_ts) {
		switch_core_inthash_insert(jb->node_hash_ts, node->packet.header.ts, node);
	}

	jb_debug(jb, (packet->header.m ? 1 : 2), "PUT packet last_ts:%u ts:%u seq:%u%s\n", 
			 ntohl(jb->highest_wrote_ts), ntohl(node->packet.header.ts), ntohs(node->packet.header.seq), packet->header.m ? " <MARK>" : "");





	if (jb->write_init && ((abs(((int)ntohs(packet->header.seq) - ntohs(jb->highest_wrote_seq))) >= jb->max_frame_len) || 
						   (abs((int)((int64_t)ntohl(node->packet.header.ts) - (int64_t)ntohl(jb->highest_wrote_ts))) > (900000 * 5)))) {
		jb_debug(jb, 2, "%s", "CHANGE DETECTED, PUNT\n");
		switch_jb_reset(jb);
	}
 
	 
	 

	if (!jb->write_init || ntohs(packet->header.seq) > ntohs(jb->highest_wrote_seq) || 
		(ntohs(jb->highest_wrote_seq) > USHRT_MAX - 10 && ntohs(packet->header.seq) <= 10) ) {
		jb->highest_wrote_seq = packet->header.seq;
	}

	if (jb->type == SJB_VIDEO) {
		if (jb->write_init && htons(packet->header.seq) >= htons(jb->highest_wrote_seq) && (ntohl(node->packet.header.ts) > ntohl(jb->highest_wrote_ts))) {
			jb->complete_frames++;
			jb_debug(jb, 2, "WRITE frame ts: %u complete=%u/%u n:%u\n", ntohl(node->packet.header.ts), jb->complete_frames , jb->frame_len, jb->visible_nodes);
			jb->highest_wrote_ts = packet->header.ts;
			verify_oldest_frame(jb);
		} else if (!jb->write_init) {
			jb->highest_wrote_ts = packet->header.ts;
		}
	} else {
		if (jb->write_init) {
			jb_debug(jb, 2, "WRITE frame ts: %u complete=%u/%u n:%u\n", ntohl(node->packet.header.ts), jb->complete_frames , jb->frame_len, jb->visible_nodes);
			jb->complete_frames++;
		} else {
			jb->highest_wrote_ts = packet->header.ts;
		}
	}
	
	if (!jb->write_init) jb->write_init = 1;

	if (jb->complete_frames > jb->max_frame_len + MAX_FRAME_PADDING) {
		drop_oldest_frame(jb);
	}
}

static inline void increment_ts(switch_jb_t *jb)
{
	if (!jb->target_ts) return;

	jb->target_ts = htonl((ntohl(jb->target_ts) + jb->samples_per_frame));
	jb->psuedo_seq++;
}

static inline void set_read_ts(switch_jb_t *jb, uint32_t ts)
{
	if (!ts) return;

	jb->last_target_ts = ts;
	jb->target_ts = htonl((ntohl(jb->last_target_ts) + jb->samples_per_frame));
	jb->psuedo_seq++;
}


static inline void increment_seq(switch_jb_t *jb)
{
	jb->target_seq = htons((ntohs(jb->target_seq) + 1));
}

static inline void set_read_seq(switch_jb_t *jb, uint16_t seq)
{
	jb->last_target_seq = seq;
	jb->target_seq = htons((ntohs(jb->last_target_seq) + 1));
}

static inline switch_status_t jb_next_packet_by_seq(switch_jb_t *jb, switch_jb_node_t **nodep)
{
	switch_jb_node_t *node = NULL;

 top:

	if (jb->type == SJB_VIDEO) {
		if (jb->dropped) {
			jb->dropped = 0;
			jb_debug(jb, 2, "%s", "DROPPED FRAME DETECTED RESYNCING\n");
			jb->target_seq = 0;
			jb_frame_inc(jb, 1);

			if (jb->session) {
				switch_core_session_request_video_refresh(jb->session);
			}
		}
	}

	if (!jb->target_seq) {
		if ((node = jb_find_lowest_seq(jb, 0))) {
			jb_debug(jb, 2, "No target seq using seq: %u as a starting point\n", ntohs(node->packet.header.seq));
		} else {
			jb_debug(jb, 1, "%s", "No nodes available....\n");
		}
		jb_hit(jb);
	} else if ((node = switch_core_inthash_find(jb->node_hash, jb->target_seq))) {
		jb_debug(jb, 2, "FOUND desired seq: %u\n", ntohs(jb->target_seq));
		jb_hit(jb);
	} else {
		jb_debug(jb, 2, "MISSING desired seq: %u\n", ntohs(jb->target_seq));
		jb_miss(jb);

		if (jb->type == SJB_VIDEO) {
			int x;

			if (jb->session) {
				switch_core_session_request_video_refresh(jb->session);
			}
			
			for (x = 0; x < 10; x++) {
				increment_seq(jb);
				if ((node = switch_core_inthash_find(jb->node_hash, jb->target_seq))) {
					jb_debug(jb, 2, "FOUND incremental seq: %u\n", ntohs(jb->target_seq));

					if (node->packet.header.m ||  node->packet.header.ts == jb->highest_read_ts) {
						jb_debug(jb, 2, "%s", "SAME FRAME DROPPING\n");
						jb->dropped++;
						drop_ts(jb, node->packet.header.ts);
						node = NULL;
						goto top;
					}
					break;
				} else {
					jb_debug(jb, 2, "MISSING incremental seq: %u\n", ntohs(jb->target_seq));
				}
			}
		} else {
			increment_seq(jb);
		}
	}

	*nodep = node;
	
	if (node) {
		set_read_seq(jb, node->packet.header.seq);
		return SWITCH_STATUS_SUCCESS;
	}

	return SWITCH_STATUS_NOTFOUND;
	
}


static inline switch_status_t jb_next_packet_by_ts(switch_jb_t *jb, switch_jb_node_t **nodep)
{
	switch_jb_node_t *node = NULL;

	if (!jb->target_ts) {
		if ((node = jb_find_lowest_node(jb))) {
			jb_debug(jb, 2, "No target ts using ts: %u as a starting point\n", ntohl(node->packet.header.ts));
		} else {
			jb_debug(jb, 1, "%s", "No nodes available....\n");
		}
		jb_hit(jb);
	} else if ((node = switch_core_inthash_find(jb->node_hash_ts, jb->target_ts))) {
		jb_debug(jb, 2, "FOUND desired ts: %u\n", ntohl(jb->target_ts));
		jb_hit(jb);
	} else {
		jb_debug(jb, 2, "MISSING desired ts: %u\n", ntohl(jb->target_ts));
		jb_miss(jb);
		increment_ts(jb);
	}

	*nodep = node;
	
	if (node) {
		set_read_ts(jb, node->packet.header.ts);
		node->packet.header.seq = htons(jb->psuedo_seq);
		return SWITCH_STATUS_SUCCESS;
	}

	return SWITCH_STATUS_NOTFOUND;
	
}

static inline switch_status_t jb_next_packet(switch_jb_t *jb, switch_jb_node_t **nodep)
{
	if (jb->samples_per_frame) {
		return jb_next_packet_by_ts(jb, nodep);
	} else {
		return jb_next_packet_by_seq(jb, nodep);
	}
}

static inline void free_nodes(switch_jb_t *jb)
{
	switch_mutex_lock(jb->list_mutex);
	jb->node_list = NULL;
	switch_mutex_unlock(jb->list_mutex);
}

SWITCH_DECLARE(void) switch_jb_ts_mode(switch_jb_t *jb, uint32_t samples_per_frame, uint32_t samples_per_second)
{
	jb->samples_per_frame = samples_per_frame;
	jb->samples_per_second = samples_per_second;
	switch_core_inthash_init(&jb->node_hash_ts);
}

SWITCH_DECLARE(void) switch_jb_set_session(switch_jb_t *jb, switch_core_session_t *session)
{
	jb->session = session;
	jb->channel = switch_core_session_get_channel(session);
}

SWITCH_DECLARE(void) switch_jb_set_flag(switch_jb_t *jb, switch_jb_flag_t flag)
{
	switch_set_flag(jb, flag);
}

SWITCH_DECLARE(void) switch_jb_clear_flag(switch_jb_t *jb, switch_jb_flag_t flag)
{
	switch_clear_flag(jb, flag);
}

SWITCH_DECLARE(int) switch_jb_poll(switch_jb_t *jb)
{
	return (jb->complete_frames >= jb->frame_len);
}

SWITCH_DECLARE(int) switch_jb_frame_count(switch_jb_t *jb)
{
	return jb->complete_frames;
}

SWITCH_DECLARE(void) switch_jb_debug_level(switch_jb_t *jb, uint8_t level)
{
	jb->debug_level = level;
}

SWITCH_DECLARE(void) switch_jb_reset(switch_jb_t *jb)
{

	if (jb->type == SJB_VIDEO) {
		switch_mutex_lock(jb->mutex);
		switch_core_inthash_destroy(&jb->missing_seq_hash);
		switch_core_inthash_init(&jb->missing_seq_hash);
		switch_mutex_unlock(jb->mutex);
	}

	jb_debug(jb, 2, "%s", "RESET BUFFER\n");


	jb->last_target_seq = 0;
	jb->target_seq = 0;
	jb->write_init = 0;
	jb->highest_wrote_seq = 0;
	jb->highest_wrote_ts = 0;
	jb->next_seq = 0;
	jb->highest_read_ts = 0;
	jb->highest_read_seq = 0;
	jb->complete_frames = 0;
	jb->read_init = 0;
	jb->next_seq = 0;
	jb->complete_frames = 0;
	jb->period_miss_count = 0;
	jb->consec_miss_count = 0;
	jb->period_miss_pct = 0;
	jb->period_good_count = 0;
	jb->consec_good_count = 0;
	jb->period_count = 0;
	jb->target_ts = 0;
	jb->last_target_ts = 0;
	
	switch_mutex_lock(jb->mutex);
	hide_nodes(jb);
	switch_mutex_unlock(jb->mutex);
}

SWITCH_DECLARE(switch_status_t) switch_jb_peek_frame(switch_jb_t *jb, uint32_t ts, uint16_t seq, int peek, switch_frame_t *frame)
{
	switch_jb_node_t *node = NULL;

	if (seq) {
		uint16_t want_seq = seq + peek;
		node = switch_core_inthash_find(jb->node_hash, want_seq);
	} else if (ts && jb->samples_per_frame) {
		uint32_t want_ts = ts + (peek * jb->samples_per_frame);	
		node = switch_core_inthash_find(jb->node_hash_ts, want_ts);
	}

	if (node) {
		frame->seq = ntohs(node->packet.header.seq);
		frame->timestamp = ntohl(node->packet.header.ts);
		frame->m = node->packet.header.m;
		frame->datalen = node->len;
		if (frame->data && frame->buflen > node->len) {
			memcpy(frame->data, node->packet.body, node->len);
		}
		return SWITCH_STATUS_SUCCESS;
	}

	return SWITCH_STATUS_FALSE;
}

SWITCH_DECLARE(switch_status_t) switch_jb_get_frames(switch_jb_t *jb, uint32_t *min_frame_len, uint32_t *max_frame_len, uint32_t *cur_frame_len, uint32_t *highest_frame_len) 
{

	switch_mutex_lock(jb->mutex);

	if (min_frame_len) {
		*min_frame_len = jb->min_frame_len;
	}

	if (max_frame_len) {
		*max_frame_len = jb->max_frame_len;
	}

	if (cur_frame_len) {
		*cur_frame_len = jb->frame_len;
	}

	switch_mutex_unlock(jb->mutex);

	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(switch_status_t) switch_jb_set_frames(switch_jb_t *jb, uint32_t min_frame_len, uint32_t max_frame_len)
{
	switch_mutex_lock(jb->mutex);
	jb->min_frame_len = min_frame_len;
	jb->max_frame_len = max_frame_len;

	if (jb->frame_len > jb->max_frame_len) {
		jb->frame_len = jb->max_frame_len;
	}

	if (jb->frame_len < jb->min_frame_len) {
		jb->frame_len = jb->min_frame_len;
	}
	
	if (jb->frame_len > jb->highest_frame_len) {
		jb->highest_frame_len = jb->frame_len;
	}

	switch_mutex_unlock(jb->mutex);

	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(switch_status_t) switch_jb_create(switch_jb_t **jbp, switch_jb_type_t type,
												 uint32_t min_frame_len, uint32_t max_frame_len, switch_memory_pool_t *pool)
{
	switch_jb_t *jb;
	int free_pool = 0;

	if (!pool) {
		switch_core_new_memory_pool(&pool);
		free_pool = 1;
	}

	jb = switch_core_alloc(pool, sizeof(*jb));
	jb->free_pool = free_pool;
	jb->min_frame_len = jb->frame_len = min_frame_len;
	jb->max_frame_len = max_frame_len;
	jb->pool = pool;
	jb->type = type;
	jb->highest_frame_len = jb->frame_len;

	if (jb->type == SJB_VIDEO) {
		switch_core_inthash_init(&jb->missing_seq_hash);
	}
	switch_core_inthash_init(&jb->node_hash);
	switch_mutex_init(&jb->mutex, SWITCH_MUTEX_NESTED, pool);
	switch_mutex_init(&jb->list_mutex, SWITCH_MUTEX_NESTED, pool);

	*jbp = jb;

	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(switch_status_t) switch_jb_destroy(switch_jb_t **jbp)
{
	switch_jb_t *jb = *jbp;
	*jbp = NULL;
	
	if (jb->type == SJB_VIDEO) {
		switch_core_inthash_destroy(&jb->missing_seq_hash);
	}
	switch_core_inthash_destroy(&jb->node_hash);

	if (jb->node_hash_ts) {
		switch_core_inthash_destroy(&jb->node_hash_ts);
	}

	free_nodes(jb);

	if (jb->free_pool) {
		switch_core_destroy_memory_pool(&jb->pool);
	}

	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(uint32_t) switch_jb_pop_nack(switch_jb_t *jb)
{
	switch_hash_index_t *hi = NULL;
	uint32_t nack = 0;
	uint16_t blp = 0;
	uint16_t least = 0;
	int i = 0;
	
	void *val;
	const void *var;

	if (jb->type != SJB_VIDEO) {
		return 0;
	}

	switch_mutex_lock(jb->mutex);


	for (hi = switch_core_hash_first(jb->missing_seq_hash); hi; hi = switch_core_hash_next(&hi)) {
		uint16_t seq;
		const char *token;

		switch_core_hash_this(hi, &var, NULL, &val);
		token = (const char *) val;

		if (token == TOKEN_2) {
			//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "SKIP %u %s\n", ntohs(*((uint16_t *) var)), token);
			continue;
		}
		seq = ntohs(*((uint16_t *) var));

		if (!least || seq < least) {
			least = seq;
		}
	}

	if (least && switch_core_inthash_delete(jb->missing_seq_hash, (uint32_t)htons(least))) {
		jb_debug(jb, 3, "Found smallest NACKABLE seq %u\n", least);
		nack = (uint32_t) htons(least);

		switch_core_inthash_insert(jb->missing_seq_hash, nack, (void *) TOKEN_2);

		for(i = 0; i < 16; i++) {
			if (switch_core_inthash_delete(jb->missing_seq_hash, (uint32_t)htons(least + i + 1))) {
				switch_core_inthash_insert(jb->missing_seq_hash, (uint32_t)htons(least + i + 1), (void *) TOKEN_2);
				jb_debug(jb, 3, "Found addtl NACKABLE seq %u\n", least + i + 1);
				blp |= (1 << i);
			}
		}

		blp = htons(blp);
		nack |= (uint32_t) blp << 16;

		//jb_frame_inc(jb, 1);
	}
	
	switch_mutex_unlock(jb->mutex);


	return nack;
}

SWITCH_DECLARE(switch_status_t) switch_jb_push_packet(switch_jb_t *jb, switch_rtp_packet_t *packet, switch_size_t len)
{
	add_node(jb, packet, len);

	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(switch_status_t) switch_jb_put_packet(switch_jb_t *jb, switch_rtp_packet_t *packet, switch_size_t len)
{
	uint32_t i;
	uint16_t want = ntohs(jb->next_seq), got = ntohs(packet->header.seq);
	int missing = 0;

	switch_mutex_lock(jb->mutex);

	if (!want) want = got;

	if (switch_test_flag(jb, SJB_QUEUE_ONLY) || jb->type == SJB_AUDIO) {
		jb->next_seq = htons(got + 1);
	} else {

		switch_core_inthash_delete(jb->missing_seq_hash, (uint32_t)htons(got));

		if (!missing || want == got) {
			if (got > want) {
				//jb_debug(jb, 2, "GOT %u WANTED %u; MARK SEQS MISSING %u - %u\n", got, want, want, got - 1);
				//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "XXXXXXXXXXXXXXXXXX   WTF GOT %u WANTED %u; MARK SEQS MISSING %u - %u\n", got, want, want, got - 1);
				for (i = want; i < got; i++) {
					//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "MISSING %u\n", i);
					switch_core_inthash_insert(jb->missing_seq_hash, (uint32_t)htons(i), (void *)TOKEN_1);
				}
			
			}

			if (got >= want || (want - got) > 1000) {
				jb->next_seq = htons(got + 1);
			}
		}


	}

	add_node(jb, packet, len);
	
	switch_mutex_unlock(jb->mutex);

	return SWITCH_STATUS_SUCCESS;
}

SWITCH_DECLARE(switch_status_t) switch_jb_get_packet_by_seq(switch_jb_t *jb, uint16_t seq, switch_rtp_packet_t *packet, switch_size_t *len)
{
	switch_jb_node_t *node;
	switch_status_t status = SWITCH_STATUS_NOTFOUND;

	switch_mutex_lock(jb->mutex);
	if ((node = switch_core_inthash_find(jb->node_hash, seq))) {
		jb_debug(jb, 2, "Found buffered seq: %u\n", ntohs(seq));
		*packet = node->packet;
		*len = node->len;
		memcpy(packet->body, node->packet.body, node->len);
		status = SWITCH_STATUS_SUCCESS;
	} else {
		jb_debug(jb, 2, "Missing buffered seq: %u\n", ntohs(seq));
	}
	switch_mutex_unlock(jb->mutex);

	return status;
}

SWITCH_DECLARE(switch_size_t) switch_jb_get_last_read_len(switch_jb_t *jb)
{
	return jb->last_len;
}


SWITCH_DECLARE(switch_status_t) switch_jb_get_packet(switch_jb_t *jb, switch_rtp_packet_t *packet, switch_size_t *len)
{
	switch_jb_node_t *node = NULL;
	switch_status_t status;
	
	switch_mutex_lock(jb->mutex);
	jb_debug(jb, 2, "GET PACKET %u/%u n:%d\n", jb->complete_frames , jb->frame_len, jb->visible_nodes);

	if (jb->complete_frames < jb->frame_len) {
		jb_debug(jb, 2, "BUFFERING %u/%u\n", jb->complete_frames , jb->frame_len);
		switch_goto_status(SWITCH_STATUS_MORE_DATA, end);
	}

	if (++jb->period_count >= PERIOD_LEN) {

		if (jb->consec_good_count >= (PERIOD_LEN - 5)) {
			jb_frame_inc(jb, -1);
		}

		jb->period_count = 1;
		jb->period_miss_count = 0;
		jb->period_good_count = 0;
		jb->consec_miss_count = 0;
		jb->consec_good_count = 0;
	}

	jb->period_miss_pct = ((double)jb->period_miss_count / jb->period_count) * 100;

	if ((status = jb_next_packet(jb, &node)) == SWITCH_STATUS_SUCCESS) {
		jb_debug(jb, 2, "Found next frame cur ts: %u seq: %u\n", htonl(node->packet.header.ts), htons(node->packet.header.seq));

		if (!jb->read_init || ntohs(node->packet.header.seq) > ntohs(jb->highest_read_seq) || 
			(ntohs(jb->highest_read_seq) > USHRT_MAX - 10 && ntohs(node->packet.header.seq) <= 10) ) {
			jb->highest_read_seq = node->packet.header.seq;
		}
		
		if (jb->read_init && htons(node->packet.header.seq) >= htons(jb->highest_read_seq) && (ntohl(node->packet.header.ts) > ntohl(jb->highest_read_ts))) {
			jb->complete_frames--;
			jb_debug(jb, 2, "READ frame ts: %u complete=%u/%u n:%u\n", ntohl(node->packet.header.ts), jb->complete_frames , jb->frame_len, jb->visible_nodes);
			jb->highest_read_ts = node->packet.header.ts;
		} else if (!jb->read_init) {
			jb->highest_read_ts = node->packet.header.ts;
		}
		
		if (!jb->read_init) jb->read_init = 1;
	} else {
		if (jb->type == SJB_VIDEO) {
			switch_jb_reset(jb);
			jb_frame_inc(jb, 1);

			switch(status) {
			case SWITCH_STATUS_RESTART:
				jb_debug(jb, 2, "%s", "Error encountered ask for new keyframe\n");
				switch_goto_status(SWITCH_STATUS_RESTART, end);
			case SWITCH_STATUS_NOTFOUND:
			default:
				jb_debug(jb, 2, "%s", "No frames found wait for more\n");
				switch_goto_status(SWITCH_STATUS_MORE_DATA, end);
			}
		} else {
			switch(status) {
			case SWITCH_STATUS_RESTART:
				jb_debug(jb, 2, "%s", "Error encountered\n");
				switch_jb_reset(jb);
				switch_goto_status(SWITCH_STATUS_RESTART, end);
			case SWITCH_STATUS_NOTFOUND:
			default:							
				if (jb->consec_miss_count > jb->frame_len) {
					switch_jb_reset(jb);
					jb_frame_inc(jb, 1);
					jb_debug(jb, 2, "%s", "Too many frames not found, RESIZE\n");
					switch_goto_status(SWITCH_STATUS_RESTART, end);
				} else {
					jb_debug(jb, 2, "%s", "Frame not found suggest PLC\n");
					switch_goto_status(SWITCH_STATUS_NOTFOUND, end);
				}
			}
		}
	}
	
	if (node) {
		status = SWITCH_STATUS_SUCCESS;
		
		*packet = node->packet;
		*len = node->len;
		jb->last_len = *len;
		memcpy(packet->body, node->packet.body, node->len);
		hide_node(node, SWITCH_TRUE);

		jb_debug(jb, 1, "GET packet ts:%u seq:%u %s\n", ntohl(packet->header.ts), ntohs(packet->header.seq), packet->header.m ? " <MARK>" : "");

	} else {
		status = SWITCH_STATUS_MORE_DATA;
	}

 end:

	switch_mutex_unlock(jb->mutex);

	return status;

}


/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
