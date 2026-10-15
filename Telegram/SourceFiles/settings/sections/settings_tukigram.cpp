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
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kFakeUserId = UserId(0xFFFFFFF001ULL);
constexpr auto kMaxDeletedMarkLength = 16;

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

void BuildTukiGramSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	builder.addSkip();

	const auto registration = builder.addButton({
		.id = u"tukigram/registration"_q,
		.title = tr::lng_materialgram_info_registration(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->birthDateEnabled()),
		.keywords = { u"tukigram"_q, u"registration"_q, u"date"_q },
	});
	if (registration) {
		registration->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->birthDateEnabled());
		}) | rpl::on_next([=](bool enabled) {
			settings->setBirthDateEnabled(enabled);
			Core::App().saveSettingsDelayed();
		}, registration->lifetime());
	}

	const auto datacenter = builder.addButton({
		.id = u"tukigram/datacenter"_q,
		.title = tr::lng_materialgram_info_dc(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->datacenterEnabled()),
		.keywords = { u"tukigram"_q, u"datacenter"_q, u"dc"_q },
	});
	if (datacenter) {
		datacenter->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->datacenterEnabled());
		}) | rpl::on_next([=](bool enabled) {
			settings->setDatacenterEnabled(enabled);
			Core::App().saveSettingsDelayed();
		}, datacenter->lifetime());
	}

	const auto gamee = builder.addButton({
		.id = u"tukigram/gamee"_q,
		.title = tr::lng_settings_profile_photo_privacy(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->gameeEnabled()),
		.keywords = { u"tukigram"_q, u"gamee"_q, u"profile"_q, u"photo"_q },
	});
	if (gamee) {
		gamee->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->gameeEnabled());
		}) | rpl::on_next([=](bool enabled) {
			settings->setGameeEnabled(enabled);
			Core::App().saveSettingsDelayed();
		}, gamee->lifetime());
	}

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"tukigram/deleted"_q,
		.title = tr::lng_tukigram_deleted_title(),
		.keywords = { u"tukigram"_q, u"deleted"_q },
	});

	const auto saveDeleted = builder.addButton({
		.id = u"tukigram/save_deleted"_q,
		.title = tr::lng_materialgram_settings_save_deleted(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->saveDeletedMessages()),
		.keywords = { u"tukigram"_q, u"deleted"_q, u"messages"_q },
	});
	if (saveDeleted) {
		saveDeleted->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->saveDeletedMessages());
		}) | rpl::on_next([=](bool enabled) {
			settings->setSaveDeletedMessages(enabled);
			Core::App().saveSettingsDelayed();
		}, saveDeleted->lifetime());
	}

	const auto saveOwnDeleted = builder.addButton({
		.id = u"tukigram/save_own_deleted"_q,
		.title = tr::lng_materialgram_settings_save_deleted_own(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->saveOwnDeletedMessages()),
		.keywords = { u"tukigram"_q, u"deleted"_q, u"own"_q },
	});
	if (saveOwnDeleted) {
		saveOwnDeleted->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->saveOwnDeletedMessages());
		}) | rpl::on_next([=](bool enabled) {
			settings->setSaveOwnDeletedMessages(enabled);
			Core::App().saveSettingsDelayed();
		}, saveOwnDeleted->lifetime());
	}

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

	const auto transparent = builder.addButton({
		.id = u"tukigram/deleted_transparent"_q,
		.title = tr::lng_tukigram_deleted_transparent(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->deletedTransparent()),
		.keywords = { u"tukigram"_q, u"deleted"_q, u"transparent"_q },
	});
	if (transparent) {
		transparent->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->deletedTransparent());
		}) | rpl::on_next([=](bool enabled) {
			settings->setDeletedTransparent(enabled);
			Core::App().saveSettingsDelayed();
		}, transparent->lifetime());
	}

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"tukigram/privacy"_q,
		.title = tr::lng_tukigram_privacy_title(),
		.keywords = { u"tukigram"_q, u"privacy"_q },
	});

	const auto forwardProtected = builder.addButton({
		.id = u"tukigram/forward_protected"_q,
		.title = tr::lng_materialgram_settings_forward_protected(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->forwardProtectedMessages()),
		.keywords = {
			u"tukigram"_q,
			u"forward"_q,
			u"protected"_q,
			u"restricted"_q,
		},
	});
	if (forwardProtected) {
		forwardProtected->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->forwardProtectedMessages());
		}) | rpl::on_next([=](bool enabled) {
			settings->setForwardProtectedMessages(enabled);
			Core::App().saveSettingsDelayed();
		}, forwardProtected->lifetime());
	}

	const auto keepTtl = builder.addButton({
		.id = u"tukigram/keep_ttl"_q,
		.title = tr::lng_materialgram_settings_keep_ttl(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->keepSelfDestructingMedia()),
		.keywords = {
			u"tukigram"_q,
			u"self-destructing"_q,
			u"view once"_q,
			u"timer"_q,
		},
	});
	if (keepTtl) {
		keepTtl->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->keepSelfDestructingMedia());
		}) | rpl::on_next([=](bool enabled) {
			settings->setKeepSelfDestructingMedia(enabled);
			Core::App().saveSettingsDelayed();
		}, keepTtl->lifetime());
	}

	const auto ttlRead = builder.addButton({
		.id = u"tukigram/ttl_read"_q,
		.title = tr::lng_materialgram_settings_ttl_read(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->markSelfDestructingRead()),
		.keywords = {
			u"tukigram"_q,
			u"self-destructing"_q,
			u"viewed"_q,
			u"read"_q,
		},
	});
	if (ttlRead) {
		ttlRead->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->markSelfDestructingRead());
		}) | rpl::on_next([=](bool enabled) {
			settings->setMarkSelfDestructingRead(enabled);
			Core::App().saveSettingsDelayed();
		}, ttlRead->lifetime());
	}

	const auto editHistory = builder.addButton({
		.id = u"tukigram/edit_history"_q,
		.title = tr::lng_materialgram_settings_edit_history(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->saveEditHistory()),
		.keywords = {
			u"tukigram"_q,
			u"edit"_q,
			u"history"_q,
			u"edited"_q,
		},
	});
	if (editHistory) {
		editHistory->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->saveEditHistory());
		}) | rpl::on_next([=](bool enabled) {
			settings->setSaveEditHistory(enabled);
			Core::App().saveSettingsDelayed();
		}, editHistory->lifetime());
	}

	builder.addSkip();
}

class TukiGram : public Section<TukiGram> {
public:
	TukiGram(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = TukiGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_tukigram_settings,
	.icon = &st::menuIconCustomize,
}, [](SectionBuilder &builder) {
	BuildTukiGramSection(builder);
});

const SectionBuildMethod kTukiGramSection = kMeta.build;

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

	build(content, kTukiGramSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type TukiGramId() {
	return TukiGram::Id();
}

} // namespace Settings
