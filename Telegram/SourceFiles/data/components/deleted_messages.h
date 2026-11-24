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
class PhotoData;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct DeletedStoredMessage {
	MsgId id = 0;
	TimeId date = 0;
	PeerId from = 0;
	QString author;
	FullMsgId replyTo;
	MsgId topicRootId = 0;
	PeerId monoforumPeerId = 0;
	uint64 groupedId = 0;
	TextWithEntities text;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	bool out = false;
	bool post = false;
	bool invertMedia = false;
	bool spoiler = false;
};

// materialgram: kept deleted messages, an encrypted append-only journal.
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
	[[nodiscard]] std::vector<PeerId> peers();
	[[nodiscard]] int count(PeerId peer);
	[[nodiscard]] int totalCount();
	[[nodiscard]] std::vector<DeletedStoredMessage> messages(PeerId peer);
	[[nodiscard]] HistoryItem *lookup(not_null<History*> history, MsgId id);
	[[nodiscard]] rpl::producer<PeerId> changes() const;

private:
	[[nodiscard]] QByteArray serialize(not_null<HistoryItem*> item) const;
	[[nodiscard]] std::optional<DeletedStoredMessage> parse(
		MsgId id,
		const QByteArray &serialized) const;
	[[nodiscard]] HistoryItem *deserialize(
		not_null<History*> history,
		MsgId id,
		const QByteArray &serialized);
	void ensureLoaded();
	void loadLegacy(const QByteArray &serialized);
	void applyRecord(const QByteArray &record);
	void pushRecord(QByteArray record);
	[[nodiscard]] std::vector<QByteArray> snapshotRecords() const;
	void compactIfNeeded();
	void scheduleSave();
	void save();

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, base::flat_map<MsgId, QByteArray>> _messages;
	std::vector<QByteArray> _pending;
	base::Timer _saveTimer;
	rpl::event_stream<PeerId> _changes;
	int _journalRecords = 0;
	bool _loaded = false;
	bool _restoring = false;

};

} // namespace Data
