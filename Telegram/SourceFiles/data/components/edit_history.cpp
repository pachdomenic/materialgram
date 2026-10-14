/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/edit_history.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer_id.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kSaveDelay = 5 * crl::time(1000);
constexpr auto kFormatVersion = 1;
constexpr auto kMaxVersionsPerMessage = 50;
constexpr auto kMaxMessages = 20000;

} // namespace

EditHistory::EditHistory(not_null<Main::Session*> session)
: _session(session)
, _saveTimer([=] { save(); }) {
}

EditHistory::~EditHistory() {
	if (_saveTimer.isActive()) {
		save();
	}
}

void EditHistory::remember(
		not_null<HistoryItem*> item,
		TextWithEntities was,
		TimeId wasDate) {
	if (!Core::App().settings().saveEditHistory()
		|| item->isLocal()
		|| item->isService()) {
		return;
	}
	ensureLoaded();
	auto &list = _versions[item->fullId()];
	if (!list.empty() && list.back().text == was) {
		return;
	}
	list.push_back({ .text = std::move(was), .date = wasDate });
	if (int(list.size()) > kMaxVersionsPerMessage) {
		list.erase(begin(list));
	}
	if (int(_versions.size()) > kMaxMessages) {
		_versions.erase(begin(_versions));
	}
	_saveTimer.callOnce(kSaveDelay);
}

std::vector<EditVersion> EditHistory::lookup(FullMsgId id) {
	ensureLoaded();
	const auto i = _versions.find(id);
	return (i != end(_versions)) ? i->second : std::vector<EditVersion>();
}

bool EditHistory::has(FullMsgId id) {
	ensureLoaded();
	return _versions.contains(id);
}

void EditHistory::ensureLoaded() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	const auto serialized = _session->local().readEditHistory();
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto count = qint32();
	stream >> version >> count;
	if (version != kFormatVersion || count < 0) {
		return;
	}
	for (auto i = 0; i != count; ++i) {
		auto peer = quint64();
		auto msg = qint64();
		auto size = qint32();
		stream >> peer >> msg >> size;
		if (stream.status() != QDataStream::Ok || size < 0) {
			break;
		}
		auto list = std::vector<EditVersion>();
		list.reserve(size);
		for (auto j = 0; j != size; ++j) {
			auto date = qint32();
			auto text = QString();
			auto tags = QByteArray();
			stream >> date >> text >> tags;
			if (stream.status() != QDataStream::Ok) {
				return;
			}
			const auto entities = TextUtilities::ConvertTextTagsToEntities(
				TextUtilities::DeserializeTags(tags, text.size()));
			list.push_back({
				.text = { std::move(text), entities },
				.date = TimeId(date),
			});
		}
		_versions.emplace(
			FullMsgId(DeserializePeerId(peer), MsgId(msg)),
			std::move(list));
	}
}

void EditHistory::save() {
	_saveTimer.cancel();
	auto serialized = QByteArray();
	{
		auto stream = QDataStream(&serialized, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(kFormatVersion) << qint32(_versions.size());
		for (const auto &[id, list] : _versions) {
			stream
				<< quint64(SerializePeerId(id.peer))
				<< qint64(id.msg.bare)
				<< qint32(list.size());
			for (const auto &version : list) {
				stream
					<< qint32(version.date)
					<< version.text.text
					<< TextUtilities::SerializeTags(
						TextUtilities::ConvertEntitiesToTextTags(
							version.text.entities));
			}
		}
	}
	_session->local().writeEditHistory(serialized);
}

} // namespace Data
