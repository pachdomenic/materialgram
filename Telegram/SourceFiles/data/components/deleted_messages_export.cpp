/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/deleted_messages_export.h"

#include "data/components/deleted_messages.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace Data {
namespace {

struct FileTask {
	QString source;
	Storage::Cache::Key cacheKey;
	QString target;
	int chat = 0;
	int message = 0;
};

struct ExportState {
	QString path;
	QJsonArray chats;
	std::vector<FileTask> files;
	int messages = 0;
	int written = 0;
	Fn<void(DeletedExportResult)> done;
};

struct WriteState {
	std::vector<DeletedMediaFile> files;
	std::vector<bool> written;
	int index = 0;
	Fn<void(std::vector<bool>)> done;
};

void WriteNext(
		not_null<Main::Session*> session,
		std::shared_ptr<WriteState> state) {
	if (state->index >= int(state->files.size())) {
		state->done(base::take(state->written));
		return;
	}
	const auto index = state->index++;
	const auto source = state->files[index].source;
	const auto cacheKey = state->files[index].cacheKey;
	const auto target = state->files[index].target;
	const auto next = [=](bool written) {
		state->written[index] = written;
		WriteNext(session, state);
	};
	if (!source.isEmpty()) {
		crl::async([=, weak = base::make_weak(session)]() mutable {
			QFile::remove(target);
			const auto written = QFile::copy(source, target);
			crl::on_main(std::move(weak), [=] { next(written); });
		});
		return;
	}
	session->data().cache().get(cacheKey, [=, weak = base::make_weak(
			session)](QByteArray &&bytes) mutable {
		auto written = false;
		if (!bytes.isEmpty()) {
			auto file = QFile(target);
			written = file.open(QIODevice::WriteOnly)
				&& (file.write(bytes) == bytes.size());
		}
		crl::on_main(std::move(weak), [=] { next(written); });
	});
}

[[nodiscard]] QString SerializePeer(PeerId peer) {
	if (const auto chat = peerToChat(peer)) {
		return u"chat"_q + QString::number(chat.bare);
	} else if (const auto channel = peerToChannel(peer)) {
		return u"channel"_q + QString::number(channel.bare);
	}
	return u"user"_q + QString::number(peerToUser(peer).bare);
}

[[nodiscard]] QString PeerType(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return peer->isSelf() ? u"saved_messages"_q : u"personal_chat"_q;
	} else if (peer->isBroadcast()) {
		return u"channel"_q;
	}
	return u"group"_q;
}

[[nodiscard]] QString PeerName(
		not_null<Main::Session*> session,
		PeerId id) {
	const auto peer = id ? session->data().peerLoaded(id) : nullptr;
	return peer ? peer->name() : QString();
}

[[nodiscard]] QString EntityTypeName(EntityType type) {
	switch (type) {
	case EntityType::Url: return u"link"_q;
	case EntityType::CustomUrl: return u"text_link"_q;
	case EntityType::Email: return u"email"_q;
	case EntityType::Hashtag: return u"hashtag"_q;
	case EntityType::Cashtag: return u"cashtag"_q;
	case EntityType::Mention: return u"mention"_q;
	case EntityType::MentionName: return u"mention_name"_q;
	case EntityType::CustomEmoji: return u"custom_emoji"_q;
	case EntityType::BotCommand: return u"bot_command"_q;
	case EntityType::Phone: return u"phone"_q;
	case EntityType::BankCard: return u"bank_card"_q;
	case EntityType::Bold: return u"bold"_q;
	case EntityType::Semibold: return u"bold"_q;
	case EntityType::Italic: return u"italic"_q;
	case EntityType::Underline: return u"underline"_q;
	case EntityType::StrikeOut: return u"strikethrough"_q;
	case EntityType::Code: return u"code"_q;
	case EntityType::Pre: return u"pre"_q;
	case EntityType::Blockquote: return u"blockquote"_q;
	case EntityType::Spoiler: return u"spoiler"_q;
	default: return QString();
	}
}

[[nodiscard]] QJsonArray SerializeEntities(const TextWithEntities &text) {
	auto result = QJsonArray();
	for (const auto &entity : text.entities) {
		const auto type = EntityTypeName(entity.type());
		if (type.isEmpty()) {
			continue;
		}
		auto object = QJsonObject{
			{ u"type"_q, type },
			{ u"offset"_q, entity.offset() },
			{ u"length"_q, entity.length() },
		};
		if (!entity.data().isEmpty()) {
			object.insert(u"data"_q, entity.data());
		}
		result.append(object);
	}
	return result;
}

[[nodiscard]] QString SafeFileName(QString name) {
	static const auto kBad = QRegularExpression(u"[\\\\/:*?\"<>|]"_q);
	name.replace(kBad, u"_"_q);
	return name.trimmed();
}

[[nodiscard]] QJsonObject SerializeMessage(
		not_null<Main::Session*> session,
		const DeletedStoredMessage &message,
		PeerId peer,
		std::vector<FileTask> &files,
		int chatIndex,
		int messageIndex) {
	auto result = QJsonObject{
		{ u"id"_q, qint64(message.id.bare) },
		{
			u"date"_q,
			QDateTime::fromSecsSinceEpoch(message.date).toString(Qt::ISODate)
		},
		{ u"date_unixtime"_q, qint64(message.date) },
		{ u"out"_q, message.out },
		{ u"text"_q, message.text.text },
		{ u"text_entities"_q, SerializeEntities(message.text) },
	};
	if (message.from) {
		result.insert(u"from_id"_q, SerializePeer(message.from));
		result.insert(u"from"_q, PeerName(session, message.from));
	}
	if (!message.author.isEmpty()) {
		result.insert(u"author"_q, message.author);
	}
	if (message.replyTo.msg) {
		result.insert(
			u"reply_to_message_id"_q,
			qint64(message.replyTo.msg.bare));
		if (message.replyTo.peer && message.replyTo.peer != peer) {
			result.insert(
				u"reply_to_peer_id"_q,
				SerializePeer(message.replyTo.peer));
		}
	}
	if (message.groupedId) {
		result.insert(u"grouped_id"_q, QString::number(message.groupedId));
	}
	const auto prefix = u"files/"_q
		+ QString::number(peer.value)
		+ '_'
		+ QString::number(message.id.bare)
		+ '_';
	if (const auto photo = message.photo) {
		const auto key = photo->location(PhotoSize::Large).file().cacheKey();
		const auto target = prefix + u"photo.jpg"_q;
		result.insert(u"media"_q, QJsonObject{
			{ u"type"_q, u"photo"_q },
			{ u"spoiler"_q, message.spoiler },
		});
		if (key) {
			files.push_back({
				.cacheKey = key,
				.target = target,
				.chat = chatIndex,
				.message = messageIndex,
			});
		}
	} else if (const auto document = message.document) {
		const auto name = document->filename();
		auto media = QJsonObject{
			{ u"type"_q, u"document"_q },
			{ u"file_name"_q, name },
			{ u"mime_type"_q, document->mimeString() },
			{ u"size"_q, qint64(document->size) },
			{ u"spoiler"_q, message.spoiler },
		};
		if (const auto duration = document->duration(); duration > 0) {
			media.insert(u"duration_seconds"_q, qint64(duration / 1000));
		}
		result.insert(u"media"_q, media);
		const auto target = prefix + (name.isEmpty()
			? u"file"_q
			: SafeFileName(name));
		const auto local = document->filepath(true);
		if (!local.isEmpty()) {
			files.push_back({
				.source = local,
				.target = target,
				.chat = chatIndex,
				.message = messageIndex,
			});
		} else if (const auto key = document->cacheKey()) {
			files.push_back({
				.cacheKey = key,
				.target = target,
				.chat = chatIndex,
				.message = messageIndex,
			});
		}
	}
	return result;
}

void MarkFileWritten(
		const std::shared_ptr<ExportState> &state,
		const FileTask &task) {
	auto chat = state->chats[task.chat].toObject();
	auto messages = chat[u"messages"_q].toArray();
	auto message = messages[task.message].toObject();
	auto media = message[u"media"_q].toObject();
	media.insert(u"file"_q, task.target);
	message.insert(u"media"_q, media);
	messages[task.message] = message;
	chat.insert(u"messages"_q, messages);
	state->chats[task.chat] = chat;
	++state->written;
}

void Finish(const std::shared_ptr<ExportState> &state) {
	const auto now = QDateTime::currentDateTime();
	const auto root = QJsonObject{
		{ u"about"_q, u"Deleted messages kept by TukiGram."_q },
		{ u"exported"_q, now.toString(Qt::ISODate) },
		{ u"exported_unixtime"_q, now.toSecsSinceEpoch() },
		{ u"chats"_q, state->chats },
	};
	auto file = QFile(QDir(state->path).filePath(u"result.json"_q));
	const auto ok = file.open(QIODevice::WriteOnly)
		&& (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented))
			> 0);
	state->done({
		.path = state->path,
		.messages = state->messages,
		.files = state->written,
		.failed = !ok,
	});
}

} // namespace

void WriteDeletedMediaFiles(
		not_null<Main::Session*> session,
		std::vector<DeletedMediaFile> files,
		Fn<void(std::vector<bool>)> done) {
	auto state = std::make_shared<WriteState>();
	state->written.resize(files.size(), false);
	state->files = std::move(files);
	state->done = std::move(done);
	WriteNext(session, std::move(state));
}

void ExportDeletedMessages(
		not_null<Main::Session*> session,
		const QString &folder,
		Fn<void(DeletedExportResult)> done) {
	const auto name = u"DeletedMessages_"_q
		+ QDateTime::currentDateTime().toString(u"yyyy-MM-dd_HH-mm-ss"_q);
	auto state = std::make_shared<ExportState>();
	state->path = QDir(folder).filePath(name);
	state->done = std::move(done);
	if (!QDir().mkpath(QDir(state->path).filePath(u"files"_q))) {
		state->done({ .path = state->path, .failed = true });
		return;
	}
	auto &deleted = session->deletedMessages();
	for (const auto &peerId : deleted.peers()) {
		const auto chatIndex = int(state->chats.size());
		auto messages = QJsonArray();
		for (const auto &message : deleted.messages(peerId)) {
			messages.append(SerializeMessage(
				session,
				message,
				peerId,
				state->files,
				chatIndex,
				int(messages.size())));
		}
		if (messages.isEmpty()) {
			continue;
		}
		state->messages += int(messages.size());
		auto chat = QJsonObject{
			{ u"id"_q, SerializePeer(peerId) },
			{ u"name"_q, PeerName(session, peerId) },
			{ u"messages"_q, messages },
		};
		if (const auto peer = session->data().peerLoaded(peerId)) {
			chat.insert(u"type"_q, PeerType(peer));
		}
		state->chats.append(chat);
	}
	auto files = std::vector<DeletedMediaFile>();
	files.reserve(state->files.size());
	for (const auto &task : state->files) {
		files.push_back({
			.source = task.source,
			.cacheKey = task.cacheKey,
			.target = QDir(state->path).filePath(task.target),
		});
	}
	WriteDeletedMediaFiles(session, std::move(files), [=](
			std::vector<bool> written) {
		for (auto i = 0; i != int(written.size()); ++i) {
			if (written[i]) {
				MarkFileWritten(state, state->files[i]);
			}
		}
		Finish(state);
	});
}

} // namespace Data
