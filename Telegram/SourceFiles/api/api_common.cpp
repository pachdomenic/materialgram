/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_common.h"

#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "history/history.h"

namespace Api {
namespace {

constexpr auto kGhostScheduleDelay = TimeId(12);

} // namespace

MTPSuggestedPost SuggestToMTP(SuggestOptions suggest) {
	using Flag = MTPDsuggestedPost::Flag;
	return suggest.exists
		? MTP_suggestedPost(
			MTP_flags((suggest.date ? Flag::f_schedule_date : Flag())
				| (suggest.price().empty() ? Flag() : Flag::f_price)),
			StarsAmountToTL(suggest.price()),
			MTP_int(suggest.date))
		: MTPSuggestedPost();
}

SendAction::SendAction(
	not_null<Data::Thread*> thread,
	SendOptions options)
: history(thread->owningHistory())
, options(options)
, replyTo({ .messageId = { history->peer->id, thread->topicRootId() } }) {
	replyTo.topicRootId = replyTo.messageId.msg;
}

SendOptions DefaultSendWhenOnlineOptions() {
	return {
		.scheduled = kScheduledUntilOnlineTimestamp,
		.silent = base::IsCtrlPressed(),
	};
}

TimeId RequestScheduleDate(const SendOptions &options) {
	return (options.ghostScheduled
		&& !options.scheduled
		&& !options.shortcutId)
		? (base::unixtime::now() + kGhostScheduleDelay)
		: options.scheduled;
}

void MarkGhostScheduled(SendAction &action) {
	const auto &options = action.options;
	action.options.ghostScheduled = Core::App().settings().ghostOption(
		Core::GhostOption::ScheduledSend)
		&& !options.scheduled
		&& !options.shortcutId
		&& !options.suggest
		&& !action.history->peer->isSelf();
}

MTPInputReplyTo SendAction::mtpReplyTo() const {
	return Data::ReplyToForMTP(history, replyTo);
}

} // namespace Api
