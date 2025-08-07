#pragma once

#include <QRect>
#include <QSettings>
#include <QVariant>

#include <fdtviewer/types.hpp>

using settings = QSettings;

template <typename type>
class settings_property {
public:
    settings_property(const QString &name, type &&value = {})
            : m_name(name) {
        if (!m_settings.contains(name))
            set(std::forward<type>(value));
    }

    auto set(const type &value) noexcept -> void {
        m_settings.setValue(m_name, value);
    }

    auto value() const noexcept -> type {
        return m_settings.value(m_name).value<type>();
    }

private:
    settings m_settings;
    const QString m_name;
};

class viewer_settings {
public:
    viewer_settings() = default;

    settings_property<bool> view_word_wrap{"view/word_wrap", true};
    settings_property<bool> window_show_fullscreen{"window/fullscreen", false};
    settings_property<QRect> window_position{"window/position", {}};

    // minor dark theme fixes
    settings_property<bool> view_darkstyle{"view/darkstyle", false};
    // allow auto opening of last loaded files
    settings_property<bool> view_autoopen_lastloaded{"view/autoopen_last_loaded", false};
    // list of last loaded files
    settings_property<QStringList> view_last_loaded{"view/last_loaded", QStringList()};
    // allow exit when pressing escape key
    settings_property<bool> window_escape_exit{"window/escape_exit", false};
};
