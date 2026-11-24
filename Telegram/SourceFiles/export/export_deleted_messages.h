/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Export {
namespace Data {
struct DeletedMessagesSnapshot;
} // namespace Data

struct Settings;

void PrepareDeletedMessages(
	not_null<Main::Session*> session,
	const Settings &settings,
	Fn<void(std::shared_ptr<Data::DeletedMessagesSnapshot>)> done);

} // namespace Export
