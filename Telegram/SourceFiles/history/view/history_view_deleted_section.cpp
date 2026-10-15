/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_deleted_section.h"

#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_view_swipe_back_session.h"
#include "data/components/deleted_messages.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/shadow.h"
#include "ui/chat/chat_style.h"
#include "ui/ui_utility.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

namespace HistoryView {

DeletedMemento::DeletedMemento(not_null<History*> history)
: _history(history) {
	_list.setAroundPosition({
		.fullId = FullMsgId(_history->peer->id, MsgId()),
		.date = TimeId(0),
	});
}

object_ptr<Window::SectionWidget> DeletedMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<DeletedWidget>(parent, controller, _history);
	result->setInternalState(geometry, this);
	return result;
}

DeletedWidget::DeletedWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller, history->peer)
, WindowListDelegate(controller)
, _history(history)
, _topBar(this, controller)
, _topBarShadow(this)
, _scroll(std::make_unique<Ui::ElasticScroll>(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll)))
, _clearButton(std::make_unique<Ui::FlatButton>(
	this,
	tr::lng_tukigram_deleted_history_clear(tr::now),
	st::historyComposeButton))
, _cornerButtons(
		_scroll.get(),
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::on_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		history->peer
	) | rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	_topBar->setActiveChat(
		TopBarWidget::ActiveChat{
			.key = _history,
			.section = Dialogs::EntryState::Section::Pinned,
		},
		nullptr);
	_topBar->setSearchAllowed(false);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();
	_topBar->setCustomTitle(tr::lng_tukigram_deleted_title(tr::now));

	_topBar->deleteSelectionRequest(
	) | rpl::on_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::on_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	controller->adaptive().value(
	) | rpl::on_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_scroll->setHandleTouch(false);
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_inner->lower();
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_scroll->setOverscrollEdges([=] {
		return _inner->loadedAtTopKnown() && _inner->loadedAtTop();
	}, [=] {
		return _inner->loadedAtBottomKnown() && _inner->loadedAtBottom();
	});
	_scroll->scrolls(
	) | rpl::on_next([=] {
		onScroll();
	}, lifetime());

	_inner->scrollKeyEvents(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());

	setupClearButton();
	Window::SetupSwipeBackSection(this, _scroll.get(), _inner);
}

DeletedWidget::~DeletedWidget() = default;

void DeletedWidget::setupClearButton() {
	_clearButton->setClickedCallback([=] {
		confirmClearAll();
	});
}

void DeletedWidget::confirmClearAll() {
	controller()->show(Ui::MakeConfirmBox({
		.text = tr::lng_tukigram_deleted_history_clear_sure(),
		.confirmed = crl::guard(this, [=](Fn<void()> &&close) {
			close();
			clearAll();
		}),
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void DeletedWidget::clearAll() {
	const auto peer = _history->peer;
	const auto owner = &_history->owner();
	const auto deleted = &_history->session().deletedMessages();
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto id : deleted->ids(peer->id)) {
		const auto item = owner->message(peer, id);
		if (item && owner->isMessageDeleted(item)) {
			items.push_back(item);
		}
	}
	deleted->forgetAll(peer->id);
	if (!items.empty()) {
		owner->notifyItemsAboutToBeDestroyed(items);
		for (const auto &item : items) {
			item->destroy();
		}
	}
	if (!_history->chatListMessageKnown()) {
		_history->requestChatListMessage();
	}
}

void DeletedWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	_inner->showAtPosition(position, {});
}

Data::Thread *DeletedWidget::cornerButtonsThread() {
	return _history;
}

FullMsgId DeletedWidget::cornerButtonsCurrentId() {
	return {};
}

bool DeletedWidget::cornerButtonsIgnoreVisibility() {
	return animatingShow();
}

std::optional<bool> DeletedWidget::cornerButtonsDownShown() {
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax()) {
		return true;
	} else if (_inner->loadedAtBottomKnown()) {
		return !_inner->loadedAtBottom();
	}
	return std::nullopt;
}

bool DeletedWidget::cornerButtonsUnreadMayBeShown() {
	return false;
}

bool DeletedWidget::cornerButtonsHas(CornerButtonType type) {
	return (type == CornerButtonType::Down);
}

void DeletedWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> DeletedWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor DeletedWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->peer->id, ShowAtUnreadMsgId)
	};
}

QPixmap DeletedWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void DeletedWidget::checkActivation() {
	_inner->checkActivation();
}

void DeletedWidget::doSetInnerFocus() {
	_inner->setFocus();
}

bool DeletedWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (const auto deleted = dynamic_cast<DeletedMemento*>(memento.get())) {
		if (deleted->getHistory() == _history) {
			restoreState(deleted);
			return true;
		}
	}
	return false;
}

void DeletedWidget::setInternalState(
		const QRect &geometry,
		not_null<DeletedMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<Window::SectionMemento> DeletedWidget::createMemento() {
	auto result = std::make_shared<DeletedMemento>(_history);
	saveState(result.get());
	return result;
}

bool DeletedWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	return false;
}

void DeletedWidget::saveState(not_null<DeletedMemento*> memento) {
	_inner->saveState(memento->list());
}

void DeletedWidget::restoreState(not_null<DeletedMemento*> memento) {
	_inner->restoreState(memento->list());
}

void DeletedWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	recountChatWidth();
	updateControlsGeometry();
}

void DeletedWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
}

void DeletedWidget::setMessagesCount(int count) {
	if (_messagesCount == count) {
		return;
	}
	_messagesCount = count;
	_topBar->setCustomTitle(
		tr::lng_tukigram_deleted_history_title(tr::now, lt_count, count));
}

void DeletedWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + takeTopDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height() - _clearButton->height();
	_clearButton->resizeToWidth(contentWidth);
	_clearButton->move(0, bottom);
	const auto top = _topBar->height();
	const auto scrollSize = QSize(contentWidth, bottom - top);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, top);
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}

	_cornerButtons.updatePositions();
}

void DeletedWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}

	const auto aboveHeight = _topBar->height();
	const auto bg = e->rect().intersected(
		QRect(0, aboveHeight, width(), height() - aboveHeight));
	SectionWidget::PaintBackground(controller(), _theme.get(), this, bg);
}

void DeletedWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void DeletedWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_cornerButtons.updateJumpDownVisibility();
}

void DeletedWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
}

void DeletedWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_inner->showFinished();
}

bool DeletedWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect DeletedWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context DeletedWidget::listContext() {
	return Context::Pinned;
}

bool DeletedWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void DeletedWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedIds().empty()) {
		clearSelected();
		return;
	}
	controller()->showBackFromStack();
}

void DeletedWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

void DeletedWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

Data::MessagesSlice DeletedWidget::buildSlice(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto deleted = &_history->session().deletedMessages();
	const auto ids = deleted->ids(_history->peer->id);
	const auto count = int(ids.size());
	auto result = Data::MessagesSlice();
	result.fullCount = count;
	if (!count) {
		result.skippedBefore = result.skippedAfter = 0;
		return result;
	}
	const auto around = aroundId.fullId.msg
		? aroundId.fullId.msg
		: (ServerMaxMsgId - 1);
	const auto index = std::min(
		int(ranges::lower_bound(ids, around) - begin(ids)),
		count - 1);
	const auto from = std::max(index - limitBefore, 0);
	const auto till = std::min(index + limitAfter + 1, count);
	result.skippedBefore = from;
	result.skippedAfter = count - till;
	result.nearestToAround = FullMsgId(_history->peer->id, ids[index]);
	result.ids.reserve(till - from);
	for (auto i = from; i != till; ++i) {
		if (const auto item = deleted->lookup(_history, ids[i])) {
			result.ids.push_back(item->fullId());
		}
	}
	return result;
}

rpl::producer<Data::MessagesSlice> DeletedWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto peerId = _history->peer->id;
	return rpl::single(
		peerId
	) | rpl::then(
		_history->session().deletedMessages().changes()
	) | rpl::filter(
		rpl::mappers::_1 == peerId
	) | rpl::map([=] {
		return buildSlice(aroundId, limitBefore, limitAfter);
	}) | rpl::filter([=](const Data::MessagesSlice &slice) {
		if (*slice.fullCount) {
			setMessagesCount(*slice.fullCount);
			return true;
		} else if (!_closing) {
			_closing = true;
			crl::on_main(this, [=] {
				controller()->showBackFromStack();
			});
		}
		return false;
	});
}

bool DeletedWidget::listAllowsMultiSelect() {
	return true;
}

bool DeletedWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->canBeSelected();
}

bool DeletedWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void DeletedWidget::listSelectionChanged(SelectedItems &&items) {
	auto state = TopBarWidget::SelectedState();
	state.count = items.size();
	state.canDeleteCount = int(ranges::count(
		items,
		true,
		&SelectedItem::canDelete));
	_topBar->showSelected(state);
}

void DeletedWidget::listMarkReadTill(not_null<HistoryItem*> item) {
}

void DeletedWidget::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData DeletedWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void DeletedWidget::listContentRefreshed() {
}

void DeletedWidget::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool DeletedWidget::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool DeletedWidget::listElementShownUnread(not_null<const Element*> view) {
	return false;
}

bool DeletedWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return view->data()->isRegular();
}

void DeletedWidget::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void DeletedWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = _history->peer->isUser()
		? Dialogs::Key()
		: Dialogs::Key(_history);
	controller()->searchMessages(query, inChat);
}

void DeletedWidget::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> DeletedWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType DeletedWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType DeletedWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType DeletedWidget::listSelectRestrictionType() {
	return SelectRestrictionTypeFor(_history->peer);
}

auto DeletedWidget::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void DeletedWidget::listShowPremiumToast(not_null<DocumentData*> document) {
}

void DeletedWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { .id = context });
}

void DeletedWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { .id = context });
}

void DeletedWidget::listPaintEmpty(
	Painter &p,
	const Ui::ChatPaintContext &context) {
}

QString DeletedWidget::listElementAuthorRank(not_null<const Element*> view) {
	return {};
}

bool DeletedWidget::listElementHideTopicButton(
		not_null<const Element*> view) {
	return true;
}

History *DeletedWidget::listTranslateHistory() {
	return nullptr;
}

void DeletedWidget::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
}

Ui::ElasticScroll *DeletedWidget::listScrollArea() const {
	return _scroll.get();
}

bool DeletedWidget::listThanosEffectEnabled() const {
	return false;
}

void DeletedWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void DeletedWidget::clearSelected() {
	_inner->cancelSelection();
}

} // namespace HistoryView
