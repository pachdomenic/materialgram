/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class History;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Data {

// materialgram: kept deleted messages, restored into history on load.
class DeletedMessages final {
public:
	explicit DeletedMessages(not_null<Main::Session*> session);
	~DeletedMessages();

	void remember(not_null<HistoryItem*> item);
	void forget(PeerId peer, const QVector<MTPint> &ids);
	void forgetAll(PeerId peer);
	void restore(
		not_null<History*> history,
		std::vector<not_null<HistoryItem*>> &items);

	[[nodiscard]] bool has(PeerId peer);
	[[nodiscard]] std::vector<MsgId> ids(PeerId peer);
	[[nodiscard]] HistoryItem *lookup(not_null<History*> history, MsgId id);
	[[nodiscard]] rpl::producer<PeerId> changes() const;

private:
	[[nodiscard]] QByteArray serialize(not_null<HistoryItem*> item) const;
	[[nodiscard]] HistoryItem *deserialize(
		not_null<History*> history,
		MsgId id,
		const QByteArray &serialized);
	void ensureLoaded();
	void scheduleSave();
	void save();

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, base::flat_map<MsgId, QByteArray>> _messages;
	base::Timer _saveTimer;
	rpl::event_stream<PeerId> _changes;
	bool _loaded = false;
	bool _restoring = false;

};

} // namespace Data
