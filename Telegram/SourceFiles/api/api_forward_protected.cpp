/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_forward_protected.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/base_file_utilities.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_prepare.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QMimeDatabase>

namespace Api {
namespace {

constexpr auto kCheckDelay = crl::time(250);
constexpr auto kSendStartTimeout = 60 * crl::time(1000);
constexpr auto kForwardTimeout = 30 * crl::time(1000);

enum class UnitType {
	Forward,
	Text,
	Media,
	Sticker,
	Voice,
};

struct Unit {
	UnitType type = UnitType::Forward;
	std::vector<FullMsgId> ids;
	std::vector<TextWithTags> captions;
	TextWithTags text;
	DocumentData *sticker = nullptr;
	MessageGroupId groupId;
	bool invertCaption = false;
};

struct Download {
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	std::shared_ptr<Data::PhotoMedia> photoView;
	std::shared_ptr<Data::DocumentMedia> documentView;
	FullMsgId origin;
	Ui::PreparedFile::Type type = Ui::PreparedFile::Type::File;
	QString displayName;
	QString folder;
	QString path;
	bool spoiler = false;
	bool started = false;
	bool finished = false;
};

[[nodiscard]] bool IsProtectedContent(not_null<const HistoryItem*> item) {
	return item->forbidsForward()
		|| !item->history()->peer->allowsForwarding();
}

[[nodiscard]] TextWithTags ItemText(not_null<const HistoryItem*> item) {
	const auto &text = item->originalText();
	return {
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
}

[[nodiscard]] bool FileReady(const QString &path, int64 size) {
	const auto info = QFileInfo(path);
	return !path.isEmpty()
		&& info.isFile()
		&& (size ? (info.size() == size) : (info.size() > 0));
}

[[nodiscard]] QString DocumentDisplayName(not_null<DocumentData*> document) {
	const auto name = base::FileNameFromUserString(document->filename());
	if (!name.isEmpty()) {
		return name;
	} else if (document->isVoiceMessage()) {
		return u"audio.ogg"_q;
	} else if (document->isVideoMessage()
		|| document->isVideoFile()
		|| document->isGifv()) {
		return u"video.mp4"_q;
	}
	const auto suffix = QMimeDatabase().mimeTypeForName(
		document->mimeString()).preferredSuffix();
	return suffix.isEmpty() ? u"file"_q : (u"file."_q + suffix);
}

[[nodiscard]] Ui::PreparedFile::Type DocumentFileType(
		not_null<DocumentData*> document) {
	using Type = Ui::PreparedFile::Type;
	return (document->isVideoFile() || document->isGifv())
		? Type::Video
		: (document->isSong() || document->isAudioFile())
		? Type::Music
		: Type::File;
}

[[nodiscard]] std::shared_ptr<ChatHelpers::Show> ShowFor(
		not_null<PeerData*> peer) {
	if (const auto window = Core::App().windowFor(peer)) {
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				return controller->uiShow();
			}
		}
	}
	return nullptr;
}

class Forwarder final : public std::enable_shared_from_this<Forwarder> {
public:
	Forwarder(
		Data::ResolvedForwardDraft &&draft,
		const SendAction &action,
		FnMut<void()> &&done);

	void tick();

private:
	void buildUnits(const HistoryItemsList &items);
	void addDownload(
		not_null<HistoryItem*> item,
		PhotoData *photo,
		DocumentData *document);
	void addTextUnit(TextWithTags text);
	void moveLongCaptions();

	[[nodiscard]] QString makeFolder() const;
	void updateDownload(Download &download);
	[[nodiscard]] bool updateDownloads();
	[[nodiscard]] bool unitDownloaded(const Unit &unit) const;

	void sendUnit(const Unit &unit);
	void sendForward(const Unit &unit);
	void sendMedia(const Unit &unit);
	void sendVoice(const Unit &unit);
	void sendText(TextWithTags text);
	[[nodiscard]] SendAction actionFor(const Unit &unit) const;

	void startWaiting(bool trackLocal);
	[[nodiscard]] bool sendFinished();
	void finishUnit();
	void finish();
	void schedule();

	const not_null<Main::Session*> _session;
	const not_null<History*> _history;
	const SendAction _action;
	const Data::ForwardOptions _options;
	FnMut<void()> _done;

	std::vector<Unit> _units;
	base::flat_map<FullMsgId, Download> _downloads;
	int _current = 0;

	base::flat_set<MsgId> _before;
	crl::time _sentAt = 0;
	bool _waiting = false;
	bool _trackLocal = false;
	bool _seenLocal = false;
	bool _unitSent = false;
	bool _forwardDone = false;
	bool _loadingToastShown = false;
	bool _failed = false;

};

Forwarder::Forwarder(
	Data::ResolvedForwardDraft &&draft,
	const SendAction &action,
	FnMut<void()> &&done)
: _session(&action.history->session())
, _history(action.history)
, _action(action)
, _options(draft.options)
, _done(std::move(done)) {
	buildUnits(draft.items);
	moveLongCaptions();
}

void Forwarder::buildUnits(const HistoryItemsList &items) {
	const auto dropCaptions = (_options
		== Data::ForwardOptions::NoNamesAndCaptions);
	for (const auto &item : items) {
		if (!NeedsProtectedResend(item)) {
			if (_units.empty() || _units.back().type != UnitType::Forward) {
				_units.push_back({ .type = UnitType::Forward });
			}
			_units.back().ids.push_back(item->fullId());
			continue;
		}
		const auto media = item->media();
		const auto webpage = media && media->webpage();
		const auto photo = (media && !webpage) ? media->photo() : nullptr;
		const auto document = (media && !webpage)
			? media->document()
			: nullptr;
		const auto caption = dropCaptions ? TextWithTags() : ItemText(item);
		if (!photo && !document) {
			addTextUnit(ItemText(item));
		} else if (document && document->sticker()) {
			_units.push_back({
				.type = UnitType::Sticker,
				.ids = { item->fullId() },
				.sticker = document,
			});
		} else if (document
			&& (document->isVoiceMessage() || document->isVideoMessage())) {
			_units.push_back({
				.type = UnitType::Voice,
				.ids = { item->fullId() },
				.captions = { caption },
			});
			addDownload(item, nullptr, document);
		} else {
			const auto groupId = item->groupId();
			const auto joins = groupId
				&& !_units.empty()
				&& (_units.back().type == UnitType::Media)
				&& (_units.back().groupId == groupId);
			if (!joins) {
				_units.push_back({
					.type = UnitType::Media,
					.groupId = groupId,
					.invertCaption = item->invertMedia(),
				});
			}
			_units.back().ids.push_back(item->fullId());
			_units.back().captions.push_back(caption);
			addDownload(item, photo, document);
		}
	}
}

void Forwarder::addDownload(
		not_null<HistoryItem*> item,
		PhotoData *photo,
		DocumentData *document) {
	auto &download = _downloads[item->fullId()];
	download.origin = item->fullId();
	download.spoiler = item->media()->hasSpoiler();
	if (photo) {
		download.photo = photo;
		download.photoView = photo->createMediaView();
		download.type = Ui::PreparedFile::Type::Photo;
		download.displayName = u"photo.jpg"_q;
	} else {
		download.document = document;
		download.documentView = document->createMediaView();
		download.type = DocumentFileType(document);
		download.displayName = DocumentDisplayName(document);
	}
}

void Forwarder::addTextUnit(TextWithTags text) {
	if (!text.text.isEmpty()) {
		_units.push_back({ .type = UnitType::Text, .text = std::move(text) });
	}
}

void Forwarder::moveLongCaptions() {
	const auto limit = Data::PremiumLimits(_session).captionLengthCurrent();
	for (auto i = 0; i != int(_units.size()); ++i) {
		auto overflow = std::vector<TextWithTags>();
		for (auto &caption : _units[i].captions) {
			if (caption.text.size() > limit) {
				overflow.push_back(base::take(caption));
			}
		}
		for (auto &text : overflow) {
			_units.insert(begin(_units) + (++i), Unit{
				.type = UnitType::Text,
				.text = std::move(text),
			});
		}
	}
}

QString Forwarder::makeFolder() const {
	const auto result = _session->local().tempDirectory()
		+ u"forward/"_q
		+ QString::number(base::RandomValue<uint64>())
		+ '/';
	QDir().mkpath(result);
	return result;
}

void Forwarder::updateDownload(Download &download) {
	if (download.finished) {
		return;
	} else if (const auto photo = download.photo) {
		const auto view = download.photoView.get();
		if (!view->loaded() && !download.started) {
			download.started = true;
			view->wanted(Data::PhotoSize::Large, download.origin);
			return;
		} else if (!view->loaded() && photo->loading()) {
			return;
		}
		download.folder = makeFolder();
		const auto path = download.folder + download.displayName;
		if (view->saveToFile(path)) {
			download.path = path;
		}
		download.finished = true;
	} else if (const auto document = download.document) {
		const auto existing = document->filepath(true);
		if (FileReady(existing, document->size)) {
			download.path = existing;
			download.finished = true;
			return;
		} else if (document->loading()) {
			return;
		} else if (!download.started) {
			download.started = true;
			download.folder = makeFolder();
			document->save(
				download.origin,
				download.folder + download.displayName);
			return;
		}
		const auto path = download.folder + download.displayName;
		if (FileReady(path, document->size)) {
			download.path = path;
		}
		download.finished = true;
	}
	if (download.path.isEmpty()) {
		_failed = true;
	}
}

bool Forwarder::updateDownloads() {
	auto result = true;
	for (auto &[id, download] : _downloads) {
		updateDownload(download);
		result = result && download.finished;
	}
	return result;
}

bool Forwarder::unitDownloaded(const Unit &unit) const {
	return ranges::all_of(unit.ids, [&](FullMsgId id) {
		const auto i = _downloads.find(id);
		return (i == end(_downloads)) || i->second.finished;
	});
}

SendAction Forwarder::actionFor(const Unit &unit) const {
	auto result = _action;
	result.clearDraft = false;
	result.options.effectId = 0;
	result.options.invertCaption = unit.invertCaption;
	return result;
}

void Forwarder::sendUnit(const Unit &unit) {
	switch (unit.type) {
	case UnitType::Forward: sendForward(unit); return;
	case UnitType::Text: sendText(unit.text); return;
	case UnitType::Media: sendMedia(unit); return;
	case UnitType::Voice: sendVoice(unit); return;
	case UnitType::Sticker:
		startWaiting(true);
		SendExistingDocument(MessageToSend(actionFor(unit)), unit.sticker);
		return;
	}
}

void Forwarder::sendForward(const Unit &unit) {
	auto items = _session->data().idsToItems(unit.ids);
	startWaiting(false);
	if (items.empty()) {
		_forwardDone = true;
		return;
	}
	const auto weak = weak_from_this();
	_session->api().forwardMessages(
		{ .items = std::move(items), .options = _options },
		_action,
		[=] {
			if (const auto strong = weak.lock()) {
				strong->_forwardDone = true;
			}
		});
}

void Forwarder::sendMedia(const Unit &unit) {
	auto list = Ui::PreparedList();
	auto fallback = TextWithTags();
	auto allVisual = true;
	for (auto i = 0; i != int(unit.ids.size()); ++i) {
		const auto &download = _downloads[unit.ids[i]];
		if (download.path.isEmpty()) {
			if (fallback.text.isEmpty()) {
				fallback = unit.captions[i];
			}
			continue;
		}
		auto file = Ui::PreparedFile(download.path);
		file.displayName = download.displayName;
		file.size = QFileInfo(download.path).size();
		file.type = download.type;
		file.caption = unit.captions[i];
		file.spoiler = download.spoiler;
		file.sendLargePhotos = true;
		allVisual = allVisual
			&& ((file.type == Ui::PreparedFile::Type::Photo)
				|| (file.type == Ui::PreparedFile::Type::Video));
		list.files.push_back(std::move(file));
	}
	if (list.files.empty()) {
		sendText(std::move(fallback));
		return;
	}
	const auto album = (list.files.size() > 1)
		? std::make_shared<SendingAlbum>()
		: nullptr;
	const auto type = allVisual ? SendMediaType::Photo : SendMediaType::File;
	startWaiting(true);
	_session->api().sendFiles(std::move(list), type, album, actionFor(unit));
}

void Forwarder::sendVoice(const Unit &unit) {
	const auto &download = _downloads[unit.ids.front()];
	auto file = QFile(download.path);
	auto bytes = file.open(QIODevice::ReadOnly)
		? file.readAll()
		: QByteArray();
	if (bytes.isEmpty()) {
		sendText(unit.captions.front());
		return;
	}
	const auto document = download.document;
	const auto voice = document->voice();
	startWaiting(true);
	_session->api().sendVoiceMessage(
		std::move(bytes),
		voice ? voice->waveform : VoiceWaveform(),
		document->duration(),
		document->isVideoMessage(),
		actionFor(unit),
		unit.captions.front());
}

void Forwarder::sendText(TextWithTags text) {
	if (text.text.isEmpty()) {
		startWaiting(false);
		_forwardDone = true;
		return;
	}
	auto message = MessageToSend(actionFor({}));
	message.textWithTags = std::move(text);
	startWaiting(true);
	_session->api().sendMessage(std::move(message));
}

void Forwarder::startWaiting(bool trackLocal) {
	const auto scheduled = _action.options.scheduled
		|| _action.options.shortcutId;
	_waiting = true;
	_trackLocal = trackLocal && !scheduled;
	_seenLocal = false;
	_unitSent = false;
	_forwardDone = trackLocal && scheduled;
	_sentAt = crl::now();
	_before.clear();
	if (_trackLocal) {
		for (const auto &item : _history->clientSideMessages()) {
			_before.emplace(item->id);
		}
	}
}

bool Forwarder::sendFinished() {
	const auto passed = crl::now() - _sentAt;
	if (!_trackLocal) {
		return _forwardDone || (passed >= kForwardTimeout);
	}
	auto fresh = std::vector<not_null<HistoryItem*>>();
	for (const auto &item : _history->clientSideMessages()) {
		if (!_before.contains(item->id)) {
			fresh.push_back(item);
		}
	}
	if (!fresh.empty()) {
		_seenLocal = true;
		return ranges::none_of(fresh, [](not_null<HistoryItem*> item) {
			return item->isSending();
		});
	} else if (_seenLocal) {
		_unitSent = true;
		return true;
	}
	return (passed >= kSendStartTimeout);
}

void Forwarder::finishUnit() {
	if (_unitSent) {
		for (const auto &id : _units[_current].ids) {
			const auto i = _downloads.find(id);
			if (i != end(_downloads) && !i->second.folder.isEmpty()) {
				QDir(i->second.folder).removeRecursively();
			}
		}
	}
	_waiting = false;
	++_current;
}

void Forwarder::tick() {
	const auto downloaded = updateDownloads();
	if (!downloaded && !_loadingToastShown) {
		_loadingToastShown = true;
		if (const auto show = ShowFor(_history->peer)) {
			show->showToast(
				tr::lng_materialgram_forward_protected_loading(tr::now));
		}
	}
	while (_current < int(_units.size())) {
		if (_waiting) {
			if (!sendFinished()) {
				break;
			}
			finishUnit();
			continue;
		}
		const auto &unit = _units[_current];
		if (!unitDownloaded(unit)) {
			break;
		}
		sendUnit(unit);
	}
	if (_current < int(_units.size())) {
		schedule();
	} else {
		finish();
	}
}

void Forwarder::schedule() {
	base::call_delayed(kCheckDelay, _session, [self = shared_from_this()] {
		self->tick();
	});
}

void Forwarder::finish() {
	if (_failed) {
		if (const auto show = ShowFor(_history->peer)) {
			show->showToast(
				tr::lng_materialgram_forward_protected_failed(tr::now));
		}
	}
	if (_done) {
		base::take(_done)();
	}
}

} // namespace

bool CanForwardProtected(not_null<const HistoryItem*> item) {
	if (!Core::App().settings().forwardProtectedMessages()
		|| !item->isRegular()
		|| item->isService()
		|| item->richPage()) {
		return false;
	}
	const auto media = item->media();
	return !media
		|| media->webpage()
		|| ((dynamic_cast<const Data::MediaPhoto*>(media)
				|| dynamic_cast<const Data::MediaFile*>(media))
			&& media->allowsForward());
}

bool NeedsProtectedResend(not_null<const HistoryItem*> item) {
	return IsProtectedContent(item) && CanForwardProtected(item);
}

void ForwardWithProtected(
		Data::ResolvedForwardDraft &&draft,
		const SendAction &action,
		FnMut<void()> &&done) {
	std::make_shared<Forwarder>(
		std::move(draft),
		action,
		std::move(done))->tick();
}

} // namespace Api
