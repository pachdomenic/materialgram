/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_edit_history.h"

#include "base/unixtime.h"
#include "data/components/edit_history.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

void EditHistoryBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		FullMsgId id) {
	box->setTitle(tr::lng_materialgram_edit_history());
	box->setWidth(st::boxWideWidth);

	auto versions = session->editHistory().lookup(id);
	if (const auto item = session->data().message(id)) {
		const auto edited = item->Get<HistoryMessageEdited>();
		versions.push_back({
			.text = item->originalText(),
			.date = edited ? edited->date : item->date(),
		});
	}
	const auto content = box->verticalLayout();
	for (auto i = 0; i != int(versions.size()); ++i) {
		const auto &version = versions[i];
		auto title = langDateTimeFull(base::unixtime::parse(version.date));
		if (i + 1 == int(versions.size())) {
			title = tr::lng_materialgram_edit_history_current(tr::now)
				+ u" · "_q
				+ title;
		}
		Ui::AddSubsectionTitle(content, rpl::single(title));
		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(version.text),
				st::boxLabel),
			st::boxRowPadding);
		label->setSelectable(true);
	}
	Ui::AddSkip(content);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void AddEditHistoryAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	if (!session->editHistory().has(id)) {
		return;
	}
	menu->addAction(tr::lng_materialgram_edit_history(tr::now), [=] {
		controller->show(Box(EditHistoryBox, session, id));
	}, &st::menuIconEdit);
}

} // namespace HistoryView
