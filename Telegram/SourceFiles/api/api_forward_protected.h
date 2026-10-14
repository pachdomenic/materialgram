/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Data {
struct ResolvedForwardDraft;
} // namespace Data

namespace Api {

struct SendAction;

[[nodiscard]] bool CanForwardProtected(not_null<const HistoryItem*> item);
[[nodiscard]] bool NeedsProtectedResend(not_null<const HistoryItem*> item);

// materialgram: the server refuses to forward protected content.
void ForwardWithProtected(
	Data::ResolvedForwardDraft &&draft,
	const SendAction &action,
	FnMut<void()> &&done);

} // namespace Api
