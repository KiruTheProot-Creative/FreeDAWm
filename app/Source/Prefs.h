#pragma once

#include "Common.h"

/** User preferences: typed access with defaults, persisted in the app's settings file.
    Every change is broadcast, so the UI and AppState can react immediately. */
class Prefs : public juce::ChangeBroadcaster {
public:
    explicit Prefs(juce::PropertiesFile* file) : file_(file) {}

    juce::var get(const juce::String& key) const {
        if (file_ != nullptr && file_->containsKey("pref." + key)) {
            const auto& def = defaultFor(key);
            const juce::String raw = file_->getValue("pref." + key);
            if (def.isBool()) return raw == "1" || raw == "true";
            if (def.isInt() || def.isInt64()) return raw.getIntValue();
            if (def.isDouble()) return raw.getDoubleValue();
            return raw;
        }
        return defaultFor(key);
    }

    int          getInt(const juce::String& key) const    { return static_cast<int>(get(key)); }
    bool         getBool(const juce::String& key) const   { return static_cast<bool>(get(key)); }
    double       getDouble(const juce::String& key) const { return static_cast<double>(get(key)); }
    juce::String getString(const juce::String& key) const { return get(key).toString(); }

    void set(const juce::String& key, const juce::var& value) {
        if (file_ == nullptr) return;
        const juce::String text = value.isBool() ? juce::String(static_cast<bool>(value) ? "1" : "0") : value.toString();
        if (file_->getValue("pref." + key) == text && file_->containsKey("pref." + key)) return;
        file_->setValue("pref." + key, text);
        file_->saveIfNeeded();
        sendChangeMessage();
    }

    void reset(const juce::String& key) {
        if (file_ == nullptr) return;
        file_->removeValue("pref." + key);
        file_->saveIfNeeded();
        sendChangeMessage();
    }

    void changed() { sendChangeMessage(); }

    // ---- defaults ----------------------------------------------------------------------
    static const juce::var& defaultFor(const juce::String& key) {
        static const std::map<juce::String, juce::var> defaults = {
            // General
            {"startup", 1},                   // 0 last project, 1 demo, 2 empty
            {"projectFolder", ""},            // "" = Documents/FreeDAWm
            {"autosaveMinutes", 5},           // 0 = off
            {"backups", 3},
            {"lastProject", ""},
            {"undoSteps", 200},
            {"checkUpdates", false},
            {"crashReports", false},
            // Audio
            {"latencyOffsetMs", 0.0},
            {"cpuMeter", 0},                  // 0 percent, 1 bar, 2 hidden
            {"multicore", false},
            // MIDI
            {"midiClockSend", false},
            {"midiClockReceive", false},
            {"keyboardPiano", true},
            {"keyboardOctave", 4},
            {"keyboardVelocity", 100},
            // Plugins
            {"scanOnStartup", false},
            {"pluginWindowsOnTop", true},
            {"openEditorOnLoad", false},
            // Library
            {"previewGainDb", -6.0},
            {"autoPreview", true},
            {"loopPreview", false},
            // Recording
            {"recordFormat", 0},
            {"recordBitDepth", 1},
            {"countInBars", 0},
            // Export
            {"exportFormatType", 0},
            {"exportBitDepth", 1},            // 0 = 16, 1 = 24, 2 = 32 float
            {"exportSampleRate", 0},          // 0 = audio device rate, then 44.1k, 48k, 88.2k, 96k
            {"exportTail", 2.0},
            {"exportNormalize", false},
            {"exportNormalizeDb", -0.3},
            {"exportDither", true},
            {"exportFolder", ""},             // "" = project folder
            {"exportAsk", true},
            // Editing
            {"timelineSnap", 0},              // 0 adaptive, 1 bar, 2 beat, 3 1/8, 4 1/16, 5 off
            {"followPlayhead", true},
            {"wheelZoom", 0},                 // 0 Ctrl+wheel zooms, 1 wheel zooms
            {"defaultClipBars", 0},           // index: bars - 1
            {"defaultVelocity", 100},
            {"clipNaming", 0},                // 0 "Clip N", 1 track name, 2 track name + N
            {"audioFadeMs", 1.5},
            {"tempoFollow", 0},
            // Appearance
            {"accent", 0},
            {"contrast", 0},
            {"uiScale", 2},                   // index: 80 90 100 110 125 150 175 200 %
            {"trackHeight", 1},               // 0 small, 1 medium, 2 large
            {"peakHold", true},
            {"meterColours", 0},
            {"tooltips", true},
        };
        static const juce::var none;
        auto it = defaults.find(key);
        return it != defaults.end() ? it->second : none;
    }

    // ---- folders ------------------------------------------------------------------------
    juce::File projectFolder() const {
        const auto custom = getString("projectFolder");
        if (custom.isNotEmpty() && juce::File::isAbsolutePath(custom)) return juce::File(custom);
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("FreeDAWm");
    }
    juce::File exportFolder() const {
        const auto custom = getString("exportFolder");
        if (custom.isNotEmpty() && juce::File::isAbsolutePath(custom)) return juce::File(custom);
        return projectFolder();
    }

    // ---- keyboard shortcuts -------------------------------------------------------------
    struct Action { const char* id; const char* name; const char* defaultKey; };

    static const std::vector<Action>& actions() {
        static const std::vector<Action> list = {
            {"playStop",      "Play / stop",                 "spacebar"},
            {"toStart",       "Go to start",                 "home"},
            {"loop",          "Loop on / off",               "L"},
            {"newProject",    "New project",                 "command + N"},
            {"openProject",   "Open project",                "command + O"},
            {"save",          "Save",                        "command + S"},
            {"saveAs",        "Save as",                     "command + shift + S"},
            {"export",        "Export audio",                "command + E"},
            {"preferences",   "Preferences",                 "command + ,"},
            {"addAudioTrack", "Add audio track",             "command + T"},
            {"addMidiTrack",  "Add MIDI track",              "command + shift + T"},
            {"duplicate",     "Duplicate clip",              "command + D"},
            {"delete",        "Delete clip",                 "delete"},
            {"zoomIn",        "Zoom in",                     "command + ="},
            {"zoomOut",       "Zoom out",                    "command + -"},
            {"showPianoRoll", "Show piano roll",             "command + 1"},
            {"showFx",        "Show FX chain",               "command + 2"},
            {"showMixer",     "Show mixer",                  "command + 3"},
            {"octaveDown",    "Keyboard piano: octave down", "Z"},
            {"octaveUp",      "Keyboard piano: octave up",   "X"},
        };
        return list;
    }

    juce::KeyPress shortcut(const juce::String& id) const {
        juce::String text;
        if (file_ != nullptr && file_->containsKey("key." + id)) text = file_->getValue("key." + id);
        else
            for (auto& a : actions()) if (id == a.id) text = a.defaultKey;
        return text.isEmpty() ? juce::KeyPress() : juce::KeyPress::createFromDescription(text);
    }

    /** Sets a shortcut; any other action that used the same key loses it. Returns that action's name, if any. */
    juce::String setShortcut(const juce::String& id, const juce::KeyPress& key) {
        juce::String clashed;
        if (key.isValid())
            for (auto& a : actions())
                if (id != a.id && shortcut(a.id) == key) { storeKey(a.id, {}); clashed = a.name; }
        storeKey(id, key);
        sendChangeMessage();
        return clashed;
    }

    void resetShortcuts() {
        if (file_ == nullptr) return;
        for (auto& a : actions()) file_->removeValue(juce::String("key.") + a.id);
        file_->saveIfNeeded();
        sendChangeMessage();
    }

    /** The action bound to a key press, or an empty string. */
    juce::String actionFor(const juce::KeyPress& key) const {
        for (auto& a : actions())
            if (const auto k = shortcut(a.id); k.isValid() && k == key) return a.id;
        return {};
    }

private:
    void storeKey(const juce::String& id, const juce::KeyPress& key) {
        if (file_ == nullptr) return;
        file_->setValue("key." + id, key.isValid() ? key.getTextDescription() : juce::String());
        file_->saveIfNeeded();
    }

    juce::PropertiesFile* file_;
};
