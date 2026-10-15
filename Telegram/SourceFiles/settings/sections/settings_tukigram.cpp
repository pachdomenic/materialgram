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
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "ui/basic_click_handlers.h"
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
	Deleted,
	Messages,
	Media,
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

[[nodiscard]] QString FontFamilyLabel(const QString &family) {
	return family.isEmpty()
		? tr::lng_font_default(tr::now)
		: (family == style::SystemFontTag())
		? tr::lng_font_system(tr::now)
		: family;
}

void BuildDeletedCategory(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();

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

	builder.addSkip();
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

void BuildMediaCategory(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();

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

	builder.addSkip();
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
	AddToggle(
		builder,
		u"tukigram/message_seconds"_q,
		tr::lng_tukigram_message_seconds(),
		[=] { return settings->messageSeconds(); },
		[=](bool value) { settings->setMessageSeconds(value); },
		{ u"tukigram"_q, u"seconds"_q, u"time"_q });
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
		.title = tr::lng_tukigram_deleted_title(),
		.targetSection = Category<CategoryId::Deleted>::Id(),
		.icon = { &st::menuIconDelete },
		.keywords = { u"tukigram"_q, u"deleted"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_tukigram_messages_title(),
		.targetSection = Category<CategoryId::Messages>::Id(),
		.icon = { &st::menuIconChatBubble },
		.keywords = { u"tukigram"_q, u"forward"_q, u"edit"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_tukigram_media_title(),
		.targetSection = Category<CategoryId::Media>::Id(),
		.icon = { &st::menuIconTimer },
		.keywords = { u"tukigram"_q, u"self-destructing"_q, u"timer"_q },
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

const auto kDeletedMeta = BuildHelper({
	.id = Category<CategoryId::Deleted>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_deleted_title,
	.icon = &st::menuIconDelete,
}, BuildDeletedCategory);

const auto kMessagesMeta = BuildHelper({
	.id = Category<CategoryId::Messages>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_messages_title,
	.icon = &st::menuIconChatBubble,
}, BuildMessagesCategory);

const auto kMediaMeta = BuildHelper({
	.id = Category<CategoryId::Media>::Id(),
	.parentId = TukiGram::Id(),
	.title = &tr::lng_tukigram_media_title,
	.icon = &st::menuIconTimer,
}, BuildMediaCategory);

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
	case CategoryId::Deleted: return tr::lng_tukigram_deleted_title;
	case CategoryId::Messages: return tr::lng_tukigram_messages_title;
	case CategoryId::Media: return tr::lng_tukigram_media_title;
	case CategoryId::Profile: return tr::lng_tukigram_profile_title;
	case CategoryId::Appearance: return tr::lng_tukigram_appearance_title;
	}
	Unexpected("Category in CategoryTitle.");
}

SectionBuildMethod CategoryBuildMethod(CategoryId id) {
	switch (id) {
	case CategoryId::Deleted: return kDeletedMeta.build;
	case CategoryId::Messages: return kMessagesMeta.build;
	case CategoryId::Media: return kMediaMeta.build;
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
