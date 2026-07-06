/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/elastic_scroll.h"

namespace Ui {

// This class is designed for seamless scrolling of
// on-demand augmented content.
class ContinuousScroll final : public ElasticScroll {
public:
	ContinuousScroll(
		QWidget *parent,
		const style::ScrollArea &st = st::defaultScrollArea,
		Qt::Orientation orientation = Qt::Vertical);

	void keyPressEvent(QKeyEvent *e) override;

	[[nodiscard]] rpl::producer<> addContentRequests() const;
	void contentAdded();

	void setTrackingContent(bool value);

private:
	bool _contentAdded = false;
	bool _tracking = false;

	rpl::event_stream<> _addContentRequests;

};

} // namespace Ui
