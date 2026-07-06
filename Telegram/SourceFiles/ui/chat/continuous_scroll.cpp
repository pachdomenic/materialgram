/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/continuous_scroll.h"

#include <QWheelEvent>
#include <QKeyEvent>

namespace Ui {

ContinuousScroll::ContinuousScroll(
	QWidget *parent,
	const style::ScrollArea &st,
	Qt::Orientation orientation)
: ElasticScroll(parent, st, orientation) {
	setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		if (_tracking
			&& !e->angleDelta().isNull()
			&& (e->angleDelta().y() < 0)
			&& (scrollTopMax() == scrollTop())) {
			_addContentRequests.fire({});
			// Carry into freshly appended content, else swallow the over-scroll.
			return !base::take(_contentAdded);
		}
		return false;
	});
}

void ContinuousScroll::keyPressEvent(QKeyEvent *e) {
	const auto key = e->key();
	if (_tracking
		&& (key == Qt::Key_Down || key == Qt::Key_PageDown)
		&& (scrollTopMax() == scrollTop())) {
		_addContentRequests.fire({});
		base::take(_contentAdded);
	}
	ElasticScroll::keyPressEvent(e);
}

void ContinuousScroll::setTrackingContent(bool value) {
	_tracking = value;
}

void ContinuousScroll::contentAdded() {
	_contentAdded = true;
}

rpl::producer<> ContinuousScroll::addContentRequests() const {
	return _addContentRequests.events();
}

} // namespace Ui
