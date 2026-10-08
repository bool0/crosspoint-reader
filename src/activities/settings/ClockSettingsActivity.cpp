#include "ClockSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>

#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TimezonePickerActivity.h"
#include "components/UITheme.h"
#include "util/Timezones.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_TIMEZONE = 0,
  ITEM_DST,
  ITEM_FORMAT,
  ITEM_SHOW_ON_HOME,
  ITEM_SYNC,
  ITEM_STYLE,
  ITEM_FACE_DATE,
  ITEM_FACE_BATTERY,
  ITEM_FACE_MEMO,
  ITEM_FACE_DARK,
  ITEM_KEEP_TIME,
};

const StrId menuNames[ClockSettingsActivity::ITEM_COUNT] = {
    StrId::STR_TIMEZONE,        StrId::STR_CLOCK_DST,   StrId::STR_CLOCK_FORMAT,       StrId::STR_CLOCK_IN_HEADER,
    StrId::STR_CLOCK_SYNC_NOW,  StrId::STR_CLOCK_STYLE, StrId::STR_CLOCK_SHOW_DATE,    StrId::STR_CLOCK_SHOW_BATTERY,
    StrId::STR_CLOCK_SHOW_MEMO, StrId::STR_CLOCK_DARK,  StrId::STR_KEEP_TIME_IN_SLEEP,
};

const StrId styleNames[CrossPointSettings::CLOCK_STYLE_COUNT] = {
    StrId::STR_CLOCK_STYLE_ROUNDED, StrId::STR_CLOCK_STYLE_THIN, StrId::STR_CLOCK_STYLE_BOLD,
    StrId::STR_CLOCK_STYLE_LCD};

const StrId dstNames[CrossPointSettings::CLOCK_DST_MODE_COUNT] = {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON,
                                                                  StrId::STR_STATE_OFF};
}  // namespace

ClockSettingsActivity::ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ClockSettings", renderer, mappedInput) {}

void ClockSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* ClockSettingsActivity::headerTitle() const { return tr(STR_CLOCK); }

void ClockSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case ITEM_TIMEZONE:
      if (auto activity = makeUniqueNoThrow<TimezonePickerActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: TimezonePickerActivity");
      }
      return;
    case ITEM_DST:
      SETTINGS.clockDst = (SETTINGS.clockDst + 1) % CrossPointSettings::CLOCK_DST_MODE_COUNT;
      timezones::applyToClock();
      break;
    case ITEM_FORMAT:
      SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % 2;
      break;
    case ITEM_SHOW_ON_HOME:
      SETTINGS.clockShowInHeader = (SETTINGS.clockShowInHeader + 1) % 2;
      break;
    case ITEM_STYLE:
      SETTINGS.clockFaceStyle = (SETTINGS.clockFaceStyle + 1) % CrossPointSettings::CLOCK_STYLE_COUNT;
      break;
    case ITEM_FACE_DATE:
      SETTINGS.clockFaceDate = !SETTINGS.clockFaceDate;
      break;
    case ITEM_FACE_BATTERY:
      SETTINGS.clockFaceBattery = !SETTINGS.clockFaceBattery;
      break;
    case ITEM_FACE_MEMO:
      SETTINGS.clockFaceMemo = !SETTINGS.clockFaceMemo;
      break;
    case ITEM_FACE_DARK:
      SETTINGS.clockFaceDark = !SETTINGS.clockFaceDark;
      break;
    case ITEM_KEEP_TIME:
      SETTINGS.keepTimeInSleep = !SETTINGS.keepTimeInSleep;
      break;
    case ITEM_SYNC:
      if (auto activity = makeUniqueNoThrow<ClockSyncActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: ClockSyncActivity");
      }
      return;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void ClockSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Every value is a flash/translation string or the member time buffer, so
  // the render pass allocates nothing.
  rowItems_[ITEM_TIMEZONE].value = timezones::table()[timezones::activeIndex()].name;
  const uint8_t dst = SETTINGS.clockDst < CrossPointSettings::CLOCK_DST_MODE_COUNT ? SETTINGS.clockDst : uint8_t{0};
  rowItems_[ITEM_DST].value = I18N.get(dstNames[dst]);
  rowItems_[ITEM_FORMAT].value = SETTINGS.clockFormat == 1 ? tr(STR_CLOCK_FORMAT_12H) : tr(STR_CLOCK_FORMAT_24H);
  GUI.setCheckboxRow(rowItems_[ITEM_SHOW_ON_HOME], SETTINGS.clockShowInHeader);
  rowItems_[ITEM_STYLE].value = I18N.get(
      styleNames[SETTINGS.clockFaceStyle < CrossPointSettings::CLOCK_STYLE_COUNT ? SETTINGS.clockFaceStyle : 0]);
  GUI.setCheckboxRow(rowItems_[ITEM_FACE_DATE], SETTINGS.clockFaceDate);
  GUI.setCheckboxRow(rowItems_[ITEM_FACE_BATTERY], SETTINGS.clockFaceBattery);
  GUI.setCheckboxRow(rowItems_[ITEM_FACE_MEMO], SETTINGS.clockFaceMemo);
  GUI.setCheckboxRow(rowItems_[ITEM_FACE_DARK], SETTINGS.clockFaceDark);
  GUI.setCheckboxRow(rowItems_[ITEM_KEEP_TIME], SETTINGS.keepTimeInSleep);
  // The sync row's value is the current time itself: it confirms the sync,
  // previews format/zone changes, and reads "Not Set" until the first sync.
  rowItems_[ITEM_SYNC].value =
      SETTINGS.clockHasBeenSynced && halClock.formatTime(syncTime_, sizeof(syncTime_), SETTINGS.clockFormat == 1)
          ? syncTime_
          : tr(STR_NOT_SET);

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
