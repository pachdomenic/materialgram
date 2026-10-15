/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/deleted_messages.h"

#include "core/version.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "storage/serialize_document.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kSaveDelay = 5 * crl::time(1000);
constexpr auto kFormatVersion = 1;
constexpr auto kMaxPerPeer = 5000;

constexpr auto kFlagOut = quint32(1 << 0);
constexpr auto kFlagPost = quint32(1 << 1);
constexpr auto kFlagInvertMedia = quint32(1 << 2);

enum class StoredMedia : qint32 {
	None,
	Photo,
	Document,
};

constexpr auto kPhotoSizes = std::array{
	PhotoSize::Small,
	PhotoSize::Thumbnail,
	PhotoSize::Large,
};

[[nodiscard]] uint64 PhotoAccessHash(not_null<PhotoData*> photo) {
	return photo->mtpInput().match([](const MTPDinputPhoto &data) {
		return data.vaccess_hash().v;
	}, [](const auto &) {
		return uint64();
	});
}

} // namespace

DeletedMessages::DeletedMessages(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(); }) {
}

DeletedMessages::~DeletedMessages() {
	if (_saveTimer.isActive()) {
		save();
	}
}

void DeletedMessages::remember(not_null<HistoryItem*> item) {
	if (_restoring || !item->isRegular()) {
		return;
	}
	ensureLoaded();
	auto &list = _messages[item->history()->peer->id];
	list[item->id] = serialize(item);
	while (int(list.size()) > kMaxPerPeer) {
		list.erase(begin(list));
	}
	scheduleSave();
	_changes.fire_copy(item->history()->peer->id);
}

void DeletedMessages::forget(PeerId peer, const QVector<MTPint> &ids) {
	ensureLoaded();
	const auto i = _messages.find(peer);
	if (i == end(_messages)) {
		return;
	}
	auto changed = false;
	for (const auto &id : ids) {
		changed = i->second.remove(MsgId(id.v)) || changed;
	}
	if (i->second.empty()) {
		_messages.erase(i);
	}
	if (changed) {
		scheduleSave();
		_changes.fire_copy(peer);
	}
}

void DeletedMessages::forgetAll(PeerId peer) {
	ensureLoaded();
	if (_messages.remove(peer)) {
		scheduleSave();
		_changes.fire_copy(peer);
	}
}

void DeletedMessages::restore(
		not_null<History*> history,
		std::vector<not_null<HistoryItem*>> &items) {
	if (items.empty()) {
		return;
	}
	ensureLoaded();
	const auto i = _messages.find(history->peer->id);
	if (i == end(_messages)) {
		return;
	}
	const auto id = [](not_null<HistoryItem*> item) {
		return item->id;
	};
	const auto minId = ranges::min(items, ranges::less(), id)->id;
	const auto maxId = ranges::max(items, ranges::less(), id)->id;
	const auto last = history->lastServerMessageKnown()
		? history->lastServerMessage()
		: nullptr;
	const auto includeNewer = last && (last->id <= maxId);
	const auto owner = &history->owner();

	auto restored = std::vector<not_null<HistoryItem*>>();
	_restoring = true;
	for (const auto &[msgId, serialized] : i->second) {
		if (msgId <= minId || msgId == maxId) {
			continue;
		} else if (msgId > maxId && !includeNewer) {
			break;
		} else if (const auto existing = owner->message(history->peer, msgId)) {
			if (!existing->mainView() && owner->isMessageDeleted(existing)) {
				restored.push_back(existing);
			}
			continue;
		}
		if (const auto item = deserialize(history, msgId, serialized)) {
			owner->markMessageDeleted(item);
			restored.push_back(item);
		}
	}
	_restoring = false;
	if (restored.empty()) {
		return;
	}
	items.insert(end(items), begin(restored), end(restored));
	ranges::stable_sort(items, ranges::less(), id);
}

bool DeletedMessages::has(PeerId peer) {
	ensureLoaded();
	return _messages.contains(peer);
}

std::vector<MsgId> DeletedMessages::ids(PeerId peer) {
	ensureLoaded();
	const auto i = _messages.find(peer);
	if (i == end(_messages)) {
		return {};
	}
	return i->second | ranges::views::keys | ranges::to_vector;
}

HistoryItem *DeletedMessages::lookup(
		not_null<History*> history,
		MsgId id) {
	const auto owner = &history->owner();
	if (const auto existing = owner->message(history->peer, id)) {
		return existing;
	}
	ensureLoaded();
	const auto i = _messages.find(history->peer->id);
	if (i == end(_messages)) {
		return nullptr;
	}
	const auto j = i->second.find(id);
	if (j == end(i->second)) {
		return nullptr;
	}
	_restoring = true;
	const auto item = deserialize(history, id, j->second);
	if (item) {
		owner->markMessageDeleted(item);
	}
	_restoring = false;
	return item;
}

rpl::producer<PeerId> DeletedMessages::changes() const {
	return _changes.events();
}

QByteArray DeletedMessages::serialize(not_null<HistoryItem*> item) const {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);

	const auto reply = item->replyTo();
	const auto &text = item->originalText();
	const auto sign = item->Get<HistoryMessageSigned>();
	const auto flags = (item->out() ? kFlagOut : 0)
		| (item->isPost() ? kFlagPost : 0)
		| (item->invertMedia() ? kFlagInvertMedia : 0);
	const auto media = item->media();
	const auto webpage = media && media->webpage();
	const auto photo = (media && !webpage) ? media->photo() : nullptr;
	const auto document = (media && !webpage) ? media->document() : nullptr;
	stream
		<< qint32(AppVersion)
		<< qint32(item->date())
		<< quint64(SerializePeerId(item->from()->id))
		<< flags
		<< (sign ? sign->author : QString())
		<< quint64(SerializePeerId(reply.messageId.peer))
		<< qint64(reply.messageId.msg.bare)
		<< qint64(reply.topicRootId.bare)
		<< quint64(SerializePeerId(reply.monoforumPeerId))
		<< quint64(item->groupId().value)
		<< text.text
		<< TextUtilities::SerializeTags(
			TextUtilities::ConvertEntitiesToTextTags(text.entities))
		<< qint32((media && media->hasSpoiler()) ? 1 : 0);
	if (photo) {
		stream
			<< qint32(StoredMedia::Photo)
			<< quint64(photo->id)
			<< quint64(PhotoAccessHash(photo))
			<< photo->fileReference()
			<< qint32(photo->date())
			<< qint32(photo->_dc)
			<< qint32(photo->hasAttachedStickers() ? 1 : 0)
			<< photo->inlineThumbnailBytes();
		for (const auto size : kPhotoSizes) {
			stream
				<< photo->location(size).serialize()
				<< qint32(photo->imageByteSize(size));
		}
	} else if (document) {
		stream << qint32(StoredMedia::Document);
		Serialize::Document::writeToStream(stream, document);
	} else {
		stream << qint32(StoredMedia::None);
	}
	return result;
}

HistoryItem *DeletedMessages::deserialize(
		not_null<History*> history,
		MsgId id,
		const QByteArray &serialized) {
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto appVersion = qint32();
	auto date = qint32();
	auto from = quint64();
	auto flags = quint32();
	auto author = QString();
	auto replyPeer = quint64();
	auto replyMsg = qint64();
	auto topicRootId = qint64();
	auto monoforumPeer = quint64();
	auto groupedId = quint64();
	auto text = QString();
	auto tags = QByteArray();
	auto spoiler = qint32();
	auto type = qint32();
	stream
		>> appVersion
		>> date
		>> from
		>> flags
		>> author
		>> replyPeer
		>> replyMsg
		>> topicRootId
		>> monoforumPeer
		>> groupedId
		>> text
		>> tags
		>> spoiler
		>> type;
	if (stream.status() != QDataStream::Ok) {
		return nullptr;
	}

	const auto fromId = DeserializePeerId(from);
	auto messageFlags = MessageFlags();
	if (flags & kFlagOut) {
		messageFlags |= MessageFlag::Outgoing;
	}
	if (flags & kFlagPost) {
		messageFlags |= MessageFlag::Post;
	}
	if (flags & kFlagInvertMedia) {
		messageFlags |= MessageFlag::InvertMedia;
	}
	if (fromId && fromId != history->peer->id) {
		messageFlags |= MessageFlag::HasFromId;
	}
	if (!author.isEmpty()) {
		messageFlags |= MessageFlag::HasPostAuthor;
	}
	auto fields = HistoryItemCommonFields{
		.id = id,
		.flags = messageFlags,
		.from = fromId,
		.replyTo = {
			.messageId = FullMsgId(
				DeserializePeerId(replyPeer),
				MsgId(replyMsg)),
			.topicRootId = MsgId(topicRootId),
			.monoforumPeerId = DeserializePeerId(monoforumPeer),
		},
		.date = TimeId(date),
		.postAuthor = author,
		.groupedId = groupedId,
		.mediaSpoiler = (spoiler != 0),
	};
	const auto caption = TextWithEntities{
		text,
		TextUtilities::ConvertTextTagsToEntities(
			TextUtilities::DeserializeTags(tags, text.size())),
	};

	if (type == qint32(StoredMedia::Photo)) {
		auto photoId = quint64();
		auto access = quint64();
		auto fileReference = QByteArray();
		auto photoDate = qint32();
		auto dc = qint32();
		auto hasStickers = qint32();
		auto inlineThumbnail = QByteArray();
		auto images = std::array<ImageWithLocation, kPhotoSizes.size()>();
		stream
			>> photoId
			>> access
			>> fileReference
			>> photoDate
			>> dc
			>> hasStickers
			>> inlineThumbnail;
		for (auto &image : images) {
			auto location = QByteArray();
			auto bytesCount = qint32();
			stream >> location >> bytesCount;
			if (const auto parsed = ImageLocation::FromSerialized(location)) {
				image.location = *parsed;
			}
			image.bytesCount = bytesCount;
		}
		if (stream.status() != QDataStream::Ok) {
			return nullptr;
		}
		const auto photo = history->owner().photo(
			photoId,
			access,
			fileReference,
			photoDate,
			dc,
			(hasStickers != 0),
			inlineThumbnail,
			images[0],
			images[1],
			images[2],
			ImageWithLocation(),
			ImageWithLocation(),
			crl::time(0));
		return history->makeMessage(std::move(fields), photo, caption);
	} else if (type == qint32(StoredMedia::Document)) {
		const auto document = Serialize::Document::readFromStream(
			_session,
			appVersion,
			stream);
		if (document && stream.status() == QDataStream::Ok) {
			return history->makeMessage(
				std::move(fields),
				not_null(document),
				caption);
		}
	}
	return history->makeMessage(
		std::move(fields),
		caption,
		MTP_messageMediaEmpty());
}

void DeletedMessages::ensureLoaded() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	const auto serialized = _session->local().readDeletedMessages();
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto peers = qint32();
	stream >> version >> peers;
	if (stream.status() != QDataStream::Ok
		|| version != kFormatVersion
		|| peers < 0) {
		return;
	}
	for (auto i = 0; i != peers; ++i) {
		auto peer = quint64();
		auto count = qint32();
		stream >> peer >> count;
		if (stream.status() != QDataStream::Ok || count < 0) {
			return;
		}
		auto &list = _messages[DeserializePeerId(peer)];
		for (auto j = 0; j != count; ++j) {
			auto msg = qint64();
			auto data = QByteArray();
			stream >> msg >> data;
			if (stream.status() != QDataStream::Ok) {
				return;
			}
			list.emplace(MsgId(msg), std::move(data));
		}
	}
}

void DeletedMessages::scheduleSave() {
	_saveTimer.callOnce(kSaveDelay);
}

void DeletedMessages::save() {
	_saveTimer.cancel();
	auto serialized = QByteArray();
	{
		auto stream = QDataStream(&serialized, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(kFormatVersion) << qint32(_messages.size());
		for (const auto &[peer, list] : _messages) {
			stream << quint64(SerializePeerId(peer)) << qint32(list.size());
			for (const auto &[msgId, data] : list) {
				stream << qint64(msgId.bare) << data;
			}
		}
	}
	_session->local().writeDeletedMessages(serialized);
}

} // namespace Data
