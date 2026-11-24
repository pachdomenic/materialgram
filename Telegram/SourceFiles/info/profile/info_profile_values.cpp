/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_values.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "countries/countries_instance.h"
#include "data/notify/data_notify_settings.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_shared_media.h"
#include "data/data_user.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_phone_menu.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/wrap/slide_wrap.h"

namespace Info {
namespace Profile {
namespace {

using UpdateFlag = Data::PeerUpdate::Flag;

auto PlainAboutValue(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::About
	) | rpl::map([=] {
		return peer->about();
	});
}

auto PlainUsernameValue(not_null<PeerData*> peer) {
	return rpl::merge(
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Username),
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Usernames)
	) | rpl::map([=] {
		return peer->username();
	});
}

auto PlainPrimaryUsernameValue(not_null<PeerData*> peer) {
	return UsernamesValue(
		peer
	) | rpl::map([=](std::vector<TextWithEntities> usernames) {
		if (!usernames.empty()) {
			return rpl::single(usernames.front().text) | rpl::type_erased;
		} else {
			return PlainUsernameValue(peer) | rpl::type_erased;
		}
	}) | rpl::flatten_latest();
}

void StripExternalLinks(
		not_null<Main::Session*> session,
		TextWithEntities &text) {
	const auto local = [&](const QString &url) {
		return !UrlRequiresConfirmation(session, QUrl::fromUserInput(url));
	};
	const auto notLocal = [&](const EntityInText &entity) {
		if (entity.type() == EntityType::CustomUrl) {
			return !local(entity.data());
		} else if (entity.type() == EntityType::Url) {
			return !local(text.text.mid(entity.offset(), entity.length()));
		} else {
			return false;
		}
	};
	text.entities.erase(
		ranges::remove_if(text.entities, notLocal),
		text.entities.end());
}

struct RegistrationPoint {
	int64 id = 0;
	TimeId date = 0;
};

// Monotone (user id -> most likely registration date) curve, UTC.
// Taken from Ghostgram/Features/RegDate/Sources/Estimate/GGRegDateAnchors.swift
// of github.com/ichmagmaus111/ghostgram at 534cabe35b (data version 1):
// a fit of 479 observations from 9 public sources, linear between nodes.
constexpr RegistrationPoint kRegistrationPoints[] = {
	{0, 1376438400},
	{25, 1382662080},
	{217509, 1382662080},
	{1000000, 1383264000},
	{2768400, 1383264000},
	{3941874, 1388448000},
	{6256671, 1388448000},
	{7679610, 1390013486},
	{11538510, 1391212000},
	{15835240, 1392940000},
	{23646070, 1393459000},
	{38015510, 1393459000},
	{44634660, 1393632000},
	{44634663, 1398167867},
	{45641754, 1399334400},
	{46145305, 1400198000},
	{54845230, 1400198000},
	{54845238, 1408075000},
	{60457419, 1411257000},
	{63263518, 1414454000},
	{72776425, 1425600000},
	{95308423, 1426204800},
	{101260938, 1427835600},
	{101323197, 1428238000},
	{103151531, 1432771000},
	{103204956, 1433032767},
	{109393468, 1434020244},
	{110306836, 1434314267},
	{111220210, 1436244004},
	{112594714, 1437711062},
	{115831565, 1437782000},
	{116812045, 1438524943},
	{122600695, 1438524943},
	{124872440, 1439856000},
	{125828524, 1441324000},
	{127929226, 1442623033},
	{130029930, 1444003200},
	{139646974, 1444273680},
	{143445120, 1448928000},
	{148670295, 1449370973},
	{152079341, 1449370973},
	{155983984, 1452211200},
	{157242073, 1453420000},
	{171295414, 1454045640},
	{172144707, 1457481600},
	{181783990, 1457850160},
	{188459175, 1460246400},
	{222021230, 1465344000},
	{225034350, 1466208000},
	{267103082, 1472677200},
	{278941742, 1473142920},
	{279333717, 1473465600},
	{281973858, 1475301600},
	{285253070, 1476835000},
	{294851030, 1479600000},
	{297621220, 1481846000},
	{310042805, 1482969000},
	{328122828, 1485561600},
	{328594461, 1487205960},
	{341546272, 1487894400},
	{352940995, 1489772244},
	{369669040, 1490918000},
	{399597034, 1496257200},
	{400169470, 1501459000},
	{409518943, 1502884799},
	{441343998, 1506848400},
	{484976017, 1516084200},
	{501535268, 1518638400},
	{554990133, 1521187200},
	{573604341, 1523822400},
	{601341900, 1529625600},
	{616816630, 1531829829},
	{655730151, 1532821500},
	{675879664, 1537472400},
	{681896077, 1539510300},
	{695531082, 1541019600},
	{707751101, 1542304800},
	{717887953, 1543708800},
	{727572658, 1543708800},
	{774723749, 1545429600},
	{796147074, 1550821640},
	{805158066, 1557302400},
	{820050353, 1561951200},
	{871177218, 1563208000},
	{920778234, 1563290000},
	{925078064, 1568803164},
	{928636984, 1568803164},
	{970269006, 1573862399},
	{979195258, 1574693313},
	{998725855, 1574693313},
	{1020178246, 1576957200},
	{1040151903, 1577817000},
	{1047436723, 1579175999},
	{1054883348, 1579175999},
	{1057704545, 1580393640},
	{1059845356, 1580504400},
	{1081554131, 1580504400},
	{1130489378, 1583002800},
	{1145856008, 1586342040},
	{1170070589, 1588284000},
	{1202891775, 1588310640},
	{1227964864, 1590890220},
	{1232810985, 1590934320},
	{1279382482, 1596127860},
	{1334421204, 1597360114},
	{1355237859, 1597579199},
	{1382531194, 1600188120},
	{1383895297, 1601492400},
	{1384783988, 1602849599},
	{1415571066, 1604817900},
	{1432107681, 1606762800},
	{1439824385, 1606813200},
	{1468778795, 1608119999},
	{1485680298, 1608119999},
	{1488888443, 1609437600},
	{1514538817, 1609441200},
	{1530835010, 1612119600},
	{1578805533, 1612670965},
	{1658586909, 1613148540},
	{1660971491, 1613329440},
	{1692464211, 1615402500},
	{1719536397, 1618786994},
	{1787945512, 1618786994},
	{1788462219, 1622458800},
	{1824157837, 1622576984},
	{1881430862, 1623440640},
	{1893429550, 1623440640},
	{1938921569, 1626933294},
	{1972424006, 1630556060},
	{1974255900, 1631669400},
	{1991158335, 1631989620},
	{2030606431, 1631992680},
	{2041327411, 1631992680},
	{2078711279, 1634321820},
	{2104178931, 1636714020},
	{2107129437, 1636714020},
	{2120496865, 1636714020},
	{2123596685, 1636714020},
	{2138472342, 1636714020},
	{2250645697, 1637590800},
	{5017595749, 1638309600},
	{5022636255, 1638748800},
	{5031711230, 1639094400},
	{5035057026, 1640352847},
	{5045293264, 1640977200},
	{5047148663, 1642032000},
	{5065900297, 1642550400},
	{5070164216, 1643662800},
	{5106451106, 1643662800},
	{5124771193, 1645488000},
	{5149590651, 1645587469},
	{5177789190, 1646098973},
	{5249255238, 1646383782},
	{5259159476, 1647907200},
	{5288930461, 1647907200},
	{5290311052, 1648760400},
	{5297354132, 1648760400},
	{5304951856, 1650844800},
	{5308260177, 1651178335},
	{5318972588, 1651937341},
	{5351497367, 1652024520},
	{5387234031, 1653633233},
	{5394432429, 1654341504},
	{5396587273, 1654553525},
	{5402540458, 1656615600},
	{5409444610, 1657497600},
	{5434011049, 1657497600},
	{5442755368, 1658534400},
	{5468433192, 1658570663},
	{5468950164, 1658576503},
	{5488407539, 1658796303},
	{5502182459, 1658951911},
	{5505809357, 1658992882},
	{5513192189, 1659076282},
	{5515826405, 1659106039},
	{5537251684, 1659454701},
	{5546930145, 1659812776},
	{5562600534, 1660392534},
	{5568348673, 1660605198},
	{5614111200, 1662259343},
	{5644144970, 1663287005},
	{5666819340, 1664039074},
	{5684689868, 1664359946},
	{5694365966, 1664533684},
	{5721138769, 1665014400},
	{5731896020, 1665100800},
	{5756095415, 1665100800},
	{5765259845, 1665709961},
	{5778063231, 1667088000},
	{5795034000, 1667477640},
	{5795660441, 1667692800},
	{5802242180, 1671821040},
	{5815100469, 1672274140},
	{5839137822, 1672461662},
	{5844821188, 1672506000},
	{5853442730, 1672912537},
	{5854845236, 1672978670},
	{5862080962, 1673117760},
	{5869015726, 1673117760},
	{5869978651, 1674259200},
	{5885964106, 1675280531},
	{5931294587, 1675280531},
	{5964221956, 1676464256},
	{5982648124, 1680976608},
	{5983753471, 1681228934},
	{6025903944, 1683678523},
	{6032606998, 1683691443},
	{6058560984, 1683741468},
	{6101607245, 1683824437},
	{6108395402, 1684368000},
	{6132325730, 1684368000},
	{6135597783, 1684886400},
	{6182056052, 1685125813},
	{6188508923, 1685159066},
	{6227468000, 1685690678},
	{6254094947, 1686923555},
	{6271031786, 1687707763},
	{6279839148, 1688115560},
	{6297860552, 1688399160},
	{6321562426, 1688688000},
	{6326011828, 1689247446},
	{6364973680, 1692114535},
	{6373693377, 1692301478},
	{6386727079, 1692301478},
	{6401027363, 1692301478},
	{6429580803, 1692763697},
	{6433264186, 1695301080},
	{6451891234, 1695301080},
	{6507968649, 1695301080},
	{6514802524, 1695548414},
	{6520005489, 1695748458},
	{6527226055, 1696026075},
	{6536173556, 1696370089},
	{6609923725, 1698961564},
	{6670760749, 1700017225},
	{6684986493, 1700264075},
	{6703731755, 1700589348},
	{6732829831, 1701094266},
	{6765129195, 1701723768},
	{6813121418, 1702876763},
	{6845043276, 1703643673},
	{6854829938, 1703878793},
	{6872061796, 1704292782},
	{6903333095, 1704758400},
	{6947316117, 1704758400},
	{7000000000, 1711876800},
	{7043862629, 1712361600},
	{7078066115, 1715299200},
	{7085776398, 1715299200},
	{7100000000, 1715299200},
	{7103700000, 1715299200},
	{7104310277, 1715623200},
	{7107400000, 1715623200},
	{7129689557, 1718272800},
	{7191651807, 1721136600},
	{7224009547, 1721136600},
	{7229898489, 1722556800},
	{7242296450, 1722556800},
	{7243375923, 1723075200},
	{7280136256, 1723381271},
	{7450316621, 1725010893},
	{7458668365, 1725098400},
	{7469700000, 1726221600},
	{7490725809, 1729065600},
	{7509628470, 1729065600},
	{7519637470, 1732991400},
	{7538420552, 1733071378},
	{7552093513, 1733313281},
	{7600158321, 1734163646},
	{7654300000, 1734435819},
	{7657400000, 1734449730},
	{7683400000, 1734566400},
	{7708562823, 1735510378},
	{7716779743, 1735818634},
	{7747102337, 1736956180},
	{7798375391, 1738879679},
	{7817256746, 1739327871},
	{7825518194, 1739490110},
	{7829910989, 1739576376},
	{7834356221, 1739663672},
	{7851389063, 1739998165},
	{7884373548, 1740645919},
	{7899152800, 1740936155},
	{7939700000, 1741732426},
	{7947063900, 1741877039},
	{7967849270, 1742285225},
	{8000499714, 1742926418},
	{8017192943, 1743254242},
	{8044853035, 1744655298},
	{8102800000, 1747833293},
	{8135088730, 1749604109},
	{8179125032, 1752019200},
	{8200159552, 1757635200},
	{8238766847, 1757789280},
	{8325327280, 1758656732},
	{8343319353, 1758972025},
	{8343786378, 1758980209},
	{8369442459, 1759429805},
	{8379482677, 1759605750},
	{8384648263, 1761177600},
	{8454563873, 1761177600},
	{8461412540, 1761350400},
	{8461579295, 1762300800},
	{8480708838, 1762300800},
	{8541347308, 1762819200},
	{8559682245, 1763236800},
	{8587904527, 1764543600},
	{8608772687, 1781567999},
	{8631703617, 1781567999},
	{8636864387, 1781567999},
	{8681634807, 1781567999},
	{8695922065, 1785314402},
	{8874594977, 1785314402},
	{8893764521, 1785314402},
	{8966578432, 1785314402},
};

// ID growth over the last year of the curve, used past its last node.
constexpr auto kRegistrationIdsPerSecond = 23.650657;

[[nodiscard]] QString FormatRegistrationTime(QString prefix, TimeId date) {
	return prefix + base::unixtime::parse(date)
		.toString(QLocale::system().dateFormat(QLocale::ShortFormat));
}

[[nodiscard]] QString EstimateRegistrationTime(int64 userId) {
	const auto &last = kRegistrationPoints[std::size(kRegistrationPoints) - 1];
	if (userId >= last.id) {
		const auto extrapolated = last.date
			+ (userId - last.id) / kRegistrationIdsPerSecond;
		const auto now = base::unixtime::now();
		return FormatRegistrationTime(
			u"~ "_q,
			TimeId(std::clamp(extrapolated, double(last.date), double(now))));
	}
	const auto till = ranges::upper_bound(
		kRegistrationPoints,
		userId,
		ranges::less(),
		&RegistrationPoint::id);
	const auto from = till - 1;
	const auto t = double(userId - from->id) / (till->id - from->id);
	return FormatRegistrationTime(
		u"~ "_q,
		TimeId(from->date + t * (till->date - from->date)));
}

} // namespace

rpl::producer<TextWithEntities> RegistrationValue(not_null<PeerData*> peer) {
	const auto userId = int64(peer->id.to<UserId>().bare);
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::FullInfo | UpdateFlag::BarSettings
	) | rpl::map([=] {
		const auto month = peer->registrationMonth();
		const auto year = peer->registrationYear();
		return (month && year)
			? langMonthOfYearFull(month, year)
			: EstimateRegistrationTime(userId);
	}) | rpl::distinct_until_changed() | rpl::map(Ui::Text::WithEntities);
}

rpl::producer<TextWithEntities> DataCenterValue(not_null<PeerData*> peer) {
	auto dcId = std::get<StorageFileLocation>(peer->userpicLocation().file().data).dcId();
	auto dcName = (dcId == 1) ? "1, Miami" :
		(dcId == 2) ? "2, Amsterdam" :
		(dcId == 3) ? "3, Miami" :
		(dcId == 4) ? "4, Amsterdam" :
		(dcId == 5) ? "5, Singapore" :
		"Unknown";
	return rpl::single(QString(dcName)) | rpl::map(Ui::Text::WithEntities);
}

rpl::producer<QString> NameValue(not_null<PeerData*> peer) {
	if (const auto broadcast = peer->monoforumBroadcast()) {
		return NameValue(broadcast);
	}
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::Name
	) | rpl::map([=] { return peer->name(); });
}

rpl::producer<QString> TitleValue(not_null<Data::ForumTopic*> topic) {
	return topic->session().changes().topicFlagsValue(
		topic,
		Data::TopicUpdate::Flag::Title
	) | rpl::map([=] { return topic->title(); });
}

rpl::producer<DocumentId> IconIdValue(not_null<Data::ForumTopic*> topic) {
	return topic->session().changes().topicFlagsValue(
		topic,
		Data::TopicUpdate::Flag::IconId
	) | rpl::map([=] { return topic->iconId(); });
}

rpl::producer<int32> ColorIdValue(not_null<Data::ForumTopic*> topic) {
	return topic->session().changes().topicFlagsValue(
		topic,
		Data::TopicUpdate::Flag::ColorId
	) | rpl::map([=] { return topic->colorId(); });
}

rpl::producer<TextWithEntities> PhoneValue(not_null<UserData*> user) {
	return rpl::merge(
		Countries::Instance().updated(),
		user->session().changes().peerFlagsValue(
			user,
			UpdateFlag::PhoneNumber) | rpl::to_empty
	) | rpl::map([=] {
		return tr::marked(Ui::FormatPhone(user->phone()));
	});
}

QString PublicLinkByPhone(not_null<UserData*> user) {
	return user->session().createInternalLinkFull('+' + user->phone());
}

rpl::producer<TextWithEntities> PhoneOrHiddenValue(not_null<UserData*> user) {
	return rpl::combine(
		PhoneValue(user),
		PlainUsernameValue(user),
		PlainAboutValue(user),
		tr::lng_info_mobile_hidden()
	) | rpl::map([user](
			const TextWithEntities &phone,
			const QString &username,
			const QString &about,
			const QString &hidden) {
		if (phone.text.isEmpty() && username.isEmpty() && about.isEmpty()) {
			return tr::marked(hidden);
		} else if (IsCollectiblePhone(user)) {
			return tr::link(phone, u"internal:collectible_phone/"_q
				+ user->phone() + '@' + QString::number(user->id.value));
		} else {
			return phone;
		}
	});
}

rpl::producer<TextWithEntities> PhoneWithSpoilerValue(
		not_null<UserData*> user,
		rpl::producer<TextWithEntities> phone) {
	if (!user->isSelf()) {
		return phone;
	}
	return rpl::combine(
		std::move(phone),
		user->session().settings().phoneNumberHiddenValue()
	) | rpl::map([](const TextWithEntities &phone, bool hidden) {
		return hidden
			? Ui::Text::Wrapped(phone, EntityType::Spoiler)
			: phone;
	});
}

void CopyPhoneToClipboard(rpl::producer<TextWithEntities> phone) {
	auto text = rpl::variable<TextWithEntities>(
		std::move(phone)).current().text;
	text.replace(' ', QString()).replace('-', QString());
	TextUtilities::SetClipboardText({ text });
}

rpl::producer<TextWithEntities> UsernameValue(
		not_null<PeerData*> peer,
		bool primary) {
	return (primary
		? PlainPrimaryUsernameValue(peer)
		: (PlainUsernameValue(peer) | rpl::type_erased)
	) | rpl::map([](QString &&username) {
		return username.isEmpty()
			? tr::marked()
			: tr::marked('@' + username);
	});
}

QString UsernameUrl(
		not_null<PeerData*> peer,
		const QString &username,
		bool link) {
	const auto type = !peer->isUsernameEditable(username)
		? u"collectible_username"_q
		: link
		? u"username_link"_q
		: u"username_regular"_q;
	return u"internal:"_q
		+ type
		+ u"/"_q
		+ username
		+ "@"
		+ QString::number(peer->id.value);
}

rpl::producer<std::vector<TextWithEntities>> UsernamesValue(
		not_null<PeerData*> peer) {
	const auto map = [=](const std::vector<QString> &usernames) {
		return ranges::views::all(
			usernames
		) | ranges::views::transform([&](const QString &u) {
			return tr::link(u, UsernameUrl(peer, u));
		}) | ranges::to_vector;
	};
	auto value = rpl::merge(
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Username),
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Usernames)
	);
	if (const auto user = peer->asUser()) {
		return std::move(value) | rpl::map([=] {
			return map(user->usernames());
		});
	} else if (const auto channel = peer->asChannel()) {
		return std::move(value) | rpl::map([=] {
			return map(channel->usernames());
		});
	} else {
		return rpl::single(std::vector<TextWithEntities>());
	}
}

TextWithEntities AboutWithEntities(
		not_null<PeerData*> peer,
		const QString &value) {
	auto flags = TextParseLinks | TextParseMentions;
	const auto user = peer->asUser();
	const auto isBot = user && user->isBot();
	const auto isPremium = user && user->isPremium();
	if (!user) {
		flags |= TextParseHashtags;
	} else if (isBot) {
		flags |= TextParseHashtags | TextParseBotCommands;
	}
	const auto stripExternal = peer->isChat()
		|| peer->isMegagroup()
		|| (user && !isBot && !isPremium);
	auto result = TextWithEntities{ value };
	TextUtilities::ParseEntities(result, flags);
	if (stripExternal) {
		StripExternalLinks(&peer->session(), result);
	}
	return result;
}

rpl::producer<TextWithEntities> AboutValue(not_null<PeerData*> peer) {
	return PlainAboutValue(
		peer
	) | rpl::map([peer](const QString &value) {
		return AboutWithEntities(peer, value);
	});
}

QString TopicLink(not_null<Data::ForumTopic*> topic, bool full) {
	const auto channel = topic->channel();
	const auto id = topic->rootId();
	const auto base = channel->hasUsername()
		? channel->username()
		: "c/" + QString::number(peerToChannel(channel->id).bare);
	return channel->session().createInternalLinkFull(full
		? base + '/' + QString::number(id.bare)
		: base);
}

rpl::producer<LinkWithUrl> LinkValue(
		not_null<PeerData*> peer,
		bool primary,
		MsgId rootId) {
	return (primary
		? PlainPrimaryUsernameValue(peer)
		: PlainUsernameValue(peer) | rpl::type_erased
	) | rpl::map([=](QString &&username) {
		if (username.isEmpty()) {
			if (const auto topic
				= rootId ? peer->forumTopicFor(rootId) : nullptr) {
				const auto link = TopicLink(topic, false);
				return LinkWithUrl{
					.text = link,
					.url = link,
				};
			} else {
				return LinkWithUrl{};
			}
		} else {
			return LinkWithUrl{
				.text = peer->session().createInternalLinkFull(username),
				.url = UsernameUrl(peer, username, true),
			};
		}
		return LinkWithUrl{
			.text = (username.isEmpty()
				? QString()
				: peer->session().createInternalLinkFull(username)),
			.url = (username.isEmpty()
				? QString()
				: UsernameUrl(peer, username, true)),
		};
	});
}

rpl::producer<const ChannelLocation*> LocationValue(
		not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::ChannelLocation
	) | rpl::map([=] {
		return channel->getLocation();
	});
}

rpl::producer<bool> NotificationsEnabledValue(
		not_null<Data::Thread*> thread) {
	const auto topic = thread->asTopic();
	if (!topic) {
		return NotificationsEnabledValue(thread->peer());
	}
	const auto weak = base::make_weak(topic);
	return rpl::merge(
		topic->session().changes().topicFlagsValue(
			topic,
			Data::TopicUpdate::Flag::Notifications
		) | rpl::to_empty,
		topic->session().changes().peerUpdates(
			topic->peer(),
			UpdateFlag::Notifications
		) | rpl::to_empty,
		topic->owner().notifySettings().defaultUpdates(topic->peer())
	) | rpl::filter([=] {
		return (weak.get() != nullptr);
	}) | rpl::map([=] {
		return !topic->owner().notifySettings().isMuted(topic);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> NotificationsEnabledValue(not_null<PeerData*> peer) {
	return rpl::merge(
		peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Notifications
		) | rpl::to_empty,
		peer->owner().notifySettings().defaultUpdates(peer)
	) | rpl::map([=] {
		return !peer->owner().notifySettings().isMuted(peer);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> IsContactValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::IsContact
	) | rpl::map([=] {
		return user->isContact();
	});
}

bool CanReportBot(not_null<UserData*> user) {
	return user->isBot()
		&& !user->isSelf()
		&& !user->isSupport()
		&& !user->isVerifyCodes();
}

[[nodiscard]] rpl::producer<QString> InviteToChatButton(
		not_null<UserData*> user) {
	if (!user->isBot()
		|| user->isRepliesChat()
		|| user->isVerifyCodes()
		|| user->isSupport()) {
		return rpl::single(QString());
	}
	using Flag = Data::PeerUpdate::Flag;
	return user->session().changes().peerFlagsValue(
		user,
		Flag::BotCanBeInvited | Flag::Rights
	) | rpl::map([=] {
		const auto info = user->botInfo.get();
		return info->cantJoinGroups
			? (info->channelAdminRights
				? tr::lng_profile_invite_to_channel(tr::now)
				: QString())
			: (info->channelAdminRights
				? tr::lng_profile_add_bot_as_admin(tr::now)
				: tr::lng_profile_invite_to_group(tr::now));
	});
}

[[nodiscard]] rpl::producer<QString> InviteToChatAbout(
		not_null<UserData*> user) {
	if (!user->isBot()
		|| user->isRepliesChat()
		|| user->isVerifyCodes()
		|| user->isSupport()) {
		return rpl::single(QString());
	}
	using Flag = Data::PeerUpdate::Flag;
	return user->session().changes().peerFlagsValue(
		user,
		Flag::BotCanBeInvited | Flag::Rights
	) | rpl::map([=] {
		const auto info = user->botInfo.get();
		return (info->cantJoinGroups || !info->groupAdminRights)
			? (info->channelAdminRights
				? tr::lng_profile_invite_to_channel_about(tr::now)
				: QString())
			: (info->channelAdminRights
				? tr::lng_profile_add_bot_as_admin_about(tr::now)
				: tr::lng_profile_invite_to_group_about(tr::now));
	});
}

rpl::producer<bool> CanShareContactValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::CanShareContact
	) | rpl::map([=] {
		return user->canShareThisContact();
	});
}

rpl::producer<bool> CanAddContactValue(not_null<UserData*> user) {
	using namespace rpl::mappers;
	if (user->isBot() || user->isSelf() || user->isInaccessible()) {
		return rpl::single(false);
	}
	return IsContactValue(
		user
	) | rpl::map(!_1);
}

rpl::producer<Data::Birthday> BirthdayValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::Birthday
	) | rpl::map([=] {
		return user->birthday();
	});
}

rpl::producer<ChannelData*> PersonalChannelValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::PersonalChannel
	) | rpl::map([=] {
		const auto channelId = user->personalChannelId();
		return channelId ? user->owner().channel(channelId).get() : nullptr;
	});
}

rpl::producer<bool> AmInChannelValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::ChannelAmIn
	) | rpl::map([=] {
		return channel->amIn();
	});
}

rpl::producer<int> MembersCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Members
		) | rpl::map([=] {
			return chat->amIn()
				? std::max(chat->count, int(chat->participants.size()))
				: 0;
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Members
		) | rpl::map([=] {
			return channel->membersCount();
		});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> PendingRequestsCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::PendingRequests
		) | rpl::map([=] {
			return chat->pendingRequestsCount();
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::PendingRequests
		) | rpl::map([=] {
			return channel->pendingRequestsCount();
		});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> AdminsCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Admins | UpdateFlag::Rights
		) | rpl::map([=] {
			return chat->participants.empty()
				? 0
				: int(chat->admins.size() + (chat->creator ? 1 : 0));
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Admins | UpdateFlag::Rights
		) | rpl::map([=] {
			return channel->canViewAdmins()
				? channel->adminsCount()
				: 0;
		});
	}
	Unexpected("User in AdminsCountValue().");
}


rpl::producer<int> RestrictionsCountValue(not_null<PeerData*> peer) {
	const auto countOfRestrictions = [](
			Data::RestrictionsSetOptions options,
			ChatRestrictions restrictions) {
		auto count = 0;
		const auto list = Data::ListOfRestrictions(options);
		for (const auto &f : list) {
			if (restrictions & f) count++;
		}
		return int(list.size()) - count;
	};

	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return countOfRestrictions({}, chat->defaultRestrictions());
		});
	} else if (const auto channel = peer->asChannel()) {
		return rpl::combine(
			Data::PeerFlagValue(channel, ChannelData::Flag::Forum),
			channel->session().changes().peerFlagsValue(
				channel,
				UpdateFlag::Rights)
		) | rpl::map([=] {
			return countOfRestrictions(
				{ .isForum = channel->isForum() },
				channel->defaultRestrictions());
		});
	}
	Unexpected("User in RestrictionsCountValue().");
}

rpl::producer<not_null<PeerData*>> MigratedOrMeValue(
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Migration
		) | rpl::map([=] {
			return chat->migrateToOrMe();
		});
	} else {
		return rpl::single(peer);
	}
}

rpl::producer<int> RestrictedCountValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::BannedUsers | UpdateFlag::Rights
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->restrictedCount()
			: 0;
	});
}

rpl::producer<int> KickedCountValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::BannedUsers | UpdateFlag::Rights
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->kickedCount()
			: 0;
	});
}

rpl::producer<int> SharedMediaCountValue(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		PeerData *migrated,
		Storage::SharedMediaType type) {
	auto aroundId = 0;
	auto limit = 0;
	auto updated = SharedMediaMergedViewer(
		&peer->session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				peer->id,
				topicRootId,
				monoforumPeerId,
				migrated ? migrated->id : 0,
				aroundId),
			type),
		limit,
		limit
	) | rpl::map([](const SparseIdsMergedSlice &slice) {
		return slice.fullCount();
	}) | rpl::filter_optional();
	return rpl::single(0) | rpl::then(std::move(updated));
}

rpl::producer<int> CommonGroupsCountValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::CommonChats
	) | rpl::map([=] {
		return user->commonChatsCount();
	});
}

rpl::producer<int> SimilarPeersCountValue(
		not_null<PeerData*> peer) {
	const auto participants = &peer->session().api().chatParticipants();
	participants->loadSimilarPeers(peer);
	return rpl::single(peer) | rpl::then(
		participants->similarLoaded()
	) | rpl::filter(
		rpl::mappers::_1 == peer
	) | rpl::map([=] {
		const auto &similar = participants->similar(peer);
		return int(similar.list.size()) + similar.more;
	});
}

rpl::producer<int> SavedSublistCountValue(
		not_null<PeerData*> peer) {
	const auto saved = &peer->owner().savedMessages();
	const auto sublist = saved->sublist(peer);
	if (!sublist->fullCount().has_value()) {
		sublist->loadFullCount();
		return rpl::single(0) | rpl::then(sublist->fullCountValue());
	}
	return sublist->fullCountValue();
}

rpl::producer<int> PeerGiftsCountValue(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::PeerGifts
	) | rpl::map([=] {
		return peer->peerGiftsCount();
	});
}

rpl::producer<bool> CanAddMemberValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return chat->canAddMembers();
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return channel->canAddMembers();
		});
	}
	return rpl::single(false);
}

rpl::producer<bool> CanViewParticipantsValue(
		not_null<ChannelData*> megagroup) {
	if (megagroup->amCreator()) {
		return rpl::single(true);
	}
	return rpl::combine(
		megagroup->session().changes().peerFlagsValue(
			megagroup,
			UpdateFlag::Rights),
		megagroup->flagsValue(),
		[=] { return megagroup->canViewMembers(); }
	) | rpl::distinct_until_changed();
}

template <typename Flag, typename Peer>
rpl::producer<BadgeType> BadgeValueFromFlags(Peer peer) {
	return rpl::combine(
		Data::PeerFlagsValue(
			peer,
			Flag::Verified | Flag::Scam | Flag::Fake),
		Data::PeerPremiumValue(peer)
	) | rpl::map([=](base::flags<Flag> value, bool premium) {
		return (value & Flag::Scam)
			? BadgeType::Scam
			: (value & Flag::Fake)
			? BadgeType::Fake
			: peer->isMonoforum()
			? BadgeType::Direct
			: (value & Flag::Verified)
			? BadgeType::Verified
			: premium
			? BadgeType::Premium
			: BadgeType::None;
	});
}

rpl::producer<BadgeType> BadgeValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return BadgeValueFromFlags<UserDataFlag>(user);
	} else if (const auto channel = peer->asChannel()) {
		return BadgeValueFromFlags<ChannelDataFlag>(channel);
	}
	return rpl::single(BadgeType::None);
}

rpl::producer<EmojiStatusId> EmojiStatusIdValue(not_null<PeerData*> peer) {
	if (peer->isChat()) {
		return rpl::single(EmojiStatusId());
	}
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::map([=] { return peer->emojiStatusId(); });
}

rpl::producer<EmojiStatusId> ShownEmojiStatusIdValue(
		not_null<PeerData*> peer) {
	if (peer->isChat()) {
		return rpl::single(EmojiStatusId());
	}
	return rpl::merge(
		EmojiStatusIdValue(peer) | rpl::to_empty,
		peer->barSettingsValue() | rpl::to_empty
	) | rpl::map([=] {
		return peer->hideEmojiStatus()
			? EmojiStatusId()
			: peer->emojiStatusId();
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> BirthdayLabelText(
		rpl::producer<Data::Birthday> birthday) {
	return std::move(birthday) | rpl::map([](Data::Birthday value) {
		return rpl::conditional(
			Data::IsBirthdayTodayValue(value),
			tr::lng_info_birthday_today_label(),
			tr::lng_info_birthday_label());
	}) | rpl::flatten_latest();
}

rpl::producer<TextWithEntities> BirthdayValueMarkedText(
		not_null<UserData*> user,
		rpl::producer<Data::Birthday> birthday) {
	const auto session = &user->session();
	const auto cake = Data::BirthdayCake();
	const auto emoji = Ui::Emoji::Find(cake);
	return rpl::combine(
		BirthdayValueText(std::move(birthday)),
		rpl::single(rpl::empty) | rpl::then(
			session->emojiStickersPack().refreshed())
	) | rpl::map([=](const QString &text, const auto &) {
		auto result = TextWithEntities{ text };
		const auto position = emoji ? text.indexOf(cake) : -1;
		if (position < 0) {
			return result;
		}
		const auto id = session->emojiStickersPack().standardEmojiDocument(
			emoji);
		if (id) {
			result.entities.push_back({
				EntityType::CustomEmoji,
				int(position),
				int(cake.size()),
				Data::SerializeCustomEmojiId(id),
			});
		}
		return result;
	});
}

rpl::producer<QString> BirthdayValueText(
		rpl::producer<Data::Birthday> birthday,
		bool fullMonth) {
	return std::move(
		birthday
	) | rpl::map([=](Data::Birthday value) -> rpl::producer<QString> {
		if (!value) {
			return rpl::single(QString());
		}
		return Data::IsBirthdayTodayValue(
			value
		) | rpl::map([=](bool today) {
			auto text = Data::BirthdayText(value, fullMonth);
			if (const auto age = Data::BirthdayAge(value)) {
				text = (today
					? tr::lng_info_birthday_today_years
					: tr::lng_info_birthday_years)(
						tr::now,
						lt_count,
						age,
						lt_date,
						text);
			}
			if (today) {
				text = tr::lng_info_birthday_today(
					tr::now,
					lt_emoji,
					Data::BirthdayCake(),
					lt_date,
					text);
			}
			return text;
		});
	}) | rpl::flatten_latest();
}

} // namespace Profile
} // namespace Info
