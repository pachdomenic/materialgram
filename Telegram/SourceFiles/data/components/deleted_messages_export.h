/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_types.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct DeletedMediaFile {
	QString source;
	Storage::Cache::Key cacheKey;
	QString target;
};

void WriteDeletedMediaFiles(
	not_null<Main::Session*> session,
	std::vector<DeletedMediaFile> files,
	Fn<void(std::vector<bool>)> done);

struct DeletedExportResult {
	QString path;
	int messages = 0;
	int files = 0;
	bool failed = false;
};

// materialgram: exports kept deleted messages with locally available media.
void ExportDeletedMessages(
	not_null<Main::Session*> session,
	const QString &folder,
	Fn<void(DeletedExportResult)> done);

} // namespace Data
