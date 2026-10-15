/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_tukigram.h"

#include "settings/settings_common_session.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_bottom_info.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "storage/localstorage.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kFakeUserId = UserId(0xFFFFFFF001ULL);
constexpr auto kMaxDeletedMarkLength = 16;
constexpr auto kGhostModeOptions = std::array{
	Core::GhostOption::HideReads,
	Core::GhostOption::HideStoryViews,
	Core::GhostOption::HideOnline,
	Core::GhostOption::HideTyping,
	Core::GhostOption::OfflineAfterSend,
};

class PreviewDelegate final : public HistoryView::DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update)
	: _parent(parent)
	, _pathGradient(HistoryView::MakePathShiftGradient(st, update)) {
	}

	bool elementAnimationsPaused() override {
		return _parent->window()->isActiveWindow();
	}
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override {
		return _pathGradient.get();
	}
	HistoryView::Context elementContext() override {
		return HistoryView::Context::ChatPreview;
	}

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

class DeletedPreview final : public Ui::RpWidget {
public:
	DeletedPreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~DeletedPreview();

private:
	void paintEvent(QPaintEvent *e) override;

	[[nodiscard]] not_null<HistoryItem*> addItem(bool out, QString text);
	void createViews();
	void updateHeight(int width);

	const not_null<Main::Session*> _session;
	const not_null<History*> _history;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	const not_null<HistoryItem*> _out;
	const not_null<HistoryItem*> _in;
	std::unique_ptr<HistoryView::Element> _outView;
	std::unique_ptr<HistoryView::Element> _inView;
	int _inTop = 0;

};

DeletedPreview::DeletedPreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _session(&controller->session())
, _history(_session->data().history(_session->data().user(kFakeUserId)))
, _theme(controller->defaultChatTheme())
, _style(std::make_unique<Ui::ChatStyle>(_session->colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(this, _style.get(), [=] {
	update();
}))
, _out(addItem(true, tr::lng_tukigram_preview_out(tr::now)))
, _in(addItem(false, tr::lng_tukigram_preview_in(tr::now))) {
	_style->apply(_theme.get());
	createViews();

	widthValue() | rpl::on_next([=](int width) {
		updateHeight(width);
	}, lifetime());

	Core::App().settings().deletedAppearanceChanges(
	) | rpl::on_next([=] {
		createViews();
		updateHeight(width());
		update();
	}, lifetime());

	_session->data().viewRepaintRequest(
	) | rpl::on_next([=](Data::RequestViewRepaint data) {
		if (data.view == _outView.get() || data.view == _inView.get()) {
			update();
		}
	}, lifetime());
}

DeletedPreview::~DeletedPreview() {
	_outView = nullptr;
	_inView = nullptr;
	_out->destroy();
	_in->destroy();
}

not_null<HistoryItem*> DeletedPreview::addItem(bool out, QString text) {
	const auto item = _history->addNewLocalMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId
			| (out ? MessageFlag::Outgoing : MessageFlag())),
		.from = (out ? _session->userPeerId() : _history->peer->id),
		.date = base::unixtime::now(),
	}, TextWithEntities{ std::move(text) }, MTP_messageMediaEmpty());
	_session->data().markMessageDeleted(item);
	return item;
}

void DeletedPreview::createViews() {
	_outView = _out->createView(_delegate.get());
	_inView = _in->createView(_delegate.get());
	_outView->initDimensions();
	_inView->initDimensions();
}

void DeletedPreview::updateHeight(int width) {
	if (width <= st::msgMinWidth) {
		return;
	}
	const auto top = st::msgMargin.bottom();
	_inTop = top + _outView->resizeGetHeight(width);
	resize(width, _inTop + _inView->resizeGetHeight(width) + top);
}

void DeletedPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();

	p.setClipRect(clip);
	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(width(), window()->height()),
		clip);

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		rect(),
		clip,
		!window()->isActiveWindow());

	const auto top = st::msgMargin.bottom();
	p.translate(0, top);
	_outView->draw(p, context.translated(0, -top));
	p.translate(0, _inTop - top);
	_inView->draw(p, context.translated(0, -_inTop));
}

class BubbleRadiusPreview final : public Ui::RpWidget {
public:
	BubbleRadiusPreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<int> large,
		rpl::producer<int> small);

private:
	struct Bubble {
		Ui::Text::String text;
		QString time;
		int infoWidth = 0;
		bool out = false;
		bool attachedTop = false;
		bool attachedBottom = false;
		QRect geometry;
	};

	void paintEvent(QPaintEvent *e) override;
	void updateTime();
	void updateGeometries(int width);

	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	std::array<Bubble, 3> _bubbles;
	int _large = 0;
	int _small = 0;

};

[[nodiscard]] QPainterPath BubblePath(
		QRectF rect,
		float64 topLeft,
		float64 topRight,
		float64 bottomRight,
		float64 bottomLeft) {
	auto result = QPainterPath();
	result.moveTo(rect.left() + topLeft, rect.top());
	result.lineTo(rect.right() - topRight, rect.top());
	result.arcTo(
		rect.right() - 2 * topRight,
		rect.top(),
		2 * topRight,
		2 * topRight,
		90,
		-90);
	result.lineTo(rect.right(), rect.bottom() - bottomRight);
	result.arcTo(
		rect.right() - 2 * bottomRight,
		rect.bottom() - 2 * bottomRight,
		2 * bottomRight,
		2 * bottomRight,
		0,
		-90);
	result.lineTo(rect.left() + bottomLeft, rect.bottom());
	result.arcTo(
		rect.left(),
		rect.bottom() - 2 * bottomLeft,
		2 * bottomLeft,
		2 * bottomLeft,
		270,
		-90);
	result.lineTo(rect.left(), rect.top() + topLeft);
	result.arcTo(
		rect.left(),
		rect.top(),
		2 * topLeft,
		2 * topLeft,
		180,
		-90);
	result.closeSubpath();
	return result;
}

BubbleRadiusPreview::BubbleRadiusPreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<int> large,
	rpl::producer<int> small)
: RpWidget(parent)
, _theme(controller->defaultChatTheme())
, _style(std::make_unique<Ui::ChatStyle>(
	controller->session().colorIndicesValue())) {
	_style->apply(_theme.get());

	const auto init = [](
			Bubble &bubble,
			const QString &text,
			bool out,
			bool attachedTop,
			bool attachedBottom) {
		bubble.text.setText(st::messageTextStyle, text);
		bubble.out = out;
		bubble.attachedTop = attachedTop;
		bubble.attachedBottom = attachedBottom;
	};
	init(
		_bubbles[0],
		tr::lng_tukigram_bubble_preview_first(tr::now),
		false,
		false,
		true);
	init(
		_bubbles[1],
		tr::lng_tukigram_bubble_preview_second(tr::now),
		false,
		true,
		false);
	init(
		_bubbles[2],
		tr::lng_tukigram_bubble_preview_out(tr::now),
		true,
		false,
		false);
	updateTime();

	Core::App().settings().messageLayoutChanges(
	) | rpl::on_next([=] {
		updateTime();
		updateGeometries(width());
		update();
	}, lifetime());

	rpl::combine(
		std::move(large),
		std::move(small)
	) | rpl::on_next([=](int large, int small) {
		_large = Ui::BubbleRadiusFromPixels(large);
		_small = Ui::BubbleRadiusFromPixels(small);
		update();
	}, lifetime());

	widthValue() | rpl::on_next([=](int width) {
		updateGeometries(width);
	}, lifetime());
}

void BubbleRadiusPreview::updateTime() {
	const auto time = HistoryView::FormatMessageTime(QTime::currentTime());
	for (auto &bubble : _bubbles) {
		bubble.time = time;
		bubble.infoWidth = st::msgDateFont->width(time)
			+ (bubble.out ? st::historySendStateSpace : 0);

		// Reserve room for the time on the last line, as messages do.
		bubble.text.updateSkipBlock(
			st::msgDateSpace + bubble.infoWidth - st::msgDateDelta.x(),
			st::msgDateFont->height - st::msgDateDelta.y());
	}
}

void BubbleRadiusPreview::updateGeometries(int width) {
	const auto &padding = st::msgPadding;
	const auto skip = st::msgMargin.left();
	const auto bubbleMax = std::min(
		st::msgMaxWidth,
		width - skip - st::msgMargin.right());
	const auto innerMax = bubbleMax - padding.left() - padding.right();
	if (innerMax <= 0) {
		return;
	}
	auto top = 0;
	for (auto &bubble : _bubbles) {
		top += bubble.attachedTop
			? st::msgMarginTopAttached
			: st::msgMargin.top();
		const auto inner = std::min(bubble.text.maxWidth(), innerMax);
		const auto bubbleWidth = inner + padding.left() + padding.right();
		const auto bubbleHeight = padding.top()
			+ bubble.text.countHeight(inner)
			+ padding.bottom();
		const auto left = bubble.out ? (width - skip - bubbleWidth) : skip;
		bubble.geometry = QRect(left, top, bubbleWidth, bubbleHeight);
		top += bubbleHeight + st::msgMargin.bottom();
	}
	resize(width, top + st::msgMargin.top());
}

void BubbleRadiusPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();

	p.setClipRect(clip);
	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(width(), window()->height()),
		clip);

	const auto &padding = st::msgPadding;
	for (const auto &bubble : _bubbles) {
		const auto &st = _style->messageStyle(bubble.out, false);
		const auto rect = bubble.geometry;
		const auto max = std::min(rect.width(), rect.height()) / 2.;
		const auto large = std::min(float64(_large), max);
		const auto small = std::min(float64(_small), max);

		// Grouped corners are on the sender's side, like in the chat.
		const auto top = bubble.attachedTop ? small : large;
		const auto bottom = bubble.attachedBottom ? small : large;
		const auto path = bubble.out
			? BubblePath(rect, large, top, bottom, large)
			: BubblePath(rect, top, large, large, bottom);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st.msgShadow);
			p.drawPath(path.translated(0, st::msgShadow));
			p.setBrush(st.msgBg);
			p.drawPath(path);
		}

		p.setPen(st.historyTextFg);
		bubble.text.draw(p, {
			.position = rect.topLeft() + QPoint(padding.left(), padding.top()),
			.outerWidth = width(),
			.availableWidth = rect.width() - padding.left() - padding.right(),
			.palette = &st.textPalette,
		});

		const auto infoRight = rect.x()
			+ rect.width()
			- (padding.right() - st::msgDateDelta.x());
		const auto infoBottom = rect.y()
			+ rect.height()
			- (padding.bottom() - st::msgDateDelta.y());
		auto dateRight = infoRight;
		if (bubble.out) {
			st.historySentIcon.paint(
				p,
				QPoint(infoRight, infoBottom) + st::historySendStatePosition,
				width());
			dateRight -= st::historySendStateSpace;
		}
		p.setFont(st::msgDateFont);
		p.setPen(st.msgDateFg);
		p.drawTextLeft(
			dateRight - st::msgDateFont->width(bubble.time),
			infoBottom - st::msgDateFont->height,
			width(),
			bubble.time);
	}
}

void DeletedMarkBox(not_null<Ui::GenericBox*> box) {
	const auto settings = &Core::App().settings();
	box->setTitle(tr::lng_tukigram_deleted_mark());

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_tukigram_deleted_mark_placeholder(),
		settings->deletedMark()));
	field->setMaxLength(kMaxDeletedMarkLength);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_tukigram_deleted_mark_about(),
		st::boxDividerLabel));
	box->setFocusCallback([=] {
		field->setFocusFast();
		field->selectAll();
	});

	const auto apply = [=](QString value) {
		settings->setDeletedMark(value);
		Core::App().saveSettingsDelayed();
		box->closeBox();
	};
	const auto save = [=] {
		apply(field->getLastText().trimmed());
	};
	field->submits() | rpl::on_next(save, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->addLeftButton(tr::lng_background_reset_default(), [=] {
		apply(Core::Settings::DefaultDeletedMark());
	});
}

[[nodiscard]] rpl::producer<QString> DeletedMarkLabel() {
	const auto settings = &Core::App().settings();
	return rpl::single(
		rpl::empty
	) | rpl::then(
		settings->deletedAppearanceChanges()
	) | rpl::map([=] {
		const auto mark = settings->deletedMark();
		return mark.isEmpty()
			? tr::lng_tukigram_deleted_mark_none(tr::now)
			: mark;
	});
}

class TukiGram final : public Section<TukiGram> {
public:
	TukiGram(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

enum class CategoryId {
	Illegal,
	Messages,
	Profile,
	Appearance,
};

[[nodiscard]] const tr::phrase<> &CategoryTitle(CategoryId id);
[[nodiscard]] SectionBuildMethod CategoryBuildMethod(CategoryId id);

template <CategoryId kId>
class Category final : public Section<Category<kId>> {
public:
	Category(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section<Category<kId>>(parent, controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		this->build(content, CategoryBuildMethod(kId));
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return CategoryTitle(kId)();
	}

};

void AddToggle(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		Fn<bool()> value,
		Fn<void(bool)> apply,
		QStringList keywords) {
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(value()),
		.keywords = std::move(keywords),
	});
	if (button) {
		button->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != value());
		}) | rpl::on_next([=](bool enabled) {
			apply(enabled);
			Core::App().saveSettingsDelayed();
		}, button->lifetime());
	}
}

struct ValueSliderArgs {
	QString id;
	rpl::producer<QString> title;
	int min = 0;
	int max = 0;
	int step = 0;
	int current = 0;
	Fn<void(int)> chosen;
	QStringList keywords;
	Fn<void(int)> changing;
	QString suffix = u"%"_q;
};

void AddValueSlider(SectionBuilder &builder, ValueSliderArgs &&args) {
	const auto button = builder.addButton({
		.id = std::move(args.id),
		.title = std::move(args.title),
		.st = &st::settingsButtonNoIcon,
		.keywords = std::move(args.keywords),
	});
	if (!button) {
		return;
	}
	button->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto min = args.min;
	const auto max = args.max;
	const auto step = args.step;
	const auto count = (max - min) / step + 1;
	const auto current = args.current;
	const auto chosen = args.chosen;
	const auto changing = args.changing;
	const auto suffix = args.suffix;
	builder.add([=](const WidgetContext &ctx) {
		auto row = object_ptr<Ui::RpWidget>(ctx.container);
		const auto raw = row.data();
		const auto slider = Ui::CreateChild<Ui::MediaSlider>(
			raw,
			st::settingsScale);
		const auto &labelSt = st::settingsButtonNoIcon.rightLabel;
		const auto label = Ui::CreateChild<Ui::FlatLabel>(raw, labelSt);

		const auto format = [=](int value) {
			return QString::number(value) + suffix;
		};
		const auto &font = labelSt.style.font;
		const auto labelWidth = std::max(
			font->width(format(min)),
			font->width(format(max)));
		const auto height = std::max(
			st::settingsScale.seekSize.height(),
			font->height);
		const auto placeLabel = [=] {
			label->resizeToNaturalWidth(labelWidth);
			label->moveToRight(0, (height - label->height()) / 2);
		};
		const auto setLabel = [=](int value) {
			label->setText(format(value));
			placeLabel();
		};

		slider->setPseudoDiscrete(
			count,
			[=](int index) { return min + index * step; },
			current,
			[=](int value) {
				setLabel(value);
				if (changing) {
					changing(value);
				}
			},
			chosen);
		setLabel(current);

		raw->resize(raw->width(), height);
		raw->widthValue() | rpl::on_next([=](int width) {
			const auto seek = st::settingsScale.seekSize.height();
			slider->resize(
				std::max(width - labelWidth - st::settingsButtonRightSkip, 0),
				seek);
			slider->moveToLeft(0, (height - seek) / 2);
			placeLabel();
		}, raw->lifetime());

		const auto &padding = st::settingsBigScalePadding;
		return SectionBuilder::WidgetToAdd{
			.widget = std::move(row),
			.margin = QMargins(
				padding.left(),
				padding.top(),
				st::settingsButtonNoIcon.padding.right(),
				padding.bottom()),
		};
	});
}

[[nodiscard]] QString FontFamilyLabel(const QString &family) {
	return family.isEmpty()
		? tr::lng_font_default(tr::now)
		: (family == style::SystemFontTag())
		? tr::lng_font_system(tr::now)
		: family;
}

void ShowRestartBox(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_settings_need_restart(),
		.confirmed = [] {
			Local::writeSettings();
			Core::Restart();
		},
		.confirmText = tr::lng_settings_restart_now(),
	}));
}

void AddDeletedOptions(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	AddToggle(
		builder,
		u"tukigram/save_deleted"_q,
		tr::lng_materialgram_settings_save_deleted(),
		[=] { return settings->saveDeletedMessages(); },
		[=](bool value) { settings->setSaveDeletedMessages(value); },
		{ u"tukigram"_q, u"deleted"_q, u"messages"_q });
	AddToggle(
		builder,
		u"tukigram/save_own_deleted"_q,
		tr::lng_materialgram_settings_save_deleted_own(),
		[=] { return settings->saveOwnDeletedMessages(); },
		[=](bool value) { settings->setSaveOwnDeletedMessages(value); },
		{ u"tukigram"_q, u"deleted"_q, u"own"_q });

	const auto controller = builder.controller();
	if (controller) {
		builder.add([=](const WidgetContext &ctx) {
			return SectionBuilder::WidgetToAdd{
				.widget = object_ptr<DeletedPreview>(ctx.container, controller),
				.margin = QMargins(0, st::settingsCheckboxesSkip, 0, 0),
			};
		});
	}

	builder.addButton({
		.id = u"tukigram/deleted_mark"_q,
		.title = tr::lng_tukigram_deleted_mark(),
		.st = &st::settingsButtonNoIcon,
		.label = DeletedMarkLabel(),
		.onClick = [=] {
			if (controller) {
				controller->show(Box(DeletedMarkBox));
			}
		},
		.keywords = { u"tukigram"_q, u"deleted"_q, u"mark"_q, u"emoji"_q },
	});
	AddToggle(
		builder,
		u"tukigram/deleted_transparent"_q,
		tr::lng_tukigram_deleted_transparent(),
		[=] { return settings->deletedTransparent(); },
		[=](bool value) { settings->setDeletedTransparent(value); },
		{ u"tukigram"_q, u"deleted"_q, u"transparent"_q });
}

void BuildMessagesCategory(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();

	AddToggle(
		builder,
		u"tukigram/forward_protected"_q,
		tr::lng_materialgram_settings_forward_protected(),
		[=] { return settings->forwardProtectedMessages(); },
		[=](bool value) { settings->setForwardProtectedMessages(value); },
		{ u"tukigram"_q, u"forward"_q, u"protected"_q, u"restricted"_q });
	AddToggle(
		builder,
		u"tukigram/edit_history"_q,
		tr::lng_materialgram_settings_edit_history(),
		[=] { return settings->saveEditHistory(); },
		[=](bool value) { settings->setSaveEditHistory(value); },
		{ u"tukigram"_q, u"edit"_q, u"history"_q, u"edited"_q });

	builder.addSkip();
}

void AddMediaOptions(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	AddToggle(
		builder,
		u"tukigram/keep_ttl"_q,
		tr::lng_materialgram_settings_keep_ttl(),
		[=] { return settings->keepSelfDestructingMedia(); },
		[=](bool value) { settings->setKeepSelfDestructingMedia(value); },
		{ u"tukigram"_q, u"self-destructing"_q, u"view once"_q, u"timer"_q });
	AddToggle(
		builder,
		u"tukigram/ttl_read"_q,
		tr::lng_materialgram_settings_ttl_read(),
		[=] { return settings->markSelfDestructingRead(); },
		[=](bool value) { settings->setMarkSelfDestructingRead(value); },
		{ u"tukigram"_q, u"self-destructing"_q, u"viewed"_q, u"read"_q });
}

void BuildProfileCategory(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();

	AddToggle(
		builder,
		u"tukigram/registration"_q,
		tr::lng_materialgram_info_registration(),
		[=] { return settings->birthDateEnabled(); },
		[=](bool value) { settings->setBirthDateEnabled(value); },
		{ u"tukigram"_q, u"registration"_q, u"date"_q });
	AddToggle(
		builder,
		u"tukigram/datacenter"_q,
		tr::lng_materialgram_info_dc(),
		[=] { return settings->datacenterEnabled(); },
		[=](bool value) { settings->setDatacenterEnabled(value); },
		{ u"tukigram"_q, u"datacenter"_q, u"dc"_q });
	AddToggle(
		builder,
		u"tukigram/gamee"_q,
		tr::lng_settings_profile_photo_privacy(),
		[=] { return settings->gameeEnabled(); },
		[=](bool value) { settings->setGameeEnabled(value); },
		{ u"tukigram"_q, u"gamee"_q, u"profile"_q, u"photo"_q });

	builder.addSkip();
}

void AddGhostToggle(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		Fn<bool()> value,
		Fn<void(bool)> apply,
		QStringList keywords) {
	const auto settings = &Core::App().settings();
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(
			rpl::empty
		) | rpl::then(
			settings->ghostModeChanges()
		) | rpl::map(value),
		.keywords = std::move(keywords),
	});
	if (button) {
		button->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != value());
		}) | rpl::on_next([=](bool enabled) {
			apply(enabled);
			Core::App().saveSettingsDelayed();
		}, button->lifetime());
	}
}

void AddGhostOption(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		Core::GhostOption option,
		QStringList keywords) {
	const auto settings = &Core::App().settings();
	AddGhostToggle(
		builder,
		std::move(id),
		std::move(title),
		[=] { return settings->ghostOption(option); },
		[=](bool value) { settings->setGhostOption(option, value); },
		std::move(keywords));
}

[[nodiscard]] object_ptr<Ui::SettingsButton> MakeGhostModeButton(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<rpl::variable<bool>> expanded) {
	const auto settings = &Core::App().settings();
	const auto &st = st::settingsButtonNoIcon;
	auto result = object_ptr<Ui::SettingsButton>(container, nullptr, st);
	const auto button = result.data();
	const auto toggleButton = Ui::CreateChild<Ui::SettingsButton>(
		button,
		nullptr,
		st);

	struct State final {
		State(const style::Toggle &st, Fn<void()> callback)
		: checkView(st, false, std::move(callback)) {
		}
		Ui::ToggleView checkView;
		Ui::Animations::Simple animation;
	};
	const auto state = button->lifetime().make_state<State>(
		st.toggle,
		[=] { toggleButton->update(); });
	const auto checkView = &state->checkView;
	const auto checkedCount = [=] {
		return rpl::single(
			rpl::empty
		) | rpl::then(
			settings->ghostModeChanges()
		) | rpl::map([=] {
			return int(ranges::count_if(kGhostModeOptions, [&](
					Core::GhostOption option) {
				return settings->ghostOption(option);
			}));
		});
	};

	const auto separator = Ui::CreateChild<Ui::RpWidget>(button);
	separator->paintRequest(
	) | rpl::on_next([=, bg = st.textBgOver] {
		auto p = QPainter(separator);
		p.fillRect(separator->rect(), bg);
	}, separator->lifetime());
	const auto separatorHeight = 2 * st.toggle.border + st.toggle.diameter;
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto w = st::rightsButtonToggleWidth;
		toggleButton->setGeometry(size.width() - w, 0, w, size.height());
		separator->setGeometry(
			toggleButton->x() - st::lineWidth,
			(size.height() - separatorHeight) / 2,
			st::lineWidth,
			separatorHeight);
	}, toggleButton->lifetime());

	const auto checkWidget = Ui::CreateChild<Ui::RpWidget>(toggleButton);
	checkWidget->resize(checkView->getSize());
	checkWidget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(checkWidget);
		checkView->paint(p, 0, 0, checkWidget->width());
	}, checkWidget->lifetime());
	toggleButton->sizeValue(
	) | rpl::on_next([=](QSize size) {
		checkWidget->moveToRight(
			st.toggleSkip,
			(size.height() - checkWidget->height()) / 2);
	}, toggleButton->lifetime());
	checkedCount(
	) | rpl::on_next([=](int count) {
		checkView->setChecked(count > 0, anim::type::normal);
	}, toggleButton->lifetime());
	checkView->finishAnimating();

	const auto total = int(kGhostModeOptions.size());
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		rpl::combine(
			tr::lng_tukigram_ghost_title(),
			checkedCount()
		) | rpl::map([=](const QString &title, int checked) {
			return TextWithEntities::Simple(title).append(tr::bold("  "
				+ QString::number(checked)
				+ '/'
				+ QString::number(total)));
		}));
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto arrow = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &icon = st::permissionsExpandIcon;
	arrow->resize(icon.size());
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	arrow->paintRequest(
	) | rpl::on_next([=, &icon] {
		auto p = QPainter(arrow);
		const auto center = QPointF(icon.width() / 2., icon.height() / 2.);
		const auto progress = state->animation.value(
			expanded->current() ? 1. : 0.);
		auto hq = std::optional<PainterHighQualityEnabler>();
		if (progress > 0.) {
			hq.emplace(p);
			p.translate(center);
			p.rotate(progress * 180.);
			p.translate(-center);
		}
		icon.paint(p, 0, 0, arrow->width());
	}, arrow->lifetime());

	rpl::combine(
		button->sizeValue(),
		checkedCount()
	) | rpl::on_next([=, &st](QSize size, int) {
		const auto labelLeft = st.padding.left();
		const auto labelRight = size.width() - st::rightsButtonToggleWidth;
		const auto arrowSkip = st::rightsButtonArrowSkip;
		label->resizeToWidth(
			labelRight - labelLeft - arrow->width() - arrowSkip);
		label->moveToLeft(labelLeft, (size.height() - label->height()) / 2);
		arrow->moveToLeft(
			std::min(
				labelLeft + label->textMaxWidth() + arrowSkip,
				labelRight - arrow->width()),
			(size.height() - arrow->height()) / 2);
	}, button->lifetime());

	expanded->changes(
	) | rpl::on_next([=](bool toggled) {
		state->animation.start(
			[=] { arrow->update(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::slideWrapDuration);
	}, button->lifetime());

	button->clicks(
	) | rpl::on_next([=] {
		*expanded = !expanded->current();
	}, button->lifetime());
	toggleButton->clicks(
	) | rpl::on_next([=] {
		settings->setGhostMode(!checkView->checked());
		Core::App().saveSettingsDelayed();
	}, toggleButton->lifetime());

	return result;
}

void AddGhostOptions(SectionBuilder &builder) {
	using Option = Core::GhostOption;
	const auto expanded = std::make_shared<rpl::variable<bool>>(false);

	builder.addControl({
		.factory = [=](not_null<Ui::VerticalLayout*> container) {
			return MakeGhostModeButton(container, expanded);
		},
		.id = u"tukigram/ghost"_q,
		.title = tr::lng_tukigram_ghost_title(),
		.keywords = {
			u"tukigram"_q,
			u"ghost"_q,
			u"stealth"_q,
			u"invisible"_q,
		},
	});
	builder.scope([&] {
		AddGhostOption(
			builder,
			u"tukigram/ghost_reads"_q,
			tr::lng_tukigram_ghost_reads(),
			Option::HideReads,
			{ u"ghost"_q, u"read"_q, u"receipts"_q });
		AddGhostOption(
			builder,
			u"tukigram/ghost_stories"_q,
			tr::lng_tukigram_ghost_story_views(),
			Option::HideStoryViews,
			{ u"ghost"_q, u"stories"_q, u"views"_q });
		AddGhostOption(
			builder,
			u"tukigram/ghost_online"_q,
			tr::lng_tukigram_ghost_online(),
			Option::HideOnline,
			{ u"ghost"_q, u"online"_q, u"last seen"_q });
		AddGhostOption(
			builder,
			u"tukigram/ghost_typing"_q,
			tr::lng_tukigram_ghost_typing(),
			Option::HideTyping,
			{ u"ghost"_q, u"typing"_q, u"upload"_q });
		AddGhostOption(
			builder,
			u"tukigram/ghost_offline_after_send"_q,
			tr::lng_tukigram_ghost_offline_after_send(),
			Option::OfflineAfterSend,
			{ u"ghost"_q, u"offline"_q, u"send"_q });
	}, expanded->value());
	builder.addSkip();
	builder.addDividerText(tr::lng_tukigram_ghost_about());
	builder.addSkip();

	builder.addSubsectionTitle(tr::lng_tukigram_ghost_exceptions());
	AddGhostOption(
		builder,
		u"tukigram/ghost_read_after_send"_q,
		tr::lng_tukigram_ghost_read_after_send(),
		Option::ReadAfterSend,
		{ u"ghost"_q, u"read"_q, u"send"_q });
	builder.addSkip();
	builder.addDividerText(tr::lng_tukigram_ghost_exceptions_about());
	builder.addSkip();

	builder.addSubsectionTitle(tr::lng_tukigram_ghost_sending());
	AddGhostOption(
		builder,
		u"tukigram/ghost_scheduled_send"_q,
		tr::lng_tukigram_ghost_scheduled_send(),
		Option::ScheduledSend,
		{ u"ghost"_q, u"scheduled"_q, u"send"_q, u"online"_q });
	builder.addSkip();
	builder.addDividerText(tr::lng_tukigram_ghost_scheduled_send_about());
	builder.addSkip();
}

void BuildIllegalCategory(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_tukigram_media_title());
	AddMediaOptions(builder);
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	AddGhostOptions(builder);

	builder.addSubsectionTitle(tr::lng_tukigram_deleted_title());
	AddDeletedOptions(builder);
	builder.addSkip();
}

void BuildAppearanceCategory(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	const auto controller = builder.controller();
	builder.addSkip();

	builder.addSubsectionTitle(tr::lng_tukigram_fonts());
	builder.addButton({
		.id = u"tukigram/font"_q,
		.title = tr::lng_settings_font_family(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(FontFamilyLabel(settings->customFontFamily())),
		.onClick = [=] {
			if (controller) {
				ShowChooseFontBox(controller, false);
			}
		},
		.keywords = { u"tukigram"_q, u"font"_q, u"family"_q },
	});
	builder.addButton({
		.id = u"tukigram/mono_font"_q,
		.title = tr::lng_tukigram_mono_font(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			FontFamilyLabel(settings->monospaceFontFamily())),
		.onClick = [=] {
			if (controller) {
				ShowChooseFontBox(controller, true);
			}
		},
		.keywords = { u"tukigram"_q, u"font"_q, u"monospace"_q, u"code"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_tukigram_fonts_about());
	builder.addSkip();

	builder.addSubsectionTitle(tr::lng_tukigram_messages_title());
	const auto large = std::make_shared<rpl::variable<int>>(
		settings->bubbleRadiusLarge());
	const auto small = std::make_shared<rpl::variable<int>>(
		settings->bubbleRadiusSmall());
	if (controller) {
		builder.add([=](const WidgetContext &ctx) {
			return SectionBuilder::WidgetToAdd{
				.widget = object_ptr<BubbleRadiusPreview>(
					ctx.container,
					controller,
					large->value(),
					small->value()),
				.margin = QMargins(
					0,
					st::settingsCheckboxesSkip,
					0,
					st::settingsCheckboxesSkip),
			};
		});
	}
	AddToggle(
		builder,
		u"tukigram/message_seconds"_q,
		tr::lng_tukigram_message_seconds(),
		[=] { return settings->messageSeconds(); },
		[=](bool value) { settings->setMessageSeconds(value); },
		{ u"tukigram"_q, u"seconds"_q, u"time"_q });
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	AddValueSlider(builder, {
		.id = u"tukigram/bubble_radius"_q,
		.title = tr::lng_tukigram_bubble_radius(),
		.min = Core::Settings::kBubbleRadiusMin,
		.max = Core::Settings::kBubbleRadiusMax,
		.step = 1,
		.current = large->current(),
		.chosen = [=](int px) {
			settings->setBubbleRadiusLarge(px);
			Core::App().saveSettingsDelayed();
		},
		.keywords = {
			u"tukigram"_q,
			u"bubble"_q,
			u"radius"_q,
			u"corner"_q,
			u"round"_q,
		},
		.changing = [=](int px) { *large = px; },
		.suffix = u"px"_q,
	});
	AddValueSlider(builder, {
		.id = u"tukigram/bubble_radius_small"_q,
		.title = tr::lng_tukigram_bubble_radius_small(),
		.min = Core::Settings::kBubbleRadiusMin,
		.max = Core::Settings::kBubbleRadiusMax,
		.step = 1,
		.current = small->current(),
		.chosen = [=](int px) {
			settings->setBubbleRadiusSmall(px);
			Core::App().saveSettingsDelayed();
		},
		.keywords = {
			u"tukigram"_q,
			u"bubble"_q,
			u"radius"_q,
			u"grouped"_q,
			u"corner"_q,
		},
		.changing = [=](int px) { *small = px; },
		.suffix = u"px"_q,
	});
	builder.addButton({
		.title = tr::lng_tukigram_bubble_restart(),
		.st = &st::settingsButtonLightNoIcon,
		.onClick = [] {
			Local::writeSettings();
			Core::Restart();
		},
		.shown = rpl::combine(
			large->value(),
			small->value()
		) | rpl::map([](int large, int small) {
			return (Ui::BubbleRadiusFromPixels(large)
					!= st::bubbleRadiusLarge)
				|| (Ui::BubbleRadiusFromPixels(small)
					!= st::bubbleRadiusSmall);
		}),
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_tukigram_bubble_radius_about());
	builder.addSkip();

	AddValueSlider(builder, {
		.id = u"tukigram/message_width"_q,
		.title = tr::lng_tukigram_message_width(),
		.min = Core::Settings::kMessageWidthMin,
		.max = Core::Settings::kMessageWidthMax,
		.step = 10,
		.current = settings->messageWidthPercent(),
		.chosen = [=](int percent) {
			if (percent == settings->messageWidthPercent()) {
				return;
			}
			settings->setMessageWidthPercent(percent);
			Core::App().saveSettingsDelayed();
			if (controller) {
				ShowRestartBox(controller);
			}
		},
		.keywords = { u"tukigram"_q, u"wide"_q, u"width"_q, u"bubble"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_tukigram_message_width_about());
	builder.addSkip();

	AddValueSlider(builder, {
		.id = u"tukigram/video_message_size"_q,
		.title = tr::lng_tukigram_video_message_size(),
		.min = Core::Settings::kVideoMessageSizeMin,
		.max = Core::Settings::kVideoMessageSizeMax,
		.step = 10,
		.current = settings->videoMessageSizePercent(),
		.chosen = [=](int percent) {
			settings->setVideoMessageSizePercent(percent);
			Core::App().saveSettingsDelayed();
		},
		.keywords = { u"tukigram"_q, u"round"_q, u"video"_q, u"circle"_q },
	});
	builder.addSkip();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeHeader(QWidget *parent) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();

	const auto size = st::tukigramLogoSize;
	const auto logo = raw->add(
		object_ptr<Ui::RpWidget>(raw),
		st::tukigramLogoMargin);
	logo->resize(size, size);
	logo->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		const auto circle = QRect((logo->width() - size) / 2, 0, size, size);
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawEllipse(circle);
		st::tukigramLogoIcon.paintInCenter(p, circle);
	}, logo->lifetime());

	const auto padding = st::boxRowPadding;
	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			tr::lng_tukigram_version(
				lt_version,
				rpl::single(QString::fromLatin1(AppVersionStr))),
			st::settingsAgeVerifyTitle),
		QMargins(padding.left(), 0, padding.right(), 0),
		style::al_top);
	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			tr::lng_tukigram_about(),
			st::settingsAgeVerifyText),
		QMargins(
			padding.left(),
			st::settingsAgeVerifyMargin.top(),
			padding.right(),
			st::tukigramLogoMargin.top()),
		style::al_top);
	return result;
}

void BuildTukiGramSection(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = MakeHeader(ctx.container),
		};
	});

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_tukigram_categories());
	builder.addSectionButton({
		.title = tr::lng_tukigram_illegal_title(),
		.targetSection = Category<CategoryId::Illegal>::Id(),
		.icon = { &st::menuIconStealth },
		.keywords = {
			u"tukigram"_q,
			u"ghost"_q,
			u"stealth"_q,
			u"deleted"_q,
			u"self-destructing"_q,
			u"timer"_q,
		},
	});
	builder.addSectionButton({
		.title = tr::lng_tukigram_messages_title(),
		.targetSection = Category<CategoryId::Messages>::Id(),
		.icon = { &st::menuIconChatBubble },
		.keywords = { u"tukigram"_q, u"forward"_q, u"edit"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_tukigram_profile_title(),
		.targetSection = Category<CategoryId::Profile>::Id(),
		.icon = { &st::menuIconProfile },
		.keywords = { u"tukigram"_q, u"profile"_q, u"dc"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_tukigram_appearance_title(),
		.targetSection = Category<CategoryId::Appearance>::Id(),
		.icon = { &st::menuIconPalette },
		.keywords = { u"tukigram"_q, u"appearance"_q, u"font"_q },
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_tukigram_links());
	builder.addButton({
		.id = u"tukigram/github"_q,
		.title = rpl::single(u"GitHub"_q),
		.icon = { &st::menuIconGitHub },
		.label = rpl::single(u"pachdomenic/materialgram"_q),
		.onClick = [] {
			UrlClickHandler::Open(
				u"https://github.com/pachdomenic/materialgram"_q);
		},
		.keywords = { u"tukigram"_q, u"github"_q },
	});
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = TukiGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_tukigram_settings,
	.icon = &st::menuIconTukiGram,
}, BuildTukiGramSection);

const auto kIllegalMeta = BuildHelper({
	.id = Category<CategoryId::Illegal>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_illegal_title,
	.icon = &st::menuIconStealth,
}, BuildIllegalCategory);

const auto kMessagesMeta = BuildHelper({
	.id = Category<CategoryId::Messages>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_messages_title,
	.icon = &st::menuIconChatBubble,
}, BuildMessagesCategory);

const auto kProfileMeta = BuildHelper({
	.id = Category<CategoryId::Profile>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_profile_title,
	.icon = &st::menuIconProfile,
}, BuildProfileCategory);

const auto kAppearanceMeta = BuildHelper({
	.id = Category<CategoryId::Appearance>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_appearance_title,
	.icon = &st::menuIconPalette,
}, BuildAppearanceCategory);

const tr::phrase<> &CategoryTitle(CategoryId id) {
	switch (id) {
	case CategoryId::Illegal: return tr::lng_tukigram_illegal_title;
	case CategoryId::Messages: return tr::lng_tukigram_messages_title;
	case CategoryId::Profile: return tr::lng_tukigram_profile_title;
	case CategoryId::Appearance: return tr::lng_tukigram_appearance_title;
	}
	Unexpected("Category in CategoryTitle.");
}

SectionBuildMethod CategoryBuildMethod(CategoryId id) {
	switch (id) {
	case CategoryId::Illegal: return kIllegalMeta.build;
	case CategoryId::Messages: return kMessagesMeta.build;
	case CategoryId::Profile: return kProfileMeta.build;
	case CategoryId::Appearance: return kAppearanceMeta.build;
	}
	Unexpected("Category in CategoryBuildMethod.");
}

TukiGram::TukiGram(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> TukiGram::title() {
	return tr::lng_tukigram_settings();
}

void TukiGram::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kMeta.build);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type TukiGramId() {
	return TukiGram::Id();
}

} // namespace Settings
