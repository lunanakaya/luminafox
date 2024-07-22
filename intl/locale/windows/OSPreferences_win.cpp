/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OSPreferences.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/WindowsVersion.h"
#include "nsReadableUtils.h"

#include <windows.h>

#ifndef __MINGW32__  // WinRT headers not yet supported by MinGW
#  include <roapi.h>
#  include <wrl.h>
#  include <Windows.System.UserProfile.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::System::UserProfile;
#endif

using namespace mozilla::intl;

OSPreferences::OSPreferences() {}

bool OSPreferences::ReadSystemLocales(nsTArray<nsCString>& aLocaleList) {
  return false;
}

bool OSPreferences::ReadRegionalPrefsLocales(nsTArray<nsCString>& aLocaleList) {
  return false;
}

static LCTYPE ToDateLCType(OSPreferences::DateTimeFormatStyle aFormatStyle) {
  switch (aFormatStyle) {
    case OSPreferences::DateTimeFormatStyle::None:
      return LOCALE_SLONGDATE;
    case OSPreferences::DateTimeFormatStyle::Short:
      return LOCALE_SSHORTDATE;
    case OSPreferences::DateTimeFormatStyle::Medium:
      return LOCALE_SSHORTDATE;
    case OSPreferences::DateTimeFormatStyle::Long:
      return LOCALE_SLONGDATE;
    case OSPreferences::DateTimeFormatStyle::Full:
      return LOCALE_SLONGDATE;
    case OSPreferences::DateTimeFormatStyle::Invalid:
    default:
      MOZ_ASSERT_UNREACHABLE("invalid date format");
      return LOCALE_SLONGDATE;
  }
}

static LCTYPE ToTimeLCType(OSPreferences::DateTimeFormatStyle aFormatStyle) {
  switch (aFormatStyle) {
    case OSPreferences::DateTimeFormatStyle::None:
      return LOCALE_STIMEFORMAT;
/*    case OSPreferences::DateTimeFormatStyle::Short:
      return LOCALE_SSHORTTIME;
    case OSPreferences::DateTimeFormatStyle::Medium:
      return LOCALE_SSHORTTIME;*/
    case OSPreferences::DateTimeFormatStyle::Long:
      return LOCALE_STIMEFORMAT;
    case OSPreferences::DateTimeFormatStyle::Full:
      return LOCALE_STIMEFORMAT;
    case OSPreferences::DateTimeFormatStyle::Invalid:
    default:
      MOZ_ASSERT_UNREACHABLE("invalid time format");
      return LOCALE_STIMEFORMAT;
  }
}

/**
 * Windows API includes regional preferences from the user only
 * if we pass empty locale string or if the locale string matches
 * the current locale.
 *
 * Since Windows API only allows us to retrieve two options - short/long
 * we map it to our four options as:
 *
 *   short  -> short
 *   medium -> short
 *   long   -> long
 *   full   -> long
 *
 * In order to produce a single date/time format, we use CLDR pattern
 * for combined date/time string, since Windows API does not provide an
 * option for this.
 */
bool OSPreferences::ReadDateTimePattern(DateTimeFormatStyle aDateStyle,
                                        DateTimeFormatStyle aTimeStyle,
                                        const nsACString& aLocale,
                                        nsACString& aRetVal) {
  nsAutoString localeName;
  CopyASCIItoUTF16(aLocale, localeName);

  bool isDate = aDateStyle != DateTimeFormatStyle::None &&
                aDateStyle != DateTimeFormatStyle::Invalid;
  bool isTime = aTimeStyle != DateTimeFormatStyle::None &&
                aTimeStyle != DateTimeFormatStyle::Invalid;

  // If both date and time are wanted, we'll initially read them into a
  // local string, and then insert them into the overall date+time pattern;
  nsAutoString str;
  if (isDate && isTime) {
    if (!GetDateTimeConnectorPattern(aLocale, aRetVal)) {
      NS_WARNING("failed to get date/time connector");
      aRetVal.AssignLiteral("{1} {0}");
    }
  } else if (!isDate && !isTime) {
    aRetVal.Truncate(0);
    return true;
  }

  if (isDate) {
    LCTYPE lcType = ToDateLCType(aDateStyle);
    size_t len = 0;
    if (len == 0) {
      return false;
    }

    // We're doing it to ensure the terminator will fit when Windows writes the
    // data to its output buffer. See bug 1358159 for details.
    str.SetLength(len);
    str.SetLength(len - 1);  // -1 because len counts the null terminator

    // Windows uses "ddd" and "dddd" for abbreviated and full day names
    // respectively,
    //   https://msdn.microsoft.com/en-us/library/windows/desktop/dd317787(v=vs.85).aspx
    // but in a CLDR/ICU-style pattern these should be "EEE" and "EEEE".
    //   http://userguide.icu-project.org/formatparse/datetime
    // So we fix that up here.
    nsAString::const_iterator start, pos, end;
    start = str.BeginReading(pos);
    str.EndReading(end);
    if (FindInReadable(u"dddd"_ns, pos, end)) {
      str.ReplaceLiteral(pos - start, 4, u"EEEE");
    } else {
      pos = start;
      if (FindInReadable(u"ddd"_ns, pos, end)) {
        str.ReplaceLiteral(pos - start, 3, u"EEE");
      }
    }

    // Also, Windows uses lowercase "g" or "gg" for era, but ICU wants uppercase
    // "G" (it would interpret "g" as "modified Julian day"!). So fix that.
    int32_t index = str.FindChar('g');
    if (index >= 0) {
      str.Replace(index, 1, 'G');
      // If it was a double "gg", just drop the second one.
      index++;
      if (str.CharAt(index) == 'g') {
        str.Cut(index, 1);
      }
    }

    // If time was also requested, we need to substitute the date pattern from
    // Windows into the date+time format that we have in aRetVal.
    if (isTime) {
      nsACString::const_iterator start, pos, end;
      start = aRetVal.BeginReading(pos);
      aRetVal.EndReading(end);
      if (FindInReadable("{1}"_ns, pos, end)) {
        aRetVal.Replace(pos - start, 3, NS_ConvertUTF16toUTF8(str));
      }
    } else {
      aRetVal = NS_ConvertUTF16toUTF8(str);
    }
  }

  if (isTime) {
    LCTYPE lcType = ToTimeLCType(aTimeStyle);
    size_t len = 0;
    if (len == 0) {
      return false;
    }

    // We're doing it to ensure the terminator will fit when Windows writes the
    // data to its output buffer. See bug 1358159 for details.
    str.SetLength(len);
    str.SetLength(len - 1);

    // Windows uses "t" or "tt" for a "time marker" (am/pm indicator),
    //   https://msdn.microsoft.com/en-us/library/windows/desktop/dd318148(v=vs.85).aspx
    // but in a CLDR/ICU-style pattern that should be "a".
    //   http://userguide.icu-project.org/formatparse/datetime
    // So we fix that up here.
    int32_t index = str.FindChar('t');
    if (index >= 0) {
      str.Replace(index, 1, 'a');
      index++;
      if (str.CharAt(index) == 't') {
        str.Cut(index, 1);
      }
    }

    if (isDate) {
      nsACString::const_iterator start, pos, end;
      start = aRetVal.BeginReading(pos);
      aRetVal.EndReading(end);
      if (FindInReadable("{0}"_ns, pos, end)) {
        aRetVal.Replace(pos - start, 3, NS_ConvertUTF16toUTF8(str));
      }
    } else {
      aRetVal = NS_ConvertUTF16toUTF8(str);
    }
  }

  return true;
}

void OSPreferences::RemoveObservers() {}
