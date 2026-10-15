/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_tukigram.h"

#include "settings/settings_common_session.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

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
