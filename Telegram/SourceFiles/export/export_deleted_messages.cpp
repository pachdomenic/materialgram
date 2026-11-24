/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_deleted_messages.h"

#include "api/api_text_entities.h"
#include "base/base_file_utilities.h"
#include "data/components/deleted_messages.h"
#include "data/components/deleted_messages_export.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "export/data/export_data_types.h"
#include "export/export_settings.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Export {
namespace {

struct PendingFile {
	PeerId peer = 0;
	int index = 0;
};

[[nodiscard]] PeerId PeerFromInput(
		not_null<Main::Session*> session,
		const MTPInputPeer &input) {
	return input.match([&](const MTPDinputPeerSelf &) {
		return session->userPeerId();
	}, [](const MTPDinputPeerUser &data) {
		return peerFromUser(UserId(data.vuser_id().v));
	}, [](const MTPDinputPeerChat &data) {
		return peerFromChat(ChatId(data.vchat_id().v));
	}, [](const MTPDinputPeerChannel &data) {
		return peerFromChannel(ChannelId(data.vchannel_id().v));
	}, [](const auto &) {
		return PeerId();
	});
}

[[nodiscard]] Data::Peer ExportPeer(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		auto result = Data::User();
		result.bareId = peerToUser(user->id).bare;
		result.info.userId = peerToUser(user->id);
		result.info.firstName = user->firstName.toUtf8();
		result.info.lastName = user->lastName.toUtf8();
		result.info.phoneNumber = user->phone().toUtf8();
		result.info.colorIndex = user->colorIndex();
		result.colorIndex = user->colorIndex();
		result.username = user->username().toUtf8();
		result.isBot = user->isBot();
		result.isSelf = user->isSelf();
		return Data::Peer{ .data = std::move(result) };
	}
	auto result = Data::Chat();
	result.bareId = peer->isChannel()
		? peerToChannel(peer->id).bare
		: peerToChat(peer->id).bare;
	result.title = peer->name().toUtf8();
	result.username = peer->username().toUtf8();
	result.colorIndex = peer->colorIndex();
	result.isBroadcast = peer->isBroadcast();
	result.isSupergroup = peer->isMegagroup();
	return Data::Peer{ .data = std::move(result) };
}

[[nodiscard]] Data::Media ExportPhoto(
		const ::Data::DeletedStoredMessage &stored,
		not_null<PhotoData*> photo) {
	const auto &large = photo->location(::Data::PhotoSize::Large);
	auto result = Data::Photo();
	result.id = photo->id;
	result.date = stored.date;
	result.spoilered = stored.spoiler;
	result.image.width = large.width();
	result.image.height = large.height();
	result.image.file.size = photo->imageByteSize(::Data::PhotoSize::Large);
	result.image.file.suggestedPath = u"photos/photo_"_q
		+ QString::number(stored.id.bare)
		+ u"_deleted.jpg"_q;
	return Data::Media{ .content = std::move(result) };
}

[[nodiscard]] Data::Media ExportDocument(
		const ::Data::DeletedStoredMessage &stored,
		not_null<DocumentData*> document) {
	auto result = Data::Document();
	result.id = document->id;
	result.date = stored.date;
	result.name = document->filename().toUtf8();
	result.mime = document->mimeString().toUtf8();
	result.width = document->dimensions.width();
	result.height = document->dimensions.height();
	result.duration = int(std::max(document->duration(), crl::time()) / 1000);
	result.isSticker = (document->sticker() != nullptr);
	result.isAnimated = document->isAnimation();
	result.isVideoMessage = document->isVideoMessage();
	result.isVoiceMessage = document->isVoiceMessage();
	result.isVideoFile = document->isVideoFile();
	result.isAudioFile = document->isAudioFile();
	result.spoilered = stored.spoiler;
	result.file.size = document->size;
	const auto name = document->filename().isEmpty()
		? (u"file_"_q + QString::number(document->id))
		: document->filename();
	result.file.suggestedPath = Data::DocumentFolder(result)
		+ '/'
		+ base::FileNameFromUserString(name);
	return Data::Media{ .content = std::move(result) };
}

[[nodiscard]] Data::Message ExportMessage(
		not_null<Main::Session*> session,
		const ::Data::DeletedStoredMessage &stored,
		PeerId peer) {
	auto result = Data::Message();
	result.id = int32(stored.id.bare);
	result.date = stored.date;
	result.peerId = peer;
	result.selfId = session->userPeerId();
	result.fromId = stored.from ? stored.from : peer;
	result.out = stored.out;
	result.deleted = true;
	result.signature = stored.author.toUtf8();
	if (stored.replyTo.msg) {
		result.replyToMsgId = int32(stored.replyTo.msg.bare);
		if (stored.replyTo.peer && stored.replyTo.peer != peer) {
			result.replyToPeerId = stored.replyTo.peer;
		}
	}
	result.text = Data::ParseText(
		MTP_string(stored.text.text),
		Api::EntitiesToMTP(
			session,
			stored.text.entities,
			Api::ConvertOption::SkipLocal).v);
	if (const auto photo = stored.photo) {
		result.media = ExportPhoto(stored, photo);
	} else if (const auto document = stored.document) {
		result.media = ExportDocument(stored, document);
	}
	return result;
}

} // namespace

void PrepareDeletedMessages(
		not_null<Main::Session*> session,
		const Settings &settings,
		Fn<void(std::shared_ptr<Data::DeletedMessagesSnapshot>)> done) {
	if (settings.onlySingleTopic()) {
		done(nullptr);
		return;
	}
	auto &deleted = session->deletedMessages();
	auto peers = deleted.peers();
	if (settings.onlySinglePeer()) {
		const auto single = PeerFromInput(session, settings.singlePeer);
		peers = ranges::contains(peers, single)
			? std::vector<PeerId>{ single }
			: std::vector<PeerId>();
	}
	if (peers.empty()) {
		done(nullptr);
		return;
	}
	const auto folder = QDir(session->local().tempDirectory()).filePath(
		u"deleted_export"_q);
	QDir().mkpath(folder);

	auto snapshot = std::make_shared<Data::DeletedMessagesSnapshot>();
	auto files = std::vector<::Data::DeletedMediaFile>();
	auto pending = std::vector<PendingFile>();
	const auto addPeer = [&](Data::DeletedChat &chat, PeerId id) {
		if (!id || chat.peers.contains(id)) {
			return;
		} else if (const auto peer = session->data().peerLoaded(id)) {
			chat.peers.emplace(id, ExportPeer(peer));
		}
	};
	for (const auto &peerId : peers) {
		auto &chat = snapshot->chats[peerId];
		addPeer(chat, peerId);
		for (const auto &stored : deleted.messages(peerId)) {
			auto message = ExportMessage(session, stored, peerId);
			addPeer(chat, message.fromId);
			const auto target = QDir(folder).filePath(
				QString::number(peerId.value)
				+ '_'
				+ QString::number(stored.id.bare));
			if (const auto photo = stored.photo) {
				const auto key = photo->location(
					::Data::PhotoSize::Large).file().cacheKey();
				if (key) {
					files.push_back({ .cacheKey = key, .target = target });
					pending.push_back({ peerId, int(chat.list.size()) });
				}
			} else if (const auto document = stored.document) {
				const auto local = document->filepath(true);
				if (!local.isEmpty()) {
					message.file().localPath = local;
				} else if (const auto key = document->cacheKey()) {
					files.push_back({ .cacheKey = key, .target = target });
					pending.push_back({ peerId, int(chat.list.size()) });
				}
			}
			chat.list.push_back(std::move(message));
		}
	}
	::Data::WriteDeletedMediaFiles(session, files, [=](
			std::vector<bool> written) {
		for (auto i = 0; i != int(written.size()); ++i) {
			if (written[i]) {
				const auto &entry = pending[i];
				auto &message = snapshot->chats[entry.peer].list[entry.index];
				message.file().localPath = files[i].target;
			}
		}
		done(snapshot);
	});
}

} // namespace Export
