/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct EditVersion {
	TextWithEntities text;
	TimeId date = 0;
};

// materialgram: previous texts of edited messages, kept on disk.
class EditHistory final {
public:
	explicit EditHistory(not_null<Main::Session*> session);
	~EditHistory();

	void remember(
		not_null<HistoryItem*> item,
		TextWithEntities was,
		TimeId wasDate);
	[[nodiscard]] std::vector<EditVersion> lookup(FullMsgId id);
	[[nodiscard]] bool has(FullMsgId id);

private:
	void ensureLoaded();
	void save();

	const not_null<Main::Session*> _session;
	base::flat_map<FullMsgId, std::vector<EditVersion>> _versions;
	base::Timer _saveTimer;
	bool _loaded = false;

};

} // namespace Data
