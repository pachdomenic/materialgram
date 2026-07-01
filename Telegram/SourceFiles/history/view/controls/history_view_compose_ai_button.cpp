/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_ai_button.h"

#include "ui/effects/premium_graphics.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_chat_helpers.h"

#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

namespace HistoryView::Controls {

ComposeAiButton::ComposeAiButton(
	QWidget *parent,
	const style::IconButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	const auto factor = style::DevicePixelRatio();
	const auto side = st::historyAiComposeButtonStarSize;
	const auto size = QSize(side, side);
	_star = QImage(size * factor, QImage::Format_ARGB32_Premultiplied);
	_star.setDevicePixelRatio(factor);
	_star.fill(Qt::transparent);
	{
		auto q = QPainter(&_star);
		auto svg = QSvgRenderer(
			Ui::Premium::ColorizedSvg(Ui::Premium::ButtonGradientStops()));
		svg.render(&q, QRectF(QPointF(), QSizeF(size)));
	}
}

void ComposeAiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	const auto over = isDown() || isOver() || forceRippled();
	paintRipple(p, _st.rippleAreaPosition);

	const auto &letters = st::historyAiComposeButtonLetters;
	if (over) {
		letters.paintInCenter(p, rect(), st::historyComposeIconFgOver->c);
	} else {
		letters.paintInCenter(p, rect());
	}

	const auto side = st::historyAiComposeButtonStarSize;
	const auto shift = st::historyAiComposeButtonStarPosition;
	const auto left = (width() - side) / 2 + shift.x();
	const auto top = (height() - side) / 2 + shift.y();
	p.drawImage(left, top, _star);
}

void ComposeAiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage ComposeAiButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint ComposeAiButton::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
	const auto rect = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

} // namespace HistoryView::Controls
